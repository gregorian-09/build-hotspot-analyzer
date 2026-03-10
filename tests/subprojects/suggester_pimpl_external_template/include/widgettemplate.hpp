#pragma once

#include "heavy_types.hpp"

#include <deque>
#include <filesystem>
#include <list>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace pimpl_external::templated {

template<typename Tag>
class Widgettemplate {
public:
    Widgettemplate();
    ~Widgettemplate();

    void add_value(int value);
    int total() const;
    std::string label() const;

private:
    std::string label_;
    std::vector<int> values_;
    std::deque<int> queue_;
    std::list<int> list_;
    std::map<std::string, int> counters_;
    std::unordered_map<std::string, int> fast_lookup_;
    std::set<int> seen_;
    std::optional<std::regex> matcher_;
    std::variant<int, std::string> payload_;
    std::tuple<int, int, int> triple_{};
    std::filesystem::path cache_path_;
    int total_ = 0;
    heavy::Expander<224> expander_{};
    heavy::HeavySeq2048 seq_{};
};

}  // namespace pimpl_external::templated
