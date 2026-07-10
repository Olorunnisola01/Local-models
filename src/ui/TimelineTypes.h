#pragma once

#include <string>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "../core/TtsTypes.h"

// One editable block on the waveform timeline: raw segment audio plus timing
// controls applied when the timeline is rebuilt into a single buffer.
struct TimelineSegment {
    std::string label;
    std::string text; // full speakable text used for captions
    std::vector<float> samples;
    int sampleRate = 44100;
    int pauseAfterMs = 0;
    int trimStartMs = 0;
    int trimEndMs = 0; // ms trimmed from the end; 0 = no end trim
    int fadeInMs = 0;
    int fadeOutMs = 0;

    QJsonObject toJson() const;
    static TimelineSegment fromJson(const QJsonObject& obj);
};

struct SynthOutput {
    tts::AudioBuffer audio;
    std::vector<TimelineSegment> timeline;
};

QJsonArray timelineToJson(const std::vector<TimelineSegment>& segments);
std::vector<TimelineSegment> timelineFromJson(const QJsonArray& arr);