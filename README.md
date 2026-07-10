# EdgeTTS-Studio Native

A C++/Qt6 desktop TTS studio with offline and online engines, multi-speaker
dialogue, timeline editing, pronunciation dictionary, batch export, and
production packaging. No Python runtime required to run the app.

## Features

- **Providers**: Supertonic, Kokoro (local + German martin/victoria), Piper,
  Microsoft Edge TTS (322 voices), Fish Audio S2 (remote Kaggle GPU)
- **Workflow**: Single speaker, multi-speaker dialogue, timeline editor,
  pronunciation panel, voice presets, project files (`.edgettsproj`)
- **Export**: WAV / MP3 / FLAC (ffmpeg), caption packages (SRT/VTT), batch queue
- **DSP**: 7-band graphic EQ, DeepFilterNet enhance, Natural Humanizer (Edge TTS)
- **GPU**: DirectML (AMD/Intel) with optional CUDA when available in ONNX Runtime
- **Tools**: Model Manager, First-Run Wizard, dark mode, portable packaging script

## Prerequisites

- Visual Studio 2022 Build Tools with the "Desktop development with C++" workload
- CMake >= 3.21
- Qt 6.8.x for `msvc2022_64` (Widgets, Multimedia, Network)
- ONNX Runtime 1.20.x prebuilt for `win-x64` in `third_party/onnxruntime/`
- espeak-ng prebuilt in `third_party/espeak-ng/` (phonemization for Kokoro/Piper)
- Optional: `ffmpeg.exe` on PATH or next to the exe for MP3/FLAC export

## One-time setup

```powershell
.\scripts\fetch_onnxruntime.ps1
.\scripts\sync_models.ps1
.\scripts\fetch_piper.ps1          # optional Piper voices
.\scripts\fetch_kokoro.ps1         # Kokoro setup instructions
.\scripts\fetch_deepfilternet.ps1  # DeepFilterNet status check
```

## Build

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build --config Release
```

## Run

```powershell
# GUI app (recommended)
.\build\Release\EdgeTTSStudioNative.exe

# CLI smoke tests
.\build\Release\test_supertonic_cli.exe models\supertonic\onnx models\supertonic\voice_styles\M1.json out.wav "Hello."
.\build\Release\test_kokoro_cli.exe models\kokoro espeak-ng-data af_heart en-us out.wav "Hello."
.\build\Release\test_text_markup.exe
```

The build copies `onnxruntime.dll`, `espeak-ng-data`, `models/`, and Qt
runtime DLLs next to the executables via `windeployqt`.

## Portable package

```powershell
.\scripts\package_portable.ps1 -Config Release
```

## Roadmap (completed milestones)

- **M1–M4**: Supertonic, Kokoro, Piper, EQ, WAV export
- **M5**: Portable packaging (`package_portable.ps1`)
- **M6**: Edge TTS online engine
- **M7**: Natural Humanizer DSP (lightweight; richer chain ongoing)
- **Extras**: Fish Audio S2 remote, timeline editor, batch queue, Model Manager,
  concurrent chunk synthesis, voice search, autosave recovery