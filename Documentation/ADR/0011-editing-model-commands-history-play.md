# ADR-0011: Editing model — UUID-addressed commands, history, dirty tracking, isolated Play

- **Status:** accepted (implemented, tested; Qt-free)
- **Date:** 2026-11-19

## Context
The Editor needs undo/redo for create/delete/transform/rename/reparent, a reliable "unsaved changes" flag, drag-friendly history, and a Play mode that cannot damage the authored scene (reference §10.2, §10.4; ADR-0001). Undo of a delete must not leave dangling references.

## Decision
- **Commands address objects by persistent UUID, never by `Handle`.** A handle dies with its object; undoing a delete re-creates the same object (same UUID) with a new handle, and every later command still finds it. The Editor UI holds selection as UUID too.
- **`SceneEditor` (Engine/Editing)** is the document model the UI talks to: it owns the `Scene`, a `CommandHistory`, dirty tracking and Play. The UI never mutates a `Scene` directly. No Qt, no rendering.
- **Commands:** create, delete (whole subtree; restores UUIDs, names, kinds, transforms, hierarchy **and sibling position**), set-transform (mergeable), rename, reparent (restores old parent and sibling index). `Scene` gained sibling-indexed insertion (`create_with_uuid`/`reparent` take an index; default appends).
- **Failure semantics:** a failed edit changes nothing, records no history entry and does **not** discard the redo branch. A failed undo/redo (scene changed behind the history's back) returns an error and keeps the history; a partially restored delete is rolled back.
- **Merging:** `execute(cmd, allow_merge)`. Only the immediately preceding merge-enabled command can absorb it, and only until a gesture end, undo, redo or save. Merging keeps the first command's "old" value, so one undo reverts a whole drag.
- **Dirty tracking:** position-based. Clean iff the current position equals the saved position. The saved state becomes permanently unreachable (document stays dirty) if its redo branch is discarded by a new edit, if history trimming drops it (default depth 1000), or if a merge folds an edit into the saved step. A failed save leaves the document dirty.
- **Play (M0.5):** `begin_play()` makes an independent value copy of the authoring scene, including unsaved edits. Runtime code mutates only `play_scene()`. `end_play()` discards the copy. While playing, all authoring edits and undo/redo are refused (`editor.read_only_while_playing`) and `can_undo/can_redo` report false. `save()` while playing writes the authoring scene, never the copy. Play does not change history or the dirty flag. `active_scene()` is what a viewport draws. This resolves ADR-0001's open "deep copy vs serialize-reload" question in favour of the value copy (exact, simple, cheap enough; revisit if measured scene sizes make it costly).

- **Reparent semantics (added 2026-11-23):** `make_reparent_command(id, parent, sibling_index, mode)`. The index is counted after the object is removed from its old spot (same parent = reorder). `KeepLocal` leaves the transform numbers alone (the object may jump); `KeepWorld` recomputes the local transform as `inverse(new_parent_world) * old_world`, split by `decompose_trs` (T*R*S, Euler `Ry*Rx*Rz`, a reflection on the X scale). Because a local transform is position + Euler + per-axis scale, KeepWorld cannot always be expressed: a rotated object under a non-uniformly scaled parent needs shear. That is **rejected** with `command.world_transform_not_representable` (never approximated), and a zero-scale parent with `command.singular_parent_transform`; the scene is unchanged and no history entry is made. `EditorDocument::reparent` defaults to KeepWorld (what drag-and-drop should do), `SceneEditor::reparent` defaults to KeepLocal (unchanged engine behaviour). A KeepWorld move that leaves the world matrix unchanged keeps the exact transform bytes; a move to the place the object already is records no undo step.

## Options considered
Handle-based commands (rejected: dangling after delete). Storing full-scene snapshots per step (rejected: memory grows with scene size). Allowing edits during Play with automatic revert (rejected for M0.5: ambiguous semantics, easy to lose work). Serialize-reload for the Play copy (rejected: adds a failure mode and cost for no extra isolation).

## Evidence (verified, Linux/GCC; Debug, Release and ASan+UBSan)
`Tests/Unit/test_commands.cpp`: per-command undo/redo with exact serialized-state equality; merge rules; failure paths; clean/dirty edge cases; trimming; rollback of a conflicting restore; a randomized model test (1,500 steps, 40% edits / 30% undo / 30% redo) comparing the serialized scene against a snapshot list after every step, with guards that all paths were exercised; Play isolation in both directions, blocked edits, repeated sessions. Mutation checks: see TESTING.md.

## Trade-offs / limitations
- Handles are numerically identical in the source and the Play copy (ADR-0003): never carry a handle between them; use UUIDs.
- No composite commands yet (duplicate, paste). Undo of `reparent` assumes LIFO history.
- KeepWorld decomposition normalises Euler angles (X in [-90, 90]; Z = 0 at the poles): the moved object is equivalent, but its Inspector numbers can differ from what a user typed. Undo restores the original numbers exactly.
- Property edits are limited to name and transform until the reflection layer exists (ADR-0002).
- Memory of the Play copy is proportional to scene size.

## Consequences
The Editor shell (M05-T01/T04) only needs to call `SceneEditor` and render `active_scene()`.

## Rollback
The editing layer is internal; scene files are unaffected.
