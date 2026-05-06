param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [ValidateSet("x64", "x86")]
    [string]$Architecture = "x64",
    [string]$StandaloneDir = "",
    [string]$OutputDir = "dist",
    [string]$IssFile = "packaging/windows/MTControls.iss",
    [string]$FfmpegExe = $env:MTCONTROLS_FFMPEG_BIN_WINDOWS,
    [switch]$RequireFfmpeg
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($StandaloneDir)) {
    $StandaloneDir = if ($Architecture -eq "x86") {
        "build/windows-x86-release/MTControls_artefacts/Release/Standalone"
    } else {
        "build/windows-release/MTControls_artefacts/Release/Standalone"
    }
}

if (!(Test-Path -LiteralPath $IssFile)) {
    throw "Inno Setup script not found: $IssFile"
}

if (!(Test-Path -LiteralPath $StandaloneDir)) {
    throw "Standalone directory not found: $StandaloneDir"
}

$standaloneFull = (Resolve-Path -LiteralPath $StandaloneDir).Path
$outputFull = [System.IO.Path]::GetFullPath($OutputDir)
$issFull = (Resolve-Path -LiteralPath $IssFile).Path
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

New-Item -ItemType Directory -Force -Path $outputFull | Out-Null

$exe = Get-ChildItem -Path $standaloneFull -Filter *.exe -File | Select-Object -First 1
if ($null -eq $exe) {
    throw "No .exe found in standalone directory: $standaloneFull"
}

$ffmpegDestDir = Join-Path $standaloneFull "ffmpeg"
$ffmpegDestExe = Join-Path $ffmpegDestDir "ffmpeg.exe"
$ffmpegAlreadyBundled = Test-Path -LiteralPath $ffmpegDestExe

if (-not [string]::IsNullOrWhiteSpace($FfmpegExe)) {
    if (!(Test-Path -LiteralPath $FfmpegExe)) {
        throw "Configured ffmpeg.exe not found: $FfmpegExe"
    }
    New-Item -ItemType Directory -Force -Path $ffmpegDestDir | Out-Null
    Copy-Item -LiteralPath $FfmpegExe -Destination $ffmpegDestExe -Force
    $ffmpegAlreadyBundled = $true
    Write-Host "Bundled ffmpeg.exe -> $ffmpegDestExe"
}

if ($RequireFfmpeg -and -not $ffmpegAlreadyBundled) {
    throw @"
ffmpeg.exe is required but was not found in Standalone output.

Expected bundled path:
  $ffmpegDestExe

You can provide a source binary using:
  -FfmpegExe "C:\path\to\ffmpeg.exe"
or env var:
  MTCONTROLS_FFMPEG_BIN_WINDOWS
"@
}

if (-not $ffmpegAlreadyBundled) {
    Write-Warning "ffmpeg.exe was not found/bundled. Installer will be generated without ffmpeg (runtime bootstrap will download it)."
}

$iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($iscc)) {
    $defaultIscc = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"
    if (Test-Path -LiteralPath $defaultIscc) {
        $iscc = $defaultIscc
    } else {
        throw "ISCC.exe not found. Install Inno Setup first."
    }
}

$outputBaseFilename = "playworship-windows-installer-$Version-$Architecture"
$exeName = $exe.Name
$installerIconFile = Join-Path $repoRoot "assets/icons/app-icon.ico"
$hasInstallerIcon = Test-Path -LiteralPath $installerIconFile

$args = @(
  "/DAppVersion=$Version",
  "/DInstallerArch=$Architecture",
  "/DSourceDir=$standaloneFull",
  "/DOutputDir=$outputFull",
  "/DOutputBaseFilename=$outputBaseFilename",
  "/DAppExeName=$exeName"
)

if ($hasInstallerIcon) {
  $args += "/DInstallerIconFile=$installerIconFile"
}

$args += $issFull

& $iscc @args

$installerPath = Join-Path $outputFull "$outputBaseFilename.exe"
if (!(Test-Path -LiteralPath $installerPath)) {
    throw "Installer was not generated: $installerPath"
}

Write-Host "Created $installerPath"
