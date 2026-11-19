// Scratch directory helper for tests.
#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "Engine/Identity/Uuid.h"

namespace ktest {

// Unique scratch directory under the system temp path, removed on scope exit.
class TempDir {
public:
    TempDir() : path_(std::filesystem::temp_directory_path() / ("kengine_test_" + k::Uuid::generate().to_string())) {
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path_, ec); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

    std::vector<std::string> listing() const {
        std::vector<std::string> names;
        for (const auto& e : std::filesystem::directory_iterator(path_)) names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }

private:
    std::filesystem::path path_;
};


}  // namespace ktest
