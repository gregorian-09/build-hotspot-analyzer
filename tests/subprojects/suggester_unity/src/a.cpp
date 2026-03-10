#include "common.hpp"

namespace unity_fixture {
    namespace {
        int local_adjust_a(int value) {
            return value + 1;
        }
    }

    std::string compose_name(std::string_view prefix, std::int32_t value) {
        return std::string(prefix) + std::to_string(local_adjust_a(value));
    }
}
