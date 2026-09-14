BeforeAll {
  $sourcePath = Join-Path $PSScriptRoot "..\..\scripts\windows\sign-driver-package.ps1"
  . $sourcePath -PackagePath "."

  function global:Invoke-LibVirtualHidSignTestCommand {
    param([Parameter(ValueFromRemainingArguments)] $Arguments)

    $null = $Arguments
    $global:LASTEXITCODE = $global:LibVirtualHidSignTestExitCode
  }
}

AfterAll {
  Remove-Item -LiteralPath Function:\Invoke-LibVirtualHidSignTestCommand -ErrorAction SilentlyContinue
  Remove-Variable -Name LibVirtualHidSignTestExitCode -Scope Global -ErrorAction SilentlyContinue
}

Describe "Find-SignTool" {
  It "uses signtool from PATH when available" {
    Mock Get-Command {
      [pscustomobject]@{ Source = "C:\tools\signtool.exe" }
    }

    Find-SignTool | Should -Be "C:\tools\signtool.exe"
  }

  It "finds the newest x64 SDK signtool" {
    Mock Get-Command { $null }
    Mock Test-Path { $true }
    Mock Get-ChildItem {
      @(
        [pscustomobject]@{ FullName = "C:\sdk\10.0.1\x64\signtool.exe" },
        [pscustomobject]@{ FullName = "C:\sdk\10.0.2\x86\signtool.exe" },
        [pscustomobject]@{ FullName = "C:\sdk\10.0.3\x64\signtool.exe" }
      )
    }
    $previousWindowsSdkDir = $env:WindowsSdkDir
    try {
      $env:WindowsSdkDir = "C:\sdk"

      Find-SignTool | Should -Be "C:\sdk\10.0.3\x64\signtool.exe"
    } finally {
      $env:WindowsSdkDir = $previousWindowsSdkDir
    }
  }

  It "throws when no signtool is installed" {
    Mock Get-Command { $null }
    Mock Test-Path { $false }
    $previousWindowsSdkDir = $env:WindowsSdkDir
    $previousWdkContentRoot = $env:WDKContentRoot
    try {
      $env:WindowsSdkDir = $null
      $env:WDKContentRoot = $null

      { Find-SignTool } | Should -Throw "signtool.exe was not found*"
    } finally {
      $env:WindowsSdkDir = $previousWindowsSdkDir
      $env:WDKContentRoot = $previousWdkContentRoot
    }
  }
}

Describe "sign-driver-package.ps1 entry point" {
  It "executes normal invocation and requires the driver catalog" {
    {
      & $sourcePath -PackagePath $TestDrive
    } | Should -Throw "Driver catalog was not found:*"
  }

  It "creates, exports, uses, and removes a temporary signing certificate" {
    $packagePath = Join-Path $TestDrive "driver"
    $certificatePath = Join-Path $TestDrive "certificates\driver.cer"
    New-Item -ItemType Directory -Path $packagePath | Out-Null
    New-Item -ItemType File -Path (Join-Path $packagePath "libvirtualhid.cat") | Out-Null
    Mock New-SelfSignedCertificate {
      [pscustomobject]@{ Thumbprint = "ABC123" }
    }
    Mock Export-Certificate {} -RemoveParameterType Cert
    Mock Get-Command {
      [pscustomobject]@{ Source = "Invoke-LibVirtualHidSignTestCommand" }
    }
    Mock Remove-Item {}
    $global:LibVirtualHidSignTestExitCode = 0

    & $sourcePath `
      -PackagePath $packagePath `
      -CertificatePath $certificatePath `
      -ValidDays 3

    Should -Invoke New-SelfSignedCertificate -Times 1 -Exactly -Scope It -ParameterFilter {
      $Subject -eq "CN=libvirtualhid CI Test Driver Signing" -and
        $Type -eq "CodeSigningCert" -and
        $KeyLength -eq 3072
    }
    Should -Invoke Export-Certificate -Times 1 -Exactly -Scope It -ParameterFilter {
      $Cert.Thumbprint -eq "ABC123" -and $FilePath -eq $certificatePath
    }
    Should -Invoke Remove-Item -Times 1 -Exactly -Scope It -ParameterFilter {
      $LiteralPath -eq "Cert:\CurrentUser\My\ABC123"
    }
  }
}

Describe "Invoke-CheckedCommand" {
  It "returns after a successful signing command" {
    $global:LibVirtualHidSignTestExitCode = 0

    {
      Invoke-CheckedCommand `
        -FilePath "Invoke-LibVirtualHidSignTestCommand" `
        -Arguments @("sign", "driver.cat")
    } | Should -Not -Throw
  }

  It "throws after a failed signing command" {
    $global:LibVirtualHidSignTestExitCode = 9

    {
      Invoke-CheckedCommand `
        -FilePath "Invoke-LibVirtualHidSignTestCommand" `
        -Arguments @("sign", "driver.cat")
    } | Should -Throw "*exited with code 9*"
  }
}
