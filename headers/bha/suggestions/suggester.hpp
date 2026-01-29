//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_SUGGESTER_HPP
#define BHA_SUGGESTER_HPP

/**
 * @file suggester.hpp
 * @brief Interface for suggestion generators.
 *
 * Suggesters analyze build traces and analysis results to produce
 * actionable optimization suggestions. Each suggester focuses on
 * a specific optimization strategy:
 *
 * - PCHSuggester: Identifies candidates for precompiled headers
 * - ForwardDeclSuggester: Finds opportunities for forward declarations
 * - IncludeSuggester: Detects removable or reducible includes
 * - TemplateSuggester: Suggests explicit instantiations
 *
 * All suggesters follow the Result<T,E> error handling pattern.
 */

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace bha::suggestions {

    /**
     * Context passed to suggesters containing all analysis data.
     */
    struct SuggestionContext {
        const BuildTrace& trace;
        const analyzers::AnalysisResult& analysis;
        const SuggesterOptions& options;

        /// Optional cancellation token. Suggesters should check this periodically
        /// in long-running loops and return early if canceled.
        std::atomic<bool>* cancelled = nullptr;

        /// Optional filter for incremental analysis. When set, only analyze
        /// files in this list. Empty means analyze all files.
        std::vector<fs::path> target_files{};

        /// Check if the operation has been canceled.
        [[nodiscard]] bool is_cancelled() const noexcept {
            return cancelled != nullptr && cancelled->load(std::memory_order_relaxed);
        }

        /// Check if a file should be analyzed (respects target_files filter).
        [[nodiscard]] bool should_analyze(const fs::path& file) const {
            if (target_files.empty()) {
                return true;
            }

            return std::ranges::any_of(
                target_files,
                [&](const fs::path& target) {
                    return file == target || file.filename() == target.filename();
                }
            );
        }
    };

    /**
     * Generates a unique suggestion ID from a prefix and file path.
     *
     * Uses hash of the full path to avoid collisions when multiple files
     * have the same filename in different directories.
     *
     * @param prefix Suggestion type prefix (e.g., "pch", "fwd", "unused")
     * @param path File path to include in the ID
     * @param suffix Optional additional suffix for disambiguation
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const fs::path& path,
        const std::string_view suffix = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-";

        const std::size_t path_hash = std::hash<std::string>{}(path.string());
        oss << std::hex << (path_hash & 0xFFFFFF);  // 24 bits (6 hex chars)

        // Readable filename for human identification
        oss << "-" << path.filename().string();

        if (!suffix.empty()) {
            oss << "-" << suffix;
        }

        return oss.str();
    }

    /**
     * Generates a unique suggestion ID from a prefix and counter.
     *
     * Use this variant when there's no natural file path (e.g., unity builds).
     *
     * @param prefix Suggestion type prefix
     * @param counter Unique counter value
     * @param name Optional descriptive name
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const std::size_t counter,
        const std::string_view name = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-" << counter;
        if (!name.empty()) {
            oss << "-" << name;
        }
        return oss.str();
    }

    /**
     * Result of suggestion generation.
     */
    struct SuggestionResult {
        std::vector<Suggestion> suggestions{};
        Duration generation_time = Duration::zero();
        std::size_t items_analyzed = 0;
        std::size_t items_skipped = 0;
    };

    /**
     * Interface for suggestion generators.
     *
     * Each suggester produces a specific type of optimization suggestion.
     * Suggesters are stateless and thread-safe for concurrent use.
     */
    class ISuggester {
    public:
        virtual ~ISuggester() = default;

        /**
         * Returns the suggester's unique identifier.
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * Returns a human-readable description.
         */
        [[nodiscard]] virtual std::string_view description() const noexcept = 0;

        /**
         * Returns the type of suggestions this suggester produces.
         */
        [[nodiscard]] virtual SuggestionType suggestion_type() const noexcept = 0;

        /**
         * Generates suggestions from the analysis context.
         *
         * @param context The analysis context with trace and results
         * @return Suggestions or an error
         */
        [[nodiscard]] virtual Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const = 0;
    };

    /**
     * Registry for all available suggesters.
     */
    class SuggesterRegistry {
    public:
        static SuggesterRegistry& instance();

        void register_suggester(std::unique_ptr<ISuggester> suggester);

        [[nodiscard]] const std::vector<std::unique_ptr<ISuggester>>& suggesters() const noexcept {
            return suggesters_;
        }

        [[nodiscard]] const ISuggester* find(std::string_view name) const;

    private:
        SuggesterRegistry() = default;
        std::vector<std::unique_ptr<ISuggester>> suggesters_;
    };

    /**
     * Runs all registered suggesters and collects results.
     *
     * @param trace The build trace data
     * @param analysis The analysis results
     * @param options Suggester configuration
     * @return All suggestions sorted by priority and impact
     */
    [[nodiscard]] Result<std::vector<Suggestion>, Error> generate_all_suggestions(
        const BuildTrace& trace,
        const analyzers::AnalysisResult& analysis,
        const SuggesterOptions& options
    );

}  // namespace bha::suggestions

#endif //BHA_SUGGESTER_HPP