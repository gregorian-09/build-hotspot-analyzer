#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace bha::suggestions {
    inline std::string semantic_replay_tool_name(const std::vector<std::string>& command_line) {
        return command_line.empty() ? "clang-tool" : command_line.front();
    }

    inline bool is_trace_capture_argument(const std::string_view argument) {
        return argument == "-ftime-trace" || argument.starts_with("-ftime-trace=") ||
            argument.starts_with("-ftime-trace-granularity=") ||
            argument == "-ftime-trace-verbose" || argument.starts_with("-ftime-trace-verbose=") ||
            argument == "/ftime-trace";
    }

    inline void append_semantic_replay_resource_dir(
        std::vector<std::string>& arguments,
        const std::string_view resource_dir
    ) {
        if (resource_dir.empty()) {
            return;
        }
        for (const auto& argument : arguments) {
            if (argument == "-resource-dir" || argument.starts_with("-resource-dir=")) {
                return;
            }
        }
        arguments.emplace_back("-resource-dir");
        arguments.emplace_back(resource_dir);
    }

    inline void append_semantic_replay_resource_dir(std::vector<std::string>& arguments) {
        if (const char* resource_dir = std::getenv("BHA_CLANG_RESOURCE_DIR")) {
            append_semantic_replay_resource_dir(arguments, resource_dir);
        }
    }
}
