#pragma once

#include "heavy_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pimpl_external::explicit_copy {

class WidgetExternalExplicitCopy {
public:
    WidgetExternalExplicitCopy();
    ~WidgetExternalExplicitCopy();
    WidgetExternalExplicitCopy(const WidgetExternalExplicitCopy&);
    WidgetExternalExplicitCopy& operator=(const WidgetExternalExplicitCopy&);

    void add_value(int value);
    int total() const;
    std::string label() const;

private:
    void update_total(int value);

    std::string label_;
    std::vector<int> values_;
    std::map<std::string, int> counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    heavy::Expander<72> expander_{};
    heavy::HeavySeq256 seq_{};
};

}  // namespace pimpl_external::explicit_copy
