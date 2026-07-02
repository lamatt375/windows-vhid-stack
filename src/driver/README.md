# Driver Skeleton

This directory contains the initial no-report KMDF/VHF driver skeleton with a minimal HID descriptor.

Current boundaries:

- no active HID report-submission path;
- no IOCTL queue accepting input reports;
- no install/load instructions;
- no build approval implied by these files.

Current lifecycle scope:

- creates and starts a VHF device during KMDF device creation;
- exposes a static keyboard and relative mouse HID descriptor for future read-only validation;
- deletes the VHF device during cleanup.

Future work will add report submission only after review and VM-only approval gates open.
