#pragma once

#include <string>

#include "TtsTypes.h"

namespace tts {

// HTTP client for a remote Fish Audio S2 Pro TTS server (fishaudio/s2-pro
// weights, running via fish_speech.api_server on a Kaggle T4 x2 GPU, exposed
// through a cloudflared quick tunnel).
//
// Two synthesis modes:
//   1. Random voice — refAudioB64 is empty; the model generates a random style.
//   2. Voice cloning — refAudioB64 is the base64-encoded bytes of a WAV file
//      containing 10-30 s of the target speaker.  An optional refText
//      (transcript of the reference audio) improves cloning accuracy.
//
// Fish Audio S2 inline emotion tags ([laugh], [whisper], [cry], [happy],
// [sad], etc.) may be embedded directly in the synthesis text.
//
// Each call opens its own QNetworkAccessManager and blocks on a local
// QEventLoop until the HTTP response arrives — safe to call from a
// QtConcurrent background thread, including concurrently from multiple
// chunk-synthesis threads.
class RemoteFishSpeechEngine {
public:
    explicit RemoteFishSpeechEngine(std::string baseUrl);

    // Synthesizes `text` (may contain Fish Audio emotion tags like [laugh]).
    // refAudioB64: base64-encoded WAV bytes of a reference speaker (empty = random voice).
    // refText:     optional transcript of the reference audio (improves cloning quality).
    // speed:       0.5-2.0, passed as the Fish Audio "speed" param.
    // Throws std::runtime_error on network/server error or invalid WAV response.
    AudioBuffer synthesize(const std::string& text,
                            const std::string& refAudioB64,
                            const std::string& refText,
                            float speed) const;

    // Uses pre-extracted VQ token JSON (from extractTokens) instead of re-uploading WAV.
    AudioBuffer synthesizeWithReferenceJson(const std::string& text, const std::string& referenceJson,
                                             float speed) const;

    // Calls POST /v1/models/vqgan/encode to extract VQ tokens from the reference
    // audio.  Returns the raw JSON string from the server (caller saves to disk).
    // Throws std::runtime_error on network/server error.
    std::string extractTokens(const std::string& refAudioB64) const;

    // POSTs /shutdown so the remote server process exits and frees the Kaggle
    // GPU session.  Returns true if the request completed without a network error.
    static bool stopSession(const std::string& baseUrl);

private:
    std::string baseUrl_; // trailing '/' stripped
};

} // namespace tts
