// K Engine — Editor/Spike/D3D11Renderer.h
//
// STATUS: UNVERIFIED. Written without access to a Windows toolchain; it has
// never been compiled or run by its author. See README.md for the checks.
//
// Minimal Direct3D 11 renderer that draws a DrawList (Engine/Rendering) into a
// window handle. It requests feature levels 11_0, 10_1 and 10_0 and works at
// 10_1 (Shader Model 4.1 class hardware, for example Intel HD Graphics in an
// Intel Core i5-560M): vertex/pixel shaders compiled for vs_4_0/ps_4_0, no
// compute, no tessellation, no MSAA, classic (bit-blt) swap chain, 32-bit
// indices, D24S8 depth. No Qt types appear here.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Core/Result.h"
#include "Engine/Math/Mat4.h"
#include "Engine/Rendering/DrawList.h"

namespace k {

struct GpuInfo {
    std::string adapter_name;      // e.g. "Intel(R) HD Graphics"
    std::string feature_level;     // "11_0", "10_1", "10_0"
    std::string driver_type;       // "hardware" or "WARP (software)"
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint64_t dedicated_video_mb = 0;
    std::uint64_t shared_system_mb = 0;
};

struct CapturedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4, top row first
};

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    // `hwnd` is a Win32 HWND (passed as void* to keep Windows headers out of this header).
    // If hardware device creation fails and `allow_warp` is true, the WARP software
    // rasterizer is used and GpuInfo::driver_type says so. Error codes: d3d11.*
    Status initialize(void* hwnd, int width, int height, bool allow_warp);
    bool initialized() const;
    const GpuInfo& info() const;

    // Error codes: d3d11.not_initialized, d3d11.resize_failed.
    Status resize(int width, int height);

    // Draws one frame and presents it. If `capture` is non-null the frame is also
    // read back (before Present, while the back buffer contents are defined).
    Status render(const std::vector<DrawItem>& items, const Mat4& view, const Mat4& projection,
                  CapturedImage* capture);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace k
