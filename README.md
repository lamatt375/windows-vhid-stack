# Windows VHID Stack

Windows VHID Stack is an initial Windows KMDF/VHF virtual HID driver source skeleton for virtual mouse and keyboard input.

Current status: source skeleton with minimal VHF lifecycle, HID descriptor, one no-payload manual trigger, and a callback-paced fixed report sequence for a VM-only smoke proof. This repository is not ready to sign, install, load, or use outside an explicitly approved isolated VM test.

## Safety Boundary

- No host-machine driver testing.
- No generated keyboard or mouse input outside an explicitly approved isolated VM test.
- No arbitrary or repeated input generation.
- No driver/sample build without explicit approval.
- No signing, certificate changes, TESTSIGNING changes, reboot, install/load/unload, driver-store operations, or expanded report-write behavior without explicit approval.
- Runtime testing should happen only in an isolated VM/lab environment after source review and an approved public push.

## Repository Layout

```text
src/driver/           KMDF/VHF driver skeleton with descriptor and fixed reports
src/shared/           Shared descriptor and fixed trigger identity
tools/proof-client/   Minimal status, smoke trigger, move-abs, click-abs, and keytap helper
tests/                Descriptor validation and placeholder README files
```

## Current Code Boundary

The driver skeleton intentionally has only a narrow fixed trigger and report path:

- one device interface exposes one no-payload IOCTL that kickstarts the fixed smoke sequence with the first neutral report; later VHF ready callbacks submit reports 2-7;
- `VhfReadReportSubmit` is limited to one callback-paced sequence: keyboard neutral pre-clear, mouse neutral pre-clear, keyboard `A` press, keyboard release, mouse move right by one unit, mouse neutral post-clear, and keyboard neutral final clear;
- one fixed `KeyTap(key)` command can submit a single key-down plus neutral key-up using a driver-owned basic US HID keyboard mapping;
- no TypeText/string command, Ctrl/Alt/Win shortcut command, repeat count, raw HID key payload, or multi-key macro behavior;
- no arbitrary movement report, wheel report builder, or general button report builder;
- one fixed `ClickAbsolute(x,y)` command can submit a left-button click at normalized absolute coordinates after explicit proof-client request;
- no right click, double click, drag, wheel, arbitrary button values, or click repeat behavior;
- no IOCTL carrying HID report bytes, raw key usages, text strings, arbitrary payloads, repeat counts, or arbitrary commands;
- no user-mode input fallback;
- no install/load scripts.

Before any approved VM execution, use a known keyboard layout, keep Caps Lock off, disable IME/composition, focus a blank disposable text field, submit press/release without observation delay while the key is down, and fail the test on uppercase, wrong, composed, repeated, or multiple characters.

Future work must add separate command-send vs observed-effect logging and explicit VM-only approval before any broader write-capable proof.
