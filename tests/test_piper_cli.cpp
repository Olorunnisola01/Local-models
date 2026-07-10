// Headless smoke test for the Piper (VITS) ONNX pipeline (no Qt).
//
// Usage:
//   test_piper_cli <onnx_path> <onnx_json_path> <espeak_data_dir> <output_wav> [text]
//
// Example:
//   test_piper_cli models/piper/de_DE-thorsten-high.onnx models/piper/de_DE-thorsten-high.onnx.json . out.wav "Hallo Welt."

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include "../src/core/PiperEngine.h"
#include "../src/core/Phonemizer.h"
#include "../src/core/TtsTypes.h"
#include "../src/dsp/GraphicEq.h"
#include "../src/dsp/Normalizer.h"
#include "../src/dsp/WavWriter.h"

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "Usage: %s <onnx_path> <onnx_json_path> <espeak_data_dir> <output_wav> [text]\n",
                      argv[0]);
        return 1;
    }

    const std::string onnxPath = argv[1];
    const std::string jsonPath = argv[2];
    const std::string espeakDataDir = argv[3];
    const std::string outPath = argv[4];
    std::string text = "Dies ist ein Test der nativen Piper-Engine.";
    if (argc > 5) text = argv[5];

    try {
        std::printf("Initializing espeak-ng phonemizer (%s) ...\n", espeakDataDir.c_str());
        tts::Phonemizer phonemizer(espeakDataDir);

        std::printf("Loading Piper engine from %s ...\n", onnxPath.c_str());
        tts::PiperEngine engine(onnxPath, jsonPath);
        std::printf("Execution provider: %s\n",
                     engine.usingGpu() ? ("DirectML GPU (" + engine.gpuName() + ")").c_str() : "CPU");

        std::printf("Synthesizing: \"%s\"\n", text.c_str());
        auto t0 = std::chrono::steady_clock::now();
        tts::AudioBuffer audio = engine.synthesize(phonemizer, text, 1.0f);
        auto t1 = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
        std::printf("Generated %zu samples @ %d Hz (%.2f s) in %.3f s wall time\n", audio.samples.size(),
                     audio.sampleRate, static_cast<double>(audio.samples.size()) / audio.sampleRate, elapsedSec);

        tts::GraphicEq eq;
        eq.setGainsDb({}, static_cast<float>(audio.sampleRate));
        eq.process(audio.samples);
        tts::peakNormalize(audio.samples);

        if (!tts::writeWavPcm16(outPath, audio.samples, static_cast<uint32_t>(audio.sampleRate))) {
            std::fprintf(stderr, "Failed to write WAV file: %s\n", outPath.c_str());
            return 1;
        }
        std::printf("Wrote %s\n", outPath.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
