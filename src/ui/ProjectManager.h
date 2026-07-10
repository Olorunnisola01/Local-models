#pragma once

#include <array>
#include <string>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "../core/VoiceCatalog.h"
#include "../dsp/GraphicEq.h"
#include "TimelineTypes.h"

// Serializable single-speaker voice preset (provider + voices + mix + speed + EQ).
struct VoicePreset {
    QString name;
    tts::Provider provider = tts::Provider::Supertonic;
    QString voiceAShort;
    QString voiceBShort;
    bool mixEnabled = false;
    int pctA = 50;
    float speed = 1.05f;
    std::array<float, tts::GraphicEq::kNumBands> eqGainsDb{};

    QJsonObject toJson() const;
    static VoicePreset fromJson(const QJsonObject& obj);
};

// Full project snapshot: both tabs, global settings, and timeline metadata.
struct ProjectData {
    int activeTab = 0;
    QString singleText;
    int singleProvider = 0;
    QString singleVoiceAShort;
    QString singleVoiceBShort;
    bool singleMixEnabled = false;
    int singlePctA = 50;
    float singleSpeed = 1.05f;
    std::array<float, tts::GraphicEq::kNumBands> singleEqGainsDb{};
    bool humanizerEnabled = false;

    QString multiScript;
    int multiSpeakerCount = 2;
    int multiCurrentSpeaker = 0;
    int multiPauseMs = 220;
    QJsonArray multiSpeakerSettings;

    bool useGpu = true;
    bool useRemoteKokoro = false;
    QString remoteKokoroUrl;
    bool useRemoteFish = false;
    QString remoteFishUrl;
    int maxChunkChars = 400;
    int sentenceGapMs = 150;
    int paragraphGapMs = 600;

    std::vector<TimelineSegment> timeline;
    QJsonArray pronunciationDictionary;
};

class ProjectManager {
public:
    static constexpr int kProjectVersion = 1;

    static bool saveProject(const QString& path, const ProjectData& data);
    static bool loadProject(const QString& path, ProjectData* data, QString* error);

    static bool saveTimelineSegments(const QString& projectPath, const std::vector<TimelineSegment>& segments);
    static bool loadTimelineSegments(const QString& projectPath, std::vector<TimelineSegment>* segments,
                                     QString* error);

    static QString segmentsDirForProject(const QString& projectPath);

    static std::vector<VoicePreset> loadVoicePresets();
    static void saveVoicePresets(const std::vector<VoicePreset>& presets);
};