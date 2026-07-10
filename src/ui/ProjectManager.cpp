#include "ProjectManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include "../dsp/WavReader.h"
#include "../dsp/WavWriter.h"

namespace {

QJsonArray eqToJson(const std::array<float, tts::GraphicEq::kNumBands>& gains) {
    QJsonArray arr;
    for (float g : gains) {
        arr.append(g);
    }
    return arr;
}

std::array<float, tts::GraphicEq::kNumBands> eqFromJson(const QJsonArray& arr) {
    std::array<float, tts::GraphicEq::kNumBands> gains{};
    for (int i = 0; i < tts::GraphicEq::kNumBands && i < arr.size(); ++i) {
        gains[i] = static_cast<float>(arr[i].toDouble());
    }
    return gains;
}

} // namespace

QJsonObject VoicePreset::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["provider"] = static_cast<int>(provider);
    obj["voiceAShort"] = voiceAShort;
    obj["voiceBShort"] = voiceBShort;
    obj["mixEnabled"] = mixEnabled;
    obj["pctA"] = pctA;
    obj["speed"] = speed;
    obj["eqGainsDb"] = eqToJson(eqGainsDb);
    return obj;
}

VoicePreset VoicePreset::fromJson(const QJsonObject& obj) {
    VoicePreset preset;
    preset.name = obj["name"].toString();
    preset.provider = static_cast<tts::Provider>(obj["provider"].toInt(0));
    preset.voiceAShort = obj["voiceAShort"].toString();
    preset.voiceBShort = obj["voiceBShort"].toString();
    preset.mixEnabled = obj["mixEnabled"].toBool(false);
    preset.pctA = obj["pctA"].toInt(50);
    preset.speed = static_cast<float>(obj["speed"].toDouble(1.05));
    preset.eqGainsDb = eqFromJson(obj["eqGainsDb"].toArray());
    return preset;
}

QString ProjectManager::segmentsDirForProject(const QString& projectPath) {
    const QFileInfo info(projectPath);
    return info.absolutePath() + "/" + info.completeBaseName() + "_segments";
}

bool ProjectManager::saveTimelineSegments(const QString& projectPath,
                                           const std::vector<TimelineSegment>& segments) {
    const QString dirPath = segmentsDirForProject(projectPath);
    QDir dir(dirPath);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    if (segments.empty()) {
        return true;
    }
    if (!dir.mkpath(".")) {
        return false;
    }

    for (size_t i = 0; i < segments.size(); ++i) {
        const TimelineSegment& seg = segments[i];
        if (seg.samples.empty()) {
            continue;
        }
        const QString wavPath =
            dirPath + QString("/%1.wav").arg(static_cast<int>(i), 4, 10, QChar('0'));
        if (!tts::writeWavPcm16(wavPath.toStdString(), seg.samples,
                                 static_cast<uint32_t>(seg.sampleRate))) {
            return false;
        }
    }
    return true;
}

bool ProjectManager::loadTimelineSegments(const QString& projectPath,
                                           std::vector<TimelineSegment>* segments, QString* error) {
    const QString dirPath = segmentsDirForProject(projectPath);
    QDir dir(dirPath);
    if (!dir.exists()) {
        return true;
    }

    const QStringList files =
        dir.entryList(QStringList() << "*.wav", QDir::Files, QDir::Name);
    for (const QString& file : files) {
        const int index = file.left(4).toInt();
        if (index < 0 || index >= static_cast<int>(segments->size())) {
            continue;
        }
        tts::AudioBuffer buf;
        if (!tts::readWavToMono((dirPath + "/" + file).toStdString(), &buf)) {
            if (error) {
                *error = "Failed to read segment audio: " + file;
            }
            return false;
        }
        (*segments)[static_cast<size_t>(index)].samples = std::move(buf.samples);
        (*segments)[static_cast<size_t>(index)].sampleRate = buf.sampleRate;
    }
    return true;
}

bool ProjectManager::saveProject(const QString& path, const ProjectData& data) {
    QJsonObject root;
    root["version"] = kProjectVersion;
    root["activeTab"] = data.activeTab;

    QJsonObject single;
    single["text"] = data.singleText;
    single["provider"] = data.singleProvider;
    single["voiceAShort"] = data.singleVoiceAShort;
    single["voiceBShort"] = data.singleVoiceBShort;
    single["mixEnabled"] = data.singleMixEnabled;
    single["pctA"] = data.singlePctA;
    single["speed"] = data.singleSpeed;
    single["eqGainsDb"] = eqToJson(data.singleEqGainsDb);
    single["humanizerEnabled"] = data.humanizerEnabled;
    root["singleSpeaker"] = single;

    QJsonObject multi;
    multi["script"] = data.multiScript;
    multi["speakerCount"] = data.multiSpeakerCount;
    multi["currentSpeaker"] = data.multiCurrentSpeaker;
    multi["pauseMs"] = data.multiPauseMs;
    multi["speakerSettings"] = data.multiSpeakerSettings;
    root["multiSpeaker"] = multi;

    QJsonObject settings;
    settings["useGpu"] = data.useGpu;
    settings["useRemoteKokoro"] = data.useRemoteKokoro;
    settings["remoteKokoroUrl"] = data.remoteKokoroUrl;
    settings["useRemoteFish"] = data.useRemoteFish;
    settings["remoteFishUrl"] = data.remoteFishUrl;
    settings["maxChunkChars"] = data.maxChunkChars;
    settings["sentenceGapMs"] = data.sentenceGapMs;
    settings["paragraphGapMs"] = data.paragraphGapMs;
    root["settings"] = settings;

    root["timeline"] = timelineToJson(data.timeline);
    root["pronunciationDictionary"] = data.pronunciationDictionary;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return saveTimelineSegments(path, data.timeline);
}

bool ProjectManager::loadProject(const QString& path, ProjectData* data, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = "Could not open project file.";
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = "Invalid project file format.";
        }
        return false;
    }

    const QJsonObject root = doc.object();
    if (root["version"].toInt(0) != kProjectVersion) {
        if (error) {
            *error = "Unsupported project version.";
        }
        return false;
    }

    data->activeTab = root["activeTab"].toInt(0);

    const QJsonObject single = root["singleSpeaker"].toObject();
    data->singleText = single["text"].toString();
    data->singleProvider = single["provider"].toInt(0);
    data->singleVoiceAShort = single["voiceAShort"].toString();
    data->singleVoiceBShort = single["voiceBShort"].toString();
    data->singleMixEnabled = single["mixEnabled"].toBool(false);
    data->singlePctA = single["pctA"].toInt(50);
    data->singleSpeed = static_cast<float>(single["speed"].toDouble(1.05));
    data->singleEqGainsDb = eqFromJson(single["eqGainsDb"].toArray());
    data->humanizerEnabled = single["humanizerEnabled"].toBool(false);

    const QJsonObject multi = root["multiSpeaker"].toObject();
    data->multiScript = multi["script"].toString();
    data->multiSpeakerCount = multi["speakerCount"].toInt(2);
    data->multiCurrentSpeaker = multi["currentSpeaker"].toInt(0);
    data->multiPauseMs = multi["pauseMs"].toInt(220);
    data->multiSpeakerSettings = multi["speakerSettings"].toArray();

    const QJsonObject settings = root["settings"].toObject();
    data->useGpu = settings["useGpu"].toBool(true);
    data->useRemoteKokoro = settings["useRemoteKokoro"].toBool(false);
    data->remoteKokoroUrl = settings["remoteKokoroUrl"].toString();
    data->useRemoteFish = settings["useRemoteFish"].toBool(false);
    data->remoteFishUrl = settings["remoteFishUrl"].toString();
    data->maxChunkChars = settings["maxChunkChars"].toInt(400);
    data->sentenceGapMs = settings["sentenceGapMs"].toInt(150);
    data->paragraphGapMs = settings["paragraphGapMs"].toInt(600);

    data->timeline = timelineFromJson(root["timeline"].toArray());
    data->pronunciationDictionary = root["pronunciationDictionary"].toArray();
    return loadTimelineSegments(path, &data->timeline, error);
}

std::vector<VoicePreset> ProjectManager::loadVoicePresets() {
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    const QJsonDocument doc =
        QJsonDocument::fromJson(settings.value("voicePresets").toString().toUtf8());
    std::vector<VoicePreset> presets;
    if (!doc.isArray()) {
        return presets;
    }
    for (const QJsonValue& val : doc.array()) {
        presets.push_back(VoicePreset::fromJson(val.toObject()));
    }
    return presets;
}

void ProjectManager::saveVoicePresets(const std::vector<VoicePreset>& presets) {
    QJsonArray arr;
    for (const VoicePreset& preset : presets) {
        arr.append(preset.toJson());
    }
    QSettings settings("EdgeTTS-Studio", "EdgeTTS-Studio");
    settings.setValue("voicePresets", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}