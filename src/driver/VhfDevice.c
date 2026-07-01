#include "VhfDevice.h"
#include "Trace.h"

VOID
VhidVhfContextInit(
    _Out_ PVHID_VHF_CONTEXT Context
    )
{
    RtlZeroMemory(Context, sizeof(*Context));
}

NTSTATUS
VhidVhfInitializeNoReports(
    _Inout_ PVHID_VHF_CONTEXT Context,
    _In_ WDFDEVICE Device
    )
{
    UNREFERENCED_PARAMETER(Device);

    /*
     * This first skeleton intentionally does not create/start a VHF device and
     * does not expose any report submission API. Future phases may replace this
     * placeholder with reviewed VHF initialization after VM-only build approval.
     */
    Context->Initialized = TRUE;
    Context->ReportSubmissionEnabled = FALSE;

    return STATUS_SUCCESS;
}

VOID
VhidVhfCleanup(
    _Inout_ PVHID_VHF_CONTEXT Context
    )
{
    if (Context == NULL) {
        return;
    }

    Context->ReportSubmissionEnabled = FALSE;
    Context->Initialized = FALSE;
}
