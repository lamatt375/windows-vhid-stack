#requires -Version 5.1

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$DriverRoot,
    [string]$InfPath,
    [string]$SysPath,
    [string]$CatPath,
    [string]$PackageRoot,
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
if (-not $PackageRoot) { $PackageRoot = 'C:\vhid-lab\pkg\windows-vhid-stack' }
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

function Get-NormalizedAbsolutePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'Path must not be empty.' }
    if ($Path -notmatch '^[A-Za-z]:[\\/]') {
        throw "Path must be absolute and drive-qualified, got: $Path"
    }

    $full = [System.IO.Path]::GetFullPath($Path)
    return $full.TrimEnd('\')
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

function Assert-SafePackageRoot {
    param([Parameter(Mandatory = $true)][string]$Path)

    $full = Get-NormalizedAbsolutePath -Path $Path
    $allowedParent = Get-NormalizedAbsolutePath -Path 'C:\vhid-lab\pkg'
    $repo = Get-NormalizedAbsolutePath -Path $RepoRoot
    $driver = Get-NormalizedAbsolutePath -Path $DriverRoot
    $root = ([System.IO.Path]::GetPathRoot($full)).TrimEnd('\')
    $labRoot = Get-NormalizedAbsolutePath -Path 'C:\vhid-lab'
    $systemRoot = if ($env:SystemRoot) { Get-NormalizedAbsolutePath -Path $env:SystemRoot } else { '' }
    $userProfile = if ($env:USERPROFILE) { Get-NormalizedAbsolutePath -Path $env:USERPROFILE } else { '' }
    $tempRoot = if ($env:TEMP -and $env:TEMP -match '^[A-Za-z]:[\\/]') { Get-NormalizedAbsolutePath -Path $env:TEMP } else { '' }
    $tmpRoot = if ($env:TMP -and $env:TMP -match '^[A-Za-z]:[\\/]') { Get-NormalizedAbsolutePath -Path $env:TMP } else { '' }

    if ($full -ieq $root) { throw "Refusing to use drive root as PackageRoot: $full" }
    if ($full -ieq $labRoot) { throw "Refusing to use lab root as PackageRoot: $full" }
    if ($full -ieq $allowedParent) { throw "Refusing to use staging parent as PackageRoot: $full" }
    if (-not $full.StartsWith($allowedParent + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "PackageRoot must be a child under $allowedParent, got: $full"
    }
    if ($full -ieq $repo -or $full.StartsWith($repo + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use repo path or descendant as PackageRoot: $full"
    }
    if ($full -ieq $driver -or $full.StartsWith($driver + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use driver path or descendant as PackageRoot: $full"
    }
    if ($systemRoot -and ($full -ieq $systemRoot -or $full.StartsWith($systemRoot + '\', [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to use system path as PackageRoot: $full"
    }
    if ($userProfile -and ($full -ieq $userProfile -or $full.StartsWith($userProfile + '\', [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to use user profile path as PackageRoot: $full"
    }
    if ($tempRoot -and ($full -ieq $tempRoot -or $full.StartsWith($tempRoot + '\', [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to use temp path as PackageRoot: $full"
    }
    if ($tmpRoot -and ($full -ieq $tmpRoot -or $full.StartsWith($tmpRoot + '\', [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to use temp path as PackageRoot: $full"
    }

    return $full
}

function Initialize-PackageStage {
    $script:PackageRoot = Assert-SafePackageRoot -Path $PackageRoot
    $script:StagedInfPath = Join-Path $script:PackageRoot 'VirtualInput.inf'
    $script:StagedSysPath = Join-Path $script:PackageRoot 'VirtualInput.sys'
    if (-not $CatPath) {
        $script:StagedCatPath = Join-Path $script:PackageRoot 'VirtualInput.cat'
    } else {
        $script:StagedCatPath = Join-Path $script:PackageRoot (Split-Path -Leaf $CatPath)
    }

    Write-Log "PackageRoot=$script:PackageRoot"
    Write-Log "SourceInf=$InfPath"
    Write-Log "SourceSys=$SysPath"
    Write-Log "StagedInf=$script:StagedInfPath"
    Write-Log "StagedSys=$script:StagedSysPath"
    Write-Log "StagedCat=$script:StagedCatPath"

    if ($DryRun) {
        Write-Log "DRY-RUN: would clean/create package staging directory: $script:PackageRoot"
        Write-Log "DRY-RUN: would copy INF to staged package: $InfPath -> $script:StagedInfPath"
        Write-Log "DRY-RUN: would copy SYS to staged package: $SysPath -> $script:StagedSysPath"
        return
    }

    if (Test-Path -LiteralPath $script:PackageRoot) {
        Remove-Item -LiteralPath $script:PackageRoot -Recurse -Force -ErrorAction Stop
    }
    New-Item -ItemType Directory -Force -Path $script:PackageRoot | Out-Null
    Copy-Item -LiteralPath $InfPath -Destination $script:StagedInfPath -Force -ErrorAction Stop
    Copy-Item -LiteralPath $SysPath -Destination $script:StagedSysPath -Force -ErrorAction Stop

    if (-not (Test-Path -LiteralPath $script:StagedInfPath)) { throw "Staged INF missing after copy: $script:StagedInfPath" }
    if (-not (Test-Path -LiteralPath $script:StagedSysPath)) { throw "Staged SYS missing after copy: $script:StagedSysPath" }
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

Initialize-PackageStage

Write-Log 'Current matching devices/packages before action:'
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-devices', '/instanceid', $DeviceInstanceId) -AllowNonZeroExit | Out-Null
Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-drivers') -AllowNonZeroExit | Out-Null

if (-not $SkipCatalog) {
    if ($inf2cat) {
        Invoke-LoggedCommand -FilePath $inf2cat -Arguments @("/driver:$PackageRoot", "/os:$Inf2CatOs") -MutatesSystem | Out-Null
    } else {
        Write-Log "DRY-RUN: would run inf2cat for PackageRoot=$PackageRoot OS=$Inf2CatOs"
    }
}

if (-not $SkipSigning) {
    if ($signtool) {
        Invoke-LoggedCommand -FilePath $signtool -Arguments @('sign', '/fd', 'SHA256', '/n', $CertificateSubject, $StagedSysPath) -MutatesSystem | Out-Null
        Invoke-LoggedCommand -FilePath $signtool -Arguments @('sign', '/fd', 'SHA256', '/n', $CertificateSubject, $StagedCatPath) -MutatesSystem | Out-Null
    } else {
        Write-Log "DRY-RUN: would sign staged SYS and CAT with certificate subject '$CertificateSubject'"
    }
}

if (-not $SkipInstall) {
    if ($pnputil) {
        Invoke-LoggedCommand -FilePath $pnputil -Arguments @('/add-driver', $StagedInfPath, '/install') -MutatesSystem | Out-Null
    } else {
        Write-Log "DRY-RUN: would install/update package from staged INF=$StagedInfPath"
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
