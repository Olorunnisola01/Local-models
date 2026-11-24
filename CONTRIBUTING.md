# Contributing to EdgeTTS-Studio Native

Thanks for your interest in improving EdgeTTS-Studio Native! Contributions of
all sizes are welcome: bug reports, new voices or engines, DSP improvements,
documentation fixes and translations.

## Ways to contribute

- **Report a bug:** open an issue using the *Bug report* template. Include the
  engine, voice, Windows version, GPU (if DirectML is enabled) and the exact
  error text or a short audio sample.
- **Request a feature or engine:** use the *Feature request* template and
  describe the use case.
- **Submit code:** fork the repo, create a branch, and open a pull request.

## Development setup

Follow the README steps 1–6 to install the toolchain (Visual Studio 2022,
CMake ≥ 3.21, Qt 6.8 for `msvc2022_64`), fetch ONNX Runtime and espeak-ng, and
build:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build --config Release
```

You only need the models for the engine you are working on. Microsoft Edge
voices need no models at all.

## Before opening a pull request

1. Build in **Release** with no new compiler warnings.
2. Run the CLI smoke tests relevant to your change, for example:
   ```powershell
   .\build\Release\test_text_markup.exe
   .\build\Release\test_edge_tts_cli.exe en-US-JennyNeural out.mp3 "Hello."
   ```
3. Keep pull requests focused: one feature or fix per PR.
4. Update the README if you change user-facing behaviour, settings or
   setup steps.
5. **Never commit model weights, binaries, audio outputs or logs.** They are
   git-ignored for a reason (size and third-party licences).

## Code style

- C++20, Qt 6 Widgets. Follow the existing style in `src/`: engines in
  `src/core/`, audio processing in `src/dsp/`, UI in `src/ui/`.
- Prefer clear names over comments; add comments for non-obvious DSP or model
  I/O details.
- New engines should implement the same interface as the existing engines in
  `src/core/` and get a CLI smoke test in `tests/`.

## Licensing of contributions

By submitting a contribution you agree that it is licensed under the project's
[MIT License](LICENSE). Do not submit code or models you do not have the right
to license this way.

## Code of conduct

Please be respectful and constructive. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
