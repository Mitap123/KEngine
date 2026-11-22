# k_editor — the K Engine Editor (M0.5) — FIRST COMPILE PENDING

**Status:** written, **never compiled with the real Qt**. The only check done on `MainWindow.cpp`, `EditorViewport.cpp` and `main.cpp` is a C++ syntax/type check against a hand-written minimal Qt stub (it catches typos, wrong member names, lambda/signal arity; it does **not** validate real Qt API signatures or any behaviour). Expect a few compile fixes on the first build. The Qt-free logic behind it (`Editor/Core`) is unit-tested on Linux (152 unit tests).

## What it is
Qt 6 Widgets window: menus + toolbar (File / Edit / Create / View / Play), Explorer (hierarchy tree), Inspector (name, kind, UUID, position / rotation / scale), Console, and the Direct3D 11 viewport shared with the spike (`Editor/Spike/D3D11Renderer.*`). Every action goes through `EditorDocument` (`Editor/Core`).
Viewport: left-drag orbit, right/middle-drag or Shift+left-drag pan, wheel zoom, click selects (orange), click empty space clears, `F` = Frame All.
Keys: Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z / redo, Delete, F5 = Play, Shift+F5 = Stop.
Play runs a demo on a **copy** of the scene (cubes and spheres spin); editing is locked; Stop restores the authored scene exactly.
Limits: Inspector scale cannot go below 0.001; no drag-and-drop reparenting; no multi-select; no grid/gizmos; objects are created at the origin under the root.

## Build (Windows 10/11, same environment that built the spike)
```
cmake -S . -B Build/cmake -DKENGINE_BUILD_EDITOR_SPIKE=ON -DKENGINE_BUILD_EDITOR=ON -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/msvc2022_64
cmake --build Build/cmake --config Debug
ctest --test-dir Build/cmake -C Debug --output-on-failure
```
(Qt's `bin` folder must be on PATH, as before.) Unit tests are now **fast by default** (a few seconds on Linux); `-DKENGINE_FULL_TESTS=ON` adds ctest `unit_full` with the old full sizes.
Expected tests: `unit`, `cli`, `png_probe`, `viewport_screenshot` (spike), and the new `editor_smoke`, `editor_screenshot`.

## What each new test does
- `editor_smoke` = `k_editor --smoke`: drives the REAL widgets (Create, Explorer selection, Inspector spin boxes and name box, Undo/Redo, Save/Open of a temp file, Play/Stop) and checks the UI after each step. Prints `[ OK ]/[FAIL]` lines and `SMOKE: N checks, M failed`.
- `editor_screenshot` = `k_editor --demo --screenshot out.png` judged by the same black/lit pixel probe as the spike.

## Run by hand
`Build\cmake\Editor\App\Debug\k_editor.exe --demo` (demo scene) or `k_editor.exe some.kscene`. Other flags: `--screenshot out.png`, `--smoke`, `--no-warp`.

## What to send back
1. Compiler output of the first build, verbatim (or "built clean").
2. `ctest` result for all tests; for `editor_smoke` the full printed check list if anything fails.
3. By hand: does the window look sane (docks, menus, viewport)? Create > Cube/Sphere/Plane work? Explorer click and viewport click select and stay in sync? Editing a Position box moves the object? Rename updates the Explorer? Undo/Redo menu text changes? Save/Open round trip? Play spins the shapes and locks editing, Stop restores? Closing with unsaved changes asks?
