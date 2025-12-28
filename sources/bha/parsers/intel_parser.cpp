//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/intel_parser.hpp"
#include "bha/parsers/clang_parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/string_utils.hpp"

#include <nlohmann/json.hpp>
#include <regex>
#include <charconv>

namespace bha::parsers {

    using json = nlohmann::json;

    namespace {

        constexpr std::string_view ICC_MARKER = "Intel(R) C++ Compiler";
        constexpr std::string_view ICC_OPT_REPORT = "LOOP BEGIN";
        constexpr std::string_view ICX_MARKER = "icx";

        Duration parse_icc_time(const std::string_view time_str) {
            double seconds = 0.0;
            auto trimmed = string_utils::trim(time_str);

            if (string_utils::ends_with(trimmed, "s")) {
                trimmed = trimmed.substr(0, trimmed.size() - 1);
            }

            std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), seconds);
            return std::chrono::duration_cast<Duration>(
                std::chrono::duration<double>(seconds)
            );
        }

    }  // namespace

    // =============================================================================
    // Intel Classic Compiler (ICC)
    // =============================================================================

    bool IntelClassicParser::can_parse(const fs::path& path) const {
        if (const auto ext = path.extension().string(); ext != ".optrpt" && ext != ".txt" && ext != ".log") {
            return false;
        }

        auto result = file_utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool IntelClassicParser::can_parse_content(std::string_view content) const {
        return string_utils::contains(content, ICC_MARKER) ||
               (string_utils::contains(content, ICC_OPT_REPORT) &&
                string_utils::contains(content, "icc"));
    }

    Result<CompilationUnit, Error> IntelClassicParser::parse_file(
        const fs::path& path
    ) const {
        auto content_result = file_utils::read_file(path);
        if (content_result.is_err()) {
            return Result<CompilationUnit, Error>::failure(content_result.error());
        }

        auto source_file = path;
        if (path.extension() == ".optrpt") {
            source_file.replace_extension(".cpp");
        }

        return parse_content(content_result.value(), source_file);
    }

    Result<CompilationUnit, Error> IntelClassicParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        CompilationUnit unit;
        unit.source_file = source_hint;
        unit.metrics.path = source_hint;

        const auto lines = string_utils::split(content, '\n');

        const std::regex time_regex(R"((\d+\.?\d*)\s*(?:s|seconds?))");
        const std::regex loop_regex(R"(LOOP BEGIN at ([^:]+):(\d+))");

        Duration total_time = Duration::zero();

        for (const auto& line : lines) {
            std::string line_str(line);

            if (std::smatch loop_match; std::regex_search(line_str, loop_match, loop_regex)) {
                if (unit.source_file.empty() && loop_match.size() > 1) {
                    unit.source_file = loop_match[1].str();
                    unit.metrics.path = unit.source_file;
                }
            }

            if (std::smatch time_match; std::regex_search(line_str, time_match, time_regex)) {
                total_time += parse_icc_time(time_match[1].str());
            }
        }

        unit.metrics.total_time = total_time;
        unit.metrics.breakdown.optimization = total_time;

        return Result<CompilationUnit, Error>::success(std::move(unit));
    }

    // =============================================================================
    // Intel oneAPI Compiler (ICX) - Clang-based
    // =============================================================================

    bool IntelOneAPIParser::can_parse(const fs::path& path) const {
        if (path.extension() != ".json") {
            return false;
        }

        auto result = file_utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool IntelOneAPIParser::can_parse_content(const std::string_view content) const {
        if (!string_utils::contains(content, "traceEvents")) {
            return false;
        }

        return string_utils::contains(content, ICX_MARKER) ||
               string_utils::contains(content, "Intel") ||
               string_utils::contains(content, "oneAPI");
    }

    Result<CompilationUnit, Error> IntelOneAPIParser::parse_file(
        const fs::path& path
    ) const {
        const ClangTraceParser clang_parser;

        if (auto result = clang_parser.parse_file(path); result.is_ok()) {
            return result;
        }

        return Result<CompilationUnit, Error>::failure(
            Error::parse_error("Failed to parse Intel ICX trace", path.string())
        );
    }

    Result<CompilationUnit, Error> IntelOneAPIParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        const ClangTraceParser clang_parser;
        return clang_parser.parse_content(content, source_hint);
    }

    void register_intel_parsers() {
        ParserRegistry::instance().register_parser(
            std::make_unique<IntelClassicParser>()
        );
        ParserRegistry::instance().register_parser(
            std::make_unique<IntelOneAPIParser>()
        );
    }

}  // namespace bha::parsers