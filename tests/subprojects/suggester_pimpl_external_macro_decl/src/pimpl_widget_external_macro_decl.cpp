#include "pimpl_widget_external_macro_decl.hpp"

#include <filesystem>
#include <map>
#include <regex>

namespace pimpl_external::macro_decl {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalMacroDecl::WidgetExternalMacroDecl() = default;
WidgetExternalMacroDecl::~WidgetExternalMacroDecl() = default;

void WidgetExternalMacroDecl::add_value(int value) {
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
}

int WidgetExternalMacroDecl::total() const {
    int sum = 0;
    for (int value : values_) {
        sum += value;
    }
    return sum + static_cast<int>(queue_.size()) + static_cast<int>(list_.size()) +
           static_cast<int>(seen_.size()) + use_expander<192>() + use_expander<208>() + expander_.value;
}

std::string WidgetExternalMacroDecl::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

}  // namespace pimpl_external::macro_decl
