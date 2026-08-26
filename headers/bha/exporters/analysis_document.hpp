#ifndef BHA_ANALYSIS_DOCUMENT_HPP
#define BHA_ANALYSIS_DOCUMENT_HPP

/**
 * @file analysis_document.hpp
 * @brief Canonical JSON representation of an analysis result.
 *
 * All renderers that need structured analysis data should consume this
 * document rather than maintaining a second field-by-field serializer.
 */

#include "bha/analyzers/analyzer.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace bha::exporters {

    struct AnalysisDocumentOptions {
        bool include_metadata = true;
        bool include_file_details = true;
        bool include_dependencies = true;
        bool include_templates = true;
        bool include_symbols = true;
        bool include_suggestions = false;
        std::size_t max_files = 0;
        std::size_t max_suggestions = 0;
        Duration min_compile_time = Duration::zero();
        std::string schema_version = "0.1.0";
    };

    /**
     * Build the canonical, lossless analysis document used by exporters and
     * structured CLI output. Filters only affect optional detail collections;
     * aggregate metrics and their provenance remain available.
     */
    [[nodiscard]] nlohmann::json make_analysis_document(
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const AnalysisDocumentOptions& options = {}
    );

} // namespace bha::exporters

#endif // BHA_ANALYSIS_DOCUMENT_HPP
