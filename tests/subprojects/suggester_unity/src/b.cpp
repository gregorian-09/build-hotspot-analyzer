#include "common.hpp"

namespace unity_fixture {
    namespace {
        int local_adjust_b(int value) {
            return value + 2;
        }
    }

    std::vector<std::int32_t> make_data(std::int32_t seed) {
        return {seed, local_adjust_b(seed), local_adjust_b(seed + 1)};
    }
}
