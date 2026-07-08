# Windows VHID Stack

Windows VHID Stack is an initial Windows KMDF/VHF virtual HID driver source skeleton for virtual mouse and keyboard input.

Current status: source skeleton with minimal VHF lifecycle, HID descriptor, and callback-paced all-zero neutral report submission. This repository is not ready to sign, install, load, or generate input.

## Safety Boundary

- No host-machine driver testing.
- No generated keyboard or mouse input from the current source skeleton.
- No driver/sample build without explicit approval.
- No signing, certificate changes, TESTSIGNING changes, reboot, install/load/unload, driver-store operations, or non-neutral report writes without explicit approval.
- Runtime testing should happen only in an isolated VM/lab environment after source review and an approved public push.

## Repository Layout

```text
src/driver/           KMDF/VHF driver skeleton with descriptor and neutral reports
src/shared/           Shared inert protocol identity constants
tools/proof-client/   Inert proof-client placeholder
tests/                Placeholder README files for future tests
```

## Current Code Boundary

The driver skeleton intentionally has no active generated-input path:

- `VhfReadReportSubmit` is limited to one-shot all-zero keyboard and mouse reports paced by VHF ready callbacks;
- no non-neutral key, button, movement, or wheel report builders;
- no IOCTL queue for HID reports;
- no user-mode client that opens the driver;
- no click/type behavior;
- no install/load scripts.

Future work must add separate command-send vs observed-effect logging and explicit VM-only approval before any non-neutral write-capable proof.
