#include "alpha.hpp"

namespace fwd_decl {

Alpha::Alpha() = default;

void Alpha::set_beta(Beta* beta) {
    beta_ = beta;
}

void Alpha::replace_beta(Beta* beta) {
    beta_ = beta;
}

std::string Alpha::describe(const Beta& beta) const {
    return beta.name();
}

}  // namespace fwd_decl
