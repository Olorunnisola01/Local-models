#pragma once

#include <QStringList>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tts {

enum class AudioExportFormat { Wav, Mp3, Flac };

// Writes mono float audio. WAV is native; MP3/FLAC use ffmpeg when available.
bool exportAudio(const std::string& path, AudioExportFormat format,
                 const std::vector<float>& samples, uint32_t sampleRate,
                 const std::function<void(int percent)>& onProgress = [](int) {});

QStringList supportedExportFilters();

AudioExportFormat formatFromPath(const std::string& path);

} // namespace tts