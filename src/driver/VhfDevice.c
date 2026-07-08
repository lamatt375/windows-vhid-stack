#include "VhfDevice.h"
#include "../shared/VhidHidDescriptor.h"
#include "Trace.h"

#define VHID_STATUS_VHF_CREATE_FAILED STATUS_DEVICE_NOT_READY
#define VHID_STATUS_VHF_START_FAILED STATUS_DEVICE_POWER_FAILURE
#define VHID_KEYBOARD_REPORT_ID ((UCHAR)0x01)
#define VHID_MOUSE_REPORT_ID ((UCHAR)0x02)
#define VHID_KEYBOARD_INPUT_REPORT_LENGTH 9u
#define VHID_MOUSE_INPUT_REPORT_LENGTH 4u

static UCHAR VhidNeutralKeyboardReport[VHID_KEYBOARD_INPUT_REPORT_LENGTH] = {
    VHID_KEYBOARD_REPORT_ID, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static UCHAR VhidNeutralMouseReport[VHID_MOUSE_INPUT_REPORT_LENGTH] = {
    VHID_MOUSE_REPORT_ID, 0x00, 0x00, 0x00
};

C_ASSERT(sizeof(VhidNeutralKeyboardReport) == VHID_KEYBOARD_INPUT_REPORT_LENGTH);
C_ASSERT(sizeof(VhidNeutralMouseReport) == VHID_MOUSE_INPUT_REPORT_LENGTH);

static
BOOLEAN
VhidIsNeutralReport(
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength,
    _In_ UCHAR ExpectedReportId,
    _In_ ULONG ExpectedReportLength
    )
{
    ULONG index;

    if (Report == NULL || ReportLength != ExpectedReportLength) {
        return FALSE;
    }

    if (Report[0] != ExpectedReportId) {
        return FALSE;
    }

    for (index = 1; index < ReportLength; index++) {
        if (Report[index] != 0x00) {
            return FALSE;
        }
    }

    return TRUE;
}

static
VOID
VhidVhfDisableNeutralSubmission(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ VHID_NEUTRAL_REPORT_STATE State,
    _In_ NTSTATUS Status
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    Context->LastReportSubmitStatus = Status;
    Context->ReportSubmissionEnabled = FALSE;
    InterlockedExchange(&Context->NeutralReportState, (LONG)State);
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
}

static
VOID
VhidVhfArmNeutralSubmission(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    Context->LastReportSubmitStatus = STATUS_SUCCESS;
    Context->Deleting = FALSE;
    InterlockedExchange(
        &Context->NeutralReportState,
        (LONG)VhidNeutralKeyboardPending);
    Context->ReportSubmissionEnabled = TRUE;
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
}

static
BOOLEAN
VhidVhfTryAcquireSubmit(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _Out_ VHFHANDLE* VhfHandle,
    _Out_ PBOOLEAN DeleteInProgress
    )
{
    KIRQL oldIrql;
    BOOLEAN acquired;

    *VhfHandle = NULL;
    *DeleteInProgress = FALSE;
    acquired = FALSE;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    if (Context->Deleting) {
        *DeleteInProgress = TRUE;
    } else if (Context->ReportSubmissionEnabled &&
               Context->VhfStarted &&
               Context->VhfHandle != NULL) {
        Context->ActiveSubmissions++;
        KeClearEvent(&Context->NoActiveSubmissionsEvent);
        *VhfHandle = Context->VhfHandle;
        acquired = TRUE;
    }

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    return acquired;
}

static
VOID
VhidVhfCompleteSubmit(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ NTSTATUS Status,
    _In_ VHID_NEUTRAL_REPORT_STATE SuccessState,
    _In_ BOOLEAN DisableOnSuccess
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    Context->LastReportSubmitStatus = Status;

    if (!NT_SUCCESS(Status)) {
        Context->ReportSubmissionEnabled = FALSE;
        InterlockedExchange(
            &Context->NeutralReportState,
            (LONG)VhidNeutralReportFailed);
    } else if (!Context->Deleting) {
        if (DisableOnSuccess) {
            Context->ReportSubmissionEnabled = FALSE;
        }

        InterlockedExchange(
            &Context->NeutralReportState,
            (LONG)SuccessState);
    }

    if (Context->ActiveSubmissions > 0) {
        Context->ActiveSubmissions--;
    }

    if (Context->ActiveSubmissions == 0) {
        KeSetEvent(&Context->NoActiveSubmissionsEvent, IO_NO_INCREMENT, FALSE);
    }

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
}

static
NTSTATUS
VhidVhfSubmitNeutralReport(
    _In_ VHFHANDLE VhfHandle,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength,
    _In_ UCHAR ReportId
    )
{
    HID_XFER_PACKET packet;

    if (VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (ReportId == VHID_KEYBOARD_REPORT_ID) {
        if (!VhidIsNeutralReport(
                Report,
                ReportLength,
                VHID_KEYBOARD_REPORT_ID,
                VHID_KEYBOARD_INPUT_REPORT_LENGTH)) {
            return STATUS_INVALID_PARAMETER;
        }
    } else if (ReportId == VHID_MOUSE_REPORT_ID) {
        if (!VhidIsNeutralReport(
                Report,
                ReportLength,
                VHID_MOUSE_REPORT_ID,
                VHID_MOUSE_INPUT_REPORT_LENGTH)) {
            return STATUS_INVALID_PARAMETER;
        }
    } else {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = Report;
    packet.reportBufferLen = ReportLength;
    packet.reportId = ReportId;

    return VhfReadReportSubmit(VhfHandle, &packet);
}

VOID
VhidVhfContextInit(
    _Out_ PVHID_VHF_CONTEXT Context
    )
{
    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeSpinLock(&Context->SubmissionLock);
    KeInitializeEvent(
        &Context->NoActiveSubmissionsEvent,
        NotificationEvent,
        TRUE);
    Context->NeutralReportState = (LONG)VhidNeutralReportDisabled;
    Context->LastReportSubmitStatus = STATUS_SUCCESS;
}

NTSTATUS
VhidVhfInitialize(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS status;
    VHF_CONFIG config;

    VHF_CONFIG_INIT(
        &config,
        WdfDeviceWdmGetDeviceObject(Device),
        (USHORT)VHID_HID_REPORT_DESCRIPTOR_LENGTH,
        (PUCHAR)VhidHidReportDescriptor);

    config.VhfClientContext = Context;
    config.EvtVhfReadyForNextReadReport = VhidEvtVhfReadyForNextReadReport;

    VhidVhfDisableNeutralSubmission(
        Context,
        VhidNeutralReportDisabled,
        STATUS_SUCCESS);

    status = VhfCreate(&config, &Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "VhfCreate failed, status=0x%08X",
            status);
        return VHID_STATUS_VHF_CREATE_FAILED;
    }

    Context->VhfCreated = TRUE;

    status = VhfStart(Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "VhfStart failed, status=0x%08X",
            status);
        VhfDelete(Context->VhfHandle, TRUE);
        VhidVhfContextInit(Context);
        return VHID_STATUS_VHF_START_FAILED;
    }

    Context->VhfStarted = TRUE;
    Context->Initialized = TRUE;
    VhidVhfArmNeutralSubmission(Context);

    return status;
}

VOID
VhidEvtVhfReadyForNextReadReport(
    _In_ PVOID VhfClientContext
    )
{
    PVHID_VHF_CONTEXT context;
    VHFHANDLE vhfHandle;
    BOOLEAN deleteInProgress;
    LONG previousState;
    NTSTATUS status;

    context = (PVHID_VHF_CONTEXT)VhfClientContext;
    if (context == NULL) {
        return;
    }

    if (!context->ReportSubmissionEnabled) {
        return;
    }

    previousState = InterlockedCompareExchange(
        &context->NeutralReportState,
        (LONG)VhidNeutralKeyboardSubmitting,
        (LONG)VhidNeutralKeyboardPending);

    if (previousState == (LONG)VhidNeutralKeyboardPending) {
        if (!VhidVhfTryAcquireSubmit(context, &vhfHandle, &deleteInProgress)) {
            if (!deleteInProgress) {
                VhidVhfDisableNeutralSubmission(
                    context,
                    VhidNeutralReportFailed,
                    STATUS_DEVICE_NOT_READY);
            }
            return;
        }

        status = VhidVhfSubmitNeutralReport(
            vhfHandle,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport),
            VHID_KEYBOARD_REPORT_ID);

        VhidVhfCompleteSubmit(
            context,
            status,
            VhidNeutralMousePending,
            FALSE);

        if (!NT_SUCCESS(status)) {
            VHID_LOG_ERROR(
                "Neutral keyboard report submit failed, status=0x%08X",
                status);
            return;
        }

        VHID_LOG_INFO(
            "Neutral keyboard report submitted, reportId=%u",
            VHID_KEYBOARD_REPORT_ID);
        return;
    }

    previousState = InterlockedCompareExchange(
        &context->NeutralReportState,
        (LONG)VhidNeutralMouseSubmitting,
        (LONG)VhidNeutralMousePending);

    if (previousState == (LONG)VhidNeutralMousePending) {
        if (!VhidVhfTryAcquireSubmit(context, &vhfHandle, &deleteInProgress)) {
            if (!deleteInProgress) {
                VhidVhfDisableNeutralSubmission(
                    context,
                    VhidNeutralReportFailed,
                    STATUS_DEVICE_NOT_READY);
            }
            return;
        }

        status = VhidVhfSubmitNeutralReport(
            vhfHandle,
            VhidNeutralMouseReport,
            sizeof(VhidNeutralMouseReport),
            VHID_MOUSE_REPORT_ID);

        VhidVhfCompleteSubmit(
            context,
            status,
            VhidNeutralReportComplete,
            TRUE);

        if (!NT_SUCCESS(status)) {
            VHID_LOG_ERROR(
                "Neutral mouse report submit failed, status=0x%08X",
                status);
            return;
        }

        VHID_LOG_INFO(
            "Neutral mouse report submitted, reportId=%u",
            VHID_MOUSE_REPORT_ID);
    }
}

VOID
VhidVhfCleanup(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    KIRQL oldIrql;
    BOOLEAN waitForSubmissions;
    VHFHANDLE vhfHandle;

    if (Context == NULL) {
        return;
    }

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    Context->Deleting = TRUE;
    Context->ReportSubmissionEnabled = FALSE;
    InterlockedExchange(
        &Context->NeutralReportState,
        (LONG)VhidNeutralReportDisabled);
    Context->VhfStarted = FALSE;
    waitForSubmissions = (Context->ActiveSubmissions > 0);
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (waitForSubmissions) {
        KeWaitForSingleObject(
            &Context->NoActiveSubmissionsEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL);
    }

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    vhfHandle = Context->VhfHandle;
    Context->VhfHandle = NULL;
    Context->ReportSubmissionEnabled = FALSE;
    InterlockedExchange(
        &Context->NeutralReportState,
        (LONG)VhidNeutralReportDisabled);
    Context->VhfCreated = FALSE;
    Context->Initialized = FALSE;
    Context->ActiveSubmissions = 0;
    KeSetEvent(&Context->NoActiveSubmissionsEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (vhfHandle != NULL) {
        VhfDelete(vhfHandle, TRUE);
    }
}
