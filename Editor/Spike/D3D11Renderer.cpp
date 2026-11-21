// STATUS: UNVERIFIED — see D3D11Renderer.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>

#include "Editor/Spike/ComPtr.h"
#include "Editor/Spike/D3D11Renderer.h"
#include "Engine/Rendering/Primitives.h"

namespace k {

namespace {

Error hr_error(const char* what, HRESULT hr) {
    char buf[64];
    std::snprintf(buf, sizeof buf, " (HRESULT 0x%08lX)", static_cast<unsigned long>(hr));
    return make_error(std::string("d3d11.") + what, std::string(what) + " failed" + buf);
}

std::string wide_to_utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(static_cast<std::size_t>(n - 1));
    return s;
}

const char* feature_level_name(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        default: return "unknown (below 10_0)";
    }
}

// Shader Model 4.0 HLSL: valid on feature level 10_0 and above.
// Matrices are uploaded exactly as the CPU stores them (row-major), see ADR-0010.
const char* kShaderSource = R"HLSL(
cbuffer Frame : register(b0) { row_major float4x4 viewProj; };
cbuffer Object : register(b1) { row_major float4x4 world; float4 color; };
struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_POSITION; float3 nrm : TEXCOORD0; };
VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = mul(viewProj, mul(world, float4(i.pos, 1.0)));
    o.nrm = mul((float3x3)world, i.nrm);   // exact for uniform scale only (spike limitation)
    return o;
}
float4 PSMain(VSOut i) : SV_Target {
    float3 n = normalize(i.nrm);
    float3 l = normalize(float3(0.4, 0.8, 0.5));
    float diff = saturate(dot(n, l));
    return float4(color.rgb * (0.25 + 0.75 * diff), 1.0);
}
)HLSL";

struct FrameCB  { float view_proj[16]; };
struct ObjectCB { float world[16]; float color[4]; };

struct GpuMesh {
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Buffer> indices;
    UINT index_count = 0;
};

}  // namespace

struct D3D11Renderer::Impl {
    GpuInfo info;
    bool ready = false;
    int width = 0, height = 0;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11Texture2D> depth_tex;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> frame_cb, object_cb;
    ComPtr<ID3D11RasterizerState> raster_cull, raster_nocull;
    ComPtr<ID3D11DepthStencilState> depth_state;
    GpuMesh cube, sphere, plane;

    Status create_targets(int w, int h) {
        rtv.reset(); dsv.reset(); depth_tex.reset();
        ComPtr<ID3D11Texture2D> back;
        HRESULT hr = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.put()));
        if (FAILED(hr)) return hr_error("GetBuffer", hr);
        hr = device->CreateRenderTargetView(back.get(), nullptr, rtv.put());
        if (FAILED(hr)) return hr_error("CreateRenderTargetView", hr);

        D3D11_TEXTURE2D_DESC d = {};
        d.Width = static_cast<UINT>(w);
        d.Height = static_cast<UINT>(h);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = device->CreateTexture2D(&d, nullptr, depth_tex.put());
        if (FAILED(hr)) return hr_error("CreateTexture2D(depth)", hr);
        hr = device->CreateDepthStencilView(depth_tex.get(), nullptr, dsv.put());
        if (FAILED(hr)) return hr_error("CreateDepthStencilView", hr);
        width = w;
        height = h;
        return Status::success();
    }

    Status make_mesh(ObjectKind kind, GpuMesh& out) {
        const MeshData* m = mesh_for(kind);
        if (!m) return Status::success();
        D3D11_BUFFER_DESC bd = {};
        D3D11_SUBRESOURCE_DATA sd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = static_cast<UINT>(m->vertices.size() * sizeof(MeshVertex));
        sd.pSysMem = m->vertices.data();
        HRESULT hr = device->CreateBuffer(&bd, &sd, out.vertices.put());
        if (FAILED(hr)) return hr_error("CreateBuffer(vertices)", hr);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = static_cast<UINT>(m->indices.size() * sizeof(std::uint32_t));
        sd.pSysMem = m->indices.data();
        hr = device->CreateBuffer(&bd, &sd, out.indices.put());
        if (FAILED(hr)) return hr_error("CreateBuffer(indices)", hr);
        out.index_count = static_cast<UINT>(m->indices.size());
        return Status::success();
    }

    Status create_pipeline() {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        ComPtr<ID3DBlob> vs_blob, ps_blob, errors;
        HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "k_spike.hlsl", nullptr, nullptr,
                                "VSMain", "vs_4_0", flags, 0, vs_blob.put(), errors.put());
        if (FAILED(hr)) {
            std::string msg = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "";
            return make_error("d3d11.shader_compile", "vertex shader compile failed: " + msg);
        }
        hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "k_spike.hlsl", nullptr, nullptr,
                        "PSMain", "ps_4_0", flags, 0, ps_blob.put(), errors.put());
        if (FAILED(hr)) {
            std::string msg = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "";
            return make_error("d3d11.shader_compile", "pixel shader compile failed: " + msg);
        }
        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, vs.put());
        if (FAILED(hr)) return hr_error("CreateVertexShader", hr);
        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, ps.put());
        if (FAILED(hr)) return hr_error("CreatePixelShader", hr);

        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = device->CreateInputLayout(elements, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), layout.put());
        if (FAILED(hr)) return hr_error("CreateInputLayout", hr);

        D3D11_BUFFER_DESC cb = {};
        cb.Usage = D3D11_USAGE_DYNAMIC;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cb.ByteWidth = sizeof(FrameCB);       // 64: multiple of 16 as required
        hr = device->CreateBuffer(&cb, nullptr, frame_cb.put());
        if (FAILED(hr)) return hr_error("CreateBuffer(frame cb)", hr);
        cb.ByteWidth = sizeof(ObjectCB);      // 80: multiple of 16
        hr = device->CreateBuffer(&cb, nullptr, object_cb.put());
        if (FAILED(hr)) return hr_error("CreateBuffer(object cb)", hr);

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.FrontCounterClockwise = TRUE;      // engine convention: CCW is the front face (ADR-0010)
        rd.DepthClipEnable = TRUE;
        hr = device->CreateRasterizerState(&rd, raster_cull.put());
        if (FAILED(hr)) return hr_error("CreateRasterizerState", hr);
        rd.CullMode = D3D11_CULL_NONE;        // planes are double-sided
        hr = device->CreateRasterizerState(&rd, raster_nocull.put());
        if (FAILED(hr)) return hr_error("CreateRasterizerState(nocull)", hr);

        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dd.DepthFunc = D3D11_COMPARISON_LESS;
        hr = device->CreateDepthStencilState(&dd, depth_state.put());
        if (FAILED(hr)) return hr_error("CreateDepthStencilState", hr);

        if (Status st = make_mesh(ObjectKind::Cube, cube); !st) return st;
        if (Status st = make_mesh(ObjectKind::Sphere, sphere); !st) return st;
        if (Status st = make_mesh(ObjectKind::Plane, plane); !st) return st;
        return Status::success();
    }

    template <class CB>
    bool update_cb(ID3D11Buffer* buf, const CB& data) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(context->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        std::memcpy(mapped.pData, &data, sizeof(CB));
        context->Unmap(buf, 0);
        return true;
    }
};

D3D11Renderer::D3D11Renderer() : impl_(std::make_unique<Impl>()) {}
D3D11Renderer::~D3D11Renderer() = default;
bool D3D11Renderer::initialized() const { return impl_->ready; }
const GpuInfo& D3D11Renderer::info() const { return impl_->info; }

Status D3D11Renderer::initialize(void* hwnd_void, int width, int height, bool allow_warp) {
    Impl& s = *impl_;
    if (width <= 0 || height <= 0) return make_error("d3d11.bad_size", "window size must be positive");
    HWND hwnd = static_cast<HWND>(hwnd_void);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = static_cast<UINT>(width);
    sd.BufferDesc.Height = static_cast<UINT>(height);
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;   // classic model: widest driver support, incl. old hardware

    // 11_1 is deliberately not requested: it makes CreateDevice fail with E_INVALIDARG on
    // systems without the D3D 11.1 runtime.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_10_0;
    s.info.driver_type = "hardware";
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3, D3D11_SDK_VERSION,
                                               &sd, s.swap_chain.put(), s.device.put(), &achieved, s.context.put());
    if (FAILED(hr) && allow_warp) {
        s.info.driver_type = "WARP (software)";
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 3, D3D11_SDK_VERSION,
                                           &sd, s.swap_chain.put(), s.device.put(), &achieved, s.context.put());
    }
    if (FAILED(hr)) return hr_error("D3D11CreateDeviceAndSwapChain", hr);
    s.info.feature_level = feature_level_name(achieved);

    // Adapter description.
    ComPtr<IDXGIDevice> dxgi_device;
    if (SUCCEEDED(s.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi_device.put())))) {
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgi_device->GetAdapter(adapter.put()))) {
            DXGI_ADAPTER_DESC ad = {};
            if (SUCCEEDED(adapter->GetDesc(&ad))) {
                s.info.adapter_name = wide_to_utf8(ad.Description);
                s.info.vendor_id = ad.VendorId;
                s.info.device_id = ad.DeviceId;
                s.info.dedicated_video_mb = static_cast<std::uint64_t>(ad.DedicatedVideoMemory) / (1024 * 1024);
                s.info.shared_system_mb = static_cast<std::uint64_t>(ad.SharedSystemMemory) / (1024 * 1024);
            }
        }
    }

    if (Status st = s.create_targets(width, height); !st) return st;
    if (Status st = s.create_pipeline(); !st) return st;
    s.ready = true;
    return Status::success();
}

Status D3D11Renderer::resize(int width, int height) {
    Impl& s = *impl_;
    if (!s.ready) return make_error("d3d11.not_initialized", "renderer is not initialized");
    if (width <= 0 || height <= 0) return Status::success();   // minimized: keep the old buffers
    if (width == s.width && height == s.height) return Status::success();
    s.context->OMSetRenderTargets(0, nullptr, nullptr);        // release references before ResizeBuffers
    s.rtv.reset(); s.dsv.reset(); s.depth_tex.reset();
    const HRESULT hr = s.swap_chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return make_error("d3d11.resize_failed", hr_error("ResizeBuffers", hr).message);
    return s.create_targets(width, height);
}

Status D3D11Renderer::render(const std::vector<DrawItem>& items, const Mat4& view, const Mat4& projection,
                             CapturedImage* capture) {
    Impl& s = *impl_;
    if (!s.ready) return make_error("d3d11.not_initialized", "renderer is not initialized");

    const float clear[4] = {0.12f, 0.13f, 0.15f, 1.0f};
    ID3D11RenderTargetView* rtv = s.rtv.get();
    s.context->OMSetRenderTargets(1, &rtv, s.dsv.get());
    s.context->ClearRenderTargetView(rtv, clear);
    s.context->ClearDepthStencilView(s.dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(s.width), static_cast<float>(s.height), 0.0f, 1.0f};
    s.context->RSSetViewports(1, &vp);
    s.context->OMSetDepthStencilState(s.depth_state.get(), 0);
    s.context->IASetInputLayout(s.layout.get());
    s.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.context->VSSetShader(s.vs.get(), nullptr, 0);
    s.context->PSSetShader(s.ps.get(), nullptr, 0);

    FrameCB fcb;
    const Mat4 vpm = projection * view;
    for (std::size_t i = 0; i < 16; ++i) fcb.view_proj[i] = static_cast<float>(vpm.m[i]);   // row-major, unchanged
    if (!s.update_cb(s.frame_cb.get(), fcb)) return make_error("d3d11.map_failed", "cannot update frame constant buffer");
    ID3D11Buffer* cbs[2] = {s.frame_cb.get(), s.object_cb.get()};
    s.context->VSSetConstantBuffers(0, 2, cbs);
    // The pixel shader reads `color` from the Object buffer (b1): bind it to the PS stage as well,
    // otherwise the shader sees zeros and every object renders black (found on real hardware).
    s.context->PSSetConstantBuffers(1, 1, &cbs[1]);

    for (const DrawItem& item : items) {
        const GpuMesh* mesh = item.kind == ObjectKind::Cube ? &s.cube : item.kind == ObjectKind::Sphere ? &s.sphere
                            : item.kind == ObjectKind::Plane ? &s.plane : nullptr;
        if (!mesh || !mesh->vertices) continue;
        ObjectCB ocb;
        std::memcpy(ocb.world, item.world, sizeof ocb.world);
        const float base[4] = {0.55f, 0.70f, 0.90f, 1.0f}, sel[4] = {1.0f, 0.60f, 0.15f, 1.0f};
        std::memcpy(ocb.color, item.selected ? sel : base, sizeof ocb.color);
        if (!s.update_cb(s.object_cb.get(), ocb)) return make_error("d3d11.map_failed", "cannot update object constant buffer");
        s.context->RSSetState(item.kind == ObjectKind::Plane ? s.raster_nocull.get() : s.raster_cull.get());
        ID3D11Buffer* vb = mesh->vertices.get();
        const UINT stride = sizeof(MeshVertex), offset = 0;
        s.context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        s.context->IASetIndexBuffer(mesh->indices.get(), DXGI_FORMAT_R32_UINT, 0);
        s.context->DrawIndexed(mesh->index_count, 0, 0);
    }

    if (capture) {
        // Read back BEFORE Present: with the discard swap effect the back buffer is undefined afterwards.
        ComPtr<ID3D11Texture2D> back, staging;
        HRESULT hr = s.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.put()));
        if (FAILED(hr)) return hr_error("GetBuffer(capture)", hr);
        D3D11_TEXTURE2D_DESC td = {};
        back->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.MiscFlags = 0;
        hr = s.device->CreateTexture2D(&td, nullptr, staging.put());
        if (FAILED(hr)) return hr_error("CreateTexture2D(staging)", hr);
        s.context->CopyResource(staging.get(), back.get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = s.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return hr_error("Map(staging)", hr);
        capture->width = static_cast<int>(td.Width);
        capture->height = static_cast<int>(td.Height);
        capture->rgba.assign(static_cast<std::size_t>(td.Width) * td.Height * 4, 0);
        for (UINT row = 0; row < td.Height; ++row) {
            std::memcpy(capture->rgba.data() + static_cast<std::size_t>(row) * td.Width * 4,
                        static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
                        static_cast<std::size_t>(td.Width) * 4);
        }
        s.context->Unmap(staging.get(), 0);
    }

    const HRESULT hr = s.swap_chain->Present(1, 0);   // vsync: keeps CPU/GPU load low on weak hardware
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) return hr_error("Present", hr);
    return Status::success();
}

}  // namespace k
