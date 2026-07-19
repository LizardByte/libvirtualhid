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
  [Alias("Profile")]
  [string] $GamepadProfile = "xseries",

  [int] $HoldSeconds = 12,

  [int] $DeviceStartTimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"

function Invoke-PnPUtil {
  param([string[]] $Arguments)

  $output = @(pnputil.exe @Arguments 2>&1)
  $exitCode = $LASTEXITCODE

  if ($exitCode -ne 0) {
    throw "pnputil.exe $($Arguments -join ' ') exited with code $exitCode`n$($output -join "`n")"
  }

  return $output
}

function ConvertTo-PnPUtilDeviceRecord {
  param([string] $InstanceId)

  return @{
    InstanceId = $InstanceId.Trim()
    DeviceDescription = $null
    DriverName = $null
    HardwareIds = @()
    Status = $null
    ProblemCode = $null
    ProblemStatus = $null
  }
}

function ConvertFrom-PnPUtilDeviceLine {
  param(
    [hashtable] $Record,
    [string] $Line,
    [string] $Section
  )

  $singleValueLabels = @{
    "Device Description" = "DeviceDescription"
    "Driver Name" = "DriverName"
    "Status" = "Status"
    "Problem Code" = "ProblemCode"
    "Problem Status" = "ProblemStatus"
  }

  if ($Line -match "^\s{0,2}([^:]+):\s*(.*)\s*$") {
    $label = $Matches[1].Trim()
    $value = $Matches[2].Trim()
    if ($label -eq "Hardware IDs") {
      if ($value) {
        $Record.HardwareIds += $value
      }
      return "HardwareIds"
    }

    if ($singleValueLabels.ContainsKey($label)) {
      $Record[$singleValueLabels[$label]] = $value
    }
    return $null
  }

  if ($Section -eq "HardwareIds" -and $Line -match "^\s+(.+)\s*$") {
    $Record.HardwareIds += $Matches[1].Trim()
  }

  return $Section
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
      $current = ConvertTo-PnPUtilDeviceRecord -InstanceId $Matches[1]
      $section = $null
      continue
    }

    if (-not $current) {
      continue
    }

    $section = ConvertFrom-PnPUtilDeviceLine -Record $current -Line $line -Section $section
  }

  if ($current) {
    $records += [pscustomobject] $current
  }

  return $records
}

function Write-PnPRecordVerbose {
  param([object] $Record)

  Write-Verbose "Matched device: $($Record.InstanceId)"
  if ($Record.DeviceDescription) {
    Write-Verbose "  Description: $($Record.DeviceDescription)"
  }
  if ($Record.Status) {
    Write-Verbose "  Status: $($Record.Status)"
  }
  if ($Record.DriverName) {
    Write-Verbose "  Driver Name: $($Record.DriverName)"
  }
  if ($Record.HardwareIds) {
    Write-Verbose "  Hardware IDs: $($Record.HardwareIds -join ', ')"
  }
  if ($Record.ProblemCode) {
    Write-Verbose "  Problem Code: $($Record.ProblemCode)"
  }
  if ($Record.ProblemStatus) {
    Write-Verbose "  Problem Status: $($Record.ProblemStatus)"
  }
}

function Get-PnPUtilDevicesByDeviceId {
  param([string] $DeviceId)

  $output = Invoke-PnPUtil -Arguments @(
    "/enum-devices",
    "/deviceids",
    "/drivers"
  )
  $matchingDevices = @(ConvertFrom-PnPUtilDeviceOutput -Output $output |
    Where-Object {
      $_.InstanceId -like "$DeviceId\*" -or
        $_.HardwareIds -contains $DeviceId
    }
  )
  foreach ($device in $matchingDevices) {
    Write-PnPRecordVerbose -Record $device
  }

  return $matchingDevices
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
  param([string] $TargetHardwareId)

  $rootDevices = @(Get-PnPUtilDevicesByDeviceId -DeviceId $TargetHardwareId)
  if (-not $rootDevices) {
    throw "No installed libvirtualhid root device was found for $TargetHardwareId."
  }

  foreach ($device in $rootDevices) {
    Assert-StartedPnPRecord -Record $device -Description "Root device $($device.InstanceId)"
  }
}

function Assert-ControlDeviceOpen {
  param([string] $Path)

  try {
    $stream = [System.IO.File]::Open(
      $Path,
      [System.IO.FileMode]::Open,
      [System.IO.FileAccess]::ReadWrite,
      [System.IO.FileShare]::ReadWrite
    )
    $stream.Dispose()
  } catch {
    throw "Could not open ${Path}: $($_.Exception.Message)"
  }
}

function Get-ExpectedGamepadHardwareId {
  param([string] $ProfileName)

  switch ($ProfileName) {
    "generic" { return @("HID\VID_1209&PID_0001") }
    "x360" { return @("HID\VID_045E&PID_028E&IG_00") }
    "xone" { return @("HID\VID_045E&PID_02EA&IG_00") }
    "xseries" { return @("HID\VID_045E&PID_0B13") }
    "ds4" { return @("HID\VID_054C&PID_05C4") }
    "ds5" { return @("HID\VID_054C&PID_0CE6") }
    "switch" { return @("HID\VID_057E&PID_2009") }
  }

  throw "Unsupported profile: $ProfileName"
}

function Wait-ForStartedGamepadChild {
  param(
    [string] $ProfileName,
    [int] $TimeoutSeconds
  )

  $deviceIds = @(Get-ExpectedGamepadHardwareId -ProfileName $ProfileName)
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $latestRecords = @()

  do {
    $latestRecords = @()
    foreach ($deviceId in $deviceIds) {
      $latestRecords += @(Get-PnPUtilDevicesByDeviceId -DeviceId $deviceId)
    }

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
    throw "No gamepad child device was found for $($deviceIds -join ', ')."
  }

  foreach ($record in $latestRecords) {
    Assert-StartedPnPRecord -Record $record -Description "Gamepad child device $($deviceIds -join ', ')"
  }
}

function Invoke-GamepadAdapterSmoke {
  param(
    [string] $Path,
    [string] $ProfileName,
    [int] $HoldSeconds,
    [int] $DeviceStartTimeoutSeconds
  )

  if (-not $Path) {
    return
  }

  if ($ProfileName -eq "x360") {
    throw "The Windows UMDF/VHF backend does not expose Xbox 360 XUSB gamepads. Use the consumer's XUSB fallback for x360."
  }

  $resolvedGamepadAdapterPath = (Resolve-Path -LiteralPath $Path).Path
  $stdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-gamepad-adapter.out"
  $stderrPath = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-gamepad-adapter.err"
  Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

  $process = Start-Process `
    -FilePath $resolvedGamepadAdapterPath `
    -ArgumentList @($ProfileName, "--hold-seconds", "$HoldSeconds") `
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

    Wait-ForStartedGamepadChild -ProfileName $ProfileName -TimeoutSeconds $DeviceStartTimeoutSeconds
  } finally {
    if (-not $process.HasExited) {
      Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
  }
}

Assert-RootDeviceStarted -TargetHardwareId $HardwareId
Assert-ControlDeviceOpen -Path $ControlDevicePath
Invoke-GamepadAdapterSmoke `
  -Path $GamepadAdapterPath `
  -ProfileName $GamepadProfile `
  -HoldSeconds $HoldSeconds `
  -DeviceStartTimeoutSeconds $DeviceStartTimeoutSeconds
