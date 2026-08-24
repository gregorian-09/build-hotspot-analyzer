//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/symbol_analyzer.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers {
    namespace {

        std::string path_key(const fs::path& path) {
            return path.lexically_normal().string();
        }

    }  // namespace

    Result<AnalysisResult, Error> SymbolAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& /*options*/
    ) const {
        AnalysisResult result;

        struct SymbolData {
            std::vector<fs::path> defined_in;
            std::unordered_set<std::string> defined_in_set;
            std::vector<fs::path> used_in;
            std::unordered_set<std::string> used_in_set;
            std::size_t usage_count = 0;
        };
        std::unordered_map<std::string, SymbolData> symbol_map;

        for (const auto& unit : trace.units) {
            const std::string source_key = path_key(unit.source_file);

            // A symbol name is retained exactly as supplied by the producer.
            // Its type, linkage, ODR status, and code-size impact require AST or
            // object-file evidence and are intentionally not inferred here.
            for (const auto& symbol : unit.symbols_defined) {
                if (symbol.empty()) {
                    continue;
                }

                auto& data = symbol_map[symbol];
                if (data.defined_in_set.insert(source_key).second) {
                    data.defined_in.push_back(unit.source_file);
                }
            }

            // An include proves only that a header was read. A use is accepted
            // only when the producer provides the exact referenced symbol name.
            for (const auto& include : unit.includes) {
                for (const auto& symbol : include.symbols_used) {
                    if (symbol.empty()) {
                        continue;
                    }

                    auto& data = symbol_map[symbol];
                    if (data.used_in_set.insert(source_key).second) {
                        data.used_in.push_back(unit.source_file);
                        ++data.usage_count;
                    }
                }
            }
        }

        std::size_t unused_symbols = 0;
        result.symbols.symbols.reserve(symbol_map.size());
        for (const auto& [symbol_name, data] : symbol_map) {
            SymbolAnalysisResult::SymbolInfo info;
            info.name = symbol_name;
            info.defined_in = data.defined_in.empty() ? fs::path() : data.defined_in.front();
            info.used_in = data.used_in;
            info.usage_count = data.usage_count;
            result.symbols.symbols.push_back(std::move(info));

            if (!data.defined_in.empty() && data.usage_count == 0) {
                ++unused_symbols;
            }
        }

        result.symbols.total_symbols = result.symbols.symbols.size();
        result.symbols.unused_symbols = unused_symbols;

        std::ranges::sort(
            result.symbols.symbols,
            [](const auto& left, const auto& right) {
                if (left.usage_count != right.usage_count) {
                    return left.usage_count > right.usage_count;
                }
                return left.name < right.name;
            }
        );

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_symbol_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<SymbolAnalyzer>()
        );
    }
}  // namespace bha::analyzers
