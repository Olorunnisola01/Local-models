#include "AudioTimelineBuilder.h"

#include <algorithm>
#include <cmath>

#include "../dsp/Resampler.h"

namespace timeline {

int msToSamples(int ms, int sampleRate) {
    return static_cast<int>(sampleRate * (ms / 1000.0));
}

std::vector<float> extractTrimmed(const TimelineSegment& segment) {
    const int sr = segment.sampleRate;
    const int total = static_cast<int>(segment.samples.size());
    int start = msToSamples(segment.trimStartMs, sr);
    int end = total;
    if (segment.trimEndMs > 0) {
        end = std::max(start, total - msToSamples(segment.trimEndMs, sr));
    }
    start = std::clamp(start, 0, total);
    end = std::clamp(end, start, total);
    return std::vector<float>(segment.samples.begin() + start, segment.samples.begin() + end);
}

void applyFade(std::vector<float>& samples, int sampleRate, int fadeInMs, int fadeOutMs) {
    const int fadeInSamples = msToSamples(fadeInMs, sampleRate);
    const int fadeOutSamples = msToSamples(fadeOutMs, sampleRate);
    const int n = static_cast<int>(samples.size());

    if (fadeInSamples > 0) {
        const int len = std::min(fadeInSamples, n);
        for (int i = 0; i < len; ++i) {
            samples[i] *= static_cast<float>(i + 1) / static_cast<float>(len);
        }
    }
    if (fadeOutSamples > 0) {
        const int len = std::min(fadeOutSamples, n);
        for (int i = 0; i < len; ++i) {
            const int idx = n - 1 - i;
            samples[idx] *= static_cast<float>(i + 1) / static_cast<float>(len);
        }
    }
}

tts::AudioBuffer buildFromTimeline(const std::vector<TimelineSegment>& segments) {
    constexpr int kTargetRate = 44100;
    std::vector<float> out;

    for (const TimelineSegment& seg : segments) {
        std::vector<float> chunk = extractTrimmed(seg);
        if (chunk.empty()) {
            continue;
        }
        if (seg.sampleRate != kTargetRate) {
            chunk = tts::resampleLinear(chunk, seg.sampleRate, kTargetRate);
        }
        applyFade(chunk, kTargetRate, seg.fadeInMs, seg.fadeOutMs);
        out.insert(out.end(), chunk.begin(), chunk.end());

        if (seg.pauseAfterMs > 0) {
            const int pauseSamples = msToSamples(seg.pauseAfterMs, kTargetRate);
            out.insert(out.end(), static_cast<size_t>(pauseSamples), 0.0f);
        }
    }

    return tts::AudioBuffer{std::move(out), kTargetRate};
}

std::vector<float> computeWaveformPeaks(const std::vector<float>& samples, int targetPoints) {
    if (samples.empty() || targetPoints <= 0) {
        return {};
    }
    const int block = std::max(1, static_cast<int>(samples.size()) / targetPoints);
    std::vector<float> peaks;
    peaks.reserve(static_cast<size_t>(targetPoints));

    for (int i = 0; i < targetPoints; ++i) {
        const int start = i * block;
        const int end = std::min(static_cast<int>(samples.size()), start + block);
        float peak = 0.0f;
        for (int j = start; j < end; ++j) {
            peak = std::max(peak, std::abs(samples[j]));
        }
        peaks.push_back(peak);
    }
    return peaks;
}

} // namespace timeline