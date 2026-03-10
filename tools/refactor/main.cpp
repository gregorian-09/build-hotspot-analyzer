#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bha/refactor/pimpl_tooling.hpp"
#include "bha/refactor/types.hpp"
#include "bha/suggestions/pimpl_suggester.hpp"

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
        return 0;
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

    std::optional<std::size_t> line_col_to_offset(
        const std::string& content,
        const std::size_t line,
        const std::size_t col
    ) {
        std::size_t current_line = 0;
        std::size_t line_start = 0;

        for (std::size_t index = 0; index < content.size(); ++index) {
            if (current_line == line) {
                const std::size_t offset = line_start + col;
                return std::min(offset, content.size());
            }
            if (content[index] == '\n') {
                ++current_line;
                line_start = index + 1;
            }
        }

        if (current_line == line) {
            return std::min(line_start + col, content.size());
        }
        if (line > current_line) {
            return content.size();
        }
        return std::nullopt;
    }

    std::optional<bha::refactor::Replacement> to_replacement(
        const bha::TextEdit& edit
    ) {
        std::ifstream in(edit.file, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        const std::string content(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );

        auto start_offset = line_col_to_offset(content, edit.start_line, edit.start_col);
        auto end_offset = line_col_to_offset(content, edit.end_line, edit.end_col);
        if (!start_offset || !end_offset) {
            return std::nullopt;
        }
        if (*start_offset > *end_offset) {
            std::swap(start_offset, end_offset);
        }

        bha::refactor::Replacement replacement;
        replacement.file = edit.file;
        replacement.offset = *start_offset;
        replacement.length = *end_offset - *start_offset;
        replacement.replacement_text = edit.new_text;
        return replacement;
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
    if (tooling_result.success) {
        const bool pretty = option_or_default(parsed->options, "output-format", "json") == "json";
        return emit_result(tooling_result, pretty);
    }
    if (!tooling_result.allow_fallback) {
        const bool pretty = option_or_default(parsed->options, "output-format", "json") == "json";
        return emit_result(tooling_result, pretty);
    }

    auto edits_result = bha::suggestions::generate_pimpl_refactor_edits(
        request.compile_commands_path,
        request.source_file,
        request.header_file,
        request.class_name
    );
    if (edits_result.is_err()) {
        result.engine = "text-fallback";
        result.diagnostics.insert(
            result.diagnostics.end(),
            tooling_result.diagnostics.begin(),
            tooling_result.diagnostics.end()
        );
        add_diagnostic(result, DiagnosticSeverity::Error, edits_result.error().message());
        const bool pretty = option_or_default(parsed->options, "output-format", "json") == "json";
        return emit_result(result, pretty);
    }

    result.engine = "text-fallback";
    result.diagnostics.insert(
        result.diagnostics.end(),
        tooling_result.diagnostics.begin(),
        tooling_result.diagnostics.end()
    );
    std::unordered_set<std::string> seen_files;
    for (const auto& edit : edits_result.value()) {
        if (auto replacement = to_replacement(edit)) {
            if (seen_files.insert(replacement->file.string()).second) {
                result.files.push_back(replacement->file);
            }
            result.replacements.push_back(std::move(*replacement));
        } else {
            add_diagnostic(
                result,
                DiagnosticSeverity::Error,
                "Failed to translate a generated text edit into a byte-range replacement",
                edit.file
            );
        }
    }

    if (result.replacements.empty()) {
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "No replacements were generated for the requested PIMPL refactor"
        );
    } else {
        result.success = true;
        result.validated_structure = true;
        result.summary.copy_mode = "strict-subset";
    }

    const bool pretty = option_or_default(parsed->options, "output-format", "json") == "json";
    return emit_result(result, pretty);
}
