# Viewport spike (Direct3D 11, feature level 10_0 floor)

**Status (2026-11-19):** compiled and run on the owner's Windows 10 / Intel HD Graphics (device 0x0046, feature level **10_0**) / Qt 6.12 / MSVC 19.51 machine: hardware device created, frames rendered, screenshot written, exit code 0. First run found a black-shading bug (pixel shader constant buffer not bound), now fixed but **not yet re-verified**; interactive controls are still unreported. Decision record: `Documentation/ADR/0004-renderer-and-qt-integration.md`.

## What is here
| Target | Needs | Purpose |
|---|---|---|
| `k_gpu_probe` | C++ compiler + Direct3D 11/DXGI headers (MSVC Windows SDK or MinGW-w64). **No Qt.** | Prints every adapter and the Direct3D feature level it reaches, plus whether the WARP software device works. Confirms the hardware assumptions in ADR-0004. |
| `k_viewport_spike` | Qt 6 Widgets + the above | Window with a Direct3D 11 viewport: ground plane, cube, sphere, child cube. Orbit (left drag), pan (right/middle drag or Shift+left drag), zoom (wheel), click to select (orange highlight). |

The portable, tested logic (matrices, primitives, camera, picking, draw list) lives in `Engine/` and is verified on Linux; this folder is only the thin Windows layer.

## Step 0 — verify the portable part first (also the first real test of `CMakeLists.txt`)
From the repository root (MSVC example; use an "x64 Native Tools" prompt, or adapt for MinGW):
```
cmake -S . -B Build/cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build Build/cmake --config Debug
ctest --test-dir Build/cmake -C Debug --output-on-failure
```
Expected: 2 tests pass (`unit`, `cli`). `cli` needs Python 3 on PATH. If configuration or any test fails, stop and report the output: that is a real finding about the portable code on Windows.

## Step 1 — GPU probe
```
cmake -S . -B Build/cmake -DKENGINE_BUILD_EDITOR_SPIKE=ON -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2019_64
cmake --build Build/cmake --config Debug --target k_gpu_probe
Build\cmake\Editor\Spike\Debug\k_gpu_probe.exe
```
(`CMAKE_PREFIX_PATH` may be omitted for this step; without Qt only the probe is built, with a warning. Single-config generators such as Ninja or MinGW put the exe in `Build\cmake\Editor\Spike\`.)

**Measured on the owner's i5-560M machine:** Intel HD Graphics (0x8086:0x0046) reaches feature level **10_0** (not 10_1); the Microsoft Basic Render Driver and WARP reach 11_0. The engine's floor is therefore 10_0.

## Step 2 — viewport
```
cmake --build Build/cmake --config Debug --target k_viewport_spike
```
Qt's DLLs must be found at run time: either run `windeployqt` on the exe, or put your Qt kit's `bin` folder on `PATH`.
```
Build\cmake\Editor\Spike\Debug\k_viewport_spike.exe
```
Check: you see the shapes; the label shows adapter, feature level, driver (`hardware` vs `WARP (software)`), and a frame time (it includes the vsync wait, so it is not a GPU time); drag/zoom works; clicking a shape turns it orange and the label names it; clicking empty space clears the selection; resizing the window keeps rendering.

## Step 3 — automated screenshot
```
k_viewport_spike.exe --screenshot out.png
```
Exit code 0 and a non-blank `out.png`. Exit 2 = GPU initialization failed, 1 = screenshot could not be saved, 3 = timed out. `--no-warp` makes it fail instead of silently using the software rasterizer (use it to be sure you are testing the real GPU).

## Re-verification after the shading fix (please do this next)
Rebuild and run again (`cmake --build Build/cmake --config Debug --target k_viewport_spike`, then `ctest --test-dir Build/cmake -C Debug --output-on-failure` — expect **2/2 pass**, unit 135/135). Then report:
1. Does `out.png` now show **lit, light-blue** shapes (not black) with the cube/sphere shaded by a light from the upper right?
2. Interactive checks in the window, one line each: orbit (left drag), pan (right drag or Shift+left drag), zoom (wheel), click selects (turns **orange**, label names it), click empty space clears, resizing/maximizing keeps rendering. Which work and which do not?
3. The label text shown in the window.

## What to send back
1. Output of Step 0's `ctest` (or the first error).
2. Full `k_gpu_probe` output.
3. Console output and exit code of the viewport and of `--screenshot --no-warp`, plus `out.png` if produced.
4. Windows version, GPU driver version (Device Manager → Display adapters → Driver), Qt version/kit, compiler.

## Known limitations of the spike
Lighting is exact only for uniform scale (the shader transforms normals by the world matrix). Rendering is a single directional light with flat colours. No UI beyond the label. The mouse sensitivity values are untuned. Frame time is CPU-side and includes vsync.
