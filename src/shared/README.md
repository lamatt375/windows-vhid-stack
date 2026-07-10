# Shared Sources

This directory contains neutral shared definitions used by the driver, tests, and minimal helper tools.

Current contents:

- `VhidHidDescriptor.h`: static HID report descriptor used by the driver and descriptor validation.
- `VhidProtocol.h`: project identity, device-interface GUID, one no-payload smoke trigger IOCTL, one fixed read-only status IOCTL, one fixed `MoveAbsolute` IOCTL with normalized 0..32767 coordinates, and one fixed left-only `ClickAbsolute` IOCTL with normalized 0..32767 coordinates.

The trigger IOCTL does not carry HID reports, key codes, text, coordinates, clicks, repeats, or arbitrary commands. The status IOCTL accepts no input and only returns the fixed driver state snapshot. The `MoveAbsolute` IOCTL accepts only the fixed request structure and rejects raw HID reports, click/buttons, text, repeats, batches, macros, and unsupported command types. The `ClickAbsolute` IOCTL accepts only the fixed request structure and performs one left click only; it rejects raw HID reports, arbitrary buttons, text, repeats, batches, macros, and unsupported command types.
