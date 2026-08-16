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

    // Bazel Adapter
    // --------------------------------------------------------------------------

    class BazelAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Bazel"; }

        [[nodiscard]] std::string description() const override {
            return "Bazel build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / "WORKSPACE") || fs::exists(project_path / "WORKSPACE.bazel")) {
                return 0.95;
            }
            if (fs::exists(project_path / "MODULE.bazel")) {
                return 0.95;
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

            auto compiler = get_compiler_info(options);

            std::ostringstream cmd;
            cmd << "bazel build //...";
            cmd << " --jobs=" << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.build_type == "Debug") {
                cmd << " -c dbg";
            } else {
                cmd << " -c opt";
            }

            const fs::path trace_output_dir = options.trace_output_dir.empty() ? project_path : options.trace_output_dir;
            const fs::path profile_path = trace_output_dir / "bazel_profile.json";

            if (options.enable_tracing) {
                std::error_code ec;
                fs::create_directories(trace_output_dir, ec);
                cmd << " --profile=" << fs::absolute(profile_path).string();
                cmd << " --generate_json_trace_profile";
                if (auto flags = CompilerFlags::for_compiler(compiler.type, true, options.enable_memory_profiling); !flags.empty()) {
                    for (const auto& flag : {flags.tracing_flags, flags.memory_flags}) {
                        if (!flag.empty()) {
                            std::istringstream iss(flag);
                            std::string f;
                            while (iss >> f) {
                                cmd << " --copt=" << f;
                            }
                        }
                    }
                }
            }

            if (options.verbose) {
                cmd << " --verbose_failures";
            }

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

            if (options.enable_tracing && fs::exists(profile_path)) {
                result.trace_files.push_back(profile_path);
            }

            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(project_path);
            }

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            const std::string cmd = "bazel clean";

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

            const std::string cmd = "bazel run @hedron_compile_commands//:refresh_all 2>/dev/null || "
                                   "bazel run //:refresh_compile_commands 2>/dev/null";

            if (auto [exit_code, output] = execute_command(cmd, project_path);
                exit_code == 0 && fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound,
                      "compile_commands.json not found. Use hedron_compile_commands or similar.")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_bazel_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<BazelAdapter>()
        );
    }

}  // namespace bha::build_systems
