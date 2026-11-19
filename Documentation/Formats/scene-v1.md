# Scene format `kengine.scene` v1

Status: **implemented and unit-tested**: in-memory (`SceneSerializer.*`) and file I/O (`SceneFile.*`, Linux/GCC only). Decisions: ADR-0006.

## Example
```json
{
  "schema": "kengine.scene",
  "schema_version": 1,
  "objects": [
    {
      "id": "01234567-89ab-4def-bedc-ba9876543210",
      "name": "Cube",
      "kind": "cube",
      "parent": null,
      "transform": {
        "position": [0, 1, 0],
        "rotation_euler_deg": [0, 45, 0],
        "scale": [1, 1, 1]
      }
    }
  ]
}
```

## Fields
| Path | Type | Required | Notes |
|---|---|---|---|
| `schema` | string | yes | Must equal `"kengine.scene"`. |
| `schema_version` | integer | yes | Must be `1` for this build. Greater → error "newer"; < 1 → error. |
| `objects` | array | yes | Any order on load; written in hierarchy preorder. |
| `objects[].id` | string | yes | Canonical UUID (`8-4-4-4-12` hex, either case on load, lowercase on save). Non-nil, unique in the file. |
| `objects[].name` | string | yes | Any UTF-8; not required to be unique. |
| `objects[].kind` | string | yes | `empty`, `cube`, `sphere`, `plane` (case-sensitive). |
| `objects[].parent` | string \| null | no | UUID of another object in the file; absent/null = root. |
| `objects[].transform` | object | no | Absent = identity. |
| `transform.position` | [x,y,z] numbers | no | Default `[0,0,0]`. |
| `transform.rotation_euler_deg` | [x,y,z] numbers | no | Degrees. Default `[0,0,0]`. **Normative:** rotation matrix `R = Ry * Rx * Rz` (Z applied first), right-handed, counter-clockwise positive. See ADR-0010. |
| `transform.scale` | [x,y,z] numbers | no | Default `[1,1,1]`. |

Transforms are **local**: relative to the parent object (`world = parent_world * T*R*S`). Roots are relative to the world origin. Right-handed, +Y up (ADR-0010). Primitive sizes: cube edge 1, sphere radius 0.5, plane 1×1 in XZ; scale comes from the transform.

Sibling order: children of a parent keep the relative order in which they appear in `objects`; root order likewise.

## Rules
- Strict JSON (see `Json.h`): no comments, no trailing commas, UTF-8 only, no BOM, no duplicate keys, nesting ≤ 128.
- Only persistent data is stored. Runtime handles/indices/addresses never appear.
- Unknown fields are ignored with a warning and **not preserved** when the scene is saved again.
- Numbers are doubles written in shortest round-trip form.

## Error codes (stable)
`json.syntax`, `scene.schema.invalid_root`, `scene.schema.missing_field`, `scene.schema.wrong_type`, `scene.schema.unknown_schema`, `scene.schema.unsupported_version`, `scene.object.invalid_uuid`, `scene.object.unknown_kind`, `scene.object.missing_parent`, `scene.transform.invalid`, `scene.nil_uuid`, `scene.duplicate_uuid`, `scene.cycle`. Messages include a pointer such as `/objects/3/parent`; match on `code`, not message text.

## Versioning
Any incompatible change bumps `schema_version` and adds a migration step (`vN → vN+1`). Additive optional fields do not bump the version but older tools will drop them on re-save.

## File I/O (`Engine/Serialization/SceneFile.h`)
- **Save** is atomic: serialize in memory → write `.<name>.<8 hex>.tmp` in the *same directory* (`O_EXCL`) → `fsync` → copy any existing file to `<name>.bak` (single generation, `SaveOptions::keep_backup`) → `rename()` over the target → best-effort directory `fsync`. On any failure the temp file is removed and the existing file is untouched. A crash can leave a stray `.tmp` file; it is never loaded.
- The `.bak` copy is a plain copy, not itself atomic: a crash during the backup step can leave a damaged `.bak` but never a damaged target.
- Save refuses to overwrite a directory/device (`file.not_regular`) or a symbolic link (`file.symlink_unsupported`). It does not create missing directories.
- **Load** refuses non-regular files, reads at most 64 MiB by default (`file.too_large`), and prefixes every error with the file path. Loading through a symlink works.
- File error codes: `file.invalid_path`, `file.not_found`, `file.not_regular`, `file.symlink_unsupported`, `file.too_large`, `file.io_error`, `file.read_failed`, `file.temp_create_failed`, `file.write_failed`, `file.sync_failed`, `file.backup_failed`, `file.rename_failed`.
- Limits of what is verified: only the POSIX path has been built and run. Write/fsync/backup/rename failures are *simulated* via a test-only hook, not produced by a real failing disk; power-loss durability is not tested.
