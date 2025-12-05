//
// Created by gregorian on 15/10/2025.
//

#include "bha/parsers/parser.h"
#include "bha/parsers/clang_parser.h"
#include "bha/parsers/gcc_parser.h"
#include "bha/parsers/msvc_parser.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include "bha/utils/path_utils.h"
#include <unordered_map>
#include <functional>

namespace bha::parsers
{
    static std::unordered_map<CompilerType, std::function<std::unique_ptr<TraceParser>()>> parser_registry;

    core::Result<std::unique_ptr<TraceParser>> ParserFactory::create_parser(
        const std::string_view file_path
    ) {
        const CompilerType type = detect_compiler_from_file(file_path);

        if (type == CompilerType::UNKNOWN) {
            return core::Result<std::unique_ptr<TraceParser>>::failure(
                core::ErrorCode::UNSUPPORTED_FORMAT,
                "Unable to detect compiler type from file: " + std::string(file_path)
            );
        }

        return create_parser_for_compiler(type);
    }

    core::Result<std::unique_ptr<TraceParser>> ParserFactory::create_parser_for_compiler(
        const CompilerType type
    ) {
        switch (type) {
            case CompilerType::CLANG:
                return core::Result<std::unique_ptr<TraceParser>>::success(
                    std::make_unique<ClangTimeTraceParser>()
                );

            case CompilerType::GCC:
                return core::Result<std::unique_ptr<TraceParser>>::success(
                    std::make_unique<GCCTimeReportParser>()
                );

            case CompilerType::MSVC:
                return core::Result<std::unique_ptr<TraceParser>>::success(
                    std::make_unique<MSVCTraceParser>()
                );

            case CompilerType::UNKNOWN:
            default:
                return core::Result<std::unique_ptr<TraceParser>>::failure(
                    core::ErrorCode::UNSUPPORTED_COMPILER,
                    "Unsupported compiler type"
                );
        }
    }

    void ParserFactory::register_parser(
        CompilerType type,
        std::function<std::unique_ptr<TraceParser>()> factory
    ) {
        parser_registry[type] = std::move(factory);
    }

    CompilerType ParserFactory::detect_compiler_from_file(std::string_view file_path) {
        const auto content = utils::read_file(file_path);

        if (!content || content->empty()) {
            if (const std::string ext = utils::get_extension(file_path); ext == ".json") {
                return CompilerType::CLANG;
            } else if (ext == ".txt" || ext == ".log") {
                return CompilerType::GCC;
            }

            return CompilerType::UNKNOWN;
        }

        return detect_compiler_from_content(*content);
    }

    CompilerType ParserFactory::detect_compiler_from_content(std::string_view content) {
        if (content.empty()) {
            return CompilerType::UNKNOWN;
        }

        if (utils::contains(content, "traceEvents") ||
            utils::contains(content, "\"name\":") ||
            utils::contains(content, "ftime-trace")) {
            return CompilerType::CLANG;
        }

        if (utils::contains(content, "Time variable") ||
            utils::contains(content, "phase parsing") ||
            utils::contains(content, "TOTAL")) {
            return CompilerType::GCC;
        }

        if (utils::contains(content, "c1xx.dll") ||
            utils::contains(content, "time(") ||
            utils::contains(content, "Microsoft")) {
            return CompilerType::MSVC;
        }

        return CompilerType::UNKNOWN;
    }

    std::vector<CompilerType> ParserFactory::get_supported_compilers() {
        return {
            CompilerType::CLANG,
            CompilerType::GCC,
            CompilerType::MSVC
        };
    }

    std::string to_string(const CompilerType type) {
        switch (type) {
            case CompilerType::CLANG: return "clang";
            case CompilerType::GCC: return "gcc";
            case CompilerType::MSVC: return "msvc";
            case CompilerType::UNKNOWN: default: return "unknown";
        }
    }

    CompilerType compiler_type_from_string(const std::string_view str) {
        const std::string lower = utils::to_lower(str);

        if (lower == "clang") return CompilerType::CLANG;
        if (lower == "gcc" || lower == "g++") return CompilerType::GCC;
        if (lower == "msvc" || lower == "cl" || lower == "cl.exe") return CompilerType::MSVC;

        return CompilerType::UNKNOWN;
    }

    core::Result<CompilerType> ParserFactory::detect_compiler_version(
        const std::string_view compiler_path,
        std::string& out_version
    ) {
        const std::string path(compiler_path);
        std::string command = path + " --version";

        #ifdef _WIN32
        FILE* pipe = _popen(command.c_str(), "r");
        #else
        FILE* pipe = popen(command.c_str(), "r");
        #endif

        if (!pipe) {
            return core::Result<CompilerType>::failure(
                core::ErrorCode::INTERNAL_ERROR,
                "Failed to execute compiler version command"
            );
        }

        char buffer[256];
        std::string output;

        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }

        #ifdef _WIN32
        _pclose(pipe);
        #else
        pclose(pipe);
        #endif

        auto type = CompilerType::UNKNOWN;

        if (utils::contains(output, "clang")) {
            type = CompilerType::CLANG;

            if (const auto lines = utils::split_lines(output); !lines.empty()) {
                out_version = lines[0];
            }
        } else if (utils::contains(output, "gcc") || utils::contains(output, "g++")) {
            type = CompilerType::GCC;

            if (const auto lines = utils::split_lines(output); !lines.empty()) {
                out_version = lines[0];
            }
        } else if (utils::contains(output, "Microsoft")) {
            type = CompilerType::MSVC;
            out_version = output;
        }

        if (type == CompilerType::UNKNOWN) {
            return core::Result<CompilerType>::failure(
                core::ErrorCode::UNSUPPORTED_COMPILER,
                "Unable to detect compiler type from version output"
            );
        }

        return core::Result<CompilerType>::success(type);
    }

} // namespace bha::parsers