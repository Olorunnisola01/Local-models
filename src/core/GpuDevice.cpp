#include "GpuDevice.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdlib>

#include <windows.h>

#include <dxgi1_4.h>

#include <dml_provider_factory.h>

namespace tts {

namespace {

std::string wideToUtf8(const wchar_t* wide) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// Seeded from EDGETTS_FORCE_CPU so the existing escape hatch still works as
// the initial default; overridden at runtime by the UI's GPU/CPU toggle.
std::atomic<bool> g_gpuPreferred{std::getenv("EDGETTS_FORCE_CPU") == nullptr};

} // namespace

void setGpuPreferred(bool preferred) { g_gpuPreferred.store(preferred); }
bool isGpuPreferred() { return g_gpuPreferred.load(); }

GpuDeviceInfo selectBestDmlDevice() {
    GpuDeviceInfo result;

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return result;
    }

    UINT64 bestVram = 0;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            if (result.deviceId < 0 || desc.DedicatedVideoMemory > bestVram) {
                bestVram = desc.DedicatedVideoMemory;
                result.deviceId = static_cast<int>(i);
                result.name = wideToUtf8(desc.Description);
            }
        }
        adapter->Release();
    }

    factory->Release();
    return result;
}

bool tryEnableCuda(Ort::SessionOptions& opts, std::string* gpuName) {
    if (!isGpuPreferred()) {
        return false;
    }
    // Vendored win-x64 ORT builds may omit CUDA; resolve at runtime to avoid link errors.
    HMODULE module = GetModuleHandleW(L"onnxruntime.dll");
    if (!module) {
        return false;
    }
    using AppendCudaFn = OrtStatus* (*)(OrtSessionOptions*, int);
    auto* appendCuda = reinterpret_cast<AppendCudaFn>(
        GetProcAddress(module, "OrtSessionOptionsAppendExecutionProvider_CUDA"));
    if (!appendCuda) {
        return false;
    }
    try {
        Ort::ThrowOnError(appendCuda(opts, 0));
        if (gpuName) {
            *gpuName = "CUDA GPU 0";
        }
        return true;
    } catch (const Ort::Exception&) {
        return false;
    }
}

bool tryEnableBestGpu(Ort::SessionOptions& opts, std::string* gpuName, bool disableMetacommands) {
    if (tryEnableCuda(opts, gpuName)) {
        return true;
    }
    return tryEnableDml(opts, gpuName, disableMetacommands);
}

bool tryEnableDml(Ort::SessionOptions& opts, std::string* gpuName, bool disableMetacommands) {
    if (!isGpuPreferred()) {
        return false;
    }

    GpuDeviceInfo gpu = selectBestDmlDevice();
    if (gpu.deviceId < 0) {
        return false;
    }

    try {
        if (disableMetacommands) {
            Ort::ThrowOnError(Ort::GetApi().AddSessionConfigEntry(opts, "ep.dml.disable_metacommands", "1"));
        }
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(opts, gpu.deviceId));
        opts.DisableMemPattern(); // required by the DirectML EP
        if (gpuName) *gpuName = gpu.name;
        return true;
    } catch (const Ort::Exception&) {
        return false;
    }
}

} // namespace tts
