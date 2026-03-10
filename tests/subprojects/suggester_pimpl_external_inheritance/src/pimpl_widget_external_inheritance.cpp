#include "pimpl_widget_external_inheritance.hpp"

namespace pimpl_external::inheritance {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalInheritance::WidgetExternalInheritance() = default;
WidgetExternalInheritance::~WidgetExternalInheritance() = default;

void WidgetExternalInheritance::add_value(int value) {
    values_.push_back(value);
    counters_["total"] += value;
    fast_lookup_["total"] = counters_["total"];
    total_ += value;
}

int WidgetExternalInheritance::total() const {
    return total_ + use_expander<180>() + use_expander<220>() + expander_.value;
}

std::string WidgetExternalInheritance::label() const {
    return label_ + std::to_string(expander_.value);
}

}  // namespace pimpl_external::inheritance
