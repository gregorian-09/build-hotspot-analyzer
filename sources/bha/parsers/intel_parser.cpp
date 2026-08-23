//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/intel_parser.hpp"
#include "bha/parsers/clang_parser.hpp"
#include "bha/utils/file_utils.hpp"

namespace bha::parsers {

    bool IntelOneAPIParser::can_parse(const fs::path& path) const {
        if (path.extension() != ".json") {
            return false;
        }

        auto result = utils::read_file(path);
        if (result.is_err()) {
            return false;
        }

        return can_parse_content(result.value());
    }

    bool IntelOneAPIParser::can_parse_content(const std::string_view content) const {
        const ClangTraceParser clang_parser;
        return clang_parser.can_parse_content(content);
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
            std::make_unique<IntelOneAPIParser>()
        );
    }

}  // namespace bha::parsers
