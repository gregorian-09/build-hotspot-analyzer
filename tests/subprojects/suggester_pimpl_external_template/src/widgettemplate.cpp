#include "widgettemplate.hpp"

#include <filesystem>
#include <map>
#include <regex>
#include <type_traits>
#include <utility>

namespace pimpl_external::templated {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

template<typename Tag>
Widgettemplate<Tag>::Widgettemplate() = default;

template<typename Tag>
Widgettemplate<Tag>::~Widgettemplate() = default;

template<typename Tag>
void Widgettemplate<Tag>::add_value(int value) {
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
int Widgettemplate<Tag>::total() const {
    constexpr int type_bias = std::is_same_v<Tag, int> ? 11 : 5;
    return total_ + type_bias + static_cast<int>(queue_.size()) + static_cast<int>(list_.size()) +
           static_cast<int>(seen_.size()) + use_expander<176>() + use_expander<208>() + expander_.value;
}

template<typename Tag>
std::string Widgettemplate<Tag>::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

template class Widgettemplate<int>;
template class Widgettemplate<long>;
template class Widgettemplate<std::integral_constant<int, 1>>;
template class Widgettemplate<std::integral_constant<int, 2>>;
template class Widgettemplate<std::integral_constant<int, 3>>;
template class Widgettemplate<std::integral_constant<int, 4>>;
template class Widgettemplate<std::integral_constant<int, 5>>;
template class Widgettemplate<std::integral_constant<int, 6>>;
template class Widgettemplate<std::integral_constant<int, 7>>;
template class Widgettemplate<std::integral_constant<int, 8>>;
template class Widgettemplate<std::integral_constant<int, 9>>;
template class Widgettemplate<std::integral_constant<int, 10>>;
template class Widgettemplate<std::integral_constant<int, 11>>;
template class Widgettemplate<std::integral_constant<int, 12>>;

}  // namespace pimpl_external::templated
