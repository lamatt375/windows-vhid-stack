# Dev/Test Packaging Helpers

This folder contains conservative helpers for reviewed isolated VM development. They are not production installers.

Prefer the `.cmd` wrappers on Windows. They launch the matching PowerShell script with a process-local execution-policy bypass so a default Windows policy does not block local dev scripts.

## Scripts

- `Install-VhidDev.cmd` / `Install-VhidDev.ps1` perform preflight checks, stage a clean package directory, regenerate the catalog there, sign the staged package with an existing test certificate, install/update from the staged INF, and run `proof-client status`.
- `Uninstall-VhidDev.cmd` / `Uninstall-VhidDev.ps1` list matching devices and driver packages by default, and remove only explicit selected targets when `-Apply` and removal switches are supplied.
- `Test-VhidStatus.cmd` / `Test-VhidStatus.ps1` run read-only device inventory and `proof-client status`. They do not send move, click, keytap, trigger, or raw HID commands.

## Safety Defaults

- `Install-VhidDev.ps1` and `Uninstall-VhidDev.ps1` default to dry-run mode. Use `-Apply` only in an approved isolated dev/test VM after reviewing the logged plan.
- Logs are written under `C:\vhid-lab\logs` by default.
- Install staging uses `C:\vhid-lab\pkg\windows-vhid-stack` by default. `-PackageRoot` must be an absolute child directory under `C:\vhid-lab\pkg\`, not the staging parent itself.
- The expected root-enumerated device instance is `ROOT\WINDOWSVHIDSTACKVIRTUALINPUT\0000`.
- Mutating actions require explicit parameters and are guarded by PowerShell `ShouldProcess`.
- `Install-VhidDev.ps1` copies only `VirtualInput.inf` and the built `VirtualInput.sys` into the staging directory before running Inf2Cat, so build output folders under `src\driver` are not scanned as package inputs.
- These scripts do not create certificates, enable TESTSIGNING, reboot, or run generated input tests.

## Typical Reviewed VM Flow

```powershell
.\tools\dev\Install-VhidDev.cmd
.\tools\dev\Install-VhidDev.cmd -Apply
.\tools\dev\Test-VhidStatus.cmd
.\tools\dev\Uninstall-VhidDev.cmd
.\tools\dev\Uninstall-VhidDev.cmd -RemovePackage -PublishedName oemNN.inf -Apply
```

Review the dry-run logs and replace `oemNN.inf` only after confirming it is the Windows VHID Stack package selected for removal.

The .cmd wrappers are the portable Windows entrypoint.
