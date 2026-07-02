#include "VhfDevice.h"
#include "Trace.h"

/*
 * Minimal descriptor for read-only identity validation:
 * - Report ID 1: boot-style keyboard input report (modifiers, reserved, 6 keys)
 * - Report ID 2: relative mouse input report (3 buttons, X, Y)
 *
 * There is intentionally no report-submission path in this slice.
 */
static const UCHAR VhidReportDescriptor[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x06,        /* Usage (Keyboard) */
    0xA1, 0x01,        /* Collection (Application) */
    0x85, 0x01,        /*   Report ID (1) */
    0x05, 0x07,        /*   Usage Page (Keyboard/Keypad) */
    0x19, 0xE0,        /*   Usage Minimum (Keyboard LeftControl) */
    0x29, 0xE7,        /*   Usage Maximum (Keyboard Right GUI) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x01,        /*   Logical Maximum (1) */
    0x75, 0x01,        /*   Report Size (1) */
    0x95, 0x08,        /*   Report Count (8) */
    0x81, 0x02,        /*   Input (Data, Variable, Absolute) */
    0x95, 0x01,        /*   Report Count (1) */
    0x75, 0x08,        /*   Report Size (8) */
    0x81, 0x01,        /*   Input (Constant) */
    0x95, 0x06,        /*   Report Count (6) */
    0x75, 0x08,        /*   Report Size (8) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x65,        /*   Logical Maximum (101) */
    0x05, 0x07,        /*   Usage Page (Keyboard/Keypad) */
    0x19, 0x00,        /*   Usage Minimum (Reserved) */
    0x29, 0x65,        /*   Usage Maximum (Keyboard Application) */
    0x81, 0x00,        /*   Input (Data, Array, Absolute) */
    0xC0,              /* End Collection */

    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x02,        /* Usage (Mouse) */
    0xA1, 0x01,        /* Collection (Application) */
    0x85, 0x02,        /*   Report ID (2) */
    0x09, 0x01,        /*   Usage (Pointer) */
    0xA1, 0x00,        /*   Collection (Physical) */
    0x05, 0x09,        /*     Usage Page (Button) */
    0x19, 0x01,        /*     Usage Minimum (Button 1) */
    0x29, 0x03,        /*     Usage Maximum (Button 3) */
    0x15, 0x00,        /*     Logical Minimum (0) */
    0x25, 0x01,        /*     Logical Maximum (1) */
    0x95, 0x03,        /*     Report Count (3) */
    0x75, 0x01,        /*     Report Size (1) */
    0x81, 0x02,        /*     Input (Data, Variable, Absolute) */
    0x95, 0x01,        /*     Report Count (1) */
    0x75, 0x05,        /*     Report Size (5) */
    0x81, 0x01,        /*     Input (Constant) */
    0x05, 0x01,        /*     Usage Page (Generic Desktop) */
    0x09, 0x30,        /*     Usage (X) */
    0x09, 0x31,        /*     Usage (Y) */
    0x15, 0x81,        /*     Logical Minimum (-127) */
    0x25, 0x7F,        /*     Logical Maximum (127) */
    0x75, 0x08,        /*     Report Size (8) */
    0x95, 0x02,        /*     Report Count (2) */
    0x81, 0x06,        /*     Input (Data, Variable, Relative) */
    0xC0,              /*   End Collection */
    0xC0               /* End Collection */
};

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
        (USHORT)sizeof(VhidReportDescriptor),
        (PUCHAR)VhidReportDescriptor);

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
