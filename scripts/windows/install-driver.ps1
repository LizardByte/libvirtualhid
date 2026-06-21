<#
.SYNOPSIS
Stages and installs the libvirtualhid Windows UMDF development driver.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory = $true)]
  [string] $InfPath,

  [string] $CertificatePath,

  [string] $HardwareId = "ROOT\LIBVIRTUALHID",

  [switch] $StageOnly
)

$ErrorActionPreference = "Stop"

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

function Find-Devcon {
  if ($env:DEVCON_EXE -and (Test-Path -LiteralPath $env:DEVCON_EXE)) {
    return $env:DEVCON_EXE
  }

  $roots = @(
    $env:WDKContentRoot,
    $env:WindowsSdkDir,
    "${env:ProgramFiles(x86)}\Windows Kits\10"
  ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

  foreach ($root in $roots) {
    $candidate = Get-ChildItem -LiteralPath $root -Recurse -Filter devcon.exe -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -match "\\x64\\devcon\.exe$" } |
      Select-Object -First 1
    if ($candidate) {
      return $candidate.FullName
    }
  }

  return $null
}

function Import-DriverCertificate {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Path)

  if (-not $Path -or -not (Test-Path -LiteralPath $Path)) {
    return
  }

  $resolvedCertificate = (Resolve-Path -LiteralPath $Path).Path
  foreach ($store in @("Cert:\LocalMachine\Root", "Cert:\LocalMachine\TrustedPublisher")) {
    if ($PSCmdlet.ShouldProcess($store, "Trust libvirtualhid driver certificate $resolvedCertificate")) {
      Import-Certificate -FilePath $resolvedCertificate -CertStoreLocation $store | Out-Null
    }
  }
}

$resolvedInf = (Resolve-Path -LiteralPath $InfPath).Path
Import-DriverCertificate -Path $CertificatePath

if ($PSCmdlet.ShouldProcess($resolvedInf, "Stage libvirtualhid driver package")) {
  Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/add-driver", $resolvedInf, "/install")
}

if ($StageOnly) {
  return
}

$devcon = Find-Devcon
if (-not $devcon) {
  Write-Warning "devcon.exe was not found. The package was staged, but the ROOT\LIBVIRTUALHID development device was not created."
  return
}

if ($PSCmdlet.ShouldProcess($HardwareId, "Create libvirtualhid development device")) {
  Invoke-CheckedCommand -FilePath $devcon -Arguments @("install", $resolvedInf, $HardwareId)
}
