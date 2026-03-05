#pragma once

#include "heavy_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pimpl_external::copyable {

class WidgetExternalCopyable {
public:
    WidgetExternalCopyable();
    ~WidgetExternalCopyable();

    void add_value(int value);
    void set_label(std::string label);
    int total() const;
    std::string label() const;

private:
    int bias() const;
    void update_total(int value);

    std::string label_;
    std::vector<int> values_;
    std::map<std::string, int> counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    heavy::Expander<220> expander_{};
    heavy::HeavySeq960 seq_{};
    heavy::HeavySeq960 seq2_{};
};

}  // namespace pimpl_external::copyable
