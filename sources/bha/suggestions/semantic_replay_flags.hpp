#pragma once

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
}
