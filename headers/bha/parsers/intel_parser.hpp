//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BHA_INTEL_PARSER_HPP
#define BHA_INTEL_PARSER_HPP

/**
 * @file intel_parser.hpp
 * @brief Intel compiler timing parsers.
 *
 * Supports Intel oneAPI ICX/ICPX through its Clang-compatible time trace.
 */

#include "bha/parsers/parser.hpp"

namespace bha::parsers {

    /**
     * Parser for Intel oneAPI Compiler (ICX/ICPX).
     *
     * ICX is based on Clang/LLVM and supports -ftime-trace,
     * so this parser inherits behavior similar to ClangTraceParser
     * while retaining Intel's explicit compiler-family selection.
     */
    class IntelOneAPIParser : public ITraceParser {
    public:
        /// Human-readable parser name.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "Intel ICX";
        }

        /// Compiler family handled by this parser.
        [[nodiscard]] CompilerType compiler_type() const noexcept override {
            return CompilerType::IntelOneAPI;
        }

        /// Preferred trace artifact extension for ICX clang-style traces.
        [[nodiscard]] std::vector<std::string> supported_extensions() const override {
            return {".json"};
        }

        /// Quick path-based eligibility check.
        [[nodiscard]] bool can_parse(const fs::path& path) const override;

        /// Content-signature check for oneAPI/clang trace JSON.
        [[nodiscard]] bool can_parse_content(std::string_view content) const override;

        /// Parse an Intel oneAPI trace file into one compilation unit.
        [[nodiscard]] Result<CompilationUnit, Error> parse_file(
            const fs::path& path
        ) const override;

        /// Parse in-memory Intel oneAPI trace content with optional source hint.
        [[nodiscard]] Result<CompilationUnit, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint
        ) const override;
    };

    /**
     * Registers the Intel oneAPI parser with the global registry.
     */
    void register_intel_parsers();

}  // namespace bha::parsers

#endif //BHA_INTEL_PARSER_HPP
