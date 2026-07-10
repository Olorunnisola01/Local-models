#pragma once

#include <array>
#include <vector>

#include "BiquadFilter.h"

namespace tts {

// A fixed 7-band graphic EQ: a low-shelf at the bottom, 5 peaking bands in
// the middle, and a high-shelf at the top. Center frequencies are roughly
// log-spaced across the audible range.
class GraphicEq {
public:
    static constexpr int kNumBands = 7;
    static constexpr std::array<float, kNumBands> kCenterFreqs = {
        60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 12000.0f};

    // gainsDb: one gain per band, in decibels (typically -12..+12).
    void setGainsDb(const std::array<float, kNumBands>& gainsDb, float sampleRate);

    // Applies the cascade in-place over the whole buffer. Resets filter
    // state first, since this is offline batch processing (not streaming).
    void process(std::vector<float>& samples);

private:
    std::array<BiquadFilter, kNumBands> bands_{};
};

} // namespace tts
