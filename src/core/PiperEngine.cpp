#include "PiperEngine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "GpuDevice.h"
#include "UnicodeProcessor.h"

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

} // namespace

PiperEngine::NamedSession PiperEngine::loadSession(Ort::Env& env, const Ort::SessionOptions& opts,
                                                     const std::string& path) {
    NamedSession ns;
    std::filesystem::path p(path);
    ns.session = Ort::Session(env, p.c_str(), opts);

    Ort::AllocatorWithDefaultOptions alloc;
    for (size_t i = 0; i < ns.session.GetInputCount(); ++i) {
        ns.inputNames.emplace_back(ns.session.GetInputNameAllocated(i, alloc).get());
    }
    for (size_t i = 0; i < ns.session.GetOutputCount(); ++i) {
        ns.outputNames.emplace_back(ns.session.GetOutputNameAllocated(i, alloc).get());
    }
    return ns;
}

std::vector<Ort::Value> PiperEngine::run(const NamedSession& ns,
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
            throw std::runtime_error("PiperEngine: ONNX graph expects input '" + expectedName +
                                      "' which was not provided.");
        }
    }

    std::vector<const char*> outputNamePtrs;
    outputNamePtrs.reserve(ns.outputNames.size());
    for (const auto& n : ns.outputNames) outputNamePtrs.push_back(n.c_str());

    return const_cast<Ort::Session&>(ns.session).Run(Ort::RunOptions{nullptr}, inputNamePtrs.data(),
                                                       inputValues.data(), inputValues.size(),
                                                       outputNamePtrs.data(), outputNamePtrs.size());
}

// ERROR (not WARNING): with the DML EP, ORT always logs a benign "Some nodes
// were not assigned to the preferred execution providers" warning for
// shape-related ops it intentionally keeps on CPU.
PiperEngine::PiperEngine(const std::string& onnxPath, const std::string& jsonPath) : env_(ORT_LOGGING_LEVEL_ERROR, "piper") {
    {
        std::ifstream f(jsonPath, std::ios::binary);
        if (!f) {
            throw std::runtime_error("Piper config not found: " + jsonPath);
        }
        nlohmann::json j;
        f >> j;

        sampleRate_ = j.at("audio").at("sample_rate").get<int>();
        if (j.contains("espeak") && j.at("espeak").contains("voice")) {
            espeakVoice_ = j.at("espeak").at("voice").get<std::string>();
        }
        if (j.contains("inference")) {
            const auto& inf = j.at("inference");
            if (inf.contains("noise_scale")) noiseScale_ = inf.at("noise_scale").get<float>();
            if (inf.contains("length_scale")) lengthScale_ = inf.at("length_scale").get<float>();
            if (inf.contains("noise_w")) noiseW_ = inf.at("noise_w").get<float>();
        }

        for (auto& [key, value] : j.at("phoneme_id_map").items()) {
            std::u32string cps = utf8ToCodepoints(key);
            if (cps.size() == 1 && value.is_array() && !value.empty()) {
                phonemeIdMap_[cps[0]] = value[0].get<int64_t>();
            }
        }
    }

    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    usingGpu_ = tryEnableBestGpu(opts, &gpuName_);
    if (!usingGpu_) {
        // Capped so multiple chunks can be synthesized concurrently (see
        // MainWindow::synthesizeChunksConcurrently) without oversubscribing the CPU.
        opts.SetIntraOpNumThreads(2);
    }
    session_ = loadSession(env_, opts, onnxPath);
}

AudioBuffer PiperEngine::synthesize(Phonemizer& phonemizer, const std::string& text, float speed) const {
    std::string phonemes = phonemizer.phonemize(text, espeakVoice_);
    std::u32string cps = utf8ToCodepoints(phonemes);

    auto idFor = [this](char32_t cp) -> int64_t {
        auto it = phonemeIdMap_.find(cp);
        return it != phonemeIdMap_.end() ? it->second : -1;
    };

    std::vector<int64_t> ids;
    ids.reserve(cps.size() * 2 + 2);
    ids.push_back(idFor(U'^')); // BOS
    int64_t padId = idFor(U'_');
    for (char32_t cp : cps) {
        int64_t id = idFor(cp);
        if (id < 0) continue;
        ids.push_back(id);
        ids.push_back(padId);
    }
    ids.push_back(idFor(U'$')); // EOS

    std::vector<int64_t> lengths = {static_cast<int64_t>(ids.size())};

    // length_scale is inversely proportional to speaking speed.
    float lengthScale = lengthScale_ / std::max(0.1f, speed);
    std::vector<float> scales = {noiseScale_, lengthScale, noiseW_};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<std::pair<std::string, Ort::Value>> inputs;
    inputs.emplace_back("input", makeInt64Tensor(mem, ids, {1, static_cast<int64_t>(ids.size())}));
    inputs.emplace_back("input_lengths", makeInt64Tensor(mem, lengths, {1}));
    inputs.emplace_back("scales", makeFloatTensor(mem, scales, {3}));

    auto outputs = run(session_, std::move(inputs));

    AudioBuffer audio;
    audio.sampleRate = sampleRate_;
    auto info = outputs[0].GetTensorTypeAndShapeInfo();
    size_t count = info.GetElementCount();
    const float* data = outputs[0].GetTensorData<float>();
    audio.samples.assign(data, data + count);
    return audio;
}

} // namespace tts
