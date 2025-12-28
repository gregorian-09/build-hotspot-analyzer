//
// Created by gregorian on 15/12/2025.
//

#ifndef BUILD_DETECTOR_H
#define BUILD_DETECTOR_H

#include "bha/core/result.h"
#include <string>
#include <vector>
#include <filesystem>

namespace bha::cli {

    enum class BuildSystemType {
        CMAKE,
        NINJA,
        MAKE,
        MSBUILD,
        XCODE,
        BAZEL,
        UNKNOWN
    };

    struct BuildSystemInfo {
        BuildSystemType type;
        std::filesystem::path root_dir;
        std::filesystem::path build_dir;
        std::vector<std::filesystem::path> config_files;
        std::string detected_compiler;
    };

    struct TraceFileInfo {
        std::filesystem::path path;
        std::string compiler_type;
        size_t file_size;
        std::filesystem::file_time_type modified_time;
    };

    class BuildDetector {
    public:
        static core::Result<BuildSystemInfo> detect_build_system(
            const std::filesystem::path& start_dir = std::filesystem::current_path()
        );

        static core::Result<std::vector<TraceFileInfo>> find_trace_files(
            const std::filesystem::path& search_dir = std::filesystem::current_path(),
            bool recursive = true
        );

        static core::Result<std::filesystem::path> find_build_directory(
            const std::filesystem::path& project_root = std::filesystem::current_path()
        );

        static core::Result<std::filesystem::path> find_project_root(
            const std::filesystem::path& start_dir = std::filesystem::current_path()
        );

        static core::Result<std::string> detect_compiler(
            const BuildSystemInfo& build_info
        );

        static std::string build_system_to_string(BuildSystemType type);

        static bool is_trace_file(const std::filesystem::path& path);

        static std::string guess_compiler_from_trace(const std::filesystem::path& trace_path);

    private:
        static BuildSystemType detect_from_files(
            const std::filesystem::path& dir,
            std::vector<std::filesystem::path>& config_files
        );

        static bool has_file(const std::filesystem::path& dir, const std::string& filename);

        static std::vector<std::filesystem::path> find_files_recursive(
            const std::filesystem::path& dir,
            const std::string& pattern,
            int max_depth = 5
        );
    };

} // namespace bha::cli

#endif //BUILD_DETECTOR_H
