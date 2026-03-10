#include "common.hpp"

#include <numeric>

namespace unity_fixture {
    namespace {
        std::int32_t local_adjust_c(std::int32_t value) {
            return value * 2;
        }
    }

    std::int32_t checksum(std::int32_t seed) {
        auto data = make_data(seed);
        return std::accumulate(data.begin(), data.end(), local_adjust_c(seed));
    }
}
