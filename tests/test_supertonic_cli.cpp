// Headless smoke test for the Supertonic ONNX pipeline (no Qt).
//
// Usage:
//   test_supertonic_cli <model_dir> <voice_style_json> <output_wav> [text] [--debug-io]
//
// Example:
//   test_supertonic_cli models/supertonic/onnx models/supertonic/voice_styles/M1.json out.wav "Hello world."

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include "../src/core/SupertonicEngine.h"
#include "../src/core/TtsTypes.h"
#include "../src/core/VoiceStyle.h"
#include "../src/dsp/GraphicEq.h"
#include "../src/dsp/Normalizer.h"
#include "../src/dsp/WavWriter.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                      "Usage: %s <model_dir> <voice_style_json> <output_wav> [text] [--debug-io]\n",
                      argv[0]);
        return 1;
    }

    const std::string modelDir = argv[1];
    const std::string stylePath = argv[2];
    const std::string outPath = argv[3];
    std::string text = "This is a test of the native Supertonic engine.";
    bool debugIo = false;

    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--debug-io") {
            debugIo = true;
        } else {
            text = arg;
        }
    }

    try {
        std::printf("Loading Supertonic engine from %s ...\n", modelDir.c_str());
        tts::SupertonicEngine engine(modelDir);
        std::printf("Execution provider: %s\n",
                     engine.usingGpu() ? ("DirectML GPU (" + engine.gpuName() + ")").c_str() : "CPU");

        if (debugIo) {
            engine.debugPrintIO();
        }

        std::printf("Loading voice style from %s ...\n", stylePath.c_str());
        tts::VoiceStyle style = tts::VoiceStyle::loadFromJson(stylePath);

        tts::SynthParams params;
        params.text = text;
        params.speed = 1.05f;
        params.totalSteps = 8;
        params.lang = "na";

        std::printf("Synthesizing: \"%s\"\n", text.c_str());
        auto t0 = std::chrono::steady_clock::now();
        tts::AudioBuffer audio = engine.synthesize(params, style);
        auto t1 = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
        std::printf("Generated %zu samples @ %d Hz (%.2f s) in %.3f s wall time\n", audio.samples.size(),
                     audio.sampleRate, static_cast<double>(audio.samples.size()) / audio.sampleRate, elapsedSec);

        // Default-flat EQ + peak normalize, mirroring the GUI's processing chain.
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
