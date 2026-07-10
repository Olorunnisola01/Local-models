#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tts {

// Writes a canonical 44-byte-header mono 16-bit PCM WAV file. Each float
// sample is clamped to [-1, 1] and converted with round(x * 32767).
// Returns false if the file could not be opened for writing.
bool writeWavPcm16(const std::string& path, const std::vector<float>& samples, uint32_t sampleRate);

// Same as above, but writes the sample data in chunks and calls
// `onProgress(percent)` after each chunk so callers can drive a progress bar.
bool writeWavPcm16(const std::string& path, const std::vector<float>& samples, uint32_t sampleRate,
                    const std::function<void(int)>& onProgress);

} // namespace tts
