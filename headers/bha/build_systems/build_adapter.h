//
// Created by gregorian on 21/10/2025.
//

#ifndef BUILD_ADAPTER_H
#define BUILD_ADAPTER_H

#include "bha/core/types.h"
#include "bha/core/result.h"
#include <string>
#include <vector>
#include <map>

namespace bha::build_systems {

    /**
     * @enum BuildSystemType
     * Enumerates supported build system types.
     */
    enum class BuildSystemType {
        CMAKE,   ///< CMake build system
        NINJA,   ///< Ninja build system
        MAKE,    ///< GNU Make build system
        MSBUILD, ///< Microsoft MSBuild build system
        UNKNOWN  ///< Unknown or unsupported build system
    };

    /**
     * @struct BuildSystemInfo
     * Describes information about a detected build system.
     *
     * Includes the type, version, build directory and source directory.
     */
    struct BuildSystemInfo {
        BuildSystemType type;        ///< Detected build system type
        std::string version;         ///< Version string of the build system
        std::string build_directory; ///< Path to the build output directory
        std::string source_directory;//< Path to the source code directory
    };

    /**
     * @struct CompileCommand
     * Represents a single compile‐command extracted from a build system.
     *
     * Contains the file being compiled, the working directory, the full command,
     * individual arguments, and the output artifact.
     */
    struct CompileCommand {
        std::string file;              ///< Source file path being compiled
        std::string directory;         ///< Working directory for the compile command
        std::string command;           ///< Full compile command string
        std::vector<std::string> arguments; ///< Individual command line arguments
        std::string output;            ///< Path of the output artifact (object file)
    };

    /**
     * @class BuildAdapter
     * Abstract interface for build‐system adaptation and information extraction.
     *
     * Concrete implementations should detect the build system, extract compile commands,
     * retrieve trace/log files, enumerate targets, obtain build order, and enable tracing.
     */
    class BuildAdapter {
    public:
        /** Virtual destructor. */
        virtual ~BuildAdapter() = default;

        /**
         * Detects the build system used in the specified build directory.
         *
         * @param build_dir Path to the build directory to inspect.
         * @return A Result containing a @ref BuildSystemInfo on success.
         */
        virtual core::Result<BuildSystemInfo> detect_build_system(
            const std::string& build_dir
        ) = 0;

        /**
         * Extracts compile commands from the build system invocation.
         *
         * @return A Result containing a vector of @ref CompileCommand entries.
         */
        virtual core::Result<std::vector<CompileCommand>> extract_compile_commands() = 0;

        /**
         * Retrieves trace or log files relevant to the build.
         *
         * @param build_dir Path to the build directory.
         * @return A Result containing a vector of file paths.
         */
        virtual core::Result<std::vector<std::string>> get_trace_files(
            const std::string& build_dir
        ) = 0;

        /**
         * Retrieves mapping from target names to the files they contain.
         *
         * @return A Result containing a map from target name to list of file paths.
         */
        virtual core::Result<std::map<std::string, std::vector<std::string>>> get_targets() = 0;

        /**
         * Retrieves the build order of targets or files from the build system.
         *
         * @return A Result containing a vector of file or target paths in build order.
         */
        virtual core::Result<std::vector<std::string>> get_build_order() = 0;

        /**
         * Enables compiler tracing in the build system for profiling or analysis.
         *
         * @param build_dir Path to the build directory.
         * @param compiler_type The compiler type (e.g., "clang", "gcc") to enable tracing for.
         * @return A Result containing a bool indicating success.
         */
        virtual core::Result<bool> enable_tracing(
            const std::string& build_dir,
            const std::string& compiler_type
        ) = 0;

    protected:
        std::string build_dir_; ///< Build directory that this adapter operates on
    };

    /**
     * @class BuildAdapterFactory
     * Factory for creating appropriate build system adapters.
     *
     * Detects the build system type from a build directory and returns a concrete adapter.
     */
    class BuildAdapterFactory {
    public:
        /**
         * Creates an adapter instance suited to the build directory.
         *
         * @param build_dir Path to the build directory.
         * @return A Result containing a unique_ptr to a @ref BuildAdapter.
         */
        static core::Result<std::unique_ptr<BuildAdapter>> create_adapter(
            const std::string& build_dir
        );

        /**
         * Detects the build system type in the specified directory.
         *
         * @param build_dir Path to the build directory.
         * @return A Result containing the detected @ref BuildSystemType.
         */
        static core::Result<BuildSystemType> detect_build_system_type(
            const std::string& build_dir
        );
    };
}

#endif //BUILD_ADAPTER_H
