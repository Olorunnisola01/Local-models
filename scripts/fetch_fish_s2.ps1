# Downloads fishaudio/s2-pro weights for Kaggle Dataset upload (~9 GB).
# After this finishes, zip/upload the output folder as a Kaggle Dataset and
# attach it to kaggle_fish_s2_server.ipynb via Add Data.
$ErrorActionPreference = "Stop"

$RepoId = "fishaudio/s2-pro"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "models\fish-s2-pro"

$required = @(
    "codec.pth",
    "model-00001-of-00002.safetensors",
    "model-00002-of-00002.safetensors",
    "model.safetensors.index.json",
    "config.json",
    "tokenizer.json",
    "tokenizer_config.json"
)

$optional = @(
    "chat_template.jinja",
    "special_tokens_map.json"
)

function Test-CheckpointComplete([string]$Path) {
    foreach ($name in $required) {
        if (-not (Test-Path (Join-Path $Path $name))) {
            return $false
        }
    }
    return $true
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "Fish Audio S2 Pro model download"
Write-Host "Source: https://huggingface.co/$RepoId"
Write-Host "Destination: $dest"
Write-Host ""

if (Test-CheckpointComplete $dest) {
    Write-Host "All required files already present - skipping download."
} else {
    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue

    if ($pyLauncher) {
        Write-Host "Downloading via huggingface_hub (py -3.10) ... (first run may take 10-30+ minutes)"
        $pyCode = @"
from huggingface_hub import snapshot_download
snapshot_download(
    repo_id='$RepoId',
    local_dir=r'$dest',
    local_dir_use_symlinks=False,
    resume_download=True,
)
print('snapshot_download complete')
"@
        & py -3.10 -c $pyCode
        if ($LASTEXITCODE -ne 0) {
            throw "huggingface_hub snapshot_download failed with exit code $LASTEXITCODE"
        }
    } else {
        $hfCli = Get-Command hf -ErrorAction SilentlyContinue
        if (-not $hfCli) {
            $hfCli = Get-Command huggingface-cli -ErrorAction SilentlyContinue
        }
        if (-not $hfCli) {
            throw "python or hf not found. Install with: pip install -U huggingface_hub"
        }

        Write-Host "Downloading via $($hfCli.Name) ... (first run may take 10-30+ minutes)"
        & $hfCli.Source download $RepoId --local-dir $dest
        if ($LASTEXITCODE -ne 0) {
            throw "hf download failed with exit code $LASTEXITCODE"
        }
    }
}

Write-Host ""
Write-Host "File check:"
foreach ($name in $required) {
    $path = Join-Path $dest $name
    if (Test-Path $path) {
        $sizeMb = [math]::Round((Get-Item $path).Length / 1MB, 1)
        Write-Host ("  [OK] {0} ({1} MB)" -f $name, $sizeMb)
    } else {
        Write-Host ("  [MISSING] {0}" -f $name)
    }
}

foreach ($name in $optional) {
    $path = Join-Path $dest $name
    if (Test-Path $path) {
        Write-Host ("  [OK] {0} (optional)" -f $name)
    }
}

if (-not (Test-CheckpointComplete $dest)) {
    throw "Download incomplete. Re-run this script or download manually from https://huggingface.co/$RepoId"
}

Write-Host ""
Write-Host "Fish S2 Pro weights ready in:"
Write-Host "  $dest"
Write-Host ""
Write-Host "Next: upload this folder as a Kaggle Dataset, then Add Data in kaggle_fish_s2_server.ipynb."