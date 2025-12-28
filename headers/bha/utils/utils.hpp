//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_UTILS_HPP
#define BUILDTIMEHOTSPOTANALYZER_UTILS_HPP

/**
 * @file utils.hpp
 * @brief Main utilities header.
 *
 * Includes all utility modules for convenience.
 */

#include "string_utils.hpp"
#include "file_utils.hpp"
#include "path_utils.hpp"
#include "parallel.hpp"

// json_utils.hpp is not included by default since it pulls in nlohmann/json
// Include it explicitly when needed:
// #include "bha/utils/json_utils.hpp"

#endif //BUILDTIMEHOTSPOTANALYZER_UTILS_HPP