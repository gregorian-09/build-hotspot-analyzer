//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/symbol_analyzer.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers
{
    namespace {

        /**
         * Symbol visibility/linkage types.
         *
         * Based on C++ linkage rules:
         * - Internal: static, anonymous namespace (one definition per TU)
         * - External: default linkage (one definition across all TUs, ODR)
         * - Inline: inline/constexpr (can appear in multiple TUs if identical)
         * - Template: template definitions (instantiated per TU)
         */
        enum class SymbolLinkage {
            External,
            Internal,
            Inline,
            Template
        };

        /**
         * Classifies a symbol's type based on its name and context.
         *
         * Uses pattern matching to identify:
         * - Classes/structs (type definitions)
         * - Methods (member functions with ::)
         * - Functions (standalone callable entities)
         * - Templates (entities with < > type parameters)
         * - Variables (everything else)
         */
        std::string classify_symbol_type(const std::string& symbol) {
            if (symbol.find('<') != std::string::npos &&
                symbol.find('>') != std::string::npos) {
                if (symbol.find('(') != std::string::npos) {
                    return "template_function";
                }
                return "template_class";
                }

            if (symbol.find("class ") == 0 || symbol.find("struct ") == 0 ||
                symbol.find("enum ") == 0 || symbol.find("union ") == 0) {
                return "type";
                }

            if (symbol.find("::") != std::string::npos) {
                if (symbol.find('(') != std::string::npos) {
                    std::string lower_symbol;
                    lower_symbol.reserve(symbol.size());
                    for (const char c : symbol) {
                        lower_symbol += static_cast<char>(
                            std::tolower(static_cast<unsigned char>(c)));
                    }

                    if (lower_symbol.find("operator") != std::string::npos) {
                        return "operator";
                    }
                    if (symbol.find("::~") != std::string::npos) {
                        return "destructor";
                    }

                    if (const auto last_colon = symbol.rfind("::"); last_colon != std::string::npos) {
                        if (const auto paren = symbol.find('(', last_colon); paren != std::string::npos) {
                            const std::string method_name = symbol.substr(last_colon + 2, paren - last_colon - 2);
                            // Find class name before ::
                            const auto prev_colon = symbol.rfind("::", last_colon - 1);
                            const std::size_t class_start = (prev_colon != std::string::npos) ? prev_colon + 2 : 0;
                            if (const std::string class_name = symbol.substr(class_start, last_colon - class_start); class_name == method_name) {
                                return "constructor";
                            }
                        }
                    }
                    return "method";
                }
                // Member variable or nested type
                return "member";
            }

            // Check for standalone function
            if (symbol.find('(') != std::string::npos) {
                return "function";
            }

            // Check for macro-like patterns (ALL_CAPS)
            bool is_all_caps = true;
            bool has_letter = false;
            for (const char c : symbol) {
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    has_letter = true;
                    if (!std::isupper(static_cast<unsigned char>(c))) {
                        is_all_caps = false;
                        break;
                    }
                }
            }
            if (is_all_caps && has_letter && symbol.length() > 2) {
                return "macro_or_constant";
            }

            return "variable";
        }

        /**
         * Infers symbol linkage from naming patterns and context.
         *
         * This is heuristic-based since we don't have full AST information:
         * - Symbols starting with _ in global scope are often internal
         * - Anonymous namespace patterns indicate internal linkage
         * - inline/constexpr patterns indicate inline linkage
         * - Template patterns indicate template linkage
         */
        SymbolLinkage infer_linkage(const std::string& symbol, const std::string& type) {
            if (type == "template_function" || type == "template_class") {
                return SymbolLinkage::Template;
            }

            if (symbol.find("inline ") != std::string::npos ||
                symbol.find("constexpr ") != std::string::npos) {
                return SymbolLinkage::Inline;
                }

            if (symbol.find("static ") == 0 ||
                symbol.find("(anonymous namespace)") != std::string::npos ||
                symbol.find("::(anonymous)::") != std::string::npos) {
                return SymbolLinkage::Internal;
                }

            if (!symbol.empty() && symbol[0] == '_' &&
                symbol.find("::") == std::string::npos) {
                return SymbolLinkage::Internal;
                }

            return SymbolLinkage::External;
        }

        /**
         * Detects potential ODR (One Definition Rule) violations.
         *
         * ODR violations occur when:
         * - The same external symbol is defined in multiple translation units
         * - Inline/template symbols have different definitions across TUs
         *
         * Returns true if this looks like an ODR violation.
         */
        bool detect_odr_violation(
            const SymbolLinkage linkage,
            const std::vector<fs::path>& definition_files
        ) {
            if (definition_files.size() <= 1) {
                return false;
            }

            // External symbols should only be defined once
            if (linkage == SymbolLinkage::External) {
                return true;
            }

            // Internal linkage is allowed in multiple TUs
            if (linkage == SymbolLinkage::Internal) {
                return false;
            }

            // Inline/template linkage: multiple definitions are OK if identical
            // We can't verify identity, but flag if in many different directories
            // (same header included = OK, different implementations = bad)
            if (linkage == SymbolLinkage::Inline || linkage == SymbolLinkage::Template) {
                std::unordered_set<std::string> parent_dirs;
                for (const auto& file : definition_files) {
                    parent_dirs.insert(file.parent_path().string());
                }
                // If definitions come from many different directories, flag it
                return parent_dirs.size() > 3;
            }

            return false;
        }

        /**
         * Calculates a "bloat score" for inline/template symbols.
         *
         * Inline and template code is duplicated in each translation unit,
         * contributing to code bloat. The bloat score estimates the impact:
         *
         * bloat_score = instantiation_count * estimated_code_size
         *
         * Higher scores indicate symbols that might benefit from:
         * - Explicit template instantiation
         * - Moving inline code to .cpp files
         * - Using the PIMPL idiom
         */
        double calculate_bloat_score(
            const std::string& type,
            const SymbolLinkage linkage,
            const std::size_t instantiation_count,
            const Duration total_instantiation_time
        ) {
            if (linkage != SymbolLinkage::Inline && linkage != SymbolLinkage::Template) {
                return 0.0;
            }

            const auto count_factor = static_cast<double>(instantiation_count);

            // Time factor: more time = more code generated
            const auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                total_instantiation_time).count();
            const double time_factor = static_cast<double>(time_ms) / 100.0;

            // Type multiplier: templates typically cause more bloat
            double type_multiplier = 1.0;
            if (type == "template_class") {
                type_multiplier = 2.0;
            } else if (type == "template_function") {
                type_multiplier = 1.5;
            }

            return count_factor * (1.0 + time_factor) * type_multiplier;
        }

    }  // namespace

    Result<AnalysisResult, Error> SymbolAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        AnalysisResult result;

        struct SymbolData {
            std::vector<fs::path> defined_in;  // Can be defined in multiple files
            std::vector<fs::path> used_in;
            std::size_t usage_count = 0;
            Duration total_time = Duration::zero();
            std::string type;
            SymbolLinkage linkage = SymbolLinkage::External;
            bool potential_odr_violation = false;
            double bloat_score = 0.0;
        };
        std::unordered_map<std::string, SymbolData> symbol_map;

        // First pass: collect symbol definitions and their properties
        for (const auto& unit : trace.units) {
            for (const auto& symbol : unit.symbols_defined) {
                if (symbol.empty()) continue;

                auto& data = symbol_map[symbol];
                data.defined_in.push_back(unit.source_file);

                if (data.type.empty()) {
                    data.type = classify_symbol_type(symbol);
                    data.linkage = infer_linkage(symbol, data.type);
                }
            }

            // Templates: track instantiations with timing
            for (const auto& tmpl : unit.templates) {
                std::string symbol = tmpl.name;
                auto& data = symbol_map[symbol];

                if (data.type.empty()) {
                    data.type = classify_symbol_type(symbol);
                    data.linkage = SymbolLinkage::Template;
                }

                // Each instantiation is both a definition and a use
                if (std::ranges::find(data.defined_in,
                                      unit.source_file) == data.defined_in.end()) {
                    data.defined_in.push_back(unit.source_file);
                              }

                data.used_in.push_back(unit.source_file);
                data.usage_count += tmpl.count;
                data.total_time += tmpl.time;
            }
        }

        // Second pass: track symbol usage through includes
        for (const auto& unit : trace.units) {
            for (const auto& inc : unit.includes) {
                // Find symbols defined in the included file
                for (auto& data : symbol_map | std::views::values) {
                    for (const auto& def_file : data.defined_in) {
                        if (def_file == inc.header) {
                            // This file uses symbols from the included file
                            if (std::ranges::find(data.used_in,
                                                  unit.source_file) == data.used_in.end()) {
                                data.used_in.push_back(unit.source_file);
                                data.usage_count++;
                                          }
                            break;
                        }
                    }
                }
            }
        }

        // Third pass: analyze for ODR violations and bloat
        for (auto& data : symbol_map | std::views::values) {
            data.potential_odr_violation = detect_odr_violation(
                data.linkage, data.defined_in);

            data.bloat_score = calculate_bloat_score(
                data.type, data.linkage, data.usage_count, data.total_time);
        }

        std::size_t total_symbols = 0;
        std::size_t unused_symbols = 0;

        for (const auto& [symbol_name, data] : symbol_map) {
            SymbolAnalysisResult::SymbolInfo info;
            info.name = symbol_name;
            info.type = data.type;
            info.defined_in = data.defined_in.empty() ? fs::path() : data.defined_in[0];
            info.used_in = data.used_in;
            info.usage_count = data.usage_count;

            result.symbols.symbols.push_back(std::move(info));
            ++total_symbols;

            if (data.usage_count == 0) {
                ++unused_symbols;
            }
        }

        result.symbols.total_symbols = total_symbols;
        result.symbols.unused_symbols = unused_symbols;

        std::ranges::sort(result.symbols.symbols,
                          [](const auto& a, const auto& b) {
                              return a.usage_count > b.usage_count;
                          });

        (void)options;

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_symbol_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<SymbolAnalyzer>()
        );
    }
}  // namespace bha::analyzers