#include "DeepFilterNetEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <stdexcept>

#include "Fft.h"
#include "Resampler.h"

namespace tts {

namespace {

// DeepFilterNet3 constants (see models/deepfilternet/config.ini [df]).
constexpr int kFftSize = 960;
constexpr int kHopSize = 480;
constexpr int kFreqSize = kFftSize / 2 + 1; // 481
constexpr int kNbErb = 32;
constexpr int kNbDf = 96;
constexpr int kMinNbErbFreqs = 2;
constexpr int kDfOrder = 5;
constexpr int kConvLookahead = 2;

// Local-SNR gating thresholds (libDF/src/tract.rs RuntimeParams::default).
constexpr float kMinDbThresh = -10.0f;
constexpr float kMaxDbErbThresh = 30.0f;
constexpr float kMaxDbDfThresh = 20.0f;

// Exponential running-state normalization factor:
// calc_norm_alpha(sr=48000, hop_size=480, tau=1.0) == 0.99
constexpr float kAlpha = 0.99f;

float freq2erb(float freqHz) {
    return 9.265f * std::log1p(freqHz / (24.7f * 9.265f));
}

float erb2freq(float nErb) {
    return 24.7f * 9.265f * (std::exp(nErb / 9.265f) - 1.0f);
}

// Port of libDF/src/lib.rs::erb_fb for sr=48000, fft_size=960, nb_bands=32,
// min_nb_freqs=2. Returns the number of frequency bins per ERB band; sums to
// kFreqSize (481).
std::vector<int> buildErbFilterbank() {
    const int sr = DeepFilterNetEngine::kModelSampleRate;
    const int nyq = sr / 2;
    const float freqWidth = static_cast<float>(sr) / static_cast<float>(kFftSize);
    const float erbLow = freq2erb(0.0f);
    const float erbHigh = freq2erb(static_cast<float>(nyq));
    const float step = (erbHigh - erbLow) / static_cast<float>(kNbErb);

    std::vector<int> erb(kNbErb, 0);
    int prevFreq = 0;
    int freqOver = 0;
    for (int i = 1; i <= kNbErb; ++i) {
        const float f = erb2freq(erbLow + static_cast<float>(i) * step);
        const int fb = static_cast<int>(std::round(f / freqWidth));
        int nbFreqs = fb - prevFreq - freqOver;
        if (nbFreqs < kMinNbErbFreqs) {
            freqOver = kMinNbErbFreqs - nbFreqs;
            nbFreqs = kMinNbErbFreqs;
        } else {
            freqOver = 0;
        }
        erb[i - 1] = nbFreqs;
        prevFreq = fb;
    }
    erb[kNbErb - 1] += 1;
    int sum = 0;
    for (int v : erb) sum += v;
    const int tooLarge = sum - kFreqSize;
    if (tooLarge > 0) erb[kNbErb - 1] -= tooLarge;
    return erb;
}

// Vorbis analysis/synthesis window: sin(pi/2 * sin^2(pi*n/N)).
std::vector<float> buildVorbisWindow() {
    constexpr double kPi = 3.14159265358979323846;
    std::vector<float> w(kFftSize);
    const double half = kFftSize / 2;
    for (int i = 0; i < kFftSize; ++i) {
        const double s = std::sin(0.5 * kPi * (i + 0.5) / half);
        w[i] = static_cast<float>(std::sin(0.5 * kPi * s * s));
    }
    return w;
}

// STFT analysis for `numFrames` hop-sized steps starting at `padded[0]`.
// `padded` must contain at least (numFrames+1)*kHopSize samples. Frame t
// covers padded[t*hop .. t*hop+fft_size), windowed and scaled by
// wnorm = 1/fft_size (since hop == fft_size/2 here).
std::vector<std::vector<std::complex<float>>> stftAnalysis(const std::vector<float>& padded, const Fft& fft,
                                                             const std::vector<float>& window, int numFrames) {
    std::vector<std::vector<std::complex<float>>> out(numFrames, std::vector<std::complex<float>>(kFreqSize));
    constexpr float kWnorm = 1.0f / static_cast<float>(kFftSize);
    std::vector<std::complex<float>> buf(kFftSize);
    for (int t = 0; t < numFrames; ++t) {
        const float* frame = padded.data() + static_cast<size_t>(t) * kHopSize;
        for (int i = 0; i < kFftSize; ++i) buf[i] = std::complex<float>(frame[i] * window[i], 0.0f);
        const auto X = fft.forward(buf);
        for (int k = 0; k < kFreqSize; ++k) out[t][k] = X[k] * kWnorm;
    }
    return out;
}

// ISTFT synthesis (50% overlap-add with the Vorbis window). Returns
// spectra.size() * kHopSize output samples.
std::vector<float> istftSynthesis(const std::vector<std::vector<std::complex<float>>>& spectra, const Fft& fft,
                                   const std::vector<float>& window) {
    const int numFrames = static_cast<int>(spectra.size());
    std::vector<float> output(static_cast<size_t>(numFrames) * kHopSize, 0.0f);
    std::vector<float> synthMem(kHopSize, 0.0f);
    std::vector<std::complex<float>> full(kFftSize);
    for (int t = 0; t < numFrames; ++t) {
        const auto& spec = spectra[t];
        for (int k = 0; k < kFreqSize; ++k) full[k] = spec[k];
        for (int k = 1; k < kFftSize / 2; ++k) full[kFftSize - k] = std::conj(spec[k]);
        const auto x = fft.inverse(full);

        float* outFrame = output.data() + static_cast<size_t>(t) * kHopSize;
        // realfft's c2r transform is unnormalized (forward * inverse == N*x),
        // while Fft::inverse divides by N, so multiply back by kFftSize here.
        for (int i = 0; i < kHopSize; ++i) {
            const float xw = x[i].real() * static_cast<float>(kFftSize) * window[i];
            outFrame[i] = xw + synthMem[i];
        }
        for (int i = 0; i < kHopSize; ++i) {
            const float xw = x[kHopSize + i].real() * static_cast<float>(kFftSize) * window[kHopSize + i];
            synthMem[i] = xw;
        }
    }
    return output;
}

// out[band] = mean over the band's frequency bins of Re(a[f] * conj(b[f])).
void computeBandCorr(std::vector<float>& out, const std::vector<std::complex<float>>& a,
                      const std::vector<std::complex<float>>& b, const std::vector<int>& erbFb) {
    int bcsum = 0;
    for (size_t band = 0; band < erbFb.size(); ++band) {
        const int bandSize = erbFb[band];
        float sum = 0.0f;
        for (int j = 0; j < bandSize; ++j) {
            const int idx = bcsum + j;
            sum += a[idx].real() * b[idx].real() + a[idx].imag() * b[idx].imag();
        }
        out[band] = sum / static_cast<float>(bandSize);
        bcsum += bandSize;
    }
}

// Multiplies each frequency bin's complex spectrum value by its ERB band's
// gain (apply_interp_band_gain in libDF/src/lib.rs).
void applyInterpBandGain(std::vector<std::complex<float>>& spec, const float* bandGains,
                          const std::vector<int>& erbFb) {
    int bcsum = 0;
    for (size_t band = 0; band < erbFb.size(); ++band) {
        const int bandSize = erbFb[band];
        for (int j = 0; j < bandSize; ++j) spec[bcsum + j] *= bandGains[band];
        bcsum += bandSize;
    }
}

// apply_stages from libDF/src/tract.rs: decides which stages to apply for a
// frame given its local-SNR estimate.
struct Stages {
    bool applyGains;
    bool applyGainZeros;
    bool applyDf;
};

Stages applyStages(float lsnr) {
    if (lsnr < kMinDbThresh) return {false, true, false};
    if (lsnr > kMaxDbErbThresh) return {false, false, false};
    if (lsnr > kMaxDbDfThresh) return {true, false, false};
    return {true, false, true};
}

Ort::Value makeFloatTensor(Ort::MemoryInfo& mem, std::vector<float>& data, const std::vector<int64_t>& shape) {
    return Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), shape.data(), shape.size());
}

// Wraps an existing output tensor's buffer as a new (non-owning) input
// tensor. `v` must stay alive for as long as the returned value is used.
Ort::Value wrapAsInput(Ort::MemoryInfo& mem, Ort::Value& v) {
    auto info = v.GetTensorTypeAndShapeInfo();
    auto shape = info.GetShape();
    return Ort::Value::CreateTensor<float>(mem, v.GetTensorMutableData<float>(), info.GetElementCount(), shape.data(),
                                            shape.size());
}

} // namespace

DeepFilterNetEngine::NamedSession DeepFilterNetEngine::loadSession(Ort::Env& env, const Ort::SessionOptions& opts,
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

std::vector<Ort::Value> DeepFilterNetEngine::run(const NamedSession& ns,
                                                  std::vector<std::pair<std::string, Ort::Value>> inputs) {
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
            throw std::runtime_error("DeepFilterNetEngine: ONNX graph expects input '" + expectedName +
                                      "' which was not provided.");
        }
    }

    std::vector<const char*> outputNamePtrs;
    outputNamePtrs.reserve(ns.outputNames.size());
    for (const auto& n : ns.outputNames) outputNamePtrs.push_back(n.c_str());

    return const_cast<Ort::Session&>(ns.session).Run(Ort::RunOptions{nullptr}, inputNamePtrs.data(),
                                                       inputValues.data(), inputValues.size(), outputNamePtrs.data(),
                                                       outputNamePtrs.size());
}

DeepFilterNetEngine::DeepFilterNetEngine(const std::string& modelDir) : env_(ORT_LOGGING_LEVEL_WARNING, "deepfilternet") {
    std::filesystem::path dir(modelDir);
    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    // DFN's 3 ONNX models are tiny (GRU-heavy encoder/decoders, ~100-150 nodes
    // each, ~1.5s of audio per call). DirectML's one-time session-creation +
    // first-run kernel-compile cost (~3.1s combined for these 3 sessions,
    // measured) dwarfs any per-call savings, and even warm, DML runs these
    // graphs ~2x slower than CPU (GRU/Einsum ops don't fuse into DML's Conv
    // metacommands). So this engine stays on CPU.

    encoder_ = loadSession(env_, opts, (dir / "enc.onnx").string());
    erbDecoder_ = loadSession(env_, opts, (dir / "erb_dec.onnx").string());
    dfDecoder_ = loadSession(env_, opts, (dir / "df_dec.onnx").string());
}

std::vector<float> DeepFilterNetEngine::enhance(const std::vector<float>& input, int sampleRate) const {
    if (input.empty()) return input;

    std::vector<float> signal =
        (sampleRate == kModelSampleRate) ? input : resampleLinear(input, sampleRate, kModelSampleRate);
    const int N = static_cast<int>(signal.size());
    if (N == 0) return input;

    static const Fft fft(kFftSize);
    static const std::vector<float> window = buildVorbisWindow();
    static const std::vector<int> erbFb = buildErbFilterbank();

    // F_stft frames cover the signal with one extra flush frame; F_total adds
    // kConvLookahead more frames so every output frame's encoder lookup
    // (encT = outT + kConvLookahead) stays in range.
    const int numFrames = (N + kHopSize - 1) / kHopSize;
    const int fStft = numFrames + 1;
    const int fTotal = fStft + kConvLookahead;

    // padded = [hop zeros] + [signal, zero-padded to numFrames*hop] + [(kConvLookahead+1)*hop zeros]
    std::vector<float> padded(static_cast<size_t>(fTotal + 1) * kHopSize, 0.0f);
    std::copy(signal.begin(), signal.end(), padded.begin() + kHopSize);

    const std::vector<std::vector<std::complex<float>>> nsy = stftAnalysis(padded, fft, window, fTotal);

    // --- Feature extraction with exponential running-state normalization ---
    std::vector<float> meanNormState(kNbErb);
    {
        constexpr float lo = -60.0f, hi = -90.0f;
        const float step = (hi - lo) / static_cast<float>(kNbErb - 1);
        for (int i = 0; i < kNbErb; ++i) meanNormState[i] = lo + static_cast<float>(i) * step;
    }
    std::vector<float> unitNormState(kNbDf);
    {
        constexpr float lo = 0.001f, hi = 0.0001f;
        const float step = (hi - lo) / static_cast<float>(kNbDf - 1);
        for (int i = 0; i < kNbDf; ++i) unitNormState[i] = lo + static_cast<float>(i) * step;
    }

    std::vector<float> featErb(static_cast<size_t>(fTotal) * kNbErb);
    std::vector<float> featSpec(static_cast<size_t>(2) * fTotal * kNbDf);
    float* featSpecRe = featSpec.data();
    float* featSpecIm = featSpec.data() + static_cast<size_t>(fTotal) * kNbDf;

    std::vector<float> bandCorr(kNbErb);
    for (int t = 0; t < fTotal; ++t) {
        computeBandCorr(bandCorr, nsy[t], nsy[t], erbFb);
        for (int b = 0; b < kNbErb; ++b) {
            const float val = std::log10(bandCorr[b] + 1e-10f) * 10.0f;
            meanNormState[b] = val * (1.0f - kAlpha) + meanNormState[b] * kAlpha;
            featErb[static_cast<size_t>(t) * kNbErb + b] = (val - meanNormState[b]) / 40.0f;
        }
        for (int f = 0; f < kNbDf; ++f) {
            const std::complex<float>& x = nsy[t][f];
            const float mag = std::abs(x);
            unitNormState[f] = mag * (1.0f - kAlpha) + unitNormState[f] * kAlpha;
            const float s = std::sqrt(unitNormState[f]);
            featSpecRe[static_cast<size_t>(t) * kNbDf + f] = x.real() / s;
            featSpecIm[static_cast<size_t>(t) * kNbDf + f] = x.imag() / s;
        }
    }

    // --- Run the 3 ONNX models once over the full sequence ---
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    const std::vector<int64_t> erbShape = {1, 1, fTotal, kNbErb};
    const std::vector<int64_t> specShape = {1, 2, fTotal, kNbDf};

    std::vector<std::pair<std::string, Ort::Value>> encInputs;
    encInputs.emplace_back("feat_erb", makeFloatTensor(mem, featErb, erbShape));
    encInputs.emplace_back("feat_spec", makeFloatTensor(mem, featSpec, specShape));
    std::vector<Ort::Value> encOut = run(encoder_, std::move(encInputs));

    auto outIdx = [](const NamedSession& ns, const char* name) -> size_t {
        for (size_t i = 0; i < ns.outputNames.size(); ++i) {
            if (ns.outputNames[i] == name) return i;
        }
        throw std::runtime_error(std::string("DeepFilterNetEngine: output '") + name + "' not found");
    };

    Ort::Value& e0 = encOut[outIdx(encoder_, "e0")];
    Ort::Value& e1 = encOut[outIdx(encoder_, "e1")];
    Ort::Value& e2 = encOut[outIdx(encoder_, "e2")];
    Ort::Value& e3 = encOut[outIdx(encoder_, "e3")];
    Ort::Value& emb = encOut[outIdx(encoder_, "emb")];
    Ort::Value& c0 = encOut[outIdx(encoder_, "c0")];
    Ort::Value& lsnrOut = encOut[outIdx(encoder_, "lsnr")];

    std::vector<std::pair<std::string, Ort::Value>> erbInputs;
    erbInputs.emplace_back("emb", wrapAsInput(mem, emb));
    erbInputs.emplace_back("e0", wrapAsInput(mem, e0));
    erbInputs.emplace_back("e1", wrapAsInput(mem, e1));
    erbInputs.emplace_back("e2", wrapAsInput(mem, e2));
    erbInputs.emplace_back("e3", wrapAsInput(mem, e3));
    std::vector<Ort::Value> erbOut = run(erbDecoder_, std::move(erbInputs));
    Ort::Value& m = erbOut[outIdx(erbDecoder_, "m")];

    std::vector<std::pair<std::string, Ort::Value>> dfInputs;
    dfInputs.emplace_back("emb", wrapAsInput(mem, emb));
    dfInputs.emplace_back("c0", wrapAsInput(mem, c0));
    std::vector<Ort::Value> dfOut = run(dfDecoder_, std::move(dfInputs));
    Ort::Value& coefs = dfOut[outIdx(dfDecoder_, "coefs")];

    // m: [1,1,fTotal,32]  -> flat index t*32+band
    // coefs: [1,fTotal,96,10] -> flat index (t*96+f)*10 + tap*2 + {0:re,1:im}
    // lsnr: [1,fTotal,1] -> flat index t
    const float* lsnrData = lsnrOut.GetTensorData<float>();
    const float* mData = m.GetTensorData<float>();
    const float* coefsData = coefs.GetTensorData<float>();

    // --- Apply ERB gains / deep filtering per output frame ---
    std::vector<std::vector<std::complex<float>>> outSpec(fStft, std::vector<std::complex<float>>(kFreqSize));
    for (int outT = 0; outT < fStft; ++outT) {
        const int encT = outT + kConvLookahead; // guaranteed <= fTotal - 1
        const float lsnr = lsnrData[encT];
        const Stages stages = applyStages(lsnr);

        std::vector<std::complex<float>> spec = nsy[outT];
        if (stages.applyGains) {
            applyInterpBandGain(spec, mData + static_cast<size_t>(encT) * kNbErb, erbFb);
        } else if (stages.applyGainZeros) {
            std::fill(spec.begin(), spec.end(), std::complex<float>(0.0f, 0.0f));
        }

        if (stages.applyDf) {
            for (int f = 0; f < kNbDf; ++f) {
                std::complex<float> acc(0.0f, 0.0f);
                for (int i = 0; i < kDfOrder; ++i) {
                    int srcT = outT + i - (kDfOrder / 2);
                    srcT = std::clamp(srcT, 0, fTotal - 1);
                    const size_t coefBase = (static_cast<size_t>(encT) * kNbDf + f) * (kDfOrder * 2) + i * 2;
                    const std::complex<float> coef(coefsData[coefBase], coefsData[coefBase + 1]);
                    acc += nsy[srcT][f] * coef;
                }
                spec[f] = acc;
            }
        }

        outSpec[outT] = std::move(spec);
    }

    // --- ISTFT and trim the analysis delay (one hop) ---
    std::vector<float> raw = istftSynthesis(outSpec, fft, window);
    std::vector<float> enhanced48k(raw.begin() + kHopSize, raw.begin() + kHopSize + N);

    if (sampleRate == kModelSampleRate) return enhanced48k;
    std::vector<float> result = resampleLinear(enhanced48k, kModelSampleRate, sampleRate);
    result.resize(input.size(), 0.0f);
    return result;
}

} // namespace tts
