#include "ProviderValidation.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace tts {
namespace {

bool fileExists(const std::string& path) { return fs::exists(path) && fs::is_regular_file(path); }
bool dirExists(const std::string& path) { return fs::exists(path) && fs::is_directory(path); }

ProviderValidationResult fail(std::string message) {
    return {false, std::move(message)};
}

} // namespace

ProviderValidationResult validateProviderReady(Provider provider, const std::string& appDir,
                                                const VoiceEntry* voiceA, bool remoteKokoroEnabled,
                                                const std::string& remoteKokoroUrl, bool remoteFishEnabled,
                                                const std::string& remoteFishUrl) {
    switch (provider) {
        case Provider::Supertonic: {
            const std::string onnxDir = appDir + "/models/supertonic/onnx";
            if (!fileExists(onnxDir + "/vector_estimator.onnx")) {
                return fail("Supertonic models are missing. Use Tools → Model Manager to sync models.");
            }
            if (voiceA && !fileExists(appDir + "/models/supertonic/voice_styles/" + voiceA->shortName + ".json")) {
                return fail("Supertonic voice style not found: " + voiceA->shortName);
            }
            break;
        }
        case Provider::Kokoro: {
            if (remoteKokoroEnabled) {
                if (remoteKokoroUrl.empty()) {
                    return fail("Remote Kokoro is enabled but no tunnel URL is set in the Remote GPU panel.");
                }
                break;
            }
            if (!fileExists(appDir + "/espeak-ng-data/phontab")) {
                return fail("espeak-ng data is missing next to the application (espeak-ng-data/).");
            }
            if (!voiceA) {
                break;
            }
            const bool isMartin = voiceA->shortName == "martin";
            const bool isVictoria = voiceA->shortName == "victoria";
            const std::string modelDir = isMartin    ? appDir + "/models/kokoro_de_martin"
                                         : isVictoria ? appDir + "/models/kokoro_de_victoria"
                                                      : appDir + "/models/kokoro";
            const std::string modelFile = isMartin    ? "kokoro-martin.onnx"
                                          : isVictoria ? "kokoro-victoria.onnx"
                                                       : "kokoro-v1.0.onnx";
            if (!fileExists(modelDir + "/" + modelFile)) {
                return fail("Kokoro model not found: " + modelDir + "/" + modelFile);
            }
            if (!fileExists(modelDir + "/vocab.json")) {
                return fail("Kokoro vocab.json not found in " + modelDir);
            }
            if (!fileExists(modelDir + "/voices/" + voiceA->shortName + ".bin")) {
                return fail("Kokoro voice file not found: " + voiceA->shortName + ".bin");
            }
            break;
        }
        case Provider::Piper: {
            if (!fileExists(appDir + "/espeak-ng-data/phontab")) {
                return fail("espeak-ng data is missing next to the application (espeak-ng-data/).");
            }
            if (!voiceA) {
                break;
            }
            const std::string stem = voiceA->shortName;
            const std::string onnx = appDir + "/models/piper/" + stem + ".onnx";
            const std::string json = onnx + ".json";
            if (!fileExists(onnx)) {
                return fail("Piper model not found: " + onnx);
            }
            if (!fileExists(json)) {
                return fail("Piper config not found: " + json);
            }
            break;
        }
        case Provider::EdgeTts:
            break;
        case Provider::FishSpeech: {
            if (!remoteFishEnabled || remoteFishUrl.empty()) {
                return fail("Fish Audio S2 requires a Kaggle server. Enable it in the Remote GPU panel and enter the tunnel URL.");
            }
            break;
        }
    }
    return {};
}

} // namespace tts