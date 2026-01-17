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

        /**
         * Execute a command and capture output.
         */
        std::pair<int, std::string> execute_command(
            const std::string& command,
            const fs::path& working_dir = fs::path()
        ) {
            std::string output;
            int exit_code = -1;

#ifdef _WIN32
            // Windows implementation
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

            if (CreateProcessA(
                    nullptr,
                    const_cast<char*>(cmd_line.c_str()),
                    nullptr, nullptr, TRUE, 0, nullptr,
                    work_dir.empty() ? nullptr : work_dir.c_str(),
                    &si, &pi)) {

                CloseHandle(write_pipe);

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

        /**
         * Find trace files in a directory.
         */
        std::vector<fs::path> find_trace_files(const fs::path& dir) {
            std::vector<fs::path> traces;

            if (!fs::exists(dir)) {
                return traces;
            }

            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;

                std::string filename = entry.path().filename().string();

                // Clang time-trace files
                if (std::string ext = entry.path().extension().string(); ext == ".json" && filename.find("time-trace") != std::string::npos) {
                    traces.push_back(entry.path());
                }
                // Generic trace files
                if (filename.find(".trace") != std::string::npos ||
                    filename.find("_trace") != std::string::npos) {
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

            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();

                if (ext == ".su" || ext == ".map") {
                    memory_files.push_back(entry.path());
                }
            }

            return memory_files;
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
        for (const auto& adapter : adapters_) {
            if (adapter->name() == name) {
                return adapter.get();
            }
        }
        return nullptr;
    }

    // --------------------------------------------------------------------------
    // CMake Adapter
    // --------------------------------------------------------------------------

    class CMakeAdapter : public IBuildSystemAdapter {
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
            fs::path build_dir = options.build_dir;
            if (build_dir.empty()) {
                build_dir = project_path / "build";
            }

            fs::create_directories(build_dir);

            std::ostringstream cmd;
            cmd << "cmake";
            cmd << " -S \"" << project_path.string() << "\"";
            cmd << " -B \"" << build_dir.string() << "\"";
            cmd << " -DCMAKE_BUILD_TYPE=" << options.build_type;
            cmd << " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";

            if (options.enable_tracing || options.enable_memory_profiling) {
                std::string flags;
                if (options.enable_tracing) {
                    flags += "-ftime-trace";
                }
                if (options.enable_memory_profiling) {
                    if (!flags.empty()) flags += " ";
                    flags += "-fmem-report -fstack-usage";
                }
                cmd << " -DCMAKE_CXX_FLAGS=\"" << flags << "\"";
                cmd << " -DCMAKE_C_FLAGS=\"" << flags << "\"";
            }

            if (!options.compiler.empty()) {
                cmd << " -DCMAKE_CXX_COMPILER=" << options.compiler;
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

            fs::path build_dir = options.build_dir;
            if (build_dir.empty()) {
                build_dir = project_path / "build";
            }

            if (!fs::exists(build_dir / "CMakeCache.txt")) {
                if (auto config_result = configure(project_path, options); !config_result.is_ok()) {
                    result.error_message = config_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            std::ostringstream cmd;
            cmd << "cmake --build \"" << build_dir.string() << "\"";

            int jobs = options.parallel_jobs > 0 ?
                       options.parallel_jobs : get_cpu_count();
            cmd << " -j " << jobs;

            if (options.verbose) {
                cmd << " --verbose";
            }

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                result.error_message = "Build failed";
            }

            result.trace_files = find_trace_files(build_dir);
            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(build_dir);
            }

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

    class NinjaAdapter : public IBuildSystemAdapter {
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
            // Ninja doesn't have a separate configure step
            // If there's a CMakeLists.txt, use CMake to generate Ninja files
            if (fs::exists(project_path / "CMakeLists.txt")) {
                fs::path build_dir = options.build_dir;
                if (build_dir.empty()) {
                    build_dir = project_path / "build";
                }

                fs::create_directories(build_dir);

                std::ostringstream cmd;
                cmd << "cmake -G Ninja";
                cmd << " -S \"" << project_path.string() << "\"";
                cmd << " -B \"" << build_dir.string() << "\"";
                cmd << " -DCMAKE_BUILD_TYPE=" << options.build_type;

                if (options.enable_tracing || options.enable_memory_profiling) {
                    std::string flags;
                    if (options.enable_tracing) {
                        flags += "-ftime-trace";
                    }
                    if (options.enable_memory_profiling) {
                        if (!flags.empty()) flags += " ";
                        flags += "-fmem-report -fstack-usage";
                    }
                    cmd << " -DCMAKE_CXX_FLAGS=\"" << flags << "\"";
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
                if (fs::exists(project_path / "build.ninja")) {
                    build_dir = project_path;
                } else {
                    build_dir = project_path / "build";
                }
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
                result.error_message = "Build failed";
            }

            result.trace_files = find_trace_files(build_dir);
            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(build_dir);
            }

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

    class MakeAdapter : public IBuildSystemAdapter {
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
            if (fs::exists(project_path / "configure")) {
                std::ostringstream cmd;
                cmd << "./configure";

                if (options.enable_tracing || options.enable_memory_profiling) {
                    std::string flags;
                    if (options.enable_tracing) {
                        flags += "-ftime-trace";
                    }
                    if (options.enable_memory_profiling) {
                        if (!flags.empty()) flags += " ";
                        flags += "-fmem-report -fstack-usage";
                    }
                    cmd << " CXXFLAGS=\"" << flags << "\"";
                    cmd << " CFLAGS=\"" << flags << "\"";
                }

                if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                    return Result<void, Error>::failure(
                        Error(ErrorCode::InternalError, "Configure failed: " + output)
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
            const auto start = std::chrono::steady_clock::now();

            if (options.clean_first) {
                clean(project_path, options);
            }

            std::ostringstream cmd;
            cmd << "make";

            const int jobs = options.parallel_jobs > 0 ?
                       options.parallel_jobs : get_cpu_count();
            cmd << " -j" << jobs;

            if (options.enable_tracing) {
                cmd << " CXXFLAGS=\"-ftime-trace\"";
                cmd << " CFLAGS=\"-ftime-trace\"";
            }

            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            auto [exit_code, output] = execute_command(cmd.str(), project_path);

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                result.error_message = "Build failed";
            }

            result.trace_files = find_trace_files(project_path);
            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(project_path);
            }

            const auto end = std::chrono::steady_clock::now();
            result.build_time = std::chrono::duration_cast<Duration>(end - start);

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

    class MSBuildAdapter : public IBuildSystemAdapter {
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
                result.error_message = "Build failed";
            }

            result.trace_files = find_trace_files(project_path);
            if (options.enable_memory_profiling) {
                result.memory_files = find_memory_files(project_path);
            }

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

}  // namespace bha::build_systems