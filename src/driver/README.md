# Driver Skeleton

This directory contains the initial KMDF/VHF driver skeleton with a minimal HID descriptor and fixed callback-paced report submission.

Current boundaries:

- VHF report submission is limited to a fixed seven-report sequence;
- the only keyboard press is one `A` press followed by keyboard release;
- the only mouse movement is one relative X +1 report followed by mouse neutral clear;
- no mouse button, click, arbitrary movement, or wheel reports;
- no arbitrary key API or repeated key path;
- no IOCTL queue accepting input reports;
- no install/load instructions;
- no build approval implied by these files.

Current lifecycle scope:

- creates and starts a VHF device during KMDF device creation;
- exposes a static keyboard and relative mouse HID descriptor for validation;
- arms one fixed report sequence after VHF start;
- submits keyboard neutral pre-clear, mouse neutral pre-clear, keyboard `A` press, keyboard release, mouse move right by one unit, mouse neutral post-clear, and keyboard neutral final clear only from VHF ready callbacks;
- disables report submission after sequence completion or failure;
- synchronizes cleanup against active report submission before deleting the VHF device.

Before any approved isolated VM execution, use a known keyboard layout, keep Caps Lock off, disable IME/composition, focus a blank disposable text field, submit press/release without observation delay while the key is down, and fail on uppercase, wrong, composed, repeated, or multiple characters.
