#include "VhfDevice.h"
#include "../shared/VhidHidDescriptor.h"
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
    NTSTATUS status;
    VHF_CONFIG config;

    VHF_CONFIG_INIT(
        &config,
        WdfDeviceWdmGetDeviceObject(Device),
        (USHORT)VHID_HID_REPORT_DESCRIPTOR_LENGTH,
        (PUCHAR)VhidHidReportDescriptor);

    config.VhfClientContext = Context;

    Context->ReportSubmissionEnabled = FALSE;

    status = VhfCreate(&config, &Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    Context->VhfCreated = TRUE;

    status = VhfStart(Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(Context->VhfHandle, TRUE);
        VhidVhfContextInit(Context);
        return status;
    }

    Context->VhfStarted = TRUE;
    Context->Initialized = TRUE;

    return status;
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
    Context->VhfStarted = FALSE;

    if (Context->VhfHandle != NULL) {
        VhfDelete(Context->VhfHandle, TRUE);
        Context->VhfHandle = NULL;
    }

    Context->VhfCreated = FALSE;
    Context->Initialized = FALSE;
}
