#pragma once

#include <array>
#include <tuple>
#include <utility>

namespace split_heavy {

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
struct Seq : Seq<N - 1, N - 1, Ns...> {};

template<int... Ns>
struct Seq<0, Ns...> {
    using type = std::integer_sequence<int, Ns...>;
};

template<int N>
struct Nested {
    using type = std::tuple<typename Nested<N - 1>::type, std::array<int, (N % 32) + 1>>;
};

template<>
struct Nested<0> {
    using type = std::tuple<int, double>;
};

template<int N>
struct Node {
    using seq = typename Seq<N>::type;
    using nested = typename Nested<(N / 6) + 1>::type;
    std::array<int, 32> data{};
    Node<N - 1> prev;
};

template<>
struct Node<0> {
    using seq = std::integer_sequence<int>;
    using nested = std::tuple<int>;
    std::array<int, 32> data{};
};

using HeavyNode = Node<96>;
using HeavySeq = Seq<768>::type;

}  // namespace split_heavy
