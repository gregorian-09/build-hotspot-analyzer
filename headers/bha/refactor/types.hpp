#ifndef BUILDTIMEHOTSPOTANALYZER_REFACTOR_TYPES_HPP
#define BUILDTIMEHOTSPOTANALYZER_REFACTOR_TYPES_HPP

/**
 * @file types.hpp
 * @brief Shared data model for external refactor operations.
 */

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace bha::refactor {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    /**
     * @brief Diagnostic severity levels for refactor diagnostics.
     */
    enum class DiagnosticSeverity {
        Error,
        Warning,
        Note
    };

    /**
     * @brief One diagnostic entry produced by refactor execution/validation.
     */
    struct Diagnostic {
        /// Severity category.
        DiagnosticSeverity severity = DiagnosticSeverity::Note;
        /// Human-readable diagnostic message.
        std::string message;
        /// Optional source file associated with the issue.
        fs::path file;
        /// Optional 1-based source line.
        std::size_t line = 0;
    };

    /**
     * @brief Text replacement operation for one file region.
     */
    struct Replacement {
        /// Target file path.
        fs::path file;
        /// Byte offset of replacement start.
        std::size_t offset = 0;
        /// Byte length of replaced range.
        std::size_t length = 0;
        /// New text written into the replaced range.
        std::string replacement_text;
    };

    /**
     * @brief High-level refactor summary statistics.
     */
    struct Summary {
        /// Refactored class name.
        std::string class_name;
        /// Copy-semantics handling mode reported by the refactor engine.
        std::string copy_mode = "unsupported";
        /// Number of private fields moved to implementation object.
        std::size_t moved_private_fields = 0;
        /// Number of method declarations/definitions rewritten.
        std::size_t rewritten_methods = 0;
    };

    /**
     * @brief Input parameters for a PIMPL external refactor request.
     */
    struct PimplRequest {
        /// Path to `compile_commands.json` used for semantic tooling.
        fs::path compile_commands_path;
        /// Source file implementing the target class.
        fs::path source_file;
        /// Header file declaring the target class.
        fs::path header_file;
        /// Exact class name to refactor.
        std::string class_name;
    };

    /**
     * @brief Complete external-refactor result payload.
     */
    struct Result {
        /// Whether the refactor completed successfully.
        bool success = false;
        /// Refactor kind identifier.
        std::string refactor_type;
        /// Engine backend used to produce this result.
        std::string engine = "clang-tooling";
        /// Files touched by the refactor.
        std::vector<fs::path> files;
        /// Concrete textual replacements emitted by the refactor.
        std::vector<Replacement> replacements;
        /// Diagnostics produced during planning/execution.
        std::vector<Diagnostic> diagnostics;
        /// Summary counters and high-level metadata.
        Summary summary;
        /// Whether caller should run a rebuild afterwards.
        bool requires_rebuild = true;
        /// Whether structural validation passed.
        bool validated_structure = false;
        /// Whether fallback behavior was permitted.
        bool allow_fallback = true;
    };

    /**
     * @brief Convert diagnostic severity enum to stable string token.
     */
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

    /**
     * @brief Serialize `Diagnostic` to JSON.
     */
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

    /**
     * @brief Serialize `Replacement` to JSON.
     */
    inline void to_json(json& j, const Replacement& replacement) {
        j["file"] = replacement.file.string();
        j["offset"] = replacement.offset;
        j["length"] = replacement.length;
        j["replacement_text"] = replacement.replacement_text;
    }

    /**
     * @brief Serialize `Summary` to JSON.
     */
    inline void to_json(json& j, const Summary& summary) {
        j["class_name"] = summary.class_name;
        j["copy_mode"] = summary.copy_mode;
        j["moved_private_fields"] = summary.moved_private_fields;
        j["rewritten_methods"] = summary.rewritten_methods;
    }

    /**
     * @brief Serialize refactor `Result` to JSON.
     */
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
