BeforeAll {
  $sourcePath = Join-Path $PSScriptRoot "..\..\scripts\windows\libvirtualhid-driver-common.ps1"
  . $sourcePath
}

Describe "Transcript helpers" {
  BeforeEach {
    $script:LibVirtualHidTranscriptStarted = $false
    Mock New-Item {}
    Mock Start-Transcript {}
    Mock Stop-Transcript {}
    Mock Write-Warning {}
  }

  It "does nothing when no transcript path is supplied" {
    Start-LibVirtualHidTranscript

    Should -Invoke Start-Transcript -Times 0 -Exactly -Scope It
  }

  It "creates the log directory and starts and stops a transcript" {
    Start-LibVirtualHidTranscript -Path "C:\logs\driver.log"
    Stop-LibVirtualHidTranscript

    $script:LibVirtualHidTranscriptStarted | Should -BeTrue
    Should -Invoke New-Item -Times 1 -Exactly -Scope It -ParameterFilter {
      $ItemType -eq "Directory" -and $Path -eq "C:\logs"
    }
    Should -Invoke Start-Transcript -Times 1 -Exactly -Scope It -ParameterFilter {
      $Path -eq "C:\logs\driver.log" -and $Append
    }
    Should -Invoke Stop-Transcript -Times 1 -Exactly -Scope It
  }

  It "does not stop a transcript that was never started" {
    Stop-LibVirtualHidTranscript

    Should -Invoke Stop-Transcript -Times 0 -Exactly -Scope It
  }

  It "warns when starting the transcript fails" {
    Mock Start-Transcript { throw "start failed" }

    Start-LibVirtualHidTranscript -Path "driver.log"

    Should -Invoke Write-Warning -Times 1 -Exactly -Scope It -ParameterFilter {
      $Message -eq "Unable to start libvirtualhid driver transcript: start failed"
    }
  }

  It "warns when stopping the transcript fails" {
    $script:LibVirtualHidTranscriptStarted = $true
    Mock Stop-Transcript { throw "stop failed" }

    Stop-LibVirtualHidTranscript

    Should -Invoke Write-Warning -Times 1 -Exactly -Scope It -ParameterFilter {
      $Message -eq "Unable to stop libvirtualhid driver transcript: stop failed"
    }
  }
}

Describe "Get-LibVirtualHidRootDeviceInstanceId" {
  BeforeEach {
    Mock Write-Verbose {}
  }

  It "returns instance identifiers reported by pnputil" {
    Mock pnputil.exe {
      $global:LASTEXITCODE = 0
      @(
        "Instance ID: ROOT\LIBVIRTUALHID\0000",
        "  Instance ID : ROOT\LIBVIRTUALHID\0001  "
      )
    }
    Mock Get-CimInstance { throw "CIM should not be used" }

    $result = @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId "ROOT\LIBVIRTUALHID")

    $result | Should -Be @("ROOT\LIBVIRTUALHID\0000", "ROOT\LIBVIRTUALHID\0001")
    Should -Invoke Get-CimInstance -Times 0 -Exactly -Scope It
  }

  It "falls back to matching CIM devices" {
    Mock pnputil.exe { $global:LASTEXITCODE = 1 }
    Mock Get-CimInstance {
      @(
        [pscustomobject]@{
          PNPDeviceID = "ROOT\LIBVIRTUALHID\0000"
          HardwareID = @("ROOT\LIBVIRTUALHID")
        },
        [pscustomobject]@{
          PNPDeviceID = "ROOT\OTHER\0000"
          HardwareID = @("ROOT\OTHER")
        },
        [pscustomobject]@{
          PNPDeviceID = "CUSTOM\INSTANCE"
          HardwareID = @("ROOT\LIBVIRTUALHID")
        }
      )
    }

    $result = @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId "ROOT\LIBVIRTUALHID")

    $result | Should -Be @("ROOT\LIBVIRTUALHID\0000", "CUSTOM\INSTANCE")
  }

  It "returns an empty collection when both enumerators fail" {
    Mock pnputil.exe { throw "pnputil failed" }
    Mock Get-CimInstance { throw "CIM failed" }

    @(Get-LibVirtualHidRootDeviceInstanceId -TargetHardwareId "ROOT\LIBVIRTUALHID").Count |
      Should -Be 0
    Should -Invoke Write-Verbose -Times 2 -Exactly -Scope It
  }
}

Describe "Get-LibVirtualHidRegistryRootDevice" {
  BeforeEach {
    Mock Get-ChildItem {
      if ($LiteralPath -eq "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT") {
        return [pscustomobject]@{
          PSChildName = "LIBVIRTUALHID"
          PSPath = "root-key"
        }
      }
      if ($LiteralPath -eq "root-key") {
        return [pscustomobject]@{
          PSChildName = "0000"
          PSPath = "instance-key"
        }
      }
    }
    Mock Write-Verbose {}
  }

  It "reports exact hardware IDs and legacy HID class state" {
    Mock Get-ItemProperty {
      if ($Name -eq "HardwareID") {
        return [pscustomobject]@{ HardwareID = @("ROOT\LIBVIRTUALHID") }
      }
      return [pscustomobject]@{ ClassGUID = "{745A17A0-74D3-11D0-B6FE-00A0C90F57DA}" }
    }

    $result = @(Get-LibVirtualHidRegistryRootDevice -TargetHardwareId "ROOT\LIBVIRTUALHID")

    $result.Count | Should -Be 1
    $result[0].InstanceId | Should -Be "ROOT\LIBVIRTUALHID\0000"
    $result[0].HasExactHardwareId | Should -BeTrue
    $result[0].HasCorruptHardwareId | Should -BeFalse
    $result[0].HasLegacyHidClass | Should -BeTrue
  }

  It "recognizes a hardware ID split across corrupt registry values" {
    Mock Get-ItemProperty {
      if ($Name -eq "HardwareID") {
        return [pscustomobject]@{ HardwareID = @("ROOT\LIBVIRTUAL", "HID") }
      }
      throw "properties unavailable"
    }

    $result = @(Get-LibVirtualHidRegistryRootDevice -TargetHardwareId "ROOT\LIBVIRTUALHID")

    $result.Count | Should -Be 1
    $result[0].HasExactHardwareId | Should -BeFalse
    $result[0].HasCorruptHardwareId | Should -BeTrue
    $result[0].HasLegacyHidClass | Should -BeFalse
    Should -Invoke Write-Verbose -Times 1 -Exactly -Scope It
  }
}
