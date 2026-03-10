#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace unity_fixture {
    std::string compose_name(std::string_view prefix, std::int32_t value);
    std::vector<std::int32_t> make_data(std::int32_t seed);
}
