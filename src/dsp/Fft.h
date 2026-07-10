#pragma once

#include <complex>
#include <vector>

namespace tts {

// Arbitrary-length complex DFT/IDFT via Bluestein's algorithm, backed by a
// power-of-two radix-2 FFT. Needed because DeepFilterNet's STFT uses a
// 960-point transform (960 = 2^6 * 3 * 5, not a power of two).
class Fft {
public:
    explicit Fft(int n);

    // X[k] = sum_j x[j] * exp(-2*pi*i*j*k/n)
    std::vector<std::complex<float>> forward(const std::vector<std::complex<float>>& x) const;

    // x[j] = (1/n) * sum_k X[k] * exp(+2*pi*i*j*k/n)
    std::vector<std::complex<float>> inverse(const std::vector<std::complex<float>>& X) const;

    int size() const { return n_; }

private:
    static std::vector<std::complex<float>> radix2Fft(std::vector<std::complex<float>> a, bool inverse);

    int n_;
    int m_; // power-of-two convolution size used by Bluestein's algorithm
    std::vector<std::complex<float>> chirp_; // exp(-i*pi*k^2/n) for k=0..n-1
    std::vector<std::complex<float>> bFreq_; // FFT of the Bluestein convolution kernel
};

} // namespace tts
