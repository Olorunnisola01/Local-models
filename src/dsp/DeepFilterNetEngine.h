#pragma once

#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

namespace tts {

// Offline (non-streaming) C++ port of DeepFilterNet3's inference pipeline:
// STFT -> ERB/complex feature extraction -> 3 ONNX models (encoder,
// ERB-gain decoder, deep-filtering decoder) -> ERB gain + deep filtering ->
// ISTFT. Operates on the whole signal at once (batched over the time axis),
// which is fine for offline post-processing of already-generated audio.
class DeepFilterNetEngine {
public:
    // modelDir: path to a directory containing enc.onnx, erb_dec.onnx,
    // df_dec.onnx (DeepFilterNet3 ONNX export).
    explicit DeepFilterNetEngine(const std::string& modelDir);

    // Denoises `input` (mono PCM, any sample rate) and returns a denoised
    // signal of the same length and sample rate. Internally resamples to/from
    // the model's native 48kHz.
    std::vector<float> enhance(const std::vector<float>& input, int sampleRate) const;

    static constexpr int kModelSampleRate = 48000;

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
    static std::vector<Ort::Value> run(const NamedSession& ns,
                                        std::vector<std::pair<std::string, Ort::Value>> inputs);

    Ort::Env env_;
    NamedSession encoder_;
    NamedSession erbDecoder_;
    NamedSession dfDecoder_;

    bool usingGpu_ = false;
    std::string gpuName_;
};

} // namespace tts
