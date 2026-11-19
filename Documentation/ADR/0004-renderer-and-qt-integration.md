# ADR-0004: Renderer backend and Qt integration (hardware floor: Direct3D 10.1)

- **Status:** accepted as the **direction for the viewport spike**; final acceptance requires the spike results from the owner's machine (see Validation plan). Supersedes the first draft of this ADR (OpenGL 3.3 core via `QOpenGLWidget`), which is **rejected** for the reason below.
- **Date:** 2026-11-19

## Context
The project owner develops on a machine with an Intel Core i5-560M and requires the application to support **Direct3D 10.1**. M0.5 needs a 3D viewport (cube/sphere/plane, picking, screenshot probe) inside a Qt dock layout (reference §9.4, §11.1). One backend only, justified by evidence.

## Hardware facts and their status
| Fact | Status |
|---|---|
| Owner requirement: the app must support DX 10.1 | **Stated by the owner** |
| i5-560M is a 2010 "Arrandale" dual-core CPU (2 cores / 4 threads) with an on-package Intel HD Graphics (Ironlake) iGPU that shares system memory | recollection — **UNVERIFIED** |
| That iGPU exposes Direct3D 10.1 (Shader Model 4.1) and **OpenGL 2.1 only** on Windows | recollection — **UNVERIFIED** (`k_gpu_probe` will show the real feature level) |
| It has no Vulkan support and no Direct3D 11 hardware feature level (11_0), but the Direct3D 11 *API* can drive 10.x hardware through feature levels 10_0/10_1 | recollection — **UNVERIFIED** |
| The sandbox that wrote this ADR has no Windows, no GPU, no Qt, no D3D headers | verified |

## Why the first draft was rejected
OpenGL 3.3 core requires a GL 3.x-class driver. If the target iGPU stops at OpenGL 2.1 (as recalled), context creation would fail on the owner's own machine. Whether that is true is cheap to confirm, but the owner's stated requirement (D3D 10.1) already points to a Direct3D backend.

## Options
1. **Direct3D 11 API at feature level 10_1** (request 11_0, 10_1, 10_0), own native child window inside a Qt Widgets window. One API covers 10.x and newer hardware; Windows only.
2. **Direct3D 10.1 API directly.** Same hardware reach, but the D3D10 API is legacy/frozen; moving to D3D11 later costs a rewrite.
3. OpenGL 3.3 core (`QOpenGLWidget`). **Rejected** (above).
4. OpenGL 2.1 fallback. Reaches the hardware but needs a separate, older shader/pipeline path; poor foundation.
5. **Qt RHI / Qt Quick.** Couples the renderer to Qt, and the *minimum Direct3D feature level its D3D11 backend accepts is unknown to me* — **UNVERIFIED**; it could reject 10_1 hardware. Avoided by using **Qt Widgets only** (CPU-rasterized UI needs no GPU API).
6. Vulkan. Not available on this hardware class (recollection, UNVERIFIED).
7. Software-only (WARP). Kept only as an automatic fallback, not as the plan.

## Decision
- **Renderer: Direct3D 11 API, minimum feature level 10_0, target 10_1** (Shader Model 4.0/4.1). Implementation in `Editor/Spike/D3D11Renderer.*` (no Qt types), hosted by a Qt Widgets `QWidget` with its own native window (`WA_NativeWindow`, `WA_PaintOnScreen`) that owns the swap chain.
- **Engine core stays API-neutral.** The boundary is `DrawList` (`Engine/Rendering/DrawList.h`): a flat list of {object, kind, world matrix, selected}. A backend never reads the `Scene`. A second backend (for a non-Windows target) can be added later behind the same boundary; none is planned now.
- **Low-end profile (design constraints, not yet measured):** shaders compile for `vs_4_0/ps_4_0`; no compute, tessellation or geometry shaders; no MSAA; 32-bit indices; D24S8 depth; classic bit-blt swap chain (widest driver support on old hardware); **render on demand** (draw only when something changed, vsync on); no per-frame resource creation (dynamic constant buffers only); modest default window size. Keep texture sizes ≤ 4096 in the editor until the real limits are measured (feature level 10 guarantees 8192).
- **Qt usage:** Widgets only for the Editor UI. Do **not** use Qt Quick, QRhi or `QOpenGLWidget` in the Editor.
- **Screenshot:** read-back of the back buffer into a staging texture before `Present`, saved by Qt as PNG; reported honestly (the probe never claims a screenshot it did not take).
- **Picking:** CPU ray casting against exact primitives (`Engine/Rendering/Picking`), no GPU readback.

## Evidence
- **Verified here (Linux/GCC):** everything that feeds the renderer — `DrawList`, primitive meshes (closed, consistently wound, outward normals), orbit camera, projection/view math with Direct3D clip depth [0,1], picking — covered by unit tests and mutation checks (see TESTING.md).
- **Checked only at stub level:** `D3D11Renderer.cpp` and `gpu_probe.cpp` pass a C++ syntax check against hand-written stub headers. That catches typos and type errors; it does **not** validate Direct3D API signatures, shader code, or behaviour.
- **Never compiled or run:** all Windows/Direct3D/Qt code, including `ViewportWidget` and `main.cpp`.

## Trade-offs
Windows-only viewport (the owner's platform) in exchange for working on the target hardware. Linux/macOS Editors would need a second backend and are out of scope for M0.5. Direct3D 10.1-class hardware is slow: the profile above is deliberately conservative.

## Consequences
- `Engine/*` remains portable and Qt-free; only `Editor/` is Windows-specific.
- Shader math follows ADR-0010 (row-major upload, `row_major float4x4`, `mul(M, v)`).
- Light normals in the spike shader are exact only for uniform scale; correct lighting under non-uniform scale (inverse-transpose) is a known limitation to fix before the real viewport.

## Migration / rollback
Replace the backend behind `DrawList`; scene files, the Scene model, picking and the camera are unaffected.

## Validation plan (owner's machine; each step is a pass/fail with a recorded output)
1. Build and run `k_gpu_probe` (needs only a C++ compiler + Windows SDK/MinGW headers). Record adapter name, feature level, memory. **This confirms or refutes the hardware table above.**
2. Build and run `k_viewport_spike`; the window shows ground, cube, sphere and a child cube, orbit/pan/zoom work, clicking highlights an object in orange.
3. `k_viewport_spike --screenshot out.png` writes a non-blank PNG and exits 0.
4. Record OS version, GPU driver version, Qt version/kit and compiler in `DEVELOPMENT_STATUS.md`. If any step fails, fix this ADR before writing more viewport code.

## Open questions for the owner
1. Windows version (Qt 6 may not support Windows 7/8 — **UNVERIFIED**; if Windows 7 must be supported, the Qt choice needs review)?
2. Qt kit: MSVC or MinGW (affects the D3D compiler import library name and warnings)?
3. Is a non-Windows viewport ever required?
