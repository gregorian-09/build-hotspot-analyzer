#pragma once

#include "heavy_types.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace pimpl {

class Widget {
public:
    Widget();
    ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    void add_value(int value);
    int total() const;
    std::string label() const;

private:
    std::string label_;
    std::vector<int> values_;
    std::map<std::string, int> counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    heavy::Expander<160> expander_{};
    heavy::Expander<220> expander2_{};
    heavy::HeavySeq900 seq_{};
    heavy::MakeSeq<768>::type seq2_{};
};

}  // namespace pimpl
