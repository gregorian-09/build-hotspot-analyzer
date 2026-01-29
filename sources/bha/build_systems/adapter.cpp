//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/build_systems/adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace bha::build_systems
{
    namespace {

        enum class CompilerFamily { GCC, Clang, MSVC, Unknown };

        struct CompilerInfo {
            CompilerFamily family = CompilerFamily::Unknown;
            std::string c_compiler;
            std::string cxx_compiler;

            static CompilerInfo from_name(const std::string& compiler) {
                CompilerInfo info;
                if (compiler.empty()) {
                    info.family = CompilerFamily::GCC;
                    info.c_compiler = "gcc";
                    info.cxx_compiler = "g++";
                } else if (compiler.find("clang") != std::string::npos) {
                    info.family = CompilerFamily::Clang;
                    info.c_compiler = compiler;
                    info.cxx_compiler = (compiler == "clang") ? "clang++" : compiler;
                } else if (compiler.find("gcc") != std::string::npos ||
                           compiler.find("g++") != std::string::npos) {
                    info.family = CompilerFamily::GCC;
                    info.c_compiler = (compiler == "g++") ? "gcc" : compiler;
                    info.cxx_compiler = (compiler == "gcc") ? "g++" : compiler;
                } else if (compiler.find("cl") != std::string::npos ||
                           compiler.find("msvc") != std::string::npos) {
                    info.family = CompilerFamily::MSVC;
                    info.c_compiler = "cl";
                    info.cxx_compiler = "cl";
                } else {
                    info.family = CompilerFamily::GCC;
                    info.c_compiler = compiler;
                    info.cxx_compiler = compiler;
                }
                return info;
            }
        };

        struct CompilerFlags {
            std::string tracing_flags;
            std::string memory_flags;

            static CompilerFlags for_compiler(const CompilerFamily family, const bool tracing, const bool memory) {
                CompilerFlags flags;

                if (tracing) {
                    switch (family) {
                        case CompilerFamily::Clang:
                            flags.tracing_flags = "-ftime-trace";
                            break;
                        case CompilerFamily::GCC:
                            flags.tracing_flags = "-ftime-report";
                            break;
                        default:
                            break;
                    }
                }

                if (memory) {
                    switch (family) {
                    case CompilerFamily::GCC:
                    case CompilerFamily::Clang:
                        flags.memory_flags = "-fstack-usage";
                        break;
                    default:
                        break;
                    }

                }

                return flags;
            }

            [[nodiscard]] std::string combined() const {
                if (tracing_flags.empty()) return memory_flags;
                if (memory_flags.empty()) return tracing_flags;
                return tracing_flags + " " + memory_flags;
            }

            [[nodiscard]] bool empty() const {
                return tracing_flags.empty() && memory_flags.empty();
            }
        };

        fs::path find_capture_script(const fs::path& project_path) {
#ifdef _WIN32
            auto script_name = "bha-capture.bat";
#else
            const char* script_name = "bha-capture.sh";
#endif
            const std::vector search_paths = {
                project_path / "cmake" / script_name,
                fs::current_path() / "cmake" / script_name,
                fs::current_path().parent_path() / "cmake" / script_name
            };

            for (const auto& path : search_paths) {
                if (fs::exists(path)) {
                    return fs::absolute(path);
                }
            }
            return {};
        }

        std::string extract_error_summary(const std::string& output, size_t max_lines = 50) {
            std::istringstream stream(output);
            std::string line;
            std::vector<std::string> all_lines;
            std::vector<std::string> error_lines;

            while (std::getline(stream, line)) {
                all_lines.push_back(line);
                std::string lower_line = line;
                std::ranges::transform(lower_line, lower_line.begin(),
                                      [](const unsigned char c) { return std::tolower(c); });

                if (lower_line.find("error") != std::string::npos ||
                    lower_line.find("fatal") != std::string::npos ||
                    lower_line.find("undefined reference") != std::string::npos ||
                    lower_line.find("cannot find") != std::string::npos) {
                    error_lines.push_back(line);
                }
            }

            std::ostringstream summary;

            if (!error_lines.empty()) {
                size_t count = std::min(error_lines.size(), max_lines);
                for (size_t i = 0; i < count; ++i) {
                    summary << error_lines[i] << "\n";
                }
                if (error_lines.size() > max_lines) {
                    summary << "... (" << (error_lines.size() - max_lines) << " more errors)\n";
                }
            } else if (!all_lines.empty()) {
                size_t start = all_lines.size() > max_lines ? all_lines.size() - max_lines : 0;
                for (size_t i = start; i < all_lines.size(); ++i) {
                    summary << all_lines[i] << "\n";
                }
            }

            return summary.str();
        }

        std::pair<int, std::string> execute_command(
            const std::string& command,
            const fs::path& working_dir = fs::path()
        ) {
            std::string output;
            int exit_code = -1;

#ifdef _WIN32
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = nullptr;
            sa.bInheritHandle = TRUE;

            HANDLE read_pipe, write_pipe;
            if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
                return {-1, "Failed to create pipe"};
            }

            SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = write_pipe;
            si.hStdError = write_pipe;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

            PROCESS_INFORMATION pi = {};

            std::string cmd_line = "cmd /c " + command;
            std::string work_dir = working_dir.empty() ? "" : working_dir.string();

            BOOL process_created = CreateProcessA(
                nullptr,
                const_cast<char*>(cmd_line.c_str()),
                nullptr, nullptr, TRUE, 0, nullptr,
                work_dir.empty() ? nullptr : work_dir.c_str(),
                &si, &pi
            );

            CloseHandle(write_pipe);

            if (process_created) {
                std::array<char, 4096> buffer{};
                DWORD bytes_read;
                while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)
                       && bytes_read > 0) {
                    output.append(buffer.data(), bytes_read);
                }

                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD code;
                GetExitCodeProcess(pi.hProcess, &code);
                exit_code = static_cast<int>(code);

                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }

            CloseHandle(read_pipe);
#else
            // Unix implementation
            std::string full_command = command;
            if (!working_dir.empty()) {
                full_command = "cd '" + working_dir.string() + "' && " + command;
            }
            full_command += " 2>&1";

            if (FILE* pipe = popen(full_command.c_str(), "r")) {
                std::array<char, 4096> buffer{};
                while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                    output += buffer.data();
                }
                exit_code = pclose(pipe);
                if (WIFEXITED(exit_code)) {
                    exit_code = WEXITSTATUS(exit_code);
                }
            }
#endif

            return std::pair{exit_code, std::move(output)};
        }

        std::vector<fs::path> find_trace_files(const fs::path& dir) {
            std::vector<fs::path> traces;

            if (!fs::exists(dir)) {
                return traces;
            }

            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;

                std::string filename = entry.path().filename().string();
                std::string ext = entry.path().extension().string();

                if (ext == ".json") {
                    if (std::string stem = entry.path().stem().string(); stem.ends_with(".c") || stem.ends_with(".cc") ||
                        stem.ends_with(".cpp") || stem.ends_with(".cxx") ||
                        stem.ends_with(".C") || stem.ends_with(".c++") ||
                        stem.ends_with(".m") || stem.ends_with(".mm")) {
                        traces.push_back(entry.path());
                    }
                }

                if (ext == ".txt" && filename.ends_with(".bha.txt")) {
                    traces.push_back(entry.path());
                }

                if (ext == ".trace" || filename.ends_with("_trace.json")) {
                    traces.push_back(entry.path());
                }
            }

            return traces;
        }

        std::vector<fs::path> find_memory_files(const fs::path& dir) {
            std::vector<fs::path> memory_files;

            if (!fs::exists(dir)) {
                return memory_files;
            }

            for (std::error_code ec; const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;

                if (entry.path().extension() == ".su") {
                    memory_files.push_back(entry.path());
                }
            }

            return memory_files;
        }

        void copy_trace_files(const fs::path& source_dir, const fs::path& dest_dir,
                            std::vector<fs::path>& trace_files, std::vector<fs::path>& memory_files) {
            if (source_dir == dest_dir || dest_dir.empty()) {
                return;
            }

            std::error_code ec;
            fs::create_directories(dest_dir, ec);
            if (ec) return;

            std::vector<fs::path> new_trace_files;
            std::vector<fs::path> new_memory_files;

            const auto normalized_dest = fs::absolute(dest_dir, ec).lexically_normal();
            if (ec) return;

            for (const auto& trace_file : trace_files) {
                auto normalized_file = fs::absolute(trace_file, ec).lexically_normal();
                if (ec) {
                    new_trace_files.push_back(trace_file);
                    ec.clear();
                    continue;
                }

                if (normalized_file.parent_path() == normalized_dest) {
                    new_trace_files.push_back(trace_file);
                } else {
                    fs::path dest_file = dest_dir / trace_file.filename();
                    fs::copy_file(trace_file, dest_file, fs::copy_options::overwrite_existing, ec);
                    new_trace_files.push_back(ec ? trace_file : dest_file);
                    ec.clear();
                }
            }

            for (const auto& memory_file : memory_files) {
                auto normalized_file = fs::absolute(memory_file, ec).lexically_normal();
                if (ec) {
                    new_memory_files.push_back(memory_file);
                    ec.clear();
                    continue;
                }

                if (normalized_file.parent_path() == normalized_dest) {
                    new_memory_files.push_back(memory_file);
                } else {
                    fs::path dest_file = dest_dir / memory_file.filename();
                    fs::copy_file(memory_file, dest_file, fs::copy_options::overwrite_existing, ec);
                    new_memory_files.push_back(ec ? memory_file : dest_file);
                    ec.clear();
                }
            }

            trace_files = std::move(new_trace_files);
            memory_files = std::move(new_memory_files);
        }

        /**
         * Get number of CPU cores.
         */
        int get_cpu_count() {
#ifdef _WIN32
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            return static_cast<int>(si.dwNumberOfProcessors);
#else
            const long cores = sysconf(_SC_NPROCESSORS_ONLN);
            return cores > 0 ? static_cast<int>(cores) : 1;
#endif
        }

        bool init_git_submodules(const fs::path& project_path) {
            const std::string cmd = "git submodule update --init --recursive";
            auto [exit_code, output] = execute_command(cmd, project_path);
            return exit_code == 0;
        }

        bool has_git_submodules(const fs::path& project_path) {
            return fs::exists(project_path / ".gitmodules");
        }

    }  // namespace

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
        adapters_.push_back(std::move(adapter));
    }

    IBuildSystemAdapter* BuildSystemRegistry::detect(const fs::path& project_path) const {
        IBuildSystemAdapter* best = nullptr;
        double best_confidence = 0.0;

        for (const auto& adapter : adapters_) {
            if (const double confidence = adapter->detect(project_path); confidence > best_confidence) {
                best_confidence = confidence;
                best = adapter.get();
            }
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

            fs::path build_dir = options.build_dir.empty() ? project_path / "build" : options.build_dir;
            fs::create_directories(build_dir);

            auto compiler = CompilerInfo::from_name(options.compiler);
            auto flags = CompilerFlags::for_compiler(compiler.family, options.enable_tracing, options.enable_memory_profiling);

            std::ostringstream cmd;
            cmd << "cmake";
            cmd << " -S \"" << project_path.string() << "\"";
            cmd << " -B \"" << build_dir.string() << "\"";
            cmd << " -DCMAKE_BUILD_TYPE=" << options.build_type;
            cmd << " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

            if (!options.compiler.empty()) {
                cmd << " -DCMAKE_C_COMPILER=" << compiler.c_compiler;
                cmd << " -DCMAKE_CXX_COMPILER=" << compiler.cxx_compiler;
            }

            if (options.enable_tracing && compiler.family == CompilerFamily::GCC) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    cmd << " -DCMAKE_CXX_COMPILER_LAUNCHER=\"" << script.string() << "\"";
                    cmd << " -DCMAKE_C_COMPILER_LAUNCHER=\"" << script.string() << "\"";
                }
            }

            if (!flags.empty()) {
                cmd << " -DCMAKE_CXX_FLAGS=\"" << flags.combined() << "\"";
                cmd << " -DCMAKE_C_FLAGS=\"" << flags.combined() << "\"";
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

            fs::path build_dir = options.build_dir.empty() ? project_path / "build" : options.build_dir;
            fs::path trace_output_dir = options.trace_output_dir.empty() && options.enable_tracing
                ? build_dir / "traces" : options.trace_output_dir;

            if (!fs::exists(build_dir / "CMakeCache.txt")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            auto compiler = CompilerInfo::from_name(options.compiler);

            std::ostringstream cmd;
            if (options.enable_tracing && !trace_output_dir.empty() && compiler.family == CompilerFamily::GCC) {
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << trace_output_dir.string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << trace_output_dir.string() << "\" ";
#endif
            }

            cmd << "cmake --build \"" << build_dir.string() << "\"";
            cmd << " -j " << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.verbose) {
                cmd << " --verbose";
            }

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                std::string error_summary = extract_error_summary(output);
                result.error_message = error_summary.empty() ? "Build failed" : error_summary;
            }

            result.trace_files = find_trace_files(build_dir);
            if (!trace_output_dir.empty()) {
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
            // Check in common build directories
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

                fs::path build_dir = options.build_dir.empty() ? project_path / "build" : options.build_dir;
                fs::create_directories(build_dir);

                auto compiler = CompilerInfo::from_name(options.compiler);
                auto flags = CompilerFlags::for_compiler(compiler.family, options.enable_tracing, options.enable_memory_profiling);

                std::ostringstream cmd;
                cmd << "cmake -G Ninja";
                cmd << " -S \"" << project_path.string() << "\"";
                cmd << " -B \"" << build_dir.string() << "\"";
                cmd << " -DCMAKE_BUILD_TYPE=" << options.build_type;

                if (!options.compiler.empty()) {
                    cmd << " -DCMAKE_C_COMPILER=" << compiler.c_compiler;
                    cmd << " -DCMAKE_CXX_COMPILER=" << compiler.cxx_compiler;
                }

                if (!flags.empty()) {
                    cmd << " -DCMAKE_CXX_FLAGS=\"" << flags.combined() << "\"";
                    cmd << " -DCMAKE_C_FLAGS=\"" << flags.combined() << "\"";
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

            if (!fs::exists(build_dir / "build.ninja")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            std::ostringstream cmd;
            cmd << "ninja -C \"" << build_dir.string() << "\"";

            int jobs = options.parallel_jobs > 0 ?
                       options.parallel_jobs : get_cpu_count();
            cmd << " -j " << jobs;

            if (options.verbose) {
                cmd << " -v";
            }

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                std::string error_summary = extract_error_summary(output);
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

            // Try to generate with ninja
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

    class MakeAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Make"; }

        [[nodiscard]] std::string description() const override {
            return "GNU Make build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (fs::exists(project_path / "Makefile")) {
                return 0.7;
            }
            if (fs::exists(project_path / "makefile")) {
                return 0.7;
            }
            if (fs::exists(project_path / "GNUmakefile")) {
                return 0.75;
            }
            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            if (!fs::exists(project_path / "configure")) {
                return Result<void, Error>::success();
            }

            auto compiler = CompilerInfo::from_name(options.compiler);
            auto flags = CompilerFlags::for_compiler(compiler.family, options.enable_tracing, options.enable_memory_profiling);

            std::string capture_launcher;
            if (options.enable_tracing && compiler.family == CompilerFamily::GCC) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    capture_launcher = script.string() + " ";
                }
            }

            std::ostringstream cmd;
            cmd << "CC=\"" << capture_launcher << compiler.c_compiler << "\" ";
            cmd << "CXX=\"" << capture_launcher << compiler.cxx_compiler << "\" ";

            if (!flags.empty()) {
                cmd << "CFLAGS=\"" << flags.combined() << "\" ";
                cmd << "CXXFLAGS=\"" << flags.combined() << "\" ";
            }

            cmd << "./configure";
            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::InternalError, "Configure failed: " + output)
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

            if (fs::exists(project_path / "configure") && !fs::exists(project_path / "Makefile")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            auto compiler = CompilerInfo::from_name(options.compiler);
            auto flags = CompilerFlags::for_compiler(compiler.family, options.enable_tracing, options.enable_memory_profiling);

            std::ostringstream cmd;

            if (options.enable_tracing && !options.trace_output_dir.empty() && compiler.family == CompilerFamily::GCC) {
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << options.trace_output_dir.string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << options.trace_output_dir.string() << "\" ";
#endif
            }

            cmd << "make -j" << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            std::string capture_launcher;
            if (options.enable_tracing && compiler.family == CompilerFamily::GCC) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    capture_launcher = script.string() + " ";
                }
            }

            cmd << " CC=\"" << capture_launcher << compiler.c_compiler << "\"";
            cmd << " CXX=\"" << capture_launcher << compiler.cxx_compiler << "\"";

            if (!flags.empty()) {
                cmd << " CFLAGS=\"" << flags.combined() << "\"";
                cmd << " CXXFLAGS=\"" << flags.combined() << "\"";
            }

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                std::string error_summary = extract_error_summary(output);
                result.error_message = error_summary.empty() ? "Build failed" : error_summary;
            }

            result.trace_files = find_trace_files(project_path);
            if (!options.trace_output_dir.empty()) {
                auto trace_dir_files = find_trace_files(options.trace_output_dir);
                result.trace_files.insert(result.trace_files.end(), trace_dir_files.begin(), trace_dir_files.end());
            }

            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(project_path);
            }

            copy_trace_files(project_path, options.trace_output_dir, result.trace_files, result.memory_files);

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            const std::string cmd = "make clean";

            if (auto [exit_code, output] = execute_command(cmd, project_path); exit_code != 0) {
                execute_command("make distclean", project_path);
            }

            return Result<void, Error>::success();
        }

        Result<fs::path, Error> get_compile_commands(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            // Make doesn't generate compile_commands.json natively
            // Use Bear or compiledb if available
            const fs::path compile_commands = project_path / "compile_commands.json";

            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            const std::string cmd = "bear -- make -j" + std::to_string(get_cpu_count());

            if (auto [exit_code, output] = execute_command(cmd, project_path); exit_code == 0 && fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
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

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                std::string error_summary = extract_error_summary(output);
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
            fs::path build_dir = options.build_dir.empty() ? project_path / "builddir" : options.build_dir;

            if (fs::exists(build_dir / "meson-private")) {
                return Result<void, Error>::success();
            }

            fs::create_directories(build_dir);

            auto [family, c_compiler, cxx_compiler] = CompilerInfo::from_name(options.compiler);
            auto flags = CompilerFlags::for_compiler(family, options.enable_tracing, options.enable_memory_profiling);

            std::string capture_launcher;
            if (options.enable_tracing && family == CompilerFamily::GCC) {
                if (auto script = find_capture_script(project_path); !script.empty()) {
                    capture_launcher = script.string() + " ";
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

            fs::path build_dir = options.build_dir.empty() ? project_path / "builddir" : options.build_dir;

            if (!fs::exists(build_dir / "build.ninja") && !fs::exists(build_dir / "meson-private")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            auto compiler = CompilerInfo::from_name(options.compiler);

            std::ostringstream cmd;
            if (options.enable_tracing && !options.trace_output_dir.empty() && compiler.family == CompilerFamily::GCC) {
#ifdef _WIN32
                cmd << "set BHA_TRACE_DIR=" << options.trace_output_dir.string() << " && ";
#else
                cmd << "BHA_TRACE_DIR=\"" << options.trace_output_dir.string() << "\" ";
#endif
            }

            cmd << "meson compile -C \"" << build_dir.string() << "\"";
            cmd << " -j " << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.verbose) {
                cmd << " -v";
            }

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                std::string error_summary = extract_error_summary(output);
                result.error_message = error_summary.empty() ? "Build failed" : error_summary;
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

}  // namespace bha::build_systems