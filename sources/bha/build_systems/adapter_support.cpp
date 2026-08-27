
#include "bha/build_systems/adapter.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
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
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#include "bha/build_systems/adapter_support.hpp"
#include "bha/utils/file_utils.hpp"

namespace bha::build_systems::detail {
#ifndef _WIN32
        std::string shell_escape_posix(const std::string& input) {
            std::string escaped;
            escaped.reserve(input.size() + 2);
            escaped.push_back('\'');
            for (const char c : input) {
                if (c == '\'') {
                    escaped += "'\\''";
                } else {
                    escaped.push_back(c);
                }
            }
            escaped.push_back('\'');
            return escaped;
        }
#endif

        fs::path find_compiler_path(const std::string& compiler_name) {
            if (compiler_name.empty()) {
                return {};
            }

            if (fs::path p(compiler_name); p.is_absolute() && fs::exists(p)) {
                return p;
            }

            std::string cmd;
#ifdef _WIN32
            cmd = "where " + compiler_name + " 2>nul";
#else
            cmd = "which " + compiler_name + " 2>/dev/null";
#endif
            if (auto [exit_code, output] = execute_command(cmd); exit_code == 0 && !output.empty()) {
                std::istringstream iss(output);
                std::string first_line;
                std::getline(iss, first_line);
                while (!first_line.empty() && (first_line.back() == '\r' || first_line.back() == '\n')) {
                    first_line.pop_back();
                }
                if (!first_line.empty() && fs::exists(first_line)) {
                    return first_line;
                }
            }

            return {};
        }

        bool executable_on_path(const std::string& executable) {
            if (executable.empty()) {
                return false;
            }
            return !find_compiler_path(executable).empty();
        }

        bool adapter_tool_available(const IBuildSystemAdapter& adapter) {
            const std::string adapter_name = adapter.name();
            if (adapter_name == "CMake") {
                return executable_on_path("cmake");
            }
            if (adapter_name == "Ninja") {
                return executable_on_path("ninja");
            }
            if (adapter_name == "Make") {
                return executable_on_path("make") || executable_on_path("gmake");
            }
            if (adapter_name == "Meson") {
                return executable_on_path("meson");
            }
            if (adapter_name == "Bazel") {
                return executable_on_path("bazel") || executable_on_path("bazelisk");
            }
            if (adapter_name == "Buck2") {
                return executable_on_path("buck2") || executable_on_path("buck");
            }
            if (adapter_name == "SCons") {
                return executable_on_path("scons");
            }
            if (adapter_name == "XCode") {
                return executable_on_path("xcodebuild");
            }
            if (adapter_name == "MSBuild") {
                return executable_on_path("msbuild");
            }
            return true;
        }

        CompilerType detect_compiler_type(const std::string& compiler) {
            if (compiler.empty()) {
                return CompilerType::GCC;
            }

            std::string lower = compiler;
            std::ranges::transform(lower, lower.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lower.find("apple") != std::string::npos && lower.find("clang") != std::string::npos) {
                return CompilerType::AppleClang;
            }
            if (lower.find("armclang") != std::string::npos) {
                return CompilerType::ArmClang;
            }
            if (lower.find("icx") != std::string::npos || lower.find("icpx") != std::string::npos) {
                return CompilerType::IntelOneAPI;
            }
            if (lower.find("icc") != std::string::npos || lower.find("icpc") != std::string::npos) {
                return CompilerType::IntelClassic;
            }
            if (lower.find("nvcc") != std::string::npos) {
                return CompilerType::NVCC;
            }
            if (lower.find("clang") != std::string::npos) {
                return CompilerType::Clang;
            }
            if (lower.find("gcc") != std::string::npos || lower.find("g++") != std::string::npos) {
                return CompilerType::GCC;
            }
            if (lower.find("cl.exe") != std::string::npos || lower.find("msvc") != std::string::npos ||
                (lower == "cl")) {
                return CompilerType::MSVC;
            }

            return CompilerType::Unknown;
        }

        struct CompilerPairDefaults {
            std::string c_name;
            std::string cxx_name;
        };

        CompilerPairDefaults compiler_pair_defaults(const CompilerType type) {
            switch (type) {
                case CompilerType::Clang:
                case CompilerType::AppleClang:
                    return {"clang", "clang++"};
                case CompilerType::ArmClang:
                    return {"armclang", "armclang++"};
                case CompilerType::GCC:
                    return {"gcc", "g++"};
                case CompilerType::MSVC:
                    return {"cl", "cl"};
                case CompilerType::IntelClassic:
                    return {"icc", "icpc"};
                case CompilerType::IntelOneAPI:
                    return {"icx", "icpx"};
                case CompilerType::NVCC:
                    return {"nvcc", "nvcc"};
                case CompilerType::Unknown:
                default:
                    return {"gcc", "g++"};
            }
        }

        std::string compiler_leaf_name(const std::string& compiler) {
            if (compiler.empty()) {
                return {};
            }
            std::string leaf = fs::path(compiler).filename().string();
            if (leaf.empty()) {
                leaf = compiler;
            }
            std::ranges::transform(leaf, leaf.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return leaf;
        }

        bool compiler_looks_like_path(const std::string& compiler) {
            return compiler.find('/') != std::string::npos ||
                compiler.find('\\') != std::string::npos ||
                compiler.starts_with(".");
        }

        std::string resolve_compiler_command(const std::string& compiler) {
            if (compiler.empty()) {
                return {};
            }

            const fs::path candidate(compiler);
            if ((candidate.is_absolute() || compiler_looks_like_path(compiler)) && fs::exists(candidate)) {
                return candidate.lexically_normal().string();
            }

            if (const auto resolved = find_compiler_path(compiler); !resolved.empty()) {
                return resolved.string();
            }

            return {};
        }

        std::string resolve_sibling_compiler_command(
            const std::string& known_compiler,
            const std::string& sibling_name
        ) {
            if (known_compiler.empty()) {
                return resolve_compiler_command(sibling_name);
            }

            const fs::path known_path(known_compiler);
            if ((known_path.is_absolute() || compiler_looks_like_path(known_compiler)) &&
                !known_path.parent_path().empty()) {
                const fs::path sibling = known_path.parent_path() / sibling_name;
                if (fs::exists(sibling)) {
                    return sibling.lexically_normal().string();
                }
            }

            return resolve_compiler_command(sibling_name);
        }

        bool compiler_name_is_cxx_driver(const CompilerType type, const std::string& compiler) {
            const std::string leaf = compiler_leaf_name(compiler);
            switch (type) {
                case CompilerType::Clang:
                case CompilerType::AppleClang:
                case CompilerType::ArmClang:
                    return leaf.ends_with("clang++") || leaf.ends_with("clang-cl");
                case CompilerType::GCC:
                    return leaf == "g++" || leaf.starts_with("g++-");
                case CompilerType::IntelClassic:
                    return leaf == "icpc";
                case CompilerType::IntelOneAPI:
                    return leaf == "icpx";
                case CompilerType::MSVC:
                case CompilerType::NVCC:
                    return true;
                case CompilerType::Unknown:
                default:
                    return leaf.find("++") != std::string::npos;
            }
        }

        CompilerInfo get_compiler_info(const BuildOptions& options) {
            CompilerInfo info;

            const std::string explicit_c = options.c_compiler;
            const std::string explicit_cxx = options.cxx_compiler;
            const std::string legacy = options.compiler;

            info.type = detect_compiler_type(!explicit_cxx.empty() ? explicit_cxx :
                (!explicit_c.empty() ? explicit_c : legacy));
            if (info.type == CompilerType::Unknown) {
                info.type = CompilerType::GCC;
            }

            const CompilerPairDefaults defaults = compiler_pair_defaults(info.type);

            if (!explicit_c.empty()) {
                info.c_compiler = resolve_compiler_command(explicit_c);
            }
            if (!explicit_cxx.empty()) {
                info.cxx_compiler = resolve_compiler_command(explicit_cxx);
            }

            if (info.c_compiler.empty() && info.cxx_compiler.empty() && !legacy.empty()) {
                const bool legacy_is_cxx = compiler_name_is_cxx_driver(info.type, legacy);
                if (legacy_is_cxx) {
                    info.cxx_compiler = resolve_compiler_command(legacy);
                    info.c_compiler = resolve_sibling_compiler_command(legacy, defaults.c_name);
                } else {
                    info.c_compiler = resolve_compiler_command(legacy);
                    info.cxx_compiler = resolve_sibling_compiler_command(legacy, defaults.cxx_name);
                }
            }

            if (info.c_compiler.empty() && !info.cxx_compiler.empty()) {
                info.c_compiler = resolve_sibling_compiler_command(info.cxx_compiler, defaults.c_name);
            }
            if (info.cxx_compiler.empty() && !info.c_compiler.empty()) {
                info.cxx_compiler = resolve_sibling_compiler_command(info.c_compiler, defaults.cxx_name);
            }

            if (info.c_compiler.empty()) {
                info.c_compiler = resolve_compiler_command(defaults.c_name);
            }
            if (info.cxx_compiler.empty()) {
                info.cxx_compiler = resolve_compiler_command(defaults.cxx_name);
            }

            return info;
        }

        bool needs_capture_script(const CompilerType type) {
            return type == CompilerType::GCC ||
                   type == CompilerType::MSVC;
        }

        fs::path get_executable_path() {
#ifdef _WIN32
            char path[MAX_PATH];
            if (GetModuleFileNameA(nullptr, path, MAX_PATH) > 0) {
                return fs::path(path).parent_path();
            }
#elif defined(__APPLE__)
            char path[1024];
            uint32_t size = sizeof(path);
            if (::_NSGetExecutablePath(path, &size) == 0) {
                return fs::canonical(fs::path(path)).parent_path();
            }
#else
            std::error_code ec;
            if (auto exe_path = fs::read_symlink("/proc/self/exe", ec); !ec) {
                return exe_path.parent_path();
            }
#endif
            return {};
        }

        fs::path find_capture_script(const fs::path& project_path) {
#ifdef _WIN32
            auto script_name = "bha-capture.bat";
#else
            const char* script_name = "bha-capture.sh";
#endif
            std::vector search_paths = {
                project_path / "cmake" / script_name,
                fs::current_path() / "cmake" / script_name,
                fs::current_path().parent_path() / "cmake" / script_name
            };

            if (const auto exe_dir = get_executable_path(); !exe_dir.empty()) {
                search_paths.push_back(exe_dir / "cmake" / script_name);
                search_paths.push_back(exe_dir.parent_path() / "cmake" / script_name);
                search_paths.push_back(exe_dir / ".." / "cmake" / script_name);
            }

            // BHA_SCRIPT_DIR environment variable as fallback
#ifdef _WIN32
            char* script_dir = nullptr;
            size_t len = 0;
            if (_dupenv_s(&script_dir, &len, "BHA_SCRIPT_DIR") == 0 && script_dir) {
                search_paths.insert(search_paths.begin(),
                    fs::path(script_dir) / script_name);
                free(script_dir);
            }
#else
            if (const char* script_dir = std::getenv("BHA_SCRIPT_DIR")) {
                search_paths.insert(search_paths.begin(),
                    fs::path(script_dir) / script_name);
            }
#endif


            for (const auto& path : search_paths) {
                if (std::error_code ec; fs::exists(path, ec) && !ec) {
                    return fs::absolute(path, ec);
                }
            }
            return {};
        }

        std::string extract_error_summary(const std::string& output, size_t max_lines) {
            std::istringstream stream(output);
            std::string line;
            std::vector<std::string> all_lines;
            std::vector<std::string> error_lines;

            while (std::getline(stream, line)) {
                all_lines.push_back(line);
                std::string lower_line = line;
                std::ranges::transform(lower_line, lower_line.begin(),
                                      [](const unsigned char c) {
                                          return static_cast<char>(std::tolower(c));
                                      });

                if (lower_line.find("error") != std::string::npos ||
                    lower_line.find("fatal") != std::string::npos ||
                    lower_line.find("undefined reference") != std::string::npos ||
                    lower_line.find("cannot find") != std::string::npos) {
                    error_lines.push_back(line);
                }
            }

            std::ostringstream summary;

            if (!error_lines.empty()) {
                const size_t count = std::min(error_lines.size(), max_lines);
                for (size_t i = 0; i < count; ++i) {
                    summary << error_lines[i] << "\n";
                }
                if (error_lines.size() > max_lines) {
                    summary << "... (" << (error_lines.size() - max_lines) << " more errors)\n";
                }
            } else if (!all_lines.empty()) {
                const size_t start = all_lines.size() > max_lines ? all_lines.size() - max_lines : 0;
                for (size_t i = start; i < all_lines.size(); ++i) {
                    summary << all_lines[i] << "\n";
                }
            }

            return summary.str();
        }

        std::pair<int, std::string> execute_command(
            const std::string& command,
            const fs::path& working_dir,
            const std::function<void(const std::string&)>& on_output_line,
            const std::function<bool()>& should_cancel
        ) {
            std::string output;
            int exit_code = -1;
            std::string pending_line;

            const auto emit_output = [&](const std::string_view chunk) {
                if (chunk.empty()) {
                    return;
                }
                output.append(chunk);
                if (!on_output_line) {
                    return;
                }

                pending_line.append(chunk);
                std::size_t newline_pos = 0;
                while ((newline_pos = pending_line.find('\n')) != std::string::npos) {
                    std::string line = pending_line.substr(0, newline_pos);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    on_output_line(line);
                    pending_line.erase(0, newline_pos + 1);
                }
            };

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

            const std::string cmd_line = "cmd /c " + command;
            const std::string work_dir = working_dir.empty() ? "" : working_dir.string();

            const BOOL process_created = CreateProcessA(
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
                while (true) {
                    if (should_cancel && should_cancel()) {
                        TerminateProcess(pi.hProcess, 130);
                    }
                    if (!ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr)
                        || bytes_read == 0) {
                        break;
                    }
                    emit_output(std::string_view(buffer.data(), bytes_read));
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
            int pipe_fds[2]{-1, -1};
            if (pipe(pipe_fds) == 0) {
                const pid_t pid = fork();
                if (pid == 0) {
                    if (!working_dir.empty()) {
                        if (chdir(working_dir.c_str()) != 0) {
                            _exit(127);
                        }
                    }
                    dup2(pipe_fds[1], STDOUT_FILENO);
                    dup2(pipe_fds[1], STDERR_FILENO);
                    close(pipe_fds[0]);
                    close(pipe_fds[1]);
                    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
                    _exit(127);
                }

                close(pipe_fds[1]);
                pipe_fds[1] = -1;

                if (pid > 0) {
                    const int flags = fcntl(pipe_fds[0], F_GETFL, 0);
                    if (flags >= 0) {
                        (void)fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
                    }

                    std::array<char, 4096> buffer{};
                    bool child_running = true;
                    bool terminated_for_cancel = false;

                    while (child_running) {
                        const ssize_t bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
                        if (bytes_read > 0) {
                            emit_output(std::string_view(buffer.data(), static_cast<std::size_t>(bytes_read)));
                        } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            break;
                        }

                        int status = 0;
                        const pid_t wait_result = waitpid(pid, &status, WNOHANG);
                        if (wait_result == pid) {
                            child_running = false;
                            if (WIFEXITED(status)) {
                                exit_code = WEXITSTATUS(status);
                            } else if (WIFSIGNALED(status)) {
                                exit_code = 128 + WTERMSIG(status);
                            } else {
                                exit_code = status;
                            }
                            continue;
                        }

                        if (should_cancel && should_cancel()) {
                            terminated_for_cancel = true;
                            kill(pid, SIGTERM);
                            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                            bool child_exited_on_cancel = false;
                            while (std::chrono::steady_clock::now() < deadline) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                const pid_t cancel_wait = waitpid(pid, &status, WNOHANG);
                                if (cancel_wait == pid) {
                                    child_exited_on_cancel = true;
                                    if (WIFEXITED(status)) {
                                        exit_code = WEXITSTATUS(status);
                                    } else if (WIFSIGNALED(status)) {
                                        exit_code = 128 + WTERMSIG(status);
                                    } else {
                                        exit_code = status;
                                    }
                                    break;
                                }
                            }
                            if (!child_exited_on_cancel) {
                                kill(pid, SIGKILL);
                                waitpid(pid, &status, 0);
                                exit_code = WIFSIGNALED(status) ? 128 + WTERMSIG(status) : status;
                            }
                            break;
                        }

                        if (bytes_read <= 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(25));
                        }
                    }

                    while (true) {
                        const ssize_t bytes_read = read(pipe_fds[0], buffer.data(), buffer.size());
                        if (bytes_read <= 0) {
                            break;
                        }
                        emit_output(std::string_view(buffer.data(), static_cast<std::size_t>(bytes_read)));
                    }

                    if (terminated_for_cancel && exit_code == -1) {
                        exit_code = 130;
                    }
                } else {
                    close(pipe_fds[0]);
                    pipe_fds[0] = -1;
                }

                if (pipe_fds[0] >= 0) {
                    close(pipe_fds[0]);
                }
            }
#endif

            if (on_output_line && !pending_line.empty()) {
                if (!pending_line.empty() && pending_line.back() == '\r') {
                    pending_line.pop_back();
                }
                on_output_line(pending_line);
            }

            if (const char* log_path = std::getenv("BHA_BUILD_LOG"); log_path && *log_path) {
                std::ofstream log(log_path, std::ios::app);
                if (log) {
                    log << "=== BHA build command ===\n";
                    log << "Working dir: " << (working_dir.empty() ? "." : working_dir.string()) << "\n";
                    log << "Command: " << command << "\n";
                    log << "Exit code: " << exit_code << "\n";
                    if (output.empty()) {
                        log << "(no output)\n";
                    } else {
                        log << output;
                        if (!output.ends_with('\n')) {
                            log << "\n";
                        }
                    }
                }
            }

            return std::pair{exit_code, std::move(output)};
        }

        std::vector<fs::path> find_trace_files(const fs::path& dir) {
            std::vector<fs::path> traces;

            if (!fs::exists(dir)) {
                return traces;
            }

            for (std::error_code ec; const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }

                const std::string filename = entry.path().filename().string();
                const std::string ext = entry.path().extension().string();

                if (ext == ".json") {
                    if (const std::string stem = entry.path().stem().string(); stem.ends_with(".c") || stem.ends_with(".cc") ||
                        stem.ends_with(".cpp") || stem.ends_with(".cxx") ||
                        stem.ends_with(".C") || stem.ends_with(".c++") ||
                        stem.ends_with(".m") || stem.ends_with(".mm")) {
                        traces.push_back(entry.path());
                    } else {
                        if (const fs::path parent = entry.path().parent_path(); fs::exists(parent / (stem + ".o")) ||
                            fs::exists(parent / (stem + ".obj"))) {
                            traces.push_back(entry.path());
                        }
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
                if (ec) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }

                if (entry.path().extension() == ".su") {
                    memory_files.push_back(entry.path());
                }
            }

            return memory_files;
        }

        namespace {
            std::optional<fs::path> latest_json_file(
                const fs::path& directory,
                const std::string_view prefix
            ) {
                std::error_code ec;
                if (!fs::is_directory(directory, ec)) {
                    return std::nullopt;
                }

                std::optional<fs::path> latest;
                std::optional<fs::file_time_type> latest_time;
                for (const auto& entry : fs::directory_iterator(directory, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_regular_file(ec) || ec || entry.path().extension() != ".json" ||
                        !entry.path().filename().string().starts_with(prefix)) {
                        ec.clear();
                        continue;
                    }

                    const auto modified = fs::last_write_time(entry.path(), ec);
                    if (ec) {
                        ec.clear();
                        continue;
                    }
                    if (!latest_time.has_value() || modified > *latest_time ||
                        (modified == *latest_time && entry.path() > *latest)) {
                        latest = entry.path();
                        latest_time = modified;
                    }
                }
                return latest;
            }
        }

        std::optional<fs::path> find_cmake_instrumentation_index(
            const fs::path& build_directory
        ) {
            return latest_json_file(
                build_directory / ".cmake" / "instrumentation" / "v1" / "data" / "index",
                "index-"
            );
        }

        std::optional<fs::path> find_cmake_file_api_index(
            const fs::path& build_directory
        ) {
            return latest_json_file(
                build_directory / ".cmake" / "api" / "v1" / "reply",
                "index-"
            );
        }

        bool ensure_cmake_analysis_queries(const fs::path& build_directory) {
            bool created = false;
            const fs::path file_api_query =
                build_directory / ".cmake" / "api" / "v1" / "query" / "client-bha" /
                "codemodel-v2";
            if (!fs::exists(file_api_query)) {
                const auto result = utils::write_file(file_api_query, "");
                if (!result.is_ok()) {
                    return false;
                }
                created = true;
            }

            const fs::path instrumentation_query =
                build_directory / ".cmake" / "instrumentation" / "v1" / "query" /
                "client-bha.json";
            if (!fs::exists(instrumentation_query)) {
                constexpr std::string_view content = R"json({
  "version": 1,
  "hooks": ["postCMakeBuild"],
  "options": ["staticSystemInformation", "dynamicSystemInformation"]
})json";
                const auto result = utils::write_file(instrumentation_query, content);
                if (!result.is_ok()) {
                    return false;
                }
                created = true;
            }
            return created;
        }

        void copy_trace_files(const fs::path& source_dir, const fs::path& dest_dir,
                            std::vector<fs::path>& trace_files, std::vector<fs::path>& memory_files) {
            if (source_dir == dest_dir || dest_dir.empty()) {
                return;
            }

            std::error_code ec;
            fs::create_directories(dest_dir, ec);
            if (ec) {
                return;
            }

            std::vector<fs::path> new_trace_files;
            std::vector<fs::path> new_memory_files;

            const auto normalized_dest = fs::absolute(dest_dir, ec).lexically_normal();
            if (ec) {
                return;
            }

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
                    const fs::path dest_file = dest_dir / trace_file.filename();
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
                    const fs::path dest_file = dest_dir / memory_file.filename();
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


}  // namespace bha::build_systems::detail
