#pragma once

#include <array>
#include <cstddef>

namespace heavy {

template<int N>
struct Expander {
    static constexpr int value = N;
    std::array<int, N> storage{};
};

struct HeavySeq768 {
    std::array<int, 768> storage{};
};

}  // namespace heavy
