#pragma once

namespace fwd_decl {
class Beta;
}  // namespace fwd_decl

#include "alpha_fwd.hpp"
#include "alpha.hpp"
#include "heavy_types.hpp"

#include <string>

namespace fwd_decl {

class Beta {
public:
    Beta();
    void attach_owner(Alpha* owner);
    std::string name() const;

private:
    Alpha* owner_{};
    heavy::HeavyExpander expander_{};
    heavy::HeavySeq512 seq_a_{};
    heavy::HeavySeq768 seq_b_{};
};

}  // namespace fwd_decl
