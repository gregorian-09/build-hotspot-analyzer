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

#define DECLARE_PRIVATE_FIELD(...) __VA_ARGS__;
#define DECLARE_TRACKING_FIELD(...) __VA_ARGS__;

namespace pimpl_external::macro_decl {

class WidgetExternalMacroDecl {
public:
    WidgetExternalMacroDecl();
    ~WidgetExternalMacroDecl();
    WidgetExternalMacroDecl(const WidgetExternalMacroDecl&) = delete;
    WidgetExternalMacroDecl& operator=(const WidgetExternalMacroDecl&) = delete;

    void add_value(int value);
    int total() const;
    std::string label() const;

private:
    DECLARE_PRIVATE_FIELD(std::string label_)
    DECLARE_PRIVATE_FIELD(std::vector<int> values_)
    DECLARE_PRIVATE_FIELD(std::deque<int> queue_)
    DECLARE_PRIVATE_FIELD(std::list<int> list_)
    DECLARE_PRIVATE_FIELD(std::map<std::string, int> counters_)
    DECLARE_PRIVATE_FIELD(std::unordered_map<std::string, int> fast_lookup_)
    DECLARE_TRACKING_FIELD(std::set<int> seen_)
    DECLARE_TRACKING_FIELD(std::optional<std::regex> matcher_)
    DECLARE_TRACKING_FIELD(std::variant<int, std::string> payload_)
    DECLARE_TRACKING_FIELD(std::tuple<int, int, int> triple_)
    DECLARE_TRACKING_FIELD(std::filesystem::path cache_path_)
    DECLARE_PRIVATE_FIELD(heavy::Expander<224> expander_)
    DECLARE_PRIVATE_FIELD(heavy::HeavySeq2048 seq_)
};

}  // namespace pimpl_external::macro_decl
