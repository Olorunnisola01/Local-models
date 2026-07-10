#pragma once

#include <vector>

#include "../core/TtsTypes.h"
#include "TimelineTypes.h"

namespace timeline {

// Converts milliseconds to sample count at the given rate.
int msToSamples(int ms, int sampleRate);

// Returns a trimmed copy of the segment's samples (does not apply fade).
std::vector<float> extractTrimmed(const TimelineSegment& segment);

// Applies linear fade-in/out in-place.
void applyFade(std::vector<float>& samples, int sampleRate, int fadeInMs, int fadeOutMs);

// Concatenates all timeline segments (trim, fade, pause) into one buffer at 44100 Hz.
tts::AudioBuffer buildFromTimeline(const std::vector<TimelineSegment>& segments);

// Downsamples peaks for waveform display (values in [0, 1]).
std::vector<float> computeWaveformPeaks(const std::vector<float>& samples, int targetPoints);

} // namespace timeline