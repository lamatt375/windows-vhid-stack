#pragma once

#include <ntddk.h>
#include <wdf.h>

#include "VhfDevice.h"

typedef struct _VHID_DEVICE_CONTEXT {
    VHID_VHF_CONTEXT Vhf;
} VHID_DEVICE_CONTEXT, *PVHID_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VHID_DEVICE_CONTEXT, VhidGetDeviceContext)

EVT_WDF_OBJECT_CONTEXT_CLEANUP VhidEvtDeviceContextCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VhidEvtIoDeviceControl;

NTSTATUS
VhidCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );
