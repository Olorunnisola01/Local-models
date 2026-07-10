#include "Humanizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "BiquadFilter.h"
#include "Normalizer.h"

// Naturalness DSP "Humanisation" pipeline.
//
// Ported from the Python reference (EdgeTTS-Studio app.py: apply_naturalness_pipeline
// + apply_chirp_hd_polish). The previous native implementation only applied a
// +-0.35 dB gain jitter, which is inaudible — this replaces it with the full chain
// that actually de-robotises Microsoft Edge voices:
//
//   1. De-Robot EQ      warmth low-shelf + harsh-mid cut + air high-shelf
//   2. Compression      gentle 3:1 glue compressor with makeup gain
//   3. Subtle Reverb    Schroeder-lite room at ~8% wet for natural "space"
//   4. De-Esser         split-band dynamic notch tames synthetic sibilance
//   5. Loudness         RMS normalise toward a broadcast-ish target
//   6. Safety ceiling   -1 dBFS peak limiter (reuses peakNormalize)
//
// All stages are click-free and deterministic.

namespace tts {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Runs a single biquad section over the whole buffer (in place).
void runBiquad(std::vector<float>& x, const BiquadCoeffs& c) {
    BiquadFilter f;
    f.setCoeffs(c);
    f.reset();
    for (float& s : x) {
        s = f.processSample(s);
    }
}

// --- Stage 1: De-Robotising EQ -------------------------------------------------
// Low-shelf warmth ~195 Hz, gentle mid cut ~2.1 kHz (removes the "boxy/robotic"
// resonance), high-shelf air ~5.9 kHz. Same curve as the reference Stage 3.
// Each band is skipped when its gain is ~0 dB so a flat setting is a true bypass.
void applyDeRobotEq(std::vector<float>& x, float sr, float warmthDb, float midCutDb, float airDb) {
    if (std::fabs(warmthDb) > 0.05f) {
        runBiquad(x, makeLowShelf(sr, 195.0f, warmthDb, 0.8f));
    }
    if (std::fabs(midCutDb) > 0.05f) {
        runBiquad(x, makePeakingEq(sr, 2100.0f, midCutDb, 1.15f));
    }
    if (std::fabs(airDb) > 0.05f) {
        runBiquad(x, makeHighShelf(sr, 5900.0f, airDb, 0.65f));
    }
}

// --- Stage 2: Gentle dynamic compression --------------------------------------
// Feed-forward compressor with a peak envelope follower in the dB domain.
void applyCompressor(std::vector<float>& x, float sr, float thresholdDb, float ratio,
                     float attackMs, float releaseMs, float makeupDb) {
    if (x.empty()) {
        return;
    }
    const float attackCoef = std::exp(-1.0f / (sr * (attackMs / 1000.0f)));
    const float releaseCoef = std::exp(-1.0f / (sr * (releaseMs / 1000.0f)));
    const float makeup = std::pow(10.0f, makeupDb / 20.0f);
    constexpr float eps = 1e-9f;

    float env = 0.0f;
    for (float& s : x) {
        const float a = std::fabs(s);
        const float coef = (a > env) ? attackCoef : releaseCoef;
        env = coef * env + (1.0f - coef) * a;

        const float envDb = 20.0f * std::log10(env + eps);
        const float over = envDb - thresholdDb;
        const float grDb = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
        const float gain = std::pow(10.0f, grDb / 20.0f);
        s = s * gain * makeup;
    }
}

// --- Stage 3: Subtle Schroeder-lite reverb ------------------------------------
// 4 parallel damped feedback combs -> 2 series allpasses, mixed in at a low wet
// level so it reads as natural room ambience rather than obvious reverb.
class FeedbackComb {
public:
    void init(int delaySamples, float feedback, float damping) {
        buf_.assign(std::max(1, delaySamples), 0.0f);
        idx_ = 0;
        feedback_ = feedback;
        damping_ = damping;
        store_ = 0.0f;
    }
    float process(float in) {
        const float out = buf_[idx_];
        store_ = out * (1.0f - damping_) + store_ * damping_;
        buf_[idx_] = in + store_ * feedback_;
        if (++idx_ >= static_cast<int>(buf_.size())) {
            idx_ = 0;
        }
        return out;
    }

private:
    std::vector<float> buf_;
    int idx_ = 0;
    float feedback_ = 0.0f;
    float damping_ = 0.0f;
    float store_ = 0.0f;
};

class Allpass {
public:
    void init(int delaySamples, float feedback) {
        buf_.assign(std::max(1, delaySamples), 0.0f);
        idx_ = 0;
        feedback_ = feedback;
    }
    float process(float in) {
        const float bufout = buf_[idx_];
        const float out = -in + bufout;
        buf_[idx_] = in + bufout * feedback_;
        if (++idx_ >= static_cast<int>(buf_.size())) {
            idx_ = 0;
        }
        return out;
    }

private:
    std::vector<float> buf_;
    int idx_ = 0;
    float feedback_ = 0.0f;
};

void applyReverb(std::vector<float>& x, float sr, float wetLevel) {
    if (x.empty() || wetLevel <= 0.0f) {
        return;
    }
    // Freeverb comb/allpass tunings (samples @44.1 kHz), scaled to this sample rate.
    const float scale = sr / 44100.0f;
    auto sc = [scale](int n) { return std::max(1, static_cast<int>(std::lround(n * scale))); };

    FeedbackComb combs[4];
    combs[0].init(sc(1116), 0.80f, 0.5f);
    combs[1].init(sc(1188), 0.80f, 0.5f);
    combs[2].init(sc(1277), 0.80f, 0.5f);
    combs[3].init(sc(1356), 0.80f, 0.5f);

    Allpass aps[2];
    aps[0].init(sc(556), 0.5f);
    aps[1].init(sc(441), 0.5f);

    const float dryLevel = std::max(0.0f, 1.0f - wetLevel - 0.02f);
    for (float& s : x) {
        const float in = s * 0.30f; // input gain into the reverb tank
        float wet = 0.0f;
        for (FeedbackComb& c : combs) {
            wet += c.process(in);
        }
        for (Allpass& a : aps) {
            wet = a.process(wet);
        }
        s = s * dryLevel + wet * wetLevel;
    }
}

// --- Stage 4: Split-band dynamic de-esser -------------------------------------
// RBJ band-pass (constant 0 dB peak) to isolate sibilance, fast envelope-driven
// gain reduction only on that band, then recombined. Same idea as the reference
// _split_band_deesser.
BiquadCoeffs makeBandpass(float sr, float freq, float q) {
    const float w0 = 2.0f * kPi * freq / sr;
    const float cw0 = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    BiquadCoeffs c;
    c.b0 = alpha / a0;
    c.b1 = 0.0f;
    c.b2 = -alpha / a0;
    c.a1 = (-2.0f * cw0) / a0;
    c.a2 = (1.0f - alpha) / a0;
    return c;
}

void applyDeEsser(std::vector<float>& x, float sr, float centerHz, float bandwidthHz,
                  float thresholdDb, float ratio, float attackMs, float releaseMs) {
    if (x.empty()) {
        return;
    }
    const float q = std::max(0.3f, centerHz / std::max(1.0f, bandwidthHz));

    // Extract the sibilant band.
    std::vector<float> sib = x;
    runBiquad(sib, makeBandpass(sr, centerHz, q));

    const float attackCoef = std::exp(-1.0f / (sr * (attackMs / 1000.0f)));
    const float releaseCoef = std::exp(-1.0f / (sr * (releaseMs / 1000.0f)));
    constexpr float eps = 1e-9f;

    float env = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        const float a = std::fabs(sib[i]);
        const float coef = (a > env) ? attackCoef : releaseCoef;
        env = coef * env + (1.0f - coef) * a;

        const float envDb = 20.0f * std::log10(env + eps);
        const float over = envDb - thresholdDb;
        const float grDb = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
        const float gain = std::pow(10.0f, grDb / 20.0f);

        // rest = full - sibilant; recombine with the band attenuated.
        const float rest = x[i] - sib[i];
        x[i] = rest + sib[i] * gain;
    }
}

// --- Stage 5: RMS loudness normalisation --------------------------------------
// Approximate, K-weighting-free loudness match toward a speech-friendly target.
void applyLoudnessNormalize(std::vector<float>& x, float targetRmsDb) {
    if (x.empty()) {
        return;
    }
    double sumSq = 0.0;
    for (float s : x) {
        sumSq += static_cast<double>(s) * s;
    }
    const float rms = static_cast<float>(std::sqrt(sumSq / x.size()));
    if (rms < 1e-6f) {
        return;
    }
    const float rmsDb = 20.0f * std::log10(rms);
    float gainDb = targetRmsDb - rmsDb;
    gainDb = std::clamp(gainDb, -12.0f, 12.0f); // never over-correct
    const float gain = std::pow(10.0f, gainDb / 20.0f);
    for (float& s : x) {
        s *= gain;
    }
}

} // namespace

void applyHumanizer(AudioBuffer& buf, const HumanizerSettings& s) {
    if (buf.samples.empty() || buf.sampleRate <= 0) {
        return;
    }
    const float sr = static_cast<float>(buf.sampleRate);
    std::vector<float>& x = buf.samples;

    // 1) De-robotising EQ — the single biggest contributor to naturalness.
    if (s.eqEnabled) {
        applyDeRobotEq(x, sr, s.eqWarmthDb, s.eqMidCutDb, s.eqAirDb);
    }

    // 2) Gentle glue compression with makeup gain.
    if (s.compEnabled) {
        applyCompressor(x, sr, s.compThresholdDb, std::max(1.0f, s.compRatio),
                        /*attackMs*/ 10.0f, /*releaseMs*/ 150.0f, s.compMakeupDb);
    }

    // 3) Subtle room reverb for natural ambience.
    if (s.reverbEnabled) {
        applyReverb(x, sr, std::clamp(s.reverbWet, 0.0f, 0.5f));
    }

    // 4) Split-band de-esser to tame synthetic S/T/Z sibilance.
    if (s.deEsserEnabled) {
        applyDeEsser(x, sr, /*centerHz*/ 7500.0f, /*bandwidthHz*/ 5000.0f,
                     s.deEsserThreshDb, std::max(1.0f, s.deEsserRatio),
                     /*attackMs*/ 3.0f, /*releaseMs*/ 80.0f);
    }

    // 5) Loudness match toward a consistent speech level.
    if (s.loudnessEnabled) {
        applyLoudnessNormalize(x, s.loudnessTargetDb);
    }

    // 6) Brick-wall safety ceiling (scales down only; never clips).
    if (s.ceilingEnabled) {
        peakNormalize(x, s.ceilingDb);
    }
}

} // namespace tts
