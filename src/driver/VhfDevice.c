#include "VhfDevice.h"
#include "../shared/VhidHidDescriptor.h"
#include "../shared/VhidProtocol.h"
#include "Trace.h"

#define VHID_STATUS_VHF_CREATE_FAILED STATUS_DEVICE_NOT_READY
#define VHID_STATUS_VHF_START_FAILED STATUS_DEVICE_POWER_FAILURE
#define VHID_KEYBOARD_REPORT_ID ((UCHAR)VHID_HID_REPORT_ID_KEYBOARD)
#define VHID_MOUSE_REPORT_ID ((UCHAR)VHID_HID_REPORT_ID_RELATIVE_MOUSE)
#define VHID_ABSOLUTE_MOUSE_REPORT_ID ((UCHAR)VHID_HID_REPORT_ID_ABSOLUTE_MOUSE)
#define VHID_KEYBOARD_INPUT_REPORT_LENGTH VHID_HID_KEYBOARD_REPORT_LENGTH
#define VHID_MOUSE_INPUT_REPORT_LENGTH 4u
#define VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH VHID_HID_ABSOLUTE_MOUSE_REPORT_LENGTH
#define VHID_KEYBOARD_MODIFIER_LEFT_SHIFT 0x02u

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
C_ASSERT(VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH == VHID_HID_ABSOLUTE_MOUSE_REPORT_LENGTH);

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
VhidIsValidAbsoluteReport(
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength
    )
{
    ULONG x;
    ULONG y;

    if (Report == NULL || ReportLength != VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH) {
        return FALSE;
    }

    if (Report[0] != VHID_ABSOLUTE_MOUSE_REPORT_ID ||
        (Report[1] != 0x00 && Report[1] != 0x01)) {
        return FALSE;
    }

    x = VhidReadUshortLittleEndian(&Report[2]);
    y = VhidReadUshortLittleEndian(&Report[4]);

    return x <= VHID_HID_ABSOLUTE_COORDINATE_MAX &&
           y <= VHID_HID_ABSOLUTE_COORDINATE_MAX;
}

static
VOID
VhidBuildAbsoluteReport(
    _Out_writes_bytes_(VHID_ABSOLUTE_MOUSE_INPUT_REPORT_LENGTH) UCHAR* Report,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ UCHAR Buttons
    )
{
    Report[0] = VHID_ABSOLUTE_MOUSE_REPORT_ID;
    Report[1] = Buttons;
    Report[2] = (UCHAR)(X & 0xFFu);
    Report[3] = (UCHAR)((X >> 8) & 0xFFu);
    Report[4] = (UCHAR)(Y & 0xFFu);
    Report[5] = (UCHAR)((Y >> 8) & 0xFFu);
}
static
VOID
VhidBuildKeyboardReport(
    _Out_writes_bytes_(VHID_KEYBOARD_INPUT_REPORT_LENGTH) UCHAR* Report,
    _In_ UCHAR Modifier,
    _In_ UCHAR Usage
    )
{
    RtlZeroMemory(Report, VHID_KEYBOARD_INPUT_REPORT_LENGTH);
    Report[0] = VHID_KEYBOARD_REPORT_ID;
    Report[1] = Modifier;
    Report[3] = Usage;
}

static
BOOLEAN
VhidIsValidKeyTapUsage(
    _In_ UCHAR Usage
    )
{
    return (Usage >= 0x04 && Usage <= 0x27) ||
           (Usage >= 0x28 && Usage <= 0x38);
}

static
BOOLEAN
VhidIsValidKeyboardTapReport(
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength
    )
{
    ULONG index;

    if (Report == NULL || ReportLength != VHID_KEYBOARD_INPUT_REPORT_LENGTH) {
        return FALSE;
    }

    if (Report[0] != VHID_KEYBOARD_REPORT_ID ||
        (Report[1] != 0x00 && Report[1] != VHID_KEYBOARD_MODIFIER_LEFT_SHIFT) ||
        Report[2] != 0x00) {
        return FALSE;
    }

    for (index = 4; index < VHID_KEYBOARD_INPUT_REPORT_LENGTH; index++) {
        if (Report[index] != 0x00) {
            return FALSE;
        }
    }

    if (Report[3] == 0x00) {
        return Report[1] == 0x00;
    }

    return VhidIsValidKeyTapUsage(Report[3]);
}

static
NTSTATUS
VhidMapKeyTapKeyCode(
    _In_ ULONG KeyCode,
    _Out_ UCHAR* Modifier,
    _Out_ UCHAR* Usage
    )
{
    if (Modifier == NULL || Usage == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *Modifier = 0x00;
    *Usage = 0x00;

    if (KeyCode >= 'a' && KeyCode <= 'z') {
        *Usage = (UCHAR)(0x04u + (KeyCode - 'a'));
        return STATUS_SUCCESS;
    }

    if (KeyCode >= 'A' && KeyCode <= 'Z') {
        *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT;
        *Usage = (UCHAR)(0x04u + (KeyCode - 'A'));
        return STATUS_SUCCESS;
    }

    if (KeyCode >= '1' && KeyCode <= '9') {
        *Usage = (UCHAR)(0x1Eu + (KeyCode - '1'));
        return STATUS_SUCCESS;
    }

    switch (KeyCode) {
    case '0': *Usage = 0x27; return STATUS_SUCCESS;
    case '!': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x1E; return STATUS_SUCCESS;
    case '@': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x1F; return STATUS_SUCCESS;
    case '#': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x20; return STATUS_SUCCESS;
    case '$': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x21; return STATUS_SUCCESS;
    case '%': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x22; return STATUS_SUCCESS;
    case '^': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x23; return STATUS_SUCCESS;
    case '&': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x24; return STATUS_SUCCESS;
    case '*': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x25; return STATUS_SUCCESS;
    case '(': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x26; return STATUS_SUCCESS;
    case ')': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x27; return STATUS_SUCCESS;
    case ' ': *Usage = 0x2C; return STATUS_SUCCESS;
    case '-': *Usage = 0x2D; return STATUS_SUCCESS;
    case '_': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x2D; return STATUS_SUCCESS;
    case '=': *Usage = 0x2E; return STATUS_SUCCESS;
    case '+': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x2E; return STATUS_SUCCESS;
    case '[': *Usage = 0x2F; return STATUS_SUCCESS;
    case '{': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x2F; return STATUS_SUCCESS;
    case ']': *Usage = 0x30; return STATUS_SUCCESS;
    case '}': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x30; return STATUS_SUCCESS;
    case '\\': *Usage = 0x31; return STATUS_SUCCESS;
    case '|': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x31; return STATUS_SUCCESS;
    case ';': *Usage = 0x33; return STATUS_SUCCESS;
    case ':': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x33; return STATUS_SUCCESS;
    case '\'': *Usage = 0x34; return STATUS_SUCCESS;
    case '"': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x34; return STATUS_SUCCESS;
    case '`': *Usage = 0x35; return STATUS_SUCCESS;
    case '~': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x35; return STATUS_SUCCESS;
    case ',': *Usage = 0x36; return STATUS_SUCCESS;
    case '<': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x36; return STATUS_SUCCESS;
    case '.': *Usage = 0x37; return STATUS_SUCCESS;
    case '>': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x37; return STATUS_SUCCESS;
    case '/': *Usage = 0x38; return STATUS_SUCCESS;
    case '?': *Modifier = VHID_KEYBOARD_MODIFIER_LEFT_SHIFT; *Usage = 0x38; return STATUS_SUCCESS;
    case VHID_KEY_TAP_KEY_ENTER: *Usage = 0x28; return STATUS_SUCCESS;
    case VHID_KEY_TAP_KEY_ESCAPE: *Usage = 0x29; return STATUS_SUCCESS;
    case VHID_KEY_TAP_KEY_BACKSPACE: *Usage = 0x2A; return STATUS_SUCCESS;
    case VHID_KEY_TAP_KEY_TAB: *Usage = 0x2B; return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_PARAMETER;
    }
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
    Context->CurrentReceiptId = 0;
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
ULONG
VhidVhfAllocateReceiptId(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    ULONG receiptId;

    receiptId = Context->NextReceiptId;
    if (receiptId == 0) {
        receiptId = 1;
    }

    Context->NextReceiptId = receiptId + 1;
    if (Context->NextReceiptId == 0) {
        Context->NextReceiptId = 1;
    }

    return receiptId;
}

static
VOID
VhidVhfResetReleaseReceipt(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ BOOLEAN FinalNeutralKnown
    )
{
    Context->LastCommandReleaseStatus = STATUS_SUCCESS;
    Context->LastCommandReleaseRetryStatus = STATUS_SUCCESS;
    Context->LastCommandReleaseRetryAttempted = FALSE;
    Context->LastCommandReleaseRetrySucceeded = FALSE;
    Context->LastCommandFinalNeutralKnown = FinalNeutralKnown;
}

static
VOID
VhidVhfSetReleaseReceipt(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ NTSTATUS ReleaseStatus,
    _In_ BOOLEAN RetryAttempted,
    _In_ NTSTATUS RetryStatus,
    _In_ BOOLEAN RetrySucceeded,
    _In_ BOOLEAN FinalNeutralKnown
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    Context->LastCommandReleaseStatus = ReleaseStatus;
    Context->LastCommandReleaseRetryAttempted = RetryAttempted;
    Context->LastCommandReleaseRetryStatus = RetryStatus;
    Context->LastCommandReleaseRetrySucceeded = RetrySucceeded;
    Context->LastCommandFinalNeutralKnown = FinalNeutralKnown;
    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
}
static
VOID
VhidVhfStoreCurrentReceiptLocked(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ NTSTATUS Status
    )
{
    if (Context->CurrentReceiptId == 0) {
        return;
    }

    Context->LastCommandType = Context->CurrentCommandType;
    Context->LastCommandSequenceId = Context->CurrentCommandSequenceId;
    Context->LastReceiptId = Context->CurrentReceiptId;
    Context->LastCommandStatus = Status;
    Context->LastCommandAcceptStatus = STATUS_SUCCESS;
}

static
VOID
VhidVhfRecordRejectedCommandLocked(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ ULONG CommandType,
    _In_ ULONG SequenceId,
    _In_ NTSTATUS Status
    )
{
    Context->LastRejectedCommandType = CommandType;
    Context->LastRejectedCommandSequenceId = SequenceId;
    Context->LastRejectedCommandStatus = Status;
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
        VhidVhfStoreCurrentReceiptLocked(Context, Status);
        Context->ReportSubmissionEnabled = FALSE;
        Context->CurrentCommandType = VHID_COMMAND_NONE;
        Context->CurrentCommandSequenceId = 0;
        Context->CurrentReceiptId = 0;
        Context->LastCommandReleaseStatus = Status;
        Context->LastCommandFinalNeutralKnown = FALSE;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportSequenceFailed);
    } else if (!Context->Deleting) {
        if (DisableOnSuccess) {
            VhidVhfStoreCurrentReceiptLocked(Context, Status);
            Context->ReportSubmissionEnabled = FALSE;
            if (SuccessState == VhidReportSequenceComplete) {
                Context->SmokeSequenceCompleted = TRUE;
            }
            Context->CurrentCommandType = VHID_COMMAND_NONE;
            Context->CurrentCommandSequenceId = 0;
            Context->CurrentReceiptId = 0;
            if (SuccessState == VhidReportSequenceComplete) {
                Context->LastCommandReleaseStatus = STATUS_SUCCESS;
                Context->LastCommandFinalNeutralKnown = TRUE;
            }
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
VhidVhfCompleteDirectCommand(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ NTSTATUS Status
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);

    Context->LastReportSubmitStatus = Status;
    VhidVhfStoreCurrentReceiptLocked(Context, Status);
    if (Context->CurrentReceiptId == 0) {
        Context->LastCommandStatus = Status;
    }
    Context->ReportSubmissionEnabled = FALSE;
    Context->CurrentCommandType = VHID_COMMAND_NONE;
    Context->CurrentCommandSequenceId = 0;
    Context->CurrentReceiptId = 0;

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
VOID
VhidVhfSetDirectCommandState(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ VHID_REPORT_SEQUENCE_STATE State
    )
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
    InterlockedExchange(&Context->ReportSequenceState, (LONG)State);
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
VhidVhfSubmitAbsoluteReport(
    _In_ VHFHANDLE VhfHandle,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength
    )
{
    HID_XFER_PACKET packet;

    if (VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (!VhidIsValidAbsoluteReport(Report, ReportLength)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = Report;
    packet.reportBufferLen = ReportLength;
    packet.reportId = VHID_ABSOLUTE_MOUSE_REPORT_ID;

    return VhfReadReportSubmit(VhfHandle, &packet);
}
static
NTSTATUS
VhidVhfSubmitKeyboardTapReport(
    _In_ VHFHANDLE VhfHandle,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength
    )
{
    HID_XFER_PACKET packet;

    if (VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (!VhidIsValidKeyboardTapReport(Report, ReportLength)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&packet, sizeof(packet));
    packet.reportBuffer = Report;
    packet.reportBufferLen = ReportLength;
    packet.reportId = VHID_KEYBOARD_REPORT_ID;

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
    Context->LastCommandAcceptStatus = STATUS_SUCCESS;
    Context->LastCommandReleaseStatus = STATUS_SUCCESS;
    Context->LastCommandReleaseRetryStatus = STATUS_SUCCESS;
    Context->LastCommandFinalNeutralKnown = TRUE;
    Context->LastRejectedCommandStatus = STATUS_SUCCESS;
    Context->NextReceiptId = 1;
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
        Context->LastCommandAcceptStatus = STATUS_SUCCESS;
        Context->CurrentReceiptId = VhidVhfAllocateReceiptId(Context);
        Context->LastReceiptId = Context->CurrentReceiptId;
        Context->LastCommandReleaseStatus = STATUS_PENDING;
        Context->LastCommandReleaseRetryStatus = STATUS_SUCCESS;
        Context->LastCommandReleaseRetryAttempted = FALSE;
        Context->LastCommandReleaseRetrySucceeded = FALSE;
        Context->LastCommandFinalNeutralKnown = FALSE;
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
    if (!submitKickstart) {
        VhidVhfRecordRejectedCommandLocked(
            Context,
            VHID_COMMAND_SMOKE_SEQUENCE,
            0,
            status);
    }

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
        VhidVhfRecordRejectedCommandLocked(
            Context,
            (Request != NULL) ? Request->CommandType : VHID_COMMAND_MOVE_ABSOLUTE,
            (Request != NULL) ? Request->SequenceId : 0,
            status);
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
        VhidBuildAbsoluteReport(
            Context->AbsoluteMoveReport,
            Request->X,
            Request->Y,
            0x00);
        Context->LastReportSubmitStatus = STATUS_SUCCESS;
        Context->LastCommandType = VHID_COMMAND_MOVE_ABSOLUTE;
        Context->LastCommandSequenceId = Request->SequenceId;
        Context->LastCommandStatus = STATUS_PENDING;
        Context->LastCommandAcceptStatus = STATUS_SUCCESS;
        Context->CurrentReceiptId = VhidVhfAllocateReceiptId(Context);
        Context->LastReceiptId = Context->CurrentReceiptId;
        VhidVhfResetReleaseReceipt(Context, TRUE);
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
        VhidVhfRecordRejectedCommandLocked(
            Context,
            VHID_COMMAND_MOVE_ABSOLUTE,
            Request->SequenceId,
            status);
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

    status = VhidVhfSubmitAbsoluteReport(
        vhfHandle,
        Context->AbsoluteMoveReport,
        sizeof(Context->AbsoluteMoveReport));

    VhidVhfCompleteDirectCommand(Context, status);

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

static
NTSTATUS
VhidValidateClickAbsoluteRequest(
    _In_ const VHID_CLICK_ABSOLUTE_REQUEST* Request
    )
{
    if (Request == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->Size != sizeof(*Request) ||
        Request->ProtocolVersionMajor != VHID_PROTOCOL_VERSION_MAJOR ||
        Request->ProtocolVersionMinor != VHID_PROTOCOL_VERSION_MINOR ||
        Request->CommandType != VHID_COMMAND_CLICK_ABSOLUTE) {
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
VhidVhfClickAbsolute(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ const VHID_CLICK_ABSOLUTE_REQUEST* Request
    )
{
    KIRQL oldIrql;
    LONG state;
    NTSTATUS status;
    NTSTATUS retryStatus;
    VHFHANDLE vhfHandle;
    BOOLEAN submitReports;
    BOOLEAN releaseRetryAttempted;
    BOOLEAN releaseRetrySucceeded;

    if (Context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = VhidValidateClickAbsoluteRequest(Request);
    if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
        VhidVhfRecordRejectedCommandLocked(
            Context,
            (Request != NULL) ? Request->CommandType : VHID_COMMAND_CLICK_ABSOLUTE,
            (Request != NULL) ? Request->SequenceId : 0,
            status);
        KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
        VHID_LOG_ERROR(
            "ClickAbsolute rejected, invalid request status=0x%08X",
            status);
        return status;
    }

    vhfHandle = NULL;
    submitReports = FALSE;
    retryStatus = STATUS_SUCCESS;
    releaseRetryAttempted = FALSE;
    releaseRetrySucceeded = FALSE;

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
        VhidBuildAbsoluteReport(
            Context->AbsoluteMoveReport,
            Request->X,
            Request->Y,
            0x00);
        VhidBuildAbsoluteReport(
            Context->AbsoluteClickDownReport,
            Request->X,
            Request->Y,
            0x01);
        VhidBuildAbsoluteReport(
            Context->AbsoluteClickUpReport,
            Request->X,
            Request->Y,
            0x00);
        Context->LastReportSubmitStatus = STATUS_SUCCESS;
        Context->LastCommandType = VHID_COMMAND_CLICK_ABSOLUTE;
        Context->LastCommandSequenceId = Request->SequenceId;
        Context->LastCommandStatus = STATUS_PENDING;
        Context->LastCommandAcceptStatus = STATUS_SUCCESS;
        Context->CurrentReceiptId = VhidVhfAllocateReceiptId(Context);
        Context->LastReceiptId = Context->CurrentReceiptId;
        Context->LastCommandReleaseStatus = STATUS_PENDING;
        Context->LastCommandReleaseRetryStatus = STATUS_SUCCESS;
        Context->LastCommandReleaseRetryAttempted = FALSE;
        Context->LastCommandReleaseRetrySucceeded = FALSE;
        Context->LastCommandFinalNeutralKnown = FALSE;
        Context->CurrentCommandType = VHID_COMMAND_CLICK_ABSOLUTE;
        Context->CurrentCommandSequenceId = Request->SequenceId;
        Context->ReportSubmissionEnabled = TRUE;
        Context->ReadyForNextReport = FALSE;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportClickAbsoluteMoveSubmitting);
        Context->ActiveSubmissions++;
        KeClearEvent(&Context->NoActiveSubmissionsEvent);
        vhfHandle = Context->VhfHandle;
        submitReports = TRUE;
        status = STATUS_SUCCESS;
    }

    if (!submitReports) {
        VhidVhfRecordRejectedCommandLocked(
            Context,
            VHID_COMMAND_CLICK_ABSOLUTE,
            Request->SequenceId,
            status);
    }

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (!submitReports) {
        VHID_LOG_ERROR(
            "ClickAbsolute rejected, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);
        return status;
    }

    VHID_LOG_INFO(
        "ClickAbsolute move submit attempt, x=%lu y=%lu reportId=%u sequenceId=%lu",
        Request->X,
        Request->Y,
        VHID_ABSOLUTE_MOUSE_REPORT_ID,
        Request->SequenceId);

    status = VhidVhfSubmitAbsoluteReport(
        vhfHandle,
        Context->AbsoluteMoveReport,
        sizeof(Context->AbsoluteMoveReport));
    if (!NT_SUCCESS(status)) {
        VhidVhfSetReleaseReceipt(
            Context,
            STATUS_SUCCESS,
            FALSE,
            STATUS_SUCCESS,
            FALSE,
            TRUE);
        VhidVhfCompleteDirectCommand(Context, status);
        VHID_LOG_ERROR(
            "ClickAbsolute move submit failed, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);
        return status;
    }

    VhidVhfSetDirectCommandState(
        Context,
        VhidReportClickAbsoluteDownSubmitting);

    VHID_LOG_INFO(
        "ClickAbsolute left-button down submit attempt, x=%lu y=%lu reportId=%u sequenceId=%lu",
        Request->X,
        Request->Y,
        VHID_ABSOLUTE_MOUSE_REPORT_ID,
        Request->SequenceId);

    status = VhidVhfSubmitAbsoluteReport(
        vhfHandle,
        Context->AbsoluteClickDownReport,
        sizeof(Context->AbsoluteClickDownReport));
    if (!NT_SUCCESS(status)) {
        VhidVhfSetReleaseReceipt(
            Context,
            STATUS_SUCCESS,
            FALSE,
            STATUS_SUCCESS,
            FALSE,
            TRUE);
        VhidVhfCompleteDirectCommand(Context, status);
        VHID_LOG_ERROR(
            "ClickAbsolute left-button down submit failed, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);
        return status;
    }

    VhidVhfSetDirectCommandState(
        Context,
        VhidReportClickAbsoluteUpSubmitting);

    VHID_LOG_INFO(
        "ClickAbsolute left-button up submit attempt, x=%lu y=%lu reportId=%u sequenceId=%lu",
        Request->X,
        Request->Y,
        VHID_ABSOLUTE_MOUSE_REPORT_ID,
        Request->SequenceId);

    status = VhidVhfSubmitAbsoluteReport(
        vhfHandle,
        Context->AbsoluteClickUpReport,
        sizeof(Context->AbsoluteClickUpReport));

    if (!NT_SUCCESS(status)) {
        releaseRetryAttempted = TRUE;
        VHID_LOG_ERROR(
            "ClickAbsolute left-button up submit failed, attempting emergency neutral retry, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);

        retryStatus = VhidVhfSubmitAbsoluteReport(
            vhfHandle,
            Context->AbsoluteClickUpReport,
            sizeof(Context->AbsoluteClickUpReport));

        if (NT_SUCCESS(retryStatus)) {
            VHID_LOG_INFO(
                "ClickAbsolute emergency neutral retry completed, x=%lu y=%lu reportId=%u sequenceId=%lu",
                Request->X,
                Request->Y,
                VHID_ABSOLUTE_MOUSE_REPORT_ID,
                Request->SequenceId);
            releaseRetrySucceeded = TRUE;
            status = STATUS_SUCCESS;
        } else {
            VHID_LOG_ERROR(
                "ClickAbsolute emergency neutral retry failed, x=%lu y=%lu status=0x%08X",
                Request->X,
                Request->Y,
                retryStatus);
            status = retryStatus;
        }
    }

    VhidVhfSetReleaseReceipt(
        Context,
        status,
        releaseRetryAttempted,
        retryStatus,
        releaseRetrySucceeded,
        NT_SUCCESS(status));

    VhidVhfCompleteDirectCommand(Context, status);

    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "ClickAbsolute left-button up submit failed after emergency neutral retry, x=%lu y=%lu status=0x%08X",
            Request->X,
            Request->Y,
            status);
        return status;
    }

    VHID_LOG_INFO(
        "ClickAbsolute completed, x=%lu y=%lu reportId=%u sequenceId=%lu",
        Request->X,
        Request->Y,
        VHID_ABSOLUTE_MOUSE_REPORT_ID,
        Request->SequenceId);

    return STATUS_SUCCESS;
}
static
NTSTATUS
VhidValidateKeyTapRequest(
    _In_ const VHID_KEY_TAP_REQUEST* Request,
    _Out_ UCHAR* Modifier,
    _Out_ UCHAR* Usage
    )
{
    if (Request == NULL || Modifier == NULL || Usage == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->Size != sizeof(*Request) ||
        Request->ProtocolVersionMajor != VHID_PROTOCOL_VERSION_MAJOR ||
        Request->ProtocolVersionMinor != VHID_PROTOCOL_VERSION_MINOR ||
        Request->CommandType != VHID_COMMAND_KEY_TAP) {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->Reserved0 != 0 ||
        Request->Reserved1 != 0 ||
        Request->Reserved2 != 0 ||
        Request->Reserved3 != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    return VhidMapKeyTapKeyCode(Request->KeyCode, Modifier, Usage);
}

NTSTATUS
VhidVhfKeyTap(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ const VHID_KEY_TAP_REQUEST* Request
    )
{
    KIRQL oldIrql;
    LONG state;
    NTSTATUS status;
    NTSTATUS retryStatus;
    VHFHANDLE vhfHandle;
    BOOLEAN submitReports;
    BOOLEAN releaseRetryAttempted;
    BOOLEAN releaseRetrySucceeded;
    UCHAR modifier;
    UCHAR usage;

    if (Context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    modifier = 0x00;
    usage = 0x00;
    status = VhidValidateKeyTapRequest(Request, &modifier, &usage);
    if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&Context->SubmissionLock, &oldIrql);
        VhidVhfRecordRejectedCommandLocked(
            Context,
            (Request != NULL) ? Request->CommandType : VHID_COMMAND_KEY_TAP,
            (Request != NULL) ? Request->SequenceId : 0,
            status);
        KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);
        VHID_LOG_ERROR(
            "KeyTap rejected, invalid request status=0x%08X",
            status);
        return status;
    }

    vhfHandle = NULL;
    submitReports = FALSE;
    retryStatus = STATUS_SUCCESS;
    releaseRetryAttempted = FALSE;
    releaseRetrySucceeded = FALSE;

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
        VhidBuildKeyboardReport(
            Context->KeyboardKeyDownReport,
            modifier,
            usage);
        Context->LastReportSubmitStatus = STATUS_SUCCESS;
        Context->LastCommandType = VHID_COMMAND_KEY_TAP;
        Context->LastCommandSequenceId = Request->SequenceId;
        Context->LastCommandStatus = STATUS_PENDING;
        Context->LastCommandAcceptStatus = STATUS_SUCCESS;
        Context->CurrentReceiptId = VhidVhfAllocateReceiptId(Context);
        Context->LastReceiptId = Context->CurrentReceiptId;
        Context->LastCommandReleaseStatus = STATUS_PENDING;
        Context->LastCommandReleaseRetryStatus = STATUS_SUCCESS;
        Context->LastCommandReleaseRetryAttempted = FALSE;
        Context->LastCommandReleaseRetrySucceeded = FALSE;
        Context->LastCommandFinalNeutralKnown = FALSE;
        Context->CurrentCommandType = VHID_COMMAND_KEY_TAP;
        Context->CurrentCommandSequenceId = Request->SequenceId;
        Context->ReportSubmissionEnabled = TRUE;
        Context->ReadyForNextReport = FALSE;
        InterlockedExchange(
            &Context->ReportSequenceState,
            (LONG)VhidReportKeyTapDownSubmitting);
        Context->ActiveSubmissions++;
        KeClearEvent(&Context->NoActiveSubmissionsEvent);
        vhfHandle = Context->VhfHandle;
        submitReports = TRUE;
        status = STATUS_SUCCESS;
    }

    if (!submitReports) {
        VhidVhfRecordRejectedCommandLocked(
            Context,
            VHID_COMMAND_KEY_TAP,
            Request->SequenceId,
            status);
    }

    KeReleaseSpinLock(&Context->SubmissionLock, oldIrql);

    if (!submitReports) {
        VHID_LOG_ERROR(
            "KeyTap rejected, keyCode=0x%08X status=0x%08X",
            Request->KeyCode,
            status);
        return status;
    }

    VHID_LOG_INFO(
        "KeyTap key-down submit attempt, keyCode=0x%08X modifier=0x%02X usage=0x%02X sequenceId=%lu",
        Request->KeyCode,
        modifier,
        usage,
        Request->SequenceId);

    status = VhidVhfSubmitKeyboardTapReport(
        vhfHandle,
        Context->KeyboardKeyDownReport,
        sizeof(Context->KeyboardKeyDownReport));
    if (!NT_SUCCESS(status)) {
        VhidVhfSetReleaseReceipt(
            Context,
            STATUS_SUCCESS,
            FALSE,
            STATUS_SUCCESS,
            FALSE,
            TRUE);
        VhidVhfCompleteDirectCommand(Context, status);
        VHID_LOG_ERROR(
            "KeyTap key-down submit failed, keyCode=0x%08X status=0x%08X",
            Request->KeyCode,
            status);
        return status;
    }

    VhidVhfSetDirectCommandState(
        Context,
        VhidReportKeyTapUpSubmitting);

    VHID_LOG_INFO(
        "KeyTap neutral key-up submit attempt, keyCode=0x%08X sequenceId=%lu",
        Request->KeyCode,
        Request->SequenceId);

    status = VhidVhfSubmitKeyboardTapReport(
        vhfHandle,
        VhidNeutralKeyboardReport,
        sizeof(VhidNeutralKeyboardReport));

    if (!NT_SUCCESS(status)) {
        releaseRetryAttempted = TRUE;
        VHID_LOG_ERROR(
            "KeyTap neutral key-up submit failed, attempting emergency neutral retry, keyCode=0x%08X status=0x%08X",
            Request->KeyCode,
            status);

        retryStatus = VhidVhfSubmitKeyboardTapReport(
            vhfHandle,
            VhidNeutralKeyboardReport,
            sizeof(VhidNeutralKeyboardReport));

        if (NT_SUCCESS(retryStatus)) {
            VHID_LOG_INFO(
                "KeyTap emergency neutral retry completed, keyCode=0x%08X sequenceId=%lu",
                Request->KeyCode,
                Request->SequenceId);
            releaseRetrySucceeded = TRUE;
            status = STATUS_SUCCESS;
        } else {
            VHID_LOG_ERROR(
                "KeyTap emergency neutral retry failed, keyCode=0x%08X status=0x%08X",
                Request->KeyCode,
                retryStatus);
            status = retryStatus;
        }
    }

    VhidVhfSetReleaseReceipt(
        Context,
        status,
        releaseRetryAttempted,
        retryStatus,
        releaseRetrySucceeded,
        NT_SUCCESS(status));

    VhidVhfCompleteDirectCommand(Context, status);

    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "KeyTap neutral key-up submit failed after emergency neutral retry, keyCode=0x%08X status=0x%08X",
            Request->KeyCode,
            status);
        return status;
    }

    VHID_LOG_INFO(
        "KeyTap completed, keyCode=0x%08X modifier=0x%02X usage=0x%02X sequenceId=%lu",
        Request->KeyCode,
        modifier,
        usage,
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
    statusReport->SupportedCommandMask = VHID_COMMAND_CAPABILITY_MASK;
    statusReport->CurrentReceiptId = Context->CurrentReceiptId;
    statusReport->LastReceiptId = Context->LastReceiptId;
    statusReport->LastCommandAcceptStatus = Context->LastCommandAcceptStatus;
    statusReport->LastCommandReleaseStatus = Context->LastCommandReleaseStatus;
    statusReport->LastCommandReleaseRetryStatus = Context->LastCommandReleaseRetryStatus;
    statusReport->LastCommandReleaseRetryAttempted = Context->LastCommandReleaseRetryAttempted ? 1u : 0u;
    statusReport->LastCommandReleaseRetrySucceeded = Context->LastCommandReleaseRetrySucceeded ? 1u : 0u;
    statusReport->LastCommandFinalNeutralKnown = Context->LastCommandFinalNeutralKnown ? 1u : 0u;
    statusReport->LastRejectedCommandType = Context->LastRejectedCommandType;
    statusReport->LastRejectedCommandSequenceId = Context->LastRejectedCommandSequenceId;
    statusReport->LastRejectedCommandStatus = Context->LastRejectedCommandStatus;

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
    Context->CurrentReceiptId = 0;
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
    Context->CurrentReceiptId = 0;
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
