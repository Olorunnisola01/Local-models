#pragma once

#include <vector>

namespace tts {

// Scales the buffer down (never up) so its peak sample magnitude matches
// targetPeakDb (default -1 dBFS). A safety ceiling applied after the
// graphic EQ so user EQ boosts can't cause clipping on export/playback.
void peakNormalize(std::vector<float>& samples, float targetPeakDb = -1.0f);

} // namespace tts
