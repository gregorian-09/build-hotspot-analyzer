#include "bha/build_systems/adapter.hpp"
#include "bha/build_systems/adapter_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tuple>

namespace bha::build_systems {
    using namespace detail;

    // Meson Adapter
    // --------------------------------------------------------------------------

    class MesonAdapter final : public IBuildSystemAdapter
    {
    public:
        [[nodiscard]] std::string name() const override { return "Meson"; }

        [[nodiscard]] std::string description() const override {
            return "Meson build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / "meson.build")) {
                return 0.95;
            }
            for (const auto& dir : {"build", "builddir", "out"}) {
                if (fs::exists(project_path / dir / "build.ninja") &&
                    fs::exists(project_path / dir / "meson-private")) {
                    return 0.85;
                    }
            }
            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            const fs::path build_dir = options.build_dir.empty() ? project_path / "builddir" : options.build_dir;

            if (fs::exists(build_dir / "meson-private")) {
                return Result<void, Error>::success();
            }

            fs::create_directories(build_dir);

            const auto [type, c_compiler, cxx_compiler] = get_compiler_info(options);
            auto flags = CompilerFlags::for_compiler(type, options.enable_tracing, options.enable_memory_profiling);

            std::string capture_launcher;
            if (options.enable_tracing && needs_capture_script(type)) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    capture_launcher = script.string() + " ";
                } else {
                    std::cerr << "Warning: bha-capture script not found. "
                              << "GCC tracing requires this script.\n"
                              << "Set BHA_SCRIPT_DIR to the directory containing bha-capture.sh\n";
                }
            }

            std::ostringstream cmd;
            cmd << "CC=\"" << capture_launcher << c_compiler << "\" ";
            cmd << "CXX=\"" << capture_launcher << cxx_compiler << "\" ";
            cmd << "meson setup";
            cmd << " \"" << build_dir.string() << "\"";
            cmd << " \"" << project_path.string() << "\"";
            cmd << " --buildtype=" << (options.build_type == "Debug" ? "debug" :
                                       options.build_type == "Release" ? "release" :
                                       "debugoptimized");

            if (!flags.empty()) {
                cmd << " -Dc_args='" << flags.combined() << "'";
                cmd << " -Dcpp_args='" << flags.combined() << "'";
            }

            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::InternalError, "Meson configure failed: " + output)
                );
            }

            return Result<void, Error>::success();
        }

        Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            BuildResult result;
            auto start = std::chrono::steady_clock::now();

            const fs::path build_dir = options.build_dir.empty() ? project_path / "builddir" : options.build_dir;

            // For clean builds, remove the build directory to ensure fresh configuration
            if (options.clean_first && fs::exists(build_dir)) {
                std::error_code ec;
                fs::remove_all(build_dir, ec);
            }

            if (!fs::exists(build_dir / "build.ninja") && !fs::exists(build_dir / "meson-private")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            auto compiler = get_compiler_info(options);

            const fs::path trace_output_dir = options.trace_output_dir.empty() && options.enable_tracing
                ? build_dir / "traces" : options.trace_output_dir;

            std::ostringstream cmd;
            if (options.enable_tracing && !trace_output_dir.empty() && needs_capture_script(compiler.type)) {
                fs::create_directories(trace_output_dir);
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << fs::absolute(trace_output_dir).string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << fs::absolute(trace_output_dir).string() << "\" ";
#endif
            }

            cmd << "meson compile -C \"" << build_dir.string() << "\"";
            cmd << " -j " << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.verbose) {
                cmd << " -v";
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

            result.trace_files = find_trace_files(build_dir);
            if (needs_capture_script(compiler.type) && !trace_output_dir.empty()) {
                auto trace_dir_files = find_trace_files(trace_output_dir);
                result.trace_files.insert(result.trace_files.end(), trace_dir_files.begin(), trace_dir_files.end());
            }

            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(build_dir);
            }

            copy_trace_files(build_dir, trace_output_dir, result.trace_files, result.memory_files);

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            fs::path build_dir = options.build_dir;
            if (build_dir.empty()) {
                build_dir = project_path / "builddir";
            }

            const std::string cmd = "meson compile -C \"" + build_dir.string() + "\" --clean";

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
            fs::path build_dir = options.build_dir;
            if (build_dir.empty()) {
                build_dir = project_path / "builddir";
            }

            const fs::path compile_commands = build_dir / "compile_commands.json";

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            if (!fs::exists(build_dir / "meson-private")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    return Result<fs::path, Error>::failure(config_result.error());
                }
            }

            const std::string cmd = "meson introspect --targets \"" + build_dir.string() +
                              "\" > /dev/null 2>&1";
            execute_command(cmd, project_path);

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound, "compile_commands.json not found")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_meson_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<MesonAdapter>()
        );
    }

}  // namespace bha::build_systems
