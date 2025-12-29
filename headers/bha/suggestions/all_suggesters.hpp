//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_ALL_SUGGESTERS_HPP
#define BHA_ALL_SUGGESTERS_HPP

/**
 * @file all_suggesters.hpp
 * @brief Convenience header for registering all suggesters.
 */

#include "bha/suggestions/pch_suggester.hpp"
#include "bha/suggestions/forward_decl_suggester.hpp"
#include "bha/suggestions/include_suggester.hpp"
#include "bha/suggestions/template_suggester.hpp"
#include "bha/suggestions/header_split_suggester.hpp"
#include "bha/suggestions/pimpl_suggester.hpp"

namespace bha::suggestions {

    inline void register_all_suggesters() {
        register_pch_suggester();
        register_forward_decl_suggester();
        register_include_suggester();
        register_template_suggester();
        register_header_split_suggester();
        register_pimpl_pattern_suggester();
    }

}  // namespace bha::suggestions

#endif //BHA_ALL_SUGGESTERS_HPP