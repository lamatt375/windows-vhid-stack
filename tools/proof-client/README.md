# Proof Client

This directory contains a minimal VM-only smoke trigger helper.

Current boundary:

- opens the fixed windows-vhid-stack device interface;
- accepts exactly two verbs: `trigger` and `status`;
- `trigger` sends exactly one no-payload IOCTL to request the driver's built-in fixed smoke sequence;
- `status` sends exactly one read-only no-input IOCTL and prints the fixed driver state snapshot;
- does not accept key codes, text, coordinates, report bytes, click flags, repeat counts, or arbitrary commands;
- does not move/click/type through any user-mode input API.

The helper is for a later human-approved isolated VM smoke run only. It does not sign, install, load, configure, or test the driver by itself.
