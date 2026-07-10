#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tts {

// A buffer of mono float32 PCM samples in [-1, 1] plus its sample rate.
struct AudioBuffer {
    std::vector<float> samples;
    int sampleRate = 44100;
};

// Parameters controlling a single Supertonic synthesis call.
struct SynthParams {
    std::string text;
    float speed = 1.05f;     // MIN_SPEED=0.7, MAX_SPEED=2.0
    int totalSteps = 8;       // flow-matching iterations (DEFAULT_TOTAL_STEPS)
    std::string lang = "na"; // language-agnostic fallback
};

} // namespace tts
