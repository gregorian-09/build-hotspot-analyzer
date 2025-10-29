//
// Created by gregorian on 21/10/2025.
//

#ifndef NINJA_ADAPTER_H
#define NINJA_ADAPTER_H

#include <filesystem>
#include "bha/build_systems/build_adapter.h"

namespace bha::build_systems {

    /**
     * Represents a single build entry parsed from Ninja’s build log.
     */
    struct NinjaBuildEntry {
        std::string target;     ///< The target file or rule name.
        uint64_t start_time_ms; ///< Build start time in milliseconds.
        uint64_t end_time_ms;   ///< Build end time in milliseconds.
        uint64_t duration_ms;   ///< Total duration of the build step in milliseconds.
        uint32_t restat;        ///< Indicates if the build was skipped due to restat optimization.
    };

    /**
     * Adapter implementation for the Ninja build system.
     *
     * Provides integration with Ninja build files (`build.ninja`), logs (`.ninja_log`),
     * and dependency information. Enables tracing, extraction of compile commands,
     * and build order reconstruction for analysis.
     */
    class NinjaAdapter final : public BuildAdapter {
    public:
        /**
         * Constructs a NinjaAdapter for a specific build directory.
         * @param build_dir Path to the directory containing the `build.ninja` file.
         */
        explicit NinjaAdapter(const std::string& build_dir);

        /**
         * Detects whether the given directory uses Ninja as its build system.
         * @param build_dir Path to check for Ninja configuration files.
         * @return Information about the detected Ninja build system.
         */
        core::Result<BuildSystemInfo> detect_build_system(
            const std::string& build_dir
        ) override;

        /**
         * Extracts compile commands from the Ninja build database.
         * @return List of compile commands for all translation units.
         */
        core::Result<std::vector<CompileCommand>> extract_compile_commands() override;

        /**
         * Retrieves Ninja trace or log files for profiling and diagnostics.
         * @param build_dir Path to the build directory.
         * @return List of trace file paths.
         */
        core::Result<std::vector<std::string>> get_trace_files(
            const std::string& build_dir
        ) override;

        /**
         * Lists all build targets defined in the Ninja build file.
         * @return Map of target names to their corresponding dependencies or outputs.
         */
        [[nodiscard]] core::Result<std::map<std::string, std::vector<std::string>>> get_targets() override;

        /**
         * Determines the order in which Ninja builds targets.
         * @return Ordered list of target names reflecting build dependencies.
         */
        [[nodiscard]] core::Result<std::vector<std::string>> get_build_order() override;

        /**
         * Enables build tracing or instrumentation within Ninja builds.
         * @param build_dir Path to the build directory.
         * @param compiler_type Compiler type (e.g., clang, gcc, msvc).
         * @return True if tracing was successfully enabled.
         */
        core::Result<bool> enable_tracing(
            const std::string& build_dir,
            const std::string& compiler_type
        ) override;

        /**
         * Parses the `.ninja_log` file to extract build timings and targets.
         * @return Vector of parsed NinjaBuildEntry objects.
         */
        [[nodiscard]] core::Result<std::vector<NinjaBuildEntry>> parse_ninja_log() const;

    private:
        std::filesystem::path ninja_log_path_;   ///< Path to `.ninja_log` file.
        std::filesystem::path ninja_build_path_; ///< Path to `build.ninja` file.
        std::filesystem::path ninja_deps_path_;  ///< Path to `.ninja_deps` file.

        /**
         * Retrieves the installed Ninja version.
         * @return Version string of the detected Ninja executable.
         */
        static core::Result<std::string> get_ninja_version();

        /**
         * Parses the main `build.ninja` file for build rules and targets.
         * @return List of rule or target names parsed from the build file.
         */
        static core::Result<std::vector<std::string>> parse_build_file();

        /**
         * Parses the `.ninja_deps` log to retrieve dependency relationships.
         * @return Map of target-to-dependency relationships.
         */
        static core::Result<std::map<std::string, std::string>> parse_deps_log();
    };

} // namespace bha::build_systems


#endif //NINJA_ADAPTER_H
