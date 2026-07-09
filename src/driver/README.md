# Driver Skeleton

This directory contains the initial KMDF/VHF driver skeleton with a minimal HID descriptor, one no-payload manual trigger, and fixed callback-paced report submission.

Current boundaries:

- VHF report submission is limited to a fixed seven-report sequence;
- the sequence is not auto-fired after VHF start;
- one no-payload IOCTL can arm the fixed sequence once; later VHF ready callbacks submit each report;
- the only keyboard press is one `A` press followed by keyboard release;
- the only mouse movement is one relative X +1 report followed by mouse neutral clear;
- no mouse button, click, arbitrary movement, or wheel reports;
- no arbitrary key API or repeated key path;
- no IOCTL queue accepting HID report bytes, key codes, text, coordinates, click flags, repeat counts, or arbitrary commands;
- no install/load instructions;
- no build approval implied by these files.

Current lifecycle scope:

- creates and starts a VHF device during KMDF device creation;
- exposes a static keyboard and relative mouse HID descriptor for validation;
- exposes one device interface for the fixed manual trigger;
- records VHF readiness from `EvtVhfReadyForNextReadReport`;
- submits keyboard neutral pre-clear, mouse neutral pre-clear, keyboard `A` press, keyboard release, mouse move right by one unit, mouse neutral post-clear, and keyboard neutral final clear only after readiness is observed;
- disables report submission after sequence completion or failure;
- synchronizes cleanup against active report submission before deleting the VHF device.

Before any approved isolated VM execution, use a known keyboard layout, keep Caps Lock off, disable IME/composition, focus a blank disposable text field, submit press/release without observation delay while the key is down, and fail on uppercase, wrong, composed, repeated, or multiple characters.
