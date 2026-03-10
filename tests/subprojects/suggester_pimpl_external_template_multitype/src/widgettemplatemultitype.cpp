#include "widgettemplatemultitype.hpp"

#include <filesystem>
#include <map>
#include <regex>
#include <type_traits>
#include <utility>

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

template<typename Left, typename Right>
Widgettemplatemultitype<Left, Right>::Widgettemplatemultitype() = default;

template<typename Left, typename Right>
Widgettemplatemultitype<Left, Right>::~Widgettemplatemultitype() = default;

template<typename Left, typename Right>
void Widgettemplatemultitype<Left, Right>::add_value(int value) {
    values_.push_back(value);
    queue_.push_back(value);
    list_.push_back(value);
    counters_["total"] += value;
    fast_lookup_["total"] = counters_["total"];
    seen_.insert(value);
    matcher_.emplace("[0-9]+");
    payload_ = std::to_string(value);
    triple_ = std::make_tuple(value, value + 1, value + 2);
    cache_path_ /= std::to_string(value);
    total_ += value;
}

template<typename Left, typename Right>
int Widgettemplatemultitype<Left, Right>::total() const {
    constexpr int left_bias = std::is_same_v<Left, int> ? 9 : 4;
    constexpr int right_bias = std::is_same_v<Right, long> ? 5 : 2;
    return total_ + left_bias + right_bias + static_cast<int>(queue_.size()) +
           static_cast<int>(list_.size()) + static_cast<int>(seen_.size()) +
           use_expander<176>() + use_expander<208>() + expander_.value;
}

template<typename Left, typename Right>
std::string Widgettemplatemultitype<Left, Right>::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

template class Widgettemplatemultitype<int, long>;
template class Widgettemplatemultitype<int, double>;
