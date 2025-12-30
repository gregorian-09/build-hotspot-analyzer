//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/pch_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace bha::analyzers
{
    namespace {

        /**
         * Checks if a path looks like a system header.
         *
         * System headers are typically good PCH candidates because:
         * 1. They rarely change (stable)
         * 2. They're often included by many files
         * 3. The STL headers are particularly expensive to parse
         */
        bool is_system_header(const fs::path& path) {
            if (const std::string path_str = path.string(); path_str.find("/usr/include") != std::string::npos ||
                path_str.find("/usr/local/include") != std::string::npos ||
                path_str.find("/opt/") == 0 ||
                path_str.find("C:\\Program Files") == 0 ||
                path_str.find('<') == 0) {
                return true;
                }

            const std::string filename = path.filename().string();
            const std::string stem = path.stem().string();

            // C++ standard library headers (no extension)
            static const std::unordered_set<std::string> std_headers = {
                "algorithm", "any", "array", "atomic", "bitset", "cassert", "cctype",
                "cerrno", "cfenv", "cfloat", "charconv", "chrono", "cinttypes",
                "climits", "clocale", "cmath", "codecvt", "compare", "complex",
                "concepts", "condition_variable", "coroutine", "csetjmp", "csignal",
                "cstdarg", "cstddef", "cstdint", "cstdio", "cstdlib", "cstring",
                "ctime", "cuchar", "cwchar", "cwctype", "deque", "exception",
                "execution", "filesystem", "format", "forward_list", "fstream",
                "functional", "future", "initializer_list", "iomanip", "ios",
                "iosfwd", "iostream", "istream", "iterator", "latch", "limits",
                "list", "locale", "map", "memory", "memory_resource", "mutex",
                "new", "numbers", "numeric", "optional", "ostream", "queue",
                "random", "ranges", "ratio", "regex", "scoped_allocator", "semaphore",
                "set", "shared_mutex", "source_location", "span", "sstream", "stack",
                "stdexcept", "stop_token", "streambuf", "string", "string_view",
                "syncstream", "system_error", "thread", "tuple", "type_traits",
                "typeindex", "typeinfo", "unordered_map", "unordered_set", "utility",
                "valarray", "variant", "vector", "version"
            };

            return std_headers.contains(stem) || std_headers.contains(filename);
        }

        /**
         * Determines header stability based on naming patterns.
         *
         * Stable headers are better PCH candidates because:
         * - Less risk of invalidating the PCH frequently
         * - Forward declaration headers typically have few dependencies
         * - Config/version headers rarely change
         */
        bool is_stable_header(const fs::path& path) {
            if (is_system_header(path)) {
                return true;
            }

            const std::string filename = path.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (const char c : filename) {
                lower_filename += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            // Forward declaration headers are very stable
            if (lower_filename.find("_fwd") != std::string::npos ||
                lower_filename.find("fwd_") != std::string::npos ||
                lower_filename.find("forward") != std::string::npos) {
                return true;
                }

            // Type definition headers
            if (lower_filename.find("_types") != std::string::npos ||
                lower_filename.find("types_") != std::string::npos ||
                lower_filename.find("_defs") != std::string::npos) {
                return true;
                }

            // Configuration and version headers
            if (lower_filename.find("config") != std::string::npos ||
                lower_filename.find("version") != std::string::npos ||
                lower_filename.find("platform") != std::string::npos) {
                return true;
                }

            // Existing PCH headers
            if (lower_filename.find("stdafx") != std::string::npos ||
                lower_filename.find("pch") != std::string::npos ||
                lower_filename.find("precompile") != std::string::npos) {
                return true;
                }

            return false;
        }

        /**
         * Calculates PCH score using a multi-factor model.
         *
         * Based on research from ClangBuildAnalyzer and industry practices:
         *
         * 1. Time Impact Score: Total accumulated parse time is the primary metric.
         *    Headers that consume the most parse time provide the most savings.
         *
         * 2. Coverage Score: How many files benefit from precompiling this header.
         *    Uses logarithmic scaling because marginal benefit decreases.
         *
         * 3. Efficiency Score: Parse time per inclusion. Headers that take long
         *    to parse each time (like STL containers) are better candidates.
         *
         * 4. Stability Multiplier: Stable headers get a bonus because:
         *    - PCH invalidation is costly
         *    - Unstable headers require frequent PCH rebuilds
         *
         * The formula is:
         *   score = (time_impact * 0.5 + coverage * 0.25 + efficiency * 0.25) * stability
         */
        double calculate_pch_score(
            const Duration total_parse_time,
            const std::size_t inclusion_count,
            const std::size_t including_files,
            const bool is_stable,
            const Duration avg_parse_time_per_inclusion
        ) {
            const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                total_parse_time).count();
            const auto avg_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                avg_parse_time_per_inclusion).count();

            // Time impact: normalized total parse time
            // Using log scaling to prevent extreme values from dominating
            double time_impact = 0.0;
            if (total_ms > 0) {
                time_impact = std::log(static_cast<double>(total_ms) + 1.0);
            }

            // Coverage: how many files benefit
            // Log scaling because going from 10->20 files is less impactful than 2->10
            double coverage = 0.0;
            if (including_files > 0) {
                coverage = std::log(static_cast<double>(including_files) + 1.0);
            }

            // Efficiency: how expensive is each parse
            // Headers that are slow to parse individually (like <vector>) benefit more
            double efficiency = 0.0;
            if (avg_ms > 0) {
                efficiency = std::log(static_cast<double>(avg_ms) + 1.0);
            }

            // Repetition factor: headers included multiple times per file benefit more
            double repetition_factor = 1.0;
            if (including_files > 0 && inclusion_count > including_files) {
                const double avg_inclusions_per_file =
                    static_cast<double>(inclusion_count) / static_cast<double>(including_files);
                repetition_factor = 1.0 + std::log(avg_inclusions_per_file);
            }

            const double stability_multiplier = is_stable ? 1.5 : 1.0;
            const double raw_score = (time_impact * 0.5 + coverage * 0.25 + efficiency * 0.25)
                               * repetition_factor;

            return raw_score * stability_multiplier;
        }

    }  // namespace

    Result<AnalysisResult, Error> PCHAnalyzer::analyze(
        const BuildTrace& trace,
        const AnalysisOptions& options
    ) const {
        auto pch_result = analyze_pch(trace, options);
        if (!pch_result.is_ok()) {
            return Result<AnalysisResult, Error>::failure(pch_result.error());
        }

        AnalysisResult result;
        for (const auto& candidate : pch_result.value().candidates) {
            DependencyAnalysisResult::HeaderInfo header_info;
            header_info.path = candidate.header;
            header_info.total_parse_time = candidate.total_parse_time;
            header_info.inclusion_count = candidate.inclusion_count;
            header_info.including_files = candidate.including_files;
            header_info.impact_score = candidate.pch_score;
            result.dependencies.headers.push_back(std::move(header_info));
        }

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    Result<PCHAnalysisResult, Error> PCHAnalyzer::analyze_pch(
        const BuildTrace& trace,
        const AnalysisOptions& options
    )
    {
        PCHAnalysisResult result;

        // Track headers: path -> (total parse time, inclusion count, including files)
        struct HeaderStats {
            Duration total_parse_time = Duration::zero();
            std::size_t inclusion_count = 0;
            std::unordered_set<std::string> including_files;
        };
        std::unordered_map<std::string, HeaderStats> header_map;

        // Collect header statistics from all compilation units
        for (const auto& unit : trace.units) {
            for (const auto& inc : unit.includes) {
                std::string header_path = inc.header.string();

                auto& [total_parse_time, inclusion_count, including_files] = header_map[header_path];
                total_parse_time += inc.parse_time;
                inclusion_count++;
                including_files.insert(unit.source_file.string());
            }
        }

        result.total_headers_analyzed = header_map.size();

        for (const auto& [header_path, stats] : header_map) {
            fs::path path(header_path);

            // Skip headers included only once or twice (not worth precompiling)
            if (stats.including_files.size() < 3) {
                continue;
            }

            // Skip headers with minimal parse time
            if (stats.total_parse_time < options.min_duration_threshold) {
                continue;
            }

            PCHAnalysisResult::PCHCandidate candidate;
            candidate.header = path;
            candidate.total_parse_time = stats.total_parse_time;
            candidate.inclusion_count = stats.inclusion_count;
            candidate.including_files = stats.including_files.size();

            Duration avg_parse_time = Duration::zero();
            if (stats.inclusion_count > 0) {
                avg_parse_time = stats.total_parse_time / stats.inclusion_count;
            }

            candidate.pch_score = calculate_pch_score(
                stats.total_parse_time,
                stats.inclusion_count,
                stats.including_files.size(),
                is_stable_header(path),
                avg_parse_time
            );

            // Estimate savings: if precompiled, parse time is nearly eliminated
            // But only for repeated inclusions (first compile still needs full parse)
            if (stats.inclusion_count > 1) {
                auto per_inclusion = stats.total_parse_time / stats.inclusion_count;
                candidate.estimated_savings = per_inclusion * (stats.inclusion_count - 1);
            }

            result.candidates.push_back(std::move(candidate));
            result.current_total_parse_time += stats.total_parse_time;
        }

        std::ranges::sort(result.candidates,
                          [](const auto& a, const auto& b) {
                              return a.pch_score > b.pch_score;
                          });

        for (const auto& candidate : result.candidates) {
            result.potential_savings += candidate.estimated_savings;
        }

        return Result<PCHAnalysisResult, Error>::success(std::move(result));
    }

    void register_pch_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<PCHAnalyzer>()
        );
    }
}  // namespace bha::analyzers