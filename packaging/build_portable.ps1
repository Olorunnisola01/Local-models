# Builds EdgeTTS-Studio-Portable.exe: one self-contained file that needs no
# installer, no Visual C++ redistributable, no Qt, and no model download.
#
#   .\packaging\build_portable.ps1
#
# Output: dist\EdgeTTS-Studio-Portable.exe

[CmdletBinding()]
param(
    [string]$Config   = 'Release',
    [string]$SevenZip = 'C:\Program Files\7-Zip',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pkg  = $PSScriptRoot
$dist = Join-Path $root 'dist'
$work = Join-Path $env:TEMP ("etts_portable_" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$stage = Join-Path $work 'EdgeTTSStudio'

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }

# --- 1. build the app -------------------------------------------------------
if (-not $SkipBuild) {
    Step "Building $Config"
    cmake --build (Join-Path $root 'build') --config $Config --target EdgeTTSStudioNative -- -m
    if ($LASTEXITCODE -ne 0) { throw "app build failed" }
}

$releaseDir = Join-Path $root "build\$Config"
$appExe = Join-Path $releaseDir 'EdgeTTSStudioNative.exe'
if (-not (Test-Path $appExe)) { throw "not found: $appExe" }

# --- 2. stage the payload ---------------------------------------------------
Step 'Staging payload'
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# /XD drops two trees that are dead weight at runtime:
#   models\supertonic\supertonic  - exact duplicate of models\supertonic
#   models\kokoro_de_victoria_src - PyTorch training checkpoint, never loaded
$roboArgs = @(
    $releaseDir, $stage, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/MT:8',
    '/XD', (Join-Path $releaseDir 'models\supertonic\supertonic'),
           (Join-Path $releaseDir 'models\kokoro_de_victoria_src'),
    '/XF', 'dump_onnx_io.exe', 'gui_log.txt', '*.pdb', '*.ilk', '*.exp', '*.lib'
)
& robocopy @roboArgs | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# The Visual C++ runtime must travel with the app, otherwise a clean PC needs
# the redistributable installed - which defeats the point of this package.
Step 'Adding Visual C++ runtime'
$crt = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -Directory -ErrorAction SilentlyContinue |
       Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw 'Microsoft.VC143.CRT redist folder not found' }
Copy-Item (Join-Path $crt.FullName '*.dll') $stage -Force

$payloadMB = [math]::Round((Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
Write-Host "    payload: $payloadMB MB"

# --- 3. compile the launcher ------------------------------------------------
Step 'Compiling launcher'
Copy-Item (Join-Path $SevenZip '7z.exe') $pkg -Force
Copy-Item (Join-Path $SevenZip '7z.dll') $pkg -Force

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath  = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'no MSVC toolchain found' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'

$launcherExe = Join-Path $work 'launcher.exe'
$cmd = @"
call "$vcvars" >nul
cd /d "$pkg"
rc /nologo /fo "$work\launcher.res" launcher.rc || exit /b 1
cl /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE launcher.cpp "$work\launcher.res" ^
   /Fe:"$launcherExe" /Fo:"$work\\" /link /SUBSYSTEM:WINDOWS || exit /b 1
"@
$bat = Join-Path $work 'build_launcher.bat'
New-Item -ItemType Directory -Force -Path $work | Out-Null
Set-Content -Path $bat -Value $cmd -Encoding ASCII
& cmd /c "`"$bat`""
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $launcherExe)) { throw 'launcher build failed' }

Remove-Item (Join-Path $pkg '7z.exe'), (Join-Path $pkg '7z.dll') -Force -ErrorAction SilentlyContinue

# --- 4. compress the payload ------------------------------------------------
Step 'Compressing payload (this takes a while)'
$archive = Join-Path $work 'payload.7z'
& (Join-Path $SevenZip '7z.exe') a -t7z -m0=lzma2 -mx=6 -mmt=on -ms=on `
    $archive (Join-Path $stage '*') | Out-Null
if ($LASTEXITCODE -ne 0) { throw '7z compression failed' }

# --- 5. launcher + archive = one file ---------------------------------------
Step 'Assembling single exe'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$out = Join-Path $dist 'EdgeTTS-Studio-Portable.exe'
$fs = [IO.File]::Open($out, 'Create')
foreach ($part in @($launcherExe, $archive)) {
    $in = [IO.File]::OpenRead($part)
    $in.CopyTo($fs, 1MB)
    $in.Close()
}
$fs.Close()

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue

$sizeMB = [math]::Round((Get-Item $out).Length / 1MB, 1)
Write-Host ""
Write-Host "Done: $out  ($sizeMB MB)" -ForegroundColor Green
