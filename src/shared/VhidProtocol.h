#pragma once

#define VHID_PROTOCOL_VERSION_MAJOR 0u
#define VHID_PROTOCOL_VERSION_MINOR 7u
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
 * The click-absolute IOCTL accepts exactly one fixed request structure with
 * normalized 0..32767 coordinates and performs one left click only. It does
 * not accept button selection, raw HID report bytes, text, repeats, batches,
 * or alternate commands.
 *
 * The key-tap IOCTL accepts exactly one fixed request structure with one
 * validated key code. The driver maps that key code to USB HID keyboard usage
 * bytes internally. It does not accept raw HID report bytes, strings, repeats,
 * batches, shortcuts, or alternate commands.
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

#define VHID_IOCTL_CLICK_ABSOLUTE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_WRITE_DATA)

#define VHID_IOCTL_KEY_TAP \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_WRITE_DATA)

#define VHID_COMMAND_NONE 0u
#define VHID_COMMAND_SMOKE_SEQUENCE 1u
#define VHID_COMMAND_MOVE_ABSOLUTE 2u
#define VHID_COMMAND_CLICK_ABSOLUTE 3u
#define VHID_COMMAND_KEY_TAP 4u

#define VHID_COMMAND_CAPABILITY_SMOKE_SEQUENCE 0x00000001u
#define VHID_COMMAND_CAPABILITY_MOVE_ABSOLUTE 0x00000002u
#define VHID_COMMAND_CAPABILITY_CLICK_ABSOLUTE 0x00000004u
#define VHID_COMMAND_CAPABILITY_KEY_TAP 0x00000008u
#define VHID_COMMAND_CAPABILITY_MASK ( \
    VHID_COMMAND_CAPABILITY_SMOKE_SEQUENCE | \
    VHID_COMMAND_CAPABILITY_MOVE_ABSOLUTE | \
    VHID_COMMAND_CAPABILITY_CLICK_ABSOLUTE | \
    VHID_COMMAND_CAPABILITY_KEY_TAP)

#define VHID_MOVE_ABSOLUTE_COORDINATE_MIN 0u
#define VHID_MOVE_ABSOLUTE_COORDINATE_MAX 32767u

#define VHID_KEY_TAP_KEY_ENTER 0x100u
#define VHID_KEY_TAP_KEY_ESCAPE 0x101u
#define VHID_KEY_TAP_KEY_TAB 0x102u
#define VHID_KEY_TAP_KEY_BACKSPACE 0x103u

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

typedef struct _VHID_CLICK_ABSOLUTE_REQUEST {
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
} VHID_CLICK_ABSOLUTE_REQUEST, *PVHID_CLICK_ABSOLUTE_REQUEST;

typedef struct _VHID_KEY_TAP_REQUEST {
    ULONG Size;
    ULONG ProtocolVersionMajor;
    ULONG ProtocolVersionMinor;
    ULONG CommandType;
    ULONG SequenceId;
    ULONG KeyCode;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
    ULONG Reserved3;
} VHID_KEY_TAP_REQUEST, *PVHID_KEY_TAP_REQUEST;

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
    ULONG SupportedCommandMask;
    ULONG CurrentReceiptId;
    ULONG LastReceiptId;
    LONG LastCommandAcceptStatus;
    LONG LastCommandReleaseStatus;
    LONG LastCommandReleaseRetryStatus;
    ULONG LastCommandReleaseRetryAttempted;
    ULONG LastCommandReleaseRetrySucceeded;
    ULONG LastCommandFinalNeutralKnown;
    ULONG LastRejectedCommandType;
    ULONG LastRejectedCommandSequenceId;
    LONG LastRejectedCommandStatus;
} VHID_STATUS_REPORT, *PVHID_STATUS_REPORT;
