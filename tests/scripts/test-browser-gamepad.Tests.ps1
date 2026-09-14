BeforeAll {
  $sourcePath = Join-Path $PSScriptRoot "..\..\scripts\windows\test-browser-gamepad.ps1"
  . $sourcePath -GamepadAdapterPath "unused.exe"
}

Describe "Get-ExpectedGamepadIdPattern" {
  It "returns an identifying expression for <profile>" -ForEach @(
    @{ profile = "generic"; expected = "1209" }
    @{ profile = "x360"; expected = "028e" }
    @{ profile = "xone"; expected = "02ea" }
    @{ profile = "xseries"; expected = "0b12" }
    @{ profile = "ds4"; expected = "05c4" }
    @{ profile = "ds5"; expected = "0ce6" }
    @{ profile = "switch"; expected = "2009" }
  ) {
    Get-ExpectedGamepadIdPattern -ProfileName $profile | Should -Match $expected
  }

  It "rejects an unsupported profile" {
    { Get-ExpectedGamepadIdPattern -ProfileName "unknown" } |
      Should -Throw "Unsupported profile: unknown"
  }
}

Describe "test-browser-gamepad.ps1 entry point" {
  It "executes normal invocation and validates the polling lifetime" {
    {
      & $sourcePath `
        -GamepadAdapterPath "unused.exe" `
        -TimeoutSeconds 20 `
        -HoldSeconds 20
    } | Should -Throw "-HoldSeconds must be greater than -TimeoutSeconds*"
  }
}

Describe "Resolve-BrowserPath" {
  It "resolves an explicitly supplied browser path" {
    $path = Join-Path $TestDrive "browser.exe"
    New-Item -ItemType File -Path $path | Out-Null

    Resolve-BrowserPath -Path $path | Should -Be (Resolve-Path $path).Path
  }

  It "selects the first installed supported browser" {
    Mock Test-Path { $LiteralPath -like "*Microsoft\Edge\Application\msedge.exe" }

    Resolve-BrowserPath | Should -Match "Microsoft\\Edge\\Application\\msedge.exe$"
  }

  It "requires an installed supported browser" {
    Mock Test-Path { $false }

    { Resolve-BrowserPath } | Should -Throw "No supported browser was found*"
  }
}

Describe "Get-FreeTcpPort" {
  It "returns an available ephemeral loopback port" {
    Get-FreeTcpPort | Should -BeGreaterThan 0
  }
}

Describe "Wait-ForDevToolsJson" {
  It "returns JSON from the browser endpoint" {
    $response = [pscustomobject]@{ Browser = "Edge" }
    Mock Invoke-RestMethod { $response }

    Wait-ForDevToolsJson -Port 9222 -Path "/json/version" -TimeoutSeconds 1 |
      Should -Be $response
  }

  It "times out after repeated endpoint failures" {
    Mock Invoke-RestMethod { throw "not ready" }
    Mock Start-Sleep {}

    {
      Wait-ForDevToolsJson -Port 9222 -Path "/json/version" -TimeoutSeconds 0
    } | Should -Throw "Timed out waiting for browser DevTools endpoint*"
    Should -Invoke Start-Sleep -Times 1 -Exactly -Scope It
  }
}

Describe "Wait-ForDevToolsPageTarget" {
  It "selects the page matching the expected URL" {
    Mock Wait-ForDevToolsJson {
      @(
        [pscustomobject]@{
          type = "page"
          url = "edge://newtab"
          webSocketDebuggerUrl = "ws://127.0.0.1/devtools/page/newtab"
        },
        [pscustomobject]@{
          type = "page"
          url = "https://hardwaretester.com/gamepad?test=1"
          webSocketDebuggerUrl = "ws://127.0.0.1/devtools/page/test"
        }
      )
    }

    $result = Wait-ForDevToolsPageTarget `
      -Port 9222 `
      -ExpectedUrl "https://hardwaretester.com/gamepad" `
      -TimeoutSeconds 1

    $result.url | Should -Be "https://hardwaretester.com/gamepad?test=1"
    $result.webSocketDebuggerUrl | Should -Be "ws://127.0.0.1/devtools/page/test"
  }

  It "falls back to the first non-browser-internal page" {
    Mock Wait-ForDevToolsJson {
      @(
        [pscustomobject]@{
          type = "page"
          url = "chrome://newtab"
          webSocketDebuggerUrl = "ws://127.0.0.1/devtools/page/newtab"
        },
        [pscustomobject]@{
          type = "page"
          url = "https://example.com/"
          webSocketDebuggerUrl = "ws://127.0.0.1/devtools/page/fallback"
        }
      )
    }

    $result = Wait-ForDevToolsPageTarget `
      -Port 9222 `
      -ExpectedUrl "https://hardwaretester.com/gamepad" `
      -TimeoutSeconds 1

    $result.url | Should -Be "https://example.com/"
  }

  It "times out when no page target becomes available" {
    Mock Wait-ForDevToolsJson { @() }
    Mock Start-Sleep {}

    {
      Wait-ForDevToolsPageTarget `
        -Port 9222 `
        -ExpectedUrl "https://hardwaretester.com/gamepad" `
        -TimeoutSeconds 0
    } | Should -Throw "Timed out waiting for a browser page target."
  }
}

Describe "Get-GamepadApiProbeExpression" {
  It "embeds the expected pattern, timeout, and strict matching mode" {
    $expression = Get-GamepadApiProbeExpression `
      -ExpectedIdPattern 'xbox "series"' `
      -AllowAnyGamepad $false `
      -TimeoutSeconds 17

    $expression | Should -Match 'new RegExp\("xbox \\"series\\"", "i"\)'
    $expression | Should -Match "const allowAnyGamepad = false;"
    $expression | Should -Match "Date.now\(\) \+ \(17 \* 1000\)"
  }

  It "enables any-gamepad matching when requested" {
    Get-GamepadApiProbeExpression `
      -ExpectedIdPattern "ignored" `
      -AllowAnyGamepad $true `
      -TimeoutSeconds 1 |
      Should -Match "const allowAnyGamepad = true;"
  }
}
