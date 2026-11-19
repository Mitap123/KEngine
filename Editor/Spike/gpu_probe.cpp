// k_gpu_probe — reports which Direct3D feature level each adapter on this
// machine supports. No Qt, no window. STATUS: UNVERIFIED (never compiled by its author).
//
// Purpose: turn the assumption "the target GPU is Direct3D 10.1 class" into evidence
// (ADR-0004). Exit code: 0 if some hardware adapter reaches feature level 10_0 or
// higher, 1 if none does (only software/WARP or nothing), 2 on a probe failure.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>

#include "Editor/Spike/ComPtr.h"

namespace {

const char* level_name(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        case D3D_FEATURE_LEVEL_9_3: return "9_3";
        case D3D_FEATURE_LEVEL_9_2: return "9_2";
        case D3D_FEATURE_LEVEL_9_1: return "9_1";
        default: return "unknown";
    }
}

// 11_1 is not requested: it makes CreateDevice fail with E_INVALIDARG where the 11.1 runtime is absent.
const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                     D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1};

}  // namespace

int main() {
    ComPtr<IDXGIFactory> factory;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(factory.put()));
    if (FAILED(hr)) {
        std::fprintf(stderr, "k_gpu_probe: CreateDXGIFactory failed (HRESULT 0x%08lX)\n", static_cast<unsigned long>(hr));
        return 2;
    }

    bool hardware_ok = false;
    ComPtr<IDXGIAdapter> adapter;
    for (UINT i = 0; factory->EnumAdapters(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc = {};
        adapter->GetDesc(&desc);
        std::printf("adapter %u: %ls\n", i, desc.Description);
        std::printf("  vendor 0x%04X device 0x%04X, dedicated video %llu MB, shared system %llu MB\n", desc.VendorId, desc.DeviceId,
                    static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)),
                    static_cast<unsigned long long>(desc.SharedSystemMemory / (1024 * 1024)));
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_9_1;
        hr = D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, kLevels,
                               static_cast<UINT>(sizeof kLevels / sizeof kLevels[0]), D3D11_SDK_VERSION,
                               device.put(), &achieved, context.put());
        if (SUCCEEDED(hr)) {
            std::printf("  Direct3D 11 device: yes, feature level %s\n", level_name(achieved));
            const bool is_microsoft_software = desc.VendorId == 0x1414;   // "Microsoft Basic Render Driver"
            if (!is_microsoft_software && achieved >= D3D_FEATURE_LEVEL_10_0) hardware_ok = true;
        } else {
            std::printf("  Direct3D 11 device: NO (HRESULT 0x%08lX)\n", static_cast<unsigned long>(hr));
        }
    }

    // WARP (Microsoft's software rasterizer): the fallback the viewport uses if hardware fails.
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_9_1;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, kLevels,
                               static_cast<UINT>(sizeof kLevels / sizeof kLevels[0]), D3D11_SDK_VERSION,
                               device.put(), &achieved, context.put());
        if (SUCCEEDED(hr)) std::printf("WARP software device: yes, feature level %s\n", level_name(achieved));
        else std::printf("WARP software device: NO (HRESULT 0x%08lX)\n", static_cast<unsigned long>(hr));
    }

    std::printf("result: %s\n", hardware_ok ? "a hardware adapter supports feature level 10_0 or higher"
                                            : "NO hardware adapter reached feature level 10_0");
    return hardware_ok ? 0 : 1;
}
