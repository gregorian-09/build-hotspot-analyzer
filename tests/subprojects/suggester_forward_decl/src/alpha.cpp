#include "alpha.hpp"
#include "payload.hpp"

namespace fwd_decl {

Alpha::Alpha() = default;

void Alpha::set_beta(Beta* beta) {
    beta_ = beta;
}

void Alpha::replace_beta(Beta* beta) {
    beta_ = beta;
}

std::string Alpha::describe(const Beta&) const {
    return "beta";
}

Payload* preserve_payload(Payload* payload) {
    return payload;
}

}  // namespace fwd_decl
