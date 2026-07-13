# Windows VHID Stack Dev Install Guide

This is the step-by-step dev/test installation flow for a clean Windows VM or Windows dev machine.

This is not a production installer. It uses TESTSIGNING and a local dev/test certificate.

## What You Need

- Windows VM or dev machine.
- Administrator access.
- Enterprise WDK (EWDK) ISO.
- Internet access to download the source zip.
- Default lab folder: `C:\vhid-lab`.

Current tested commit:

```text
0602dbce11e37ddac3858493d072b16bf97c7b96
```

Short name used below:

```text
0602dbc
```

## 1. Download EWDK

Download Enterprise WDK from Microsoft:

https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk

Direct Microsoft EWDK redirect used during this guide:

https://go.microsoft.com/fwlink/?linkid=2322845

After download, mount the ISO.

## 2. Enable TESTSIGNING

Run in **Admin PowerShell** or **Admin EWDK shell**:

```cmd
bcdedit /enum | findstr /i testsigning
```

If it already shows this, continue:

```text
testsigning              Yes
```

If it does not show `Yes`, run:

```cmd
bcdedit /set testsigning on
shutdown /r /t 0
```

After reboot, verify again:

```cmd
bcdedit /enum | findstr /i testsigning
```

Continue only when it shows:

```text
testsigning              Yes
```

## 3. Open EWDK Shell

Open the mounted EWDK ISO.

Run as Administrator:

```cmd
LaunchBuildEnv.cmd
```

All remaining build/install commands should be run from this **Admin EWDK shell** unless a step says PowerShell.

## 4. Confirm EWDK Tools

Run in **Admin EWDK shell**:

```cmd
where msbuild
where Inf2Cat
where signtool
```

Each command should print a path.

Now find DevCon:

```cmd
set EWDK=E:\
set DEVCON=

for /f "delims=" %F in ('dir /s /b "%EWDK%devcon.exe" ^| findstr /i "\\x64\\devcon.exe$"') do set DEVCON=%F

echo DEVCON=%DEVCON%
```

Expected shape:

```text
DEVCON=E:\Program Files\Windows Kits\10\Tools\10.0.28000.0\x64\devcon.exe
```

If your EWDK ISO is not mounted as `E:`, change this line and rerun the block:

```cmd
set EWDK=E:\
```

Do not copy `devcon.exe`. Just keep the `DEVCON` variable set.

## 5. Download Source

Run in **Admin PowerShell**:

```powershell
$commit = "0602dbce11e37ddac3858493d072b16bf97c7b96"
$short = "0602dbc"
$root = "C:\vhid-lab"
$zip = "$root\windows-vhid-stack-$short.zip"
$src = "$root\windows-vhid-stack-$short"
$expanded = "$root\windows-vhid-stack-$commit"

New-Item -ItemType Directory -Force -Path $root | Out-Null
Remove-Item -LiteralPath $src -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $expanded -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue

Invoke-WebRequest `
  -Uri "https://github.com/lamatt375/windows-vhid-stack/archive/$commit.zip" `
  -OutFile $zip

Expand-Archive -Path $zip -DestinationPath $root -Force
Rename-Item -Path $expanded -NewName "windows-vhid-stack-$short"

Select-String -Path "$src\src\driver\VirtualInput.inf" -Pattern "DriverVer"
Get-ChildItem "$src\tools\dev" | Format-Table Name,Length,LastWriteTime
```

Expected:

```text
DriverVer=07/10/2026,0.0.7.0
```

Also confirm these files are listed:

```text
New-VhidDevCert.cmd
Install-VhidDev.cmd
Test-VhidStatus.cmd
Uninstall-VhidDev.cmd
```

## 6. Build Driver And Proof Client

Run in **Admin EWDK shell**:

```cmd
set SRC=C:\vhid-lab\windows-vhid-stack-0602dbc

cd /d %SRC%

msbuild src\driver\VirtualInput.vcxproj /p:Configuration=Debug /p:Platform=x64 /v:minimal /fl /flp:logfile=C:\vhid-lab\logs\driver-build-0602dbc.log;verbosity=diagnostic

msbuild tools\proof-client\proof-client.vcxproj /p:Configuration=Debug /p:Platform=x64 /v:minimal /fl /flp:logfile=C:\vhid-lab\logs\proof-client-build-0602dbc.log;verbosity=diagnostic

dir %SRC%\src\driver\x64\Debug\VirtualInput.sys
dir %SRC%\tools\proof-client\x64\Debug\proof-client.exe
```

Expected:

- `VirtualInput.sys` exists.
- `proof-client.exe` exists.
- Both builds exit successfully.

## 7. Create Dev Certificate

Run in **VM Admin PowerShell** or **Admin EWDK shell**. This step imports the public certificate into LocalMachine Root and TrustedPublisher using `certutil`, so the shell must be elevated:

```cmd
set SRC=C:\vhid-lab\windows-vhid-stack-0602dbc

cd /d %SRC%

tools\dev\New-VhidDevCert.cmd -LogRoot C:\vhid-lab\logs
echo cert-dry-run exit=%ERRORLEVEL%

tools\dev\New-VhidDevCert.cmd -LogRoot C:\vhid-lab\logs -Apply
echo cert-apply exit=%ERRORLEVEL%
```

Expected:

```text
cert-dry-run exit=0
cert-apply exit=0
```

## 8. Install Driver

Run in **Admin EWDK shell**.

If the shell was reopened, set `SRC` and `DEVCON` again:

```cmd
set SRC=C:\vhid-lab\windows-vhid-stack-0602dbc
set EWDK=E:\
set DEVCON=

for /f "delims=" %F in ('dir /s /b "%EWDK%devcon.exe" ^| findstr /i "\\x64\\devcon.exe$"') do set DEVCON=%F

echo DEVCON=%DEVCON%
```

Then run:

```cmd
cd /d %SRC%

tools\dev\Install-VhidDev.cmd -RepoRoot %SRC% -LogRoot C:\vhid-lab\logs -DevConPath "%DEVCON%"
echo install-dry-run exit=%ERRORLEVEL%

tools\dev\Install-VhidDev.cmd -RepoRoot %SRC% -LogRoot C:\vhid-lab\logs -DevConPath "%DEVCON%" -Apply
echo install-apply exit=%ERRORLEVEL%
```

Expected:

```text
install-dry-run exit=0
install-apply exit=0
```

## 9. Verify Status

Run in **Admin EWDK shell**:

```cmd
set SRC=C:\vhid-lab\windows-vhid-stack-0602dbc

cd /d %SRC%

tools\dev\Test-VhidStatus.cmd -RepoRoot %SRC% -LogRoot C:\vhid-lab\logs
echo status-helper exit=%ERRORLEVEL%

%SRC%\tools\proof-client\x64\Debug\proof-client.exe status
echo proof-client exit=%ERRORLEVEL%

pnputil /enum-devices /problem
```

Expected:

```text
protocol=0.7
supportedCommandMask=0xF
status-helper exit=0
proof-client exit=0
No devices were found on the system.
```

## 10. Verify Device Rows

Run in **Admin PowerShell**:

```powershell
Get-PnpDevice -Class HIDClass |
  Where-Object {
    $_.FriendlyName -like "*VHID*" -or
    $_.FriendlyName -like "*Virtual HID*" -or
    $_.InstanceId -like "ROOT*" -or
    $_.InstanceId -like "VHF*"
  } |
  Format-Table Status,Class,InstanceId,FriendlyName -AutoSize
```

Expected:

- `Windows VHID Stack Virtual HID Device` appears.
- Status is `OK`.
- Extra VHF child HID rows may appear.

## 11. Verify Package Inventory

Run in **Admin PowerShell**:

```powershell
pnputil /enum-drivers |
  Select-String -Pattern "Published Name|Original Name|Provider Name|Class Name|Driver Version|Signer Name|virtualinput.inf|Windows VHID Stack" -Context 0,4

pnputil /enum-devices /drivers |
  Select-String -Pattern "Windows VHID Stack|virtualinput.inf|Driver Name|Driver Version|Best Ranked|Outranked" -Context 0,4
```

Expected:

- `Original Name: virtualinput.inf`
- `Provider Name: Windows VHID Stack`
- `Driver Version: 07/10/2026 0.0.7.0`
- Selected package is best ranked / installed.

## 12. Uninstall List Only

Run in **Admin EWDK shell**:

```cmd
set SRC=C:\vhid-lab\windows-vhid-stack-0602dbc

cd /d %SRC%

tools\dev\Uninstall-VhidDev.cmd -LogRoot C:\vhid-lab\logs
echo uninstall-list exit=%ERRORLEVEL%
```

Expected:

```text
uninstall-list exit=0
```

No removal happens without `-Apply` and explicit selectors.

## 13. Create Checkpoint After Success

Run from **Host Admin PowerShell**:

```powershell
Checkpoint-VM `
  -Name "Windows-VHID-Dev-VM" `
  -SnapshotName "Windows VHID Stack dev install - 0602dbc healthy"

Get-VMSnapshot -VMName "Windows-VHID-Dev-VM" |
  Sort-Object CreationTime |
  Format-Table VMName,Name,CreationTime
```

## Logs

Logs are under:

```text
C:\vhid-lab\logs
```

Important logs:

```text
driver-build-0602dbc.log
proof-client-build-0602dbc.log
New-VhidDevCert-*.log
Install-VhidDev-*.log
Test-VhidStatus-*.log
Uninstall-VhidDev-*.log
```
