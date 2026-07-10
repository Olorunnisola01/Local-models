#include "WavReader.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <sstream>

namespace tts {

namespace {
uint32_t readU32(const char* p) {
    return static_cast<uint32_t>(static_cast<unsigned char>(p[0])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}
uint16_t readU16(const char* p) {
    return static_cast<uint16_t>(static_cast<unsigned char>(p[0]) | (static_cast<unsigned char>(p[1]) << 8));
}

bool readWavFromStream(std::istream& f, AudioBuffer* out) {
    char riff[4];
    f.read(riff, 4);
    if (!f || std::memcmp(riff, "RIFF", 4) != 0) return false;
    f.seekg(4, std::ios::cur); // overall size
    char wave[4];
    f.read(wave, 4);
    if (!f || std::memcmp(wave, "WAVE", 4) != 0) return false;

    // Determine the stream's total size so chunk sizes read from a
    // (possibly truncated/corrupted) response can be sanity-checked before
    // allocating buffers for them.
    const std::streampos dataStart = f.tellg();
    f.seekg(0, std::ios::end);
    const std::streamoff streamSize = f.tellg() - std::streampos(0);
    f.seekg(dataStart);

    uint16_t format = 0;
    uint16_t numChannels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<char> data;

    while (f) {
        char chunkId[4];
        f.read(chunkId, 4);
        if (!f) break;
        char sizeBuf[4];
        f.read(sizeBuf, 4);
        if (!f) break;
        uint32_t chunkSize = readU32(sizeBuf);

        // A chunk size larger than the remaining bytes in the stream means
        // the response is truncated/corrupted - bail out cleanly instead of
        // allocating a buffer for a bogus size.
        const std::streamoff remaining = streamSize - (f.tellg() - std::streampos(0));
        if (remaining < 0 || static_cast<uint64_t>(chunkSize) > static_cast<uint64_t>(remaining)) return false;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            std::vector<char> fmt(chunkSize);
            f.read(fmt.data(), chunkSize);
            if (!f || chunkSize < 16) return false;
            format = readU16(fmt.data() + 0);
            numChannels = readU16(fmt.data() + 2);
            sampleRate = readU32(fmt.data() + 4);
            bitsPerSample = readU16(fmt.data() + 14);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            data.resize(chunkSize);
            f.read(data.data(), chunkSize);
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }
        // Chunks are word-aligned; skip the pad byte for odd-sized chunks.
        if (chunkSize % 2 != 0) f.seekg(1, std::ios::cur);
    }

    if (numChannels == 0 || sampleRate == 0 || data.empty()) return false;

    const int bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample <= 0) return false;
    const size_t totalSamples = data.size() / static_cast<size_t>(bytesPerSample);
    const size_t numFrames = totalSamples / numChannels;

    std::vector<float> mono(numFrames, 0.0f);
    const char* p = data.data();

    for (size_t i = 0; i < numFrames; ++i) {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
            const char* sampPtr = p + (i * numChannels + ch) * static_cast<size_t>(bytesPerSample);
            float v = 0.0f;
            if (format == 3 && bitsPerSample == 32) {
                // IEEE float
                float f32;
                std::memcpy(&f32, sampPtr, 4);
                v = f32;
            } else if (format == 1 || format == 0xFFFE) {
                switch (bitsPerSample) {
                    case 8: {
                        const int8_t s = static_cast<int8_t>(sampPtr[0]) - 128; // unsigned 8-bit PCM
                        v = static_cast<float>(s) / 128.0f;
                        break;
                    }
                    case 16: {
                        int16_t s;
                        std::memcpy(&s, sampPtr, 2);
                        v = static_cast<float>(s) / 32768.0f;
                        break;
                    }
                    case 24: {
                        int32_t s = (static_cast<unsigned char>(sampPtr[0])) |
                                     (static_cast<unsigned char>(sampPtr[1]) << 8) |
                                     (static_cast<unsigned char>(sampPtr[2]) << 16);
                        if (s & 0x800000) s |= 0xFF000000; // sign-extend
                        v = static_cast<float>(s) / 8388608.0f;
                        break;
                    }
                    case 32: {
                        int32_t s;
                        std::memcpy(&s, sampPtr, 4);
                        v = static_cast<float>(s) / 2147483648.0f;
                        break;
                    }
                    default:
                        return false;
                }
            } else {
                return false;
            }
            sum += v;
        }
        mono[i] = sum / static_cast<float>(numChannels);
    }

    out->samples = std::move(mono);
    out->sampleRate = static_cast<int>(sampleRate);
    return true;
}

} // namespace

bool readWavToMono(const std::string& path, AudioBuffer* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    return readWavFromStream(f, out);
}

bool readWavFromMemory(const uint8_t* data, size_t size, AudioBuffer* out) {
    std::istringstream stream(std::string(reinterpret_cast<const char*>(data), size), std::ios::binary);
    return readWavFromStream(stream, out);
}

} // namespace tts
