<#
.SYNOPSIS
Removes the libvirtualhid Windows UMDF development driver.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [string] $PublishedName,

  [string] $OriginalName = "libvirtualhid.inf",

  [string] $HardwareId = "ROOT\LIBVIRTUALHID",

  [string] $RemoveCertificateSubject,

  [switch] $Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "libvirtualhid-driver-common.ps1")

function Invoke-CheckedCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string] $FilePath,

    [Parameter(Mandatory = $true)]
    [string[]] $Arguments,

    [switch] $IgnoreFailure
  )

  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0 -and -not $IgnoreFailure) {
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

function Find-PublishedName {
  param([string] $TargetOriginalName)

  $drivers = & pnputil.exe /enum-drivers
  $currentPublished = $null
  $currentOriginal = $null
  $publishedNames = @()

  foreach ($line in $drivers) {
    if ($line -match "^\s*Published Name\s*:\s*(.+)$") {
      $currentPublished = $Matches[1].Trim()
      $currentOriginal = $null
      continue
    }

    if ($line -match "^\s*Original Name\s*:\s*(.+)$") {
      $currentOriginal = $Matches[1].Trim()
      if ($currentPublished -and $currentOriginal -ieq $TargetOriginalName) {
        $publishedNames += $currentPublished
      }
    }
  }

  return $publishedNames
}

function Remove-DriverCertificate {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Subject)

  if (-not $Subject) {
    return
  }

  foreach ($store in @("Cert:\LocalMachine\TrustedPublisher", "Cert:\LocalMachine\Root")) {
    $certificates = Get-ChildItem -LiteralPath $store -ErrorAction SilentlyContinue |
      Where-Object { $_.Subject -eq $Subject -and $_.Issuer -eq $Subject }

    foreach ($certificate in $certificates) {
      if ($PSCmdlet.ShouldProcess("$store\$($certificate.Thumbprint)", "Remove libvirtualhid test driver certificate")) {
        Remove-Item -LiteralPath "$store\$($certificate.Thumbprint)" -Force
      }
    }
  }
}

$devcon = Find-Devcon
if ($devcon -and $PSCmdlet.ShouldProcess($HardwareId, "Remove libvirtualhid development device")) {
  Invoke-CheckedCommand -FilePath $devcon -Arguments @("remove", $HardwareId) -IgnoreFailure
}

foreach ($instanceId in (Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $HardwareId)) {
  if ($PSCmdlet.ShouldProcess($instanceId, "Remove libvirtualhid development device with pnputil")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/remove-device", $instanceId) -IgnoreFailure
  }
}

foreach ($instanceId in (Get-LibVirtualHidRegistryRootDevice -TargetHardwareId $HardwareId | Select-Object -ExpandProperty InstanceId -Unique)) {
  if ($PSCmdlet.ShouldProcess($instanceId, "Remove libvirtualhid registry-discovered development device with pnputil")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/remove-device", $instanceId) -IgnoreFailure
  }
}

$publishedNames = @()
if ($PublishedName) {
  $publishedNames += $PublishedName
} else {
  $publishedNames = @(Find-PublishedName -TargetOriginalName $OriginalName)
}

if ($publishedNames.Count -eq 0) {
  Write-Warning "No staged libvirtualhid driver package matching $OriginalName was found."
  Remove-DriverCertificate -Subject $RemoveCertificateSubject
  return
}

foreach ($driverPackage in $publishedNames) {
  $deleteArgs = @("/delete-driver", $driverPackage, "/uninstall")
  if ($Force) {
    $deleteArgs += "/force"
  }

  if ($PSCmdlet.ShouldProcess($driverPackage, "Delete libvirtualhid driver package")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments $deleteArgs
  }
}

Remove-DriverCertificate -Subject $RemoveCertificateSubject
