# Builds a self-contained portable folder for EdgeTTS-Studio Native.
# Usage: .\scripts\package_portable.ps1 [-Config Release]
param(
    [string]$Config = "Release",
    [string]$QtPath = "C:\Qt\6.8.1\msvc2022_64"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
$buildDir = Join-Path $root "build"
$exeDir = Join-Path $buildDir $Config
$outDir = Join-Path $root "dist\EdgeTTS-Studio-Portable"

Write-Host "Building $Config..."
Push-Location $root
cmake --build $buildDir --config $Config --target EdgeTTSStudioNative
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Pop-Location

Write-Host "Packaging to $outDir ..."
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Copy-Item "$exeDir\EdgeTTSStudioNative.exe" $outDir
Copy-Item "$exeDir\*.dll" $outDir -ErrorAction SilentlyContinue
if (Test-Path "$exeDir\models") {
    Copy-Item -Recurse "$exeDir\models" (Join-Path $outDir "models")
}
if (Test-Path "$exeDir\espeak-ng-data") {
    Copy-Item -Recurse "$exeDir\espeak-ng-data" (Join-Path $outDir "espeak-ng-data")
}

$windeployqt = Join-Path $QtPath "bin\windeployqt.exe"
if (Test-Path $windeployqt) {
    & $windeployqt --no-translations --release (Join-Path $outDir "EdgeTTSStudioNative.exe")
} else {
    Write-Warning "windeployqt not found at $windeployqt — copy Qt DLLs manually."
}

# Optional ffmpeg for MP3/FLAC export
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($ffmpeg) {
    New-Item -ItemType Directory -Force -Path (Join-Path $outDir "tools") | Out-Null
    Copy-Item $ffmpeg.Source (Join-Path $outDir "tools\ffmpeg.exe")
    Write-Host "Bundled ffmpeg for MP3/FLAC export."
}

@"
EdgeTTS-Studio Native — Portable Build
======================================
Run EdgeTTSStudioNative.exe

Menu bar: File | Read me
- Read me -> Text Tags & Examples (pause/emphasis/Fish tags)
- Tools -> Model Manager, Batch Queue, Dark Mode

MP3/FLAC export requires tools\ffmpeg.exe (bundled if found on PATH during packaging).
"@ | Set-Content (Join-Path $outDir "README.txt")

Write-Host "Done: $outDir"
Write-Host "Zip it with: Compress-Archive -Path '$outDir\*' -DestinationPath '$root\dist\EdgeTTS-Studio-Portable.zip' -Force"