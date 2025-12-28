//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BHA_NVCC_PARSER_HPP
#define BHA_NVCC_PARSER_HPP

/**
 * @file nvcc_parser.hpp
 * @brief NVIDIA NVCC compiler timing parser.
 *
 * Parses timing output from NVIDIA's CUDA compiler (nvcc).
 * NVCC can output timing information with --time flag.
 */

#include "bha/parsers/parser.hpp"

namespace bha::parsers {

    /**
     * Parser for NVCC timing output.
     *
     * NVCC's --time flag produces output like:
     *   nvcc times: 0.12s compile, 0.34s ptxas, 0.56s fatbinary
     *
     * This parser extracts compilation phases specific to CUDA:
     * - Host compilation time
     * - Device compilation time (PTX generation)
     * - Fatbinary generation time
     */
    class NVCCTraceParser : public ITraceParser {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "NVCC";
        }

        [[nodiscard]] CompilerType compiler_type() const noexcept override {
            return CompilerType::NVCC;
        }

        [[nodiscard]] std::vector<std::string> supported_extensions() const override {
            return {".txt", ".log", ".nvlog"};
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
     * Registers the NVCC parser with the global registry.
     */
    void register_nvcc_parser();

}  // namespace bha::parsers

#endif //BHA_NVCC_PARSER_HPP