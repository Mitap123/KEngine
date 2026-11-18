# ADR-0002: Reflection metadata

- **Status:** proposed
- **Date:** 2026-11-18

## Context
The Inspector, serialization, undo of property edits and (later) script/plugin exposure all want property metadata. Reference §5.6.

## Constraints
No code-generation toolchain (libclang) is available or verified. Reflection must not become ECS storage. Avoid fragile text parsing and raw memory offsets.

## Options
1. **Explicit registration:** hand-written property descriptors (`name`, type, getter/setter, default, validation, hints) per component type.
2. **Macro-assisted registration:** macros expand to the same descriptors; less boilerplate, harder debugging.
3. **libclang-based code generation:** least boilerplate; adds a toolchain dependency, build complexity and a parser to maintain.
4. **Native C++ static reflection:** not available in the targeted standard (C++20).

## Decision
Start with **option 1** (explicit descriptors, getter/setter based, no memory offsets). Introduce light macros only if boilerplate demonstrably hurts. Defer code generation until there are enough component types or external exposure (M2 Python, M5 plugins) to justify it, and compare then with a measured prototype.

For M0.5 the Inspector needs only: object name, kind (read-only), position, rotation, scale.

## Evidence
Nothing implemented; reasoning only. `Transform` and `SceneObject` are plain structs today so descriptors can wrap them later without layout tricks.

## Trade-offs
Boilerplate per component vs. zero tooling dependency. Descriptors must be kept in sync by hand; mitigated by a unit test that checks every registered property round-trips through its serializer (to be written with the registry).

## Consequences
Serialization may initially be hand-written (as in `SceneSerializer`) and migrate to descriptor-driven later; scene format v1 is already independent of that choice.

## Rollback
Descriptors are an internal API; swapping the generator behind them does not change file formats.

## Validation plan
When the registry lands: round-trip test for every registered property, Inspector edit → Undo test, and a short comparison note against a codegen prototype before adopting one.
