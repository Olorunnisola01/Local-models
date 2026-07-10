// Headless concurrency stress test for KokoroEngine on the DirectML GPU EP.
//
// Usage:
//   test_kokoro_concurrency <model_dir> <espeak_data_dir> <voice_name> <espeak_lang>

#include <cstdio>
#include <exception>
#include <future>
#include <string>
#include <vector>

#include "../src/core/KokoroEngine.h"
#include "../src/core/Phonemizer.h"
#include "../src/core/TtsTypes.h"

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "Usage: %s <model_dir> <espeak_data_dir> <voice_name> <espeak_lang>\n", argv[0]);
        return 1;
    }
    const std::string modelDir = argv[1];
    const std::string espeakDataDir = argv[2];
    const std::string voiceName = argv[3];
    const std::string espeakLang = argv[4];

    try {
        tts::Phonemizer phonemizer(espeakDataDir);
        tts::KokoroEngine engine(modelDir);
        std::printf("Execution provider: %s\n",
                     engine.usingGpu() ? ("DirectML GPU (" + engine.gpuName() + ")").c_str() : "CPU");
        std::vector<float> style = engine.loadVoiceStyleTable(voiceName);

        std::vector<std::string> texts = {
            "The quick brown fox jumps over the lazy dog near the riverbank.",
            "Mixing two voices together can sometimes reveal hidden bugs.",
            "Concurrent execution on the GPU requires careful synchronization.",
            "This sentence is the fourth chunk running at the same time.",
        };

        std::vector<std::future<tts::AudioBuffer>> futures;
        for (const auto& text : texts) {
            futures.push_back(std::async(std::launch::async, [&engine, &phonemizer, &text, &espeakLang, &style]() {
                return engine.synthesize(phonemizer, text, espeakLang, style, 1.0f);
            }));
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            tts::AudioBuffer audio = futures[i].get();
            std::printf("chunk %zu: %zu samples\n", i, audio.samples.size());
        }
        std::printf("OK: all concurrent chunks completed without crashing\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
