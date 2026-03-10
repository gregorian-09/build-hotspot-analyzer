#pragma once

#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

namespace heavy {

template<int N>
struct Fib {
    static constexpr int value = Fib<N - 1>::value + Fib<N - 2>::value;
};

template<>
struct Fib<1> {
    static constexpr int value = 1;
};

template<>
struct Fib<0> {
    static constexpr int value = 0;
};

template<int N, int... Ns>
struct MakeSeq : MakeSeq<N - 1, N - 1, Ns...> {};

template<int... Ns>
struct MakeSeq<0, Ns...> {
    using type = std::integer_sequence<int, Ns...>;
};

template<int N>
struct DeepType {
    using type = std::tuple<typename DeepType<N - 1>::type, std::array<int, (N % 32) + 1>>;
};

template<>
struct DeepType<0> {
    using type = std::tuple<int, double>;
};

template<int N>
struct Expander {
    using seq = typename MakeSeq<N>::type;
    using nested = typename DeepType<N / 4 + 1>::type;
    static constexpr int value = Fib<(N % 20)>::value;
    std::array<int, 64> data{};
    Expander<N - 1> prev;
};

template<>
struct Expander<0> {
    using seq = std::integer_sequence<int>;
    using nested = std::tuple<int>;
    static constexpr int value = 0;
    std::array<int, 64> data{};
};

using HeavySeq512 = MakeSeq<512>::type;
using HeavySeq768 = MakeSeq<768>::type;
using HeavySeq900 = MakeSeq<900>::type;

using HeavyExpander = Expander<160>;

}  // namespace heavy
