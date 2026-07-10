#include "Phonemizer.h"

#include <mutex>
#include <stdexcept>

#include <espeak-ng/speak_lib.h>

namespace tts {

namespace {
bool g_espeakInitialized = false;

// espeak-ng keeps process-global state (current voice, TextToPhonemes
// iteration), so calls to phonemize()/setVoice() across all Phonemizer
// instances and threads must be serialized.
std::mutex g_espeakMutex;
}

Phonemizer::Phonemizer(const std::string& dataPath) {
    std::lock_guard<std::mutex> lock(g_espeakMutex);
    if (!g_espeakInitialized) {
        int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, dataPath.c_str(),
                                       espeakINITIALIZE_DONT_EXIT);
        if (rate <= 0) {
            throw std::runtime_error("espeak_Initialize failed (data path: " + dataPath + ")");
        }
        g_espeakInitialized = true;
    }
}

void Phonemizer::setVoice(const std::string& voice) {
    if (voice != currentVoice_) {
        if (espeak_SetVoiceByName(voice.c_str()) != EE_OK) {
            throw std::runtime_error("espeak-ng: unknown voice '" + voice + "'");
        }
        currentVoice_ = voice;
    }
}

std::string Phonemizer::phonemize(const std::string& text, const std::string& voice) {
    std::lock_guard<std::mutex> lock(g_espeakMutex);

    setVoice(voice);

    std::string result;
    const char* base = text.c_str();
    const void* textPtr = static_cast<const void*>(base);

    while (textPtr != nullptr) {
        const char* before = static_cast<const char*>(textPtr);
        const char* phonemes = espeak_TextToPhonemes(&textPtr, espeakCHARS_UTF8, espeakPHONEMES_IPA);

        if (phonemes && *phonemes) {
            if (!result.empty()) result += ' ';
            result += phonemes;
        }

        // espeak-ng silently consumes the punctuation that ended this clause.
        // Re-insert it (if it's basic sentence punctuation) so the downstream
        // tokenizer can use it as a prosody/pause hint.
        if (textPtr != nullptr) {
            const char* after = static_cast<const char*>(textPtr);
            for (const char* p = after - 1; p >= before; --p) {
                char c = *p;
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
                if (c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':') {
                    result += c;
                }
                break;
            }
        }
    }

    return result;
}

} // namespace tts
