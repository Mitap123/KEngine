# ADR-0004: Initial renderer and Qt integration

- **Status:** proposed. **Blocked on environment:** Qt 6, CMake and a GPU/GL stack are not available where this was written, so nothing below has been run.
- **Date:** 2026-11-18

## Context
M0.5 needs a 3D viewport rendering cube/sphere/plane with picking, inside a Qt dock layout, plus a screenshot probe for automation. One backend only (reference §11.1, §9.4).

## Constraints
- Qt UI stays responsive; GL/graphics context thread rules must be respected.
- Headless screenshot is desirable for CI, but a GPU is not guaranteed there.
- Backend must be justified by evidence, not preference.

## Options
1. **OpenGL 3.3 core via `QOpenGLWidget`** on the UI thread. Qt owns the context; simplest docking integration; software GL (Mesa llvmpipe) can serve CI/headless screenshots. Risk: OpenGL is deprecated on macOS (frozen at 4.1), so a future macOS target may force a second path.
2. **Vulkan via `QVulkanWindow` / window container.** Modern, explicit; far more code for a first slice; headless CI needs a software ICD.
3. **Qt RHI (QRhi)** abstraction. Cross-API for free, but couples the renderer to Qt and its API stability guarantees (to be verified against current Qt docs before relying on it).
4. **Third-party abstraction (e.g. bgfx).** Multi-API; adds a significant dependency and its own build/licensing review.
5. **CPU software rasterizer** for headless tests only. Deterministic screenshots, but is a second renderer to maintain.

## Decision (proposed)
Option 1 for M0.5, behind a **small render interface** (create/resize/render-frame/read-back/pick) so the backend can be replaced:
- Rendering happens on the Qt UI thread inside `QOpenGLWidget`; **no separate render thread** until measured need.
- Screenshot = framebuffer read-back via that interface; the CLI probe uses an offscreen/software GL context when one initializes, and otherwise reports "screenshot unavailable" and still runs state assertions (never claims a screenshot it did not take).
- Picking by ray vs. primitive bounds (CPU) first; GPU ID-buffer picking later if needed.

## Evidence
- Verified in the dev environment: no Qt, no CMake, no GL development headers (`/usr/include/GL` has only GLX headers), no `/dev/dri`; Mesa GLX/EGL runtime libraries are present. Nothing was executed.
- Everything else is reasoning/recollection and must be validated by the spike below.

## Trade-offs
Fastest path to a working viewport vs. long-term portability. We accept possible rework if macOS becomes a first-class target.

## Consequences
`Engine/Rendering` exposes the interface and a GL implementation; the Editor owns the widget. The standalone Runtime must not depend on Qt (its own window/context layer comes in M1/M4).

## Migration / rollback
Replace the implementation behind the interface; scene formats and the Editor's Scene/Undo layers are unaffected.

## Validation plan (spike, on a machine with Qt 6 + CMake + OpenGL)
1. Window with `QOpenGLWidget` clears and draws a cube; no GL errors.
2. Resize, dock/undock and hide/show do not lose or corrupt the context.
3. Screenshot read-back works under a software GL context (offscreen QPA or xvfb) — record exact commands and driver string.
4. Record driver/GPU/OS in `DEVELOPMENT_STATUS.md`. If any step fails, update this ADR before writing viewport code.
