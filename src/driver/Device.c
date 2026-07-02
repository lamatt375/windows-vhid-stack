#include "Device.h"
#include "Trace.h"

NTSTATUS
VhidCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    PVHID_DEVICE_CONTEXT context;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VHID_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = VhidEvtDeviceContextCleanup;
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = VhidGetDeviceContext(device);
    VhidVhfContextInit(&context->Vhf);

    status = VhidVhfInitializeNoReports(&context->Vhf, device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return STATUS_SUCCESS;
}

VOID
VhidEvtDeviceContextCleanup(
    _In_ WDFOBJECT Object
    )
{
    WDFDEVICE device;
    PVHID_DEVICE_CONTEXT context;

    device = (WDFDEVICE)Object;
    context = VhidGetDeviceContext(device);

    VhidVhfCleanup(&context->Vhf);
}
