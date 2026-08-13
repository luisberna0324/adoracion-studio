# add-defender-exclusion.ps1
# P0.1 (perf): agrega exclusiones puntuales de Microsoft Defender para
# PlayWorship / MTControls. El primer read de cada stem de la biblioteca
# dispara un escaneo real-time que cuesta 100-500ms por archivo en
# maquinas con antivirus empresarial (CrowdStrike, SentinelOne, etc.).
# En cambio, excluir el proceso de la app cubre automaticamente todo su
# I/O sin pedirle al usuario que desactive Defender.
#
# Idempotente: si la exclusion ya esta, no la duplica. Falla silenciosa
# si Defender no esta disponible (ej. Windows antiguo, AV de terceros
# que no expone MpPreference).

$ErrorActionPreference = 'SilentlyContinue'

$processNames = @('MTControls.exe', 'MTControls Standalone.exe')
$pathExclusions = @(
    Join-Path $env:LOCALAPPDATA 'MTControls'
)

function Test-DefenderAvailable {
    try {
        $null = Get-Command -Name 'Add-MpPreference' -ErrorAction Stop
        $null = Get-MpComputerStatus -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function Add-ExclusionIfMissing {
    param(
        [string]$Type,         # 'Process' o 'Path'
        [string]$Value
    )
    $prefs = Get-MpPreference
    $existing = switch ($Type) {
        'Process' { $prefs.ExclusionProcess }
        'Path'    { $prefs.ExclusionPath }
    }
    if ($existing -and ($existing -contains $Value)) {
        return $false
    }
    $args = @('-Exclusion' + $Type, $Value)
    & Add-MpPreference @args | Out-Null
    return $true
}

if (-not (Test-DefenderAvailable)) {
    # AV de terceros o Windows <10. Nada que hacer.
    exit 0
}

$added = 0
foreach ($name in $processNames) {
    if (Add-ExclusionIfMissing -Type 'Process' -Value $name) {
        $added++
    }
}
foreach ($path in $pathExclusions) {
    if (Test-Path -LiteralPath $path) {
        if (Add-ExclusionIfMissing -Type 'Path' -Value $path) {
            $added++
        }
    }
}

if ($added -gt 0) {
    $logDir = Join-Path $env:LOCALAPPDATA 'MTControls\Logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $logFile = Join-Path $logDir 'defender-exclusion.log'
    $timestamp = Get-Date -Format 'o'
    Add-Content -Path $logFile -Value "$timestamp Added $added Defender exclusion(s)."
}

exit 0
