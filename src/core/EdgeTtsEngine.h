#pragma once

#include <atomic>
#include <string>

#include "TtsTypes.h"

namespace tts {

// Microsoft Edge neural TTS (cloud, requires internet access).
//
// Implements the speech.platform.bing.com "readaloud" websocket protocol
// directly on top of QSslSocket (manual HTTP Upgrade handshake + RFC6455
// framing, since Qt6WebSockets is not available in this build). The
// returned audio-24khz-48kbitrate-mono-mp3 stream is decoded to PCM via
// QAudioDecoder (Windows Media Foundation backend).
class EdgeTtsEngine {
public:
    EdgeTtsEngine() = default;

    // voiceShortName: an Edge voice name, e.g. "en-US-AvaMultilingualNeural".
    // speed: 1.0 = normal; mapped to SSML <prosody rate='+N%'>.
    // humanize: when true, splits the text into sentences and gives each one
    // its own <prosody> span with a small randomized rate/pitch offset, so
    // the output sounds less mechanically uniform.
    // Throws std::runtime_error on connection/protocol/decode failure.
    // cancelFlag: when non-null and set to true, aborts the active request promptly.
    AudioBuffer synthesize(const std::string& text, const std::string& voiceShortName, float speed,
                            bool humanize = false, int streamTimeoutMs = 60000,
                            const std::atomic<bool>* cancelFlag = nullptr) const;

    struct ConnectionTestResult {
        bool ok = false;
        std::string message;
        double latencyMs = 0.0;
    };

    // Quick connectivity check: synthesizes a one-word sample.
    ConnectionTestResult testConnection(const std::string& voiceShortName = "en-US-AvaMultilingualNeural") const;
};

} // namespace tts
