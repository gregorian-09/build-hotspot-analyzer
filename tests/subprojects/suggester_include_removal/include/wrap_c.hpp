#pragma once

#include "platform_compat.hpp"
#include "unused_heavy.hpp"

inline int wrap_c_value() {
    return platform_process_id() + 3;
}
