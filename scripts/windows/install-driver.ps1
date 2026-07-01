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

    [DllImport("setupapi.dll", SetLastError = true)]
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

    public static void Install(string infPath, string hardwareId, out bool rebootRequired) {
      rebootRequired = false;

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
              className.ToString(),
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

      if (!UpdateDriverForPlugAndPlayDevices(
            IntPtr.Zero,
            hardwareId,
            infPath,
            InstallFlagForce | InstallFlagNonInteractive,
            out rebootRequired)) {
        ThrowLastWin32Error("UpdateDriverForPlugAndPlayDevices");
      }
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
    $prefix = "$TargetHardwareId\"
    @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
      Where-Object { $_.PNPDeviceID -like "$prefix*" } |
      ForEach-Object { $_.PNPDeviceID })
  } catch {
    Write-Verbose "Unable to enumerate PnP devices: $($_.Exception.Message)"
    @()
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

$resolvedInf = (Resolve-Path -LiteralPath $InfPath).Path
Import-DriverCertificate -Path $CertificatePath

if ($PSCmdlet.ShouldProcess($resolvedInf, "Stage libvirtualhid driver package")) {
  Invoke-CheckedCommand -FilePath "pnputil.exe" -Arguments @("/add-driver", $resolvedInf, "/install")
}

if ($StageOnly) {
  return
}

if ((Get-RootDeviceInstanceId -TargetHardwareId $HardwareId).Count -gt 0) {
  Write-Information "The $HardwareId device already exists." -InformationAction Continue
  return
}

$devcon = Find-Devcon
if ($devcon) {
  if ($PSCmdlet.ShouldProcess($HardwareId, "Create libvirtualhid development device with devcon")) {
    Invoke-CheckedCommand -FilePath $devcon -Arguments @("install", $resolvedInf, $HardwareId)
  }
  return
}

if ($PSCmdlet.ShouldProcess($HardwareId, "Create libvirtualhid development device with SetupAPI")) {
  Install-RootDeviceWithSetupApi -Path $resolvedInf -TargetHardwareId $HardwareId
}
