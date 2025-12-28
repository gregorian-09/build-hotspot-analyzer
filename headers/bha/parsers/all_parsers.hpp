//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BHA_ALL_PARSERS_HPP
#define BHA_ALL_PARSERS_HPP

/**
 * @file all_parsers.hpp
 * @brief Includes and registers all available trace parsers.
 *
 * Include this header and call register_all_parsers() to make
 * all supported compiler trace parsers available.
 */

#include "bha/parsers/clang_parser.hpp"
#include "bha/parsers/gcc_parser.hpp"
#include "bha/parsers/msvc_parser.hpp"
#include "bha/parsers/intel_parser.hpp"
#include "bha/parsers/nvcc_parser.hpp"

namespace bha::parsers {

    /**
     * Registers all available trace parsers with the global registry.
     *
     * Call this once during application initialization to enable
     * auto-detection and parsing of all supported trace formats.
     */
    inline void register_all_parsers() {
        register_clang_parser();
        register_gcc_parser();
        register_msvc_parser();
        register_intel_parsers();
        register_nvcc_parser();
    }

}  // namespace bha::parsers

#endif //BHA_ALL_PARSERS_HPP