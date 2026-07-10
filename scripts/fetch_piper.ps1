# Downloads Piper German voice models into models/piper/
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "models\piper"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$voices = @(
    @{ Name = "de_DE-thorsten-high"; Url = "https://huggingface.co/rhasspy/piper-voices/resolve/main/de/de_DE/thorsten/high/de_DE-thorsten-high.onnx" },
    @{ Name = "de_DE-kerstin-low"; Url = "https://huggingface.co/rhasspy/piper-voices/resolve/main/de/de_DE/kerstin/low/de_DE-kerstin-low.onnx" }
)

foreach ($v in $voices) {
    $onnx = Join-Path $dest ($v.Name + ".onnx")
    $json = $onnx + ".json"
    if (-not (Test-Path $onnx)) {
        Write-Host "Downloading $($v.Name).onnx ..."
        Invoke-WebRequest -Uri $v.Url -OutFile $onnx -UseBasicParsing
    }
    if (-not (Test-Path $json)) {
        Write-Host "Downloading $($v.Name).onnx.json ..."
        Invoke-WebRequest -Uri ($v.Url + ".json") -OutFile $json -UseBasicParsing
    }
}
Write-Host "Piper models ready in $dest"