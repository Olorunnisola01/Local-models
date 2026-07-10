#include "Normalizer.h"

#include <algorithm>
#include <cmath>

namespace tts {

void peakNormalize(std::vector<float>& samples, float targetPeakDb) {
    float peak = 0.0f;
    for (float s : samples) {
        peak = std::max(peak, std::fabs(s));
    }
    if (peak < 1e-9f) {
        return;
    }
    const float target = std::pow(10.0f, targetPeakDb / 20.0f);
    if (peak > target) {
        const float scale = target / peak;
        for (float& s : samples) {
            s *= scale;
        }
    }
}

} // namespace tts
