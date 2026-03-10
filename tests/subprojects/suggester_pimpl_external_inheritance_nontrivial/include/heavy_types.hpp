#pragma once

#include <array>

namespace heavy {

template<int N>
struct Expander {
    static constexpr int value = N;
    std::array<int, N> storage{};
};

struct HeavySeq1024 {
    std::array<int, 1024> storage{};
};

}  // namespace heavy
