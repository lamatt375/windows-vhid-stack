#include "VhfDevice.h"
#include "../shared/VhidHidDescriptor.h"
#include "../shared/VhidProtocol.h"
#include "Trace.h"

#define VHID_STATUS_VHF_CREATE_FAILED STATUS_DEVICE_NOT_READY
#define VHID_STATUS_VHF_START_FAILED STATUS_DEVICE_POWER_FAILURE
#define VHID_KEYBOARD_REPORT_ID ((UCHAR)VHID_HID_REPORT_ID_KEYBOARD)
#define VHID_MOUSE_REPORT_ID ((UCHAR)VHID_HID_REPORT_ID_RELATIVE_MOUSE)
#define VHID_ABSOLUTE_MOUSE_REPORT_ID ((UCHAR)VHID_HID_REPORT_ID_ABSOLUTE_MOUSE)
#define VHID_KEYBOARD_INPUT_REPORT_LENGTH 9u
#define VHID_MOUSE_INPUT_REPORT_LENGTH 4u
#define VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH VHID_HID_ABSOLUTE_MOUSE_REPORT_LENGTH

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
C_ASSERT(VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH == 6u);
C_ASSERT(VHID_MOVE_ABSOLUTE_COORDINATE_MAX == VHID_HID_ABSOLUTE_COORDINATE_MAX);

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
USHORT
VhidReadUshortLittleEndian(
    _In_reads_bytes_(2) const UCHAR* Bytes
    )
{
    return (USHORT)(Bytes[0] | ((USHORT)Bytes[1] << 8));
}

static
BOOLEAN
VhidIsValidAbsoluteMoveReport(
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength
    )
{
    ULONG x;
    ULONG y;

    if (Report == NULL || ReportLength != VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH) {
        return FALSE;
    }

    if (Report[0] != VHID_ABSOLUTE_MOUSE_REPORT_ID || Report[1] != 0x00) {
        return FALSE;
    }

    x = VhidReadUshortLittleEndian(&Report[2]);
    y = VhidReadUshortLittleEndian(&Report[4]);

    return x <= VHID_HID_ABSOLUTE_COORDINATE_MAX &&
           y <= VHID_HID_ABSOLUTE_COORDINATE_MAX;
}

static
VOID
VhidBuildAbsoluteMoveReport(
    _Out_writes_bytes_(VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH) UCHAR* Report,
    _In_ ULONG X,
    _In_ ULONG Y
    )
{
    Report[0] = VHID_ABSOLUTE_MOUSE_REPORT_ID;
    Report[1] = 0x00;
    Report[2] = (UCHAR)(X & 0xFFu);
    Report[3] = (UCHAR)((X >> 8) & 0xFFu);
    Report[4] = (UCHAR)(Y & 0xFFu);
    Report[5] = (UCHAR)((Y >> 8) & 0xFFu);
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
    Context->CurrentCommandType = VHID_COMMAND_NONE;
    Context->CurrentCommandSequenceId = 0;
    InterlockedExchange(&Context->ReportSequenceState, (LONG)State);
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
}

static
VHID_REPORT_SEQUENCE_STATE
VhidVhfIdleState(
    _In_ PVHID_VHF_CONTEXT Context
    )
{
    return Context->SmokeSequenceCompleted ?
        VhidReportSequenceComplete :
        VhidReportSequenceDisabled;
}

static
BOOLEAN
VhidVhfTryBeginReadySubmit(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ VHID_REPORT_SEQUENCE_STATE PendingState,
    _In_ VHID_REPORT_SEQUENCE_STATE SubmittingState,
    _Out_ VHFHANDLE* VhfHandle,
    _Out_ NTSTATUS* BeginStatus
    )
{
    KIRQL oldIrql;
    BOOLEAN acquired;

    *VhfHandle = NULL;
    *BeginStatus = STATUS_SUCCESS;
    acquired = FALSE;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    if (Context->ReportSequenceState == (LONG)PendingState) {
        if (Context->Deleting) {
            *BeginStatus = STATUS_DELETE_PENDING;
        } else if (!Context->ReportSubmissionEnabled ||
                   !Context->VhfStarted ||
                   Context->VhfHandle == NULL) {
            *BeginStatus = STATUS_DEVICE_NOT_READY;
        } else if (!Context->ReadyForNextReport) {
            *BeginStatus = STATUS_SUCCESS;
        } else {
            Context->ReadyForNextReport = FALSE;
            InterlockedExchange(
                &Context->ReportSequenceState,
                (LONG)SubmittingState);
            Context->ActiveSubmissions++;
            KeClearEvent(&Context->NoActiveSubmissionsEvent);
            *VhfHandle = Context->VhfHandle;
            acquired = TRUE;
        }
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
    _In_ BOOLEAN DisableOnSuccess,
    _In_ BOOLEAN UpdateTriggerStatus
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    Context->LastReportSubmitStatus = Status;
    if (Context->CurrentCommandType != VHID_COMMAND_NONE) {
        Context->LastCommandStatus = Status;
    }
    if (UpdateTriggerStatus) {
        Context->LastTriggerStatus = Status;
    }

    if (!NT_SUCCESS(Status)) {
        Context->ReportSubmissionEnabled = FALSE;
        Context->CurrentCommandType = VHID_COMMAND_NONE;
        Context->CurrentCommandSequenceId = 0;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportSequenceFailed);
    } else if (!Context->Deleting) {
        if (DisableOnSuccess) {
            Context->ReportSubmissionEnabled = FALSE;
            if (SuccessState == VhidReportSequenceComplete) {
                Context->SmokeSequenceCompleted = TRUE;
            }
            Context->CurrentCommandType = VHID_COMMAND_NONE;
            Context->CurrentCommandSequenceId = 0;
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
VOID
VhidVhfCompleteMoveAbsolute(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ NTSTATUS Status
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    Context->LastReportSubmitStatus = Status;
    Context->LastCommandStatus = Status;
    Context->ReportSubmissionEnabled = FALSE;
    Context->CurrentCommandType = VHID_COMMAND_NONE;
    Context->CurrentCommandSequenceId = 0;

    InterlockedExchange(
        &Context->ReportSequenceState,
        NT_SUCCESS(Status) ?
            (LONG)VhidVhfIdleState(Context) :
            (LONG)VhidReportSequenceFailed);

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

static
NTSTATUS
VhidVhfSubmitAbsoluteMoveReport(
    _In_ VHFHANDLE VhfHandle,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength
    )
{
    HID_XFER_PACKET packet;

    if (VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (!VhidIsValidAbsoluteMoveReport(Report, ReportLength)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = Report;
    packet.reportBufferLen = ReportLength;
    packet.reportId = VHID_ABSOLUTE_MOUSE_REPORT_ID;

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
    Context->ReadyForNextReport = FALSE;
    Context->ReportSequenceState = (LONG)VhidReportSequenceDisabled;
    Context->LastReportSubmitStatus = STATUS_SUCCESS;
    Context->LastTriggerStatus = STATUS_SUCCESS;
    Context->LastCommandStatus = STATUS_SUCCESS;
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

    return status;
}

static
VOID
VhidVhfMarkReadyForNextReport(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    if (!Context->Deleting &&
        Context->VhfStarted &&
        Context->VhfHandle != NULL) {
        Context->ReadyForNextReport = TRUE;
    }
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
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
    NTSTATUS status;

    if (!VhidVhfTryBeginReadySubmit(
            Context,
            PendingState,
            SubmittingState,
            &vhfHandle,
            &status)) {
        if (!NT_SUCCESS(status)) {
            VhidVhfDisableReportSequence(
                Context,
                VhidReportSequenceFailed,
                status);
            return TRUE;
        }

        return FALSE;
    }

    VHID_LOG_INFO("%s submit attempt, reportId=%u", StepName, ReportId);

    status = VhidVhfSubmitSequencedReport(
        vhfHandle,
        Report,
        ReportLength,
        ReportId);

    VhidVhfCompleteSubmit(
        Context,
        status,
        SuccessState,
        DisableOnSuccess,
        FALSE);

    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR("%s failed, status=0x%08X", StepName, status);
        return TRUE;
    }

    VHID_LOG_INFO("%s submitted, reportId=%u", StepName, ReportId);
    if (DisableOnSuccess) {
        VHID_LOG_INFO("%s", "Smoke sequence completed");
    }

    return TRUE;
}

static
BOOLEAN
VhidVhfSubmitNextReadyReport(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    LONG state;

    state = Context->ReportSequenceState;

    switch (state) {
    case VhidReportKeyboardPreClearPending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportKeyboardPreClearPending,
            VhidReportKeyboardPreClearSubmitting,
            VhidReportMousePreClearPending,
            FALSE,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard neutral pre-clear report");

    case VhidReportMousePreClearPending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportMousePreClearPending,
            VhidReportMousePreClearSubmitting,
            VhidReportKeyboardAPressPending,
            FALSE,
            VhidNeutralMouseReport,
            sizeof(VhidNeutralMouseReport),
            VHID_MOUSE_REPORT_ID,
            "Mouse neutral pre-clear report");

    case VhidReportKeyboardAPressPending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportKeyboardAPressPending,
            VhidReportKeyboardAPressSubmitting,
            VhidReportKeyboardReleasePending,
            FALSE,
            VhidKeyboardAReport,
            sizeof(VhidKeyboardAReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard A press report");

    case VhidReportKeyboardReleasePending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportKeyboardReleasePending,
            VhidReportKeyboardReleaseSubmitting,
            VhidReportMouseMoveRightPending,
            FALSE,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard release report");

    case VhidReportMouseMoveRightPending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportMouseMoveRightPending,
            VhidReportMouseMoveRightSubmitting,
            VhidReportMousePostClearPending,
            FALSE,
            VhidMouseMoveRightReport,
            sizeof(VhidMouseMoveRightReport),
            VHID_MOUSE_REPORT_ID,
            "Mouse X+1 report");

    case VhidReportMousePostClearPending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportMousePostClearPending,
            VhidReportMousePostClearSubmitting,
            VhidReportKeyboardFinalClearPending,
            FALSE,
            VhidNeutralMouseReport,
            sizeof(VhidNeutralMouseReport),
            VHID_MOUSE_REPORT_ID,
            "Mouse neutral post-clear report");

    case VhidReportKeyboardFinalClearPending:
        return VhidVhfSubmitReadyReport(
            Context,
            VhidReportKeyboardFinalClearPending,
            VhidReportKeyboardFinalClearSubmitting,
            VhidReportSequenceComplete,
            TRUE,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport),
            VHID_KEYBOARD_REPORT_ID,
            "Keyboard final neutral report");

    default:
        return FALSE;
    }
}

static
VOID
VhidVhfDrainReadyReports(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    while (VhidVhfSubmitNextReadyReport(Context)) {
        ;
    }
}

NTSTATUS
VhidVhfTriggerSmokeSequence(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    KIRQL oldIrql;
    LONG state;
    NTSTATUS status;
    VHFHANDLE vhfHandle;
    BOOLEAN submitKickstart;

    if (Context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    vhfHandle = NULL;
    submitKickstart = FALSE;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    state = Context->ReportSequenceState;

    if (Context->Deleting) {
        status = STATUS_DELETE_PENDING;
        VHID_LOG_ERROR(
            "Smoke trigger rejected, deleting status=0x%08X",
            status);
    } else if (!Context->VhfStarted || Context->VhfHandle == NULL) {
        status = STATUS_DEVICE_NOT_READY;
        VHID_LOG_ERROR(
            "Smoke trigger rejected, VHF not ready status=0x%08X",
            status);
    } else if (Context->ReportSubmissionEnabled || Context->ActiveSubmissions > 0) {
        status = STATUS_DEVICE_BUSY;
        VHID_LOG_ERROR(
            "Smoke trigger rejected, busy/running status=0x%08X",
            status);
    } else if (Context->SmokeSequenceCompleted) {
        status = STATUS_ALREADY_COMMITTED;
        VHID_LOG_ERROR(
            "Smoke trigger rejected, already completed status=0x%08X",
            status);
    } else if (state != (LONG)VhidReportSequenceDisabled) {
        status = STATUS_INVALID_DEVICE_STATE;
        VHID_LOG_ERROR(
            "Smoke trigger rejected, invalid state=%ld status=0x%08X",
            state,
            status);
    } else {
        Context->LastReportSubmitStatus = STATUS_SUCCESS;
        Context->LastCommandType = VHID_COMMAND_SMOKE_SEQUENCE;
        Context->LastCommandSequenceId = 0;
        Context->LastCommandStatus = STATUS_PENDING;
        Context->CurrentCommandType = VHID_COMMAND_SMOKE_SEQUENCE;
        Context->CurrentCommandSequenceId = 0;
        Context->ReportSubmissionEnabled = TRUE;
        Context->ReadyForNextReport = FALSE;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportKeyboardPreClearSubmitting);
        Context->ActiveSubmissions++;
        KeClearEvent(&Context->NoActiveSubmissionsEvent);
        vhfHandle = Context->VhfHandle;
        submitKickstart = TRUE;
        status = STATUS_SUCCESS;
        VHID_LOG_INFO(
            "%s",
            "Smoke trigger armed, kickstarting first report");
    }

    Context->LastTriggerStatus = status;

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (!submitKickstart) {
        return status;
    }

    VHID_LOG_INFO(
        "Keyboard neutral pre-clear report kickstart attempt, reportId=%u",
        VHID_KEYBOARD_REPORT_ID);

    status = VhidVhfSubmitSequencedReport(
        vhfHandle,
        VhidNeutralKeyboardReport,
        sizeof(VhidNeutralKeyboardReport),
        VHID_KEYBOARD_REPORT_ID);

    VhidVhfCompleteSubmit(
        Context,
        status,
        VhidReportMousePreClearPending,
        FALSE,
        TRUE);

    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "Keyboard neutral pre-clear report kickstart failed, status=0x%08X",
            status);
        return status;
    }

    VHID_LOG_INFO(
        "Keyboard neutral pre-clear report kickstart submitted, reportId=%u",
        VHID_KEYBOARD_REPORT_ID);

    VhidVhfDrainReadyReports(Context);

    return STATUS_SUCCESS;
}

static
NTSTATUS
VhidValidateMoveAbsoluteRequest(
    _In_ const VHID_MOVE_ABSOLUTE_REQUEST* Request
    )
{
    if (Request == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->Size != sizeof(*Request) ||
        Request->ProtocolVersionMajor != VHID_PROTOCOL_VERSION_MAJOR ||
        Request->ProtocolVersionMinor != VHID_PROTOCOL_VERSION_MINOR ||
        Request->CommandType != VHID_COMMAND_MOVE_ABSOLUTE) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->Reserved0 != 0 ||
        Request->Reserved1 != 0 ||
        Request->Reserved2 != 0 ||
        Request->Reserved3 != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->X > VHID_MOVE_ABSOLUTE_COORDINATE_MAX ||
        Request->Y > VHID_MOVE_ABSOLUTE_COORDINATE_MAX) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
VhidVhfMoveAbsolute(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ const VHID_MOVE_ABSOLUTE_REQUEST* Request
    )
{
    KIRQL oldIrql;
    LONG state;
    NTSTATUS status;
    VHFHANDLE vhfHandle;
    BOOLEAN submitReport;

    if (Context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = VhidValidateMoveAbsoluteRequest(Request);
    if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
        Context->LastCommandType = (Request != NULL) ? Request->CommandType : VHID_COMMAND_MOVE_ABSOLUTE;
        Context->LastCommandSequenceId = (Request != NULL) ? Request->SequenceId : 0;
        Context->LastCommandStatus = status;
        KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
        VHID_LOG_ERROR(
            "MoveAbsolute rejected, invalid request status=0x%08X",
            status);
        return status;
    }

    vhfHandle = NULL;
    submitReport = FALSE;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    state = Context->ReportSequenceState;

    if (Context->Deleting) {
        status = STATUS_DELETE_PENDING;
    } else if (!Context->VhfStarted || Context->VhfHandle == NULL) {
        status = STATUS_DEVICE_NOT_READY;
    } else if (Context->ReportSubmissionEnabled || Context->ActiveSubmissions > 0) {
        status = STATUS_DEVICE_BUSY;
    } else if (state != (LONG)VhidReportSequenceDisabled &&
               state != (LONG)VhidReportSequenceComplete) {
        status = STATUS_INVALID_DEVICE_STATE;
    } else {
        VhidBuildAbsoluteMoveReport(
            Context->AbsoluteMoveReport,
            Request->X,
            Request->Y);
        Context->LastReportSubmitStatus = STATUS_SUCCESS;
        Context->LastCommandType = VHID_COMMAND_MOVE_ABSOLUTE;
        Context->LastCommandSequenceId = Request->SequenceId;
        Context->LastCommandStatus = STATUS_PENDING;
        Context->CurrentCommandType = VHID_COMMAND_MOVE_ABSOLUTE;
        Context->CurrentCommandSequenceId = Request->SequenceId;
        Context->ReportSubmissionEnabled = TRUE;
        Context->ReadyForNextReport = FALSE;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportMoveAbsoluteSubmitting);
        Context->ActiveSubmissions++;
        KeClearEvent(&Context->NoActiveSubmissionsEvent);
        vhfHandle = Context->VhfHandle;
        submitReport = TRUE;
        status = STATUS_SUCCESS;
    }

    if (!submitReport) {
        Context->LastCommandType = VHID_COMMAND_MOVE_ABSOLUTE;
        Context->LastCommandSequenceId = Request->SequenceId;
        Context->LastCommandStatus = status;
    }

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (!submitReport) {
        VHID_LOG_ERROR(
            "MoveAbsolute rejected, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);
        return status;
    }

    VHID_LOG_INFO(
        "MoveAbsolute submit attempt, x=%lu y=%lu reportId=%u sequenceId=%lu",
        Request->X,
        Request->Y,
        VHID_ABSOLUTE_MOUSE_REPORT_ID,
        Request->SequenceId);

    status = VhidVhfSubmitAbsoluteMoveReport(
        vhfHandle,
        Context->AbsoluteMoveReport,
        sizeof(Context->AbsoluteMoveReport));

    VhidVhfCompleteMoveAbsolute(Context, status);

    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "MoveAbsolute submit failed, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);
        return status;
    }

    VHID_LOG_INFO(
        "MoveAbsolute submitted, x=%lu y=%lu reportId=%u sequenceId=%lu",
        Request->X,
        Request->Y,
        VHID_ABSOLUTE_MOUSE_REPORT_ID,
        Request->SequenceId);

    return STATUS_SUCCESS;
}

NTSTATUS
VhidVhfQueryStatus(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _Out_writes_bytes_(StatusBufferLength) PVOID StatusBuffer,
    _In_ size_t StatusBufferLength,
    _Out_ size_t* BytesWritten
    )
{
    KIRQL oldIrql;
    LONG state;
    NTSTATUS triggerStatus;
    PVHID_STATUS_REPORT statusReport;

    if (Context == NULL || StatusBuffer == NULL || BytesWritten == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *BytesWritten = 0;

    if (StatusBufferLength < sizeof(VHID_STATUS_REPORT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    statusReport = (PVHID_STATUS_REPORT)StatusBuffer;
    RtlZeroMemory(statusReport, sizeof(*statusReport));

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    state = Context->ReportSequenceState;

    if (Context->Deleting) {
        triggerStatus = STATUS_DELETE_PENDING;
    } else if (!Context->VhfStarted || Context->VhfHandle == NULL) {
        triggerStatus = STATUS_DEVICE_NOT_READY;
    } else if (Context->ReportSubmissionEnabled || Context->ActiveSubmissions > 0) {
        triggerStatus = STATUS_DEVICE_BUSY;
    } else if (Context->SmokeSequenceCompleted) {
        triggerStatus = STATUS_ALREADY_COMMITTED;
    } else if (state != (LONG)VhidReportSequenceDisabled) {
        triggerStatus = STATUS_INVALID_DEVICE_STATE;
    } else {
        triggerStatus = STATUS_SUCCESS;
    }

    statusReport->Size = (ULONG)sizeof(*statusReport);
    statusReport->ProtocolVersionMajor = VHID_PROTOCOL_VERSION_MAJOR;
    statusReport->ProtocolVersionMinor = VHID_PROTOCOL_VERSION_MINOR;
    statusReport->VhfHandlePresent = (Context->VhfHandle != NULL) ? 1u : 0u;
    statusReport->Initialized = Context->Initialized ? 1u : 0u;
    statusReport->VhfCreated = Context->VhfCreated ? 1u : 0u;
    statusReport->VhfStarted = Context->VhfStarted ? 1u : 0u;
    statusReport->Deleting = Context->Deleting ? 1u : 0u;
    statusReport->ReportSubmissionEnabled = Context->ReportSubmissionEnabled ? 1u : 0u;
    statusReport->ReadyForNextReport = Context->ReadyForNextReport ? 1u : 0u;
    statusReport->ReportSequenceState = state;
    statusReport->LastReportSubmitStatus = Context->LastReportSubmitStatus;
    statusReport->LastTriggerStatus = Context->LastTriggerStatus;
    statusReport->TriggerWouldBeAccepted = NT_SUCCESS(triggerStatus) ? 1u : 0u;
    statusReport->TriggerRejectStatus = triggerStatus;
    statusReport->SmokeSequenceCompleted = Context->SmokeSequenceCompleted ? 1u : 0u;
    statusReport->CurrentCommandType = Context->CurrentCommandType;
    statusReport->CurrentCommandSequenceId = Context->CurrentCommandSequenceId;
    statusReport->LastCommandType = Context->LastCommandType;
    statusReport->LastCommandSequenceId = Context->LastCommandSequenceId;
    statusReport->LastCommandStatus = Context->LastCommandStatus;

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    *BytesWritten = sizeof(*statusReport);
    return STATUS_SUCCESS;
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

    VHID_LOG_INFO("%s", "VHF ready callback seen");
    VhidVhfMarkReadyForNextReport(context);

    if (!context->ReportSubmissionEnabled) {
        return;
    }

    VhidVhfDrainReadyReports(context);
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
    Context->ReadyForNextReport = FALSE;
    Context->CurrentCommandType = VHID_COMMAND_NONE;
    Context->CurrentCommandSequenceId = 0;
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
    Context->ReadyForNextReport = FALSE;
    Context->CurrentCommandType = VHID_COMMAND_NONE;
    Context->CurrentCommandSequenceId = 0;
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
