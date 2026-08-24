#include "bha/lsp/diagnostic_parser.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bha::lsp {
    namespace {
        struct SourceLocation {
            std::string file;
            std::size_t line = 0;
            std::size_t column = 0;
        };

        struct SeverityMarker {
            std::string_view text;
            DiagnosticSeverity severity;
            bool msvc_style;
        };

        constexpr std::array<SeverityMarker, 6> kSeverityMarkers{{
            {": fatal error:", DiagnosticSeverity::Error, false},
            {": error:", DiagnosticSeverity::Error, false},
            {": warning:", DiagnosticSeverity::Warning, false},
            {": fatal error ", DiagnosticSeverity::Error, true},
            {": error ", DiagnosticSeverity::Error, true},
            {": warning ", DiagnosticSeverity::Warning, true},
        }};

        std::string_view trim(std::string_view value) {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty() &&
                   (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
                value.remove_suffix(1);
            }
            return value;
        }

        std::optional<std::size_t> parse_positive_number(std::string_view value) {
            value = trim(value);
            if (value.empty()) {
                return std::nullopt;
            }

            std::size_t number = 0;
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            const auto [parsed_end, error] = std::from_chars(begin, end, number);
            if (error != std::errc{} || parsed_end != end || number == 0) {
                return std::nullopt;
            }
            return number;
        }

        std::optional<SourceLocation> parse_clang_or_gcc_location(std::string_view prefix) {
            prefix = trim(prefix);
            const auto last_separator = prefix.rfind(':');
            if (last_separator == std::string_view::npos) {
                return std::nullopt;
            }

            const auto last_number = parse_positive_number(prefix.substr(last_separator + 1));
            if (!last_number.has_value()) {
                return std::nullopt;
            }

            const auto before_last = prefix.substr(0, last_separator);
            const auto previous_separator = before_last.rfind(':');
            if (previous_separator != std::string_view::npos) {
                const auto line_number = parse_positive_number(before_last.substr(previous_separator + 1));
                const auto file = trim(before_last.substr(0, previous_separator));
                if (line_number.has_value() && !file.empty()) {
                    return SourceLocation{std::string(file), *line_number, *last_number};
                }
            }

            const auto file = trim(before_last);
            if (file.empty()) {
                return std::nullopt;
            }
            return SourceLocation{std::string(file), *last_number, 0};
        }

        std::optional<SourceLocation> parse_msvc_location(std::string_view prefix) {
            prefix = trim(prefix);
            if (prefix.empty() || prefix.back() != ')') {
                return std::nullopt;
            }

            const auto open_parenthesis = prefix.rfind('(');
            if (open_parenthesis == std::string_view::npos) {
                return std::nullopt;
            }

            const auto file = trim(prefix.substr(0, open_parenthesis));
            if (file.empty()) {
                return std::nullopt;
            }

            auto coordinates = prefix.substr(open_parenthesis + 1);
            coordinates.remove_suffix(1);
            const auto comma = coordinates.find(',');
            const auto line_number = parse_positive_number(
                comma == std::string_view::npos ? coordinates : coordinates.substr(0, comma)
            );
            if (!line_number.has_value()) {
                return std::nullopt;
            }

            std::size_t column = 0;
            if (comma != std::string_view::npos) {
                const auto column_number = parse_positive_number(coordinates.substr(comma + 1));
                if (!column_number.has_value()) {
                    return std::nullopt;
                }
                column = *column_number;
            }
            return SourceLocation{std::string(file), *line_number, column};
        }

        std::optional<std::pair<SeverityMarker, std::size_t>> find_marker(std::string_view line) {
            std::optional<std::pair<SeverityMarker, std::size_t>> result;
            for (const auto& marker : kSeverityMarkers) {
                const auto position = line.find(marker.text);
                if (position == std::string_view::npos ||
                    (result.has_value() && position >= result->second)) {
                    continue;
                }
                result = std::make_pair(marker, position);
            }
            return result;
        }

        std::optional<Diagnostic> parse_line(std::string_view line) {
            const auto marker = find_marker(line);
            if (!marker.has_value()) {
                return std::nullopt;
            }

            const auto location = marker->first.msvc_style
                ? parse_msvc_location(line.substr(0, marker->second))
                : parse_clang_or_gcc_location(line.substr(0, marker->second));
            if (!location.has_value()) {
                return std::nullopt;
            }
            constexpr auto max_position = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
            if (location->line > max_position || location->column > max_position) {
                return std::nullopt;
            }

            Diagnostic diagnostic;
            diagnostic.range.start.line = static_cast<int>(location->line - 1);
            diagnostic.range.start.character = location->column == 0
                ? 0
                : static_cast<int>(location->column - 1);
            diagnostic.range.end = diagnostic.range.start;
            diagnostic.severity = marker->first.severity;
            diagnostic.message = std::string(trim(line.substr(
                marker->second + marker->first.text.size()
            )));
            diagnostic.source = "compiler";
            return diagnostic;
        }
    }

    std::vector<Diagnostic> parse_compiler_diagnostics(const std::string_view output) {
        std::vector<Diagnostic> diagnostics;
        std::size_t line_start = 0;
        while (line_start <= output.size()) {
            const auto line_end = output.find('\n', line_start);
            const auto line = output.substr(
                line_start,
                line_end == std::string_view::npos ? output.size() - line_start : line_end - line_start
            );
            if (const auto diagnostic = parse_line(line); diagnostic.has_value()) {
                diagnostics.push_back(*diagnostic);
            }
            if (line_end == std::string_view::npos) {
                break;
            }
            line_start = line_end + 1;
        }
        return diagnostics;
    }
}
