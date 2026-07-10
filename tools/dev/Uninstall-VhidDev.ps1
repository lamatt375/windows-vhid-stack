#requires -Version 5.1

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$DeviceInstanceId = 'ROOT\WINDOWSVHIDSTACKVIRTUALINPUT\0000',
    [string]$PublishedName,
    [string]$LogRoot = 'C:\vhid-lab\logs',
    [switch]$Apply,
    [switch]$RemoveDevice,
    [switch]$RemovePackage,
    [switch]$Force
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$DryRun = -not $Apply
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$script:LogPath = Join-Path $LogRoot ("Uninstall-VhidDev-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

function Write-Log {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "{0} {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Message
    Write-Host $line
    Add-Content -LiteralPath $script:LogPath -Value $line
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$MutatesSystem,
        [switch]$AllowNonZeroExit
    )

    $display = "$FilePath $($Arguments -join ' ')"
    if ($MutatesSystem -and $DryRun) {
        Write-Log "DRY-RUN: would run: $display"
        return 0
    }

    if ($MutatesSystem -and -not $PSCmdlet.ShouldProcess($display, 'Run dev/test uninstall action')) {
        Write-Log "Skipped by ShouldProcess: $display"
        return 0
    }

    Write-Log "RUN: $display"
    $output = & $FilePath @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) { Write-Log "  $line" }
    Write-Log "ExitCode=$exitCode"
    if ($exitCode -ne 0 -and -not $AllowNonZeroExit) {
        throw "Command failed with exit code $exitCode`: $display"
    }
    return $exitCode
}

function Test-PublishedName {
    param([Parameter(Mandatory = $true)][string]$Name)
    return $Name -match '^oem\d+\.inf$'
}

function Get-DriverPackageInventory {
    Write-Log 'Reading driver package inventory for package identity verification.'
    $output = & pnputil.exe /enum-drivers 2>&1
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) { Write-Log "  $line" }
    Write-Log "PackageInventoryExitCode=$exitCode"
    if ($exitCode -ne 0) { throw "pnputil /enum-drivers failed with exit code $exitCode." }

    $packages = @()
    $current = [ordered]@{}
    foreach ($line in $output) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            if ($current.Count -gt 0) {
                $packages += [pscustomobject]$current
                $current = [ordered]@{}
            }
            continue
        }

        if ($line -match '^\s*([^:]+?)\s*:\s*(.*?)\s*$') {
            $key = ($matches[1] -replace '\s+', '')
            $current[$key] = $matches[2]
        }
    }
    if ($current.Count -gt 0) { $packages += [pscustomobject]$current }
    return $packages
}

function Assert-VhidPackagePublishedName {
    param([Parameter(Mandatory = $true)][string]$Name)

    $matches = @(Get-DriverPackageInventory | Where-Object {
        $_.PSObject.Properties['PublishedName'] -and
        $_.PublishedName -ieq $Name
    })

    if ($matches.Count -ne 1) {
        throw "Expected exactly one driver package named '$Name', found $($matches.Count)."
    }

    $package = $matches[0]
    $originalName = if ($package.PSObject.Properties['OriginalName']) { $package.OriginalName } else { '' }
    $providerName = if ($package.PSObject.Properties['ProviderName']) { $package.ProviderName } else { '' }
    $className = if ($package.PSObject.Properties['ClassName']) { $package.ClassName } else { '' }

    Write-Log "SelectedPackage PublishedName=$($package.PublishedName) OriginalName=$originalName ProviderName=$providerName ClassName=$className"

    if ($originalName -ine 'VirtualInput.inf') {
        throw "Refusing package removal: '$Name' Original Name is '$originalName', expected 'VirtualInput.inf'."
    }
    if ($providerName -ine 'Windows VHID Stack') {
        throw "Refusing package removal: '$Name' Provider Name is '$providerName', expected 'Windows VHID Stack'."
    }
    if ($className -and $className -ine 'HIDClass' -and $className -ine 'Human Interface Devices') {
        throw "Refusing package removal: '$Name' Class Name is '$className', expected HIDClass/HID."
    }
}

Write-Log 'Windows VHID Stack dev/test uninstall helper starting.'
Write-Log 'WARNING: This script lists by default. Removal requires -Apply plus explicit selectors.'
Write-Log "Mode=$(@{ $true='DRY-RUN'; $false='APPLY' }[$DryRun]) Log=$script:LogPath"
Write-Log "ExpectedDeviceInstanceId=$DeviceInstanceId"
Write-Log "PublishedName=$PublishedName"

$admin = Test-Administrator
Write-Log "Administrator=$admin"
if ($Apply -and -not $admin) { throw 'Apply mode requires an elevated PowerShell session.' }

Write-Log 'Current matching device inventory:'
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-devices', '/instanceid', $DeviceInstanceId) -AllowNonZeroExit | Out-Null
Write-Log 'Current driver package inventory follows; review Windows VHID Stack entries before removal:'
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-drivers') -AllowNonZeroExit | Out-Null

if (-not $RemoveDevice -and -not $RemovePackage) {
    Write-Log 'No removal switch supplied. Listing only.'
    Write-Log 'Use -RemoveDevice and/or -RemovePackage -PublishedName oemNN.inf with -Apply after reviewing inventory.'
    exit 0
}

if ($RemoveDevice) {
    Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/remove-device', $DeviceInstanceId) -MutatesSystem | Out-Null
}

if ($RemovePackage) {
    if (-not $PublishedName) { throw '-RemovePackage requires -PublishedName oemNN.inf.' }
    if (-not (Test-PublishedName -Name $PublishedName)) { throw "PublishedName must look like oemNN.inf, got '$PublishedName'." }
    Assert-VhidPackagePublishedName -Name $PublishedName

    $deleteArgs = @('/delete-driver', $PublishedName, '/uninstall')
    if ($Force) { $deleteArgs += '/force' }
    Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments $deleteArgs -MutatesSystem | Out-Null
}

Write-Log 'Post-action inventory:'
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-devices', '/instanceid', $DeviceInstanceId) -AllowNonZeroExit | Out-Null
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-drivers') -AllowNonZeroExit | Out-Null
Write-Log 'Uninstall helper finished.'
