<#
.SYNOPSIS
Checks the MSI's native applications-to-close integration and prepares a CI fixture.
.PARAMETER MsiPath
The built Windows driver installer to inspect.
.PARAMETER FixtureDirectory
An optional temporary directory in which to build and start a harmless sunshine.exe
process. The calling CI job installs the MSI and verifies that Restart Manager closes it.
.OUTPUTS
System.Int32. The fixture process identifier when FixtureDirectory is supplied.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string] $MsiPath,

  [string] $FixtureDirectory
)

$ErrorActionPreference = "Stop"
$resolvedMsi = (Resolve-Path -LiteralPath $MsiPath).Path

<#
.SYNOPSIS
Reads the first column of a query against a Windows Installer database.
.PARAMETER Database
The read-only Windows Installer database.
.PARAMETER Query
The query whose string results should be returned.
.OUTPUTS
System.String. The first column of each matching record.
#>
function Get-MsiQueryValue {
  param($Database, [string] $Query)

  $view = $Database.OpenView($Query)
  try {
    $null = $view.Execute()
    while ($record = $view.Fetch()) {
      $record.StringData(1)
    }
  } finally {
    $null = $view.Close()
  }
}

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($resolvedMsi, 0)
$resources = @(Get-MsiQueryValue -Database $database -Query 'SELECT `Resource` FROM `Wix4RestartResource`')
foreach ($resource in @("SunshineService", "sunshine.exe")) {
  if ($resource -notin $resources) {
    throw "The MSI does not register $resource with Restart Manager."
  }
}
$dialog = Get-MsiQueryValue -Database $database -Query "SELECT ``Dialog`` FROM ``Dialog`` WHERE ``Dialog`` = 'MsiRMFilesInUse'"
if (-not $dialog) {
  throw "The MSI does not contain the native Restart Manager applications-to-close dialog."
}
$action = Get-MsiQueryValue -Database $database -Query (
  "SELECT ``Action`` FROM ``CustomAction`` WHERE ``Target`` = 'WixRegisterRestartResources'"
)
if (-not $action) {
  throw "The MSI does not contain the Restart Manager registration action."
}
$registrationSequence = Get-MsiQueryValue -Database $database -Query (
  "SELECT ``Sequence`` FROM ``InstallExecuteSequence`` WHERE ``Action`` = '$action'"
)
$validationSequence = Get-MsiQueryValue -Database $database -Query (
  "SELECT ``Sequence`` FROM ``InstallExecuteSequence`` WHERE ``Action`` = 'InstallValidate'"
)
if (-not $registrationSequence -or [int] $registrationSequence -ge [int] $validationSequence) {
  throw "Restart resources must be registered before InstallValidate displays files in use."
}

Write-Information "The MSI registers Sunshine before its native applications-to-close dialog." -InformationAction Continue

if ($FixtureDirectory) {
  New-Item -ItemType Directory -Path $FixtureDirectory | Out-Null
  $fixtureExecutable = Join-Path $FixtureDirectory "sunshine.exe"
  Add-Type -OutputAssembly $fixtureExecutable -OutputType ConsoleApplication -TypeDefinition @'
public static class SunshineRestartManagerFixture {
    public static void Main() {
        System.Threading.Thread.Sleep(System.Threading.Timeout.Infinite);
    }
}
'@
  $fixtureProcess = Start-Process -FilePath $fixtureExecutable -PassThru -WindowStyle Hidden
  if ($fixtureProcess.WaitForExit(250)) {
    throw "The Sunshine fixture exited before the MSI could detect it."
  }
  $fixtureProcess.Id
}
