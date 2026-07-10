#pragma once

#include <string>

#include "VoiceCatalog.h"

namespace tts {

struct ProviderValidationResult {
    bool ok = true;
    std::string message;
};

// Checks that models, remote URLs, and voice assets are present before synthesis.
ProviderValidationResult validateProviderReady(
    Provider provider,
    const std::string& appDir,
    const VoiceEntry* voiceA = nullptr,
    bool remoteKokoroEnabled = false,
    const std::string& remoteKokoroUrl = {},
    bool remoteFishEnabled = false,
    const std::string& remoteFishUrl = {});

} // namespace tts