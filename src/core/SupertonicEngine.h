#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "TtsTypes.h"
#include "UnicodeProcessor.h"
#include "VoiceStyle.h"

namespace tts {

// Loads the 4 Supertonic ONNX models (duration_predictor, text_encoder,
// vector_estimator, vocoder) plus the unicode indexer, and runs the
// text -> flow-matching -> vocoder pipeline described in supertonic's
// core.py (Supertonic.__call__).
class SupertonicEngine {
public:
    // modelDir: path to the directory containing tts.json, unicode_indexer.json,
    // duration_predictor.onnx, text_encoder.onnx, vector_estimator.onnx, vocoder.onnx
    // (i.e. ".../models/supertonic/onnx").
    explicit SupertonicEngine(const std::string& modelDir);

    AudioBuffer synthesize(const SynthParams& params, const VoiceStyle& style) const;

    int sampleRate() const { return sampleRate_; }

    // True if a DirectML GPU execution provider was successfully attached
    // (see GpuDevice::selectBestDmlDevice); false if running on CPU.
    bool usingGpu() const { return usingGpu_; }

    // Name of the GPU adapter used when usingGpu() is true, empty otherwise.
    const std::string& gpuName() const { return gpuName_; }

    // Logs (via printf) the input/output tensor names for all 4 sessions —
    // useful for verifying the pipeline's hardcoded tensor names against
    // the actual ONNX graphs.
    void debugPrintIO() const;

private:
    struct NamedSession {
        Ort::Session session{nullptr};
        std::vector<std::string> inputNames;
        std::vector<std::string> outputNames;
    };

    static NamedSession loadSession(Ort::Env& env, const Ort::SessionOptions& opts, const std::string& path);

    // Runs `ns` with the given name->tensor inputs (looked up by name, order
    // doesn't matter) and returns all outputs in ONNX-declared order.
    std::vector<Ort::Value> run(const NamedSession& ns,
                                 std::vector<std::pair<std::string, Ort::Value>> inputs) const;

    Ort::Env env_;
    NamedSession durationPredictor_;
    NamedSession textEncoder_;
    NamedSession vectorEstimator_;
    NamedSession vocoder_;
    UnicodeProcessor textProcessor_;

    int sampleRate_ = 44100;
    int baseChunkSize_ = 512;
    int chunkCompressFactor_ = 6;
    int latentDim_ = 24;

    bool usingGpu_ = false;
    std::string gpuName_;

    // See KokoroEngine::runMutex_ — concurrent Run() on the same DML session
    // crashes, so chunked synthesis serializes on this when usingGpu_ is true.
    mutable std::mutex runMutex_;
};

} // namespace tts
