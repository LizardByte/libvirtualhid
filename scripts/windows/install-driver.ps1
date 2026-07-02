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

  [string] $LogPath,

  [switch] $StageOnly
)

$ErrorActionPreference = "Stop"
$script:LibVirtualHidTranscriptStarted = $false

function Start-LibVirtualHidTranscript {
  param([string] $Path)

  if (-not $Path) {
    return
  }

  try {
    $logDirectory = Split-Path -Parent $Path
    if ($logDirectory) {
      New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    }
    Start-Transcript -Path $Path -Append | Out-Null
    $script:LibVirtualHidTranscriptStarted = $true
  } catch {
    Write-Warning "Unable to start libvirtualhid install transcript: $($_.Exception.Message)"
  }
}

function Stop-LibVirtualHidTranscript {
  if (-not $script:LibVirtualHidTranscriptStarted) {
    return
  }

  try {
    Stop-Transcript | Out-Null
  } catch {
    Write-Warning "Unable to stop libvirtualhid install transcript: $($_.Exception.Message)"
  }
}

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

function Add-SetupApiRootDeviceInstaller {
  if (([System.Management.Automation.PSTypeName] "LibVirtualHid.SetupApi.RootDeviceInstaller").Type) {
    return
  }

  Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace LibVirtualHid.SetupApi {
  public static class RootDeviceInstaller {
    private const uint DicdGenerateId = 0x00000001;
    private const uint DifRegisterDevice = 0x00000019;
    private const uint InstallFlagForce = 0x00000001;
    private const uint InstallFlagNonInteractive = 0x00000004;
    private const uint SpdrpHardwareId = 0x00000001;
    private static readonly IntPtr InvalidHandleValue = new IntPtr(-1);

    [StructLayout(LayoutKind.Sequential)]
    private struct SpDevinfoData {
      public uint cbSize;
      public Guid ClassGuid;
      public uint DevInst;
      public IntPtr Reserved;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SetupDiGetINFClass(
      string infName,
      out Guid classGuid,
      StringBuilder className,
      uint classNameSize,
      out uint requiredSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern IntPtr SetupDiCreateDeviceInfoList(
      ref Guid classGuid,
      IntPtr hwndParent);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SetupDiCreateDeviceInfo(
      IntPtr deviceInfoSet,
      string deviceName,
      ref Guid classGuid,
      string deviceDescription,
      IntPtr hwndParent,
      uint creationFlags,
      ref SpDevinfoData deviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SetupDiSetDeviceRegistryProperty(
      IntPtr deviceInfoSet,
      ref SpDevinfoData deviceInfoData,
      uint property,
      byte[] propertyBuffer,
      uint propertyBufferSize);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiCallClassInstaller(
      uint installFunction,
      IntPtr deviceInfoSet,
      ref SpDevinfoData deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    private static extern bool SetupDiDestroyDeviceInfoList(IntPtr deviceInfoSet);

    [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool UpdateDriverForPlugAndPlayDevices(
      IntPtr hwndParent,
      string hardwareId,
      string fullInfPath,
      uint installFlags,
      out bool rebootRequired);

    public static void Update(string infPath, string hardwareId, out bool rebootRequired) {
      rebootRequired = false;

      if (!UpdateDriverForPlugAndPlayDevices(
            IntPtr.Zero,
            hardwareId,
            infPath,
            InstallFlagForce | InstallFlagNonInteractive,
            out rebootRequired)) {
        ThrowLastWin32Error("UpdateDriverForPlugAndPlayDevices");
      }
    }

    public static void Install(string infPath, string hardwareId, out bool rebootRequired) {
      rebootRequired = false;

      string rootDeviceName = GetRootDeviceName(hardwareId);

      Guid classGuid;
      uint requiredSize;
      var className = new StringBuilder(256);
      if (!SetupDiGetINFClass(infPath, out classGuid, className, (uint) className.Capacity, out requiredSize)) {
        ThrowLastWin32Error("SetupDiGetINFClass");
      }

      IntPtr deviceInfoSet = SetupDiCreateDeviceInfoList(ref classGuid, IntPtr.Zero);
      if (deviceInfoSet == InvalidHandleValue) {
        ThrowLastWin32Error("SetupDiCreateDeviceInfoList");
      }

      try {
        var deviceInfoData = new SpDevinfoData();
        deviceInfoData.cbSize = (uint) Marshal.SizeOf(typeof(SpDevinfoData));

        if (!SetupDiCreateDeviceInfo(
              deviceInfoSet,
              rootDeviceName,
              ref classGuid,
              null,
              IntPtr.Zero,
              DicdGenerateId,
              ref deviceInfoData)) {
          ThrowLastWin32Error("SetupDiCreateDeviceInfo");
        }

        byte[] hardwareIds = Encoding.Unicode.GetBytes(hardwareId + "\0\0");
        if (!SetupDiSetDeviceRegistryProperty(
              deviceInfoSet,
              ref deviceInfoData,
              SpdrpHardwareId,
              hardwareIds,
              (uint) hardwareIds.Length)) {
          ThrowLastWin32Error("SetupDiSetDeviceRegistryProperty");
        }

        if (!SetupDiCallClassInstaller(DifRegisterDevice, deviceInfoSet, ref deviceInfoData)) {
          ThrowLastWin32Error("SetupDiCallClassInstaller");
        }
      } finally {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
      }
    }

    private static string GetRootDeviceName(string hardwareId) {
      const string rootPrefix = "ROOT\\";
      if (!hardwareId.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase)) {
        throw new ArgumentException("Hardware ID must use the ROOT\\ enumerator.", "hardwareId");
      }

      string rootDeviceName = hardwareId.Substring(rootPrefix.Length);
      if (rootDeviceName.Length == 0 || rootDeviceName.Contains("\\")) {
        throw new ArgumentException(
          "Hardware ID must be a root-enumerated device ID without an instance suffix.",
          "hardwareId");
      }

      return rootDeviceName;
    }

    private static void ThrowLastWin32Error(string action) {
      int error = Marshal.GetLastWin32Error();
      throw new InvalidOperationException(
        action + " failed with Win32 error " + error + ": " + new Win32Exception(error).Message);
    }
  }
}
"@
}

function Get-RootDeviceInstanceId {
  param([string] $TargetHardwareId)

  try {
    $devices = & pnputil.exe /enum-devices /deviceid $TargetHardwareId /deviceids
    if ($LASTEXITCODE -eq 0) {
      $instanceIds = @($devices |
        Where-Object { $_ -match "^\s*Instance ID\s*:\s*(.+)$" } |
        ForEach-Object { $Matches[1].Trim() })
      if ($instanceIds.Count -gt 0) {
        return $instanceIds
      }
    }
  } catch {
    Write-Verbose "Unable to enumerate PnP devices with pnputil: $($_.Exception.Message)"
  }

  try {
    $prefix = "$TargetHardwareId\"
    @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
      Where-Object { $_.PNPDeviceID -like "$prefix*" -or $_.HardwareID -contains $TargetHardwareId } |
      ForEach-Object { $_.PNPDeviceID })
  } catch {
    Write-Verbose "Unable to enumerate PnP devices: $($_.Exception.Message)"
    @()
  }
}

function Get-RegistryRootDevice {
  param([string] $TargetHardwareId)

  $rootKey = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT"
  Get-ChildItem -LiteralPath $rootKey -ErrorAction SilentlyContinue | ForEach-Object {
    $rootDeviceId = $_.PSChildName
    Get-ChildItem -LiteralPath $_.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
      $instanceId = "ROOT\$rootDeviceId\$($_.PSChildName)"
      $hardwareIds = @()
      try {
        $hardwareIds = @((Get-ItemProperty -LiteralPath $_.PSPath -Name HardwareID -ErrorAction Stop).HardwareID)
      } catch {
        $hardwareIds = @()
      }

      $hasExactHardwareId = $hardwareIds -contains $TargetHardwareId
      $hasCorruptHardwareId = (
        $hardwareIds.Count -gt 1 -and
        -not $hasExactHardwareId -and
        (($hardwareIds -join "") -ieq $TargetHardwareId)
      )
      $hasTargetInstanceId = $instanceId -like "$TargetHardwareId\*"

      if ($hasExactHardwareId -or $hasCorruptHardwareId -or $hasTargetInstanceId) {
        $classGuid = $null
        $classGuid = $null
        try {
          $deviceProperties = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction Stop
          $classGuid = $deviceProperties.ClassGUID
        } catch {
          Write-Verbose "Unable to read registry properties for $instanceId`: $($_.Exception.Message)"
        }

        [pscustomobject]@{
          InstanceId = $instanceId
          HasExactHardwareId = $hasExactHardwareId
          HasCorruptHardwareId = $hasCorruptHardwareId
          HasLegacyHidClass = $classGuid -ieq "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}"
        }
      }
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
    [string] $TargetHardwareId
  )

  Add-SetupApiRootDeviceInstaller
  $rebootRequired = $false
  if ($PSCmdlet.ShouldProcess($TargetHardwareId, "Update libvirtualhid development device driver")) {
    [LibVirtualHid.SetupApi.RootDeviceInstaller]::Update($Path, $TargetHardwareId, [ref] $rebootRequired)
  }
  if ($rebootRequired) {
    Write-Warning "Windows reported that a reboot is required to finish installing the libvirtualhid driver."
  }
}

function Install-RootDeviceWithSetupApi {
  param(
    [Parameter(Mandatory = $true)]
    [string] $Path,

    [Parameter(Mandatory = $true)]
    [string] $TargetHardwareId
  )

  Add-SetupApiRootDeviceInstaller
  $rebootRequired = $false
  [LibVirtualHid.SetupApi.RootDeviceInstaller]::Install($Path, $TargetHardwareId, [ref] $rebootRequired)
  if ($rebootRequired) {
    Write-Warning "Windows reported that a reboot is required to finish installing the libvirtualhid driver."
  }
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

  $registryRootDevices = @(Get-RegistryRootDevice -TargetHardwareId $HardwareId)
  foreach ($device in ($registryRootDevices | Where-Object { $_.HasCorruptHardwareId -or $_.HasLegacyHidClass })) {
    Remove-DeviceInstance -InstanceId $device.InstanceId
  }

  $rootDevices = @(Get-RootDeviceInstanceId -TargetHardwareId $HardwareId)
  if ($rootDevices.Count -gt 0) {
    Write-Information "Updating the existing $HardwareId device driver." -InformationAction Continue
    foreach ($rootDevice in $rootDevices) {
      Set-RootDeviceVhfMode -InstanceId $rootDevice
    }
    Update-RootDeviceDriverWithSetupApi -Path $resolvedInf -TargetHardwareId $HardwareId
    foreach ($rootDevice in $rootDevices) {
      Restart-RootDevice -InstanceId $rootDevice
    }
    return
  }

  if ($PSCmdlet.ShouldProcess($HardwareId, "Create libvirtualhid development device with SetupAPI")) {
    Install-RootDeviceWithSetupApi -Path $resolvedInf -TargetHardwareId $HardwareId
  }

  $rootDevices = @(Get-RootDeviceInstanceId -TargetHardwareId $HardwareId)
  foreach ($rootDevice in $rootDevices) {
    Set-RootDeviceVhfMode -InstanceId $rootDevice
  }
  Update-RootDeviceDriverWithSetupApi -Path $resolvedInf -TargetHardwareId $HardwareId
  foreach ($rootDevice in $rootDevices) {
    Restart-RootDevice -InstanceId $rootDevice
  }
} finally {
  Stop-LibVirtualHidTranscript
}
