#include "Fft.h"

#include <cmath>

namespace tts {

namespace {
constexpr double kPi = 3.14159265358979323846;

int nextPow2(int x) {
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}
}  // namespace

std::vector<std::complex<float>> Fft::radix2Fft(std::vector<std::complex<float>> a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const std::complex<float> wlen(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (auto& x : a) x /= static_cast<float>(n);
    }
    return a;
}

Fft::Fft(int n) : n_(n) {
    m_ = nextPow2(2 * n - 1);

    chirp_.resize(n);
    for (int k = 0; k < n; ++k) {
        const double angle = -kPi * static_cast<double>(k) * static_cast<double>(k) / static_cast<double>(n);
        chirp_[k] = std::complex<float>(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
    }

    std::vector<std::complex<float>> b(m_, std::complex<float>(0.0f, 0.0f));
    b[0] = std::conj(chirp_[0]);
    for (int k = 1; k < n; ++k) {
        const std::complex<float> bk = std::conj(chirp_[k]);
        b[k] = bk;
        b[m_ - k] = bk;
    }
    bFreq_ = radix2Fft(std::move(b), false);
}

std::vector<std::complex<float>> Fft::forward(const std::vector<std::complex<float>>& x) const {
    std::vector<std::complex<float>> a(m_, std::complex<float>(0.0f, 0.0f));
    for (int k = 0; k < n_; ++k) a[k] = x[k] * chirp_[k];

    auto af = radix2Fft(std::move(a), false);
    for (int i = 0; i < m_; ++i) af[i] *= bFreq_[i];
    auto c = radix2Fft(std::move(af), true);

    std::vector<std::complex<float>> out(n_);
    for (int k = 0; k < n_; ++k) out[k] = c[k] * chirp_[k];
    return out;
}

std::vector<std::complex<float>> Fft::inverse(const std::vector<std::complex<float>>& X) const {
    std::vector<std::complex<float>> conjX(n_);
    for (int k = 0; k < n_; ++k) conjX[k] = std::conj(X[k]);

    const auto y = forward(conjX);

    std::vector<std::complex<float>> out(n_);
    for (int j = 0; j < n_; ++j) out[j] = std::conj(y[j]) / static_cast<float>(n_);
    return out;
}

}  // namespace tts
