#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <vhf.h>

typedef enum _VHID_NEUTRAL_REPORT_STATE {
    VhidNeutralReportDisabled = 0,
    VhidNeutralKeyboardPending = 1,
    VhidNeutralKeyboardSubmitting = 2,
    VhidNeutralMousePending = 3,
    VhidNeutralMouseSubmitting = 4,
    VhidNeutralReportComplete = 5,
    VhidNeutralReportFailed = 6
} VHID_NEUTRAL_REPORT_STATE;

typedef struct _VHID_VHF_CONTEXT {
    VHFHANDLE VhfHandle;
    BOOLEAN Initialized;
    BOOLEAN VhfCreated;
    BOOLEAN VhfStarted;
    BOOLEAN ReportSubmissionEnabled;
    volatile LONG NeutralReportState;
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

EVT_VHF_READY_FOR_NEXT_READ_REPORT VhidEvtVhfReadyForNextReadReport;

VOID
VhidVhfCleanup(
    _Inout_ PVHID_VHF_CONTEXT Context
    );
