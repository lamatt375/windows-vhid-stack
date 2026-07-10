#requires -Version 5.1

[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ProofClientPath,
    [string]$DeviceInstanceId = 'ROOT\WINDOWSVHIDSTACKVIRTUALINPUT\0000',
    [string]$LogRoot = 'C:\vhid-lab\logs',
    [switch]$AllowInputTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if (-not $ProofClientPath) { $ProofClientPath = Join-Path $RepoRoot 'tools\proof-client\x64\Debug\proof-client.exe' }

New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$script:LogPath = Join-Path $LogRoot ("Test-VhidStatus-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

function Write-Log {
    param([Parameter(Mandatory = $true)][string]$Message)
    $line = "{0} {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Message
    Write-Host $line
    Add-Content -LiteralPath $script:LogPath -Value $line
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $display = "$FilePath $($Arguments -join ' ')"
    Write-Log "RUN: $display"
    $output = & $FilePath @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) { Write-Log "  $line" }
    Write-Log "ExitCode=$exitCode"
    return $exitCode
}

Write-Log 'Windows VHID Stack status-only test helper starting.'
Write-Log 'This script is read-only by default and does not send move, click, keytap, or smoke trigger commands.'
Write-Log "Log=$script:LogPath"
Write-Log "ExpectedDeviceInstanceId=$DeviceInstanceId"

$os = Get-CimInstance -ClassName Win32_OperatingSystem
Write-Log "OS=$($os.Caption) Version=$($os.Version) Build=$($os.BuildNumber)"

Invoke-LoggedCommand -FilePath 'pnputil.exe' -Arguments @('/enum-devices', '/instanceid', $DeviceInstanceId) | Out-Null

if (-not (Test-Path -LiteralPath $ProofClientPath)) {
    throw "proof-client not found: $ProofClientPath"
}

$statusExit = Invoke-LoggedCommand -FilePath $ProofClientPath -Arguments @('status')
if ($statusExit -ne 0) { throw "proof-client status failed with exit code $statusExit." }

if ($AllowInputTest) {
    Write-Log 'AllowInputTest was supplied, but this helper intentionally implements no input tests.'
    Write-Log 'Run reviewed proof-client commands manually in an approved isolated VM if an input gate has been opened.'
}

Write-Log 'Status-only test helper finished.'
