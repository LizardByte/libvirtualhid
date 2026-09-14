BeforeAll {
  $sourcePath = Join-Path $PSScriptRoot "..\..\scripts\windows\install-driver.ps1"
  . $sourcePath -InfPath "unused.inf"

  function global:Invoke-LibVirtualHidTestCommand {
    param([Parameter(ValueFromRemainingArguments)] $Arguments)

    $null = $Arguments
    $global:LASTEXITCODE = $global:LibVirtualHidTestExitCode
  }
}

AfterAll {
  Remove-Item -LiteralPath Function:\Invoke-LibVirtualHidTestCommand -ErrorAction SilentlyContinue
  Remove-Variable -Name LibVirtualHidTestExitCode -Scope Global -ErrorAction SilentlyContinue
}

Describe "Invoke-CheckedCommand" {
  It "accepts a configured success exit code" {
    $global:LibVirtualHidTestExitCode = 5

    {
      Invoke-CheckedCommand `
        -FilePath "Invoke-LibVirtualHidTestCommand" `
        -Arguments @("one", "two") `
        -SuccessExitCodes @(0, 5)
    } | Should -Not -Throw
  }

  It "throws for an unexpected exit code" {
    $global:LibVirtualHidTestExitCode = 12

    {
      Invoke-CheckedCommand `
        -FilePath "Invoke-LibVirtualHidTestCommand" `
        -Arguments @("one")
    } | Should -Throw "*exited with code 12*"
  }
}

Describe "install-driver.ps1 entry point" {
  It "executes normal invocation and validates the INF path" {
    {
      & $sourcePath -InfPath (Join-Path $TestDrive "missing.inf") -StageOnly
    } | Should -Throw "*Cannot find path*"
  }

  It "runs the complete orchestration path safely with WhatIf" {
    $infPath = Join-Path $TestDrive "libvirtualhid.inf"
    $setupPath = Join-Path $TestDrive "libvirtualhid_driver_setup.exe"
    New-Item -ItemType File -Path $infPath, $setupPath | Out-Null
    function pnputil.exe {
      $global:LASTEXITCODE = 0
      @()
    }
    Mock Get-CimInstance { @() }
    Mock Get-ChildItem { @() }

    try {
      {
        & $sourcePath `
          -InfPath $infPath `
          -SetupPath $setupPath `
          -HardwareId "ROOT\LIBVIRTUALHID_PESTER" `
          -WhatIf
      } | Should -Not -Throw
    } finally {
      Remove-Item -LiteralPath Function:\pnputil.exe
    }
  }
}

Describe "Driver package path helpers" {
  It "resolves an explicitly supplied broker" {
    $path = Join-Path $TestDrive "libvirtualhid_broker.exe"
    New-Item -ItemType File -Path $path | Out-Null

    Resolve-LibVirtualHidBrokerPath -Path $path | Should -Be (Resolve-Path $path).Path
  }

  It "rejects a missing explicitly supplied broker" {
    {
      Resolve-LibVirtualHidBrokerPath -Path (Join-Path $TestDrive "missing.exe")
    } | Should -Throw "The broker executable was not found*"
  }

  It "returns null when no packaged broker exists" {
    Resolve-LibVirtualHidBrokerPath | Should -BeNullOrEmpty
  }

  It "resolves an explicitly supplied setup helper" {
    $path = Join-Path $TestDrive "libvirtualhid_driver_setup.exe"
    New-Item -ItemType File -Path $path | Out-Null

    Resolve-LibVirtualHidDriverSetupPath -Path $path | Should -Be (Resolve-Path $path).Path
  }

  It "rejects a missing explicitly supplied setup helper" {
    {
      Resolve-LibVirtualHidDriverSetupPath -Path (Join-Path $TestDrive "missing.exe")
    } | Should -Throw "The driver setup helper was not found*"
  }
}

Describe "Service path quoting" {
  It "quotes a broker path" {
    Get-LibVirtualHidQuotedServiceBinaryPath -Path "C:\Program Files\libvirtualhid\broker.exe" |
      Should -Be '"C:\Program Files\libvirtualhid\broker.exe"'
  }

  It "rejects quotation marks in a broker path" {
    {
      Get-LibVirtualHidQuotedServiceBinaryPath -Path 'C:\bad"path\broker.exe'
    } | Should -Throw "*must not contain quotation marks*"
  }

  It "formats the service path for Windows PowerShell native argument parsing" {
    Get-LibVirtualHidScBinaryPathArgument -Path "C:\Program Files\libvirtualhid\broker.exe" |
      Should -Be '"""C:\Program Files\libvirtualhid\broker.exe"""'
  }

  It "accepts an exactly quoted service ImagePath" {
    Mock Get-ItemProperty {
      [pscustomobject]@{ ImagePath = '"C:\Program Files\libvirtualhid\broker.exe"' }
    }

    {
      Assert-LibVirtualHidBrokerServiceImagePath `
        -Name "libvirtualhid_broker" `
        -Path "C:\Program Files\libvirtualhid\broker.exe"
    } | Should -Not -Throw
  }

  It "rejects an unquoted service ImagePath" {
    Mock Get-ItemProperty {
      [pscustomobject]@{ ImagePath = "C:\Program Files\libvirtualhid\broker.exe" }
    }

    {
      Assert-LibVirtualHidBrokerServiceImagePath `
        -Name "libvirtualhid_broker" `
        -Path "C:\Program Files\libvirtualhid\broker.exe"
    } | Should -Throw "*is not safely quoted*"
  }
}

Describe "Broker service helpers" {
  BeforeEach {
    Mock Remove-ItemProperty {}
    Mock Stop-Service {}
  }

  It "does not clear a missing service registry key" {
    Mock Test-Path { $false }

    Clear-LibVirtualHidBrokerServiceEnvironment -Name "libvirtualhid_broker"

    Should -Invoke Remove-ItemProperty -Times 0 -Exactly -Scope It
  }

  It "clears a legacy service environment" {
    Mock Test-Path { $true }

    Clear-LibVirtualHidBrokerServiceEnvironment -Name "libvirtualhid_broker" -Confirm:$false

    Should -Invoke Remove-ItemProperty -Times 1 -Exactly -Scope It -ParameterFilter {
      $LiteralPath -eq "HKLM:\SYSTEM\CurrentControlSet\Services\libvirtualhid_broker" -and
        $Name -eq "Environment"
    }
  }

  It "does nothing when the broker service is absent" {
    Mock Get-Service { $null }

    Stop-LibVirtualHidBrokerService -Name "libvirtualhid_broker"

    Should -Invoke Stop-Service -Times 0 -Exactly -Scope It
  }

  It "stops a running broker service" {
    $service = [pscustomobject]@{
      Status = "Running"
      Waited = $false
    }
    $service | Add-Member -MemberType ScriptMethod -Name WaitForStatus -Value {
      param($Status, $Timeout)
      $null = $Status, $Timeout
      $this.Waited = $true
    }
    Mock Get-Service { $service }

    Stop-LibVirtualHidBrokerService -Name "libvirtualhid_broker" -Confirm:$false

    $service.Waited | Should -BeTrue
    Should -Invoke Stop-Service -Times 1 -Exactly -Scope It -ParameterFilter {
      $Name -eq "libvirtualhid_broker" -and $Force
    }
  }

  It "skips service registration when no broker executable exists" {
    Mock Write-Verbose {}

    Install-LibVirtualHidBrokerService

    Should -Invoke Write-Verbose -Times 1 -Exactly -Scope It -ParameterFilter {
      $Message -like "No libvirtualhid broker executable was found*"
    }
  }

  It "registers and configures a new broker service" {
    $path = Join-Path $TestDrive "libvirtualhid_broker.exe"
    New-Item -ItemType File -Path $path | Out-Null
    $resolvedPath = (Resolve-Path $path).Path
    Mock Get-Service { $null }
    Mock New-Service {}
    Mock Assert-LibVirtualHidBrokerServiceImagePath {}
    Mock Clear-LibVirtualHidBrokerServiceEnvironment {}
    Mock Invoke-CheckedCommand {}
    Mock Start-Service {}

    Install-LibVirtualHidBrokerService -Path $path -Confirm:$false

    Should -Invoke New-Service -Times 1 -Exactly -Scope It -ParameterFilter {
      $Name -eq "libvirtualhid_broker" -and
        $BinaryPathName -eq ('"' + $resolvedPath + '"') -and
        $StartupType -eq "Automatic"
    }
    Should -Invoke Assert-LibVirtualHidBrokerServiceImagePath -Times 1 -Exactly -Scope It
    Should -Invoke Clear-LibVirtualHidBrokerServiceEnvironment -Times 1 -Exactly -Scope It
    Should -Invoke Invoke-CheckedCommand -Times 2 -Exactly -Scope It
    Should -Invoke Start-Service -Times 1 -Exactly -Scope It
  }

  It "updates and configures an existing broker service" {
    $path = Join-Path $TestDrive "existing-libvirtualhid_broker.exe"
    New-Item -ItemType File -Path $path | Out-Null
    Mock Get-Service { [pscustomobject]@{ Status = "Stopped" } }
    Mock Stop-LibVirtualHidBrokerService {}
    Mock Assert-LibVirtualHidBrokerServiceImagePath {}
    Mock Clear-LibVirtualHidBrokerServiceEnvironment {}
    Mock Invoke-CheckedCommand {}
    Mock Start-Service {}

    Install-LibVirtualHidBrokerService -Path $path -Confirm:$false

    Should -Invoke Stop-LibVirtualHidBrokerService -Times 1 -Exactly -Scope It
    Should -Invoke Invoke-CheckedCommand -Times 3 -Exactly -Scope It
    Should -Invoke Invoke-CheckedCommand -Times 1 -Exactly -Scope It -ParameterFilter {
      $FilePath -eq "sc.exe" -and $Arguments[0] -eq "config"
    }
    Should -Invoke Start-Service -Times 1 -Exactly -Scope It
  }
}

Describe "Driver installation helpers" {
  It "imports an existing certificate into both required stores" {
    $path = Join-Path $TestDrive "driver.cer"
    New-Item -ItemType File -Path $path | Out-Null
    Mock Import-Certificate {}

    Import-DriverCertificate -Path $path -Confirm:$false

    Should -Invoke Import-Certificate -Times 2 -Exactly -Scope It -ParameterFilter {
      $FilePath -eq (Resolve-Path $path).Path -and
        $CertStoreLocation -in @("Cert:\LocalMachine\Root", "Cert:\LocalMachine\TrustedPublisher")
    }
  }

  It "ignores a missing certificate" {
    Mock Import-Certificate {}

    Import-DriverCertificate -Path (Join-Path $TestDrive "missing.cer")

    Should -Invoke Import-Certificate -Times 0 -Exactly -Scope It
  }

  It "removes a stale device with pnputil" {
    Mock Invoke-CheckedCommand {}

    Remove-DeviceInstance -InstanceId "ROOT\LIBVIRTUALHID\0000" -Confirm:$false

    Should -Invoke Invoke-CheckedCommand -Times 1 -Exactly -Scope It -ParameterFilter {
      $FilePath -eq "pnputil.exe" -and
        $Arguments[0] -eq "/remove-device" -and
        $Arguments[1] -eq "ROOT\LIBVIRTUALHID\0000"
    }
  }

  It "sets VhfMode on an existing root device" {
    Mock Test-Path { $true }
    Mock New-ItemProperty {}

    Set-RootDeviceVhfMode -InstanceId "ROOT\LIBVIRTUALHID\0000" -Confirm:$false

    Should -Invoke New-ItemProperty -Times 1 -Exactly -Scope It -ParameterFilter {
      $Name -eq "VhfMode" -and $Value -eq 1 -and $PropertyType -eq "DWord"
    }
  }

  It "skips VhfMode when the registry device is absent" {
    Mock Test-Path { $false }
    Mock New-ItemProperty {}
    Mock Write-Verbose {}

    Set-RootDeviceVhfMode -InstanceId "ROOT\LIBVIRTUALHID\0000"

    Should -Invoke New-ItemProperty -Times 0 -Exactly -Scope It
    Should -Invoke Write-Verbose -Times 1 -Exactly -Scope It
  }

  It "warns when restarting the device requires a reboot" {
    Mock pnputil.exe {
      $global:LASTEXITCODE = 0
      "A reboot is needed to complete the operation."
    }
    Mock Write-Warning {}

    Restart-RootDevice -InstanceId "ROOT\LIBVIRTUALHID\0000" -Confirm:$false

    Should -Invoke Write-Warning -Times 1 -Exactly -Scope It -ParameterFilter {
      $Message -like "Windows reported that a reboot is required*"
    }
  }

  It "throws when restarting the device fails" {
    Mock pnputil.exe { $global:LASTEXITCODE = 7 }

    {
      Restart-RootDevice -InstanceId "ROOT\LIBVIRTUALHID\0000" -Confirm:$false
    } | Should -Throw "*exited with code 7*"
  }

  It "updates a driver and permits SetupAPI reboot status" {
    Mock Invoke-CheckedCommand { $global:LASTEXITCODE = 3010 }
    Mock Write-Warning {}

    Update-RootDeviceDriverWithSetupApi `
      -Path "driver.inf" `
      -TargetHardwareId "ROOT\LIBVIRTUALHID" `
      -SetupHelperPath "setup.exe" `
      -Confirm:$false

    Should -Invoke Invoke-CheckedCommand -Times 1 -Exactly -Scope It -ParameterFilter {
      $FilePath -eq "setup.exe" -and
        $Arguments[0] -eq "update" -and
        $SuccessExitCodes -contains 3010
    }
    Should -Invoke Write-Warning -Times 1 -Exactly -Scope It
  }

  It "installs a root device through the SetupAPI helper" {
    Mock Invoke-CheckedCommand {}

    Install-RootDeviceWithSetupApi `
      -Path "driver.inf" `
      -TargetHardwareId "ROOT\LIBVIRTUALHID" `
      -SetupHelperPath "setup.exe"

    Should -Invoke Invoke-CheckedCommand -Times 1 -Exactly -Scope It -ParameterFilter {
      $FilePath -eq "setup.exe" -and $Arguments -join "," -eq "install,driver.inf,ROOT\LIBVIRTUALHID"
    }
  }
}
