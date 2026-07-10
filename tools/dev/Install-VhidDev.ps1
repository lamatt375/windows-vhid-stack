#requires -Version 5.1

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$DriverRoot,
    [string]$InfPath,
    [string]$SysPath,
    [string]$CatPath,
    [string]$ProofClientPath,
    [string]$CertificateSubject = 'Windows VHID Stack Test Certificate',
    [string]$DeviceInstanceId = 'ROOT\WINDOWSVHIDSTACKVIRTUALINPUT\0000',
    [string]$Inf2CatOs = '10_X64',
    [string]$LogRoot = 'C:\vhid-lab\logs',
    [switch]$Apply,
    [switch]$SkipCatalog,
    [switch]$SkipSigning,
    [switch]$SkipInstall,
    [switch]$SkipStatus
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if (-not $DriverRoot) { $DriverRoot = Join-Path $RepoRoot 'src\driver' }
if (-not $InfPath) { $InfPath = Join-Path $DriverRoot 'VirtualInput.inf' }
if (-not $SysPath) { $SysPath = Join-Path $DriverRoot 'x64\Debug\VirtualInput.sys' }
if (-not $CatPath) { $CatPath = Join-Path $DriverRoot 'VirtualInput.cat' }
if (-not $ProofClientPath) { $ProofClientPath = Join-Path $RepoRoot 'tools\proof-client\x64\Debug\proof-client.exe' }

$DryRun = -not $Apply
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$script:LogPath = Join-Path $LogRoot ("Install-VhidDev-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

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

function Get-DriverVer {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $line = Get-Content -LiteralPath $Path | Where-Object { $_ -match '^\s*DriverVer\s*=' } | Select-Object -First 1
    if ($line) { return ($line -replace '^\s*DriverVer\s*=\s*', '').Trim() }
    return $null
}

function Get-TestSigningState {
    try {
        $output = & bcdedit.exe /enum 2>&1
        $joined = ($output | Out-String)
        if ($joined -match '(?im)^\s*testsigning\s+Yes\s*$') { return 'enabled' }
        if ($joined -match '(?im)^\s*testsigning\s+No\s*$') { return 'disabled' }
        return 'not reported'
    } catch {
        return "unknown: $($_.Exception.Message)"
    }
}

function Find-Tool {
    param([Parameter(Mandatory = $true)][string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { return $cmd.Source }

    $roots = @($env:WindowsSdkDir, $env:WDKContentRoot, 'C:\Program Files (x86)\Windows Kits\10', 'C:\Program Files\Windows Kits\10') |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
        Select-Object -Unique
    foreach ($root in $roots) {
        $match = Get-ChildItem -LiteralPath $root -Filter $Name -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($match) { return $match.FullName }
    }

    return $null
}

function Get-CertificateSummary {
    param([Parameter(Mandatory = $true)][string]$Subject)
    $locations = @('Cert:\CurrentUser\My', 'Cert:\LocalMachine\My', 'Cert:\LocalMachine\TrustedPeople')
    $matches = @()
    foreach ($location in $locations) {
        try {
            if (Test-Path -LiteralPath $location) {
                $matches += Get-ChildItem -LiteralPath $location -ErrorAction Stop |
                    Where-Object { $_.Subject -like "*$Subject*" } |
                    Select-Object @{Name='Location';Expression={$location}}, Subject, Thumbprint, NotAfter
            }
        } catch {
            Write-Log "Certificate scan failed for $location`: $($_.Exception.Message)"
        }
    }
    return $matches
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

    if ($MutatesSystem -and -not $PSCmdlet.ShouldProcess($display, 'Run dev/test install action')) {
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

Write-Log 'Windows VHID Stack dev/test install helper starting.'
Write-Log 'WARNING: This script is for reviewed isolated dev/test VM use. It is not a production installer.'
Write-Log "Mode=$(@{ $true='DRY-RUN'; $false='APPLY' }[$DryRun]) Log=$script:LogPath"
Write-Log "RepoRoot=$RepoRoot"
Write-Log "DriverRoot=$DriverRoot"
Write-Log "ExpectedDeviceInstanceId=$DeviceInstanceId"

$admin = Test-Administrator
Write-Log "Administrator=$admin"
if ($Apply -and -not $admin) { throw 'Apply mode requires an elevated PowerShell session.' }

$os = Get-CimInstance -ClassName Win32_OperatingSystem
Write-Log "OS=$($os.Caption) Version=$($os.Version) Build=$($os.BuildNumber)"
$testSigningState = Get-TestSigningState
Write-Log "TESTSIGNING=$testSigningState"
if ($Apply -and $testSigningState -ne 'enabled') {
    throw 'Apply mode requires TESTSIGNING to be enabled before installing a test-signed driver package.'
}

$driverVer = Get-DriverVer -Path $InfPath
Write-Log "INF=$InfPath"
Write-Log "DriverVer=$driverVer"
if (-not (Test-Path -LiteralPath $InfPath)) {
    if ($Apply) { throw "INF not found: $InfPath" }
    Write-Log "DRY-RUN WARNING: INF not found: $InfPath"
}
if (-not (Test-Path -LiteralPath $SysPath)) {
    if ($Apply) { throw "Built SYS not found: $SysPath" }
    Write-Log "DRY-RUN WARNING: built SYS not found: $SysPath"
}
if ($SkipCatalog -and -not (Test-Path -LiteralPath $CatPath)) { Write-Log "Catalog is absent and -SkipCatalog was supplied: $CatPath" }

$inf2cat = Find-Tool -Name 'inf2cat.exe'
$signtool = Find-Tool -Name 'signtool.exe'
$pnputil = Find-Tool -Name 'pnputil.exe'
Write-Log "inf2cat=$inf2cat"
Write-Log "signtool=$signtool"
Write-Log "pnputil=$pnputil"

if (-not $SkipCatalog -and -not $inf2cat) {
    if ($Apply) { throw 'inf2cat.exe was not found. Run from a WDK/EWDK environment or add it to PATH.' }
    Write-Log 'DRY-RUN WARNING: inf2cat.exe was not found. Apply mode would fail until WDK/EWDK tools are available.'
}
if (-not $SkipSigning -and -not $signtool) {
    if ($Apply) { throw 'signtool.exe was not found. Run from a WDK/EWDK environment or add it to PATH.' }
    Write-Log 'DRY-RUN WARNING: signtool.exe was not found. Apply mode would fail until WDK/EWDK tools are available.'
}
if (-not $SkipInstall -and -not $pnputil) {
    if ($Apply) { throw 'pnputil.exe was not found.' }
    Write-Log 'DRY-RUN WARNING: pnputil.exe was not found. Apply mode would fail until pnputil is available.'
}

$certs = @(Get-CertificateSummary -Subject $CertificateSubject)
Write-Log "CertificateSubject=$CertificateSubject Matches=$($certs.Count)"
foreach ($cert in $certs) {
    Write-Log "  Cert $($cert.Location) $($cert.Thumbprint) $($cert.Subject) NotAfter=$($cert.NotAfter)"
}
if ($Apply -and -not $SkipSigning -and $certs.Count -eq 0) {
    throw "No matching test certificate was found for subject '$CertificateSubject'."
}

Write-Log 'Current matching devices/packages before action:'
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-devices', '/instanceid', $DeviceInstanceId) -AllowNonZeroExit | Out-Null
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-drivers') -AllowNonZeroExit | Out-Null

if (-not $SkipCatalog) {
    if ($inf2cat) {
        Invoke-LoggedCommand -FilePath $inf2cat -Arguments @("/driver:$DriverRoot", "/os:$Inf2CatOs") -MutatesSystem | Out-Null
    } else {
        Write-Log "DRY-RUN: would run inf2cat for DriverRoot=$DriverRoot OS=$Inf2CatOs"
    }
}

if (-not $SkipSigning) {
    if ($signtool) {
        Invoke-LoggedCommand -FilePath $signtool -Arguments @('sign', '/fd', 'SHA256', '/n', $CertificateSubject, $SysPath) -MutatesSystem | Out-Null
        Invoke-LoggedCommand -FilePath $signtool -Arguments @('sign', '/fd', 'SHA256', '/n', $CertificateSubject, $CatPath) -MutatesSystem | Out-Null
    } else {
        Write-Log "DRY-RUN: would sign SYS and CAT with certificate subject '$CertificateSubject'"
    }
}

if (-not $SkipInstall) {
    if ($pnputil) {
        Invoke-LoggedCommand -FilePath $pnputil -Arguments @('/add-driver', $InfPath, '/install') -MutatesSystem | Out-Null
    } else {
        Write-Log "DRY-RUN: would install/update package from INF=$InfPath"
    }
}

Write-Log 'Post-action inventory:'
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-devices', '/instanceid', $DeviceInstanceId) -AllowNonZeroExit | Out-Null
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-drivers') -AllowNonZeroExit | Out-Null

if (-not $SkipStatus) {
    if (Test-Path -LiteralPath $ProofClientPath) {
        if ($DryRun) {
            Write-Log "DRY-RUN: would run status verification: $ProofClientPath status"
        } else {
            Invoke-LoggedCommand -FilePath $ProofClientPath -Arguments @('status') | Out-Null
        }
    } else {
        if ($Apply) { throw "proof-client not found for requested status verification: $ProofClientPath" }
        Write-Log "proof-client not found; status verification skipped: $ProofClientPath"
    }
}

Write-Log 'Install helper finished.'
