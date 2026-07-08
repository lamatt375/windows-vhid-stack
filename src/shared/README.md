# Shared Sources

This directory contains neutral shared definitions used by the driver, tests, and minimal helper tools.

Current contents:

- `VhidHidDescriptor.h`: static HID report descriptor used by the driver and descriptor validation.
- `VhidProtocol.h`: project identity, device-interface GUID, and one no-payload smoke trigger IOCTL.

The trigger IOCTL does not carry HID reports, key codes, text, coordinates, clicks, repeats, or arbitrary commands.
