#pragma once

#include <utility>

namespace suggest_template_existing {

template<int N, int... Ns>
struct MakeSeq : MakeSeq<N - 1, N - 1, Ns...> {};

template<int... Ns>
struct MakeSeq<0, Ns...> {
    using type = std::integer_sequence<int, Ns...>;
    static constexpr int size = sizeof...(Ns);
};

template<int N>
struct BuildSet {
    using seq = typename MakeSeq<N>::type;
    static constexpr int value = MakeSeq<N>::size;
};

extern template struct BuildSet<12>;

}  // namespace suggest_template_existing
