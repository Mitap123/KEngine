#include "Engine/Serialization/SceneFile.h"

#include <cerrno>
#include <fstream>
#include <string>
#include <system_error>

#include "Engine/Identity/Uuid.h"

#if defined(__unix__) || defined(__APPLE__)
#define KENGINE_POSIX_IO 1
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace k {

namespace {

std::string errno_message(int e) { return std::generic_category().message(e); }

Error file_error(const char* code, const fs::path& path, const std::string& what) {
    return make_error(code, path.string() + ": " + what);
}

// -------------------------------------------------------------- writing ---

// Writes `data` to a new file at `tmp` (must not exist) and flushes it to
// stable storage. On failure the partially written file is removed.
// `*name_collision` is set when the temporary name already existed (EEXIST),
// so the caller can retry with a fresh name.
Status write_temp_file(const fs::path& tmp, const std::string& data, const SaveOptions& opt,
                       bool* name_collision) {
    auto inject = [&](std::string_view stage) { return opt.fault_injection && opt.fault_injection(stage); };
    auto cleanup = [&] { std::error_code ec; fs::remove(tmp, ec); };

#ifdef KENGINE_POSIX_IO
    if (inject("create_temp")) {
        return file_error("file.temp_create_failed", tmp, "injected failure");
    }
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) {
        const int e = errno;
        if (e == EEXIST && name_collision) *name_collision = true;
        return file_error("file.temp_create_failed", tmp, "cannot create temporary file: " + errno_message(e));
    }
    bool ok = !inject("write");
    int err = ok ? 0 : EIO;
    const char* p = data.data();
    std::size_t left = data.size();
    while (ok && left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            err = errno;
            break;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
    if (!ok) {
        ::close(fd);
        cleanup();
        return file_error("file.write_failed", tmp, "write failed: " + errno_message(err));
    }
    int sync_err = 0;
    if (inject("sync")) {
        sync_err = EIO;
    } else if (::fsync(fd) != 0) {
        sync_err = errno;
    }
    if (sync_err != 0) {
        const int e = sync_err;
        ::close(fd);
        cleanup();
        return file_error("file.sync_failed", tmp, "fsync failed: " + errno_message(e));
    }
    if (::close(fd) != 0) {
        const int e = errno;
        cleanup();
        return file_error("file.write_failed", tmp, "close failed: " + errno_message(e));
    }
    return Status::success();
#else
    // UNVERIFIED fallback: no fsync, weaker durability.
    if (inject("create_temp")) return file_error("file.temp_create_failed", tmp, "injected failure");
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return file_error("file.temp_create_failed", tmp, "cannot create temporary file");
    if (inject("write")) { out.close(); cleanup(); return file_error("file.write_failed", tmp, "injected failure"); }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out || inject("sync")) { out.close(); cleanup(); return file_error("file.write_failed", tmp, "write failed"); }
    return Status::success();
#endif
}

void sync_directory_best_effort(const fs::path& dir) {
#ifdef KENGINE_POSIX_IO
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)::fsync(fd);  // best effort: some filesystems refuse; the rename already happened
        ::close(fd);
    }
#else
    (void)dir;
#endif
}

}  // namespace

Status save_scene_file(const Scene& scene, const fs::path& path, const SaveOptions& options) {
    if (path.empty() || !path.has_filename()) {
        return file_error("file.invalid_path", path, "path must name a file");
    }
    auto inject = [&](std::string_view stage) { return options.fault_injection && options.fault_injection(stage); };

    auto text = save_scene_to_string(scene);
    if (!text) return text.error();  // nothing on disk has been touched yet

    // Inspect the target without following symlinks.
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(path, ec);
    bool target_exists = false;
    if (st.type() == fs::file_type::not_found) {
        target_exists = false;
    } else if (ec) {
        return file_error("file.io_error", path, "cannot inspect target: " + ec.message());
    } else {
        target_exists = true;
        if (st.type() == fs::file_type::symlink) {
            return file_error("file.symlink_unsupported", path,
                              "refusing to replace a symbolic link; save to the link's target path instead");
        }
        if (st.type() != fs::file_type::regular) {
            return file_error("file.not_regular", path, "target exists and is not a regular file");
        }
    }

    const fs::path dir = path.has_parent_path() ? path.parent_path() : fs::path(".");
    fs::path tmp;
    Status written = Status::success();
    // Unique temporary name in the same directory (same filesystem => rename is atomic).
    for (int attempt = 0; attempt < 8; ++attempt) {
        tmp = dir / ("." + path.filename().string() + "." + Uuid::generate().to_string().substr(0, 8) + ".tmp");
        bool collision = false;
        written = write_temp_file(tmp, text.value(), options, &collision);
        if (!collision) break;  // success, or a failure that a new name cannot fix
    }
    if (!written.ok()) return written.error();

    auto remove_tmp = [&] { std::error_code rec; fs::remove(tmp, rec); };

    if (target_exists && options.keep_backup) {
        fs::path bak = path;
        bak += ".bak";
        std::error_code bec;
        if (inject("backup")) {
            bec = std::make_error_code(std::errc::io_error);
        } else {
            fs::copy_file(path, bak, fs::copy_options::overwrite_existing, bec);
        }
        if (bec) {
            remove_tmp();
            return file_error("file.backup_failed", bak, "cannot back up the existing file: " + bec.message());
        }
    }

    std::error_code rec;
    if (inject("rename")) {
        rec = std::make_error_code(std::errc::io_error);
    } else {
#ifdef KENGINE_POSIX_IO
        if (::rename(tmp.c_str(), path.c_str()) != 0) rec = std::error_code(errno, std::generic_category());
#else
        fs::rename(tmp, path, rec);
#endif
    }
    if (rec) {
        remove_tmp();
        return file_error("file.rename_failed", path, "cannot replace the target: " + rec.message());
    }

    sync_directory_best_effort(dir);
    return Status::success();
}

Result<LoadedScene> load_scene_file(const fs::path& path, std::size_t max_bytes) {
    if (path.empty()) return file_error("file.invalid_path", path, "path is empty");

    std::error_code ec;
    const fs::file_status st = fs::status(path, ec);  // follows symlinks
    if (st.type() == fs::file_type::not_found) return file_error("file.not_found", path, "file does not exist");
    if (ec) return file_error("file.io_error", path, "cannot inspect file: " + ec.message());
    if (st.type() != fs::file_type::regular) {
        return file_error("file.not_regular", path, "not a regular file");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return file_error("file.read_failed", path, "cannot open for reading");

    // Bounded read: stop as soon as the cap is exceeded rather than trusting
    // the size reported by stat (the file may grow while we read).
    std::string data;
    char buf[64 * 1024];
    while (in.read(buf, sizeof buf) || in.gcount() > 0) {
        data.append(buf, static_cast<std::size_t>(in.gcount()));
        if (data.size() > max_bytes) {
            return file_error("file.too_large", path,
                              "file is larger than the limit of " + std::to_string(max_bytes) + " bytes");
        }
    }
    if (in.bad()) return file_error("file.read_failed", path, "read error");

    auto loaded = load_scene_from_string(data);
    if (!loaded) return file_error(loaded.error().code.c_str(), path, loaded.error().message);
    return loaded;
}

}  // namespace k
