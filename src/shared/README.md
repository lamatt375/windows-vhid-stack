# Shared Sources

This directory contains neutral shared definitions used by the driver, tests, and minimal helper tools.

Current contents:

- `VhidHidDescriptor.h`: static HID report descriptor used by the driver and descriptor validation.
- `VhidProtocol.h`: project identity, device-interface GUID, one no-payload smoke trigger IOCTL, one fixed read-only status IOCTL, one fixed `MoveAbsolute` IOCTL with normalized 0..32767 coordinates, one fixed left-only `ClickAbsolute` IOCTL with normalized 0..32767 coordinates, and one fixed `KeyTap` IOCTL with a validated single-key code.

The trigger IOCTL does not carry HID reports, key codes, text, coordinates, clicks, repeats, or arbitrary commands. The status IOCTL accepts no input and only returns the fixed driver state and receipt snapshot. The `MoveAbsolute` IOCTL accepts only the fixed request structure and rejects raw HID reports, click/buttons, text, repeats, batches, macros, and unsupported command types. The `ClickAbsolute` IOCTL accepts only the fixed request structure and performs one left click only; it rejects raw HID reports, arbitrary buttons, text, repeats, batches, macros, and unsupported command types. The `KeyTap` IOCTL accepts only the fixed request structure and a validated key code; the driver maps that code to USB HID keyboard usage bytes internally and rejects raw HID reports, text strings, repeats, shortcuts, batches, macros, and unsupported command types.

## Receipt Snapshot

The fixed status structure reports driver-side command evidence only. It does not report whether any application or UI consumed an input effect.

- `SupportedCommandMask` is a bitmask of the typed commands compiled into this driver.
- Request `SequenceId` fields are client-provided diagnostic IDs. The driver accepts zero and duplicate values and echoes the current/last value for correlation.
- `CurrentReceiptId` and `LastReceiptId` are driver-owned IDs. A nonzero receipt is assigned only after a command is accepted for submission. Receipt IDs increase monotonically and skip zero after wrap.
- `LastCommandAcceptStatus` records accept status for the last accepted/receipted command.
- `LastCommandStatus` and `LastReportSubmitStatus` record final command/report submission status for the accepted receipt.
- `LastRejectedCommandType`, `LastRejectedCommandSequenceId`, and `LastRejectedCommandStatus` record the latest rejected attempt separately from accepted receipt identity.
- Release fields record whether the driver believes a final neutral state is known and whether an emergency neutral retry was attempted or succeeded.
Receipt consistency scenario: if command A is accepted and still active, then command B is rejected as busy or invalid, command B updates only the last-rejected fields. When command A completes, the driver restores A's current command type, client diagnostic `SequenceId`, and driver receipt ID into the accepted last-command receipt fields before clearing current state.