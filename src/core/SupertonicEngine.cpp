#include "SupertonicEngine.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "GpuDevice.h"

namespace tts {

namespace {

Ort::Value makeFloatTensor(Ort::MemoryInfo& mem, const std::vector<float>& data,
                            const std::vector<int64_t>& shape) {
    return Ort::Value::CreateTensor<float>(mem, const_cast<float*>(data.data()), data.size(),
                                            shape.data(), shape.size());
}

Ort::Value makeInt64Tensor(Ort::MemoryInfo& mem, const std::vector<int64_t>& data,
                            const std::vector<int64_t>& shape) {
    return Ort::Value::CreateTensor<int64_t>(mem, const_cast<int64_t*>(data.data()), data.size(),
                                              shape.data(), shape.size());
}

std::vector<int64_t> tensorShape(const Ort::Value& v) {
    return v.GetTensorTypeAndShapeInfo().GetShape();
}

std::vector<float> tensorToFloatVector(const Ort::Value& v) {
    auto info = v.GetTensorTypeAndShapeInfo();
    size_t count = info.GetElementCount();
    const float* data = v.GetTensorData<float>();
    return std::vector<float>(data, data + count);
}

} // namespace

SupertonicEngine::NamedSession SupertonicEngine::loadSession(Ort::Env& env, const Ort::SessionOptions& opts,
                                                               const std::string& path) {
    NamedSession ns;
    std::filesystem::path p(path);
    ns.session = Ort::Session(env, p.c_str(), opts);

    Ort::AllocatorWithDefaultOptions alloc;
    size_t numInputs = ns.session.GetInputCount();
    for (size_t i = 0; i < numInputs; ++i) {
        auto name = ns.session.GetInputNameAllocated(i, alloc);
        ns.inputNames.emplace_back(name.get());
    }
    size_t numOutputs = ns.session.GetOutputCount();
    for (size_t i = 0; i < numOutputs; ++i) {
        auto name = ns.session.GetOutputNameAllocated(i, alloc);
        ns.outputNames.emplace_back(name.get());
    }
    return ns;
}

std::vector<Ort::Value> SupertonicEngine::run(const NamedSession& ns,
                                                std::vector<std::pair<std::string, Ort::Value>> inputs) const {
    std::unique_lock<std::mutex> lock;
    if (usingGpu_) {
        lock = std::unique_lock<std::mutex>(runMutex_);
    }

    std::vector<const char*> inputNamePtrs;
    std::vector<Ort::Value> inputValues;
    inputNamePtrs.reserve(ns.inputNames.size());
    inputValues.reserve(ns.inputNames.size());

    for (const auto& expectedName : ns.inputNames) {
        bool found = false;
        for (auto& [name, value] : inputs) {
            if (name == expectedName) {
                inputNamePtrs.push_back(expectedName.c_str());
                inputValues.push_back(std::move(value));
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("SupertonicEngine: ONNX graph expects input '" + expectedName +
                                      "' which was not provided. Check tensor names against core.py.");
        }
    }

    std::vector<const char*> outputNamePtrs;
    outputNamePtrs.reserve(ns.outputNames.size());
    for (const auto& n : ns.outputNames) {
        outputNamePtrs.push_back(n.c_str());
    }

    return const_cast<Ort::Session&>(ns.session).Run(Ort::RunOptions{nullptr}, inputNamePtrs.data(),
                                                       inputValues.data(), inputValues.size(),
                                                       outputNamePtrs.data(), outputNamePtrs.size());
}

// ERROR (not WARNING): with the DML EP, ORT always logs a benign "Some nodes
// were not assigned to the preferred execution providers" warning for
// shape-related ops it intentionally keeps on CPU.
SupertonicEngine::SupertonicEngine(const std::string& modelDir)
    : env_(ORT_LOGGING_LEVEL_ERROR, "supertonic"),
      textProcessor_((std::filesystem::path(modelDir) / "unicode_indexer.json").string()) {
    std::filesystem::path dir(modelDir);

    // Read tts.json for sample rate / latent geometry (mirrors Supertonic.__init__).
    {
        std::ifstream f(dir / "tts.json", std::ios::binary);
        if (!f) {
            throw std::runtime_error("tts.json not found in " + modelDir);
        }
        nlohmann::json j;
        f >> j;
        sampleRate_ = j.at("ae").at("sample_rate").get<int>();
        baseChunkSize_ = j.at("ae").at("base_chunk_size").get<int>();
        chunkCompressFactor_ = j.at("ttl").at("chunk_compress_factor").get<int>();
        latentDim_ = j.at("ttl").at("latent_dim").get<int>();
    }

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    // Prefer a DirectML GPU (discrete AMD/NVIDIA over integrated Intel) if one
    // is available; fall back to CPU if DML EP creation fails for any reason.
    // EDGETTS_FORCE_CPU=1 skips GPU selection entirely (useful for benchmarking
    // and as a troubleshooting escape hatch if a DML driver misbehaves).
    usingGpu_ = tryEnableBestGpu(opts, &gpuName_);
    if (!usingGpu_) {
        // Capped so multiple chunks can be synthesized concurrently (see
        // MainWindow::synthesizeChunksConcurrently) without oversubscribing the CPU.
        opts.SetIntraOpNumThreads(2);
    }

    durationPredictor_ = loadSession(env_, opts, (dir / "duration_predictor.onnx").string());
    textEncoder_ = loadSession(env_, opts, (dir / "text_encoder.onnx").string());
    vectorEstimator_ = loadSession(env_, opts, (dir / "vector_estimator.onnx").string());
    vocoder_ = loadSession(env_, opts, (dir / "vocoder.onnx").string());
}

void SupertonicEngine::debugPrintIO() const {
    auto printNs = [](const char* label, const NamedSession& ns) {
        std::printf("%s inputs:", label);
        for (const auto& n : ns.inputNames) std::printf(" %s", n.c_str());
        std::printf("\n%s outputs:", label);
        for (const auto& n : ns.outputNames) std::printf(" %s", n.c_str());
        std::printf("\n");
    };
    printNs("duration_predictor", durationPredictor_);
    printNs("text_encoder", textEncoder_);
    printNs("vector_estimator", vectorEstimator_);
    printNs("vocoder", vocoder_);
}

AudioBuffer SupertonicEngine::synthesize(const SynthParams& params, const VoiceStyle& style) const {
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // 1. Tokenize text.
    UnicodeProcessor::Tokenized tok = textProcessor_.process(params.text, params.lang);
    const int64_t L = tok.length;
    std::vector<int64_t> textIdsShape = {1, L};
    std::vector<int64_t> textMaskShape = {1, 1, L};

    std::vector<float> styleTtl = style.styleTtl; // [1,50,256]
    std::vector<float> styleDp = style.styleDp;   // [1,8,16]
    std::vector<int64_t> styleTtlShape = {1, 50, 256};
    std::vector<int64_t> styleDpShape = {1, 8, 16};

    // 2. duration_predictor: {text_ids, style_dp, text_mask} -> duration (seconds).
    float durationSec;
    {
        std::vector<std::pair<std::string, Ort::Value>> inputs;
        inputs.emplace_back("text_ids", makeInt64Tensor(mem, tok.textIds, textIdsShape));
        inputs.emplace_back("style_dp", makeFloatTensor(mem, styleDp, styleDpShape));
        inputs.emplace_back("text_mask", makeFloatTensor(mem, tok.textMask, textMaskShape));
        auto outputs = run(durationPredictor_, std::move(inputs));
        durationSec = outputs[0].GetTensorData<float>()[0];
    }
    const float speed = std::clamp(params.speed, 0.7f, 2.0f);
    durationSec /= speed;

    // 3. text_encoder: {text_ids, style_ttl, text_mask} -> text_emb.
    std::vector<float> textEmb;
    std::vector<int64_t> textEmbShape;
    {
        std::vector<std::pair<std::string, Ort::Value>> inputs;
        inputs.emplace_back("text_ids", makeInt64Tensor(mem, tok.textIds, textIdsShape));
        inputs.emplace_back("style_ttl", makeFloatTensor(mem, styleTtl, styleTtlShape));
        inputs.emplace_back("text_mask", makeFloatTensor(mem, tok.textMask, textMaskShape));
        auto outputs = run(textEncoder_, std::move(inputs));
        textEmbShape = tensorShape(outputs[0]);
        textEmb = tensorToFloatVector(outputs[0]);
    }

    // 4. sample_noisy_latent (core.py Supertonic.sample_noisy_latent, batch size 1).
    const int64_t chunkSize = static_cast<int64_t>(baseChunkSize_) * chunkCompressFactor_; // 3072
    const double wavLenMax = static_cast<double>(durationSec) * sampleRate_;
    int64_t latentLen = static_cast<int64_t>((wavLenMax + chunkSize - 1) / chunkSize);
    if (latentLen < 1) latentLen = 1;
    const int64_t latentDimFull = static_cast<int64_t>(latentDim_) * chunkCompressFactor_; // 144

    std::vector<int64_t> latentShape = {1, latentDimFull, latentLen};
    std::vector<int64_t> latentMaskShape = {1, 1, latentLen};

    std::vector<float> xt(static_cast<size_t>(latentDimFull * latentLen));
    {
        std::mt19937 rng(std::random_device{}());
        std::normal_distribution<float> dist(0.0f, 1.0f);
        for (float& v : xt) v = dist(rng);
    }
    // latent_mask is all-ones for a single (unpadded) batch element.
    std::vector<float> latentMask(static_cast<size_t>(latentLen), 1.0f);

    // 5. vector_estimator flow-matching loop.
    const int totalSteps = std::max(1, params.totalSteps);
    for (int step = 0; step < totalSteps; ++step) {
        std::vector<float> currentStep = {static_cast<float>(step)};
        std::vector<float> totalStepVec = {static_cast<float>(totalSteps)};

        std::vector<std::pair<std::string, Ort::Value>> inputs;
        inputs.emplace_back("noisy_latent", makeFloatTensor(mem, xt, latentShape));
        inputs.emplace_back("text_emb", makeFloatTensor(mem, textEmb, textEmbShape));
        inputs.emplace_back("style_ttl", makeFloatTensor(mem, styleTtl, styleTtlShape));
        inputs.emplace_back("text_mask", makeFloatTensor(mem, tok.textMask, textMaskShape));
        inputs.emplace_back("latent_mask", makeFloatTensor(mem, latentMask, latentMaskShape));
        inputs.emplace_back("current_step", makeFloatTensor(mem, currentStep, {1}));
        inputs.emplace_back("total_step", makeFloatTensor(mem, totalStepVec, {1}));

        auto outputs = run(vectorEstimator_, std::move(inputs));
        xt = tensorToFloatVector(outputs[0]); // becomes the new noisy_latent for the next step
    }

    // 6. vocoder: {latent} -> wav.
    AudioBuffer audio;
    audio.sampleRate = sampleRate_;
    {
        std::vector<std::pair<std::string, Ort::Value>> inputs;
        inputs.emplace_back("latent", makeFloatTensor(mem, xt, latentShape));
        auto outputs = run(vocoder_, std::move(inputs));
        audio.samples = tensorToFloatVector(outputs[0]);
    }
    return audio;
}

} // namespace tts
