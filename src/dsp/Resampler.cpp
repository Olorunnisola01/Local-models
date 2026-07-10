#include "Resampler.h"

#include <algorithm>
#include <cmath>

namespace tts {

std::vector<float> resampleLinear(const std::vector<float>& in, int srcRate, int dstRate) {
    if (srcRate == dstRate || in.empty()) {
        return in;
    }
    const double ratio = static_cast<double>(dstRate) / srcRate;
    const size_t outLen = static_cast<size_t>(std::llround(in.size() * ratio));
    std::vector<float> out(outLen);
    for (size_t i = 0; i < outLen; ++i) {
        const double srcPos = i / ratio;
        const size_t i0 = static_cast<size_t>(srcPos);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const double frac = srcPos - i0;
        out[i] = static_cast<float>(in[i0] * (1.0 - frac) + in[i1] * frac);
    }
    return out;
}

} // namespace tts
