#pragma once

#include "heavy_types.hpp"

namespace fwd_decl {

struct Payload {
    int value{};
    heavy::HeavyExpander expander{};
};

}  // namespace fwd_decl
