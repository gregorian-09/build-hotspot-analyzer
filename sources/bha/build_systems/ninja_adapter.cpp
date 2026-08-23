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

    // Ninja Adapter
    // --------------------------------------------------------------------------

    class NinjaAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Ninja"; }

        [[nodiscard]] std::string description() const override {
            return "Ninja build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / "build.ninja")) {
                return 0.95;
            }
            // Common build directories
            for (const auto& dir : {"build", "out", "cmake-build-debug", "cmake-build-release"}) {
                if (fs::exists(project_path / dir / "build.ninja")) {
                    return 0.8;
                }
            }
            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            if (fs::exists(project_path / "CMakeLists.txt")) {
                if (has_git_submodules(project_path)) {
                    init_git_submodules(project_path);
                }

                const fs::path build_dir = options.build_dir.empty() ? project_path / "build" : options.build_dir;
                fs::create_directories(build_dir);

                auto [type, c_compiler, cxx_compiler] = get_compiler_info(options);
                auto flags = CompilerFlags::for_compiler(type, options.enable_tracing, options.enable_memory_profiling);

                std::ostringstream cmd;
                cmd << "cmake -G Ninja";
                cmd << " -S \"" << project_path.string() << "\"";
                cmd << " -B \"" << build_dir.string() << "\"";
                cmd << " -DCMAKE_BUILD_TYPE=" << options.build_type;
                cmd << " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

                if (!c_compiler.empty() && !cxx_compiler.empty()) {
                    cmd << " -DCMAKE_C_COMPILER=\"" << c_compiler << "\"";
                    cmd << " -DCMAKE_CXX_COMPILER=\"" << cxx_compiler << "\"";
                }

                if (options.enable_tracing && needs_capture_script(type)) {
                    if (auto script = find_capture_script(project_path); !script.empty()) {
                        cmd << " -DCMAKE_CXX_COMPILER_LAUNCHER=\"" << script.string() << "\"";
                        cmd << " -DCMAKE_C_COMPILER_LAUNCHER=\"" << script.string() << "\"";
                    } else {
                        std::cerr << "Warning: bha-capture script not found. "
                                  << "GCC/Intel tracing requires this script.\n"
                                  << "Set BHA_SCRIPT_DIR to the directory containing bha-capture.sh\n";
                    }
                }

                if (!flags.empty()) {
                    const std::string combined = flags.combined();
                    cmd << " \"-DCMAKE_CXX_FLAGS=" << combined << "\"";
                    cmd << " \"-DCMAKE_C_FLAGS=" << combined << "\"";
                }

                if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                    return Result<void, Error>::failure(
                        Error(ErrorCode::InternalError, "Ninja configure failed: " + output)
                    );
                }
            }

            return Result<void, Error>::success();
        }

        Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            BuildResult result;
            auto start = std::chrono::steady_clock::now();

            fs::path build_dir = options.build_dir;
            if (build_dir.empty()) {
                build_dir = fs::exists(project_path / "build.ninja") ? project_path : project_path / "build";
            }

            if (options.clean_first && fs::exists(build_dir) && build_dir != project_path) {
                std::error_code ec;
                fs::remove_all(build_dir, ec);
            }

            if (!fs::exists(build_dir / "build.ninja")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            std::ostringstream cmd;
            cmd << "ninja -C \"" << build_dir.string() << "\"";

            const int jobs = options.parallel_jobs > 0 ?
                       options.parallel_jobs : get_cpu_count();
            cmd << " -j " << jobs;

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
                result.error_message = error_summary.empty() ? "Build failed with no specific error output" : error_summary;
            }

            result.trace_files = find_trace_files(build_dir);

            if (!options.trace_output_dir.empty()) {
                auto trace_dir_files = find_trace_files(options.trace_output_dir);
                result.trace_files.insert(result.trace_files.end(), trace_dir_files.begin(), trace_dir_files.end());
            }

            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(build_dir);
            }

            copy_trace_files(build_dir, options.trace_output_dir, result.trace_files, result.memory_files);

            auto end = std::chrono::steady_clock::now();
            result.build_time = std::chrono::duration_cast<Duration>(end - start);

            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            fs::path build_dir = options.build_dir;
            if (build_dir.empty()) {
                build_dir = project_path / "build";
            }

            const std::string cmd = "ninja -C \"" + build_dir.string() + "\" -t clean";

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
                build_dir = project_path / "build";
            }

            const fs::path compile_commands = build_dir / "compile_commands.json";

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            const std::string cmd = "ninja -C \"" + build_dir.string() +
                              "\" -t compdb > compile_commands.json";

            if (auto [exit_code, output] = execute_command(cmd, build_dir); exit_code == 0 && fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound, "Could not generate compile_commands.json")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_ninja_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<NinjaAdapter>()
        );
    }

}  // namespace bha::build_systems
