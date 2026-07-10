#pragma once

#include <string>

#include <onnxruntime_cxx_api.h>

namespace tts {

struct GpuDeviceInfo {
    // DXGI adapter index suitable for OrtSessionOptionsAppendExecutionProvider_DML,
    // or -1 if no usable hardware adapter was found.
    int deviceId = -1;
    std::string name;
};

// Enumerates DXGI adapters and picks the one with the most dedicated video
// memory, skipping software/WARP adapters. On a system with both a discrete
// GPU (e.g. AMD/NVIDIA) and an integrated GPU (e.g. Intel HD), the discrete
// GPU is preferred. Returns deviceId == -1 if DXGI enumeration fails or no
// hardware adapter is found.
GpuDeviceInfo selectBestDmlDevice();

// Attempts to append the DirectML execution provider (using the adapter from
// selectBestDmlDevice) to `opts` and disables memory pattern reuse as required
// by the DML EP. Returns true and sets *gpuName on success. Returns false
// (leaving `opts` unmodified) if no GPU is available, DML EP creation throws,
// or isGpuPreferred() is false.
//
// If disableMetacommands is true, sets the "ep.dml.disable_metacommands"
// session config entry before registering the EP. This forces DML to use its
// generic compute-shader kernels instead of driver-supplied metacommands,
// which works around E_INVALIDARG crashes some older/buggy GPU drivers throw
// for specific op configurations (e.g. certain ConvTranspose shapes), at some
// cost to performance.
bool tryEnableDml(Ort::SessionOptions& opts, std::string* gpuName, bool disableMetacommands = false);

// Attempts CUDA EP when a CUDA-enabled ONNX Runtime build is present.
// Returns true and sets *gpuName on success; false leaves opts unchanged.
bool tryEnableCuda(Ort::SessionOptions& opts, std::string* gpuName);

// Tries CUDA first, then DirectML. Returns true when either EP is attached.
bool tryEnableBestGpu(Ort::SessionOptions& opts, std::string* gpuName, bool disableMetacommands = false);

// Runtime user preference for whether engines should attempt DirectML GPU
// acceleration. Defaults to true unless EDGETTS_FORCE_CPU is set in the
// environment at startup. Changing this only affects engines constructed
// afterward - existing sessions keep whatever execution provider they were
// built with.
void setGpuPreferred(bool preferred);
bool isGpuPreferred();

} // namespace tts
