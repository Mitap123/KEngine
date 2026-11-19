# Architecture Decision Records

Process: any decision that changes public API, serialization, ownership, thread model, backend, plugin boundary or a major dependency gets an ADR before it becomes hard to reverse. Statuses: `proposed`, `accepted`, `deprecated`, `superseded`.

"Evidence" sections separate what was **verified in this repository/environment** from reasoning and recollection that still needs verification.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-authoring-runtime-layers-and-ecs.md) | Authoring/Runtime layers and ECS | accepted (M0.5 scope); ECS library choice deferred to M1 |
| [0002](0002-reflection-metadata.md) | Reflection metadata | proposed |
| [0003](0003-identity-uuid-and-handles.md) | Persistent UUID and generational handles | accepted, implemented, tested |
| [0004](0004-renderer-and-qt-integration.md) | Renderer backend and Qt integration (D3D 10.1 floor) | accepted as spike direction; needs owner-machine results |
| [0005](0005-qt-licensing-and-deployment.md) | Qt licensing and deployment | proposed (needs owner decision) |
| [0006](0006-data-formats-and-migration.md) | Data formats, schema versioning, migration | accepted for scene v1; manifest proposed |
| [0007](0007-c-abi-and-python-binding.md) | C ABI and Python binding strategy | proposed (deferred to M2/M5) |
| [0008](0008-determinism-scope.md) | Determinism requirements and limits | accepted (scope statement) |
| [0009](0009-bootstrap-build-route.md) | Bootstrap Makefile alongside CMake | accepted |
| [0010](0010-math-and-coordinate-conventions.md) | Math and coordinate conventions | accepted, implemented, tested |
| [0011](0011-editing-model-commands-history-play.md) | Editing model: commands, history, dirty tracking, isolated Play | accepted, implemented, tested |
