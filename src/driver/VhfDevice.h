#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

typedef enum _VHID_REPORT_SEQUENCE_STATE {
    VhidReportSequenceDisabled = 0,
    VhidReportKeyboardPreClearPending = 1,
    VhidReportKeyboardPreClearSubmitting = 2,
    VhidReportMousePreClearPending = 3,
    VhidReportMousePreClearSubmitting = 4,
    VhidReportKeyboardAPressPending = 5,
    VhidReportKeyboardAPressSubmitting = 6,
    VhidReportKeyboardReleasePending = 7,
    VhidReportKeyboardReleaseSubmitting = 8,
    VhidReportMouseMoveRightPending = 9,
    VhidReportMouseMoveRightSubmitting = 10,
    VhidReportMousePostClearPending = 11,
    VhidReportMousePostClearSubmitting = 12,
    VhidReportKeyboardFinalClearPending = 13,
    VhidReportKeyboardFinalClearSubmitting = 14,
    VhidReportSequenceComplete = 15,
    VhidReportSequenceFailed = 16
} VHID_REPORT_SEQUENCE_STATE;

typedef struct _VHID_VHF_CONTEXT {
    VHFHANDLE VhfHandle;
    BOOLEAN Initialized;
    BOOLEAN VhfCreated;
    BOOLEAN VhfStarted;
    BOOLEAN ReportSubmissionEnabled;
    BOOLEAN ReadyForNextReport;
    volatile LONG ReportSequenceState;
    NTSTATUS LastReportSubmitStatus;
    KSPIN_LOCK SubmissionLock;
    KEVENT NoActiveSubmissionsEvent;
    LONG ActiveSubmissions;
    BOOLEAN Deleting;
} VHID_VHF_CONTEXT, *PVHID_VHF_CONTEXT;

VOID
VhidVhfContextInit(
    _Out_ PVHID_VHF_CONTEXT Context
    );

NTSTATUS
VhidVhfInitialize(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ WDFDEVICE Device
    );

NTSTATUS
VhidVhfTriggerSmokeSequence(
    _Inout_ PVHID_VHF_CONTEXT Context
    );

EVT_VHF_READY_FOR_NEXT_READ_REPORT VhidEvtVhfReadyForNextReadReport;

VOID
VhidVhfCleanup(
    _Inout_ PVHID_VHF_CONTEXT Context
    );
