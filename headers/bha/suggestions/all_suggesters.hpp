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

    /**
     * @brief Register all builtin suggesters with the global suggester registry.
     *
     * Safe to call from multiple application entry points. Registration is
     * performed once per process so embedding the LSP manager in a CLI does not
     * execute every builtin suggester more than once.
     */
    inline void register_all_suggesters() {
        static const bool registered = [] {
            register_pch_suggester();
            register_forward_decl_suggester();
            register_include_suggester();
            register_template_suggester();
            register_header_split_suggester();
            register_unity_build_suggester();
            register_pimpl_pattern_suggester();
            return true;
        }();
        (void)registered;
    }

}  // namespace bha::suggestions

#endif //BHA_ALL_SUGGESTERS_HPP
