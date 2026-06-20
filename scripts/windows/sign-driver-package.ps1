<#
.SYNOPSIS
Creates a local test certificate and signs the libvirtualhid driver catalog.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $PackagePath,

  [string] $CatalogName = "libvirtualhid.cat",

  [string] $CertificatePath,

  [string] $CertificateSubject = "CN=libvirtualhid CI Test Driver Signing",

  [int] $ValidDays = 7,

  [switch] $KeepPrivateCertificate
)

$ErrorActionPreference = "Stop"

function Find-SignTool {
  $candidate = Get-Command signtool.exe -ErrorAction SilentlyContinue
  if ($candidate) {
    return $candidate.Source
  }

  $roots = @(
    $env:WindowsSdkDir,
    $env:WDKContentRoot,
    "${env:ProgramFiles(x86)}\Windows Kits\10"
  ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

  foreach ($root in $roots) {
    $candidate = Get-ChildItem -LiteralPath $root -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -match "\\x64\\signtool\.exe$" } |
      Sort-Object FullName -Descending |
      Select-Object -First 1
    if ($candidate) {
      return $candidate.FullName
    }
  }

  throw "signtool.exe was not found. Install the Windows SDK or WDK."
}

function Invoke-CheckedCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string] $FilePath,

    [Parameter(Mandatory = $true)]
    [string[]] $Arguments
  )

  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$FilePath exited with code $LASTEXITCODE"
  }
}

$resolvedPackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
$catalogPath = Join-Path $resolvedPackagePath $CatalogName
if (-not (Test-Path -LiteralPath $catalogPath)) {
  throw "Driver catalog was not found: $catalogPath"
}

if (-not $CertificatePath) {
  $CertificatePath = Join-Path $resolvedPackagePath "libvirtualhid-ci-test.cer"
}
$certificateDirectory = Split-Path -Path $CertificatePath -Parent
New-Item -ItemType Directory -Force -Path $certificateDirectory | Out-Null

$certificate = New-SelfSignedCertificate `
  -Subject $CertificateSubject `
  -Type CodeSigningCert `
  -CertStoreLocation "Cert:\CurrentUser\My" `
  -KeyAlgorithm RSA `
  -KeyLength 3072 `
  -KeyUsage DigitalSignature `
  -HashAlgorithm SHA256 `
  -NotAfter (Get-Date).AddDays($ValidDays)

try {
  Export-Certificate -Cert $certificate -FilePath $CertificatePath -Force | Out-Null

  $signTool = Find-SignTool
  Invoke-CheckedCommand -FilePath $signTool -Arguments @(
    "sign",
    "/fd", "SHA256",
    "/sha1", $certificate.Thumbprint,
    "/s", "My",
    $catalogPath
  )
}
finally {
  if (-not $KeepPrivateCertificate) {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -Force -ErrorAction SilentlyContinue
  }
}
