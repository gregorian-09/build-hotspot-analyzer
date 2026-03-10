#include "wrap_a.hpp"
#include "wrap_b.hpp"
#include "wrap_c.hpp"

int include_removal_test_symbol_10() {
    return wrap_a_value() + wrap_b_value() + wrap_c_value();
}
