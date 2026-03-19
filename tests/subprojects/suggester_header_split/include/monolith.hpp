#pragma once

#include "heavy_meta.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "monolith_fwd.hpp"
#include "monolith_fwd.hpp"

namespace splitproj {

struct BuildSettings {
    std::array<std::uint64_t, 24> counters{};
    std::vector<int> values{};
    split_heavy::HeavyNode node{};
    split_heavy::HeavySeq seq{};
};

class CompileUnit {
public:
    CompileUnit();
    explicit CompileUnit(std::string name);

    [[nodiscard]] int weight() const;

private:
    BuildSettings settings_{};
    std::string name_{};
};

inline CompileUnit::CompileUnit() = default;

inline CompileUnit::CompileUnit(std::string name)
    : name_(std::move(name)) {}

inline int CompileUnit::weight() const {
    return static_cast<int>(settings_.values.size()) +
           split_heavy::Fib<20>::value +
           static_cast<int>(name_.size());
}

#define SPLIT_RECORD(ID) \
struct Record##ID { \
    std::array<int, 16> data{}; \
    std::tuple<int, double, std::string> meta{}; \
    split_heavy::Seq<(ID + 192)>::type seq{}; \
    int score() const { return split_heavy::Fib<12>::value + data[0]; } \
};

SPLIT_RECORD(0)
SPLIT_RECORD(1)
SPLIT_RECORD(2)
SPLIT_RECORD(3)
SPLIT_RECORD(4)
SPLIT_RECORD(5)
SPLIT_RECORD(6)
SPLIT_RECORD(7)
SPLIT_RECORD(8)
SPLIT_RECORD(9)
SPLIT_RECORD(10)
SPLIT_RECORD(11)
SPLIT_RECORD(12)
SPLIT_RECORD(13)
SPLIT_RECORD(14)
SPLIT_RECORD(15)
SPLIT_RECORD(16)
SPLIT_RECORD(17)
SPLIT_RECORD(18)
SPLIT_RECORD(19)
SPLIT_RECORD(20)
SPLIT_RECORD(21)
SPLIT_RECORD(22)
SPLIT_RECORD(23)
SPLIT_RECORD(24)
SPLIT_RECORD(25)
SPLIT_RECORD(26)
SPLIT_RECORD(27)
SPLIT_RECORD(28)
SPLIT_RECORD(29)
SPLIT_RECORD(30)
SPLIT_RECORD(31)
SPLIT_RECORD(32)
SPLIT_RECORD(33)
SPLIT_RECORD(34)
SPLIT_RECORD(35)
SPLIT_RECORD(36)
SPLIT_RECORD(37)
SPLIT_RECORD(38)
SPLIT_RECORD(39)

#undef SPLIT_RECORD

}  // namespace splitproj
