<#
.SYNOPSIS
Stages and installs the libvirtualhid Windows UMDF development driver.
.DESCRIPTION
Stops the broker before changing devices and starts it after installation.
Driver reloads that Windows defers until reboot produce a warning rather than
failing an otherwise successful installation. Restart Windows before using the
driver when this warning appears.
#>
[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory = $true)]
  [string] $InfPath,

  [string] $CertificatePath,

  [string] $HardwareId = "ROOT\LIBVIRTUALHID",

  [string] $BrokerPath,

  [string] $SetupPath,

  [string] $LogPath,

  [switch] $StageOnly
)

$ErrorActionPreference = "Stop"
$script:LibVirtualHidTranscriptStarted = $false
$script:LibVirtualHidBrokerServiceName = "libvirtualhid_broker"
$script:LibVirtualHidBrokerServiceDisplayName = "libvirtualhid Broker"
$script:LibVirtualHidRebootRequired = $false
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

function Resolve-LibVirtualHidBrokerPath {
  param([string] $Path)

  if ($Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
      throw "The broker executable was not found at $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
  }

  $packagedPath = Join-Path $PSScriptRoot "..\..\services\windows\libvirtualhid_broker.exe"
  if (Test-Path -LiteralPath $packagedPath) {
    return (Resolve-Path -LiteralPath $packagedPath).Path
  }

  return $null
}

function Resolve-LibVirtualHidDriverSetupPath {
  param([string] $Path)

  if ($Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
      throw "The driver setup helper was not found at $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
  }

  $packagedPath = Join-Path $PSScriptRoot "..\..\tools\windows\libvirtualhid_driver_setup.exe"
  if (Test-Path -LiteralPath $packagedPath) {
    return (Resolve-Path -LiteralPath $packagedPath).Path
  }

  throw "The libvirtualhid driver setup helper was not found. Pass its path with -SetupPath."
}

function Get-LibVirtualHidQuotedServiceBinaryPath {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path
  )

  if ($Path.Contains('"')) {
    throw "The broker executable path must not contain quotation marks: $Path"
  }

  return "`"$Path`""
}

function Assert-LibVirtualHidBrokerServiceImagePath {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Name,

    [Parameter(Mandatory = $true)]
    [string] $Path
  )

  $registryPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$Name"
  $imagePath = (Get-ItemProperty -LiteralPath $registryPath -Name "ImagePath").ImagePath
  $expectedPath = Get-LibVirtualHidQuotedServiceBinaryPath -Path $Path
  if ($imagePath -cne $expectedPath) {
    throw "The $Name service ImagePath is not safely quoted. Expected $expectedPath but found $imagePath."
  }
}

function Clear-LibVirtualHidBrokerServiceEnvironment {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Name)

  $registryPath = "HKLM:\SYSTEM\CurrentControlSet\Services\$Name"
  if (-not (Test-Path -LiteralPath $registryPath)) {
    return
  }

  if ($PSCmdlet.ShouldProcess($Name, "Clear legacy libvirtualhid broker service environment")) {
    Remove-ItemProperty -LiteralPath $registryPath -Name "Environment" -ErrorAction SilentlyContinue
  }
}

function Stop-LibVirtualHidBrokerService {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Name)

  $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
  if (-not $service -or $service.Status -eq "Stopped") {
    return
  }

  if ($PSCmdlet.ShouldProcess($Name, "Stop libvirtualhid broker service")) {
    Stop-Service -Name $Name -Force -ErrorAction Stop
    $service.WaitForStatus("Stopped", [TimeSpan]::FromSeconds(15))
  }
}

<#
.SYNOPSIS
Restores a previously running broker after installation or update failure.
.PARAMETER Name
The broker service to start if it is currently stopped.
#>
function Restore-LibVirtualHidBrokerService {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Name)

  # Do not let a recovery failure hide the original installation error.
  try {
    $service = Get-Service -Name $Name -ErrorAction Stop
    if ($service.Status -eq "Stopped" -and $PSCmdlet.ShouldProcess($Name, "Restore libvirtualhid broker service")) {
      Start-Service -Name $Name -ErrorAction Stop
    }
  } catch {
    Write-Warning "Unable to restore the libvirtualhid broker service: $($_.Exception.Message)"
  }
}

<#
.SYNOPSIS
Updates the broker service without native command-line quote parsing.
.PARAMETER Path
The broker executable path to store as a quoted service binary path.
.OUTPUTS
System.Boolean. True when the service configuration was updated.
#>
function Set-LibVirtualHidBrokerServiceConfiguration {
  [CmdletBinding(SupportsShouldProcess)]
  [OutputType([bool])]
  param([string] $Path)

  if (-not $PSCmdlet.ShouldProcess($script:LibVirtualHidBrokerServiceName, "Update libvirtualhid broker service")) {
    return $false
  }

  $service = Get-CimInstance -ClassName Win32_Service `
    -Filter "Name = '$script:LibVirtualHidBrokerServiceName'" -ErrorAction Stop
  if (-not $service) {
    throw "The $script:LibVirtualHidBrokerServiceName service was not found for reconfiguration."
  }

  $result = Invoke-CimMethod -InputObject $service -MethodName Change -Arguments @{
    PathName = Get-LibVirtualHidQuotedServiceBinaryPath -Path $Path
    DisplayName = $script:LibVirtualHidBrokerServiceDisplayName
    StartMode = "Automatic"
  } -ErrorAction Stop
  if ($result.ReturnValue -ne 0) {
    throw "Updating the libvirtualhid broker service failed with Win32_Service.Change return value $($result.ReturnValue)."
  }
  return $true
}

function Install-LibVirtualHidBrokerService {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Path)

  $resolvedBroker = Resolve-LibVirtualHidBrokerPath -Path $Path
  if (-not $resolvedBroker) {
    Write-Verbose "No libvirtualhid broker executable was found; skipping broker service registration."
    return
  }

  $serviceRegistered = $false
  $service = Get-Service -Name $script:LibVirtualHidBrokerServiceName -ErrorAction SilentlyContinue
  if ($service) {
    Stop-LibVirtualHidBrokerService -Name $script:LibVirtualHidBrokerServiceName
    $serviceRegistered = Set-LibVirtualHidBrokerServiceConfiguration -Path $resolvedBroker
  } else {
    if ($PSCmdlet.ShouldProcess($script:LibVirtualHidBrokerServiceName, "Install libvirtualhid broker service")) {
      $quotedServicePath = Get-LibVirtualHidQuotedServiceBinaryPath -Path $resolvedBroker
      New-Service `
        -Name $script:LibVirtualHidBrokerServiceName `
        -BinaryPathName $quotedServicePath `
        -DisplayName $script:LibVirtualHidBrokerServiceDisplayName `
        -StartupType Automatic | Out-Null
      $serviceRegistered = $true
    }
  }

  if ($serviceRegistered) {
    Assert-LibVirtualHidBrokerServiceImagePath -Name $script:LibVirtualHidBrokerServiceName -Path $resolvedBroker
  }

  Clear-LibVirtualHidBrokerServiceEnvironment -Name $script:LibVirtualHidBrokerServiceName

  if ($PSCmdlet.ShouldProcess($script:LibVirtualHidBrokerServiceName, "Enable libvirtualhid broker service SID")) {
    Invoke-CheckedCommand -FilePath "sc.exe" -Arguments @(
      "sidtype",
      $script:LibVirtualHidBrokerServiceName,
      "unrestricted"
    )
  }

  if ($PSCmdlet.ShouldProcess($script:LibVirtualHidBrokerServiceName, "Set libvirtualhid broker service description")) {
    Invoke-CheckedCommand -FilePath "sc.exe" -Arguments @(
      "description",
      $script:LibVirtualHidBrokerServiceName,
      "Authorizes libvirtualhid virtual gamepad creation and license state."
    )
  }

  if ($PSCmdlet.ShouldProcess($script:LibVirtualHidBrokerServiceName, "Start libvirtualhid broker service")) {
    Start-Service -Name $script:LibVirtualHidBrokerServiceName
  }
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

function Remove-DeviceInstance {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $InstanceId)

  if ($PSCmdlet.ShouldProcess($InstanceId, "Remove stale libvirtualhid root device")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/remove-device", $InstanceId)
  }
}

function Set-RootDeviceVhfMode {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $InstanceId)

  $deviceRegistryPath = "HKLM:\SYSTEM\CurrentControlSet\Enum\$InstanceId"
  if (-not (Test-Path -LiteralPath $deviceRegistryPath)) {
    Write-Verbose "Unable to set VhfMode because $deviceRegistryPath does not exist."
    return
  }

  if ($PSCmdlet.ShouldProcess($InstanceId, "Set VhfMode=1 for UMDF VHF source device")) {
    New-ItemProperty -LiteralPath $deviceRegistryPath -Name "VhfMode" -Value 1 -PropertyType DWord -Force | Out-Null
    Write-Information "Set VhfMode=1 on $InstanceId." -InformationAction Continue
  }
}

function Restart-RootDevice {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $InstanceId)

  if (-not $InstanceId -or $script:LibVirtualHidRebootRequired) {
    return
  }

  if (-not $PSCmdlet.ShouldProcess($InstanceId, "Restart libvirtualhid development device")) {
    return
  }

  $command = Get-Command pnputil.exe -ErrorAction Stop
  $previousErrorActionPreference = $ErrorActionPreference
  try {
    # Windows PowerShell 5.1 turns redirected native stderr into error records.
    # Collect them so the native exit code, including reboot statuses, decides
    # whether the restart failed.
    $ErrorActionPreference = "Continue"
    $output = @(& $command /restart-device $InstanceId 2>&1)
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }
  foreach ($line in $output) {
    Write-Information ([string] $line) -InformationAction Continue
  }

  # ERROR_NOT_SUPPORTED (50) is also returned when an earlier driver update
  # left the instance pending reboot. A reboot can reload the installed driver
  # even when this OS cannot restart the device in place.
  if ($exitCode -in @(50, 3010)) {
    $script:LibVirtualHidRebootRequired = $true
    Write-Warning "Windows could not reload $InstanceId without a reboot (pnputil exit code $exitCode). Restart Windows to complete the libvirtualhid driver installation."
    return
  }

  if ($exitCode -ne 0) {
    throw "pnputil.exe /restart-device $InstanceId exited with code $exitCode"
  }

  if ($output -match "reboot is needed") {
    $script:LibVirtualHidRebootRequired = $true
    Write-Warning "Windows reported that a reboot is required to reload the libvirtualhid UMDF driver."
  }
}

function Update-RootDeviceDriverWithSetupApi {
  [CmdletBinding(SupportsShouldProcess)]
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string] $TargetHardwareId,

    [Parameter(Mandatory = $true)]
    [string] $SetupHelperPath
  )

  if ($PSCmdlet.ShouldProcess($TargetHardwareId, "Update libvirtualhid development device driver")) {
    Invoke-CheckedCommand `
      -FilePath $SetupHelperPath `
      -Arguments @("update", $Path, $TargetHardwareId) `
      -SuccessExitCodes @(0, 3010)
    if ($LASTEXITCODE -eq 3010) {
      $script:LibVirtualHidRebootRequired = $true
      Write-Warning "Windows reported that a reboot is required to finish installing the libvirtualhid driver."
    }
  }
}

function Install-RootDeviceWithSetupApi {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string] $TargetHardwareId,

    [Parameter(Mandatory = $true)]
    [string] $SetupHelperPath
  )

  Invoke-CheckedCommand -FilePath $SetupHelperPath -Arguments @("install", $Path, $TargetHardwareId)
}

<#
.SYNOPSIS
Records a terminating installation error even when transcription is unavailable.
.PARAMETER Path
The installation log path, if one was requested.
.PARAMETER Message
The original installation error to record without replacing it with a logging error.
#>
function Write-LibVirtualHidInstallFailure {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Path, [string] $Message)

  Write-Warning "libvirtualhid driver installation failed: $Message" -WarningAction Continue
  if (-not $Path -or $script:LibVirtualHidTranscriptStarted) {
    return
  }

  try {
    if ($PSCmdlet.ShouldProcess($Path, "Record libvirtualhid installation failure")) {
      Add-Content -LiteralPath $Path -Value "$(Get-Date -Format o) libvirtualhid driver installation failed: $Message" -ErrorAction Stop
    }
  } catch {
    Write-Warning "Unable to write the libvirtualhid installation error log: $($_.Exception.Message)" -WarningAction Continue
  }
}

<#
.SYNOPSIS
Installs or updates the driver while the broker has released its device handles.
.DESCRIPTION
Stages the package, stops the broker before changing any device, and registers
the broker after the driver update. A required reboot skips further restarts.
Restores a previously running broker if installation fails or no broker binary
is supplied. StageOnly and WhatIf do not stop the service.
#>
function Install-LibVirtualHidDriver {
  [CmdletBinding(SupportsShouldProcess)]
  param(
    [Parameter(Mandatory = $true)]
    [string] $InfPath,
    [string] $CertificatePath,
    [string] $HardwareId = "ROOT\LIBVIRTUALHID",
    [string] $BrokerPath,
    [string] $SetupPath,
    [switch] $StageOnly
  )

  $script:LibVirtualHidRebootRequired = $false
  $resolvedInf = (Resolve-Path -LiteralPath $InfPath).Path
  Import-DriverCertificate -Path $CertificatePath

  if ($PSCmdlet.ShouldProcess($resolvedInf, "Stage libvirtualhid driver package")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/add-driver", $resolvedInf) -SuccessExitCodes @(0, 5)
  }

  if ($StageOnly) {
    return
  }

  $resolvedSetup = Resolve-LibVirtualHidDriverSetupPath -Path $SetupPath
  $resolvedBroker = Resolve-LibVirtualHidBrokerPath -Path $BrokerPath
  $brokerService = Get-Service -Name $script:LibVirtualHidBrokerServiceName -ErrorAction SilentlyContinue
  $brokerWasRunning = $brokerService -and $brokerService.Status -ne "Stopped"

  try {
    Stop-LibVirtualHidBrokerService -Name $script:LibVirtualHidBrokerServiceName

    $registryRootDevices = @(Get-LibVirtualHidRegistryRootDevice -TargetHardwareId $HardwareId)
    foreach ($device in ($registryRootDevices | Where-Object { $_.HasCorruptHardwareId -or $_.HasLegacyHidClass })) {
      Remove-DeviceInstance -InstanceId $device.InstanceId
    }

    $rootDevices = @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $HardwareId)
    if ($rootDevices.Count -eq 0) {
      if ($PSCmdlet.ShouldProcess($HardwareId, "Create libvirtualhid development device with SetupAPI")) {
        Install-RootDeviceWithSetupApi `
          -Path $resolvedInf `
          -TargetHardwareId $HardwareId `
          -SetupHelperPath $resolvedSetup
      }
      $rootDevices = @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $HardwareId)
    } else {
      Write-Information "Updating the existing $HardwareId device driver." -InformationAction Continue
    }

    foreach ($rootDevice in $rootDevices) {
      Set-RootDeviceVhfMode -InstanceId $rootDevice
    }

    Update-RootDeviceDriverWithSetupApi `
      -Path $resolvedInf `
      -TargetHardwareId $HardwareId `
      -SetupHelperPath $resolvedSetup
    if (-not $script:LibVirtualHidRebootRequired) {
      foreach ($rootDevice in $rootDevices) {
        Restart-RootDevice -InstanceId $rootDevice
      }
    }
    Install-LibVirtualHidBrokerService -Path $resolvedBroker
  } finally {
    # Restore an existing broker after a failed update or a driver-only install.
    if ($brokerWasRunning) {
      Restore-LibVirtualHidBrokerService -Name $script:LibVirtualHidBrokerServiceName
    }
  }
}

if ($MyInvocation.InvocationName -eq ".") {
  return
}

Start-LibVirtualHidTranscript -Path $LogPath

try {
  Install-LibVirtualHidDriver `
    -InfPath $InfPath `
    -CertificatePath $CertificatePath `
    -HardwareId $HardwareId `
    -BrokerPath $BrokerPath `
    -SetupPath $SetupPath `
    -StageOnly:$StageOnly
} catch {
  Write-LibVirtualHidInstallFailure -Path $LogPath -Message $_.Exception.Message
  throw
} finally {
  Stop-LibVirtualHidTranscript
}
