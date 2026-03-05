#pragma once

#include "heavy_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pimpl_external::inheritance {

struct BaseCounter {
    virtual ~BaseCounter() = default;
    virtual int total() const = 0;
};

class WidgetExternalInheritance final : public BaseCounter {
public:
    WidgetExternalInheritance();
    ~WidgetExternalInheritance();

    void add_value(int value);
    int total() const override;
    std::string label() const;

private:
    std::string label_;
    std::vector<int> values_;
    std::map<std::string, int> counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    int total_ = 0;
    heavy::Expander<220> expander_{};
    heavy::HeavySeq960 seq_{};
    heavy::HeavySeq960 seq2_{};
};

}  // namespace pimpl_external::inheritance
