# Roadmap (gated)

Milestones are exit-gated, not dated. Details: `K_ENGINE_ARCHITECTURE_ROADMAP_REFERENCE_V2.md` §21. Evidence per feature: `FEATURE_MATRIX.md`. Task queue: `TASKS.json`.

| Milestone | Goal | Gate | State |
|---|---|---|---|
| **M0** Audit, decisions, skeleton | Buildable foundation, ADRs, CLI foundation, CI | Builds a minimal real app **or** documents the blocking toolchain issue; decisions recorded | **In progress**: audit, inventory, ADRs, Make build, CLI done; CMake unverified; CI not done |
| **M0.5** Hello Cube Editor | Qt shell, viewport, cube/sphere/plane, select/transform, save/load, undo, Play/Stop, CLI validation | Acceptance workflow (create → transform → save → reopen → undo/redo → Play → Stop → source unchanged) | Started: identity + scene model + serialization (in memory). Editor/GUI tasks **blocked by environment** (no Qt/CMake/GPU) |
| M1 Runtime foundation | World, scheduler, time, Play isolation tests, headless init/step | exit criteria in reference | planned |
| M2 Python scripting | embedded CPython, handle API | | planned |
| M3 Asset intelligence | GUID/meta, DAG, import, prefabs | | planned |
| M4 Build/export | manifest, cook, package | | planned |
| M5 Native extensions | C ABI, C++/Rust SDK | | planned |
| M6 Simulation/determinism | headless fixed-step, replay, benchmarks | | planned |
| M7 2D + production systems | | | planned |
| M8 Advanced authoring | graphs, terrain | | planned |
| M9 Long-term | streaming, remote tools, AI assistant, ecosystem | | planned |

Do not start a milestone because source files exist; meet its exit criteria with evidence.
