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

template<typename Left, typename Right>
class Widgettemplatemultitype {
public:
    Widgettemplatemultitype();
    ~Widgettemplatemultitype();

    Widgettemplatemultitype(const Widgettemplatemultitype&) = delete;
    Widgettemplatemultitype& operator=(const Widgettemplatemultitype&) = delete;

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
    heavy::Expander<208> expander_{};
    heavy::HeavySeq1024 seq_{};
};
