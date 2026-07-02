# Windows VHID Stack

Windows VHID Stack is an initial Windows KMDF/VHF virtual HID driver source skeleton for virtual mouse and keyboard input.

Current status: source skeleton with minimal VHF lifecycle and HID descriptor. This repository is not ready to sign, install, load, or generate input.

## Safety Boundary

- No host-machine driver testing.
- No generated keyboard or mouse input from the current source skeleton.
- No driver/sample build without explicit approval.
- No signing, certificate changes, TESTSIGNING changes, reboot, install/load/unload, driver-store operations, or report writes without explicit approval.
- Runtime testing should happen only in an isolated VM/lab environment after source review and an approved public push.

## Repository Layout

```text
src/driver/           No-report KMDF/VHF driver skeleton with descriptor
src/shared/           Shared inert protocol identity constants
tools/proof-client/   Inert proof-client placeholder
tests/                Placeholder README files for future tests
```

## Current Code Boundary

The driver skeleton intentionally has no active report-submission path:

- no `VhfReadReportSubmit` call;
- no IOCTL queue for HID reports;
- no user-mode client that opens the driver;
- no click/type behavior;
- no install/load scripts.

Future work must add read-only descriptor validation, readiness checks, release-all behavior, and separate command-send vs observed-effect logging before any write-capable proof.
