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
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

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

    void register_make_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<MakeAdapter>()
        );
    }

}  // namespace bha::build_systems
