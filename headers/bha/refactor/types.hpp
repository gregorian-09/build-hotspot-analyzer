#ifndef BUILDTIMEHOTSPOTANALYZER_REFACTOR_TYPES_HPP
#define BUILDTIMEHOTSPOTANALYZER_REFACTOR_TYPES_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace bha::refactor {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    enum class DiagnosticSeverity {
        Error,
        Warning,
        Note
    };

    struct Diagnostic {
        DiagnosticSeverity severity = DiagnosticSeverity::Note;
        std::string message;
        fs::path file;
        std::size_t line = 0;
    };

    struct Replacement {
        fs::path file;
        std::size_t offset = 0;
        std::size_t length = 0;
        std::string replacement_text;
    };

    struct Summary {
        std::string class_name;
        std::string copy_mode = "unsupported";
        std::size_t moved_private_fields = 0;
        std::size_t rewritten_methods = 0;
    };

    struct PimplRequest {
        fs::path compile_commands_path;
        fs::path source_file;
        fs::path header_file;
        std::string class_name;
    };

    struct Result {
        bool success = false;
        std::string refactor_type;
        std::string engine = "clang-tooling";
        std::vector<fs::path> files;
        std::vector<Replacement> replacements;
        std::vector<Diagnostic> diagnostics;
        Summary summary;
        bool requires_rebuild = true;
        bool validated_structure = false;
        bool allow_fallback = true;
    };

    inline const char* to_string(const DiagnosticSeverity severity) noexcept {
        switch (severity) {
            case DiagnosticSeverity::Error:
                return "error";
            case DiagnosticSeverity::Warning:
                return "warning";
            case DiagnosticSeverity::Note:
                return "note";
        }
        return "note";
    }

    inline void to_json(json& j, const Diagnostic& diagnostic) {
        j["severity"] = to_string(diagnostic.severity);
        j["message"] = diagnostic.message;
        if (!diagnostic.file.empty()) {
            j["file"] = diagnostic.file.string();
        }
        if (diagnostic.line > 0) {
            j["line"] = diagnostic.line;
        }
    }

    inline void to_json(json& j, const Replacement& replacement) {
        j["file"] = replacement.file.string();
        j["offset"] = replacement.offset;
        j["length"] = replacement.length;
        j["replacement_text"] = replacement.replacement_text;
    }

    inline void to_json(json& j, const Summary& summary) {
        j["class_name"] = summary.class_name;
        j["copy_mode"] = summary.copy_mode;
        j["moved_private_fields"] = summary.moved_private_fields;
        j["rewritten_methods"] = summary.rewritten_methods;
    }

    inline void to_json(json& j, const Result& result) {
        j["success"] = result.success;
        j["refactor_type"] = result.refactor_type;
        j["engine"] = result.engine;

        json files = json::array();
        for (const auto& file : result.files) {
            files.push_back(file.string());
        }
        j["files"] = std::move(files);
        j["replacements"] = result.replacements;
        j["diagnostics"] = result.diagnostics;
        j["summary"] = result.summary;
        j["requires_rebuild"] = result.requires_rebuild;
        j["validated_structure"] = result.validated_structure;
    }

}  // namespace bha::refactor

#endif
