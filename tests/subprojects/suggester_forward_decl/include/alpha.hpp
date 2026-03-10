#pragma once

#include "beta.hpp"

#include <string>

namespace fwd_decl {

class Alpha {
public:
    Alpha();
    void set_beta(Beta* beta);
    void replace_beta(Beta* beta);
    std::string describe(const Beta& beta) const;

private:
    Beta* beta_{};
};

}  // namespace fwd_decl
