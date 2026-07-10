# Downloads Kokoro-82M ONNX model assets into models/kokoro/
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "models\kokoro"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "Kokoro model download"
Write-Host "Place kokoro-v1.0.onnx, vocab.json, and voices/*.bin into:"
Write-Host "  $dest"
Write-Host ""
Write-Host "Official source: https://huggingface.co/hexgrad/Kokoro-82M"
Write-Host "ONNX export: https://huggingface.co/onnx-community/Kokoro-82M-ONNX"
Write-Host ""
Write-Host "If you already have models in the repo, copy them with:"
Write-Host "  Copy-Item -Recurse -Force (Join-Path $root 'models\kokoro\*') $dest"