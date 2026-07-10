#include "VoiceStyle.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace tts {

namespace {

// Recursively flattens a nested JSON array of floats into `out`, in
// row-major order (matching numpy's default C order, which is how the
// Python side serialized `style_ttl`/`style_dp`).
void flatten(const nlohmann::json& node, std::vector<float>& out) {
    if (node.is_array()) {
        for (const auto& child : node) {
            flatten(child, out);
        }
    } else {
        out.push_back(node.get<float>());
    }
}

} // namespace

VoiceStyle VoiceStyle::loadFromJson(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Voice style file not found: " + path);
    }
    nlohmann::json j;
    f >> j;

    VoiceStyle style;
    flatten(j.at("style_ttl").at("data"), style.styleTtl);
    flatten(j.at("style_dp").at("data"), style.styleDp);

    constexpr size_t kExpectedTtl = 1 * 50 * 256;
    constexpr size_t kExpectedDp = 1 * 8 * 16;
    if (style.styleTtl.size() != kExpectedTtl) {
        throw std::runtime_error("style_ttl in " + path + " has unexpected element count: " +
                                  std::to_string(style.styleTtl.size()));
    }
    if (style.styleDp.size() != kExpectedDp) {
        throw std::runtime_error("style_dp in " + path + " has unexpected element count: " +
                                  std::to_string(style.styleDp.size()));
    }
    return style;
}

VoiceStyle VoiceStyle::blend(const VoiceStyle& a, const VoiceStyle& b, float weightA, float weightB) {
    VoiceStyle out;
    out.styleTtl.resize(a.styleTtl.size());
    for (size_t i = 0; i < out.styleTtl.size(); ++i) {
        out.styleTtl[i] = a.styleTtl[i] * weightA + b.styleTtl[i] * weightB;
    }
    out.styleDp.resize(a.styleDp.size());
    for (size_t i = 0; i < out.styleDp.size(); ++i) {
        out.styleDp[i] = a.styleDp[i] * weightA + b.styleDp[i] * weightB;
    }
    return out;
}

} // namespace tts
