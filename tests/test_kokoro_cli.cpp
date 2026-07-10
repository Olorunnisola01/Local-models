// Headless smoke test for the Kokoro ONNX pipeline (no Qt).
//
// Usage:
//   test_kokoro_cli <model_dir> <espeak_data_dir> <voice_name> <espeak_lang> <output_wav> [text]
//
// Example:
//   test_kokoro_cli models/kokoro . af_heart en-us out.wav "Hello world."

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include "../src/core/KokoroEngine.h"
#include "../src/core/Phonemizer.h"
#include "../src/core/TtsTypes.h"
#include "../src/dsp/GraphicEq.h"
#include "../src/dsp/Normalizer.h"
#include "../src/dsp/WavWriter.h"

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                      "Usage: %s <model_dir> <espeak_data_dir> <voice_name> <espeak_lang> <output_wav> [text]\n",
                      argv[0]);
        return 1;
    }

    const std::string modelDir = argv[1];
    const std::string espeakDataDir = argv[2];
    const std::string voiceName = argv[3];
    const std::string espeakLang = argv[4];
    const std::string outPath = argv[5];
    std::string text = "This is a test of the native Kokoro engine.";
    if (argc > 6) text = argv[6];
    std::string modelFileName = "kokoro-v1.0.onnx";
    if (argc > 7) modelFileName = argv[7];

    try {
        std::printf("Initializing espeak-ng phonemizer (%s) ...\n", espeakDataDir.c_str());
        tts::Phonemizer phonemizer(espeakDataDir);

        std::printf("Loading Kokoro engine from %s/%s ...\n", modelDir.c_str(), modelFileName.c_str());
        tts::KokoroEngine engine(modelDir, modelFileName);
        std::printf("Execution provider: %s\n",
                     engine.usingGpu() ? ("DirectML GPU (" + engine.gpuName() + ")").c_str() : "CPU");

        std::printf("Loading voice style '%s' ...\n", voiceName.c_str());
        std::vector<float> style = engine.loadVoiceStyleTable(voiceName);

        std::printf("Synthesizing: \"%s\"\n", text.c_str());
        auto t0 = std::chrono::steady_clock::now();
        tts::AudioBuffer audio = engine.synthesize(phonemizer, text, espeakLang, style, 1.0f);
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
