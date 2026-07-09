#pragma once

#define VHID_PROTOCOL_VERSION_MAJOR 0u
#define VHID_PROTOCOL_VERSION_MINOR 3u
#define VHID_PROJECT_NAME "windows-vhid-stack"

/*
 * Narrow VM-lab control surface.
 *
 * The smoke trigger IOCTL accepts no input or output buffers. It starts the
 * driver's built-in fixed smoke sequence once; it does not carry key codes,
 * text, coordinates, clicks, repeats, report bytes, or arbitrary commands.
 *
 * The move-absolute IOCTL accepts exactly one fixed request structure with
 * normalized 0..32767 coordinates. It does not accept raw HID report bytes,
 * click/buttons, text, repeats, batches, or alternate commands.
 *
 * The status IOCTL is read-only. It accepts no input buffer and returns this
 * fixed status structure without arming sequences or submitting reports.
 */
DEFINE_GUID(
    GUID_DEVINTERFACE_WINDOWS_VHID_STACK,
    0x0f7f5f4c, 0x4c1d, 0x4ba7,
    0x8f, 0x67, 0x0a, 0x03, 0xa4, 0x2a, 0x0f, 0x3d);

#define VHID_IOCTL_TRIGGER_SMOKE_SEQUENCE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_WRITE_DATA)

#define VHID_IOCTL_QUERY_STATUS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA)

#define VHID_IOCTL_MOVE_ABSOLUTE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_WRITE_DATA)

#define VHID_COMMAND_NONE 0u
#define VHID_COMMAND_SMOKE_SEQUENCE 1u
#define VHID_COMMAND_MOVE_ABSOLUTE 2u

#define VHID_MOVE_ABSOLUTE_COORDINATE_MIN 0u
#define VHID_MOVE_ABSOLUTE_COORDINATE_MAX 32767u

typedef struct _VHID_MOVE_ABSOLUTE_REQUEST {
    ULONG Size;
    ULONG ProtocolVersionMajor;
    ULONG ProtocolVersionMinor;
    ULONG CommandType;
    ULONG SequenceId;
    ULONG X;
    ULONG Y;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
    ULONG Reserved3;
} VHID_MOVE_ABSOLUTE_REQUEST, *PVHID_MOVE_ABSOLUTE_REQUEST;

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
    ULONG SmokeSequenceCompleted;
    ULONG CurrentCommandType;
    ULONG CurrentCommandSequenceId;
    ULONG LastCommandType;
    ULONG LastCommandSequenceId;
    LONG LastCommandStatus;
} VHID_STATUS_REPORT, *PVHID_STATUS_REPORT;
