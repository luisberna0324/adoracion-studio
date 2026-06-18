param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [ValidateSet("x64", "x86")]
    [string]$Architecture = "x64",
    [string]$StandaloneDir = "",
    [string]$OutputDir = "dist",
    [string]$IssFile = "packaging/windows/MTControls.iss",
    [string]$FfmpegExe = $env:MTCONTROLS_FFMPEG_BIN_WINDOWS,
    [string]$GuidesZip = $env:MTCONTROLS_GUIDES_ZIP_WINDOWS,
    [string]$ClicksZip = $env:MTCONTROLS_CLICKS_ZIP_WINDOWS,
    [switch]$RequireFfmpeg,
    [string]$VcRedistExe = $env:MTCONTROLS_VC_REDIST_EXE,
    [switch]$SkipVcRedist
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

if ([string]::IsNullOrWhiteSpace($GuidesZip)) {
    $defaultGuidesZip = Join-Path $repoRoot "assets/guides/guides-en-es.zip"
    if (Test-Path -LiteralPath $defaultGuidesZip) {
        $GuidesZip = $defaultGuidesZip
    }
}

if (-not [string]::IsNullOrWhiteSpace($GuidesZip)) {
    if (!(Test-Path -LiteralPath $GuidesZip)) {
        throw "Configured guides zip not found: $GuidesZip"
    }
    $guidesDestDir = Join-Path $standaloneFull "installer"
    New-Item -ItemType Directory -Force -Path $guidesDestDir | Out-Null
    Copy-Item -LiteralPath $GuidesZip `
        -Destination (Join-Path $guidesDestDir "guides-en-es.zip") -Force
    Write-Host "Bundled dynamic guides zip -> $guidesDestDir\\guides-en-es.zip"
} else {
    Write-Warning "guides-en-es.zip not found. Dynamic guide assets won't be bundled in this installer."
}

if ([string]::IsNullOrWhiteSpace($ClicksZip)) {
    $defaultClicksZip = Join-Path $repoRoot "assets/clicks/clicks-pack.zip"
    if (Test-Path -LiteralPath $defaultClicksZip) {
        $ClicksZip = $defaultClicksZip
    }
}

if (-not [string]::IsNullOrWhiteSpace($ClicksZip)) {
    if (!(Test-Path -LiteralPath $ClicksZip)) {
        throw "Configured clicks zip not found: $ClicksZip"
    }
    $clicksDestDir = Join-Path $standaloneFull "installer"
    New-Item -ItemType Directory -Force -Path $clicksDestDir | Out-Null
    Copy-Item -LiteralPath $ClicksZip `
        -Destination (Join-Path $clicksDestDir "clicks-pack.zip") -Force
    Write-Host "Bundled click samples zip -> $clicksDestDir\\clicks-pack.zip"
} else {
    Write-Warning "clicks-pack.zip not found. Metronome click assets won't be bundled in this installer."
}

$vcRedistForInstaller = ""
if (-not $SkipVcRedist) {
    if (-not [string]::IsNullOrWhiteSpace($VcRedistExe)) {
        if (!(Test-Path -LiteralPath $VcRedistExe)) {
            throw "Configured VC redist not found: $VcRedistExe"
        }
        $vcRedistForInstaller = (Resolve-Path -LiteralPath $VcRedistExe).Path
    } else {
        $vcArch = if ($Architecture -eq "x86") { "x86" } else { "x64" }
        $vcRedistUrl = "https://aka.ms/vs/17/release/vc_redist.$vcArch.exe"
        $depsDir = Join-Path $env:TEMP "mtcontrols-installer-deps"
        New-Item -ItemType Directory -Force -Path $depsDir | Out-Null
        $downloadedVcRedist = Join-Path $depsDir "vc_redist.$vcArch.exe"
        Write-Host "Downloading VC++ redistributable ($vcArch) from $vcRedistUrl"
        Invoke-WebRequest -Uri $vcRedistUrl -OutFile $downloadedVcRedist -UseBasicParsing -TimeoutSec 180
        if (!(Test-Path -LiteralPath $downloadedVcRedist)) {
            throw "Couldn't download VC++ redistributable from $vcRedistUrl"
        }
        $vcRedistForInstaller = $downloadedVcRedist
    }
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

if (-not [string]::IsNullOrWhiteSpace($vcRedistForInstaller)) {
  $args += "/DVcRedistFile=$vcRedistForInstaller"
  Write-Host "Bundled VC++ redistributable -> $vcRedistForInstaller"
} else {
  Write-Warning "VC++ redistributable was skipped. Installer won't include automatic runtime fix for MSVCP140.dll."
}

$args += $issFull

& $iscc @args

$installerPath = Join-Path $outputFull "$outputBaseFilename.exe"
if (!(Test-Path -LiteralPath $installerPath)) {
    throw "Installer was not generated: $installerPath"
}

Write-Host "Created $installerPath"
