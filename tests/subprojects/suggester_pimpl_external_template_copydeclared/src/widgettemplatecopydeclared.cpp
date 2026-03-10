#include "widgettemplatecopydeclared.hpp"

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
Widgettemplatecopydeclared<Tag>::Widgettemplatecopydeclared() = default;

template<typename Tag>
Widgettemplatecopydeclared<Tag>::~Widgettemplatecopydeclared() = default;

template<typename Tag>
void Widgettemplatecopydeclared<Tag>::add_value(int value) {
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
int Widgettemplatecopydeclared<Tag>::total() const {
    constexpr int type_bias = std::is_same_v<Tag, int> ? 13 : 6;
    return total_ + type_bias + static_cast<int>(queue_.size()) + static_cast<int>(list_.size()) +
           static_cast<int>(seen_.size()) + use_expander<176>() + use_expander<220>() + expander_.value;
}

template<typename Tag>
std::string Widgettemplatecopydeclared<Tag>::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

template class Widgettemplatecopydeclared<int>;
template class Widgettemplatecopydeclared<long>;
