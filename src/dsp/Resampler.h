#pragma once

#include <vector>

namespace tts {

// Linear-interpolation resample from srcRate to dstRate. Returns `in`
// unchanged if the rates already match (or `in` is empty).
std::vector<float> resampleLinear(const std::vector<float>& in, int srcRate, int dstRate);

} // namespace tts
