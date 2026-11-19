# K Engine

A modular desktop game engine and IDE (in early development). **Current state: M0 in progress; the Editor does not exist yet.** What exists is the Qt-free engine core (identity, authoring scene model, strict JSON, scene serialization and file I/O, math, primitive meshes, camera, picking), a small CLI, tests, and an **unverified** Windows Direct3D 11 viewport spike (`Editor/Spike`, see its README).

```bash
make check                 # build + run unit and CLI tests
./Build/make-debug/k doctor
```

Start here: `AGENTS.md` (rules), `DEVELOPMENT_STATUS.md` (current state and next task), `BUILDING.md`, `TESTING.md`, `ARCHITECTURE.md`, `ROADMAP.md`, `FEATURE_MATRIX.md`, `Documentation/ADR/`.

Supported platforms: **Windows 10 and 11 (x64)**. The portable core is also built and tested on Linux. License: **deferred by the owner** (not needed yet; see ADR-0005); no LICENSE file is included on purpose.
