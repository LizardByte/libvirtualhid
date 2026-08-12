<#
.SYNOPSIS
Removes the libvirtualhid Windows UMDF development driver.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [string] $PublishedName,

  [string] $OriginalName = "libvirtualhid.inf",

  [string] $HardwareId = "ROOT\LIBVIRTUALHID",

  [string] $BrokerServiceName = "libvirtualhid_broker",

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

    [int[]] $SuccessExitCodes = @(0)
  )

  & $FilePath @Arguments
  if ($LASTEXITCODE -notin $SuccessExitCodes) {
    throw "$FilePath exited with code $LASTEXITCODE"
  }
}

function Remove-LibVirtualHidBrokerService {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Name)

  $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
  if (-not $service) {
    return
  }

  if ($service.Status -ne "Stopped" -and $PSCmdlet.ShouldProcess($Name, "Stop libvirtualhid broker service")) {
    Stop-Service -Name $Name -Force -ErrorAction Stop
    $service.WaitForStatus("Stopped", [TimeSpan]::FromSeconds(15))
  }

  if ($PSCmdlet.ShouldProcess($Name, "Delete libvirtualhid broker service")) {
    $service.Dispose()
    Invoke-CheckedCommand -FilePath "sc.exe" -Arguments @("delete", $Name)

    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
      if (-not (Get-Service -Name $Name -ErrorAction SilentlyContinue)) {
        return
      }
      Start-Sleep -Milliseconds 250
    }
    throw "The $Name service still exists after deletion."
  }
}

function Find-PublishedName {
  param(
    [string] $TargetOriginalName,
    [string] $TargetHardwareId
  )

  $publishedNames = @()
  $dismFailure = $null
  try {
    $publishedNames = @(
      Get-WindowsDriver -Online -All -ErrorAction Stop |
        Where-Object {
          $_.Driver -match "^oem\d+\.inf$" -and
          [IO.Path]::GetFileName($_.OriginalFileName) -ieq $TargetOriginalName
        } |
        Select-Object -ExpandProperty Driver -Unique
    )
  } catch {
    $dismFailure = $_.Exception.Message
    Write-Verbose "DISM driver-store enumeration failed: $dismFailure"
  }
  if ($publishedNames.Count -gt 0) {
    return $publishedNames
  }
  if (-not $dismFailure) {
    return @()
  }

  # Win32_PnPSignedDriver provides a language-neutral fallback for the package
  # currently bound to the root device if DISM cannot enumerate the store.
  $targetDeviceIds = @(
    Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $TargetHardwareId
  )
  if ($targetDeviceIds.Count -eq 0) {
    throw "DISM could not enumerate the driver store and no bound device is available for CIM fallback: $dismFailure"
  }

  $cimPublishedNames = @(
    Get-CimInstance -ClassName Win32_PnPSignedDriver -ErrorAction Stop |
      Where-Object {
        $_.DeviceID -in $targetDeviceIds -and
        $_.InfName -match "^oem\d+\.inf$"
      } |
      Select-Object -ExpandProperty InfName -Unique
  )
  if ($cimPublishedNames.Count -eq 0) {
    throw "DISM could not enumerate the driver store and CIM did not identify the bound package: $dismFailure"
  }
  return $cimPublishedNames
}

function Assert-PublishedName {
  param([string] $Name)

  if ($Name -notmatch "^oem\d+\.inf$") {
    throw "The published driver package name is invalid: $Name"
  }
}

function Assert-LibVirtualHidRemoved {
  param(
    [string] $TargetOriginalName,
    [string] $TargetHardwareId,
    [string] $ServiceName
  )

  if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) {
    throw "The $ServiceName service remains installed."
  }

  $remainingDevices = @(
    Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $TargetHardwareId
  )
  if ($remainingDevices.Count -gt 0) {
    throw "libvirtualhid device instances remain installed: $($remainingDevices -join ', ')"
  }

  $remainingPackages = @(
    Find-PublishedName `
      -TargetOriginalName $TargetOriginalName `
      -TargetHardwareId $TargetHardwareId
  )
  if ($remainingPackages.Count -gt 0) {
    throw "libvirtualhid driver packages remain staged: $($remainingPackages -join ', ')"
  }
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

$publishedNames = @()
if ($PublishedName) {
  Assert-PublishedName -Name $PublishedName
  $publishedNames += $PublishedName
} else {
  try {
    $publishedNames = @(
      Find-PublishedName `
        -TargetOriginalName $OriginalName `
        -TargetHardwareId $HardwareId
    )
  } catch {
    throw "Unable to discover the staged libvirtualhid driver package through Windows APIs: $($_.Exception.Message)"
  }
}

Remove-LibVirtualHidBrokerService -Name $BrokerServiceName

$deviceInstanceIds = @(
  Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $HardwareId
  Get-LibVirtualHidRegistryRootDevice -TargetHardwareId $HardwareId |
    Select-Object -ExpandProperty InstanceId
) | Select-Object -Unique

foreach ($instanceId in $deviceInstanceIds) {
  if ($PSCmdlet.ShouldProcess($instanceId, "Remove libvirtualhid development device with pnputil")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/remove-device", $instanceId)
  }
}

if ($publishedNames.Count -eq 0) {
  Write-Warning "No staged libvirtualhid driver package matching $OriginalName was found."
} else {
  foreach ($driverPackage in $publishedNames) {
    Assert-PublishedName -Name $driverPackage
    $deleteArgs = @("/delete-driver", $driverPackage, "/uninstall")
    if ($Force) {
      $deleteArgs += "/force"
    }

    if ($PSCmdlet.ShouldProcess($driverPackage, "Delete libvirtualhid driver package")) {
      Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments $deleteArgs
    }
  }
}

if (-not $WhatIfPreference) {
  Assert-LibVirtualHidRemoved `
    -TargetOriginalName $OriginalName `
    -TargetHardwareId $HardwareId `
    -ServiceName $BrokerServiceName
}
Remove-DriverCertificate -Subject $RemoveCertificateSubject
