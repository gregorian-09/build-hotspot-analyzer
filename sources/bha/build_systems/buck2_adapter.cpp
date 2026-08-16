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

    // Buck2 Adapter
    // --------------------------------------------------------------------------

    class Buck2Adapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Buck2"; }

        [[nodiscard]] std::string description() const override {
            return "Buck2 build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / ".buckconfig")) {
                return 0.9;
            }
            if (fs::exists(project_path / "BUCK") || fs::exists(project_path / "TARGETS")) {
                return 0.85;
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

            const fs::path trace_output_dir = options.trace_output_dir.empty() ? project_path : options.trace_output_dir;
            const fs::path profile_path = trace_output_dir / "buck2_profile.json";

            std::ostringstream cmd;
            cmd << "buck2 build //...";
            cmd << " --num-threads=" << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.build_type == "Debug") {
                cmd << " --config=cxx.default_flavor=debug";
            }

            if (options.enable_tracing) {
                std::error_code ec;
                fs::create_directories(trace_output_dir, ec);
                cmd << " --profile-output=" << fs::absolute(profile_path).string();
            }

            if (options.verbose) {
                cmd << " -v 2";
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

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            const std::string cmd = "buck2 clean";

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

            if (const fs::path compile_commands = project_path / "compile_commands.json"; fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound,
                      "compile_commands.json not found. Buck2 doesn't generate it natively.")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_buck2_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<Buck2Adapter>()
        );
    }

}  // namespace bha::build_systems
