#include "widgettemplatesafe.hpp"

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

template<typename Tag>
Widgettemplatesafe<Tag>::Widgettemplatesafe() = default;

template<typename Tag>
Widgettemplatesafe<Tag>::~Widgettemplatesafe() = default;

template<typename Tag>
void Widgettemplatesafe<Tag>::add_value(int value) {
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

template<typename Tag>
int Widgettemplatesafe<Tag>::total() const {
    constexpr int type_bias = std::is_same_v<Tag, int> ? 11 : 5;
    return total_ + type_bias + static_cast<int>(queue_.size()) + static_cast<int>(list_.size()) +
           static_cast<int>(seen_.size()) + use_expander<176>() + use_expander<208>() + expander_.value;
}

template<typename Tag>
std::string Widgettemplatesafe<Tag>::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

template class Widgettemplatesafe<int>;
template class Widgettemplatesafe<long>;
template class Widgettemplatesafe<std::integral_constant<int, 1>>;
template class Widgettemplatesafe<std::integral_constant<int, 2>>;
template class Widgettemplatesafe<std::integral_constant<int, 3>>;
template class Widgettemplatesafe<std::integral_constant<int, 4>>;
template class Widgettemplatesafe<std::integral_constant<int, 5>>;
template class Widgettemplatesafe<std::integral_constant<int, 6>>;
template class Widgettemplatesafe<std::integral_constant<int, 7>>;
template class Widgettemplatesafe<std::integral_constant<int, 8>>;
template class Widgettemplatesafe<std::integral_constant<int, 9>>;
template class Widgettemplatesafe<std::integral_constant<int, 10>>;
template class Widgettemplatesafe<std::integral_constant<int, 11>>;
template class Widgettemplatesafe<std::integral_constant<int, 12>>;
