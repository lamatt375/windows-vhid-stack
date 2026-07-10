#include <initguid.h>

#include "Device.h"
#include "Trace.h"

static
NTSTATUS
VhidCreateDefaultQueue(
    _In_ WDFDEVICE Device
    )
{
    WDF_IO_QUEUE_CONFIG queueConfig;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = VhidEvtIoDeviceControl;

    return WdfIoQueueCreate(
        Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE);
}

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

    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_WINDOWS_VHID_STACK,
        NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = VhidCreateDefaultQueue(device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = VhidVhfInitialize(&context->Vhf, device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
VhidHandleMoveAbsoluteRequest(
    _Inout_ PVHID_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVOID inputBuffer;

    if (InputBufferLength != sizeof(VHID_MOVE_ABSOLUTE_REQUEST) ||
        OutputBufferLength != 0) {
        status = STATUS_INVALID_PARAMETER;
        VHID_LOG_ERROR(
            "MoveAbsolute rejected, invalid buffers input=%Iu output=%Iu status=0x%08X",
            InputBufferLength,
            OutputBufferLength,
            status);
        return status;
    }

    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(VHID_MOVE_ABSOLUTE_REQUEST),
        &inputBuffer,
        NULL);
    if (!NT_SUCCESS(status)) {
        VHID_LOG_ERROR(
            "MoveAbsolute rejected, input retrieval failed status=0x%08X",
            status);
        return status;
    }

    return VhidVhfMoveAbsolute(
        &Context->Vhf,
        (const VHID_MOVE_ABSOLUTE_REQUEST*)inputBuffer);
}

VOID
VhidEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device;
    PVHID_DEVICE_CONTEXT context;
    NTSTATUS status;
    PVOID outputBuffer;
    size_t bytesWritten;
    ULONG_PTR information;

    device = WdfIoQueueGetDevice(Queue);
    context = VhidGetDeviceContext(device);
    outputBuffer = NULL;
    bytesWritten = 0;
    information = 0;

    VHID_LOG_INFO(
        "Device control received, ioctl=0x%08X input=%Iu output=%Iu",
        IoControlCode,
        InputBufferLength,
        OutputBufferLength);

    if (IoControlCode == VHID_IOCTL_TRIGGER_SMOKE_SEQUENCE) {
        if (InputBufferLength != 0 || OutputBufferLength != 0) {
            status = STATUS_INVALID_PARAMETER;
            VHID_LOG_ERROR(
                "Smoke trigger rejected, nonzero buffers input=%Iu output=%Iu status=0x%08X",
                InputBufferLength,
                OutputBufferLength,
                status);
        } else {
            VHID_LOG_INFO("%s", "Smoke trigger IOCTL received");
            status = VhidVhfTriggerSmokeSequence(&context->Vhf);
            if (NT_SUCCESS(status)) {
                VHID_LOG_INFO("%s", "Smoke trigger IOCTL accepted");
            } else {
                VHID_LOG_ERROR(
                    "Smoke trigger IOCTL failed, status=0x%08X",
                    status);
            }
        }
    } else if (IoControlCode == VHID_IOCTL_MOVE_ABSOLUTE) {
        VHID_LOG_INFO("%s", "MoveAbsolute IOCTL received");
        status = VhidHandleMoveAbsoluteRequest(
            context,
            Request,
            OutputBufferLength,
            InputBufferLength);
        if (NT_SUCCESS(status)) {
            VHID_LOG_INFO("%s", "MoveAbsolute IOCTL accepted");
        } else {
            VHID_LOG_ERROR(
                "MoveAbsolute IOCTL failed, status=0x%08X",
                status);
        }
    } else if (IoControlCode == VHID_IOCTL_QUERY_STATUS) {
        if (InputBufferLength != 0) {
            status = STATUS_INVALID_PARAMETER;
            VHID_LOG_ERROR(
                "Status query rejected, nonzero input=%Iu status=0x%08X",
                InputBufferLength,
                status);
        } else {
            status = WdfRequestRetrieveOutputBuffer(
                Request,
                sizeof(VHID_STATUS_REPORT),
                &outputBuffer,
                NULL);
            if (NT_SUCCESS(status)) {
                status = VhidVhfQueryStatus(
                    &context->Vhf,
                    outputBuffer,
                    OutputBufferLength,
                    &bytesWritten);
                if (NT_SUCCESS(status)) {
                    information = (ULONG_PTR)bytesWritten;
                }
            }

            if (!NT_SUCCESS(status)) {
                VHID_LOG_ERROR(
                    "Status query failed, status=0x%08X",
                    status);
            }
        }
    } else {
        status = STATUS_INVALID_DEVICE_REQUEST;
        VHID_LOG_ERROR(
            "Device control rejected, unsupported ioctl=0x%08X status=0x%08X",
            IoControlCode,
            status);
    }

    WdfRequestCompleteWithInformation(Request, status, information);
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
