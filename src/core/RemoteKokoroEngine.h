#pragma once

#include <string>

#include "TtsTypes.h"

namespace tts {

// Client for a remote Kokoro TTS server (e.g. a Kaggle notebook running the
// kokoro-onnx pip package on a CUDA GPU, exposed via a cloudflared quick
// tunnel). Only the main English Kokoro voices (kokoro-v1.0.onnx) are
// supported by the server side; the German "martin"/"victoria" variants and
// Supertonic/Piper remain local-only.
//
// Each call opens its own QNetworkAccessManager and blocks on a local
// QEventLoop until the HTTP response arrives, mirroring EdgeTtsEngine's
// pattern - safe to call from a QtConcurrent background thread, including
// concurrently from multiple chunk-synthesis threads.
class RemoteKokoroEngine {
public:
    explicit RemoteKokoroEngine(std::string baseUrl);

    // voiceShortName/lang match VoiceEntry::shortName/espeakLang (e.g.
    // "af_heart", "en-us"). Throws std::runtime_error on network/server error
    // or if the response isn't a valid WAV file.
    AudioBuffer synthesize(const std::string& text, const std::string& voiceShortName,
                            const std::string& lang, float speed) const;

    // Two-voice blend; pctA/100 is the weight on voiceAShortName. The server
    // blends the voice style tables itself.
    AudioBuffer synthesizeMixed(const std::string& text, const std::string& voiceAShortName,
                                 const std::string& voiceBShortName, int pctA, const std::string& lang,
                                 float speed) const;

    // POSTs /shutdown so the remote server process exits, freeing the Kaggle
    // GPU session. Returns true if the request completed without a network
    // error (the server may not get to send a body before exiting).
    static bool stopSession(const std::string& baseUrl);

private:
    std::string baseUrl_; // trailing '/' stripped
};

} // namespace tts
