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

namespace pimpl_external::inline_private {

class WidgetExternalInlinePrivate {
public:
    WidgetExternalInlinePrivate();
    ~WidgetExternalInlinePrivate();
    WidgetExternalInlinePrivate(const WidgetExternalInlinePrivate&) = delete;
    WidgetExternalInlinePrivate& operator=(const WidgetExternalInlinePrivate&) = delete;

    void add_value(int value);
    int total() const;
    std::string label() const;

private:
    void update_total(int value) {
        counters_["total"] += value;
        fast_lookup_["total"] = counters_["total"];
        queue_.push_back(value);
        list_.push_back(value);
        seen_.insert(value);
        matcher_.emplace("[0-9]+");
        payload_ = std::to_string(value);
        triple_ = std::make_tuple(value, value + 1, value + 2);
        cache_path_ /= std::to_string(value);
    }

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
    heavy::Expander<224> expander_{};
    heavy::HeavySeq2048 seq_{};
};

}  // namespace pimpl_external::inline_private
