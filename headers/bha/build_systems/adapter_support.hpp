#ifndef BHA_ADAPTER_SUPPORT_HPP
#define BHA_ADAPTER_SUPPORT_HPP

#include "bha/build_systems/adapter.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bha::build_systems::detail {

#ifndef _WIN32
    std::string shell_escape_posix(const std::string& input);
#endif

    struct CompilerInfo {
        CompilerType type = CompilerType::Unknown;
        std::string c_compiler;
        std::string cxx_compiler;
    };

    struct CompilerFlags {
        std::string tracing_flags;
        std::string memory_flags;

        static CompilerFlags for_compiler(const CompilerType type, const bool tracing, const bool memory) {
            CompilerFlags flags;
            if (tracing) {
                switch (type) {
                    case CompilerType::Clang:
                    case CompilerType::AppleClang:
                    case CompilerType::ArmClang:
                        flags.tracing_flags = "-ftime-trace";
                        break;
                    case CompilerType::GCC:
                        flags.tracing_flags = "-ftime-report";
                        break;
                    case CompilerType::MSVC:
                        flags.tracing_flags = "/Bt+ /d1reportTime";
                        break;
                    case CompilerType::IntelClassic:
                        flags.tracing_flags = "-qopt-report=5";
                        break;
                    case CompilerType::IntelOneAPI:
                        flags.tracing_flags = "-ftime-trace";
                        break;
                    case CompilerType::NVCC:
                        flags.tracing_flags = "--time";
                        break;
                    default:
                        break;
                }
            }
            if (memory) {
                switch (type) {
                    case CompilerType::GCC:
                    case CompilerType::Clang:
                    case CompilerType::AppleClang:
                    case CompilerType::ArmClang:
                    case CompilerType::IntelOneAPI:
                        flags.memory_flags = "-fstack-usage";
                        break;
                    case CompilerType::MSVC:
                        flags.memory_flags = "/FAcs";
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

    std::pair<int, std::string> execute_command(
        const std::string& command,
        const fs::path& working_dir = {},
        const std::function<void(const std::string&)>& on_output_line = {},
        const std::function<bool()>& should_cancel = {}
    );

    CompilerInfo get_compiler_info(const BuildOptions& options);
    bool needs_capture_script(CompilerType type);
    fs::path find_capture_script(const fs::path& project_path);
    std::string extract_error_summary(const std::string& output, std::size_t max_lines = 50);
    std::vector<fs::path> find_trace_files(const fs::path& directory);
    std::vector<fs::path> find_memory_files(const fs::path& directory);
    void copy_trace_files(
        const fs::path& source_directory,
        const fs::path& destination_directory,
        std::vector<fs::path>& trace_files,
        std::vector<fs::path>& memory_files
    );
    int get_cpu_count();

    bool init_git_submodules(const fs::path& project_path);
    bool has_git_submodules(const fs::path& project_path);

    bool adapter_tool_available(const IBuildSystemAdapter& adapter);

}  // namespace bha::build_systems::detail

#endif  // BHA_ADAPTER_SUPPORT_HPP
