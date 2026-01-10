//
// Created by gregorian-rayne on 1/5/26.
//

#include "bha/cli/commands/command.hpp"
#include "bha/cli/progress.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/types.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <array>
#include <memory>
#include <sstream>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

namespace bha::cli
{
    namespace fs = std::filesystem;

    /**
     * Record command - captures compiler timing output.
     *
     * This command wraps compiler invocations to capture timing information
     * from GCC (-ftime-report) and MSVC (/Bt+ /d1reportTime) that output
     * to stderr/console rather than to files.
     *
     * Usage:
     *   bha record -o trace.txt -- g++ -ftime-report -c file.cpp
     *   bha record -o traces/ -- make -j4
     *   bha record --compiler gcc -o build/traces -- cmake --build .
     */
    class RecordCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "record";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Capture compiler timing output (GCC/MSVC) during build";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha record [OPTIONS] -- <build-command...>\n"
                   "\n"
                   "Captures compiler timing output that goes to stderr/console.\n"
                   "Use this with GCC's -ftime-report or MSVC's /Bt+ flags.\n"
                   "\n"
                   "Examples:\n"
                   "  bha record -o trace.txt -- g++ -ftime-report -c file.cpp\n"
                   "  bha record -o traces/ -- make -j4 CXXFLAGS='-ftime-report'\n"
                   "  bha record --compiler msvc -o build.log -- cl /Bt+ /c file.cpp\n"
                   "\n"
                   "For Clang, use -ftime-trace instead (outputs JSON files directly).\n"
                   "\n"
                   "Compiler flags for timing:\n"
                   "  GCC:   -ftime-report        (outputs to stderr)\n"
                   "  MSVC:  /Bt+ /d1reportTime   (outputs to stdout)\n"
                   "  Clang: -ftime-trace         (outputs .json files, no need for record)";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"output", 'o', "Output file or directory for traces", true, true, "", "PATH"},
                {"compiler", 'c', "Compiler type hint (gcc, msvc, auto)", false, true, "auto", "TYPE"},
                {"append", 'a', "Append to existing output file", false, false, "", ""},
                {"timestamp", 't', "Add timestamp to output filename", false, false, "", ""},
                {"analyze", 0, "Run analysis after recording", false, false, "", ""},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (args.positional().empty()) {
                return "No build command specified. Use 'bha record [OPTIONS] -- <command>'";
            }
            return "";
        }

        [[nodiscard]] int execute(const ParsedArgs& args) override {
            if (args.get_flag("help")) {
                print_help();
                return 0;
            }

            if (args.get_flag("verbose")) {
                set_verbosity(Verbosity::Verbose);
            } else if (args.get_flag("quiet")) {
                set_verbosity(Verbosity::Quiet);
            }

            auto output_str = args.get("output");
            if (!output_str || output_str->empty()) {
                print_error("Output path required (-o PATH)");
                return 1;
            }
            fs::path output_path(*output_str);

            auto compiler_hint = args.get_or("compiler", "auto");

            const std::vector<std::string>& cmd_parts = args.positional();
            if (cmd_parts.empty()) {
                print_error("No command specified");
                return 1;
            }

            std::string command;
            for (size_t i = 0; i < cmd_parts.size(); ++i) {
                if (i > 0) command += " ";
                // Quote arguments with spaces
                if (cmd_parts[i].find(' ') != std::string::npos) {
                    command += "\"" + cmd_parts[i] + "\"";
                } else {
                    command += cmd_parts[i];
                }
            }

            print_verbose("Running: " + command);

            fs::path trace_file = output_path;
            if (fs::is_directory(output_path) || output_path.string().back() == '/') {
                fs::create_directories(output_path);
                std::string filename = "trace";
                if (args.get_flag("timestamp")) {
                    auto now = std::chrono::system_clock::now();
                    auto time = std::chrono::system_clock::to_time_t(now);
                    char buf[32];

                #ifdef _WIN32
                    std::tm time_info{};
                    localtime_s(&time_info, &time);
                    std::strftime(buf, sizeof(buf), "_%Y%m%d_%H%M%S", &time_info);
                #else
                    std::strftime(buf, sizeof(buf), "_%Y%m%d_%H%M%S", std::localtime(&time));
                #endif

                    filename += buf;
                }
                filename += ".txt";
                trace_file = output_path / filename;
            } else {
                fs::create_directories(output_path.parent_path());
            }

            print_verbose("Capturing output to: " + trace_file.string());

            std::string captured_output;
            int exit_code = execute_and_capture(command, captured_output);

            std::ios_base::openmode mode = std::ios::out;
            if (args.get_flag("append")) {
                mode |= std::ios::app;
            }

            std::ofstream out(trace_file, mode);
            if (!out) {
                print_error("Failed to open output file: " + trace_file.string());
                return 1;
            }

            out << "# BHA Trace Capture\n";
            out << "# Command: " << command << "\n";
            out << "# Exit code: " << exit_code << "\n";
            out << "# ---\n\n";
            out << captured_output;
            out.close();

            auto* parser = parsers::ParserRegistry::instance().find_parser_for_file(trace_file);
            if (parser) {
                print("Captured " + std::string(parser->name()) + " timing output to " + trace_file.string());
            } else {
                print_warning("Output captured but no timing data detected.");
                print_warning("Ensure compiler was invoked with timing flags:");
                print_warning("  GCC:  -ftime-report");
                print_warning("  MSVC: /Bt+ /d1reportTime");
            }

            if (args.get_flag("analyze") && parser) {
                print_verbose("Running analysis...");
                if (auto result = parser->parse_file(trace_file); result.is_ok()) {
                    const auto& unit = result.value();
                    auto to_ms = [](const Duration d) {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(d);
                    };
                    std::cout << "\nQuick Analysis:\n";
                    std::cout << "  Total time: " << format_duration(to_ms(unit.metrics.total_time)) << "\n";
                    std::cout << "  Frontend:   " << format_duration(to_ms(unit.metrics.frontend_time)) << "\n";
                    std::cout << "  Backend:    " << format_duration(to_ms(unit.metrics.backend_time)) << "\n";
                }
            }

            return exit_code;
        }

    private:
        static int execute_and_capture(const std::string& command, std::string& output) {
#ifdef _WIN32
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;

            HANDLE stdout_read, stdout_write;
            HANDLE stderr_read, stderr_write;

            if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
                !CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
                return -1;
                }

            SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si = {0};
            si.cb = sizeof(si);
            si.hStdOutput = stdout_write;
            si.hStdError = stderr_write;
            si.dwFlags |= STARTF_USESTDHANDLES;

            PROCESS_INFORMATION pi = {nullptr};

            std::string cmd_copy = command;
            if (!CreateProcessA(nullptr, &cmd_copy[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
                return -1;
            }

            CloseHandle(stdout_write);
            CloseHandle(stderr_write);

            // Read output
            std::array<char, 4096> buffer{};
            DWORD bytes_read;

            while (ReadFile(stdout_read, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) && bytes_read > 0) {
                output.append(buffer.data(), bytes_read);
            }
            while (ReadFile(stderr_read, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) && bytes_read > 0) {
                output.append(buffer.data(), bytes_read);
            }

            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD exit_code;
            GetExitCodeProcess(pi.hProcess, &exit_code);

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(stdout_read);
            CloseHandle(stderr_read);

            return static_cast<int>(exit_code);
#else
            // POSIX implementation using popen with stderr redirection
            const std::string cmd_with_redirect = command + " 2>&1";

            std::array<char, 4096> buffer{};
            FILE* pipe = popen(cmd_with_redirect.c_str(), "r");

            if (!pipe) {
                return -1;
            }

            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                output += buffer.data();
                // Echo to console for visibility
                if (verbosity() >= Verbosity::Verbose) {
                    std::cout << buffer.data();
                }
            }

            const int status = pclose(pipe);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        }
    };

    namespace {
        struct RecordCommandRegistrar {
            RecordCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<RecordCommand>()
                );
            }
        } record_registrar;
    }
}  // namespace bha::cli