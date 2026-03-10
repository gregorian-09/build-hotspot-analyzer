#include "pimpl_widget_external_multiline.hpp"

namespace pimpl_external_multiline {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalMultiline::WidgetExternalMultiline() = default;
WidgetExternalMultiline::~WidgetExternalMultiline() = default;

void WidgetExternalMultiline::update_total(int value, int bias) {
    counters_["total"] += value + bias;
    fast_lookup_["total"] = counters_["total"];
}

void WidgetExternalMultiline::add_value(const int value) {
    values_.push_back(value);
    update_total(value, expander_.value % 2);
}

int WidgetExternalMultiline::total() const {
    int sum = 0;
    for (int value : values_) {
        sum += value;
    }
    return sum + use_expander<180>() + expander_.value;
}

std::string WidgetExternalMultiline::label() const {
    return label_ + std::to_string(expander_.value);
}

}  // namespace pimpl_external_multiline
