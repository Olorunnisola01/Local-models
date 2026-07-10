#pragma once

namespace tts {

// RBJ Audio Cookbook biquad coefficients (a0 normalized to 1).
struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
};

// RBJ peaking EQ: boosts/cuts a band around `freq` by `gainDb`, width set by `q`.
BiquadCoeffs makePeakingEq(float sampleRate, float freq, float gainDb, float q);

// RBJ low-shelf: boosts/cuts everything below `freq` by `gainDb`.
BiquadCoeffs makeLowShelf(float sampleRate, float freq, float gainDb, float q);

// RBJ high-shelf: boosts/cuts everything above `freq` by `gainDb`.
BiquadCoeffs makeHighShelf(float sampleRate, float freq, float gainDb, float q);

// A single second-order IIR section (Direct Form II Transposed) with
// persistent state across calls to process().
class BiquadFilter {
public:
    void setCoeffs(const BiquadCoeffs& c) { c_ = c; }
    void reset() { z1_ = z2_ = 0.0f; }

    float processSample(float x) {
        float y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

private:
    BiquadCoeffs c_{};
    float z1_ = 0.0f, z2_ = 0.0f;
};

} // namespace tts
