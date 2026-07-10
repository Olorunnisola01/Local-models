#pragma once

#include <string>

namespace tts {

// Thin wrapper around the espeak-ng C API (espeak_Initialize /
// espeak_TextToPhonemes) used to convert text into IPA phoneme strings for
// the Kokoro and Piper engines, which both expect espeak-ng-style IPA input.
//
// espeak-ng has process-global state, so Initialize is only performed once
// across all Phonemizer instances (guarded by a static flag); SetVoiceByName
// is called lazily whenever the requested voice changes.
class Phonemizer {
public:
    // dataPath: directory containing the "espeak-ng-data" folder (i.e. the
    // parent of espeak-ng-data, matching the `path` argument documented for
    // espeak_Initialize).
    explicit Phonemizer(const std::string& dataPath);

    // Converts `text` to an IPA phoneme string for the given espeak-ng voice
    // (e.g. "en-us", "en-gb", "de"). Stress marks (ˈ ˌ) are preserved by
    // espeak's IPA output; basic sentence punctuation (. , ! ? ; :) that
    // espeak-ng consumes as clause separators is re-inserted so downstream
    // tokenizers can use it as prosody/pause hints. Clauses are joined by a
    // single space.
    std::string phonemize(const std::string& text, const std::string& voice);

private:
    void setVoice(const std::string& voice);

    std::string currentVoice_;
};

} // namespace tts
