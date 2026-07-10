# Downloads the prebuilt ONNX Runtime DirectML release for Windows x64 and
# vendors its headers/lib/dll into native/third_party/onnxruntime, enabling
# GPU acceleration (DirectML execution provider) via SupertonicEngine's
# GpuDevice adapter selection.
$ErrorActionPreference = "Stop"
$version = "1.20.1"
$nupkgUrl = "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/$version"
$root = Resolve-Path "$PSScriptRoot\.."
$nupkgPath = Join-Path $env:TEMP "Microsoft.ML.OnnxRuntime.DirectML.$version.nupkg"
$extractDir = Join-Path $env:TEMP "ort_dml_$version"

Write-Host "Downloading $nupkgUrl ..."
Invoke-WebRequest -Uri $nupkgUrl -OutFile $nupkgPath

Write-Host "Extracting..."
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
Expand-Archive -Path $nupkgPath -DestinationPath $extractDir

$srcInclude = Join-Path $extractDir "build\native\include"
$srcNative = Join-Path $extractDir "runtimes\win-x64\native"
$dstInclude = Join-Path $root "third_party\onnxruntime\include"
$dstLib = Join-Path $root "third_party\onnxruntime\lib"

New-Item -ItemType Directory -Force -Path $dstInclude, $dstLib | Out-Null
Copy-Item -Force "$srcInclude\*" $dstInclude
Copy-Item -Force "$srcNative\onnxruntime.dll" $dstLib
Copy-Item -Force "$srcNative\onnxruntime.lib" $dstLib

Write-Host "Vendored ONNX Runtime $version (DirectML) into $root\third_party\onnxruntime"

# DirectML.dll (the actual DirectML redistributable, loaded by onnxruntime.dll
# at runtime for the DML execution provider) is NOT included in the ORT
# DirectML nupkg's runtimes folder, and the Microsoft.AI.DirectML nupkg is
# ~190MB (multi-arch/config), which is impractically slow to fetch here.
#
# DirectML.h and directml.lib are already provided by the Windows 10/11 SDK
# (Windows Kits\10\Include\<ver>\um and Lib\<ver>\um\x64), so MSVC resolves
# <DirectML.h> via its default include paths without vendoring.
#
# DirectML.dll itself just needs to be >= 1.15.2 (the version ORT 1.20.1's
# DirectML EP depends on). Many installed apps (e.g. Microsoft 365's
# WinAppSDK, recent Adobe CC apps) ship a compatible copy. Copy one in:
$dmlCandidates = @(
    "$env:ProgramFiles\Microsoft Office\root\Office16\WinAppSDK\DirectML.dll",
    "$env:ProgramFiles\Adobe\Adobe Premiere Pro 2025\DirectML.dll"
)
$dmlSrc = $dmlCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($dmlSrc) {
    Copy-Item -Force $dmlSrc (Join-Path $dstLib "DirectML.dll")
    Write-Host "Copied DirectML.dll from $dmlSrc"
} else {
    Write-Warning "DirectML.dll not found in known locations; copy a >=1.15.2 build to $dstLib\DirectML.dll manually (e.g. from the Microsoft.AI.DirectML NuGet package)."
}
