#pragma once

#include <array>

namespace heavy {

template<int N>
struct Expander {
    static constexpr int value = N;
    std::array<int, N> storage{};
};

struct HeavySeq1536 {
    std::array<int, 1536> storage{};
};

}  // namespace heavy
