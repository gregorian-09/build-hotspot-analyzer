#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "bha/refactor/pimpl_tooling.hpp"
#include "bha/refactor/types.hpp"

namespace {

    namespace fs = std::filesystem;

    struct Arguments {
        std::string command;
        std::unordered_map<std::string, std::string> options;
    };

    std::optional<Arguments> parse_arguments(const int argc, char* argv[]) {
        if (argc < 2) {
            return std::nullopt;
        }

        Arguments parsed;
        parsed.command = argv[1];

        for (int index = 2; index < argc; ++index) {
            const std::string_view token = argv[index];
            if (!token.starts_with("--")) {
                continue;
            }

            const std::string key(token.substr(2));
            if (key == "dry-run" || key == "in-place" || key == "stdout-patch") {
                parsed.options.emplace(key, "true");
                continue;
            }

            if (index + 1 >= argc) {
                return std::nullopt;
            }

            parsed.options.emplace(key, argv[++index]);
        }

        return parsed;
    }

    std::string option_or_default(
        const std::unordered_map<std::string, std::string>& options,
        const std::string& key,
        const std::string& fallback
    ) {
        if (const auto it = options.find(key); it != options.end()) {
            return it->second;
        }
        return fallback;
    }

    std::optional<std::string> required_option(
        const std::unordered_map<std::string, std::string>& options,
        const std::string& key
    ) {
        if (const auto it = options.find(key); it != options.end() && !it->second.empty()) {
            return it->second;
        }
        return std::nullopt;
    }

    int emit_result(const bha::refactor::Result& result, const bool pretty) {
        std::cout << nlohmann::json(result).dump(pretty ? 2 : -1) << '\n';
        return result.success ? 0 : 1;
    }

    void add_diagnostic(
        bha::refactor::Result& result,
        const bha::refactor::DiagnosticSeverity severity,
        std::string message,
        fs::path file = {},
        const std::size_t line = 0
    ) {
        bha::refactor::Diagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.message = std::move(message);
        diagnostic.file = std::move(file);
        diagnostic.line = line;
        result.diagnostics.push_back(std::move(diagnostic));
    }

}  // namespace

int main(const int argc, char* argv[]) {
    using bha::refactor::DiagnosticSeverity;
    using bha::refactor::Result;

    const auto parsed = parse_arguments(argc, argv);
    if (!parsed) {
        Result result;
        result.refactor_type = "unknown";
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "Usage: bha-refactor pimpl --compile-commands <path> --source <file> --header <file> --class <name> [--output-format json]"
        );
        std::cout << nlohmann::json(result).dump(2) << '\n';
        return 2;
    }

    Result result;
    result.refactor_type = parsed->command;

    if (parsed->command != "pimpl") {
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "Only the 'pimpl' refactor is scaffolded right now"
        );
        std::cout << nlohmann::json(result).dump(2) << '\n';
        return 2;
    }

    const auto compile_commands = required_option(parsed->options, "compile-commands");
    const auto source = required_option(parsed->options, "source");
    const auto header = required_option(parsed->options, "header");
    const auto class_name = required_option(parsed->options, "class");

    if (!compile_commands || !source || !header || !class_name) {
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "Missing one or more required options: --compile-commands, --source, --header, --class"
        );
        std::cout << nlohmann::json(result).dump(2) << '\n';
        return 2;
    }

    result.summary.class_name = *class_name;
    const bha::refactor::PimplRequest request{
        .compile_commands_path = fs::path(*compile_commands),
        .source_file = fs::path(*source),
        .header_file = fs::path(*header),
        .class_name = *class_name,
    };

    if (!fs::exists(*compile_commands)) {
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "compile_commands.json was not found at the requested path",
            fs::path(*compile_commands)
        );
        std::cout << nlohmann::json(result).dump(2) << '\n';
        return 2;
    }

    if (!fs::exists(*source)) {
        add_diagnostic(
            result,
            DiagnosticSeverity::Warning,
            "The requested source file does not exist in this workspace",
            fs::path(*source)
        );
    }

    if (!fs::exists(*header)) {
        add_diagnostic(
            result,
            DiagnosticSeverity::Warning,
            "The requested header file does not exist in this workspace",
            fs::path(*header)
        );
    }

    auto tooling_result = bha::refactor::run_pimpl_refactor_with_clang_tooling(request);
    const bool pretty = option_or_default(parsed->options, "output-format", "json") == "json";
    return emit_result(tooling_result, pretty);
}
