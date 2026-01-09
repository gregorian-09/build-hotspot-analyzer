//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_ALL_SUGGESTERS_HPP
#define BHA_ALL_SUGGESTERS_HPP

/**
 * @file all_suggesters.hpp
 * @brief Convenience header for registering all suggesters.
 */

#include "unity_build_suggester.hpp"
#include "pch_suggester.hpp"
#include "forward_decl_suggester.hpp"
#include "include_suggester.hpp"
#include "template_suggester.hpp"
#include "header_split_suggester.hpp"
#include "pimpl_suggester.hpp"

namespace bha::suggestions {

    inline void register_all_suggesters() {
        register_pch_suggester();
        register_forward_decl_suggester();
        register_include_suggester();
        register_template_suggester();
        register_header_split_suggester();
        register_unity_build_suggester();
        register_pimpl_pattern_suggester();
    }

}  // namespace bha::suggestions

#endif //BHA_ALL_SUGGESTERS_HPP