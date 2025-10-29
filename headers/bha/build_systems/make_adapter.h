//
// Created by gregorian on 21/10/2025.
//

#ifndef MAKE_ADAPTER_H
#define MAKE_ADAPTER_H

#include "bha/build_systems/build_adapter.h"
#include <filesystem>

namespace bha::build_systems {

    /**
     * Represents a Makefile target with its dependencies and build commands.
     */
    struct MakeTarget {
        std::string name;                      ///< Name of the Make target.
        std::vector<std::string> dependencies; ///< List of dependent files or targets.
        std::vector<std::string> commands;     ///< Commands used to build this target.
    };

    /**
     * Adapter implementation for the GNU Make build system.
     *
     * Provides functionality to detect Make-based projects, extract compile
     * commands, parse Makefiles, determine build targets, and enable tracing.
     */
    class MakeAdapter final : public BuildAdapter {
    public:
        /**
         * Constructs a MakeAdapter for the specified build directory.
         * @param build_dir Path to the directory containing the Makefile.
         */
        explicit MakeAdapter(const std::string& build_dir);

        /**
         * Detects whether the given directory uses GNU Make as its build system.
         * @param build_dir Path to check for Makefile presence.
         * @return Information about the detected Make build system.
         */
        core::Result<BuildSystemInfo> detect_build_system(
            const std::string& build_dir
        ) override;

        /**
         * Extracts compile commands by analyzing Make build output.
         * @return List of compile commands.
         */
        core::Result<std::vector<CompileCommand>> extract_compile_commands() override;

        /**
         * Retrieves trace or log files produced by the Make build.
         * @param build_dir Path to the build directory.
         * @return List of trace file paths.
         */
        core::Result<std::vector<std::string>> get_trace_files(
            const std::string& build_dir
        ) override;

        /**
         * Lists all targets defined in the Makefile.
         * @return Map of target names and their corresponding dependencies.
         */
        [[nodiscard]] core::Result<std::map<std::string, std::vector<std::string>>> get_targets() override;

        /**
         * Determines the order in which Make builds its targets.
         * @return Ordered list of target names.
         */
        [[nodiscard]] core::Result<std::vector<std::string>> get_build_order() override;

        /**
         * Enables tracing or instrumentation during Make builds.
         * @param build_dir Path to the build directory.
         * @param compiler_type Compiler type (e.g., clang, gcc).
         * @return True if tracing was successfully enabled.
         */
        core::Result<bool> enable_tracing(
            const std::string& build_dir,
            const std::string& compiler_type
        ) override;

        /**
         * Parses a Makefile and extracts target definitions.
         * @param makefile_path Path to the Makefile to parse.
         * @return Vector of parsed MakeTarget objects.
         */
        static core::Result<std::vector<MakeTarget>> parse_makefile(
            const std::string& makefile_path
        );

    private:
        std::filesystem::path makefile_path_; ///< Path to the Makefile.
        std::filesystem::path make_log_path_; ///< Path to Make build log file.

        /**
         * Retrieves the installed Make version.
         * @return Version string of the detected Make installation.
         */
        static core::Result<std::string> get_make_version();

        /**
         * Executes a dry run of Make to obtain target and command information.
         * @return Output from running `make -n`.
         */
        static core::Result<std::string> run_make_dry_run();

        /**
         * Extracts compile commands from the output of a Make dry run.
         * @param make_output Output from `make -n`.
         * @return List of extracted compile commands.
         */
        static std::vector<std::string> extract_compile_commands_from_output(
            const std::string& make_output
        );
    };
} // namespace bha::build_systems

#endif //MAKE_ADAPTER_H
