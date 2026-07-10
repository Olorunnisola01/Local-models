# Copies the Supertonic models from the repo root into native/models so the
# CMake build can vendor them next to the exe. Re-run after updating models.
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$src = Join-Path $root "models\supertonic"
$dst = Join-Path $PSScriptRoot "..\models\supertonic"

if (-not (Test-Path $src)) {
    throw "Source models not found at $src"
}

New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
Copy-Item -Recurse -Force $src $dst
Write-Host "Synced models from $src to $dst"
