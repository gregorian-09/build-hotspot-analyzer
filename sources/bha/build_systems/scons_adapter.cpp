#include "bha/build_systems/adapter.hpp"
#include "bha/build_systems/adapter_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <tuple>

namespace bha::build_systems {
    using namespace detail;

    // SCons Adapter
    // --------------------------------------------------------------------------

    class SConsAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "SCons"; }

        [[nodiscard]] std::string description() const override {
            return "SCons build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / "SConstruct")) {
                return 0.9;
            }
            if (fs::exists(project_path / "sconstruct")) {
                return 0.9;
            }
            if (fs::exists(project_path / "SConscript")) {
                return 0.7;
            }
            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)project_path;
            (void)options;
            return Result<void, Error>::success();
        }

        Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            BuildResult result;
            auto start = std::chrono::steady_clock::now();

            if (options.clean_first) {
                clean(project_path, options);
            }

            auto [type, c_compiler, cxx_compiler] = get_compiler_info(options);
            auto flags = CompilerFlags::for_compiler(type, options.enable_tracing, options.enable_memory_profiling);

            const fs::path trace_output_dir = options.trace_output_dir.empty() && options.enable_tracing
                ? project_path / "traces" : options.trace_output_dir;

            std::ostringstream cmd;

            if (options.enable_tracing && !trace_output_dir.empty() && needs_capture_script(type)) {
                fs::create_directories(trace_output_dir);
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << fs::absolute(trace_output_dir).string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << fs::absolute(trace_output_dir).string() << "\" ";
#endif
            }

            cmd << "scons";
            cmd << " -j" << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.enable_tracing && needs_capture_script(type)) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    cmd << " CC=\"" << script.string() << " " << c_compiler << "\"";
                    cmd << " CXX=\"" << script.string() << " " << cxx_compiler << "\"";
                } else if (!c_compiler.empty()) {
                    cmd << " CC=\"" << c_compiler << "\"";
                    cmd << " CXX=\"" << cxx_compiler << "\"";
                }
            } else if (!c_compiler.empty()) {
                cmd << " CC=\"" << c_compiler << "\"";
                cmd << " CXX=\"" << cxx_compiler << "\"";
            }

            std::string base_flags = "-fPIC";
            if (!flags.empty()) {
                base_flags += " " + flags.combined();
            }
            cmd << " CFLAGS=\"" << base_flags << "\"";
            cmd << " CXXFLAGS=\"" << base_flags << "\"";

            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            auto [exit_code, output] = execute_command(
                cmd.str(),
                project_path,
                options.on_output_line,
                options.should_cancel
            );

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                const std::string error_summary = extract_error_summary(output);
                result.error_message = error_summary.empty() ? "Build failed" : error_summary;
            }

            result.trace_files = find_trace_files(project_path);
            if (needs_capture_script(type) && !trace_output_dir.empty()) {
                auto trace_dir_files = find_trace_files(trace_output_dir);
                result.trace_files.insert(result.trace_files.end(), trace_dir_files.begin(), trace_dir_files.end());
            }

            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(project_path);
            }

            copy_trace_files(project_path, trace_output_dir, result.trace_files, result.memory_files);

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            const std::string cmd = "scons -c";

            if (auto [exit_code, output] = execute_command(cmd, project_path); exit_code != 0) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::InternalError, "Clean failed: " + output)
                );
            }

            return Result<void, Error>::success();
        }

        Result<fs::path, Error> get_compile_commands(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            const fs::path compile_commands = project_path / "compile_commands.json";

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            const std::string cmd = "bear -- scons -j" + std::to_string(get_cpu_count());

            if (auto [exit_code, output] = execute_command(cmd, project_path);
                exit_code == 0 && fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound,
                      "compile_commands.json not found. Install 'bear' or use scons-compiledb.")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_scons_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<SConsAdapter>()
        );
    }

}  // namespace bha::build_systems
