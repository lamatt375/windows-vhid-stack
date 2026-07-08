# Driver Skeleton

This directory contains the initial KMDF/VHF driver skeleton with a minimal HID descriptor and neutral report submission.

Current boundaries:

- VHF report submission is limited to all-zero neutral keyboard and mouse reports;
- no non-neutral key, button, movement, or wheel reports;
- no IOCTL queue accepting input reports;
- no install/load instructions;
- no build approval implied by these files.

Current lifecycle scope:

- creates and starts a VHF device during KMDF device creation;
- exposes a static keyboard and relative mouse HID descriptor for validation;
- arms one neutral clear-state operation after VHF start;
- submits keyboard neutral and mouse neutral reports only from VHF ready callbacks;
- deletes the VHF device during cleanup.

Future work will add non-neutral report submission only after review and VM-only approval gates open.
