#include "pimpl_widget_external.hpp"

namespace pimpl_external {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternal::WidgetExternal() = default;
WidgetExternal::~WidgetExternal() = default;

void WidgetExternal::update_total(const int value) {
    counters_["total"] += value;
    fast_lookup_["total"] = counters_["total"];
}

void WidgetExternal::add_value(const int value) {
    values_.push_back(value);
    update_total(value);
}

int WidgetExternal::total() const {
    int sum = 0;
    for (int value : values_) {
        sum += value;
    }
    return sum + use_expander<180>() + expander_.value;
}

std::string WidgetExternal::label() const {
    return label_ + std::to_string(expander_.value);
}

}  // namespace pimpl_external
