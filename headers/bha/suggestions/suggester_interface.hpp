#ifndef BHA_SUGGESTER_INTERFACE_HPP
#define BHA_SUGGESTER_INTERFACE_HPP

#include "bha/suggestions/suggester_policy.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bha::suggestions {

    struct SuggestionContext;

    /**
     * Result of suggestion generation.
     */
    struct SuggestionResult {
        std::vector<Suggestion> suggestions;
        struct Diagnostic {
            std::string code;
            std::string message;
        };
        std::vector<Diagnostic> diagnostics;
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
         * Returns all suggestion types this suggester can emit.
         *
         * By default suggesters emit a single type matching suggestion_type().
         */
        [[nodiscard]] virtual std::vector<SuggestionType> supported_types() const {
            return {suggestion_type()};
        }

        /**
         * Returns language and ABI policy metadata for this suggester.
         */
        [[nodiscard]] virtual SuggesterPolicy policy() const noexcept {
            return default_suggester_policy(suggestion_type());
        }

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
        const SuggesterOptions& options,
        const fs::path& project_root = {}
    );
}  // namespace bha::suggestions

#endif  // BHA_SUGGESTER_INTERFACE_HPP
