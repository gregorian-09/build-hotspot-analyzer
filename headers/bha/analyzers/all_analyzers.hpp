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
#include "bha/analyzers/performance_analyzer.hpp"
#include "bha/analyzers/build_session_analyzer.hpp"
#include "bha/analyzers/build_target_analyzer.hpp"
#include "bha/analyzers/cache_analyzer.hpp"
#include "bha/analyzers/linker_analyzer.hpp"
#include "bha/analyzers/module_analyzer.hpp"

namespace bha::analyzers {

    /**
     * Registers all available analyzers with the global registry.
     */
    inline void register_all_analyzers() {
        register_file_analyzer();
        register_dependency_analyzer();
        register_template_analyzer();
        register_symbol_analyzer();
        register_performance_analyzer();
        register_build_session_analyzer();
        register_build_target_analyzer();
        register_cache_analyzer();
        register_linker_analyzer();
        register_module_analyzer();
    }

}  // namespace bha::analyzers

#endif //BHA_ALL_ANALYZERS_HPP
