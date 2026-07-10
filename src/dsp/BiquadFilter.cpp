#include "BiquadFilter.h"

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tts {

BiquadCoeffs makePeakingEq(float sampleRate, float freq, float gainDb, float q) {
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);
    const float cosw0 = std::cos(w0);

    const float b0 = 1.0f + alpha * A;
    const float b1 = -2.0f * cosw0;
    const float b2 = 1.0f - alpha * A;
    const float a0 = 1.0f + alpha / A;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha / A;

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoeffs makeLowShelf(float sampleRate, float freq, float gainDb, float q) {
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);
    const float cosw0 = std::cos(w0);
    const float sqrtA = std::sqrt(A);

    const float b0 = A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alpha);
    const float b1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
    const float b2 = A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alpha);
    const float a0 = (A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alpha;
    const float a1 = -2 * ((A - 1) + (A + 1) * cosw0);
    const float a2 = (A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alpha;

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

BiquadCoeffs makeHighShelf(float sampleRate, float freq, float gainDb, float q) {
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);
    const float cosw0 = std::cos(w0);
    const float sqrtA = std::sqrt(A);

    const float b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alpha);
    const float b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
    const float b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alpha);
    const float a0 = (A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alpha;
    const float a1 = 2 * ((A - 1) - (A + 1) * cosw0);
    const float a2 = (A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alpha;

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

} // namespace tts
