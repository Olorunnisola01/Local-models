# Portable single-file build

Produces `dist\EdgeTTS-Studio-Portable.exe` — one file that runs on a clean
Windows 10/11 x64 machine with **nothing installed**: no Qt, no Visual C++
redistributable, no ONNX Runtime, no model download.

```powershell
.\packaging\build_portable.ps1
```

Pass `-SkipBuild` to package the existing `build\Release` without recompiling.

## How the single file is put together

The shipped exe is a launcher with a 7z archive appended to it:

```
[ launcher.exe ][ payload.7z ]
```

`launcher.cpp` embeds `7z.exe` and `7z.dll` as resources. On first run it writes
those to a temp folder and points them at *its own path* — 7-Zip finds the
appended archive by signature, the same way it reads any SFX archive — and
unpacks into `EdgeTTSStudio\` beside the launcher. It then writes a hidden
`.unpacked` marker and starts the app.

Later runs see the marker and launch immediately, so the unpack cost is paid
once rather than on every start. If an earlier run was interrupted, the marker
is absent and the partial folder is deleted and re-extracted rather than reused.

## What goes into the payload

Everything from `build\Release`, plus the Visual C++ runtime DLLs
(`Microsoft.VC143.CRT`). Without those the app needs the VC++ redistributable
installed on the target machine, which would defeat the purpose of the package.

Two trees are deliberately excluded, saving about 700 MB:

| Excluded | Reason |
| --- | --- |
| `models\supertonic\supertonic\` | Byte-for-byte duplicate of `models\supertonic\`; `MainWindow.cpp` resolves `models/supertonic/onnx`, never the nested copy |
| `models\kokoro_de_victoria_src\` | PyTorch training checkpoint (`.pth`) and `victoria.pt` used to *export* `kokoro_de_victoria`; no reference anywhere in `src/` |

## Requirements on the build machine

- Visual Studio 2022 Build Tools (MSVC v143, `rc.exe`)
- Qt 6.8.1 msvc2022_64
- 7-Zip installed at `C:\Program Files\7-Zip` (override with `-SevenZip`)
- A populated `build\Release` — including `models\`, which is gitignored and
  comes from the `scripts\fetch_*.ps1` scripts

## Notes on distribution

- The exe is unsigned, so SmartScreen shows "Windows protected your PC" on first
  run until it builds reputation. Code signing is the only real fix.
- First run needs ~2 GB free next to the exe; the launcher checks and reports
  this rather than failing mid-extraction.
- The payload redistributes third-party model weights (Kokoro, Piper,
  Supertonic, DeepFilterNet) and 7-Zip. Check each upstream licence before
  publishing the file publicly.

## The app must be a GUI-subsystem binary

`qt_add_executable(EdgeTTSStudioNative WIN32 ...)` is what keeps a console
window from appearing behind the UI. Drop the `WIN32` keyword and the app links
as subsystem 3 (Windows CUI); Windows then allocates a console for it, which
sits behind the window for the whole life of the process. Verify with:

```
dumpbin /headers EdgeTTSStudioNative.exe | findstr /i subsystem
```

It must report `2 (Windows GUI)`.
