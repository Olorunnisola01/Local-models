// Headless smoke test for the Microsoft Edge TTS websocket client.
// Requires internet access to speech.platform.bing.com.
//
// Usage:
//   test_edge_tts_cli <output_wav> [voice] [text]
//
// Example:
//   test_edge_tts_cli out.wav de-DE-KatjaNeural "Hallo, das ist ein Test."

#include <cstdio>
#include <exception>
#include <string>

#include <QCoreApplication>

#include "../src/core/EdgeTtsEngine.h"
#include "../src/core/TtsTypes.h"
#include "../src/dsp/GraphicEq.h"
#include "../src/dsp/Normalizer.h"
#include "../src/dsp/WavWriter.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <output_wav> [voice] [text]\n", argv[0]);
        return 1;
    }

    const std::string outPath = argv[1];
    std::string voice = "en-US-AvaMultilingualNeural";
    if (argc > 2) voice = argv[2];
    std::string text = "This is a test of the Microsoft Edge text to speech engine.";
    if (argc > 3) text = argv[3];

    try {
        std::printf("Synthesizing with voice \"%s\": \"%s\"\n", voice.c_str(), text.c_str());
        tts::EdgeTtsEngine engine;
        tts::AudioBuffer audio = engine.synthesize(text, voice, 1.0f);
        std::printf("Generated %zu samples @ %d Hz (%.2f s)\n", audio.samples.size(), audio.sampleRate,
                     static_cast<double>(audio.samples.size()) / audio.sampleRate);

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
