#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "Phonemizer.h"
#include "TtsTypes.h"

namespace tts {

// Piper (VITS) ONNX engine for a single voice (e.g. de_DE-thorsten-high).
//
// Tokenization mirrors Piper's phonemes_to_ids: phonemize via espeak-ng
// (IPA), then for each phoneme codepoint found in phoneme_id_map append its
// id followed by the PAD id ("_"), wrapped in BOS ("^") ... EOS ("$").
// ONNX I/O: {input int64[1,L], input_lengths int64[1], scales float32[3]}
// -> output float32[1,1,1,-1].
class PiperEngine {
public:
    // onnxPath / jsonPath: e.g. models/piper/de_DE-thorsten-high.onnx[.json]
    PiperEngine(const std::string& onnxPath, const std::string& jsonPath);

    AudioBuffer synthesize(Phonemizer& phonemizer, const std::string& text, float speed) const;

    int sampleRate() const { return sampleRate_; }
    const std::string& espeakVoice() const { return espeakVoice_; }

    // True if a DirectML GPU execution provider was successfully attached
    // (see GpuDevice::selectBestDmlDevice); false if running on CPU.
    bool usingGpu() const { return usingGpu_; }

    // Name of the GPU adapter used when usingGpu() is true, empty otherwise.
    const std::string& gpuName() const { return gpuName_; }

private:
    struct NamedSession {
        Ort::Session session{nullptr};
        std::vector<std::string> inputNames;
        std::vector<std::string> outputNames;
    };

    static NamedSession loadSession(Ort::Env& env, const Ort::SessionOptions& opts, const std::string& path);
    std::vector<Ort::Value> run(const NamedSession& ns,
                                 std::vector<std::pair<std::string, Ort::Value>> inputs) const;

    Ort::Env env_;
    NamedSession session_;
    std::unordered_map<char32_t, int64_t> phonemeIdMap_;
    int sampleRate_ = 22050;
    std::string espeakVoice_ = "de";
    float noiseScale_ = 0.667f;
    float lengthScale_ = 1.0f;
    float noiseW_ = 0.8f;

    bool usingGpu_ = false;
    std::string gpuName_;

    // See KokoroEngine::runMutex_ — concurrent Run() on the same DML session
    // crashes, so chunked synthesis serializes on this when usingGpu_ is true.
    mutable std::mutex runMutex_;
};

} // namespace tts
