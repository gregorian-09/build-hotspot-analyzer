//
// Created by gregorian on 15/12/2025.
//

#include "bha/cli/build_detector.h"
#include <fstream>
#include <algorithm>

namespace bha::cli {

    core::Result<BuildSystemInfo> BuildDetector::detect_build_system(const std::filesystem::path& start_dir) {
        BuildSystemInfo info;
        info.root_dir = start_dir;

        std::filesystem::path search_dir = start_dir;

        for (int depth = 0; depth < 10; ++depth) {
            info.type = detect_from_files(search_dir, info.config_files);

            if (info.type != BuildSystemType::UNKNOWN) {
                info.root_dir = search_dir;

                if (auto build_dir_result = find_build_directory(search_dir); build_dir_result.is_success()) {
                    info.build_dir = build_dir_result.value();
                }

                if (auto compiler_result = detect_compiler(info); compiler_result.is_success()) {
                    info.detected_compiler = compiler_result.value();
                }

                return core::Result<BuildSystemInfo>::success(info);
            }

            if (!search_dir.has_parent_path() || search_dir == search_dir.parent_path()) {
                break;
            }
            search_dir = search_dir.parent_path();
        }

        return core::Result<BuildSystemInfo>::failure(
            core::ErrorCode::NOT_FOUND,
            "No build system detected in directory hierarchy"
        );
    }

    BuildSystemType BuildDetector::detect_from_files(
        const std::filesystem::path& dir,
        std::vector<std::filesystem::path>& config_files
    ) {
        if (has_file(dir, "CMakeLists.txt")) {
            config_files.push_back(dir / "CMakeLists.txt");
            return BuildSystemType::CMAKE;
        }

        if (has_file(dir, "build.ninja")) {
            config_files.push_back(dir / "build.ninja");
            return BuildSystemType::NINJA;
        }

        if (has_file(dir, "Makefile") || has_file(dir, "makefile")) {
            config_files.push_back(dir / (has_file(dir, "Makefile") ? "Makefile" : "makefile"));
            return BuildSystemType::MAKE;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() == ".sln") {
                config_files.push_back(entry.path());
                return BuildSystemType::MSBUILD;
            }
            if (entry.path().extension() == ".xcodeproj") {
                config_files.push_back(entry.path());
                return BuildSystemType::XCODE;
            }
        }

        if (has_file(dir, "BUILD") || has_file(dir, "BUILD.bazel")) {
            config_files.push_back(dir / (has_file(dir, "BUILD") ? "BUILD" : "BUILD.bazel"));
            return BuildSystemType::BAZEL;
        }

        return BuildSystemType::UNKNOWN;
    }

    core::Result<std::vector<TraceFileInfo>> BuildDetector::find_trace_files(
        const std::filesystem::path& search_dir,
        const bool recursive
    ) {
        std::vector<TraceFileInfo> traces;

        auto process_file = [&](const std::filesystem::path& path) {
            if (is_trace_file(path)) {
                TraceFileInfo info;
                info.path = path;
                info.compiler_type = guess_compiler_from_trace(path);
                info.file_size = std::filesystem::file_size(path);
                info.modified_time = std::filesystem::last_write_time(path);
                traces.push_back(info);
            }
        };

        try {
            if (recursive) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                    search_dir,
                    std::filesystem::directory_options::skip_permission_denied
                )) {
                    if (entry.is_regular_file()) {
                        process_file(entry.path());
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(search_dir)) {
                    if (entry.is_regular_file()) {
                        process_file(entry.path());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            return core::Result<std::vector<TraceFileInfo>>::failure(
                core::ErrorCode::FILE_READ_ERROR,
                std::string("Filesystem error: ") + e.what()
            );
        }

        std::ranges::sort(traces, [](const TraceFileInfo& a, const TraceFileInfo& b) {
            return a.modified_time > b.modified_time;
        });

        return core::Result<std::vector<TraceFileInfo>>::success(traces);
    }

    core::Result<std::filesystem::path> BuildDetector::find_build_directory(
        const std::filesystem::path& project_root
    ) {
        const std::vector<std::string> common_build_dirs = {
            "build", "Build", "BUILD",
            "cmake-build-debug", "cmake-build-release",
            "out", "output",
            "_build", ".build",
            "target/debug", "target/release"
        };

        for (const auto& dir_name : common_build_dirs) {
            if (std::filesystem::path build_path = project_root / dir_name; std::filesystem::exists(build_path) && std::filesystem::is_directory(build_path)) {
                return core::Result<std::filesystem::path>::success(build_path);
            }
        }

        return core::Result<std::filesystem::path>::failure(
            core::ErrorCode::NOT_FOUND,
            "No build directory found"
        );
    }

    core::Result<std::filesystem::path> BuildDetector::find_project_root(
        const std::filesystem::path& start_dir
    ) {
        std::filesystem::path search_dir = start_dir;

        for (int depth = 0; depth < 10; ++depth) {
            if (has_file(search_dir, ".git") ||
                has_file(search_dir, "CMakeLists.txt") ||
                has_file(search_dir, "Makefile") ||
                has_file(search_dir, "BUILD.bazel")) {
                return core::Result<std::filesystem::path>::success(search_dir);
                }

            if (!search_dir.has_parent_path() || search_dir == search_dir.parent_path()) {
                break;
            }
            search_dir = search_dir.parent_path();
        }

        return core::Result<std::filesystem::path>::failure(
            core::ErrorCode::NOT_FOUND,
            "Project root not found"
        );
    }

    core::Result<std::string> BuildDetector::detect_compiler(const BuildSystemInfo& build_info) {
        if (build_info.type == BuildSystemType::CMAKE) {
            if (const std::filesystem::path cache_file = build_info.build_dir / "CMakeCache.txt"; std::filesystem::exists(cache_file)) {
                std::ifstream file(cache_file);
                std::string line;
                while (std::getline(file, line)) {
                    if (line.find("CMAKE_CXX_COMPILER:") != std::string::npos) {
                        if (const auto pos = line.find('='); pos != std::string::npos) {
                            if (std::string compiler_path = line.substr(pos + 1); compiler_path.find("clang") != std::string::npos) {
                                return core::Result<std::string>::success("clang");
                            } else {
                                if (compiler_path.find("g++") != std::string::npos ||
                                    compiler_path.find("gcc") != std::string::npos) {
                                    return core::Result<std::string>::success("gcc");
                                }
                                if (compiler_path.find("cl.exe") != std::string::npos ||
                                    compiler_path.find("msvc") != std::string::npos) {
                                    return core::Result<std::string>::success("msvc");
                                }
                            }
                        }
                    }
                }
            }
        }

        return core::Result<std::string>::failure(
            core::ErrorCode::NOT_FOUND,
            "Could not detect compiler"
        );
    }

    std::string BuildDetector::build_system_to_string(BuildSystemType type) {
        switch (type) {
            case BuildSystemType::CMAKE: return "CMake";
            case BuildSystemType::NINJA: return "Ninja";
            case BuildSystemType::MAKE: return "Make";
            case BuildSystemType::MSBUILD: return "MSBuild";
            case BuildSystemType::XCODE: return "Xcode";
            case BuildSystemType::BAZEL: return "Bazel";
            case BuildSystemType::UNKNOWN: return "Unknown";
        }
        return "Unknown";
    }

    bool BuildDetector::is_trace_file(const std::filesystem::path& path) {
        const std::string ext = path.extension().string();
        const std::string filename = path.filename().string();

        if (ext == ".json") {
            if (filename.find("trace") != std::string::npos ||
                filename.find("time-trace") != std::string::npos ||
                filename.find("build") != std::string::npos) {
                return true;
            }

            if (std::ifstream file(path); file.is_open()) {
                std::string first_line;
                std::getline(file, first_line);
                if (first_line.find("traceEvents") != std::string::npos ||
                    first_line.find("compilation_units") != std::string::npos) {
                    return true;
                }
            }
        }

        return false;
    }

    std::string BuildDetector::guess_compiler_from_trace(const std::filesystem::path& trace_path) {
        std::ifstream file(trace_path);
        if (!file.is_open()) {
            return "unknown";
        }

        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (content.find("traceEvents") != std::string::npos &&
            content.find("\"ph\":") != std::string::npos) {
            return "clang";
        }

        if (content.find("compilation_units") != std::string::npos) {
            return "unified";
        }

        if (content.find("time report") != std::string::npos ||
            content.find("TOTAL") != std::string::npos) {
            return "gcc";
        }

        return "unknown";
    }

    bool BuildDetector::has_file(const std::filesystem::path& dir, const std::string& filename) {
        try {
            return std::filesystem::exists(dir / filename);
        } catch (...) {
            return false;
        }
    }

} // namespace bha::cli