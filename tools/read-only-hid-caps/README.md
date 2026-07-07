# Read-only HID capability inspector

`Inspect-VhidCaps.ps1` enumerates present HID device interfaces, filters to VHF-backed interfaces by default, opens each matching interface with desired access `0`, and prints HID capabilities.

It does not send HID reports, read input reports, write output reports, create devices, install drivers, or change system configuration.

Run from an elevated or normal PowerShell session:

```powershell
powershell -ExecutionPolicy Bypass -File .\Inspect-VhidCaps.ps1
```

To list every present HID interface:

```powershell
powershell -ExecutionPolicy Bypass -File .\Inspect-VhidCaps.ps1 -AllHid
```
