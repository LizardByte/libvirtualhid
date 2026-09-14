BeforeAll {
  $sourcePath = Join-Path $PSScriptRoot "..\..\scripts\windows\test-installed-driver.ps1"
  . $sourcePath
}

Describe "Invoke-PnPUtil" {
  It "returns pnputil output on success" {
    Mock pnputil.exe {
      $global:LASTEXITCODE = 0
      @("first", "second")
    }

    @(Invoke-PnPUtil -Arguments @("/enum-devices")) | Should -Be @("first", "second")
  }

  It "throws with command output on failure" {
    Mock pnputil.exe {
      $global:LASTEXITCODE = 6
      "access denied"
    }

    {
      Invoke-PnPUtil -Arguments @("/enum-devices")
    } | Should -Throw "*exited with code 6*access denied*"
  }
}

Describe "test-installed-driver.ps1 entry point" {
  It "executes normal invocation and requires an installed root device" {
    function pnputil.exe {
      $global:LASTEXITCODE = 0
      @()
    }

    try {
      {
        & $sourcePath -HardwareId "ROOT\LIBVIRTUALHID_PESTER"
      } | Should -Throw "No installed libvirtualhid root device was found*"
    } finally {
      Remove-Item -LiteralPath Function:\pnputil.exe
    }
  }
}

Describe "PnPUtil device output parsing" {
  It "creates an empty record with a trimmed instance ID" {
    $record = ConvertTo-PnPUtilDeviceRecord -InstanceId "  ROOT\LIBVIRTUALHID\0000  "

    $record.InstanceId | Should -Be "ROOT\LIBVIRTUALHID\0000"
    $record.HardwareIds.Count | Should -Be 0
    $record.Status | Should -BeNullOrEmpty
  }

  It "parses single-value fields and hardware ID continuations" {
    $record = ConvertTo-PnPUtilDeviceRecord -InstanceId "ROOT\LIBVIRTUALHID\0000"

    $section = ConvertFrom-PnPUtilDeviceLine `
      -Record $record `
      -Line "Device Description: libvirtualhid" `
      -Section $null
    $section | Should -BeNullOrEmpty
    $section = ConvertFrom-PnPUtilDeviceLine `
      -Record $record `
      -Line "Hardware IDs: ROOT\LIBVIRTUALHID" `
      -Section $section
    $section | Should -Be "HardwareIds"
    $section = ConvertFrom-PnPUtilDeviceLine `
      -Record $record `
      -Line "    ROOT\LIBVIRTUALHID_COMPAT" `
      -Section $section

    $record.DeviceDescription | Should -Be "libvirtualhid"
    $record.HardwareIds | Should -Be @("ROOT\LIBVIRTUALHID", "ROOT\LIBVIRTUALHID_COMPAT")
  }

  It "parses multiple complete device records" {
    $output = @(
      "Microsoft PnP Utility",
      "Instance ID: ROOT\LIBVIRTUALHID\0000",
      "Device Description: libvirtualhid root",
      "Driver Name: libvirtualhid.inf",
      "Status: Started",
      "Problem Code: 0",
      "Problem Status: 0x0",
      "Hardware IDs: ROOT\LIBVIRTUALHID",
      "Instance ID: HID\VID_1209&PID_0001\0000",
      "Device Description: virtual gamepad",
      "Status: Stopped"
    )

    $records = @(ConvertFrom-PnPUtilDeviceOutput -Output $output)

    $records.Count | Should -Be 2
    $records[0].Status | Should -Be "Started"
    $records[0].DriverName | Should -Be "libvirtualhid.inf"
    $records[0].ProblemCode | Should -Be "0"
    $records[0].ProblemStatus | Should -Be "0x0"
    $records[1].InstanceId | Should -Be "HID\VID_1209&PID_0001\0000"
  }
}

Describe "PnP device validation" {
  It "writes every available device field as verbose output" {
    $record = [pscustomobject]@{
      InstanceId = "ROOT\LIBVIRTUALHID\0000"
      DeviceDescription = "libvirtualhid root"
      Status = "Started"
      DriverName = "libvirtualhid.inf"
      HardwareIds = @("ROOT\LIBVIRTUALHID")
      ProblemCode = "0"
      ProblemStatus = "0x0"
    }
    Mock Write-Verbose {}

    Write-PnPRecordVerbose -Record $record

    Should -Invoke Write-Verbose -Times 7 -Exactly -Scope It
  }

  It "filters device records by instance ID and hardware ID" {
    Mock Invoke-PnPUtil {
      @(
        "Instance ID: ROOT\LIBVIRTUALHID\0000",
        "Status: Started",
        "Instance ID: CUSTOM\0000",
        "Hardware IDs: ROOT\LIBVIRTUALHID",
        "Status: Started",
        "Instance ID: ROOT\OTHER\0000",
        "Status: Started"
      )
    }
    Mock Write-PnPRecordVerbose {}

    $result = @(Get-PnPUtilDevicesByDeviceId -DeviceId "ROOT\LIBVIRTUALHID")

    $result.Count | Should -Be 2
    Should -Invoke Write-PnPRecordVerbose -Times 2 -Exactly -Scope It
  }

  It "accepts a started device" {
    $record = [pscustomobject]@{
      InstanceId = "ROOT\LIBVIRTUALHID\0000"
      Status = "Started"
      ProblemCode = $null
      ProblemStatus = $null
    }

    { Assert-StartedPnPRecord -Record $record -Description "Root device" } |
      Should -Not -Throw
  }

  It "reports status and problem details for a stopped device" {
    $record = [pscustomobject]@{
      InstanceId = "ROOT\LIBVIRTUALHID\0000"
      Status = "Stopped"
      ProblemCode = "28"
      ProblemStatus = "0xC0000490"
    }

    { Assert-StartedPnPRecord -Record $record -Description "Root device" } |
      Should -Throw "*Problem Code: 28*Problem Status: 0xC0000490*"
  }

  It "requires at least one root device" {
    Mock Get-PnPUtilDevicesByDeviceId { @() }

    { Assert-RootDeviceStarted -TargetHardwareId "ROOT\LIBVIRTUALHID" } |
      Should -Throw "No installed libvirtualhid root device was found*"
  }

  It "validates every matching root device" {
    Mock Get-PnPUtilDevicesByDeviceId {
      @(
        [pscustomobject]@{ InstanceId = "one"; Status = "Started" },
        [pscustomobject]@{ InstanceId = "two"; Status = "Started" }
      )
    }
    Mock Assert-StartedPnPRecord {}

    Assert-RootDeviceStarted -TargetHardwareId "ROOT\LIBVIRTUALHID"

    Should -Invoke Assert-StartedPnPRecord -Times 2 -Exactly -Scope It
  }
}

Describe "Control device validation" {
  It "opens an existing file for shared read and write access" {
    $path = Join-Path $TestDrive "control-device"
    New-Item -ItemType File -Path $path | Out-Null

    { Assert-ControlDeviceOpen -Path $path } | Should -Not -Throw
  }

  It "explains when the control device cannot be opened" {
    {
      Assert-ControlDeviceOpen -Path (Join-Path $TestDrive "missing-device")
    } | Should -Throw "Could not open*"
  }
}

Describe "Gamepad profile metadata" {
  It "returns the expected hardware ID for <profile>" -ForEach @(
    @{ profile = "generic"; expected = "HID\VID_1209&PID_0001" }
    @{ profile = "xone"; expected = "HID\VID_045E&PID_02EA&IG_00" }
    @{ profile = "xseries"; expected = "HID\VID_045E&PID_0B12&IG_00" }
    @{ profile = "ds4"; expected = "HID\VID_054C&PID_05C4" }
    @{ profile = "ds5"; expected = "HID\VID_054C&PID_0CE6" }
    @{ profile = "switch"; expected = "HID\VID_057E&PID_2009" }
  ) {
    @(Get-ExpectedGamepadHardwareId -ProfileName $profile) | Should -Be @($expected)
  }

  It "rejects an unsupported profile" {
    { Get-ExpectedGamepadHardwareId -ProfileName "unknown" } |
      Should -Throw "Unsupported profile: unknown"
  }
}

Describe "Gamepad device waits" {
  BeforeEach {
    Mock Start-Sleep {}
  }

  It "accepts a started non-VHF child device" {
    Mock Get-PnPUtilDevicesByDeviceId {
      [pscustomobject]@{
        InstanceId = "HID\GAMEPAD\0000"
        Status = "Started"
        DriverName = "xboxgip.inf"
        DeviceDescription = "Xbox Controller"
      }
    }

    {
      Wait-ForStartedGamepadChild -ProfileName "xseries" -TimeoutSeconds 1
    } | Should -Not -Throw
  }

  It "rejects a missing gamepad child after the timeout" {
    Mock Get-PnPUtilDevicesByDeviceId { @() }

    {
      Wait-ForStartedGamepadChild -ProfileName "xseries" -TimeoutSeconds 0
    } | Should -Throw "No gamepad child device was found*"
  }

  It "accepts a started Xbox 360 companion" {
    Mock Get-PnPUtilDevicesByDeviceId {
      [pscustomobject]@{
        InstanceId = "ROOT\LIBVIRTUALHID_XBOX360\0000"
        Status = "Started"
        DriverName = "xusb22.inf"
      }
    }

    {
      Wait-ForStartedXbox360Companion -TimeoutSeconds 1
    } | Should -Not -Throw
  }

  It "rejects a missing Xbox 360 companion after the timeout" {
    Mock Get-PnPUtilDevicesByDeviceId { @() }

    {
      Wait-ForStartedXbox360Companion -TimeoutSeconds 0
    } | Should -Throw "No Xbox 360 XUSB companion was found*"
  }
}

Describe "Invoke-GamepadAdapterSmoke" {
  It "does nothing when no adapter path is supplied" {
    Mock Start-Process { throw "adapter should not start" }

    Invoke-GamepadAdapterSmoke `
      -ProfileName "xseries" `
      -HoldSeconds 12 `
      -DeviceStartTimeoutSeconds 1

    Should -Invoke Start-Process -Times 0 -Exactly -Scope It
  }

  It "starts, validates, and stops an adapter" {
    $path = Join-Path $TestDrive "gamepad_adapter.exe"
    New-Item -ItemType File -Path $path | Out-Null
    $process = [pscustomobject]@{ HasExited = $false; Id = 42 }
    Mock Start-Process { $process }
    Mock Start-Sleep {}
    Mock Wait-ForStartedGamepadChild {}
    Mock Stop-Process {}
    Mock Wait-Process {}

    Invoke-GamepadAdapterSmoke `
      -Path $path `
      -ProfileName "xseries" `
      -HoldSeconds 12 `
      -DeviceStartTimeoutSeconds 1

    Should -Invoke Wait-ForStartedGamepadChild -Times 1 -Exactly -Scope It
    Should -Invoke Stop-Process -Times 1 -Exactly -Scope It -ParameterFilter {
      $Id -eq 42 -and $Force
    }
  }

  It "uses the Xbox 360 companion validation path" {
    $path = Join-Path $TestDrive "x360-gamepad_adapter.exe"
    New-Item -ItemType File -Path $path | Out-Null
    $process = [pscustomobject]@{ HasExited = $false; Id = 43 }
    Mock Start-Process { $process }
    Mock Start-Sleep {}
    Mock Wait-ForStartedXbox360Companion {}
    Mock Stop-Process {}
    Mock Wait-Process {}

    Invoke-GamepadAdapterSmoke `
      -Path $path `
      -ProfileName "x360" `
      -HoldSeconds 12 `
      -DeviceStartTimeoutSeconds 1

    Should -Invoke Wait-ForStartedXbox360Companion -Times 1 -Exactly -Scope It
  }
}
