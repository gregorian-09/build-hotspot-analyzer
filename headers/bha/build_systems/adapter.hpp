//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BHA_ADAPTER_HPP
#define BHA_ADAPTER_HPP

#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/types.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bha::build_systems
{
    namespace fs = std::filesystem;

    /**
     * Build configuration options.
     */
    struct BuildOptions {
        /** Build type (Debug, Release, etc.) */
        std::string build_type = "Release";

        /** Number of parallel jobs (-j) */
        int parallel_jobs = 0;  // 0 = auto-detect

        /** Extra arguments to pass to the build system */
        std::vector<std::string> extra_args;

        /** Directory for build artifacts */
        fs::path build_dir;

        /** Enable time tracing output */
        bool enable_tracing = true;

        /** Compiler to use (auto-detected if empty) */
        std::string compiler;

        /** Clean before build */
        bool clean_first = false;

        /** Verbose output */
        bool verbose = false;
    };

    /**
     * Result of running a build.
     */
    struct BuildResult {
        /** Whether the build succeeded */
        bool success = false;

        /** Path to trace output file(s) */
        std::vector<fs::path> trace_files;

        /** Build duration */
        Duration build_time = Duration::zero();

        /** Number of files compiled */
        std::size_t files_compiled = 0;

        /** Build output/log */
        std::string output;

        /** Error message if failed */
        std::string error_message;
    };

    /**
     * Interface for build system adapters.
     *
     * Adapters handle the specifics of different build systems (CMake, Ninja, etc.)
     * providing a uniform interface for:
     * - Detecting the build system in a project
     * - Configuring for tracing
     * - Running builds with tracing enabled
     * - Locating trace output
     */
    class IBuildSystemAdapter {
    public:
        virtual ~IBuildSystemAdapter() = default;

        /** Get the name of this build system (e.g., "CMake", "Ninja") */
        virtual std::string name() const = 0;

        /** Get a description of this adapter */
        virtual std::string description() const = 0;

        /**
         * Check if this adapter can handle the given project.
         *
         * @param project_path Path to the project root
         * @return Confidence level (0.0-1.0), 0 if cannot handle
         */
        virtual double detect(const fs::path& project_path) const = 0;

        /**
         * Configure the project for building with tracing.
         *
         * @param project_path Path to the project root
         * @param options Build options
         * @return Success or error
         */
        virtual Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) = 0;

        /**
         * Build the project with tracing enabled.
         *
         * @param project_path Path to the project root
         * @param options Build options
         * @return Build result with trace file paths
         */
        virtual Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) = 0;

        /**
         * Clean build artifacts.
         *
         * @param project_path Path to the project root
         * @param options Build options
         * @return Success or error
         */
        virtual Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) = 0;

        /**
         * Get compile commands for the project.
         *
         * @param project_path Path to the project root
         * @param options Build options
         * @return Path to compile_commands.json or similar
         */
        virtual Result<fs::path, Error> get_compile_commands(
            const fs::path& project_path,
            const BuildOptions& options
        ) = 0;
    };

    /**
     * Registry for build system adapters.
     */
    class BuildSystemRegistry {
    public:
        static BuildSystemRegistry& instance();

        BuildSystemRegistry(const BuildSystemRegistry&) = delete;
        BuildSystemRegistry& operator=(const BuildSystemRegistry&) = delete;

        /**
         * Register a build system adapter.
         */
        void register_adapter(std::unique_ptr<IBuildSystemAdapter> adapter);

        /**
         * Get all registered adapters.
         */
        const std::vector<std::unique_ptr<IBuildSystemAdapter>>& adapters() const {
            return adapters_;
        }

        /**
         * Auto-detect and get the best adapter for a project.
         *
         * @param project_path Path to the project root
         * @return Best matching adapter or nullptr
         */
        IBuildSystemAdapter* detect(const fs::path& project_path) const;

        /**
         * Get an adapter by name.
         *
         * @param name Adapter name
         * @return Adapter or nullptr
         */
        IBuildSystemAdapter* get(const std::string& name) const;

    private:
        BuildSystemRegistry() = default;

        std::vector<std::unique_ptr<IBuildSystemAdapter>> adapters_;
    };

    // Registration functions for built-in adapters
    void register_cmake_adapter();
    void register_ninja_adapter();
    void register_make_adapter();
    void register_msbuild_adapter();

    // Register all built-in adapters
    inline void register_all_adapters() {
        register_cmake_adapter();
        register_ninja_adapter();
        register_make_adapter();
        register_msbuild_adapter();
    }

} // namespace bha::build_systems

#endif //BHA_ADAPTER_HPP