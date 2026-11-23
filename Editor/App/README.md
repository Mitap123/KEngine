# k_editor — the K Engine Editor (M0.5) — FIRST COMPILE PENDING

**Status (2026-11-19):** **compiled and linked cleanly with real Qt 6.12 / MSVC 19.51** on the owner's machine. **Update 2026-11-23:** `ctest` passes 6/6 and `k_editor --smoke` reports 40 checks, 0 failed on the owner's machine. **Interactive behaviour (real mouse use, appearance) is still not observed**; the manual checklist below is still pending. The Qt-free logic behind it (`Editor/Core`) is unit-tested on Linux (152 unit tests).

## What it is
Qt 6 Widgets window: menus + toolbar (File / Edit / Create / View / Play), Explorer (hierarchy tree), Inspector (name, kind, UUID, position / rotation / scale), Console, and the Direct3D 11 viewport shared with the spike (`Editor/Spike/D3D11Renderer.*`). Every action goes through `EditorDocument` (`Editor/Core`).
Viewport: left-drag orbit, right/middle-drag or Shift+left-drag pan, wheel zoom, click selects (orange), click empty space clears, `F` = Frame All.
Keys: Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z / redo, Delete, F5 = Play, Shift+F5 = Stop.
Play runs a demo on a **copy** of the scene (cubes and spheres spin); editing is locked; Stop restores the authored scene exactly.
Limits: Inspector scale cannot go below 0.001; Explorer drag-and-drop reparenting and Edit > Duplicate (Ctrl+D) were written on 2026-11-23: compiled, and their document-level paths pass `--smoke`, but a **real mouse drag has not been tried** (see the checklist); no multi-select; no paste; no grid/gizmos; objects are created at the origin under the root.

## Build (Windows 10/11, same environment that built the spike)
```
cmake -S . -B Build/cmake -DKENGINE_BUILD_EDITOR_SPIKE=ON -DKENGINE_BUILD_EDITOR=ON -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/msvc2022_64
cmake --build Build/cmake --config Debug
ctest --test-dir Build/cmake -C Debug --output-on-failure
```
(Qt's `bin` folder must be on PATH, as before.) Unit tests are now **fast by default** (a few seconds on Linux); `-DKENGINE_FULL_TESTS=ON` adds ctest `unit_full` with the old full sizes.
Expected tests: `unit`, `cli`, `png_probe`, `viewport_screenshot` (spike), and the new `editor_smoke`, `editor_screenshot`.

## Troubleshooting
- A Qt-based test or exe exiting with code **3221225781 / 0xC0000135** means a DLL was not found (almost always Qt's `bin` folder is not on PATH). The ctest tests now add Qt's `bin` to PATH themselves (re-run `cmake --build`, which re-configures); when you run an exe by hand you still need `$env:PATH = "D:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"` (or `windeployqt`).

## What each new test does
- `editor_smoke` = `k_editor --smoke`: drives the REAL widgets (Create, Explorer selection, Inspector spin boxes and name box, Undo/Redo, Save/Open of a temp file, Play/Stop) and checks the UI after each step. Prints `[ OK ]/[FAIL]` lines and `SMOKE: N checks, M failed`.
- `editor_screenshot` = `k_editor --demo --screenshot out.png` judged by the same black/lit pixel probe as the spike.

## Run by hand
`Build\cmake\Editor\App\Debug\k_editor.exe --demo` (demo scene) or `k_editor.exe some.kscene`. Other flags: `--screenshot out.png`, `--smoke`, `--no-warp`.

## What to send back
1. ~~Compiler output~~ - done: built clean.
2. `ctest` result for all tests; for `editor_smoke` the full printed check list if anything fails.
3. By hand: does the window look sane (docks, menus, viewport)? Create > Cube/Sphere/Plane work? Explorer click and viewport click select and stay in sync? Editing a Position box moves the object? Rename updates the Explorer? Undo/Redo menu text changes? Save/Open round trip? Play spins the shapes and locks editing, Stop restores? Closing with unsaved changes asks? **New (2026-11-23, never run):** Edit > Duplicate (Ctrl+D) copies the selected object with its children as "Name 2" and selects it, and one Undo removes it. Drag an Explorer row onto the middle of another row (becomes its child), onto the upper/lower edge (becomes its sibling before/after), or onto empty space below all rows (becomes the last root): the object must not visibly jump in the viewport, the move must be one Undo step, a row cannot be dropped onto itself or its own child (the drop indicator should refuse), and nothing can be dropped while Play is running.
