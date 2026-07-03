function Get-LibVirtualHidRootDeviceInstanceId {
  param([string] $TargetHardwareId)

  try {
    $devices = & pnputil.exe /enum-devices /deviceid $TargetHardwareId /deviceids
    if ($LASTEXITCODE -eq 0) {
      $instanceIds = @($devices |
        Where-Object { $_ -match "^\s*Instance ID\s*:\s*(.+)$" } |
        ForEach-Object { $Matches[1].Trim() })
      if ($instanceIds.Count -gt 0) {
        return $instanceIds
      }
    }
  } catch {
    Write-Verbose "Unable to enumerate PnP devices with pnputil: $($_.Exception.Message)"
  }

  try {
    $prefix = "$TargetHardwareId\"
    @(Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
      Where-Object { $_.PNPDeviceID -like "$prefix*" -or $_.HardwareID -contains $TargetHardwareId } |
      ForEach-Object { $_.PNPDeviceID })
  } catch {
    Write-Verbose "Unable to enumerate PnP devices: $($_.Exception.Message)"
    @()
  }
}

function Get-LibVirtualHidRegistryRootDevice {
  param([string] $TargetHardwareId)

  $rootKey = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT"
  Get-ChildItem -LiteralPath $rootKey -ErrorAction SilentlyContinue | ForEach-Object {
    $rootDeviceId = $_.PSChildName
    Get-ChildItem -LiteralPath $_.PSPath -ErrorAction SilentlyContinue | ForEach-Object {
      $instanceId = "ROOT\$rootDeviceId\$($_.PSChildName)"
      $hardwareIds = @()
      try {
        $hardwareIds = @((Get-ItemProperty -LiteralPath $_.PSPath -Name HardwareID -ErrorAction Stop).HardwareID)
      } catch {
        $hardwareIds = @()
      }

      $hasExactHardwareId = $hardwareIds -contains $TargetHardwareId
      $hasCorruptHardwareId = (
        $hardwareIds.Count -gt 1 -and
        -not $hasExactHardwareId -and
        (($hardwareIds -join "") -ieq $TargetHardwareId)
      )
      $hasTargetInstanceId = $instanceId -like "$TargetHardwareId\*"

      if ($hasExactHardwareId -or $hasCorruptHardwareId -or $hasTargetInstanceId) {
        $classGuid = $null
        try {
          $deviceProperties = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction Stop
          $classGuid = $deviceProperties.ClassGUID
        } catch {
          Write-Verbose "Unable to read registry properties for $instanceId`: $($_.Exception.Message)"
        }

        [pscustomobject]@{
          InstanceId = $instanceId
          HasExactHardwareId = $hasExactHardwareId
          HasCorruptHardwareId = $hasCorruptHardwareId
          HasLegacyHidClass = $classGuid -ieq "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}"
        }
      }
    }
  }
}
