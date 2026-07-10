// Headless smoke test for DeepFilterNetEngine (no Qt).
//
// Usage:
//   test_dfn_cli <model_dir> <output_wav> [input_wav]
//
// With no input_wav, generates a synthetic tone + white noise signal.
// With input_wav, loads it, mixes in white noise, runs DeepFilterNet, and
// writes the 80% wet / 20% dry blend (the planned UI behavior) alongside the
// noisy input for comparison.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <random>
#include <vector>

#include "../src/core/TtsTypes.h"
#include "../src/dsp/DeepFilterNetEngine.h"
#include "../src/dsp/WavReader.h"
#include "../src/dsp/WavWriter.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <model_dir> <output_wav> [input_wav]\n", argv[0]);
        return 1;
    }
    const std::string modelDir = argv[1];
    const std::string outPath = argv[2];

    try {
        std::vector<float> input;
        int sr = 44100;

        if (argc > 3) {
            tts::AudioBuffer buf;
            if (!tts::readWavToMono(argv[3], &buf)) {
                std::fprintf(stderr, "ERROR: failed to read %s\n", argv[3]);
                return 1;
            }
            sr = buf.sampleRate;
            input = std::move(buf.samples);
            std::printf("Loaded %s: %zu samples @ %d Hz\n", argv[3], input.size(), sr);

            std::mt19937 rng(42);
            std::uniform_real_distribution<float> noise(-0.05f, 0.05f);
            for (float& s : input) s += noise(rng);

            tts::writeWavPcm16(outPath + ".noisy.wav", input, static_cast<uint32_t>(sr));
        } else {
            const double durationSec = 1.5;
            const size_t n = static_cast<size_t>(sr * durationSec);
            input.resize(n);
            std::mt19937 rng(42);
            std::uniform_real_distribution<float> noise(-0.15f, 0.15f);
            for (size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) / sr;
                input[i] = 0.4f * static_cast<float>(std::sin(2.0 * 3.14159265 * 220.0 * t)) + noise(rng);
            }
        }

        const size_t n = input.size();

        std::printf("Loading DeepFilterNet models from %s ...\n", modelDir.c_str());
        tts::DeepFilterNetEngine engine(modelDir);
        std::printf("Execution provider: %s\n",
                     engine.usingGpu() ? ("DirectML GPU (" + engine.gpuName() + ")").c_str() : "CPU");

        std::printf("Running enhance() on %zu samples at %d Hz ...\n", n, sr);
        auto t0 = std::chrono::steady_clock::now();
        std::vector<float> enhanced = engine.enhance(input, sr);
        auto t1 = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
        std::printf("Output length: %zu (input: %zu) in %.3f s wall time\n", enhanced.size(), input.size(),
                     elapsedSec);

        if (enhanced.size() != input.size()) {
            std::fprintf(stderr, "ERROR: output length does not match input length\n");
            return 1;
        }

        // Blend 80% wet / 20% dry, like the planned UI feature.
        std::vector<float> blended(n);
        for (size_t i = 0; i < n; ++i) blended[i] = 0.8f * enhanced[i] + 0.2f * input[i];

        if (!tts::writeWavPcm16(outPath, blended, static_cast<uint32_t>(sr))) {
            std::fprintf(stderr, "ERROR: failed to write %s\n", outPath.c_str());
            return 1;
        }
        std::printf("Wrote %s\n", outPath.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Exception: %s\n", e.what());
        return 1;
    }
    return 0;
}
