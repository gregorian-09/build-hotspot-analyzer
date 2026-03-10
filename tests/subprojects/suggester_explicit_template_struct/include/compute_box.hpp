#pragma once

#include <array>
#include <utility>

namespace outer::inner {

template<int N, int... Ns>
struct MakeSeq : MakeSeq<N - 1, N - 1, Ns...> {};

template<int... Ns>
struct MakeSeq<0, Ns...> {
    using type = std::integer_sequence<int, Ns...>;
    static constexpr int size = sizeof...(Ns);
};

template<typename T, int N>
struct Packet {
    using seq = typename MakeSeq<N>::type;
    std::array<T, N> data{};

    T score() const {
        T total{};
        for (T value : data) {
            total += value;
        }
        return total + static_cast<T>(MakeSeq<N>::size);
    }
};

}  // namespace outer::inner
