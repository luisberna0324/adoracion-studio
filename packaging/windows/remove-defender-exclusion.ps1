# remove-defender-exclusion.ps1
# P0.1 (perf): limpia las exclusiones de Defender que agrego
# add-defender-exclusion.ps1 al instalar. Se ejecuta desde
# [UninstallRun] del .iss. Idempotente: si la exclusion ya no esta,
# no falla.

$ErrorActionPreference = 'SilentlyContinue'

$processNames = @('MTControls.exe', 'MTControls Standalone.exe')
$pathExclusions = @(
    Join-Path $env:LOCALAPPDATA 'MTControls'
)

function Test-DefenderAvailable {
    try {
        $null = Get-Command -Name 'Remove-MpPreference' -ErrorAction Stop
        $null = Get-MpComputerStatus -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function Remove-ExclusionIfPresent {
    param(
        [string]$Type,
        [string]$Value
    )
    $prefs = Get-MpPreference
    $existing = switch ($Type) {
        'Process' { $prefs.ExclusionProcess }
        'Path'    { $prefs.ExclusionPath }
    }
    if (-not $existing -or -not ($existing -contains $Value)) {
        return $false
    }
    $args = @('-Exclusion' + $Type, $Value)
    & Remove-MpPreference @args | Out-Null
    return $true
}

if (-not (Test-DefenderAvailable)) {
    exit 0
}

$removed = 0
foreach ($name in $processNames) {
    if (Remove-ExclusionIfPresent -Type 'Process' -Value $name) {
        $removed++
    }
}
foreach ($path in $pathExclusions) {
    if (Remove-ExclusionIfPresent -Type 'Path' -Value $path) {
        $removed++
    }
}

if ($removed -gt 0) {
    $logDir = Join-Path $env:LOCALAPPDATA 'MTControls\Logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $logFile = Join-Path $logDir 'defender-exclusion.log'
    $timestamp = Get-Date -Format 'o'
    Add-Content -Path $logFile -Value "$timestamp Removed $removed Defender exclusion(s)."
}

exit 0
