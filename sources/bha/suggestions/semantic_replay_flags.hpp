#pragma once

#include <string_view>

namespace bha::suggestions {
    inline bool is_trace_capture_argument(const std::string_view argument) {
        return argument == "-ftime-trace" || argument.starts_with("-ftime-trace=") ||
            argument.starts_with("-ftime-trace-granularity=") ||
            argument == "-ftime-trace-verbose" || argument.starts_with("-ftime-trace-verbose=") ||
            argument == "/ftime-trace";
    }
}
