#pragma once

#include "heavy_types.hpp"

#include <array>
#include <tuple>
#include <vector>

namespace templates {

template<int N>
struct BigTemplate {
    std::array<int, N> data{};
    heavy::Expander<128> exp{};

    int sum() const {
        int total = 0;
        for (int value : data) {
            total += value;
        }
        return total + heavy::Fib<18>::value + exp.value;
    }
};

template<typename T, int Rows, int Cols>
struct Matrix {
    std::array<T, Rows * Cols> data{};

    T trace() const {
        T total{};
        for (int i = 0; i < Rows && i < Cols; ++i) {
            total += data[static_cast<std::size_t>(i * Cols + i)];
        }
        return total;
    }
};

template<int N>
struct RecursiveNode {
    RecursiveNode<N - 1> prev;
    std::array<int, 32> payload{};
};

template<>
struct RecursiveNode<0> {
    std::array<int, 32> payload{};
};

template<int N>
struct RecursiveBox {
    RecursiveNode<N> node{};
    heavy::Expander<192> exp{};

    int value() const {
        return node.payload[0] + exp.value;
    }
};

using Big64 = BigTemplate<64>;
using Big96 = BigTemplate<96>;
using Big128 = BigTemplate<128>;
using Big192 = BigTemplate<192>;
using Rec128 = RecursiveBox<128>;
using Rec160 = RecursiveBox<160>;
using Rec200 = RecursiveBox<200>;

}  // namespace templates
