#include "beta.hpp"

namespace fwd_decl {

Beta::Beta() = default;

void Beta::attach_owner(Alpha* owner) {
    owner_ = owner;
}

std::string Beta::name() const {
    return "beta";
}

}  // namespace fwd_decl
