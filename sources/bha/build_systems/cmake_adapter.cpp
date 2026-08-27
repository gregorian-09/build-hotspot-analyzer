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

    // CMake Adapter
    // --------------------------------------------------------------------------

    class CMakeAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "CMake"; }

        [[nodiscard]] std::string description() const override {
            return "CMake build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / "CMakeLists.txt")) {
                return 0.9;
            }
            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            if (has_git_submodules(project_path)) {
                init_git_submodules(project_path);
            }

            const fs::path build_dir = options.build_dir.empty() ? project_path / "build" : options.build_dir;
            fs::create_directories(build_dir);
            ensure_cmake_analysis_queries(build_dir);

            const auto [type, c_compiler, cxx_compiler] = get_compiler_info(options);
            auto flags = CompilerFlags::for_compiler(type, options.enable_tracing, options.enable_memory_profiling);

            std::ostringstream cmd;
            cmd << "cmake";
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
                              << "GCC tracing requires this script.\n"
                              << "Set BHA_SCRIPT_DIR to the directory containing bha-capture.sh\n";
                }
            }

            if (!flags.empty()) {
                const std::string combined = flags.combined();
                cmd << " \"-DCMAKE_CXX_FLAGS=" << combined << "\"";
                cmd << " \"-DCMAKE_C_FLAGS=" << combined << "\"";
            }

            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::InternalError, "CMake configure failed: " + output)
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

            const fs::path build_dir = options.build_dir.empty() ? project_path / "build" : options.build_dir;
            const fs::path trace_output_dir = options.trace_output_dir.empty() && options.enable_tracing
                ? build_dir / "traces" : options.trace_output_dir;

            // For clean builds, build directory should be removed to ensure fresh configuration
            if (options.clean_first && fs::exists(build_dir)) {
                std::error_code ec;
                fs::remove_all(build_dir, ec);
            }

            const bool analysis_queries_created = ensure_cmake_analysis_queries(build_dir);
            const auto previous_instrumentation_index =
                find_cmake_instrumentation_index(build_dir);

            if (!fs::exists(build_dir / "CMakeCache.txt") || analysis_queries_created) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            auto compiler = get_compiler_info(options);

            std::ostringstream cmd;
            if (options.enable_tracing && !trace_output_dir.empty() && needs_capture_script(compiler.type)) {
                fs::create_directories(trace_output_dir);
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << fs::absolute(trace_output_dir).string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << fs::absolute(trace_output_dir).string() << "\" ";
#endif
            }

            cmd << "cmake --build \"" << build_dir.string() << "\"";
            cmd << " -j " << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.verbose) {
                cmd << " --verbose";
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

            if (const auto instrumentation_index = find_cmake_instrumentation_index(build_dir);
                instrumentation_index.has_value() &&
                (!previous_instrumentation_index.has_value() ||
                 *instrumentation_index != *previous_instrumentation_index)) {
                result.cmake_instrumentation_index = instrumentation_index;
            }
            result.cmake_file_api_index = find_cmake_file_api_index(build_dir);

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
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

            const std::string cmd = "cmake --build \"" + build_dir.string() + "\" --target clean";

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

            if (!fs::exists(compile_commands)) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    return Result<fs::path, Error>::failure(config_result.error());
                }
            }

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound, "compile_commands.json not found")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_cmake_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<CMakeAdapter>()
        );
    }

}  // namespace bha::build_systems
