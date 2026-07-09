# Proof Client

This directory contains a minimal VM-only helper for the windows-vhid-stack device interface.

Current boundary:

- opens the fixed windows-vhid-stack device interface;
- accepts exactly three verbs: `trigger`, `status`, and `move-abs <x> <y>`;
- `trigger` sends exactly one no-payload IOCTL to request the driver's built-in fixed smoke sequence;
- `status` sends exactly one read-only no-input IOCTL and prints the fixed driver state snapshot;
- `move-abs` sends exactly one fixed `MoveAbsolute` request with normalized coordinates from 0 through 32767;
- does not accept raw HID reports, key codes, text, click flags, repeat counts, batches, macros, or arbitrary commands;
- does not move/click/type through any user-mode input API.

The helper is for a later human-approved isolated VM run only. It does not sign, install, load, configure, or test the driver by itself.
