#include "CaptionExporter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "AudioTimelineBuilder.h"
#include "../dsp/Resampler.h"
#include "../dsp/WavWriter.h"

namespace captions {
namespace {

QString segmentDisplayText(const TimelineSegment& seg) {
    if (!seg.text.empty()) {
        return QString::fromStdString(seg.text);
    }
    return QString::fromStdString(seg.label);
}

} // namespace

std::vector<CaptionCue> cuesFromTimeline(const std::vector<TimelineSegment>& segments) {
    constexpr int kTargetRate = 44100;
    std::vector<CaptionCue> cues;
    int cursorMs = 0;

    for (size_t i = 0; i < segments.size(); ++i) {
        const TimelineSegment& seg = segments[i];
        std::vector<float> chunk = timeline::extractTrimmed(seg);
        if (seg.sampleRate != kTargetRate && !chunk.empty()) {
            chunk = tts::resampleLinear(chunk, seg.sampleRate, kTargetRate);
        }
        if (chunk.empty()) {
            cursorMs += seg.pauseAfterMs;
            continue;
        }

        const int durationMs =
            static_cast<int>(static_cast<double>(chunk.size()) * 1000.0 / static_cast<double>(kTargetRate));

        CaptionCue cue;
        cue.index = static_cast<int>(i) + 1;
        cue.startMs = cursorMs;
        cue.endMs = cursorMs + durationMs;
        cue.text = segmentDisplayText(seg);
        cues.push_back(cue);

        cursorMs = cue.endMs + seg.pauseAfterMs;
    }
    return cues;
}

QString formatSrtTime(int ms) {
    const int hours = ms / 3600000;
    ms %= 3600000;
    const int minutes = ms / 60000;
    ms %= 60000;
    const int seconds = ms / 1000;
    const int millis = ms % 1000;
    return QString("%1:%2:%3,%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

QString formatVttTime(int ms) {
    const int hours = ms / 3600000;
    ms %= 3600000;
    const int minutes = ms / 60000;
    ms %= 60000;
    const int seconds = ms / 1000;
    const int millis = ms % 1000;
    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

QString toSrt(const std::vector<CaptionCue>& cues) {
    QString out;
    for (const CaptionCue& cue : cues) {
        if (cue.text.trimmed().isEmpty()) {
            continue;
        }
        out += QString::number(cue.index) + "\n";
        out += formatSrtTime(cue.startMs) + " --> " + formatSrtTime(cue.endMs) + "\n";
        out += cue.text + "\n\n";
    }
    return out;
}

QString toVtt(const std::vector<CaptionCue>& cues) {
    QString out = "WEBVTT\n\n";
    for (const CaptionCue& cue : cues) {
        if (cue.text.trimmed().isEmpty()) {
            continue;
        }
        out += formatVttTime(cue.startMs) + " --> " + formatVttTime(cue.endMs) + "\n";
        out += cue.text + "\n\n";
    }
    return out;
}

ExportPackageResult exportPackage(const QString& folder, const std::vector<TimelineSegment>& segments,
                                   const std::vector<float>& processedAudio, int sampleRate,
                                   const ExportPackageOptions& options) {
    ExportPackageResult result;
    if (folder.isEmpty()) {
        result.message = "No export folder selected.";
        return result;
    }

    QDir dir(folder);
    if (!dir.exists() && !dir.mkpath(".")) {
        result.message = "Could not create export folder.";
        return result;
    }

    const QString segmentsDir = folder + "/segments";
    if (options.segmentWavs) {
        QDir().mkpath(segmentsDir);
    }

    int written = 0;

    if (options.combinedWav && !processedAudio.empty() && sampleRate > 0) {
        const QString path = folder + "/combined.wav";
        if (tts::writeWavPcm16(path.toStdString(), processedAudio, static_cast<uint32_t>(sampleRate))) {
            ++written;
        }
    }

    if (options.segmentWavs) {
        for (size_t i = 0; i < segments.size(); ++i) {
            const TimelineSegment& seg = segments[i];
            if (seg.samples.empty()) {
                continue;
            }
            const QString path =
                segmentsDir + QString("/%1.wav").arg(static_cast<int>(i) + 1, 3, 10, QChar('0'));
            if (tts::writeWavPcm16(path.toStdString(), seg.samples, static_cast<uint32_t>(seg.sampleRate))) {
                ++written;
            }
        }
    }

    const std::vector<CaptionCue> cues = cuesFromTimeline(segments);

    if (options.srt) {
        QFile file(folder + "/captions.srt");
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(toSrt(cues).toUtf8());
            ++written;
        }
    }

    if (options.vtt) {
        QFile file(folder + "/captions.vtt");
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(toVtt(cues).toUtf8());
            ++written;
        }
    }

    if (options.manifest) {
        QJsonObject root;
        root["sampleRate"] = sampleRate;
        QJsonArray cueArr;
        for (const CaptionCue& cue : cues) {
            QJsonObject obj;
            obj["index"] = cue.index;
            obj["startMs"] = cue.startMs;
            obj["endMs"] = cue.endMs;
            obj["text"] = cue.text;
            cueArr.append(obj);
        }
        root["captions"] = cueArr;

        QFile file(folder + "/manifest.json");
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            ++written;
        }
    }

    result.success = written > 0;
    result.filesWritten = written;
    result.message =
        result.success
            ? QString("Exported %1 file(s) to %2").arg(written).arg(folder)
            : QString("Nothing was exported to %1").arg(folder);
    return result;
}

} // namespace captions