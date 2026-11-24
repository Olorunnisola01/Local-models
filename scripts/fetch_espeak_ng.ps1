# Downloads the official espeak-ng Windows x64 installer (.msi), extracts it
# without installing, and vendors what the build needs into
# third_party\espeak-ng\:
#   include\espeak-ng\speak_lib.h   (from the matching source tag)
#   lib\libespeak-ng.dll            (from the .msi)
#   lib\espeak-ng.lib               (import library generated from the dll)
#   espeak-ng-data\                 (phoneme data, from the .msi)
#
# Run from a "Developer PowerShell for VS 2022" (needs dumpbin.exe and lib.exe
# on PATH to generate the import library).
param(
    [string]$Tag = "1.52.0"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
$dst = Join-Path $root "third_party\espeak-ng"
$tmp = Join-Path $env:TEMP "espeak-ng-$Tag"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$headers = @{ "User-Agent" = "Local-models-fetch" }
if ($env:GITHUB_TOKEN) { $headers["Authorization"] = "Bearer $env:GITHUB_TOKEN" }

# 1. Locate and download the x64 .msi from the GitHub release
$release = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/espeak-ng/espeak-ng/releases/tags/$Tag"
$msis = $release.assets | Where-Object { $_.name -like "*.msi" }
$msi = ($msis | Where-Object { $_.name -match "x64|amd64|win64" } | Select-Object -First 1)
if (-not $msi) { $msi = $msis | Select-Object -First 1 }
if (-not $msi) { throw "No .msi asset found in espeak-ng release $Tag" }
$msiPath = Join-Path $tmp $msi.name
Write-Host "Downloading $($msi.browser_download_url) ..."
Invoke-WebRequest -Headers $headers -Uri $msi.browser_download_url -OutFile $msiPath

# 2. Administrative extract (no install)
$extract = Join-Path $tmp "msi"
$p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList "/a `"$msiPath`" /qn TARGETDIR=`"$extract`""
if ($p.ExitCode -ne 0) { throw "msiexec extraction failed ($($p.ExitCode))" }

$dll = Get-ChildItem -Recurse -Path $extract -Filter "libespeak-ng.dll" | Select-Object -First 1
$data = Get-ChildItem -Recurse -Directory -Path $extract -Filter "espeak-ng-data" | Select-Object -First 1
if (-not $dll -or -not $data) { throw "libespeak-ng.dll or espeak-ng-data not found in the .msi" }

# 3. Header from the matching source tag
$hdrUrl = "https://raw.githubusercontent.com/espeak-ng/espeak-ng/$Tag/src/include/espeak-ng/speak_lib.h"
New-Item -ItemType Directory -Force -Path (Join-Path $dst "include\espeak-ng"), (Join-Path $dst "lib") | Out-Null
Invoke-WebRequest -Headers @{ "User-Agent" = "Local-models-fetch" } -Uri $hdrUrl -OutFile (Join-Path $dst "include\espeak-ng\speak_lib.h")

# 4. Copy dll + data
Copy-Item -Force $dll.FullName (Join-Path $dst "lib\libespeak-ng.dll")
$dstData = Join-Path $dst "espeak-ng-data"
if (Test-Path $dstData) { Remove-Item -Recurse -Force $dstData }
Copy-Item -Recurse -Force $data.FullName $dstData

# 5. Generate the MSVC import library from the dll's exports
$exports = & dumpbin.exe /exports $dll.FullName
$names = $exports | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') { $Matches[1] }
}
if (-not $names) { throw "No exports read from libespeak-ng.dll (is dumpbin on PATH?)" }
$def = Join-Path $tmp "libespeak-ng.def"
@("LIBRARY libespeak-ng", "EXPORTS") + $names | Set-Content -Encoding ascii $def
& lib.exe /nologo /machine:x64 /def:$def /out:(Join-Path $dst "lib\espeak-ng.lib")
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed to create espeak-ng.lib" }

Write-Host "Vendored espeak-ng $Tag into $dst"
