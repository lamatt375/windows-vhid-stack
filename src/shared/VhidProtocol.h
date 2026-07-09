#pragma once

#define VHID_PROTOCOL_VERSION_MAJOR 0u
#define VHID_PROTOCOL_VERSION_MINOR 2u
#define VHID_PROJECT_NAME "windows-vhid-stack"

/*
 * Single fixed control surface for the VM-only smoke proof.
 *
 * The trigger IOCTL accepts no input or output buffers. It starts the driver's
 * built-in fixed smoke sequence once; it does not carry key codes, text,
 * coordinates, clicks, repeats, report bytes, or arbitrary commands.
 *
 * The status IOCTL is read-only. It accepts no input buffer and returns this
 * fixed status structure without arming the sequence or submitting reports.
 */
DEFINE_GUID(
    GUID_DEVINTERFACE_WINDOWS_VHID_STACK,
    0x0f7f5f4c, 0x4c1d, 0x4ba7,
    0x8f, 0x67, 0x0a, 0x03, 0xa4, 0x2a, 0x0f, 0x3d);

#define VHID_IOCTL_TRIGGER_SMOKE_SEQUENCE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

#define VHID_IOCTL_QUERY_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

typedef struct _VHID_STATUS_REPORT {
    ULONG Size;
    ULONG ProtocolVersionMajor;
    ULONG ProtocolVersionMinor;
    ULONG VhfHandlePresent;
    ULONG Initialized;
    ULONG VhfCreated;
    ULONG VhfStarted;
    ULONG Deleting;
    ULONG ReportSubmissionEnabled;
    ULONG ReadyForNextReport;
    LONG ReportSequenceState;
    LONG LastReportSubmitStatus;
    LONG LastTriggerStatus;
    ULONG TriggerWouldBeAccepted;
    LONG TriggerRejectStatus;
} VHID_STATUS_REPORT, *PVHID_STATUS_REPORT;
