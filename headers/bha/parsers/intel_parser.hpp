//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BHA_INTEL_PARSER_HPP
#define BHA_INTEL_PARSER_HPP

/**
 * @file intel_parser.hpp
 * @brief Intel compiler timing parsers.
 *
 * Supports both Intel compilers:
 * - ICC/ICPC (Intel C++ Compiler Classic): -qopt-report timing
 * - ICX/ICPX (Intel oneAPI DPC++/C++): Uses Clang-based timing
 */

#include "bha/parsers/parser.hpp"

namespace bha::parsers {

    /**
     * Parser for Intel Classic Compiler (ICC/ICPC) timing output.
     *
     * Uses -qopt-report and related flags to get optimization and
     * compilation timing information.
     */
    class IntelClassicParser : public ITraceParser {
    public:
        /// Human-readable parser name.
        [[nodiscard]] std::string_view name() const noexcept override {
            return "Intel ICC";
        }

        /// Compiler family handled by this parser.
        [[nodiscard]] CompilerType compiler_type() const noexcept override {
            return CompilerType::IntelClassic;
        }

        /// Common extensions for ICC optimization/timing reports.
        [[nodiscard]] std::vector<std::string> supported_extensions() const override {
            return {".optrpt", ".txt", ".log"};
        }

        /// Quick path-based eligibility check.
        [[nodiscard]] bool can_parse(const fs::path& path) const override;

        /// Content-signature check for Intel classic timing/report output.
        [[nodiscard]] bool can_parse_content(std::string_view content) const override;

        /// Parse an Intel classic trace/report file into one compilation unit.
        [[nodiscard]] Result<CompilationUnit, Error> parse_file(
            const fs::path& path
        ) const override;

        /// Parse in-memory Intel classic trace content with optional source hint.
        [[nodiscard]] Result<CompilationUnit, Error> parse_content(
            std::string_view content,
            const fs::path& source_hint
        ) const override;
    };

    /**
     * Parser for Intel oneAPI Compiler (ICX/ICPX).
     *
     * ICX is based on Clang/LLVM and supports -ftime-trace,
     * so this parser inherits behavior similar to ClangTraceParser
     * but with Intel-specific markers.
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
     * Registers the Intel parsers with the global registry.
     */
    void register_intel_parsers();

}  // namespace bha::parsers

#endif //BHA_INTEL_PARSER_HPP
