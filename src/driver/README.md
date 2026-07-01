# Driver Skeleton

This directory contains the initial no-report KMDF/VHF driver skeleton.

Current boundaries:

- no active HID report-submission path;
- no IOCTL queue accepting input reports;
- no install/load instructions;
- no build approval implied by these files.

Future work will add descriptor creation, VHF lifecycle, and report submission only after review and VM-only approval gates open.
