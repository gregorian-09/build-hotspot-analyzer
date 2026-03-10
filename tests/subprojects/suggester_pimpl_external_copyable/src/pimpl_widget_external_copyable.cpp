#include "pimpl_widget_external_copyable.hpp"

#include <utility>

namespace pimpl_external::copyable {

template<int N>
int use_expander() {
    heavy::Expander<N> instance{};
    return instance.value;
}

WidgetExternalCopyable::WidgetExternalCopyable() = default;
WidgetExternalCopyable::~WidgetExternalCopyable() = default;

int WidgetExternalCopyable::bias() const {
    return this->expander_.value % 4;
}

void WidgetExternalCopyable::update_total(int value) {
    counters_["total"] += value;
    this->fast_lookup_["total"] = counters_["total"];
}

void WidgetExternalCopyable::add_value(const int value) {
    this->values_.push_back(value);
    update_total(value + bias());
}

void WidgetExternalCopyable::set_label(std::string label) {
    this->label_ = std::move(label);
}

int WidgetExternalCopyable::total() const {
    int sum = 0;
    for (int value : this->values_) {
        sum += value;
    }
    return sum + use_expander<128>() + this->expander_.value;
}

std::string WidgetExternalCopyable::label() const {
    return this->label_ + std::to_string(this->expander_.value);
}

}  // namespace pimpl_external::copyable
