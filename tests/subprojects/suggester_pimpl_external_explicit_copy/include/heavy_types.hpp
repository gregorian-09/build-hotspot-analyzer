#pragma once

#include <array>

namespace heavy {

template<int N>
struct Expander {
    static constexpr int value = N;
    std::array<int, N> storage{};
};

struct HeavySeq256 {
    std::array<int, 256> storage{};
};

}  // namespace heavy
