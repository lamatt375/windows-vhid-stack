#requires -Version 5.1

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$CertificateSubject = 'Windows VHID Stack Test Certificate',
    [string]$CertOutputRoot = 'C:\vhid-lab\certs',
    [string]$LogRoot = 'C:\vhid-lab\logs',
    [switch]$Apply,
    [switch]$ForceNew,
    [switch]$SkipTrustImport
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$DryRun = -not $Apply
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null
$script:LogPath = Join-Path $LogRoot ("New-VhidDevCert-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

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

function Assert-CertificateSubject {
    param([Parameter(Mandatory = $true)][string]$Subject)

    if ([string]::IsNullOrWhiteSpace($Subject)) { throw 'CertificateSubject must not be empty.' }
    if ($Subject -match '[,=\r\n]') {
        throw 'CertificateSubject must be a simple common name and must not contain comma, equals, or newline characters.'
    }
    return $Subject.Trim()
}

function Get-CertificateMatches {
    param([Parameter(Mandatory = $true)][string]$Subject)

    $exactSubject = "CN=$Subject"
    $locations = @('Cert:\CurrentUser\My', 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPublisher')
    $matches = @()
    foreach ($location in $locations) {
        try {
            if (Test-Path -LiteralPath $location) {
                $matches += Get-ChildItem -LiteralPath $location -ErrorAction Stop |
                    Where-Object { $_.Subject -eq $exactSubject } |
                    Select-Object @{Name='Location';Expression={$location}}, Subject, Thumbprint, NotAfter, HasPrivateKey
            }
        } catch {
            Write-Log "Certificate scan failed for $location`: $($_.Exception.Message)"
        }
    }
    return $matches
}

function Import-CertificateIfNeeded {
    param(
        [Parameter(Mandatory = $true)][string]$CertificatePath,
        [Parameter(Mandatory = $true)][string]$StoreLocation,
        [Parameter(Mandatory = $true)][string]$Thumbprint
    )

    $existing = Get-ChildItem -LiteralPath $StoreLocation -ErrorAction Stop |
        Where-Object { $_.Thumbprint -eq $Thumbprint } |
        Select-Object -First 1
    if ($existing) {
        Write-Log "Trusted store already has certificate $Thumbprint in $StoreLocation"
        return
    }

    if ($DryRun) {
        Write-Log "DRY-RUN: would import $CertificatePath into $StoreLocation"
        return
    }

    if (-not $PSCmdlet.ShouldProcess($StoreLocation, "Import VHID dev certificate $Thumbprint")) {
        Write-Log "Skipped certificate import into $StoreLocation by ShouldProcess."
        return
    }

    Import-Certificate -FilePath $CertificatePath -CertStoreLocation $StoreLocation | Out-Null
    Write-Log "Imported certificate $Thumbprint into $StoreLocation"
}

$CertificateSubject = Assert-CertificateSubject -Subject $CertificateSubject

Write-Log 'Windows VHID Stack dev/test certificate helper starting.'
Write-Log 'WARNING: This script is for reviewed isolated dev/test VM use. It is not a production certificate workflow.'
Write-Log "Mode=$(@{ $true='DRY-RUN'; $false='APPLY' }[$DryRun]) Log=$script:LogPath"
Write-Log "CertificateSubject=$CertificateSubject"
Write-Log "CertOutputRoot=$CertOutputRoot"
Write-Log "ForceNew=$ForceNew SkipTrustImport=$SkipTrustImport"

$admin = Test-Administrator
Write-Log "Administrator=$admin"
if ($Apply -and -not $SkipTrustImport -and -not $admin) {
    throw 'Apply mode requires an elevated PowerShell session unless -SkipTrustImport is supplied.'
}

$matches = @(Get-CertificateMatches -Subject $CertificateSubject)
Write-Log "Existing matching certificates=$($matches.Count)"
foreach ($cert in $matches) {
    Write-Log "  Cert $($cert.Location) $($cert.Thumbprint) $($cert.Subject) HasPrivateKey=$($cert.HasPrivateKey) NotAfter=$($cert.NotAfter)"
}

$signingCert = $matches |
    Where-Object { $_.Location -eq 'Cert:\CurrentUser\My' -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if ($signingCert -and -not $ForceNew) {
    Write-Log "Reusing existing CurrentUser signing certificate: $($signingCert.Thumbprint)"
} elseif ($DryRun) {
    Write-Log "DRY-RUN: would create CurrentUser code-signing certificate CN=$CertificateSubject"
} else {
    if (-not $PSCmdlet.ShouldProcess("Cert:\CurrentUser\My", "Create VHID dev code-signing certificate CN=$CertificateSubject")) {
        throw 'Certificate creation skipped by ShouldProcess.'
    }
    $created = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=$CertificateSubject" `
        -FriendlyName $CertificateSubject `
        -CertStoreLocation 'Cert:\CurrentUser\My' `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(2)
    $signingCert = [pscustomobject]@{
        Location = 'Cert:\CurrentUser\My'
        Subject = $created.Subject
        Thumbprint = $created.Thumbprint
        NotAfter = $created.NotAfter
        HasPrivateKey = $created.HasPrivateKey
    }
    Write-Log "Created CurrentUser signing certificate: $($signingCert.Thumbprint)"
}

if ($DryRun) {
    Write-Log "DRY-RUN: would export public certificate under $CertOutputRoot"
} else {
    if (-not $signingCert) { throw 'No signing certificate is available after creation/reuse.' }
    New-Item -ItemType Directory -Force -Path $CertOutputRoot | Out-Null
    $safeSubject = ($CertificateSubject -replace '[^A-Za-z0-9_.-]', '-')
    $certPath = Join-Path $CertOutputRoot ("{0}.cer" -f $safeSubject)
    Export-Certificate -Cert "Cert:\CurrentUser\My\$($signingCert.Thumbprint)" -FilePath $certPath -Force | Out-Null
    Write-Log "Exported public certificate: $certPath"

    if ($SkipTrustImport) {
        Write-Log 'Skipping trust-store import because -SkipTrustImport was supplied.'
    } else {
        Import-CertificateIfNeeded -CertificatePath $certPath -StoreLocation 'Cert:\LocalMachine\Root' -Thumbprint $signingCert.Thumbprint
        Import-CertificateIfNeeded -CertificatePath $certPath -StoreLocation 'Cert:\LocalMachine\TrustedPublisher' -Thumbprint $signingCert.Thumbprint
    }
}

Write-Log 'Certificate helper finished.'
