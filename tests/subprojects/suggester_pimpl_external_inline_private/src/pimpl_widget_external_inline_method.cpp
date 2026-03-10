#include "pimpl_widget_external_inline_method.hpp"

#include <filesystem>
#include <map>
#include <regex>

namespace pimpl_external::inline_private {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalInlinePrivate::WidgetExternalInlinePrivate() = default;
WidgetExternalInlinePrivate::~WidgetExternalInlinePrivate() = default;

void WidgetExternalInlinePrivate::add_value(int value) {
    values_.push_back(value);
    update_total(value);
}

int WidgetExternalInlinePrivate::total() const {
    int sum = 0;
    for (int value : values_) {
        sum += value;
    }
    return sum + static_cast<int>(queue_.size()) + static_cast<int>(list_.size()) +
           static_cast<int>(seen_.size()) + use_expander<192>() + use_expander<208>() + expander_.value;
}

std::string WidgetExternalInlinePrivate::label() const {
    return label_ + std::to_string(expander_.value) + cache_path_.string();
}

}  // namespace pimpl_external::inline_private
