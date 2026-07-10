#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace tts {

namespace {
void writeU32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void writeU16(std::ofstream& f, uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); }
constexpr size_t kProgressChunkFrames = 65536;
} // namespace

bool writeWavPcm16(const std::string& path, const std::vector<float>& samples, uint32_t sampleRate) {
    return writeWavPcm16(path, samples, sampleRate, [](int) {});
}

bool writeWavPcm16(const std::string& path, const std::vector<float>& samples, uint32_t sampleRate,
                    const std::function<void(int)>& onProgress) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }

    const uint16_t numChannels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    const uint16_t blockAlign = numChannels * bitsPerSample / 8;
    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));

    // RIFF header
    f.write("RIFF", 4);
    writeU32(f, 36 + dataSize);
    f.write("WAVE", 4);

    // fmt chunk
    f.write("fmt ", 4);
    writeU32(f, 16); // PCM fmt chunk size
    writeU16(f, 1);  // PCM format
    writeU16(f, numChannels);
    writeU32(f, sampleRate);
    writeU32(f, byteRate);
    writeU16(f, blockAlign);
    writeU16(f, bitsPerSample);

    // data chunk
    f.write("data", 4);
    writeU32(f, dataSize);

    std::vector<int16_t> pcmChunk;
    pcmChunk.reserve(std::min(samples.size(), kProgressChunkFrames));

    for (size_t pos = 0; pos < samples.size(); pos += kProgressChunkFrames) {
        const size_t end = std::min(samples.size(), pos + kProgressChunkFrames);
        pcmChunk.clear();
        for (size_t i = pos; i < end; ++i) {
            float clamped = std::clamp(samples[i], -1.0f, 1.0f);
            pcmChunk.push_back(static_cast<int16_t>(std::lround(clamped * 32767.0f)));
        }
        f.write(reinterpret_cast<const char*>(pcmChunk.data()),
                static_cast<std::streamsize>(pcmChunk.size() * sizeof(int16_t)));

        const int percent = samples.empty() ? 100 : static_cast<int>(end * 100 / samples.size());
        onProgress(percent);
    }
    if (samples.empty()) {
        onProgress(100);
    }

    return true;
}

} // namespace tts
