<#
.SYNOPSIS
Validates that the installed libvirtualhid Windows driver starts and can expose a gamepad child device.
#>
[CmdletBinding()]
param(
  [string] $HardwareId = "ROOT\LIBVIRTUALHID",

  [string] $ControlDevicePath = "\\.\LibVirtualHid",

  [string] $GamepadAdapterPath,

  [ValidateSet("generic", "x360", "xone", "xseries", "ds4", "ds5", "switch")]
  [string] $Profile = "xseries",

  [int] $HoldSeconds = 12,

  [int] $DeviceStartTimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"

function Invoke-PnPUtil {
  param([string[]] $Arguments)

  $output = @(pnputil.exe @Arguments 2>&1)
  $exitCode = $LASTEXITCODE
  foreach ($line in $output) {
    Write-Verbose $line
  }

  if ($exitCode -ne 0) {
    throw "pnputil.exe $($Arguments -join ' ') exited with code $exitCode`n$($output -join "`n")"
  }

  return $output
}

function ConvertFrom-PnPUtilDeviceOutput {
  param([string[]] $Output)

  $records = @()
  $current = $null
  foreach ($line in $Output) {
    if ($line -match "^\s*Instance ID:\s*(.+)\s*$") {
      if ($current) {
        $records += [pscustomobject] $current
      }
      $current = @{
        InstanceId = $Matches[1].Trim()
        DeviceDescription = $null
        DriverName = $null
        Status = $null
        ProblemCode = $null
        ProblemStatus = $null
      }
      continue
    }

    if (-not $current) {
      continue
    }

    if ($line -match "^\s{0,2}Device Description:\s*(.+)\s*$") {
      $current.DeviceDescription = $Matches[1].Trim()
    } elseif ($line -match "^\s{0,2}Driver Name:\s*(.+)\s*$") {
      $current.DriverName = $Matches[1].Trim()
    } elseif ($line -match "^\s*Status:\s*(.+)\s*$") {
      $current.Status = $Matches[1].Trim()
    } elseif ($line -match "^\s*Problem Code:\s*(.+)\s*$") {
      $current.ProblemCode = $Matches[1].Trim()
    } elseif ($line -match "^\s*Problem Status:\s*(.+)\s*$") {
      $current.ProblemStatus = $Matches[1].Trim()
    }
  }

  if ($current) {
    $records += [pscustomobject] $current
  }

  return $records
}

function Get-PnPUtilDevicesByDeviceId {
  param([string] $DeviceId)

  $output = Invoke-PnPUtil -Arguments @(
    "/enum-devices",
    "/deviceid",
    $DeviceId,
    "/deviceids",
    "/services",
    "/drivers"
  )
  return ConvertFrom-PnPUtilDeviceOutput -Output $output
}

function Assert-StartedPnPRecord {
  param(
    [object] $Record,
    [string] $Description
  )

  if ($Record.Status -ne "Started") {
    $details = @(
      "$Description did not report Status: Started.",
      "Instance ID: $($Record.InstanceId)",
      "Status: $($Record.Status)"
    )
    if ($Record.ProblemCode) {
      $details += "Problem Code: $($Record.ProblemCode)"
    }
    if ($Record.ProblemStatus) {
      $details += "Problem Status: $($Record.ProblemStatus)"
    }

    throw ($details -join "`n")
  }
}

function Assert-RootDeviceStarted {
  $rootDevices = @(Get-PnPUtilDevicesByDeviceId -DeviceId $HardwareId)
  if (-not $rootDevices) {
    throw "No installed libvirtualhid root device was found for $HardwareId."
  }

  foreach ($device in $rootDevices) {
    Assert-StartedPnPRecord -Record $device -Description "Root device $($device.InstanceId)"
  }
}

function Assert-ControlDeviceOpens {
  try {
    $stream = [System.IO.File]::Open(
      $ControlDevicePath,
      [System.IO.FileMode]::Open,
      [System.IO.FileAccess]::ReadWrite,
      [System.IO.FileShare]::ReadWrite
    )
    $stream.Dispose()
  } catch {
    throw "Could not open ${ControlDevicePath}: $($_.Exception.Message)"
  }
}

function Add-XInputProbeType {
  if ("LvhXInputProbe" -as [type]) {
    return
  }

  Add-Type -TypeDefinition @"
using System.Runtime.InteropServices;

public static class LvhXInputProbe {
  [StructLayout(LayoutKind.Sequential)]
  public struct XInputState {
    public uint PacketNumber;
    public XInputGamepad Gamepad;
  }

  [StructLayout(LayoutKind.Sequential)]
  public struct XInputGamepad {
    public ushort Buttons;
    public byte LeftTrigger;
    public byte RightTrigger;
    public short LeftThumbX;
    public short LeftThumbY;
    public short RightThumbX;
    public short RightThumbY;
  }

  [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
  public static extern uint XInputGetState(uint userIndex, out XInputState state);
}
"@
}

function Wait-ForXInputReportFlow {
  if ($Profile -ne "xone" -and $Profile -ne "xseries") {
    return
  }

  Add-XInputProbeType
  $deadline = (Get-Date).AddSeconds($DeviceStartTimeoutSeconds)
  $observedStates = @{}

  do {
    for ($index = 0; $index -lt 4; ++$index) {
      $state = New-Object LvhXInputProbe+XInputState
      $status = [LvhXInputProbe]::XInputGetState([uint32] $index, [ref] $state)
      if ($status -ne 0 -or $state.Gamepad.RightTrigger -ne 255) {
        continue
      }

      if (-not $observedStates.ContainsKey($index)) {
        $observedStates[$index] = [pscustomobject] @{
          InitialButtons = $state.Gamepad.Buttons
          ButtonChanged = $false
          LeftThumbXNegative = $false
          LeftThumbXPositive = $false
          RightThumbYNegative = $false
          RightThumbYPositive = $false
        }
      }

      $observed = $observedStates[$index]
      if ($state.Gamepad.Buttons -ne $observed.InitialButtons) {
        $observed.ButtonChanged = $true
      }
      if ($state.Gamepad.LeftThumbX -lt -20000) {
        $observed.LeftThumbXNegative = $true
      } elseif ($state.Gamepad.LeftThumbX -gt 20000) {
        $observed.LeftThumbXPositive = $true
      }
      if ($state.Gamepad.RightThumbY -lt -20000) {
        $observed.RightThumbYNegative = $true
      } elseif ($state.Gamepad.RightThumbY -gt 20000) {
        $observed.RightThumbYPositive = $true
      }

      if ($observed.ButtonChanged -and
          $observed.LeftThumbXNegative -and
          $observed.LeftThumbXPositive -and
          $observed.RightThumbYNegative -and
          $observed.RightThumbYPositive) {
        Write-Information "XInput observed changing $Profile input on index ${index}." -InformationAction Continue
        return
      }
    }

    Start-Sleep -Milliseconds 250
  } while ((Get-Date) -lt $deadline)

  throw "XInput did not observe changing $Profile button, left-stick X, and right-stick Y input from the virtual gamepad."
}

function Get-ExpectedGamepadHardwareId {
  switch ($Profile) {
    "generic" { return "HID\VID_1209&PID_0001" }
    "x360" { return "HID\VID_045E&PID_028E&IG_00" }
    "xone" { return "HID\VID_045E&PID_02EA&IG_00" }
    "xseries" { return "HID\VID_045E&PID_02FF&IG_00" }
    "ds4" { return "HID\VID_054C&PID_05C4" }
    "ds5" { return "HID\VID_054C&PID_0CE6" }
    "switch" { return "HID\VID_057E&PID_2009" }
  }

  throw "Unsupported profile: $Profile"
}

function Wait-ForStartedGamepadChild {
  $deviceId = Get-ExpectedGamepadHardwareId
  $deadline = (Get-Date).AddSeconds($DeviceStartTimeoutSeconds)
  $latestRecords = @()

  do {
    $latestRecords = @(Get-PnPUtilDevicesByDeviceId -DeviceId $deviceId)
    $started = $latestRecords |
      Where-Object {
        $_.Status -eq "Started" -and
        $_.DriverName -ne "hidvhf.inf" -and
        $_.DeviceDescription -ne "Virtual HID Framework (VHF) HID device"
      } |
      Select-Object -First 1
    if ($started) {
      Write-Information "Gamepad child device started: $($started.InstanceId) ($($started.DriverName))" -InformationAction Continue
      return
    }

    Start-Sleep -Milliseconds 500
  } while ((Get-Date) -lt $deadline)

  if (-not $latestRecords) {
    throw "No gamepad child device was found for $deviceId."
  }

  foreach ($record in $latestRecords) {
    Assert-StartedPnPRecord -Record $record -Description "Gamepad child device $deviceId"
  }
}

function Invoke-GamepadAdapterSmoke {
  if (-not $GamepadAdapterPath) {
    return
  }

  if ($Profile -eq "x360") {
    throw "The Windows UMDF/VHF backend does not expose Xbox 360 XUSB gamepads. Use the consumer's XUSB fallback for x360."
  }

  $resolvedGamepadAdapterPath = (Resolve-Path -LiteralPath $GamepadAdapterPath).Path
  $stdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-gamepad-adapter.out"
  $stderrPath = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-gamepad-adapter.err"
  Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

  $process = Start-Process `
    -FilePath $resolvedGamepadAdapterPath `
    -ArgumentList @($Profile, "--hold-seconds", "$HoldSeconds") `
    -PassThru `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath `
    -WindowStyle Hidden

  try {
    Start-Sleep -Seconds 2
    if ($process.HasExited) {
      $stdout = Get-Content -LiteralPath $stdoutPath -Raw -ErrorAction SilentlyContinue
      $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
      throw "gamepad_adapter exited with code $($process.ExitCode).`nstdout:`n$stdout`nstderr:`n$stderr"
    }

    Wait-ForStartedGamepadChild
    Wait-ForXInputReportFlow
  } finally {
    if (-not $process.HasExited) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
  }
}

Assert-RootDeviceStarted
Assert-ControlDeviceOpens
Invoke-GamepadAdapterSmoke
