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

static UCHAR VhidKeyboardAReport[VHID_KEYBOARD_INPUT_REPORT_LENGTH] = {
    VHID_KEYBOARD_REPORT_ID, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static UCHAR VhidNeutralMouseReport[VHID_MOUSE_INPUT_REPORT_LENGTH] = {
    VHID_MOUSE_REPORT_ID, 0x00, 0x00, 0x00
};

static UCHAR VhidMouseMoveRightReport[VHID_MOUSE_INPUT_REPORT_LENGTH] = {
    VHID_MOUSE_REPORT_ID, 0x00, 0x01, 0x00
};

C_ASSERT(sizeof(VhidNeutralKeyboardReport) == VHID_KEYBOARD_INPUT_REPORT_LENGTH);
C_ASSERT(sizeof(VhidKeyboardAReport) == VHID_KEYBOARD_INPUT_REPORT_LENGTH);
C_ASSERT(sizeof(VhidNeutralMouseReport) == VHID_MOUSE_INPUT_REPORT_LENGTH);
C_ASSERT(sizeof(VhidMouseMoveRightReport) == VHID_MOUSE_INPUT_REPORT_LENGTH);

static
BOOLEAN
VhidIsExpectedReport(
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength,
    _In_reads_bytes_(ExpectedReportLength) PUCHAR ExpectedReport,
    _In_ ULONG ExpectedReportLength
    )
{
    ULONG index;

    if (Report == NULL || ExpectedReport == NULL ||
        ReportLength != ExpectedReportLength) {
        return FALSE;
    }

    for (index = 0; index < ReportLength; index++) {
        if (Report[index] != ExpectedReport[index]) {
            return FALSE;
        }
    }

    return TRUE;
}

static
VOID
VhidVhfDisableReportSequence(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ VHID_REPORT_SEQUENCE_STATE State,
    _In_ NTSTATUS Status
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    Context->LastReportSubmitStatus = Status;
    Context->ReportSubmissionEnabled = FALSE;
    InterlockedExchange(&Context->ReportSequenceState, (LONG)State);
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
}

static
VOID
VhidVhfArmReportSequence(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    Context->LastReportSubmitStatus = STATUS_SUCCESS;
    Context->Deleting = FALSE;
    InterlockedExchange(
        &Context->ReportSequenceState,
        (LONG)VhidReportKeyboardPreClearPending);
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
    _In_ VHID_REPORT_SEQUENCE_STATE SuccessState,
    _In_ BOOLEAN DisableOnSuccess
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    Context->LastReportSubmitStatus = Status;

    if (!NT_SUCCESS(Status)) {
        Context->ReportSubmissionEnabled = FALSE;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportSequenceFailed);
    } else if (!Context->Deleting) {
        if (DisableOnSuccess) {
            Context->ReportSubmissionEnabled = FALSE;
        }

        InterlockedExchange(
            &Context->ReportSequenceState,
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
VhidVhfSubmitSequencedReport(
    _In_ VHFHANDLE VhfHandle,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength,
    _In_ UCHAR ReportId
    )
{
    HID_XFER_PACKET packet;
    BOOLEAN reportAllowed;

    if (VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    reportAllowed = FALSE;

    if (ReportId == VHID_KEYBOARD_REPORT_ID &&
        ReportLength == VHID_KEYBOARD_INPUT_REPORT_LENGTH) {
        reportAllowed = VhidIsExpectedReport(
                            Report,
                            ReportLength,
                            VhidNeutralKeyboardReport,
                            sizeof(VhidNeutralKeyboardReport)) ||
                        VhidIsExpectedReport(
                            Report,
                            ReportLength,
                            VhidKeyboardAReport,
                            sizeof(VhidKeyboardAReport));
    } else if (ReportId == VHID_MOUSE_REPORT_ID &&
               ReportLength == VHID_MOUSE_INPUT_REPORT_LENGTH) {
        reportAllowed = VhidIsExpectedReport(
                            Report,
                            ReportLength,
                            VhidNeutralMouseReport,
                            sizeof(VhidNeutralMouseReport)) ||
                        VhidIsExpectedReport(
                            Report,
                            ReportLength,
                            VhidMouseMoveRightReport,
                            sizeof(VhidMouseMoveRightReport));
    }

    if (!reportAllowed) {
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
    Context->ReportSequenceState = (LONG)VhidReportSequenceDisabled;
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

    VhidVhfDisableReportSequence(
        Context,
        VhidReportSequenceDisabled,
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
    VhidVhfArmReportSequence(Context);

    return status;
}

static
BOOLEAN
VhidVhfSubmitReadyReport(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ VHID_REPORT_SEQUENCE_STATE PendingState,
    _In_ VHID_REPORT_SEQUENCE_STATE SubmittingState,
    _In_ VHID_REPORT_SEQUENCE_STATE SuccessState,
    _In_ BOOLEAN DisableOnSuccess,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength,
    _In_ UCHAR ReportId,
    _In_z_ PCSTR StepName
    )
{
    VHFHANDLE vhfHandle;
    BOOLEAN deleteInProgress;
    LONG previousState;
    NTSTATUS status;

    previousState = InterlockedCompareExchange(
        &Context->ReportSequenceState,
        (LONG)SubmittingState,
        (LONG)PendingState);

    if (previousState != (LONG)PendingState) {
        return FALSE;
    }

    if (!VhidVhfTryAcquireSubmit(Context, &vhfHandle, &deleteInProgress)) {
        if (!deleteInProgress) {
            VhidVhfDisableReportSequence(
                Context,
                VhidReportSequenceFailed,
                STATUS_DEVICE_NOT_READY);
        }
        return TRUE;
    }

    status = VhidVhfSubmitSequencedReport(
        vhfHandle,
        Report,
        ReportLength,
        ReportId);

    VhidVhfCompleteSubmit(
        Context,
        status,
        SuccessState,
        DisableOnSuccess);

    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR("%s failed, status=0x%08X", StepName, status);
        return TRUE;
    }

    VHID_LOG_INFO("%s submitted, reportId=%u", StepName, ReportId);
    return TRUE;
}

VOID
VhidEvtVhfReadyForNextReadReport(
    _In_ PVOID VhfClientContext
    )
{
    PVHID_VHF_CONTEXT context;

    context = (PVHID_VHF_CONTEXT)VhfClientContext;
    if (context == NULL) {
        return;
    }

    if (!context->ReportSubmissionEnabled) {
        return;
    }

    if (VhidVhfSubmitReadyReport(
            context,
            VhidReportKeyboardPreClearPending,
            VhidReportKeyboardPreClearSubmitting,
            VhidReportMousePreClearPending,
            FALSE,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard pre-clear report")) {
        return;
    }

    if (VhidVhfSubmitReadyReport(
            context,
            VhidReportMousePreClearPending,
            VhidReportMousePreClearSubmitting,
            VhidReportKeyboardAPressPending,
            FALSE,
            VhidNeutralMouseReport,
            sizeof(VhidNeutralMouseReport),
            VHID_MOUSE_REPORT_ID,
            "Mouse pre-clear report")) {
        return;
    }

    if (VhidVhfSubmitReadyReport(
            context,
            VhidReportKeyboardAPressPending,
            VhidReportKeyboardAPressSubmitting,
            VhidReportKeyboardReleasePending,
            FALSE,
            VhidKeyboardAReport,
            sizeof(VhidKeyboardAReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard A report")) {
        return;
    }

    if (VhidVhfSubmitReadyReport(
            context,
            VhidReportKeyboardReleasePending,
            VhidReportKeyboardReleaseSubmitting,
            VhidReportMouseMoveRightPending,
            FALSE,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard release report")) {
        return;
    }

    if (VhidVhfSubmitReadyReport(
            context,
            VhidReportMouseMoveRightPending,
            VhidReportMouseMoveRightSubmitting,
            VhidReportMousePostClearPending,
            FALSE,
            VhidMouseMoveRightReport,
            sizeof(VhidMouseMoveRightReport),
            VHID_MOUSE_REPORT_ID,
            "Mouse move-right report")) {
        return;
    }

    if (VhidVhfSubmitReadyReport(
            context,
            VhidReportMousePostClearPending,
            VhidReportMousePostClearSubmitting,
            VhidReportKeyboardFinalClearPending,
            FALSE,
            VhidNeutralMouseReport,
            sizeof(VhidNeutralMouseReport),
            VHID_MOUSE_REPORT_ID,
            "Mouse post-clear report")) {
        return;
    }

    VhidVhfSubmitReadyReport(
        context,
        VhidReportKeyboardFinalClearPending,
        VhidReportKeyboardFinalClearSubmitting,
        VhidReportSequenceComplete,
        TRUE,
        VhidNeutralKeyboardReport,
        sizeof(VhidNeutralKeyboardReport),
        VHID_KEYBOARD_REPORT_ID,
        "Keyboard final-clear report");
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
        &Context->ReportSequenceState,
        (LONG)VhidReportSequenceDisabled);
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
        &Context->ReportSequenceState,
        (LONG)VhidReportSequenceDisabled);
    Context->VhfCreated = FALSE;
    Context->Initialized = FALSE;
    Context->ActiveSubmissions = 0;
    KeSetEvent(&Context->NoActiveSubmissionsEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (vhfHandle != NULL) {
        VhfDelete(vhfHandle, TRUE);
    }
}
