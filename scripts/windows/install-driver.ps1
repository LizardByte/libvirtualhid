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

  [string] $BrokerPath,

  [string] $SetupPath,

  [string] $LogPath,

  [switch] $StageOnly
)

$ErrorActionPreference = "Stop"
$script:LibVirtualHidTranscriptStarted = $false
$script:LibVirtualHidBrokerServiceName = "libvirtualhid_broker"
$script:LibVirtualHidBrokerServiceDisplayName = "libvirtualhid Broker"
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

function Get-LibVirtualHidScBinaryPathArgument {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path
  )

  # Windows PowerShell 5.1 needs triple quotes in a native-command argument to pass one literal quote
  # through to sc.exe while keeping a path containing spaces in a single argv element.
  return (Get-LibVirtualHidQuotedServiceBinaryPath -Path $Path).Replace('"', '"""')
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

function Install-LibVirtualHidBrokerService {
  [CmdletBinding(SupportsShouldProcess)]
  param([string] $Path)

  $resolvedBroker = Resolve-LibVirtualHidBrokerPath -Path $Path
  if (-not $resolvedBroker) {
    Write-Verbose "No libvirtualhid broker executable was found; skipping broker service registration."
    return
  }

  $serviceBinaryPath = Get-LibVirtualHidScBinaryPathArgument -Path $resolvedBroker
  $serviceRegistered = $false
  $service = Get-Service -Name $script:LibVirtualHidBrokerServiceName -ErrorAction SilentlyContinue
  if ($service) {
    Stop-LibVirtualHidBrokerService -Name $script:LibVirtualHidBrokerServiceName
    if ($PSCmdlet.ShouldProcess($script:LibVirtualHidBrokerServiceName, "Update libvirtualhid broker service")) {
      Invoke-CheckedCommand -FilePath "sc.exe" -Arguments @(
        "config",
        $script:LibVirtualHidBrokerServiceName,
        "binPath=",
        $serviceBinaryPath,
        "start=",
        "auto",
        "DisplayName=",
        $script:LibVirtualHidBrokerServiceDisplayName
      )
      $serviceRegistered = $true
    }
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

  if (-not $InstanceId) {
    return
  }

  if (-not $PSCmdlet.ShouldProcess($InstanceId, "Restart libvirtualhid development device")) {
    return
  }

  $output = @(pnputil.exe /restart-device $InstanceId 2>&1)
  $exitCode = $LASTEXITCODE
  foreach ($line in $output) {
    Write-Information $line -InformationAction Continue
  }

  if ($exitCode -ne 0) {
    throw "pnputil.exe /restart-device $InstanceId exited with code $exitCode"
  }

  if ($output -match "reboot is needed") {
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

Start-LibVirtualHidTranscript -Path $LogPath

try {
  $resolvedInf = (Resolve-Path -LiteralPath $InfPath).Path
  Import-DriverCertificate -Path $CertificatePath

  if ($PSCmdlet.ShouldProcess($resolvedInf, "Stage libvirtualhid driver package")) {
    Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/add-driver", $resolvedInf) -SuccessExitCodes @(0, 5)
  }

  if ($StageOnly) {
    return
  }

  $resolvedSetup = Resolve-LibVirtualHidDriverSetupPath -Path $SetupPath

  $registryRootDevices = @(Get-LibVirtualHidRegistryRootDevice -TargetHardwareId $HardwareId)
  foreach ($device in ($registryRootDevices | Where-Object { $_.HasCorruptHardwareId -or $_.HasLegacyHidClass })) {
    Remove-DeviceInstance -InstanceId $device.InstanceId
  }

  $rootDevices = @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $HardwareId)
  if ($rootDevices.Count -gt 0) {
    Write-Information "Updating the existing $HardwareId device driver." -InformationAction Continue
    foreach ($rootDevice in $rootDevices) {
      Set-RootDeviceVhfMode -InstanceId $rootDevice
    }
    Update-RootDeviceDriverWithSetupApi `
      -Path $resolvedInf `
      -TargetHardwareId $HardwareId `
      -SetupHelperPath $resolvedSetup
    foreach ($rootDevice in $rootDevices) {
      Restart-RootDevice -InstanceId $rootDevice
    }
    Install-LibVirtualHidBrokerService -Path $BrokerPath
    return
  }

  if ($PSCmdlet.ShouldProcess($HardwareId, "Create libvirtualhid development device with SetupAPI")) {
    Install-RootDeviceWithSetupApi `
      -Path $resolvedInf `
      -TargetHardwareId $HardwareId `
      -SetupHelperPath $resolvedSetup
  }

  $rootDevices = @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId $HardwareId)
  foreach ($rootDevice in $rootDevices) {
    Set-RootDeviceVhfMode -InstanceId $rootDevice
  }
  Update-RootDeviceDriverWithSetupApi `
    -Path $resolvedInf `
    -TargetHardwareId $HardwareId `
    -SetupHelperPath $resolvedSetup
  foreach ($rootDevice in $rootDevices) {
    Restart-RootDevice -InstanceId $rootDevice
  }
  Install-LibVirtualHidBrokerService -Path $BrokerPath
} finally {
  Stop-LibVirtualHidTranscript
}
