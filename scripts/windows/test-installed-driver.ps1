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
  [string] $Profile = "x360",

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

function Get-ExpectedGamepadHardwareId {
  switch ($Profile) {
    "generic" { return "HID\VID_1209&PID_0001" }
    "x360" { return "HID\VID_045E&PID_028E&IG_00" }
    "xone" { return "HID\VID_045E&PID_02EA&IG_00" }
    "xseries" { return "HID\VID_045E&PID_0B13&IG_00" }
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
