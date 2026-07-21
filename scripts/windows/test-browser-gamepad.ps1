<#
.SYNOPSIS
Validates an installed libvirtualhid gamepad through a real browser Gamepad API.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $GamepadAdapterPath,

  [ValidateSet("generic", "x360", "xone", "xseries", "ds4", "ds5", "switch")]
  [Alias("Profile")]
  [string] $GamepadProfile = "xseries",

  [string] $BrowserPath,

  [string] $Url = "https://hardwaretester.com/gamepad",

  [int] $TimeoutSeconds = 20,

  [int] $HoldSeconds = 35,

  [string] $ExpectedIdPattern,

  [switch] $AllowAnyGamepad,

  [switch] $KeepBrowserOpen
)

$ErrorActionPreference = "Stop"
$script:DevToolsCommandId = 0

function Get-ExpectedGamepadIdPattern {
  param([string] $ProfileName)

  switch ($ProfileName) {
    "generic" { return "(1209.*0001|vid[_ -]?1209.*pid[_ -]?0001|generic)" }
    "x360" { return "(045e.*028e|vid[_ -]?045e.*pid[_ -]?028e|x-?box.*360)" }
    "xone" { return "(045e.*02ea|vid[_ -]?045e.*pid[_ -]?02ea|xbox one|x-box one)" }
    "xseries" { return "(045e.*0b12|045e.*0b13|vid[_ -]?045e.*pid[_ -]?0b12|vid[_ -]?045e.*pid[_ -]?0b13|xbox wireless|xbox series)" }
    "ds4" { return "(054c.*05c4|vid[_ -]?054c.*pid[_ -]?05c4|dualshock|wireless controller)" }
    "ds5" { return "(054c.*0ce6|vid[_ -]?054c.*pid[_ -]?0ce6|dualsense|wireless controller)" }
    "switch" { return "(057e.*2009|vid[_ -]?057e.*pid[_ -]?2009|switch|pro controller)" }
  }

  throw "Unsupported profile: $ProfileName"
}

function Resolve-BrowserPath {
  param([string] $Path)

  if ($Path) {
    return (Resolve-Path -LiteralPath $Path).Path
  }

  $candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Microsoft\Edge\Application\msedge.exe"),
    (Join-Path $env:ProgramFiles "Microsoft\Edge\Application\msedge.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Google\Chrome\Application\chrome.exe"),
    (Join-Path $env:ProgramFiles "Google\Chrome\Application\chrome.exe")
  )

  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
      return $candidate
    }
  }

  throw "No supported browser was found. Pass -BrowserPath with msedge.exe or chrome.exe."
}

function Get-FreeTcpPort {
  $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
  try {
    $listener.Start()
    return ([System.Net.IPEndPoint] $listener.LocalEndpoint).Port
  } finally {
    $listener.Stop()
  }
}

function Wait-ForDevToolsJson {
  param(
    [int] $Port,
    [string] $Path,
    [int] $TimeoutSeconds
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  $uri = "http://127.0.0.1:${Port}${Path}"
  do {
    try {
      return Invoke-RestMethod -Uri $uri -TimeoutSec 2
    } catch {
      Start-Sleep -Milliseconds 250
    }
  } while ((Get-Date) -lt $deadline)

  throw "Timed out waiting for browser DevTools endpoint $uri."
}

function Wait-ForDevToolsPageTarget {
  param(
    [int] $Port,
    [string] $ExpectedUrl,
    [int] $TimeoutSeconds
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  do {
    try {
      $rawTargets = Wait-ForDevToolsJson -Port $Port -Path "/json" -TimeoutSeconds 2
      $targets = @($rawTargets)
      if ($targets.Count -eq 1 -and $targets[0] -is [System.Array]) {
        $targets = @($targets[0])
      }
    } catch {
      $targets = @()
    }
    $page = $targets |
      Where-Object {
        $_.type -eq "page" -and
          $_.webSocketDebuggerUrl -and
          $_.url -and
          $_.url.StartsWith($ExpectedUrl, [System.StringComparison]::OrdinalIgnoreCase)
      } |
      Select-Object -First 1
    if (-not $page) {
      $page = $targets |
        Where-Object {
          $_.type -eq "page" -and
            $_.webSocketDebuggerUrl -and
            $_.url -and
            -not $_.url.StartsWith("edge://", [System.StringComparison]::OrdinalIgnoreCase) -and
            -not $_.url.StartsWith("chrome://", [System.StringComparison]::OrdinalIgnoreCase)
        } |
        Select-Object -First 1
    }
    if ($page) {
      Write-Verbose "Using browser page target: $($page.url) ($($page.webSocketDebuggerUrl))"
      return [pscustomobject] @{
        url = $page.url
        webSocketDebuggerUrl = $page.webSocketDebuggerUrl
      }
    }

    Start-Sleep -Milliseconds 250
  } while ((Get-Date) -lt $deadline)

  throw "Timed out waiting for a browser page target."
}

function Invoke-DevToolsCommand {
  param(
    [string] $WebSocketDebuggerUrl,
    [string] $Method,
    [hashtable] $Params = @{},
    [int] $TimeoutSeconds = 30
  )

  $socket = [System.Net.WebSockets.ClientWebSocket]::new()
  $cancellation = [System.Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSeconds))
  try {
    $normalizedWebSocketDebuggerUrl = $WebSocketDebuggerUrl -replace "://localhost(:|/)", '://127.0.0.1$1'
    $socket.ConnectAsync([Uri] $normalizedWebSocketDebuggerUrl, $cancellation.Token).GetAwaiter().GetResult()

    $id = [System.Threading.Interlocked]::Increment([ref] $script:DevToolsCommandId)
    $message = @{
      id = $id
      method = $Method
      params = $Params
    } | ConvertTo-Json -Depth 20 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($message)
    $socket.SendAsync(
      [ArraySegment[byte]]::new($bytes),
      [System.Net.WebSockets.WebSocketMessageType]::Text,
      $true,
      $cancellation.Token
    ).GetAwaiter().GetResult()

    do {
      $buffer = New-Object byte[] 65536
      $builder = [System.Text.StringBuilder]::new()
      do {
        $segment = [ArraySegment[byte]]::new($buffer)
        $result = $socket.ReceiveAsync($segment, $cancellation.Token).GetAwaiter().GetResult()
        if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
          throw "Browser DevTools websocket closed before $Method returned."
        }
        [void] $builder.Append([System.Text.Encoding]::UTF8.GetString($buffer, 0, $result.Count))
      } while (-not $result.EndOfMessage)

      $response = $builder.ToString() | ConvertFrom-Json
      if ($response.id -eq $id) {
        if ($response.error) {
          throw "DevTools $Method failed: $($response.error.message)"
        }
        return $response.result
      }
    } while ($true)
  } finally {
    if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
      $socket.CloseAsync(
        [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
        "done",
        [System.Threading.CancellationToken]::None
      ).GetAwaiter().GetResult()
    }
    $socket.Dispose()
    $cancellation.Dispose()
  }
}

function Get-GamepadApiProbeExpression {
  param(
    [string] $ExpectedIdPattern,
    [bool] $AllowAnyGamepad,
    [int] $TimeoutSeconds
  )

  $patternJson = ConvertTo-Json $ExpectedIdPattern -Compress
  $allowAnyJson = if ($AllowAnyGamepad) { "true" } else { "false" }
  return @"
(async () => {
  const expectedPattern = new RegExp($patternJson, "i");
  const allowAnyGamepad = $allowAnyJson;
  const deadline = Date.now() + ($TimeoutSeconds * 1000);
  const summary = {
    ok: false,
    gamepadApi: typeof navigator.getGamepads === "function",
    secureContext: window.isSecureContext,
    userAgent: navigator.userAgent,
    ids: [],
    samples: [],
    matched: null
  };

  if (!summary.gamepadApi) {
    return summary;
  }

  function round(value) {
    return Math.round(value * 1000) / 1000;
  }

  function snapshot() {
    return Array.from(navigator.getGamepads())
      .filter((pad) => pad)
      .map((pad) => ({
        id: pad.id,
        index: pad.index,
        mapping: pad.mapping,
        connected: pad.connected,
        buttonCount: pad.buttons.length,
        axisCount: pad.axes.length,
        buttons: pad.buttons.map((button) => ({
          pressed: button.pressed,
          value: round(button.value)
        })),
        axes: pad.axes.map(round)
      }));
  }

  const seenById = new Map();
  function stateFor(pad) {
    if (!seenById.has(pad.id)) {
      seenById.set(pad.id, {
        id: pad.id,
        mapping: pad.mapping,
        buttonCount: pad.buttonCount,
        axisCount: pad.axisCount,
        buttonPressed: false,
        buttonChanged: false,
        axisMoved: false,
        buttons: [],
        axes: []
      });
    }
    return seenById.get(pad.id);
  }

  function updateExtents(extents, index, value) {
    if (!extents[index]) {
      extents[index] = { min: value, max: value };
    } else {
      extents[index].min = Math.min(extents[index].min, value);
      extents[index].max = Math.max(extents[index].max, value);
    }
    return extents[index].max - extents[index].min;
  }

  while (Date.now() < deadline) {
    const pads = snapshot();
    if (summary.samples.length < 5 || Date.now() + 1000 >= deadline) {
      summary.samples.push(pads.map((pad) => ({
        id: pad.id,
        mapping: pad.mapping,
        buttonCount: pad.buttonCount,
        axisCount: pad.axisCount,
        pressedButtons: pad.buttons
          .map((button, index) => button.pressed || button.value > 0.5 ? index : null)
          .filter((index) => index !== null),
        axes: pad.axes
      })));
    }

    for (const pad of pads) {
      if (!summary.ids.includes(pad.id)) {
        summary.ids.push(pad.id);
      }
      if (!allowAnyGamepad && !expectedPattern.test(pad.id)) {
        continue;
      }

      const seen = stateFor(pad);
      seen.mapping = pad.mapping;
      for (let index = 0; index < pad.buttons.length; index += 1) {
        const button = pad.buttons[index];
        if (button.pressed || button.value > 0.5) {
          seen.buttonPressed = true;
        }
        if (updateExtents(seen.buttons, index, button.value) > 0.5) {
          seen.buttonChanged = true;
        }
      }

      for (let index = 0; index < pad.axes.length; index += 1) {
        if (updateExtents(seen.axes, index, pad.axes[index]) > 0.5) {
          seen.axisMoved = true;
        }
      }

      if (seen.buttonPressed && seen.buttonChanged && seen.axisMoved) {
        summary.ok = true;
        summary.matched = seen;
        return summary;
      }
    }

    await new Promise((resolve) => setTimeout(resolve, 200));
  }

  for (const seen of seenById.values()) {
    summary.matched = seen;
    break;
  }
  return summary;
})()
"@
}

if ($HoldSeconds -le $TimeoutSeconds) {
  throw "-HoldSeconds must be greater than -TimeoutSeconds so the adapter remains alive for browser polling."
}

$resolvedGamepadAdapterPath = (Resolve-Path -LiteralPath $GamepadAdapterPath).Path
$resolvedBrowserPath = Resolve-BrowserPath -Path $BrowserPath
$expectedPattern = if ($ExpectedIdPattern) {
  $ExpectedIdPattern
} else {
  Get-ExpectedGamepadIdPattern -ProfileName $GamepadProfile
}
if ($GamepadProfile -eq "x360") {
  throw "The Windows UMDF/VHF backend does not expose Xbox 360 XUSB gamepads. Use the consumer's XUSB fallback for x360."
}
$remoteDebuggingPort = Get-FreeTcpPort
$browserUserDataDir = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-browser-gamepad-$([Guid]::NewGuid())"
$adapterStdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-browser-gamepad-adapter.out"
$adapterStderrPath = Join-Path ([System.IO.Path]::GetTempPath()) "libvirtualhid-browser-gamepad-adapter.err"
Remove-Item -LiteralPath $adapterStdoutPath, $adapterStderrPath -Force -ErrorAction SilentlyContinue

$browserProcess = $null
$adapterProcess = $null
try {
  $browserArguments = @(
    "--user-data-dir=$browserUserDataDir",
    "--remote-debugging-port=$remoteDebuggingPort",
    "--remote-allow-origins=*",
    "--no-first-run",
    "--no-default-browser-check",
    "--disable-background-timer-throttling",
    "--disable-renderer-backgrounding",
    "--disable-backgrounding-occluded-windows",
    "--new-window",
    $Url
  )

  $browserProcess = Start-Process `
    -FilePath $resolvedBrowserPath `
    -ArgumentList $browserArguments `
    -PassThru

  [void] (Wait-ForDevToolsJson -Port $remoteDebuggingPort -Path "/json/version" -TimeoutSeconds $TimeoutSeconds)
  $page = Wait-ForDevToolsPageTarget -Port $remoteDebuggingPort -ExpectedUrl $Url -TimeoutSeconds $TimeoutSeconds
  [void] (Invoke-DevToolsCommand `
    -WebSocketDebuggerUrl $page.webSocketDebuggerUrl `
    -Method "Page.bringToFront" `
    -TimeoutSeconds 5)
  $adapterProcess = Start-Process `
    -FilePath $resolvedGamepadAdapterPath `
    -WorkingDirectory (Split-Path -Parent $resolvedGamepadAdapterPath) `
    -ArgumentList @($GamepadProfile, "--hold-seconds", "$HoldSeconds") `
    -PassThru `
    -RedirectStandardOutput $adapterStdoutPath `
    -RedirectStandardError $adapterStderrPath `
    -WindowStyle Hidden

  Start-Sleep -Seconds 2
  if ($adapterProcess.HasExited) {
    $stdout = Get-Content -LiteralPath $adapterStdoutPath -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content -LiteralPath $adapterStderrPath -Raw -ErrorAction SilentlyContinue
    throw "gamepad_adapter exited with code $($adapterProcess.ExitCode).`nstdout:`n$stdout`nstderr:`n$stderr"
  }

  [void] (Invoke-DevToolsCommand `
    -WebSocketDebuggerUrl $page.webSocketDebuggerUrl `
    -Method "Runtime.evaluate" `
    -Params @{
      expression = "window.focus(); document.body && document.body.focus && document.body.focus();"
    } `
    -TimeoutSeconds 5)
  [void] (Invoke-DevToolsCommand `
    -WebSocketDebuggerUrl $page.webSocketDebuggerUrl `
    -Method "Input.dispatchMouseEvent" `
    -Params @{
      type = "mousePressed"
      x = 32
      y = 32
      button = "left"
      clickCount = 1
    } `
    -TimeoutSeconds 5)
  [void] (Invoke-DevToolsCommand `
    -WebSocketDebuggerUrl $page.webSocketDebuggerUrl `
    -Method "Input.dispatchMouseEvent" `
    -Params @{
      type = "mouseReleased"
      x = 32
      y = 32
      button = "left"
      clickCount = 1
    } `
    -TimeoutSeconds 5)

  $expression = Get-GamepadApiProbeExpression `
    -ExpectedIdPattern $expectedPattern `
    -AllowAnyGamepad:$AllowAnyGamepad `
    -TimeoutSeconds $TimeoutSeconds
  $result = Invoke-DevToolsCommand `
    -WebSocketDebuggerUrl $page.webSocketDebuggerUrl `
    -Method "Runtime.evaluate" `
    -Params @{
      expression = $expression
      awaitPromise = $true
      returnByValue = $true
    } `
    -TimeoutSeconds ($TimeoutSeconds + 10)

  if ($result.exceptionDetails) {
    throw "Browser Gamepad API probe threw: $($result.exceptionDetails.text)"
  }

  $probe = $result.result.value
  if (-not $probe.ok) {
    $probeJson = $probe | ConvertTo-Json -Depth 20
    $expectedMessage = if ($AllowAnyGamepad) {
      "any gamepad"
    } else {
      "a gamepad matching /$expectedPattern/i"
    }
    throw "Browser Gamepad API did not observe changing input from $expectedMessage.`n$probeJson"
  }

  Write-Information `
    "Browser Gamepad API observed $GamepadProfile as '$($probe.matched.id)' with mapping '$($probe.matched.mapping)'." `
    -InformationAction Continue
} finally {
  if ($adapterProcess -and -not $adapterProcess.HasExited) {
    Stop-Process -Id $adapterProcess.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $adapterProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  }

  if ($browserProcess -and -not $KeepBrowserOpen -and -not $browserProcess.HasExited) {
    Stop-Process -Id $browserProcess.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $browserProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
  }

  if (-not $KeepBrowserOpen) {
    Remove-Item -LiteralPath $browserUserDataDir -Recurse -Force -ErrorAction SilentlyContinue
  }
}
