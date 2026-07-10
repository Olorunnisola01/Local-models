#pragma once

#include <cstdint>

#include "../core/TtsTypes.h"

namespace tts {

// Per-stage configuration for the Naturalness DSP "Humanisation" pipeline.
// Each stage has an on/off flag plus its "degree" parameters. Defaults match
// the original hard-coded values that de-robotise Microsoft Edge voices.
struct HumanizerSettings {
    // 1. De-Robot EQ
    bool  eqEnabled    = true;
    float eqWarmthDb   = 2.5f;   // low-shelf  @195 Hz  (body/warmth)
    float eqMidCutDb   = -3.0f;  // peak cut   @2100 Hz (removes robotic harshness)
    float eqAirDb      = 1.5f;   // high-shelf @5900 Hz (air/presence)

    // 2. Compressor
    bool  compEnabled    = true;
    float compThresholdDb = -18.0f; // dB
    float compRatio       = 3.0f;   // :1
    float compMakeupDb    = 2.0f;   // dB

    // 3. Subtle Reverb
    bool  reverbEnabled = true;
    float reverbWet     = 0.08f; // 0.0 .. 0.5 (fraction wet)

    // 4. De-Esser
    bool  deEsserEnabled  = true;
    float deEsserThreshDb = -40.0f; // lower = more sibilance reduction
    float deEsserRatio    = 8.0f;   // :1

    // 5. Loudness (RMS) normalisation
    bool  loudnessEnabled  = true;
    float loudnessTargetDb = -20.0f; // target RMS dBFS

    // 6. Safety ceiling (peak limiter)
    bool  ceilingEnabled = true;
    float ceilingDb      = -1.0f; // dBFS
};

// Applies the multi-stage naturalness DSP pipeline to `buf` in place. Each
// stage runs only if enabled in `settings`. The pipeline is deterministic and
// click-free. See Humanizer.cpp for the per-stage description.
void applyHumanizer(AudioBuffer& buf, const HumanizerSettings& settings);

} // namespace tts
