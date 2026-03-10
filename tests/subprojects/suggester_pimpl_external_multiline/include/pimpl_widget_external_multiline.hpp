#pragma once

#include "heavy_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pimpl_external_multiline {

class WidgetExternalMultiline {
public:
    WidgetExternalMultiline();
    ~WidgetExternalMultiline();
    WidgetExternalMultiline(const WidgetExternalMultiline&) = delete;
    WidgetExternalMultiline& operator=(const WidgetExternalMultiline&) = delete;

    void add_value(int value);
    int total() const;
    std::string label() const;

private:
    void update_total(
        int value,
        int bias = 0);

    friend struct WidgetExternalMultilineProbe;

    struct CounterTag {
        int value = 0;
    };

    using CounterMap = std::map<std::string, int>;

    std::string label_;
    std::vector<int> values_;
    CounterMap counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    heavy::Expander<160> expander_{};
    heavy::HeavySeq768 seq_{};
};

}  // namespace pimpl_external_multiline
