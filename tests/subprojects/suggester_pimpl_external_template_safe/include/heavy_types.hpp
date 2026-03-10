#pragma once

#include <array>

namespace heavy {

template<int N>
struct Expander {
    static constexpr int value = N;
    std::array<int, N> storage{};
};

struct HeavySeq2048 {
    std::array<int, 2048> storage{};
};

}  // namespace heavy
