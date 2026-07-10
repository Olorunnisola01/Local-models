#pragma once

#include <string>
#include <vector>

#include <QString>

#include "TimelineTypes.h"

struct CaptionCue {
    int index = 0;
    int startMs = 0;
    int endMs = 0;
    QString text;
};

namespace captions {

std::vector<CaptionCue> cuesFromTimeline(const std::vector<TimelineSegment>& segments);

QString formatSrtTime(int ms);
QString formatVttTime(int ms);

QString toSrt(const std::vector<CaptionCue>& cues);
QString toVtt(const std::vector<CaptionCue>& cues);

struct ExportPackageOptions {
    bool combinedWav = true;
    bool segmentWavs = true;
    bool srt = true;
    bool vtt = true;
    bool manifest = true;
};

struct ExportPackageResult {
    bool success = false;
    QString message;
    int filesWritten = 0;
};

ExportPackageResult exportPackage(const QString& folder, const std::vector<TimelineSegment>& segments,
                                   const std::vector<float>& processedAudio, int sampleRate,
                                   const ExportPackageOptions& options);

} // namespace captions