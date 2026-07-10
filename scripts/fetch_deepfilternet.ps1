# Downloads DeepFilterNet ONNX models into models/deepfilternet/
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "models\deepfilternet"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "DeepFilterNet model setup"
Write-Host "Place enc.onnx, erb_dec.onnx, and df_dec.onnx into:"
Write-Host "  $dest"
Write-Host ""
Write-Host "If models already exist in the repo, nothing else is required."

$required = @("enc.onnx", "erb_dec.onnx", "df_dec.onnx")
foreach ($f in $required) {
    $p = Join-Path $dest $f
    if (Test-Path $p) { Write-Host "[OK] $f" } else { Write-Host "[MISSING] $f" }
}