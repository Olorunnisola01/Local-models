#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../core/TtsTypes.h"

namespace tts {

// Reads a canonical PCM WAV file (8/16/24/32-bit integer or 32-bit float,
// mono or multi-channel — channels are averaged down to mono) into a
// normalized [-1, 1] float buffer. Returns false (and leaves `out`
// unmodified) if the file can't be opened or isn't a recognized WAV.
bool readWavToMono(const std::string& path, AudioBuffer* out);

// Same as readWavToMono, but parses a WAV file already held in memory (e.g.
// an HTTP response body) instead of opening a path.
bool readWavFromMemory(const uint8_t* data, size_t size, AudioBuffer* out);

} // namespace tts
