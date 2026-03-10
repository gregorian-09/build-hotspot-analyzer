#include "pimpl_widget_external_inheritance_nontrivial.hpp"

namespace pimpl_external::inheritance_nontrivial {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalInheritanceNontrivial::WidgetExternalInheritanceNontrivial() = default;
WidgetExternalInheritanceNontrivial::~WidgetExternalInheritanceNontrivial() = default;

void WidgetExternalInheritanceNontrivial::add_value(int value) {
    values_.push_back(value);
    counters_["total"] += value;
    fast_lookup_["total"] = counters_["total"];
    total_ += value;
}

int WidgetExternalInheritanceNontrivial::total() const {
    return total_ + use_expander<180>() + use_expander<228>() + expander_.value;
}

std::string WidgetExternalInheritanceNontrivial::label() const {
    return label_ + std::to_string(expander_.value);
}

}  // namespace pimpl_external::inheritance_nontrivial
