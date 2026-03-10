#pragma once

#include <string>

#if defined(_WIN32)
#include <windows.h>
inline int platform_process_id() {
    return static_cast<int>(GetCurrentProcessId());
}
#else
#include <unistd.h>
inline int platform_process_id() {
    return static_cast<int>(getpid());
}
#endif

inline std::string platform_name() {
#if defined(_WIN32)
    return "windows";
#else
    return "posix";
#endif
}
