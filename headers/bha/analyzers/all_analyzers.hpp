//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_ALL_ANALYZERS_HPP
#define BHA_ALL_ANALYZERS_HPP

/**
 * @file all_analyzers.hpp
 * @brief Includes and registers all available analyzers.
 */

#include "bha/analyzers/file_analyzer.hpp"
#include "bha/analyzers/dependency_analyzer.hpp"
#include "bha/analyzers/template_analyzer.hpp"
#include "bha/analyzers/symbol_analyzer.hpp"
#include "bha/analyzers/pch_analyzer.hpp"
#include "bha/analyzers/performance_analyzer.hpp"

namespace bha::analyzers {

    /**
     * Registers all available analyzers with the global registry.
     */
    inline void register_all_analyzers() {
        register_file_analyzer();
        register_dependency_analyzer();
        register_template_analyzer();
        register_symbol_analyzer();
        register_pch_analyzer();
        register_performance_analyzer();
    }

}  // namespace bha::analyzers

#endif //BHA_ALL_ANALYZERS_HPP