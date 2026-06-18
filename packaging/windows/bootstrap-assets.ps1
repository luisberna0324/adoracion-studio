$ErrorActionPreference = "Stop"

$assetsBaseUrl = "https://storage.googleapis.com/playworship-assets"
$encoderVersion = "2026-04-18-1"
$padsVersion = "roman-v1"
$clicksVersion = "clicks-v1"

$encoderUrl = "$assetsBaseUrl/encoder/windows/$encoderVersion/ffmpeg.exe"
$padsZipUrl = "$assetsBaseUrl/pads/$padsVersion/pads-pack.zip"
$clicksZipUrl = "$assetsBaseUrl/clicks/$clicksVersion/clicks-pack.zip"

$assetsRoot = Join-Path $env:LOCALAPPDATA "MTControls\Assets"
$encoderDir = Join-Path $assetsRoot "encoder"
$padsDir = Join-Path $assetsRoot "pads\RomanVizcaino"
$clicksDir = Join-Path $assetsRoot "clicks"
$tempDir = Join-Path $env:TEMP "mtcontrols-assets-bootstrap"

function Download-File([string]$url, [string]$dest) {
    $destDir = Split-Path -Parent $dest
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -TimeoutSec 120
}

try {
    Write-Host "Preparando descarga de recursos iniciales... / Preparing initial resource download..."
    New-Item -ItemType Directory -Force -Path $encoderDir | Out-Null
    New-Item -ItemType Directory -Force -Path $padsDir | Out-Null
    New-Item -ItemType Directory -Force -Path $clicksDir | Out-Null
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    $encoderPath = Join-Path $encoderDir "ffmpeg.exe"
    if (!(Test-Path -LiteralPath $encoderPath)) {
        Write-Host "Descargando encoder... / Downloading encoder..."
        $encoderTmp = Join-Path $tempDir "ffmpeg.exe.download"
        Download-File $encoderUrl $encoderTmp
        Move-Item -Force -Path $encoderTmp -Destination $encoderPath
    }

    $existingPads = @(Get-ChildItem -Path $padsDir -Recurse -File -Include *.mp3,*.wav,*.aif,*.aiff -ErrorAction SilentlyContinue)
    if ($existingPads.Count -lt 12) {
        Write-Host "Descargando pack de pads... / Downloading pads pack..."
        $padsZip = Join-Path $tempDir "pads-pack.zip"
        Download-File $padsZipUrl $padsZip

        if (Test-Path -LiteralPath $padsDir) {
            Remove-Item -Recurse -Force -Path $padsDir
        }
        New-Item -ItemType Directory -Force -Path $padsDir | Out-Null
        Expand-Archive -Path $padsZip -DestinationPath $padsDir -Force

        $postPads = @(Get-ChildItem -Path $padsDir -Recurse -File -Include *.mp3,*.wav,*.aif,*.aiff -ErrorAction SilentlyContinue)
        if ($postPads.Count -lt 12) {
            throw "Pads package invalid: expected at least 12 audio files."
        }
    }

    $existingClicks = @(Get-ChildItem -Path $clicksDir -Recurse -File -Include *.mp3,*.wav,*.aif,*.aiff -ErrorAction SilentlyContinue)
    if ($existingClicks.Count -lt 12) {
        Write-Host "Descargando pack de clicks... / Downloading click pack..."
        $clicksZip = Join-Path $tempDir "clicks-pack.zip"
        Download-File $clicksZipUrl $clicksZip

        if (Test-Path -LiteralPath $clicksDir) {
            Remove-Item -Recurse -Force -Path $clicksDir
        }
        New-Item -ItemType Directory -Force -Path $clicksDir | Out-Null
        Expand-Archive -Path $clicksZip -DestinationPath $clicksDir -Force

        $postClicks = @(Get-ChildItem -Path $clicksDir -Recurse -File -Include *.mp3,*.wav,*.aif,*.aiff -ErrorAction SilentlyContinue)
        if ($postClicks.Count -lt 12) {
            throw "Clicks package invalid: expected at least 12 audio files."
        }
    }
    Write-Host "Recursos descargados correctamente. / Resources downloaded successfully."
}
catch {
    $logDir = Join-Path $env:LOCALAPPDATA "MTControls"
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $logPath = Join-Path $logDir "installer-assets-error.log"
    "$(Get-Date -Format o) $($_.Exception.Message)" | Out-File -FilePath $logPath -Encoding UTF8 -Append
    exit 0
}

exit 0
