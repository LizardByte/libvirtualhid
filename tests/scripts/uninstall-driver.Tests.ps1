BeforeAll {
  $sourcePath = Join-Path $PSScriptRoot "..\..\scripts\windows\uninstall-driver.ps1"
  . $sourcePath

  function global:Invoke-LibVirtualHidUninstallTestCommand {
    param([Parameter(ValueFromRemainingArguments)] $Arguments)

    $null = $Arguments
    $global:LASTEXITCODE = $global:LibVirtualHidUninstallTestExitCode
  }
}

AfterAll {
  Remove-Item -LiteralPath Function:\Invoke-LibVirtualHidUninstallTestCommand -ErrorAction SilentlyContinue
  Remove-Variable -Name LibVirtualHidUninstallTestExitCode -Scope Global -ErrorAction SilentlyContinue
}

Describe "Invoke-CheckedCommand" {
  It "returns after an allowed exit code" {
    $global:LibVirtualHidUninstallTestExitCode = 0

    {
      Invoke-CheckedCommand `
        -FilePath "Invoke-LibVirtualHidUninstallTestCommand" `
        -Arguments @("delete")
    } | Should -Not -Throw
  }

  It "throws after a failed command" {
    $global:LibVirtualHidUninstallTestExitCode = 3

    {
      Invoke-CheckedCommand `
        -FilePath "Invoke-LibVirtualHidUninstallTestCommand" `
        -Arguments @("delete")
    } | Should -Throw "*exited with code 3*"
  }
}

Describe "uninstall-driver.ps1 entry point" {
  It "executes normal invocation and validates an explicit package name" {
    {
      & $sourcePath -PublishedName "lib.inf"
    } | Should -Throw "The published driver package name is invalid*"
  }
}

Describe "Remove-LibVirtualHidBrokerService" {
  It "does nothing when the service is absent" {
    Mock Get-Service { $null }
    Mock Stop-Service {}
    Mock Invoke-CheckedCommand {}

    Remove-LibVirtualHidBrokerService -Name "libvirtualhid_broker"

    Should -Invoke Stop-Service -Times 0 -Exactly -Scope It
    Should -Invoke Invoke-CheckedCommand -Times 0 -Exactly -Scope It
  }

  It "stops and deletes an installed service" {
    $service = [pscustomobject]@{
      Status = "Running"
      Disposed = $false
      Waited = $false
    }
    $service | Add-Member -MemberType ScriptMethod -Name WaitForStatus -Value {
      param($Status, $Timeout)
      $null = $Status, $Timeout
      $this.Waited = $true
    }
    $service | Add-Member -MemberType ScriptMethod -Name Dispose -Value {
      $this.Disposed = $true
    }
    $script:getServiceCalls = 0
    Mock Get-Service {
      $script:getServiceCalls += 1
      if ($script:getServiceCalls -eq 1) {
        return $service
      }
      return $null
    }
    Mock Stop-Service {}
    Mock Invoke-CheckedCommand {}

    Remove-LibVirtualHidBrokerService -Name "libvirtualhid_broker" -Confirm:$false

    $service.Waited | Should -BeTrue
    $service.Disposed | Should -BeTrue
    Should -Invoke Stop-Service -Times 1 -Exactly -Scope It
    Should -Invoke Invoke-CheckedCommand -Times 1 -Exactly -Scope It -ParameterFilter {
      $FilePath -eq "sc.exe" -and $Arguments -join "," -eq "delete,libvirtualhid_broker"
    }
  }
}

Describe "Find-PublishedName" {
  It "returns matching DISM driver package names" {
    Mock Get-WindowsDriver {
      @(
        [pscustomobject]@{
          Driver = "oem42.inf"
          OriginalFileName = "C:\drivers\libvirtualhid.inf"
        },
        [pscustomobject]@{
          Driver = "input.inf"
          OriginalFileName = "C:\drivers\libvirtualhid.inf"
        },
        [pscustomobject]@{
          Driver = "oem7.inf"
          OriginalFileName = "C:\drivers\other.inf"
        }
      )
    }
    Mock Get-CimInstance { throw "CIM should not be used" }

    @(Find-PublishedName `
      -TargetOriginalName "libvirtualhid.inf" `
      -TargetHardwareId "ROOT\LIBVIRTUALHID") | Should -Be @("oem42.inf")
    Should -Invoke Get-CimInstance -Times 0 -Exactly -Scope It
  }

  It "returns an empty collection when DISM succeeds without a match" {
    Mock Get-WindowsDriver { @() }

    @(Find-PublishedName `
      -TargetOriginalName "libvirtualhid.inf" `
      -TargetHardwareId "ROOT\LIBVIRTUALHID").Count | Should -Be 0
  }

  It "falls back to the package bound to the target device" {
    Mock Get-WindowsDriver { throw "DISM failed" }
    Mock Get-LibVirtualHidRootDeviceInstanceId { @("ROOT\LIBVIRTUALHID\0000") }
    Mock Get-CimInstance {
      @(
        [pscustomobject]@{
          DeviceID = "ROOT\LIBVIRTUALHID\0000"
          InfName = "oem12.inf"
        },
        [pscustomobject]@{
          DeviceID = "ROOT\OTHER\0000"
          InfName = "oem13.inf"
        }
      )
    }

    @(Find-PublishedName `
      -TargetOriginalName "libvirtualhid.inf" `
      -TargetHardwareId "ROOT\LIBVIRTUALHID") | Should -Be @("oem12.inf")
  }

  It "explains when DISM and the bound-device fallback are unavailable" {
    Mock Get-WindowsDriver { throw "DISM failed" }
    Mock Get-LibVirtualHidRootDeviceInstanceId { @() }

    {
      Find-PublishedName `
        -TargetOriginalName "libvirtualhid.inf" `
        -TargetHardwareId "ROOT\LIBVIRTUALHID"
    } | Should -Throw "*no bound device is available for CIM fallback*"
  }
}

Describe "Published package validation" {
  It "accepts a normal OEM package name" {
    { Assert-PublishedName -Name "oem123.inf" } | Should -Not -Throw
  }

  It "rejects an unsafe package name" {
    { Assert-PublishedName -Name "libvirtualhid.inf" } | Should -Throw "*is invalid*"
  }
}

Describe "Remove-LibVirtualHidDeviceInstance" {
  It "reports pnputil output without warning on success" {
    Mock pnputil.exe {
      $global:LASTEXITCODE = 0
      "Device removed"
    }
    Mock Write-Warning {}

    Remove-LibVirtualHidDeviceInstance `
      -InstanceId "ROOT\LIBVIRTUALHID\0000" `
      -Confirm:$false

    Should -Invoke Write-Warning -Times 0 -Exactly -Scope It
  }

  It "warns and continues after a failed device removal" {
    Mock pnputil.exe { $global:LASTEXITCODE = 5 }
    Mock Write-Warning {}

    Remove-LibVirtualHidDeviceInstance `
      -InstanceId "ROOT\LIBVIRTUALHID\0000" `
      -Confirm:$false

    Should -Invoke Write-Warning -Times 1 -Exactly -Scope It -ParameterFilter {
      $Message -like "pnputil.exe /remove-device*exited with code 5*"
    }
  }
}

Describe "Assert-LibVirtualHidRemoved" {
  BeforeEach {
    Mock Get-Service { $null }
    Mock Get-LibVirtualHidRootDeviceInstanceId { @() }
    Mock Find-PublishedName { @() }
  }

  It "accepts a fully removed driver" {
    {
      Assert-LibVirtualHidRemoved `
        -TargetOriginalName "libvirtualhid.inf" `
        -TargetHardwareId "ROOT\LIBVIRTUALHID" `
        -ServiceName "libvirtualhid_broker"
    } | Should -Not -Throw
  }

  It "rejects a remaining service" {
    Mock Get-Service { [pscustomobject]@{ Name = "libvirtualhid_broker" } }

    {
      Assert-LibVirtualHidRemoved `
        -TargetOriginalName "libvirtualhid.inf" `
        -TargetHardwareId "ROOT\LIBVIRTUALHID" `
        -ServiceName "libvirtualhid_broker"
    } | Should -Throw "*service remains installed*"
  }

  It "rejects remaining device instances" {
    Mock Get-LibVirtualHidRootDeviceInstanceId { @("ROOT\LIBVIRTUALHID\0000") }

    {
      Assert-LibVirtualHidRemoved `
        -TargetOriginalName "libvirtualhid.inf" `
        -TargetHardwareId "ROOT\LIBVIRTUALHID" `
        -ServiceName "libvirtualhid_broker"
    } | Should -Throw "*device instances remain installed*"
  }

  It "rejects remaining staged packages" {
    Mock Find-PublishedName { @("oem4.inf") }

    {
      Assert-LibVirtualHidRemoved `
        -TargetOriginalName "libvirtualhid.inf" `
        -TargetHardwareId "ROOT\LIBVIRTUALHID" `
        -ServiceName "libvirtualhid_broker"
    } | Should -Throw "*driver packages remain staged*"
  }
}

Describe "Remove-DriverCertificate" {
  It "does nothing without a certificate subject" {
    Mock Get-ChildItem { throw "certificate stores should not be read" }

    Remove-DriverCertificate

    Should -Invoke Get-ChildItem -Times 0 -Exactly -Scope It
  }

  It "removes only matching self-signed certificates" {
    Mock Get-ChildItem {
      @(
        [pscustomobject]@{
          Subject = "CN=libvirtualhid Test"
          Issuer = "CN=libvirtualhid Test"
          Thumbprint = "AAA"
        },
        [pscustomobject]@{
          Subject = "CN=libvirtualhid Test"
          Issuer = "CN=Other"
          Thumbprint = "BBB"
        }
      )
    }
    Mock Remove-Item {}

    Remove-DriverCertificate -Subject "CN=libvirtualhid Test" -Confirm:$false

    Should -Invoke Remove-Item -Times 2 -Exactly -Scope It -ParameterFilter {
      $LiteralPath -match "AAA$" -and $Force
    }
  }
}
