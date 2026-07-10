#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "Phonemizer.h"
#include "TtsTypes.h"

namespace tts {

// Kokoro-82M ONNX engine (kokoro-v1.0.onnx, 24000Hz output).
//
// Mirrors kokoro_onnx's Kokoro/Tokenizer (see native/scripts/kokoro_*_ref.py):
//   1. phonemize(text, lang) via espeak-ng (IPA, stress + punctuation preserved)
//   2. filter phonemes to the model's 178-entry vocab, map to token ids
//   3. tokens = [0, ...ids, 0]; style = voice[len(ids)] (row from a (510,256) table)
//   4. run ONNX: {tokens int64[1,L], style float32[1,256], speed float32[1]} -> audio float32[-1]
class KokoroEngine {
public:
    static constexpr int kStyleRows = 510;
    static constexpr int kStyleDim = 256;

    // modelDir: directory containing the ONNX model (modelFileName,
    // default "kokoro-v1.0.onnx"), vocab.json (the kokoro-onnx vocab table
    // under a "vocab" key) and a voices/ subdirectory of <voice_name>.bin
    // files (510*256 float32 each, see scripts/extract_kokoro_voices.py).
    explicit KokoroEngine(const std::string& modelDir, const std::string& modelFileName = "kokoro-v1.0.onnx");

    // Loads a voice's full (510,256) style table, flattened row-major
    // (510*256 floats). Used directly for synthesis, or blended with another
    // voice's table (weighted average) for voice mixing before synthesis.
    std::vector<float> loadVoiceStyleTable(const std::string& voiceName) const;

    // phonemizer: used to convert `text` to IPA via espeak-ng for `espeakVoice`
    // (e.g. "en-us", "en-gb", "fr-fr", "cmn", ...).
    // styleTable: a full (510*256)-float style table (from loadVoiceStyleTable,
    // or a weighted blend of two such tables for mixed voices).
    AudioBuffer synthesize(Phonemizer& phonemizer, const std::string& text,
                            const std::string& espeakVoice,
                            const std::vector<float>& styleTable, float speed) const;

    int sampleRate() const { return 24000; }

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
    std::unordered_map<char32_t, int32_t> vocab_;
    std::string voicesDir_;

    bool usingGpu_ = false;
    std::string gpuName_;

    // Concurrent Run() calls on the same session crash the DirectML EP (its
    // D3D12 command queue isn't safe to submit to from multiple threads
    // without synchronization), so chunked/concurrent synthesis serializes
    // on this when usingGpu_ is true. CPU sessions remain unaffected.
    mutable std::mutex runMutex_;
};

} // namespace tts
