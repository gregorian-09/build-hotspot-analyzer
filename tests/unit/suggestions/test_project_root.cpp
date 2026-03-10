//
// Created by gregorian-rayne on 02/17/26.
//

#include "bha/suggestions/suggester.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
    struct TempDir {
        fs::path root;
        explicit TempDir(const std::string& prefix) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root = fs::temp_directory_path() / (prefix + std::to_string(stamp));
            fs::create_directories(root);
        }
        ~TempDir() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_file(const fs::path& path, const std::string& content) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }
}  // namespace

TEST(ProjectRootFromTracePath, UsesCMakeCacheWhenPresent) {
    TempDir temp("bha_root_cache_");

    const fs::path source_root = temp.root / "srcproj";
    write_file(source_root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.10)\n");

    const fs::path build_root = temp.root / "build";
    const fs::path cache_path = build_root / "CMakeCache.txt";
    write_file(cache_path,
               "CMAKE_HOME_DIRECTORY:INTERNAL=" + source_root.generic_string() + "\n");

    const fs::path trace_dir = build_root / "traces";
    fs::create_directories(trace_dir);

    const fs::path resolved = bha::suggestions::find_project_root_from_trace_path(trace_dir);
    EXPECT_EQ(resolved.lexically_normal(), source_root.lexically_normal());
}

TEST(ProjectRootFromTracePath, FindsCacheInAncestorChildDirectory) {
    TempDir temp("bha_root_child_");

    const fs::path source_root = temp.root / "srcproj";
    write_file(source_root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.10)\n");

    const fs::path build_root = temp.root / "out" / "build";
    const fs::path cache_path = build_root / "CMakeCache.txt";
    write_file(cache_path,
               "CMAKE_SOURCE_DIR:INTERNAL=" + source_root.generic_string() + "\n");

    const fs::path trace_dir = temp.root / "logs" / "traces";
    fs::create_directories(trace_dir);

    const fs::path resolved = bha::suggestions::find_project_root_from_trace_path(trace_dir);
    EXPECT_EQ(resolved.lexically_normal(), source_root.lexically_normal());
}
