# EdgeTTS-Studio Native

A fast, native **C++/Qt6 desktop text-to-speech studio** for Windows that brings
five TTS engines under one roof — three fully offline, one free online, and one
GPU-accelerated remote engine. It supports single-speaker narration,
multi-speaker dialogue, a timeline editor, a pronunciation dictionary, a
per-voice graphic EQ, AI noise-suppression (DeepFilterNet), a "Natural
Humanizer" DSP chain, and batch/caption export — **with no Python runtime needed
to run the app**.

> **Important:** the model weights and the compiled binaries are **not** stored
> in this repository (they are gigabytes in size). After cloning you must
> **download the models** and **build the app** using the steps below. This
> README walks you through every engine so you can get all of them working.

---

## Table of contents

1. [Engines at a glance](#engines-at-a-glance)
2. [System requirements](#system-requirements)
3. [Install the build tools](#1-install-the-build-tools)
4. [Get the code](#2-get-the-code)
5. [Download ONNX Runtime & espeak-ng](#3-download-onnx-runtime--espeak-ng)
6. [Download the models](#4-download-the-models)
7. [Expected folder layout](#5-expected-folder-layout)
8. [Build the app](#6-build-the-app)
9. [Run the app](#7-run-the-app)
10. [Using each engine](#using-each-engine)
11. [Remote GPU engines (Fish Audio S2 & remote Kokoro)](#remote-gpu-engines)
12. [The Natural Humanizer](#the-natural-humanizer)
13. [Multi-speaker dialogue](#multi-speaker-dialogue)
14. [Exporting audio](#exporting-audio)
15. [Command-line test tools](#command-line-test-tools)
16. [Portable package](#portable-package)
17. [Troubleshooting](#troubleshooting)
18. [Project structure](#project-structure)
19. [Credits & licensing](#credits--licensing)

---

## Engines at a glance

| Engine | Runs | Needs GPU? | Needs download | Best for |
|--------|------|-----------|----------------|----------|
| **Supertonic** | Offline | No (DirectML optional) | Supertonic ONNX + voice styles | Fast multilingual narration, 10 built-in voices (M1–M5, F1–F5) |
| **Kokoro** | Offline | No (DirectML optional) | Kokoro-82M ONNX + voices | High-quality neural voices, 54 voices / 9 languages, plus German *martin* & *victoria* |
| **Piper** | Offline | No | Piper `.onnx` voices | Lightweight German voices (Thorsten, Kerstin, Eva K) |
| **Microsoft Edge** | **Online (free)** | No | Nothing | 322 cloud neural voices, gender/language filtering, Natural Humanizer |
| **Fish Audio S2** | **Remote GPU** | Yes (remote) | Model hosted on Kaggle/RunPod | Voice cloning & expressive S2 synthesis via a GPU server |

You do **not** need every engine. If you only want Microsoft Edge voices, you can
skip all the model downloads — Edge works out of the box once the app is built.

---

## System requirements

- **OS:** Windows 10 / 11 (x64)
- **Disk:** ~2 GB for the app + ONNX Runtime; **+2–9 GB per model set** you install
- **RAM:** 8 GB minimum (16 GB recommended for Kokoro/Fish)
- **GPU (optional):** any DirectML-capable GPU (AMD, Intel, or NVIDIA) accelerates
  Supertonic/Kokoro. CPU-only works fine too.
- **Internet:** required for Microsoft Edge voices and the first-time model downloads

---

## 1. Install the build tools

Install these once:

| Tool | Notes |
|------|-------|
| **Visual Studio 2022** (or Build Tools) | Select the **"Desktop development with C++"** workload (MSVC v143) |
| **CMake ≥ 3.21** | <https://cmake.org/download/> — add to PATH |
| **Qt 6.8.x** for `msvc2022_64` | Install via the [Qt Online Installer](https://www.qt.io/download-qt-installer); tick **Qt Widgets, Qt Multimedia, Qt Network**. Note the install path, e.g. `C:\Qt\6.8.1\msvc2022_64` |
| **Git** | <https://git-scm.com/> |
| **ffmpeg** (optional) | Needed only for MP3/FLAC export. Put `ffmpeg.exe` on PATH or beside the built `.exe` |
| **Python 3.10** (optional) | Only for `fetch_fish_s2.ps1` (downloads Fish weights via `huggingface_hub`) |

---

## 2. Get the code

```powershell
git clone https://github.com/Olorunnisola01/Local-models.git
cd Local-models
```

All commands below are run from this folder in **PowerShell**.

---

## 3. Download ONNX Runtime & espeak-ng

These power the offline engines. **Required for Supertonic, Kokoro, Piper, and
DeepFilterNet** (not needed if you only use Microsoft Edge).

### ONNX Runtime (DirectML build)

```powershell
.\scripts\fetch_onnxruntime.ps1
```

This downloads the prebuilt **ONNX Runtime DirectML** package from NuGet and
vendors `onnxruntime.dll` + headers into `third_party\onnxruntime\`. It also
copies `DirectML.dll` for GPU acceleration.

### espeak-ng (phonemizer for Kokoro & Piper)

Kokoro and Piper convert text to phonemes with **espeak-ng**. Place a prebuilt
espeak-ng into `third_party\espeak-ng\` so that `espeak-ng-data\` exists there.
Get it from the [espeak-ng releases](https://github.com/espeak-ng/espeak-ng/releases)
(Windows build). If you already have `espeak-ng-data` from another install, copy
that folder in.

---

## 4. Download the models

Each engine's weights live under `models\<engine>\`. Run only the ones you want.

### Supertonic (offline, multilingual)

Place the Supertonic ONNX models and voice-style JSONs under
`models\supertonic\` (see [layout](#5-expected-folder-layout)). If you received
a copy of the models elsewhere on disk, `scripts\sync_models.ps1` copies them
into place. Supertonic ships 10 voices: **M1–M5** (male), **F1–F5** (female).

### Kokoro-82M (offline neural, 54 voices)

```powershell
.\scripts\fetch_kokoro.ps1   # prints instructions + verifies files
```

Download `kokoro-v1.0.onnx`, `vocab.json`, and the `voices\*.bin` files from:

- Official: <https://huggingface.co/hexgrad/Kokoro-82M>
- ONNX export: <https://huggingface.co/onnx-community/Kokoro-82M-ONNX>

Place them in `models\kokoro\`. The German **martin** and **victoria** voices use
separate models in `models\kokoro_de_martin\` and `models\kokoro_de_victoria\`.

### Piper (offline German)

```powershell
.\scripts\fetch_piper.ps1
```

Automatically downloads `de_DE-thorsten-high` and `de_DE-kerstin-low` (each is an
`.onnx` + `.onnx.json`) from the [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices)
repo into `models\piper\`. Drop in any other Piper `.onnx`/`.onnx.json` pair and
the app will auto-discover it.

### DeepFilterNet (AI noise suppression — optional)

```powershell
.\scripts\fetch_deepfilternet.ps1   # prints instructions + verifies files
```

Place `enc.onnx`, `erb_dec.onnx`, and `df_dec.onnx` into
`models\deepfilternet\`. This powers the **"Enhance (DeepFilterNet)"** button.

### Microsoft Edge (online — nothing to download)

Edge TTS streams from Microsoft's free cloud service. No model files. Just pick
**Microsoft Edge (Online)** as the provider in the app.

### Fish Audio S2 (remote GPU)

Fish S2 does **not** run locally — it runs on a remote GPU server you start on
Kaggle or RunPod, which the app connects to over a URL. See
[Remote GPU engines](#remote-gpu-engines). The `scripts\fetch_fish_s2.ps1` helper
downloads the `fishaudio/s2-pro` weights (~9 GB) if you want to host them
yourself.

---

## 5. Expected folder layout

After downloading, your `models\` and `third_party\` folders should look like
this (only include the engines you use):

```
Local-models/
├─ third_party/
│  ├─ onnxruntime/           # from fetch_onnxruntime.ps1
│  │  ├─ include/            # onnxruntime headers
│  │  └─ lib/                # onnxruntime.dll, DirectML.dll, .lib
│  ├─ espeak-ng/
│  │  └─ espeak-ng-data/     # phoneme data
│  └─ nlohmann/json.hpp      # (already in repo)
├─ models/
│  ├─ supertonic/
│  │  ├─ onnx/               # Supertonic ONNX models
│  │  └─ voice_styles/       # M1.json … F5.json
│  ├─ kokoro/
│  │  ├─ kokoro-v1.0.onnx
│  │  ├─ vocab.json
│  │  └─ voices/*.bin
│  ├─ kokoro_de_martin/      # German "martin"
│  ├─ kokoro_de_victoria/    # German "victoria"
│  ├─ piper/                 # *.onnx + *.onnx.json
│  └─ deepfilternet/         # enc.onnx, erb_dec.onnx, df_dec.onnx
└─ build/                    # created by CMake (git-ignored)
```

> The app also has a built-in **Model Manager** (Tools → Model Manager) and a
> **First-Run Wizard** that check which models are present and point you to what's
> missing.

---

## 6. Build the app

Point CMake at your Qt install and build in Release:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build --config Release
```

The build automatically copies `onnxruntime.dll`, `DirectML.dll`,
`espeak-ng-data`, your `models\`, and the Qt runtime DLLs (via `windeployqt`)
next to the executable, so the result is self-contained.

- **C++ standard:** C++20 · **Toolchain:** MSVC v143 · **UI:** Qt 6 Widgets

---

## 7. Run the app

```powershell
.\build\Release\EdgeTTSStudioNative.exe
```

On first launch the **First-Run Wizard** verifies which engines are ready
(green ✓) and which need models. You can start using **Microsoft Edge** voices
immediately even with nothing else installed.

---

## Using each engine

Everything happens in the **Single Speaker** tab (or **Multi-Speaker Dialogue**).
Pick a **Provider**, then a **Voice**, type your text, and click **Synthesize** →
**Play** / **Export Audio**.

### Supertonic / Kokoro / Piper (offline)

1. Select the provider. The voice list populates from your installed models.
2. (Optional) tick **Use GPU acceleration (DirectML)** at the top for a speed-up.
3. (Supertonic/Kokoro) tick **Mix** to blend two voices with a ratio slider.
4. Adjust **Speed**, open **EQ…** for a 7-band graphic equalizer per voice.

### Microsoft Edge (online, 322 voices)

1. Select **Microsoft Edge (Online)**. Click **Test Edge TTS Connection** to
   confirm connectivity.
2. Use the **gender** and **language** dropdowns beside the voice picker to
   narrow 322 voices down fast (e.g. *Female* + *en-US*). The voice list is also
   **type-to-search** with a neat scrollable dropdown.
3. Tick **Natural Humanizer (Edge TTS)** and click the **⚙** button to tune the
   DSP stages (see [below](#the-natural-humanizer)).
4. Inline tags: `[emph]word[/emph]` or `*word*` for emphasis, `[pause=400ms]` for
   pauses.

### Fish Audio S2 (remote GPU)

See the next section — you start a server, paste its URL, and select the
provider.

---

## Remote GPU engines

Two engines can offload to a remote GPU: **Fish Audio S2** and (optionally)
**Kokoro**. The pattern is the same:

1. Start a GPU server (Kaggle notebook or RunPod pod) that runs the model and
   exposes an HTTP port.
2. Expose that port publicly — via a **cloudflared tunnel** (Kaggle) or the
   built-in **proxy URL** (RunPod).
3. Paste the resulting URL into the app's **Fish Audio S2 server** (or
   **Route Kokoro to remote server**) field and tick the checkbox.
4. Select **Fish Audio S2 (Kaggle)** as the provider and synthesize.

### Kaggle (free GPU, ~12 h/session)

A ready-made notebook is included: **`scripts\kaggle_fish_s2_server.ipynb`**.
Upload it to Kaggle, enable a **GPU T4** accelerator, and Run All. It installs
`fish-speech`, downloads the model, starts the API server on port 8080, and
opens a cloudflared tunnel that prints a URL like:

```
https://<random-words>.trycloudflare.com
```

Paste that into the app. (The tunnel URL changes each session.)

### RunPod (paid, stable, no time limit)

Deploy a **PyTorch pod** (an A40 at ~$0.44/hr is plenty), expose **HTTP port
8080**, and run the same server script (you can drop the cloudflared part —
RunPod gives you a stable `https://<POD_ID>-8080.proxy.runpod.net` URL
automatically). Paste that URL into the app. Remember to **stop the pod** when
done, since it bills per second.

> **Voice cloning:** with Fish S2 selected, use the **Voice Cloning** panel to
> browse a reference `.wav`, extract tokens, and save it to a slot for reuse.

---

## The Natural Humanizer

Microsoft Edge voices can sound slightly robotic. The **Natural Humanizer** runs
a 6-stage DSP chain over the audio to warm it up and de-robotise it. Enable the
checkbox, then click **⚙** to open the per-stage editor where every stage has an
on/off toggle and adjustable "degree":

| Stage | What it does |
|-------|--------------|
| **De-Robot EQ** | Warmth low-shelf, harsh-mid cut, air high-shelf — the biggest improvement |
| **Compressor** | Gentle glue compression for even levels |
| **Subtle Reverb** | A touch of room ambience instead of a dead studio sound |
| **De-Esser** | Tames synthetic "sss/t/z" sibilance |
| **Loudness** | Normalises to a consistent broadcast-ish level |
| **Safety Ceiling** | Brick-wall limiter so it never clips |

Settings persist between sessions. In **multi-speaker** dialogue you can enable
the Humanizer **per speaker**.

---

## Multi-speaker dialogue

Open the **Multi-Speaker Dialogue** tab. Write one turn per line prefixed with a
speaker letter:

```
A: Hello there! How are you doing today?
B: I'm doing quite well, thank you for asking.
A: Great — did you finish the project?
```

- Set the **number of speakers** (A–E) and assign each a provider + voice using
  the same dropdown/filter style as single speaker.
- Each speaker has its own **speed, EQ, voice preset, and Natural Humanizer**
  toggle.
- Click **Render All** to synthesize the whole conversation with natural pauses,
  then **Play**/**Export**. The **Timeline** tab lets you re-render or nudge
  individual segments.

---

## Exporting audio

- **Export Audio…** saves the current render as **WAV** (always) or **MP3/FLAC**
  (requires `ffmpeg`).
- **Caption packages**: export **SRT/VTT** alongside the audio.
- **Batch queue** (Tools): render many texts/projects unattended.
- Projects save as **`.edgettsproj`** and restore every setting, including
  per-speaker config and Humanizer stages.

---

## Command-line test tools

The build also produces small CLI tools for smoke-testing engines without the UI:

```powershell
.\build\Release\test_supertonic_cli.exe models\supertonic\onnx models\supertonic\voice_styles\M1.json out.wav "Hello."
.\build\Release\test_kokoro_cli.exe     models\kokoro espeak-ng-data af_heart en-us out.wav "Hello."
.\build\Release\test_piper_cli.exe      models\piper\de_DE-thorsten-high.onnx out.wav "Hallo."
.\build\Release\test_edge_tts_cli.exe   en-US-JennyNeural out.mp3 "Hello from Edge."
.\build\Release\test_text_markup.exe
```

---

## Portable package

Bundle a self-contained, redistributable folder (exe + all DLLs + models):

```powershell
.\scripts\package_portable.ps1 -Config Release
```

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| **CMake can't find Qt6** | Pass the exact path: `-DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"` |
| **`onnxruntime.dll` not found at runtime** | Re-run `fetch_onnxruntime.ps1`, then rebuild so the copy step runs |
| **Kokoro/Piper produce silence or crash** | `espeak-ng-data` is missing from `third_party\espeak-ng\` |
| **Edge TTS: "timed out waiting for audio"** | Network/proxy issue — click **Test Edge TTS Connection**; retry when it reports *Connected* |
| **Fish S2: HTTP 502** | The remote server on port 8080 isn't up yet — check the Kaggle/RunPod logs; the tunnel is fine but the backend crashed |
| **MP3/FLAC export greyed out** | Put `ffmpeg.exe` on PATH or next to the exe |
| **GPU checkbox does nothing** | Your ONNX Runtime build lacks DirectML, or no compatible GPU — CPU still works |
| **Voice missing after filtering** | Selecting a saved voice auto-clears the gender/language filters so it can reappear |

---

## Project structure

```
src/
├─ core/     # engines: Supertonic, Kokoro, Piper, EdgeTts, RemoteFishSpeech, VoiceCatalog…
├─ dsp/      # Humanizer, GraphicEq, DeepFilterNet, BiquadFilter, WavReader/Writer, Resampler…
└─ ui/       # MainWindow, SpeakerVoiceCard, MultiSpeakerPanel, TimelinePanel, dialogs…
scripts/     # fetch_*.ps1 model downloaders, sync/package helpers, Kaggle server notebook
tests/       # CLI smoke tests
third_party/ # onnxruntime, espeak-ng, nlohmann/json
```

---

## Credits & licensing

This app integrates several open models and libraries — each retains its own
license. Review and comply with the terms of whichever you use:

- **Supertonic** · **Kokoro-82M** (hexgrad) · **Piper** (rhasspy) ·
  **Fish Audio S2** (fishaudio) · **DeepFilterNet** · **espeak-ng** ·
  **ONNX Runtime** (Microsoft) · **Qt 6** (LGPL) · **nlohmann/json**
- **Microsoft Edge TTS** is a Microsoft cloud service; use it in accordance with
  Microsoft's terms.

The application code in this repository is provided as-is for personal and
research use.
