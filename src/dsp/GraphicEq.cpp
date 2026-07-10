#include "GraphicEq.h"

namespace tts {

void GraphicEq::setGainsDb(const std::array<float, kNumBands>& gainsDb, float sampleRate) {
    constexpr float kQ = 1.0f; // moderate Q for both shelves and peaking bands

    bands_[0].setCoeffs(makeLowShelf(sampleRate, kCenterFreqs[0], gainsDb[0], kQ));
    for (int i = 1; i < kNumBands - 1; ++i) {
        bands_[i].setCoeffs(makePeakingEq(sampleRate, kCenterFreqs[i], gainsDb[i], kQ));
    }
    bands_[kNumBands - 1].setCoeffs(makeHighShelf(sampleRate, kCenterFreqs[kNumBands - 1], gainsDb[kNumBands - 1], kQ));
}

void GraphicEq::process(std::vector<float>& samples) {
    for (auto& band : bands_) {
        band.reset();
    }
    for (float& s : samples) {
        for (auto& band : bands_) {
            s = band.processSample(s);
        }
    }
}

} // namespace tts
