#include "pimpl_widget_external_explicit_copy.hpp"

namespace pimpl_external::explicit_copy {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalExplicitCopy::WidgetExternalExplicitCopy() = default;
WidgetExternalExplicitCopy::~WidgetExternalExplicitCopy() = default;

WidgetExternalExplicitCopy::WidgetExternalExplicitCopy(const WidgetExternalExplicitCopy& other)
    : label_(other.label_ + "-copy")
    , values_(other.values_)
    , counters_(other.counters_)
    , fast_lookup_(other.fast_lookup_)
    , expander_(other.expander_)
    , seq_(other.seq_) {
    counters_["copies"] += 1;
}

WidgetExternalExplicitCopy& WidgetExternalExplicitCopy::operator=(const WidgetExternalExplicitCopy& other) {
    if (this == &other) {
        return *this;
    }
    label_ = other.label_ + "-assign";
    values_ = other.values_;
    counters_ = other.counters_;
    fast_lookup_ = other.fast_lookup_;
    expander_ = other.expander_;
    seq_ = other.seq_;
    counters_["assignments"] += 1;
    return *this;
}

void WidgetExternalExplicitCopy::update_total(int value) {
    counters_["total"] += value;
    fast_lookup_["total"] = counters_["total"];
}

void WidgetExternalExplicitCopy::add_value(const int value) {
    values_.push_back(value);
    update_total(value);
}

int WidgetExternalExplicitCopy::total() const {
    int sum = 0;
    for (int value : values_) {
        sum += value;
    }
    return sum + use_expander<96>() + expander_.value;
}

std::string WidgetExternalExplicitCopy::label() const {
    return label_ + std::to_string(expander_.value);
}

}  // namespace pimpl_external::explicit_copy
