//
// Created by gregorian-rayne on 12/28/25.
//

#include "bha/parsers/parser.hpp"
#include "bha/utils/file_utils.hpp"
#include "bha/utils/parallel.hpp"
#include <set>

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

    std::vector<std::string> get_supported_trace_extensions() {
        std::vector<std::string> extensions;
        std::set<std::string> unique_exts;

        for (const auto* parser : ParserRegistry::instance().list_parsers()) {
            for (const auto& ext : parser->supported_extensions()) {
                if (unique_exts.insert(ext).second) {
                    extensions.push_back(ext);
                }
            }
        }

        return extensions;
    }

    bool is_supported_trace_extension(const std::string_view ext) {
        std::string ext_str(ext);

        // Normalize: ensure leading dot
        if (!ext_str.empty() && ext_str[0] != '.') {
            ext_str = "." + ext_str;
        }

        for (const auto* parser : ParserRegistry::instance().list_parsers()) {
            for (const auto& supported_ext : parser->supported_extensions()) {
                if (supported_ext == ext_str) {
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<fs::path> collect_trace_files(
        const fs::path& path,
        const bool recursive
    )
    {
        std::vector<fs::path> result;

        if (!fs::exists(path)) {
            return result;
        }

        if (fs::is_regular_file(path)) {
            const auto ext = path.extension().string();
            if (is_supported_trace_extension(ext)) {
                result.push_back(path);
            }
            return result;
        }

        if (fs::is_directory(path)) {
            auto iterate = [&result](const auto& iterator) {
                for (const auto& entry : iterator) {
                    if (entry.is_regular_file()) {
                        if (auto ext = entry.path().extension().string(); is_supported_trace_extension(ext)) {
                            result.push_back(entry.path());
                        }
                    }
                }
            };

            if (recursive) {
                iterate(fs::recursive_directory_iterator(path));
            } else {
                iterate(fs::directory_iterator(path));
            }
        }

        return result;
    }

}  // namespace bha::parsers