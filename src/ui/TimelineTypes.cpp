#include "TimelineTypes.h"

QJsonObject TimelineSegment::toJson() const {
    QJsonObject obj;
    obj["label"] = QString::fromStdString(label);
    obj["text"] = QString::fromStdString(text);
    obj["sampleRate"] = sampleRate;
    obj["pauseAfterMs"] = pauseAfterMs;
    obj["trimStartMs"] = trimStartMs;
    obj["trimEndMs"] = trimEndMs;
    obj["fadeInMs"] = fadeInMs;
    obj["fadeOutMs"] = fadeOutMs;
    return obj;
}

TimelineSegment TimelineSegment::fromJson(const QJsonObject& obj) {
    TimelineSegment seg;
    seg.label = obj["label"].toString().toStdString();
    seg.text = obj["text"].toString().toStdString();
    if (seg.text.empty()) {
        seg.text = seg.label;
    }
    seg.sampleRate = obj["sampleRate"].toInt(44100);
    seg.pauseAfterMs = obj["pauseAfterMs"].toInt(0);
    seg.trimStartMs = obj["trimStartMs"].toInt(0);
    seg.trimEndMs = obj["trimEndMs"].toInt(0);
    seg.fadeInMs = obj["fadeInMs"].toInt(0);
    seg.fadeOutMs = obj["fadeOutMs"].toInt(0);
    return seg;
}

QJsonArray timelineToJson(const std::vector<TimelineSegment>& segments) {
    QJsonArray arr;
    for (const TimelineSegment& seg : segments) {
        arr.append(seg.toJson());
    }
    return arr;
}

std::vector<TimelineSegment> timelineFromJson(const QJsonArray& arr) {
    std::vector<TimelineSegment> segments;
    segments.reserve(arr.size());
    for (const QJsonValue& val : arr) {
        segments.push_back(TimelineSegment::fromJson(val.toObject()));
    }
    return segments;
}