#pragma once

#include <ntddk.h>
#include <wdf.h>

typedef struct _VHID_VHF_CONTEXT {
    BOOLEAN Initialized;
    BOOLEAN ReportSubmissionEnabled;
} VHID_VHF_CONTEXT, *PVHID_VHF_CONTEXT;

VOID
VhidVhfContextInit(
    _Out_ PVHID_VHF_CONTEXT Context
    );

NTSTATUS
VhidVhfInitializeNoReports(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ WDFDEVICE Device
    );

VOID
VhidVhfCleanup(
    _Inout_ PVHID_VHF_CONTEXT Context
    );
