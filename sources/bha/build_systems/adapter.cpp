//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/build_systems/adapter.hpp"
#include "bha/build_systems/adapter_support.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <tuple>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace bha::build_systems
{
    using namespace detail;

    // --------------------------------------------------------------------------
    // BuildSystemRegistry
    // --------------------------------------------------------------------------

    BuildSystemRegistry& BuildSystemRegistry::instance() {
        static BuildSystemRegistry registry;
        return registry;
    }

    void BuildSystemRegistry::register_adapter(
        std::unique_ptr<IBuildSystemAdapter> adapter
    ) {
        if (adapter) {
            const std::string adapter_name = adapter->name();
            if (get(adapter_name) != nullptr) {
                return;
            }
            adapters_.push_back(std::move(adapter));
        }
    }

    IBuildSystemAdapter* BuildSystemRegistry::detect(const fs::path& project_path) const {
        IBuildSystemAdapter* best = nullptr;
        double best_confidence = 0.0;
        IBuildSystemAdapter* best_available = nullptr;
        double best_available_confidence = 0.0;

        for (const auto& adapter : adapters_) {
            const double confidence = adapter->detect(project_path);
            if (confidence > best_confidence) {
                best_confidence = confidence;
                best = adapter.get();
            }
            if (confidence > best_available_confidence && adapter_tool_available(*adapter)) {
                best_available_confidence = confidence;
                best_available = adapter.get();
            }
        }

        if (best_available_confidence > 0.0) {
            return best_available;
        }
        return best_confidence > 0.0 ? best : nullptr;
    }

    IBuildSystemAdapter* BuildSystemRegistry::get(const std::string& name) const {
        std::string name_lower = name;
        std::ranges::transform(name_lower, name_lower.begin(),
                               [](const unsigned char c) { return std::tolower(c); });

        for (const auto& adapter : adapters_) {
            std::string adapter_name = adapter->name();
            std::ranges::transform(adapter_name, adapter_name.begin(),
                                   [](const unsigned char c) { return std::tolower(c); });

            if (adapter_name == name_lower) {
                return adapter.get();
            }
        }
        return nullptr;
    }

    // --------------------------------------------------------------------------
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
                              << "GCC/Intel/NVCC tracing requires this script.\n"
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

            if (!fs::exists(build_dir / "CMakeCache.txt")) {
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
                                  << "GCC/Intel/NVCC tracing requires this script.\n"
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
    // Make Adapter
    // --------------------------------------------------------------------------

    namespace {
        struct AutotoolsInfo {
            bool has_makefile = false;
            bool has_configure = false;
            bool has_configure_ac = false;
            bool has_makefile_am = false;
            bool has_makefile_in = false;
            bool is_autotools = false;
            std::string bootstrap_script;

            static AutotoolsInfo detect(const fs::path& project_path) {
                AutotoolsInfo info;

                info.has_makefile = fs::exists(project_path / "Makefile") ||
                                    fs::exists(project_path / "makefile") ||
                                    fs::exists(project_path / "GNUmakefile");
                info.has_configure = fs::exists(project_path / "configure");
                info.has_configure_ac = fs::exists(project_path / "configure.ac") ||
                                        fs::exists(project_path / "configure.in");
                info.has_makefile_am = fs::exists(project_path / "Makefile.am");
                info.has_makefile_in = fs::exists(project_path / "Makefile.in");

                info.is_autotools = info.has_configure_ac || info.has_makefile_am ||
                                    (info.has_configure && info.has_makefile_in);

                static const std::vector<std::string> bootstrap_scripts = {
                    "autogen.sh", "bootstrap.sh", "bootstrap", "buildconf",
                    "autogen", "buildconf.sh", "genconfig.sh", "prebuild.sh"
                };

                for (const auto& script : bootstrap_scripts) {
                    if (fs::exists(project_path / script)) {
                        info.bootstrap_script = script;
                        break;
                    }
                }

                return info;
            }
        };

        enum class BuildErrorType {
            Unknown,
            MissingDependency,
            MissingTool,
            ConfigurationFailure,
            CompilationError,
            LinkError
        };

        struct BuildErrorInfo {
            BuildErrorType type = BuildErrorType::Unknown;
            std::string summary;
            std::vector<std::string> missing_packages;
        };

        BuildErrorInfo classify_build_error(const std::string& output) {
            BuildErrorInfo info;
            std::istringstream stream(output);
            std::string line;
            std::vector<std::string> error_lines;

            while (std::getline(stream, line)) {
                std::string lower = line;
                std::ranges::transform(lower, lower.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                if (lower.find("pkg-config") != std::string::npos &&
                    (lower.find("not found") != std::string::npos ||
                     lower.find("missing") != std::string::npos)) {
                    info.type = BuildErrorType::MissingTool;
                    info.missing_packages.emplace_back("pkg-config");
                    error_lines.push_back(line);
                }
                else if (lower.find("could not find") != std::string::npos ||
                         lower.find("package '") != std::string::npos ||
                         lower.find("no package '") != std::string::npos) {
                    info.type = BuildErrorType::MissingDependency;
                    if (auto pos = lower.find("package '"); pos != std::string::npos) {
                        auto start = pos + 9;
                        if (auto end = lower.find('\'', start); end != std::string::npos) {
                            info.missing_packages.push_back(line.substr(start, end - start));
                        }
                    }
                    error_lines.push_back(line);
                }
                else if (lower.find("configure: error") != std::string::npos ||
                         lower.find("configuration failed") != std::string::npos) {
                    if (info.type == BuildErrorType::Unknown) {
                        info.type = BuildErrorType::ConfigurationFailure;
                    }
                    error_lines.push_back(line);
                }
                else if (lower.find("undefined reference") != std::string::npos ||
                         lower.find("cannot find -l") != std::string::npos) {
                    info.type = BuildErrorType::LinkError;
                    error_lines.push_back(line);
                }
                else if (lower.find("error:") != std::string::npos ||
                         lower.find("fatal error") != std::string::npos) {
                    if (info.type == BuildErrorType::Unknown) {
                        info.type = BuildErrorType::CompilationError;
                    }
                    error_lines.push_back(line);
                }
                else if (lower.find("autoreconf") != std::string::npos ||
                         lower.find("aclocal") != std::string::npos ||
                         lower.find("automake") != std::string::npos ||
                         lower.find("autoconf") != std::string::npos) {
                    if (lower.find("not found") != std::string::npos ||
                        lower.find("missing") != std::string::npos ||
                        lower.find("command not found") != std::string::npos) {
                        info.type = BuildErrorType::MissingTool;
                        error_lines.push_back(line);
                    }
                }
            }

            std::ostringstream summary;
            const size_t count = std::min(error_lines.size(), static_cast<size_t>(20));
            for (size_t i = 0; i < count; ++i) {
                summary << error_lines[i] << "\n";
            }
            if (error_lines.size() > 20) {
                summary << "... (" << (error_lines.size() - 20) << " more errors)\n";
            }

            if (!info.missing_packages.empty()) {
                summary << "\nMissing packages: ";
                for (size_t i = 0; i < info.missing_packages.size(); ++i) {
                    if (i > 0) {
                        summary << ", ";
                    }
                    summary << info.missing_packages[i];
                }
                summary << "\n";
            }

            info.summary = summary.str();
            return info;
        }
    }

    class MakeAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Make"; }

        [[nodiscard]] std::string description() const override {
            return "GNU Make / Autotools build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            auto [has_makefile, has_configure, has_configure_ac, has_makefile_am, has_makefile_in, is_autotools, bootstrap_script] = AutotoolsInfo::detect(project_path);

            if (has_makefile && !is_autotools) {
                return 0.7;
            }
            if (fs::exists(project_path / "GNUmakefile")) {
                return 0.75;
            }
            if (has_configure && has_makefile_in) {
                return 0.8;
            }
            if (has_configure_ac || has_makefile_am) {
                return 0.85;
            }
            if (!bootstrap_script.empty()) {
                return 0.8;
            }

            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            auto info = AutotoolsInfo::detect(project_path);
            fs::path work_dir = project_path;
            fs::path configure_script = project_path / "configure";
            const fs::path project_abs = fs::absolute(project_path);

            if (!fs::exists(configure_script) && info.is_autotools) {
                if (!info.bootstrap_script.empty()) {
                    std::string cmd;
                    if (info.bootstrap_script == "buildconf") {
                        cmd = "sh buildconf";
                    } else {
                        cmd = "./" + info.bootstrap_script;
                    }
                    if (auto [exit_code, output] = execute_command(cmd, project_path); exit_code != 0) {
                        if (auto error_info = classify_build_error(output); error_info.type == BuildErrorType::MissingTool) {
                            return Result<void, Error>::failure(
                                Error(ErrorCode::NotFound,
                                    "Bootstrap failed - missing tools: " + error_info.summary)
                            );
                        }
                        return Result<void, Error>::failure(
                            Error(ErrorCode::InternalError,
                                info.bootstrap_script + " failed: " + output)
                        );
                    }
                }

                if (!fs::exists(configure_script) && info.has_configure_ac) {
                    if (auto [exit_code, output] = execute_command("autoreconf -fi", project_path); exit_code != 0) {
                        if (auto error_info = classify_build_error(output); error_info.type == BuildErrorType::MissingTool) {
                            return Result<void, Error>::failure(
                                Error(ErrorCode::NotFound,
                                    "autoreconf failed - missing autotools: " + error_info.summary)
                            );
                        }
                        return Result<void, Error>::failure(
                            Error(ErrorCode::InternalError, "autoreconf failed: " + output)
                        );
                    }
                }
            }

            if (!fs::exists(configure_script)) {
                if (info.has_makefile) {
                    return Result<void, Error>::success();
                }
                return Result<void, Error>::failure(
                    Error(ErrorCode::NotFound,
                        "No configure script found and could not generate one")
                );
            }

            fs::path build_dir = options.build_dir.empty() ? project_abs : options.build_dir;
            if (build_dir.is_relative()) {
                build_dir = fs::absolute(project_abs / build_dir);
            }
            const bool use_vpath = !options.build_dir.empty() && options.build_dir != project_path;

            if (use_vpath) {
                std::error_code ec;
                fs::create_directories(build_dir, ec);
                if (ec) {
                    return Result<void, Error>::failure(
                        Error(ErrorCode::InternalError,
                            "Failed to create build directory: " + ec.message())
                    );
                }
                work_dir = build_dir;
                configure_script = fs::relative(project_path / "configure", build_dir);
                if (configure_script.empty() || configure_script.string().find("..") == std::string::npos) {
                    configure_script = fs::absolute(project_path / "configure");
                }
            }

            auto [type, c_compiler, cxx_compiler] = get_compiler_info(options);
            auto flags = CompilerFlags::for_compiler(type, options.enable_tracing, options.enable_memory_profiling);

            std::ostringstream cmd;
            cmd << "CC=\"" << c_compiler << "\" ";
            cmd << "CXX=\"" << cxx_compiler << "\" ";

            std::string base_flags = "-fPIC";
            if (!flags.empty()) {
                base_flags += " " + flags.combined();
            }
            cmd << "CFLAGS=\"" << base_flags << "\" ";
            cmd << "CXXFLAGS=\"" << base_flags << "\" ";

            if (use_vpath) {
                cmd << "\"" << configure_script.string() << "\"";
            } else {
                cmd << "./configure";
            }

            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            if (auto [exit_code, output] = execute_command(cmd.str(), work_dir); exit_code != 0) {
                auto error_info = classify_build_error(output);
                std::string error_msg;
                switch (error_info.type) {
                    case BuildErrorType::MissingDependency:
                        error_msg = "Configure failed - missing dependencies:\n" + error_info.summary;
                        return Result<void, Error>::failure(
                            Error(ErrorCode::NotFound, error_msg)
                        );
                    case BuildErrorType::MissingTool:
                        error_msg = "Configure failed - missing tools:\n" + error_info.summary;
                        return Result<void, Error>::failure(
                            Error(ErrorCode::NotFound, error_msg)
                        );
                    default:
                        error_msg = "Configure failed:\n" +
                            (error_info.summary.empty() ? output : error_info.summary);
                        return Result<void, Error>::failure(
                            Error(ErrorCode::InternalError, error_msg)
                        );
                }
            }

            const bool makefile_created = fs::exists(work_dir / "Makefile") ||
                                    fs::exists(work_dir / "makefile") ||
                                    fs::exists(work_dir / "GNUmakefile");
            if (!makefile_created) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::InternalError,
                        "Configure completed but did not generate a Makefile")
                );
            }

            return Result<void, Error>::success();
        }

        Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            BuildResult result;
            const auto start = std::chrono::steady_clock::now();

            auto info = AutotoolsInfo::detect(project_path);

            const bool needs_configure = info.is_autotools ||
                                   info.has_configure ||
                                   !info.bootstrap_script.empty();

            const fs::path project_abs = fs::absolute(project_path);
            fs::path work_dir = project_abs;
            fs::path build_dir = options.build_dir.empty() ? project_abs : options.build_dir;
            if (build_dir.is_relative()) {
                build_dir = fs::absolute(project_abs / build_dir);
            }

            if (!options.build_dir.empty() && options.build_dir != project_path) {
                work_dir = build_dir;
            }

            bool has_makefile_in_work_dir = fs::exists(work_dir / "Makefile") ||
                                            fs::exists(work_dir / "makefile") ||
                                            fs::exists(work_dir / "GNUmakefile");

            if (options.clean_first) {
                clean(project_path, options);
                has_makefile_in_work_dir = false;
            }

            if (needs_configure && !has_makefile_in_work_dir) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    auto& err = config_result.error();
                    result.error_message = err.message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            has_makefile_in_work_dir = fs::exists(work_dir / "Makefile") ||
                                       fs::exists(work_dir / "makefile") ||
                                       fs::exists(work_dir / "GNUmakefile");

            if (!has_makefile_in_work_dir && !info.has_makefile) {
                result.error_message = "No Makefile found. ";
                if (info.is_autotools) {
                    result.error_message += "This is an autotools project - configure may have failed.";
                } else if (!info.bootstrap_script.empty()) {
                    result.error_message += "Try running '" + info.bootstrap_script + "' first.";
                } else {
                    result.error_message += "This project may require manual configuration.";
                }
                return Result<BuildResult, Error>::success(result);
            }

            if (!has_makefile_in_work_dir && info.has_makefile) {
                work_dir = project_path;
            }

            const auto [type, c_compiler, cxx_compiler] = get_compiler_info(options);
            auto flags = CompilerFlags::for_compiler(type, options.enable_tracing, options.enable_memory_profiling);

            const fs::path trace_output_dir = options.trace_output_dir.empty() && options.enable_tracing
                ? work_dir / "traces" : options.trace_output_dir;

            std::ostringstream cmd;

            if (options.enable_tracing && !trace_output_dir.empty() && needs_capture_script(type)) {
                std::error_code ec;
                fs::create_directories(trace_output_dir, ec);
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << fs::absolute(trace_output_dir).string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << fs::absolute(trace_output_dir).string() << "\" ";
#endif
            }

            cmd << "make";
            cmd << " -j" << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            std::string base_cflags = "-fPIC";
            std::string base_cxxflags = "-fPIC";
            if (!flags.empty()) {
                base_cflags += " " + flags.combined();
                base_cxxflags += " " + flags.combined();
            }

            if (options.enable_tracing && needs_capture_script(type)) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    cmd << " CC=\"" << script.string() << " " << c_compiler << "\"";
                    cmd << " CXX=\"" << script.string() << " " << cxx_compiler << "\"";
                } else {
                    cmd << " CC=\"" << c_compiler << "\"";
                    cmd << " CXX=\"" << cxx_compiler << "\"";
                }
            } else {
                cmd << " CC=\"" << c_compiler << "\"";
                cmd << " CXX=\"" << cxx_compiler << "\"";
            }

            cmd << " CFLAGS=\"" << base_cflags << "\"";
            cmd << " CXXFLAGS=\"" << base_cxxflags << "\"";

            if (options.verbose) {
                cmd << " V=1";
            }

            auto [exit_code, output] = execute_command(
                cmd.str(),
                work_dir,
                options.on_output_line,
                options.should_cancel
            );

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                if (auto error_info = classify_build_error(output); !error_info.summary.empty()) {
                    result.error_message = error_info.summary;
                } else {
                    result.error_message = extract_error_summary(output);
                    if (result.error_message.empty()) {
                        result.error_message = "Build failed";
                    }
                }
            }

            result.trace_files = find_trace_files(work_dir);
            if (work_dir != project_path) {
                auto src_traces = find_trace_files(project_path);
                result.trace_files.insert(result.trace_files.end(),
                    src_traces.begin(), src_traces.end());
            }
            if (needs_capture_script(type) && !trace_output_dir.empty() && trace_output_dir != work_dir) {
                auto trace_dir_files = find_trace_files(trace_output_dir);
                result.trace_files.insert(result.trace_files.end(),
                    trace_dir_files.begin(), trace_dir_files.end());
            }

            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(work_dir);
                if (work_dir != project_path) {
                    auto src_mem = find_memory_files(project_path);
                    result.memory_files.insert(result.memory_files.end(),
                        src_mem.begin(), src_mem.end());
                }
            }

            copy_trace_files(work_dir, trace_output_dir, result.trace_files, result.memory_files);

            result.build_time = std::chrono::duration_cast<Duration>(
                std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            const fs::path project_abs = fs::absolute(project_path);
            fs::path work_dir = options.build_dir.empty() ? project_abs : options.build_dir;
            if (work_dir.is_relative()) {
                work_dir = fs::absolute(project_abs / work_dir);
            }

            bool has_makefile = fs::exists(work_dir / "Makefile") ||
                                fs::exists(work_dir / "makefile") ||
                                fs::exists(work_dir / "GNUmakefile");

            if (!has_makefile) {
                work_dir = project_abs;
                has_makefile = fs::exists(work_dir / "Makefile") ||
                               fs::exists(work_dir / "makefile") ||
                               fs::exists(work_dir / "GNUmakefile");
            }

            if (!has_makefile) {
                return Result<void, Error>::success();
            }

            const bool has_project_makefile = fs::exists(project_abs / "Makefile") ||
                                              fs::exists(project_abs / "makefile") ||
                                              fs::exists(project_abs / "GNUmakefile");
            const bool has_config_in_project = fs::exists(project_abs / "config.status");
            const bool has_config_in_work = fs::exists(work_dir / "config.status");

            bool cleaned = false;
            if (has_config_in_project && has_project_makefile) {
                auto [exit_code, output] = execute_command("make distclean", project_abs);
                (void)output;
                cleaned = (exit_code == 0);
            } else if (has_config_in_work) {
                auto [exit_code, output] = execute_command("make distclean", work_dir);
                (void)output;
                cleaned = (exit_code == 0);
            }

            if (!cleaned) {
                execute_command("make clean", work_dir);
            }

            // Remove cached compiler settings files that may persist between builds
            // These files cache CFLAGS and compiler choices, causing issues when switching compilers
            std::error_code ec;
            for (const auto& settings_file : {".make-settings", ".make-prerequisites"}) {
                fs::remove(work_dir / settings_file, ec);
                if (work_dir != project_path) {
                    fs::remove(project_path / settings_file, ec);
                }
            }

            // Also check common subdirectories for these files
            for (const auto& subdir : {"src", "deps"}) {
                if (const fs::path subdir_path = project_abs / subdir; fs::exists(subdir_path)) {
                    for (const auto& settings_file : {".make-settings", ".make-prerequisites", ".make-*"}) {
                        fs::remove(subdir_path / settings_file, ec);
                    }
                }
            }

            return Result<void, Error>::success();
        }

        Result<fs::path, Error> get_compile_commands(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            const fs::path project_abs = fs::absolute(project_path);
            fs::path work_dir = options.build_dir.empty() ? project_abs : options.build_dir;
            if (work_dir.is_relative()) {
                work_dir = fs::absolute(project_abs / work_dir);
            }
            fs::path compile_commands = work_dir / "compile_commands.json";

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            compile_commands = project_abs / "compile_commands.json";
            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            const std::string cmd = "bear -- make -j" + std::to_string(get_cpu_count());
            if (auto [exit_code, output] = execute_command(cmd, work_dir);
                exit_code == 0 && fs::exists(work_dir / "compile_commands.json")) {
                return Result<fs::path, Error>::success(work_dir / "compile_commands.json");
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound,
                    "compile_commands.json not found. Install 'bear' to generate it.")
            );
        }
    };

    // --------------------------------------------------------------------------
    // MSBuild Adapter
    // --------------------------------------------------------------------------

    class MSBuildAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "MSBuild"; }

        [[nodiscard]] std::string description() const override {
            return "Microsoft MSBuild/Visual Studio adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".sln") {
                    return 0.9;
                }
            }

            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".vcxproj") {
                    return 0.85;
                }
            }

            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)project_path;
            (void)options;
            // MSBuild doesn't need separate configuration
            return Result<void, Error>::success();
        }

        Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            BuildResult result;
            const auto start = std::chrono::steady_clock::now();

            fs::path sln_file;
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".sln") {
                    sln_file = entry.path();
                    break;
                }
            }

            if (sln_file.empty()) {
                result.error_message = "No .sln file found";
                return Result<BuildResult, Error>::success(result);
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            std::ostringstream cmd;
            cmd << "msbuild \"" << sln_file.string() << "\"";
            cmd << " /p:Configuration=" << options.build_type;
            cmd << " /m:" << (options.parallel_jobs > 0 ?
                              options.parallel_jobs : get_cpu_count());

            if (options.enable_tracing) {
                cmd << " /p:EnableBuildInsights=true";
            }

            if (options.enable_memory_profiling) {
                cmd << " /p:GenerateMapFile=true";
            }

            if (options.verbose) {
                cmd << " /v:detailed";
            } else {
                cmd << " /v:minimal";
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
                result.error_message = error_summary.empty() ? "Build failed with no specific error output" : error_summary;
            }

            result.trace_files = find_trace_files(project_path);
            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(project_path);
            }

            copy_trace_files(project_path, options.trace_output_dir, result.trace_files, result.memory_files);

            const auto end = std::chrono::steady_clock::now();
            result.build_time = std::chrono::duration_cast<Duration>(end - start);

            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            fs::path sln_file;
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".sln") {
                    sln_file = entry.path();
                    break;
                }
            }

            if (sln_file.empty()) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::NotFound, "No .sln file found")
                );
            }

            const std::string cmd = "msbuild \"" + sln_file.string() +
                              "\" /t:Clean /p:Configuration=" + options.build_type;

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
                      "compile_commands.json not available for MSBuild. "
                      "Consider using clang-cl or cmake-based build.")
            );
        }
    };

    // --------------------------------------------------------------------------
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
                              << "GCC/Intel/NVCC tracing requires this script.\n"
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
    // Unreal Adapter
    // --------------------------------------------------------------------------

    class UnrealAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Unreal"; }

        [[nodiscard]] std::string description() const override {
            return "Unreal Build Tool adapter (.uproject / ModuleRules / TargetRules)";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (find_unreal_uproject(project_path).has_value()) {
                return 0.98;
            }
            if (has_unreal_build_markers(project_path)) {
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
            const auto start = std::chrono::steady_clock::now();

            const auto uproject = find_unreal_uproject(project_path);
            if (!uproject.has_value()) {
                result.error_message = "No .uproject file found at project root";
                return Result<BuildResult, Error>::success(result);
            }

            const std::string target_name = select_unreal_target_name(project_path, *uproject);
            if (target_name.empty()) {
                result.error_message = "Could not determine Unreal target name from .Target.cs files";
                return Result<BuildResult, Error>::success(result);
            }

            const std::string platform = unreal_platform_name();
            const std::string configuration = unreal_configuration_from_build_type(options.build_type);

            if (options.clean_first) {
                if (auto clean_result = clean(project_path, options); clean_result.is_err()) {
                    result.error_message = clean_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            std::ostringstream cmd;
            if (const auto build_script = resolve_unreal_build_script(); build_script.has_value()) {
#ifdef _WIN32
                cmd << "\"" << build_script->string() << "\"";
                cmd << " \"" << target_name << "\"";
                cmd << " " << platform;
                cmd << " " << configuration;
                cmd << " -Project=\"" << uproject->string() << "\"";
#else
                cmd << shell_escape_posix(build_script->string());
                cmd << " " << shell_escape_posix(target_name);
                cmd << " " << shell_escape_posix(platform);
                cmd << " " << shell_escape_posix(configuration);
                cmd << " -Project=" << shell_escape_posix(uproject->string());
#endif
            } else {
                cmd << "UnrealBuildTool";
                cmd << " " << target_name;
                cmd << " " << platform;
                cmd << " " << configuration;
                cmd << " -Project=\"" << uproject->string() << "\"";
            }

            cmd << " -NoHotReload";
            cmd << " -Progress";
            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            const auto [exit_code, output] = execute_command(
                cmd.str(),
                project_path,
                options.on_output_line,
                options.should_cancel
            );
            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                const std::string error_summary = extract_error_summary(output);
                if (error_summary.empty()) {
                    result.error_message = "Unreal build failed";
                } else {
                    result.error_message = error_summary;
                }
            }

            result.trace_files = find_trace_files(project_path / "Saved");
            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            const auto uproject = find_unreal_uproject(project_path);
            if (!uproject.has_value()) {
                return Result<void, Error>::success();
            }

            const std::string target_name = select_unreal_target_name(project_path, *uproject);
            if (target_name.empty()) {
                return Result<void, Error>::success();
            }

            const std::string platform = unreal_platform_name();
            const std::string configuration = unreal_configuration_from_build_type(options.build_type);

            if (const auto build_script = resolve_unreal_build_script(); build_script.has_value()) {
                std::ostringstream cmd;
#ifdef _WIN32
                cmd << "\"" << build_script->string() << "\"";
                cmd << " \"" << target_name << "\"";
                cmd << " " << platform;
                cmd << " " << configuration;
                cmd << " -Project=\"" << uproject->string() << "\"";
#else
                cmd << shell_escape_posix(build_script->string());
                cmd << " " << shell_escape_posix(target_name);
                cmd << " " << shell_escape_posix(platform);
                cmd << " " << shell_escape_posix(configuration);
                cmd << " -Project=" << shell_escape_posix(uproject->string());
#endif
                cmd << " -clean";

                if (const auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                    return Result<void, Error>::failure(
                        Error(ErrorCode::InternalError, "Unreal clean failed: " + output)
                    );
                }
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
            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound, "compile_commands.json not found for Unreal project")
            );
        }
    };

    // --------------------------------------------------------------------------
    // XCode Adapter
    // --------------------------------------------------------------------------

    class XCodeAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "XCode"; }

        [[nodiscard]] std::string description() const override {
            return "Apple Xcode build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".xcodeproj") {
                    return 0.95;
                }
                if (entry.path().extension() == ".xcworkspace") {
                    return 0.95;
                }
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

            fs::path project_file;
            fs::path workspace_file;
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".xcworkspace") {
                    workspace_file = entry.path();
                    break;
                }
                if (entry.path().extension() == ".xcodeproj") {
                    project_file = entry.path();
                }
            }

            if (workspace_file.empty() && project_file.empty()) {
                result.error_message = "No Xcode project or workspace found";
                return Result<BuildResult, Error>::success(result);
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            std::ostringstream cmd;
            cmd << "xcodebuild";

            if (!workspace_file.empty()) {
                cmd << " -workspace \"" << workspace_file.string() << "\"";
                cmd << " -scheme \"" << workspace_file.stem().string() << "\"";
            } else {
                cmd << " -project \"" << project_file.string() << "\"";
            }

            cmd << " -configuration " << options.build_type;
            cmd << " -jobs " << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.enable_tracing) {
                cmd << " -enableBuildTimingTracing YES";
            }

            if (!options.verbose) {
                cmd << " -quiet";
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

            result.trace_files = find_trace_files(project_path / "build");

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            fs::path project_file;
            fs::path workspace_file;
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".xcworkspace") {
                    workspace_file = entry.path();
                    break;
                }
                if (entry.path().extension() == ".xcodeproj") {
                    project_file = entry.path();
                }
            }

            std::ostringstream cmd;
            cmd << "xcodebuild clean";

            if (!workspace_file.empty()) {
                cmd << " -workspace \"" << workspace_file.string() << "\"";
                cmd << " -scheme \"" << workspace_file.stem().string() << "\"";
            } else if (!project_file.empty()) {
                cmd << " -project \"" << project_file.string() << "\"";
            }

            cmd << " -configuration " << options.build_type;

            if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
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
                      "compile_commands.json not found. Use xcpretty or XcodeGen to generate it.")
            );
        }
    };

    // --------------------------------------------------------------------------
    // Registration functions
    // --------------------------------------------------------------------------

    void register_cmake_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<CMakeAdapter>()
        );
    }

    void register_ninja_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<NinjaAdapter>()
        );
    }

    void register_make_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<MakeAdapter>()
        );
    }

    void register_msbuild_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<MSBuildAdapter>()
        );
    }

    void register_meson_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<MesonAdapter>()
        );
    }

    void register_bazel_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<BazelAdapter>()
        );
    }

    void register_buck2_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<Buck2Adapter>()
        );
    }

    void register_scons_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<SConsAdapter>()
        );
    }

    void register_xcode_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<XCodeAdapter>()
        );
    }

    void register_unreal_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<UnrealAdapter>()
        );
    }

    void register_core_adapters() {
        register_cmake_adapter();
        register_msbuild_adapter();
    }

    void register_experimental_adapters() {
        register_ninja_adapter();
        register_make_adapter();
        register_meson_adapter();
        register_bazel_adapter();
        register_buck2_adapter();
        register_scons_adapter();
        register_xcode_adapter();
        register_unreal_adapter();
    }

    void register_all_adapters(const AdapterRegistrationMode mode) {
        register_core_adapters();
        if (mode == AdapterRegistrationMode::IncludeExperimental) {
            register_experimental_adapters();
        }
    }

}  // namespace bha::build_systems
