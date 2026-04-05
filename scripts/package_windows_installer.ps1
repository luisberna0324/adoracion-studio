param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$StandaloneDir = "build/windows-release/MTControls_artefacts/Release/Standalone",
    [string]$OutputDir = "dist",
    [string]$IssFile = "packaging/windows/MTControls.iss"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $IssFile)) {
    throw "Inno Setup script not found: $IssFile"
}

if (!(Test-Path -LiteralPath $StandaloneDir)) {
    throw "Standalone directory not found: $StandaloneDir"
}

$standaloneFull = (Resolve-Path -LiteralPath $StandaloneDir).Path
$outputFull = [System.IO.Path]::GetFullPath($OutputDir)
$issFull = (Resolve-Path -LiteralPath $IssFile).Path

New-Item -ItemType Directory -Force -Path $outputFull | Out-Null

$exe = Get-ChildItem -Path $standaloneFull -Filter *.exe -File | Select-Object -First 1
if ($null -eq $exe) {
    throw "No .exe found in standalone directory: $standaloneFull"
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

$outputBaseFilename = "playworship-windows-installer-$Version"
$exeName = $exe.Name

& $iscc `
  "/DAppVersion=$Version" `
  "/DSourceDir=$standaloneFull" `
  "/DOutputDir=$outputFull" `
  "/DOutputBaseFilename=$outputBaseFilename" `
  "/DAppExeName=$exeName" `
  $issFull

$installerPath = Join-Path $outputFull "$outputBaseFilename.exe"
if (!(Test-Path -LiteralPath $installerPath)) {
    throw "Installer was not generated: $installerPath"
}

Write-Host "Created $installerPath"
