# Dev/Test Packaging Helpers

This folder contains conservative PowerShell helpers for reviewed isolated VM development. They are not production installers.

## Scripts

- `Install-VhidDev.ps1` performs preflight checks, then can regenerate the catalog, sign the built package with an existing test certificate, install/update the package, and run `proof-client status`.
- `Uninstall-VhidDev.ps1` lists matching devices and driver packages by default, and removes only explicit selected targets when `-Apply` and removal switches are supplied.
- `Test-VhidStatus.ps1` runs read-only device inventory and `proof-client status`. It does not send move, click, keytap, trigger, or raw HID commands.

## Safety Defaults

- `Install-VhidDev.ps1` and `Uninstall-VhidDev.ps1` default to dry-run mode. Use `-Apply` only in an approved isolated dev/test VM after reviewing the logged plan.
- Logs are written under `C:\vhid-lab\logs` by default.
- The expected root-enumerated device instance is `ROOT\WINDOWSVHIDSTACKVIRTUALINPUT\0000`.
- Mutating actions require explicit parameters and are guarded by PowerShell `ShouldProcess`.
- These scripts do not create certificates, enable TESTSIGNING, reboot, or run generated input tests.

## Typical Reviewed VM Flow

```powershell
.\tools\dev\Install-VhidDev.ps1
.\tools\dev\Install-VhidDev.ps1 -Apply
.\tools\dev\Test-VhidStatus.ps1
.\tools\dev\Uninstall-VhidDev.ps1
.\tools\dev\Uninstall-VhidDev.ps1 -RemovePackage -PublishedName oemNN.inf -Apply
```

Review the dry-run logs and replace `oemNN.inf` only after confirming it is the Windows VHID Stack package selected for removal.
