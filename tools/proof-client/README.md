# Proof Client

This directory contains a minimal VM-only helper for the windows-vhid-stack device interface.

Current boundary:

- opens the fixed windows-vhid-stack device interface;
- accepts exactly five verbs: `trigger`, `status`, `move-abs <x> <y>`, `click-abs <x> <y>`, and `keytap <key>`;
- `trigger` sends exactly one no-payload IOCTL to request the driver's built-in fixed smoke sequence;
- `status` sends exactly one read-only no-input IOCTL and prints the fixed driver state and receipt snapshot;
- `move-abs` sends exactly one fixed `MoveAbsolute` request with normalized coordinates from 0 through 32767;
- `click-abs` sends exactly one fixed `ClickAbsolute` request with normalized coordinates from 0 through 32767;
- `keytap` sends exactly one fixed `KeyTap` request for one printable US ASCII key or one named key: `space`, `enter`, `esc`, `escape`, `tab`, `backspace`, or `bksp`;
- does not accept raw HID reports, raw key usages, text strings, arbitrary button values, repeat counts, batches, macros, or arbitrary commands;
- does not move/click/type through any user-mode input API.

The helper is for a later human-approved isolated VM run only. It does not sign, install, load, configure, or test the driver by itself.

`status` reports driver-side command acceptance, submission, release/neutral, receipt, and health evidence. It does not prove application-level or UI-level semantic success. The request `SequenceId` values sent by this helper are diagnostic only; the driver-owned `receiptId` fields are the authoritative accepted-command receipts. Rejected attempts are printed separately from accepted receipt identity.