#include "KokoroEngine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "GpuDevice.h"
#include "UnicodeProcessor.h"

namespace tts {

namespace {

constexpr int kMaxPhonemeLength = 510; // kokoro_onnx MAX_PHONEME_LENGTH

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

KokoroEngine::NamedSession KokoroEngine::loadSession(Ort::Env& env, const Ort::SessionOptions& opts,
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

std::vector<Ort::Value> KokoroEngine::run(const NamedSession& ns,
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
            throw std::runtime_error("KokoroEngine: ONNX graph expects input '" + expectedName +
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

KokoroEngine::KokoroEngine(const std::string& modelDir, const std::string& modelFileName)
    // ERROR (not WARNING): with the DML EP, ORT always logs a benign
    // "Some nodes were not assigned to the preferred execution providers"
    // warning for shape-related ops it intentionally keeps on CPU.
    : env_(ORT_LOGGING_LEVEL_ERROR, "kokoro") {
    std::filesystem::path dir(modelDir);
    voicesDir_ = (dir / "voices").string();

    // Load the vocab table ({"vocab": {"<char>": <id>, ...}}).
    {
        std::ifstream f(dir / "vocab.json", std::ios::binary);
        if (!f) {
            throw std::runtime_error("Kokoro vocab.json not found in " + modelDir);
        }
        nlohmann::json j;
        f >> j;
        for (auto& [key, value] : j.at("vocab").items()) {
            std::u32string cps = utf8ToCodepoints(key);
            if (cps.size() == 1) {
                vocab_[cps[0]] = value.get<int32_t>();
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
    session_ = loadSession(env_, opts, (dir / modelFileName).string());
}

std::vector<float> KokoroEngine::loadVoiceStyleTable(const std::string& voiceName) const {
    std::filesystem::path path = std::filesystem::path(voicesDir_) / (voiceName + ".bin");
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Kokoro voice style not found: " + path.string());
    }
    std::vector<float> data(static_cast<size_t>(kStyleRows) * kStyleDim);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(float)));
    if (!f) {
        throw std::runtime_error("Kokoro voice style truncated: " + path.string());
    }
    return data;
}

AudioBuffer KokoroEngine::synthesize(Phonemizer& phonemizer, const std::string& text,
                                      const std::string& espeakVoice,
                                      const std::vector<float>& styleTable, float speed) const {
    if (styleTable.size() != static_cast<size_t>(kStyleRows) * kStyleDim) {
        throw std::runtime_error("KokoroEngine::synthesize: styleTable has wrong size");
    }

    // 1. Phonemize (espeak-ng IPA, stress + punctuation preserved).
    std::string phonemes = phonemizer.phonemize(text, espeakVoice);

    // 2. Filter to vocab chars and map to token ids.
    std::u32string cps = utf8ToCodepoints(phonemes);
    std::vector<int64_t> ids;
    ids.reserve(cps.size());
    for (char32_t cp : cps) {
        auto it = vocab_.find(cp);
        if (it != vocab_.end()) {
            ids.push_back(it->second);
        }
    }
    if (static_cast<int>(ids.size()) > kMaxPhonemeLength - 2) {
        ids.resize(kMaxPhonemeLength - 2);
    }

    // 3. style = styleTable[len(ids)] (a kStyleDim row), tokens = [0, ...ids, 0].
    int row = std::clamp<int>(static_cast<int>(ids.size()), 0, kStyleRows - 1);
    std::vector<float> style(styleTable.begin() + static_cast<size_t>(row) * kStyleDim,
                              styleTable.begin() + static_cast<size_t>(row + 1) * kStyleDim);

    std::vector<int64_t> tokens;
    tokens.reserve(ids.size() + 2);
    tokens.push_back(0);
    tokens.insert(tokens.end(), ids.begin(), ids.end());
    tokens.push_back(0);

    std::vector<float> speedVec = {speed};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<std::pair<std::string, Ort::Value>> inputs;
    inputs.emplace_back("tokens", makeInt64Tensor(mem, tokens, {1, static_cast<int64_t>(tokens.size())}));
    inputs.emplace_back("style", makeFloatTensor(mem, style, {1, kStyleDim}));
    inputs.emplace_back("speed", makeFloatTensor(mem, speedVec, {1}));

    auto outputs = run(session_, std::move(inputs));

    AudioBuffer audio;
    audio.sampleRate = sampleRate();
    auto info = outputs[0].GetTensorTypeAndShapeInfo();
    size_t count = info.GetElementCount();
    const float* data = outputs[0].GetTensorData<float>();
    audio.samples.assign(data, data + count);
    return audio;
}

} // namespace tts
