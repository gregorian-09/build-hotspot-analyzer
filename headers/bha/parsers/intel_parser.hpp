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
        [[nodiscard]] std::string_view name() const noexcept override {
            return "Intel ICC";
        }

        [[nodiscard]] CompilerType compiler_type() const noexcept override {
            return CompilerType::IntelClassic;
        }

        [[nodiscard]] std::vector<std::string> supported_extensions() const override {
            return {".optrpt", ".txt", ".log"};
        }

        [[nodiscard]] bool can_parse(const fs::path& path) const override;

        [[nodiscard]] bool can_parse_content(std::string_view content) const override;

        [[nodiscard]] Result<CompilationUnit, Error> parse_file(
            const fs::path& path
        ) const override;

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
        [[nodiscard]] std::string_view name() const noexcept override {
            return "Intel ICX";
        }

        [[nodiscard]] CompilerType compiler_type() const noexcept override {
            return CompilerType::IntelOneAPI;
        }

        [[nodiscard]] std::vector<std::string> supported_extensions() const override {
            return {".json"};
        }

        [[nodiscard]] bool can_parse(const fs::path& path) const override;

        [[nodiscard]] bool can_parse_content(std::string_view content) const override;

        [[nodiscard]] Result<CompilationUnit, Error> parse_file(
            const fs::path& path
        ) const override;

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