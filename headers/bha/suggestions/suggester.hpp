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
#include "bha/project_index.hpp"
#include "bha/suggestions/forward_decl_semantic_index.hpp"
#include "bha/suggestions/suggester_policy.hpp"
#include "bha/suggestions/suggester_interface.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/path_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bha::suggestions {
    namespace fs = std::filesystem;

    using bha::utils::trim_whitespace_copy;
    using bha::utils::lowercase_ascii;
    using bha::utils::is_header_file_path;
    using bha::utils::is_source_file_path;
    using bha::utils::looks_like_macro_identifier;
    using bha::utils::contains_identifier_token;
    using bha::utils::find_outer_paren_span;

    /**
     * Context passed to suggesters containing all analysis data.
     */
    struct SuggestionContext {
        const BuildTrace& trace;
        const analyzers::AnalysisResult& analysis;
        const SuggesterOptions& options;
        fs::path project_root;
        std::shared_ptr<ProjectIndex> project_index;
        std::shared_ptr<ForwardDeclSemanticCache> forward_decl_semantic_cache;

        /// Optional cancellation token. Suggesters should check this periodically
        /// in long-running loops and return early if canceled.
        std::atomic<bool>* cancelled = nullptr;

        std::optional<std::chrono::steady_clock::time_point> deadline;

        /// Optional filter for incremental analysis. When set, only analyze
        /// files in this list. Empty means analyze all files.
        std::vector<fs::path> target_files;
        std::unordered_set<std::string> target_files_lookup;

        SuggestionContext(
            const BuildTrace& trace_ref,
            const analyzers::AnalysisResult& analysis_ref,
            const SuggesterOptions& options_ref,
            fs::path root,
            std::atomic<bool>* cancelled_ptr = nullptr,
            std::optional<std::chrono::steady_clock::time_point> deadline_value = std::nullopt,
            std::vector<fs::path> files = {},
            std::unordered_set<std::string> lookup = {}
        )
            : trace(trace_ref),
              analysis(analysis_ref),
              options(options_ref),
              project_root(std::move(root)),
              project_index(std::make_shared<ProjectIndex>(project_root, options_ref.compile_commands_path)),
              forward_decl_semantic_cache(std::make_shared<ForwardDeclSemanticCache>()),
              cancelled(cancelled_ptr),
              deadline(std::move(deadline_value)),
              target_files(std::move(files)),
              target_files_lookup(std::move(lookup)) {}

        /// Check if the operation has been canceled.
        [[nodiscard]] bool is_cancelled() const noexcept {
            if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
                return true;
            }
            if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline) {
                return true;
            }
            return false;
        }

        /// Check if a file should be analyzed (respects target_files filter).
        [[nodiscard]] bool should_analyze(const fs::path& file) const {
            if (target_files.empty()) {
                return true;
            }

            fs::path normalized_file = project_index
                ? project_index->resolve(file)
                : file.lexically_normal();
            if (!target_files_lookup.empty()) {
                if (normalized_file.parent_path().empty()) {
                    return target_files_lookup.contains(normalized_file.filename().string());
                }
                return target_files_lookup.contains(normalized_file.generic_string());
            }
            return std::ranges::any_of(
                target_files,
                [&](const fs::path& target) {
                    if (target.parent_path().empty()) {
                        return normalized_file.filename() == target;
                    }
                    const fs::path normalized_target = project_index
                        ? project_index->resolve(target)
                        : target.lexically_normal();
                    return normalized_file == normalized_target;
                }
            );
        }
    };
#include "suggester_path_helpers.inc"

    [[nodiscard]] inline std::string_view trim_preprocessor_whitespace(std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return {};
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    [[nodiscard]] inline bool is_preprocessor_identifier_char(const char character) noexcept {
        return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
    }

    struct PreprocessorDirective {
        std::string_view name;
        std::string_view argument;
    };

    struct IncludeDirective {
        std::size_t line = 0;
        std::size_t col_start = 0;
        std::size_t col_end = 0;
        std::string header_name;
        bool is_system = false;
    };

    [[nodiscard]] inline std::optional<PreprocessorDirective> parse_preprocessor_directive(
        std::string_view line
    ) {
        line = trim_preprocessor_whitespace(line);
        if (line.empty() || line.front() != '#') {
            return std::nullopt;
        }
        line.remove_prefix(1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
            line.remove_prefix(1);
        }
        const auto name_start = line.data();
        std::size_t name_length = 0;
        while (name_length < line.size() &&
               is_preprocessor_identifier_char(line[name_length])) {
            ++name_length;
        }
        if (name_length == 0) {
            return std::nullopt;
        }
        line.remove_prefix(name_length);
        return PreprocessorDirective{
            std::string_view(name_start, name_length),
            trim_preprocessor_whitespace(line)
        };
    }

    [[nodiscard]] inline std::optional<std::string_view> parse_single_preprocessor_identifier(
        std::string_view text
    ) {
        text = trim_preprocessor_whitespace(text);
        if (text.empty() || !(
                std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_')) {
            return std::nullopt;
        }
        std::size_t length = 1;
        while (length < text.size() && is_preprocessor_identifier_char(text[length])) {
            ++length;
        }
        if (!trim_preprocessor_whitespace(text.substr(length)).empty()) {
            return std::nullopt;
        }
        return text.substr(0, length);
    }

    [[nodiscard]] inline std::optional<IncludeDirective> parse_include_directive_line(
        std::string_view line
    ) {
        const auto directive = parse_preprocessor_directive(line);
        if (!directive.has_value() || directive->name != "include") {
            return std::nullopt;
        }

        const std::string_view argument = trim_preprocessor_whitespace(directive->argument);
        if (argument.size() < 3 || (argument.front() != '<' && argument.front() != '"')) {
            return std::nullopt;
        }
        const char closer = argument.front() == '<' ? '>' : '"';
        const auto end = argument.find(closer, 1);
        if (end <= 1 || end == std::string_view::npos) {
            return std::nullopt;
        }

        IncludeDirective result;
        result.header_name = std::string(argument.substr(1, end - 1));
        result.is_system = argument.front() == '<';
        result.col_end = line.size();
        return result;
    }

    [[nodiscard]] inline std::vector<IncludeDirective> find_include_directives(const fs::path& file) {
        std::vector<IncludeDirective> result;

        std::ifstream in(file);
        if (!in) {
            return result;
        }

        std::string line;
        std::size_t line_num = 0;

        while (std::getline(in, line)) {
            if (auto directive = parse_include_directive_line(line)) {
                directive->line = line_num;
                directive->col_start = 0;
                directive->col_end = line.size();
                result.push_back(std::move(*directive));
            }
            ++line_num;
        }

        return result;
    }

    [[nodiscard]] inline bool is_blank_or_whitespace_line(std::string_view line) {
        return line.find_first_not_of(" \t\r\n") == std::string_view::npos;
    }

    [[nodiscard]] inline TextEdit make_insert_after_line_edit(
        const fs::path& file,
        const std::size_t line,
        const std::string& content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line + 1;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = content + "\n";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_insert_at_start_edit(
        const fs::path& file,
        const std::string& content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = 0;
        edit.start_col = 0;
        edit.end_line = 0;
        edit.end_col = 0;
        edit.new_text = content + "\n";
        return edit;
    }

#include "suggester_edit_helpers.inc"

}  // namespace bha::suggestions

#endif //BHA_SUGGESTER_HPP
