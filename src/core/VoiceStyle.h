#pragma once

#include <string>
#include <vector>

namespace tts {

// Holds the two style tensors that condition Supertonic synthesis for a
// particular voice (e.g. "M1", "F3").
struct VoiceStyle {
    std::vector<float> styleTtl; // flattened [1, 50, 256] = 12800 floats
    std::vector<float> styleDp;  // flattened [1, 8, 16] = 128 floats

    static constexpr int64_t kStyleTtlDims[3] = {1, 50, 256};
    static constexpr int64_t kStyleDpDims[3] = {1, 8, 16};

    // Loads a voice style JSON file (e.g. models/supertonic/voice_styles/M1.json)
    // with shape {"style_ttl": {"data": [...], "dims": [1,50,256], ...},
    //             "style_dp":  {"data": [...], "dims": [1,8,16], ...}}.
    static VoiceStyle loadFromJson(const std::string& path);

    // Weighted average of two voice styles (for voice mixing), e.g.
    // blend(a, b, pctA/100.0, pctB/100.0).
    static VoiceStyle blend(const VoiceStyle& a, const VoiceStyle& b, float weightA, float weightB);
};

} // namespace tts
