#include "widgettemplatemultilinedecl.hpp"

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
Widgettemplatemultilinedecl<Tag>::Widgettemplatemultilinedecl() = default;

template<typename Tag>
Widgettemplatemultilinedecl<Tag>::~Widgettemplatemultilinedecl() = default;

template<typename Tag>
void Widgettemplatemultilinedecl<Tag>::add_value(int value) {
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
int Widgettemplatemultilinedecl<Tag>::total() const {
    constexpr int type_bias = std::is_same_v<Tag, int> ? 8 : 3;
    return total_ + type_bias + static_cast<int>(queue_.size()) + static_cast<int>(list_.size()) +
           static_cast<int>(seen_.size()) + use_expander<160>() + use_expander<200>() + expander_.value;
}

template<typename Tag>
std::string Widgettemplatemultilinedecl<Tag>::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

template class Widgettemplatemultilinedecl<int>;
template class Widgettemplatemultilinedecl<long>;
