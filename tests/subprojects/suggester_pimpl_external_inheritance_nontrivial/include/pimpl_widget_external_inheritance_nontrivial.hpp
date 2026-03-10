#pragma once

#include "heavy_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pimpl_external::inheritance_nontrivial {

struct BaseCounter {
    virtual ~BaseCounter() = default;
    virtual int total() const = 0;
};

struct BaseLabel {
    virtual ~BaseLabel() = default;
    virtual std::string label() const = 0;
};

class WidgetExternalInheritanceNontrivial final : public BaseCounter, public BaseLabel {
public:
    WidgetExternalInheritanceNontrivial();
    ~WidgetExternalInheritanceNontrivial();

    void add_value(int value);
    int total() const override;
    std::string label() const override;

private:
    std::string label_;
    std::vector<int> values_;
    std::map<std::string, int> counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    int total_ = 0;
    heavy::Expander<228> expander_{};
    heavy::HeavySeq1024 seq_{};
    heavy::HeavySeq1024 seq2_{};
};

}  // namespace pimpl_external::inheritance_nontrivial
