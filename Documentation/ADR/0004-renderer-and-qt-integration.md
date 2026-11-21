# ADR-0004: Renderer backend and Qt integration (hardware floor: Direct3D 10.1)

- **Status:** accepted. The direction was **validated on the owner's target machine** (2026-11-19, see "Measured results"): a Direct3D 11 device on the real GPU is created and renders. The hardware floor is **feature level 10_0**, not 10_1 (the table below was corrected from the measurement). One rendering defect found on that run (black surfaces) is fixed but awaiting re-verification. Supersedes the first draft (OpenGL 3.3 core via `QOpenGLWidget`), which is **rejected**.
- **Date:** 2026-11-19

## Context
The project owner develops on a machine with an Intel Core i5-560M and requires the application to support **Direct3D 10.1**. M0.5 needs a 3D viewport (cube/sphere/plane, picking, screenshot probe) inside a Qt dock layout (reference §9.4, §11.1). One backend only, justified by evidence.

## Hardware facts and their status
| Fact | Status |
|---|---|
| Owner requirement: the app must support DX 10.1 (i.e. run on a Direct3D 10.1-class machine) | stated by the owner; satisfied by targeting a **lower** floor (below) |
| Machine: Intel Core i5-560M; Windows 10 Pro 10.0.19045; adapter "Intel(R) HD Graphics", vendor 0x8086, device **0x0046**, 64 MB dedicated + 1632 MB shared memory, driver **8.15.10.2900 (2012-11-26)** | **measured** (owner) |
| That adapter exposes **Direct3D feature level 10_0** through the Direct3D 11 runtime; 10_1 was requested and **not granted** | **measured** (`k_gpu_probe`, viewport console) |
| "Microsoft Basic Render Driver" (software) and WARP both reach feature level 11_0 | **measured** |
| The iGPU offers only OpenGL 2.1; no Vulkan | recollection, **UNVERIFIED** (not measured; does not change the decision) |

**Consequence of the measurement:** the floor is **feature level 10_0 (Shader Model 4.0)**. Everything the engine ships must run there; Shader Model 4.1 / Direct3D 10.1-only features must not be required. Requesting 10_1/11_0 first is kept so that better GPUs use them, but nothing may depend on them.

## Why the first draft was rejected
OpenGL 3.3 core requires a GL 3.x-class driver. If the target iGPU stops at OpenGL 2.1 (as recalled), context creation would fail on the owner's own machine. Whether that is true is cheap to confirm, but the owner's stated requirement (D3D 10.1) already points to a Direct3D backend.

## Options
1. **Direct3D 11 API at feature level 10_0** (request 11_0, 10_1, 10_0), own native child window inside a Qt Widgets window. One API covers 10.x and newer hardware; Windows only.
2. **Direct3D 10.1 API directly.** Same hardware reach, but the D3D10 API is legacy/frozen; moving to D3D11 later costs a rewrite.
3. OpenGL 3.3 core (`QOpenGLWidget`). **Rejected** (above).
4. OpenGL 2.1 fallback. Reaches the hardware but needs a separate, older shader/pipeline path; poor foundation.
5. **Qt RHI / Qt Quick.** Couples the renderer to Qt, and the *minimum Direct3D feature level its D3D11 backend accepts is unknown to me* — **UNVERIFIED**; it could reject 10_1 hardware. Avoided by using **Qt Widgets only** (CPU-rasterized UI needs no GPU API).
6. Vulkan. Not available on this hardware class (recollection, UNVERIFIED).
7. Software-only (WARP). Kept only as an automatic fallback, not as the plan.

## Decision
- **Renderer: Direct3D 11 API, minimum and measured-target feature level 10_0** (Shader Model 4.0). Implementation in `Editor/Spike/D3D11Renderer.*` (no Qt types), hosted by a Qt Widgets `QWidget` with its own native window (`WA_NativeWindow`, `WA_PaintOnScreen`) that owns the swap chain.
- **Engine core stays API-neutral.** The boundary is `DrawList` (`Engine/Rendering/DrawList.h`): a flat list of {object, kind, world matrix, selected}. A backend never reads the `Scene`. A second backend (for a non-Windows target) can be added later behind the same boundary; none is planned now.
- **Low-end profile (design constraints, not yet measured):** shaders compile for `vs_4_0/ps_4_0`; no compute, tessellation or geometry shaders; no MSAA; 32-bit indices; D24S8 depth; classic bit-blt swap chain (widest driver support on old hardware); **render on demand** (draw only when something changed, vsync on); no per-frame resource creation (dynamic constant buffers only); modest default window size. Keep texture sizes ≤ 4096 in the editor until the real limits are measured (feature level 10_0 guarantees 8192).
- **Qt usage:** Widgets only for the Editor UI. Do **not** use Qt Quick, QRhi or `QOpenGLWidget` in the Editor.
- **Screenshot:** read-back of the back buffer into a staging texture before `Present`, saved by Qt as PNG; reported honestly (the probe never claims a screenshot it did not take).
- **Picking:** CPU ray casting against exact primitives (`Engine/Rendering/Picking`), no GPU readback.

## Measured results (owner's machine, 2026-11-19)
Environment: Windows 10 Pro 19045, Qt 6.12.0 (msvc2022_64), MSVC 19.51.36260.0 (Visual Studio Build Tools), Windows SDK 10.0.22000.0.
- The **whole Windows layer compiled and linked on the first attempt** (CMake route, `Editor/Spike`).
- `ctest`: `cli` passed; `unit` 134/135 — one failure in the **non-POSIX scene-file fallback** (`stage sync: expected file.sync_failed, got file.write_failed`); fixed (see below).
- `k_gpu_probe`: hardware adapter reaches feature level 10_0 (above).
- `k_viewport_spike --screenshot out.png` **and** `--no-warp` (so a real hardware device, no WARP): exit code 0, "screenshot saved". Console showed `Intel(R) HD Graphics | FL 10_0 | hardware | frame ~2-12 ms`, with ~28-38 ms on the third frame (the capture frame, which includes a GPU read-back and CPU map; plausible, not investigated).
- `out.png`: all primitives (ground plane, cube, sphere, child cube) in the correct positions and silhouettes on the dark background, **but every surface completely black**.
- **Root cause of the black surfaces:** the pixel shader reads `color` from the `Object` constant buffer (b1) but the buffer was bound to the vertex stage only (`VSSetConstantBuffers`), so the pixel stage saw zeros. Fixed by also calling `PSSetConstantBuffers(1, 1, ...)`. **Not yet re-verified on hardware.** A stub-level syntax check cannot catch this class of bug.
- **Not reported by the owner (still unknown):** whether orbit / pan / zoom / click-to-select / resize work interactively; the on-screen label text (the console line suggests it is correct).

## Evidence
- **Verified here (Linux/GCC):** everything that feeds the renderer — `DrawList`, primitive meshes (closed, consistently wound, outward normals), orbit camera, projection/view math with Direct3D clip depth [0,1], picking — covered by unit tests and mutation checks (see TESTING.md).
- **Windows/Direct3D/Qt code:** compiled and run on the target machine (see Measured results). Interactive behaviour is still unreported; the shading fix is unverified.

## Trade-offs
Windows-only viewport (the owner's platform) in exchange for working on the target hardware. Linux/macOS Editors would need a second backend and are out of scope for M0.5. Direct3D 10.1-class hardware is slow: the profile above is deliberately conservative.

## Consequences
- `Engine/*` remains portable and Qt-free; only `Editor/` is Windows-specific.
- Shader math follows ADR-0010 (row-major upload, `row_major float4x4`, `mul(M, v)`).
- Light normals in the spike shader are exact only for uniform scale; correct lighting under non-uniform scale (inverse-transpose) is a known limitation to fix before the real viewport.

## Migration / rollback
Replace the backend behind `DrawList`; scene files, the Scene model, picking and the camera are unaffected.

## Validation plan (owner's machine; each step is a pass/fail with a recorded output)
1. Build and run `k_gpu_probe`. **Done: confirmed the adapter and refuted the 10_1 assumption.**
2. Build and run `k_viewport_spike`; the window shows ground, cube, sphere and a child cube, orbit/pan/zoom work, clicking highlights an object in orange. **Partly done: window and geometry OK; shading fix and interactive controls still to confirm.**
3. `k_viewport_spike --screenshot out.png` writes a non-blank PNG and exits 0. **Done (exit 0, PNG written); the PNG was black-shaded before the fix.**
4. Record OS version, GPU driver version, Qt version/kit and compiler in `DEVELOPMENT_STATUS.md`. If any step fails, fix this ADR before writing more viewport code.

## Open questions for the owner
1. ~~Qt kit~~ — answered: MSVC (msvc2022_64).
2. Is a non-Windows viewport ever required?

**Resolved by the owner (2026-11-19):** supported Windows versions are **Windows 10 and Windows 11** (x64). Windows 7/8 are out of scope, which removes the old-runtime concerns (Direct3D 11 runtime availability, `d3dcompiler_47.dll` redistribution — the latter believed to ship with Windows 10/11, **UNVERIFIED**). Qt 6's own Windows 10+ requirement is recollection (**UNVERIFIED**) but consistent with this target. The Intel driver situation for a 2010 iGPU on Windows 10/11 is the real unknown and is what `k_gpu_probe` measures.
