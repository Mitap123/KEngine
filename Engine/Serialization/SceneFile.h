// K Engine — Engine/Serialization/SceneFile.h
//
// Scene file I/O on top of SceneSerializer (ADR-0006). All failures are
// returned as Result/Status with a stable `code`; nothing throws.
//
// SAVE is atomic and recoverable (POSIX path):
//   1. serialize in memory (a serialization failure touches no file),
//   2. write a temporary file in the SAME directory (O_EXCL, never clobbers),
//   3. fsync the temporary file,
//   4. if the target exists and keep_backup: copy it to "<path>.bak",
//   5. rename() the temporary file over the target (atomic on POSIX),
//   6. best-effort fsync of the directory.
// If any step fails the temporary file is removed and the existing target is
// left byte-for-byte untouched. A crash can leave a stray ".<name>.<id>.tmp"
// file next to the target; it is never read by load.
//
// Policy: refuses to save over a non-regular file or a symlink (replacing a
// symlink with a regular file would silently break the user's link).
//
// Platform notes: POSIX (Linux/GCC) is covered by unit tests here. On Windows the
// same sequence uses CreateFileW(CREATE_NEW) / WriteFile / FlushFileBuffers and
// MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH) (there is no directory fsync); this
// path was first compiled and run on Windows 10 (MSVC) by the project owner, where
// the generic ofstream fallback it replaced failed one test (sync error mapping).
// Any other platform uses an ofstream + std::filesystem::rename fallback without
// fsync: UNVERIFIED, weaker durability.
//
// LOAD reads at most `max_bytes`, refuses non-regular files (so a FIFO or
// device cannot block or flood memory), and prefixes parse/validation errors
// with the file path.
//
// Error codes (stable): file.invalid_path, file.not_found, file.not_regular,
// file.symlink_unsupported, file.too_large, file.io_error, file.read_failed,
// file.temp_create_failed, file.write_failed, file.sync_failed,
// file.backup_failed, file.rename_failed — plus every code of
// load_scene_from_string() for content errors.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>

#include "Engine/Core/Result.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/SceneSerializer.h"

namespace k {

inline constexpr std::size_t kDefaultMaxSceneFileBytes = 64ull * 1024 * 1024;

struct SaveOptions {
    // Keep the previous file as "<path>.bak" (single generation) when replacing.
    bool keep_backup = true;

    // TEST-ONLY seam. Called with a stage name: "create_temp", "write",
    // "sync", "backup", "rename". Returning true makes that stage fail exactly
    // as if the OS had reported an I/O error, so cleanup and error mapping can
    // be tested without a misbehaving filesystem. Leave empty in production.
    std::function<bool(std::string_view stage)> fault_injection;
};

Status save_scene_file(const Scene& scene, const std::filesystem::path& path,
                       const SaveOptions& options = {});

Result<LoadedScene> load_scene_file(const std::filesystem::path& path,
                                    std::size_t max_bytes = kDefaultMaxSceneFileBytes);

}  // namespace k
