//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/parallel.hpp"

namespace bha::parsers {

    ParserRegistry& ParserRegistry::instance() {
        static ParserRegistry registry;
        return registry;
    }

    void ParserRegistry::register_parser(std::unique_ptr<ITraceParser> parser) {
        parsers_.push_back(std::move(parser));
    }

    ITraceParser* ParserRegistry::find_parser_for_file(const fs::path& path) const {
        const auto ext = path.extension().string();

        for (const auto& parser : parsers_) {
            auto extensions = parser->supported_extensions();

            if (const bool ext_match = std::ranges::find(extensions, ext) != extensions.end(); ext_match && parser->can_parse(path)) {
                return parser.get();
            }
        }

        return nullptr;
    }

    ITraceParser* ParserRegistry::find_parser_for_content(const std::string_view content) const {
        for (const auto& parser : parsers_) {
            if (parser->can_parse_content(content)) {
                return parser.get();
            }
        }
        return nullptr;
    }

    ITraceParser* ParserRegistry::get_parser(const CompilerType type) const {
        for (const auto& parser : parsers_) {
            if (parser->compiler_type() == type) {
                return parser.get();
            }
        }
        return nullptr;
    }

    std::vector<ITraceParser*> ParserRegistry::list_parsers() const {
        std::vector<ITraceParser*> result;
        result.reserve(parsers_.size());

        for (const auto& parser : parsers_) {
            result.push_back(parser.get());
        }

        return result;
    }

    std::vector<Result<CompilationUnit, Error>> parse_trace_files(
        const std::vector<fs::path>& paths
    ) {
        return parallel::map(paths, [](const fs::path& path) {
            return parse_trace_file(path);
        });
    }

}  // namespace bha::parsers