#include "AudioExporter.h"

#include "WavWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>

#include <algorithm>
#include <cctype>

namespace tts {
namespace {

QString findFfmpeg() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/ffmpeg.exe",
        appDir + "/tools/ffmpeg.exe",
        "ffmpeg.exe",
        "ffmpeg",
    };
    for (const QString& c : candidates) {
        if (c.contains('/') || c.contains('\\')) {
            if (QFileInfo::exists(c)) {
                return c;
            }
        } else {
            QProcess which;
            which.start("where", {c});
            if (which.waitForFinished(3000) && which.exitCode() == 0) {
                const QString out = QString::fromLocal8Bit(which.readAllStandardOutput()).trimmed();
                if (!out.isEmpty()) {
                    return out.split('\n').first().trimmed();
                }
            }
        }
    }
    for (const QString& c : candidates) {
        if ((c.contains('/') || c.contains('\\')) && QFileInfo::exists(c)) {
            return c;
        }
    }
    return {};
}

bool runFfmpeg(const QString& ffmpeg, const QString& inputWav, const QString& outputPath,
               const QStringList& extraArgs, const std::function<void(int)>& onProgress) {
    QStringList args = {"-y", "-i", inputWav};
    args << extraArgs << outputPath;
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(5000)) {
        return false;
    }
    int lastPct = 0;
    while (proc.state() != QProcess::NotRunning) {
        proc.waitForReadyRead(200);
        onProgress(std::min(95, lastPct + 2));
        lastPct = std::min(95, lastPct + 2);
    }
    proc.waitForFinished(-1);
    onProgress(100);
    return proc.exitCode() == 0;
}

std::string lowerExt(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return {};
    }
    std::string ext = path.substr(pos + 1);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

} // namespace

AudioExportFormat formatFromPath(const std::string& path) {
    const std::string ext = lowerExt(path);
    if (ext == "mp3") {
        return AudioExportFormat::Mp3;
    }
    if (ext == "flac") {
        return AudioExportFormat::Flac;
    }
    return AudioExportFormat::Wav;
}

QStringList supportedExportFilters() {
    return {"WAV Files (*.wav)", "MP3 Files (*.mp3)", "FLAC Files (*.flac)",
            "All Supported (*.wav *.mp3 *.flac)"};
}

bool exportAudio(const std::string& path, AudioExportFormat format, const std::vector<float>& samples,
                 uint32_t sampleRate, const std::function<void(int percent)>& onProgress) {
    if (format == AudioExportFormat::Wav) {
        return writeWavPcm16(path, samples, sampleRate, onProgress);
    }

    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        return false;
    }

    QTemporaryFile tempWav(QDir::temp().filePath("edgetts_export_XXXXXX.wav"));
    tempWav.setAutoRemove(true);
    if (!tempWav.open()) {
        return false;
    }
    tempWav.close();
    const QString wavPath = tempWav.fileName();
    if (!writeWavPcm16(wavPath.toStdString(), samples, sampleRate, [&](int p) { onProgress(p / 2); })) {
        return false;
    }

    const QString outPath = QString::fromStdString(path);
    if (format == AudioExportFormat::Mp3) {
        return runFfmpeg(ffmpeg, wavPath, outPath, {"-codec:a", "libmp3lame", "-qscale:a", "2"}, onProgress);
    }
    return runFfmpeg(ffmpeg, wavPath, outPath, {"-codec:a", "flac"}, onProgress);
}

} // namespace tts