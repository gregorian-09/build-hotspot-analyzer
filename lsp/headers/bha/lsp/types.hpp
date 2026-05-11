#pragma once

/**
 * @file types.hpp
 * @brief LSP-facing data model and JSON serializers.
 */

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <nlohmann/json.hpp>

namespace bha::lsp
{
    using json = nlohmann::json;

    /// Zero-based position in a text document.
    struct Position {
        int line = 0;
        int character = 0;
    };

    /// Text range in a document (inclusive start, exclusive end).
    struct Range {
        Position start;
        Position end;
    };

    /// URI plus range reference.
    struct Location {
        std::string uri;
        Range range;
    };

    /// Text replacement in one range.
    struct TextEdit {
        Range range;
        std::string new_text;
    };

    /// LSP `window/showMessage` categories.
    enum class MessageType {
        Error = 1,
        Warning = 2,
        Info = 3,
        Log = 4
    };

    /// LSP diagnostic severity levels.
    enum class DiagnosticSeverity {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    /// LSP diagnostic payload.
    struct Diagnostic {
        Range range;
        DiagnosticSeverity severity = DiagnosticSeverity::Information;
        std::optional<std::string> code;
        std::optional<std::string> source;
        std::string message;
    };

    inline void to_json(json& j, const Diagnostic& d);

    /// Suggestion categories exposed to IDE clients.
    enum class SuggestionType {
        PrecompiledHeader,
        HeaderSplit,
        UnityBuild,
        TemplateOptimization,
        IncludeReduction,
        ForwardDeclaration,
        PIMPLPattern,
        MoveToCpp
    };

    /// User-facing prioritization buckets.
    enum class Priority {
        High,
        Medium,
        Low
    };

    /// Relative implementation complexity estimate.
    enum class Complexity {
        Trivial,
        Simple,
        Moderate,
        Complex
    };

    /// Estimated effect summary for one suggestion.
    struct Impact {
        int time_saved_ms;
        double percentage;
        int files_affected;
        Complexity complexity;
    };

    /// Summary suggestion payload used in list/code-action surfaces.
    struct Suggestion {
        std::string id;
        SuggestionType type;
        Priority priority;
        std::string title;
        std::string description;
        Impact estimated_impact;
        double confidence;
        bool auto_applicable;
        std::optional<std::string> application_mode;
        std::optional<std::string> refactor_class_name;
        std::optional<std::string> compile_commands_path;
        std::optional<std::string> application_summary;
        std::optional<std::string> application_guidance;
        std::optional<std::string> auto_apply_blocked_reason;
        std::optional<std::string> project_context;
        std::vector<std::string> module_rules_files;
        std::vector<std::string> target_rules_files;
        std::optional<std::string> safety_guard;

        /// Target file URI (file:// scheme)
        std::optional<std::string> target_uri;

        /// Source location range for code actions/diagnostics
        std::optional<Range> range;
    };

    /// Expanded suggestion payload for details views.
    struct DetailedSuggestion : Suggestion {
        std::string rationale;
        std::vector<std::string> files_to_create;
        std::vector<std::string> files_to_modify;
        std::vector<std::string> dependencies;
        std::optional<std::string> application_summary;
        std::optional<std::string> application_guidance;
        std::optional<std::string> auto_apply_blocked_reason;
    };

    /// One file-level change in apply preview/result payloads.
    struct FileChange {
        std::string file;
        enum class Type { Create, Modify, Delete } type;
        std::optional<std::string> content;
        std::vector<TextEdit> edits;
    };

    /// Build metrics surfaced to LSP clients.
    struct BuildMetrics {
        int total_duration_ms;
        int files_compiled;
        int files_up_to_date;
        /// Per-file timing metric entry.
        struct FileMetric {
            std::string file;
            int duration_ms;
            double percentage;
        };
        std::vector<FileMetric> slowest_files;
    };

    /// Validation status payload for apply/build verification.
    struct ValidationResult {
        bool valid;
        bool syntax_valid;
        bool semantics_valid;
        bool build_system_valid;
        std::vector<Diagnostic> errors;
        std::vector<Diagnostic> warnings;
    };

    /// Build execution result payload.
    struct BuildResult {
        bool success;
        BuildMetrics metrics;
        std::optional<std::string> output;
        std::vector<Diagnostic> errors;
        std::vector<Diagnostic> warnings;
    };

    // ============================================================================
    // Progress Notification Types (LSP $/progress)
    // ============================================================================

    /**
     * Progress token can be string or integer per LSP spec.
     */
    using ProgressToken = std::variant<std::string, int>;

    /**
     * Work done progress begin notification.
     */
    struct WorkDoneProgressBegin {
        std::string kind = "begin";
        std::string title;
        std::optional<bool> cancellable;
        std::optional<std::string> message;
        std::optional<int> percentage;
    };

    /**
     * Work done progress report notification.
     */
    struct WorkDoneProgressReport {
        std::string kind = "report";
        std::optional<bool> cancellable;
        std::optional<std::string> message;
        std::optional<int> percentage;
    };

    /**
     * Work done progress end notification.
     */
    struct WorkDoneProgressEnd {
        std::string kind = "end";
        std::optional<std::string> message;
    };

    inline void to_json(json& j, const WorkDoneProgressBegin& p) {
        j["kind"] = p.kind;
        j["title"] = p.title;
        if (p.cancellable) j["cancellable"] = *p.cancellable;
        if (p.message) j["message"] = *p.message;
        if (p.percentage) j["percentage"] = *p.percentage;
    }

    /// Serialize `WorkDoneProgressReport`.
    inline void to_json(json& j, const WorkDoneProgressReport& p) {
        j["kind"] = p.kind;
        if (p.cancellable) j["cancellable"] = *p.cancellable;
        if (p.message) j["message"] = *p.message;
        if (p.percentage) j["percentage"] = *p.percentage;
    }

    /// Serialize `WorkDoneProgressEnd`.
    inline void to_json(json& j, const WorkDoneProgressEnd& p) {
        j["kind"] = p.kind;
        if (p.message) j["message"] = *p.message;
    }

    /// Serialize progress token variant.
    inline void to_json(json& j, const ProgressToken& token) {
        std::visit([&j](auto&& arg) { j = arg; }, token);
    }

    /// Serialize `Position`.
    inline void to_json(json& j, const Position& p) {
        j["line"] = p.line;
        j["character"] = p.character;
    }

    /// Deserialize `Position`.
    inline void from_json(const json& j, Position& p) {
        j.at("line").get_to(p.line);
        j.at("character").get_to(p.character);
    }

    /// Serialize `Range`.
    inline void to_json(json& j, const Range& r) {
        json start_json;
        to_json(start_json, r.start);
        j["start"] = start_json;
        json end_json;
        to_json(end_json, r.end);
        j["end"] = end_json;
    }

    /// Deserialize `Range`.
    inline void from_json(const json& j, Range& r) {
        j.at("start").get_to(r.start);
        j.at("end").get_to(r.end);
    }

    /// Serialize `Diagnostic`.
    inline void to_json(json& j, const Diagnostic& d) {
        json range_json;
        to_json(range_json, d.range);
        j["range"] = range_json;
        j["severity"] = static_cast<int>(d.severity);
        j["message"] = d.message;
        if (d.code) {
            j["code"] = *d.code;
        }
        if (d.source) {
            j["source"] = *d.source;
        }
    }

    /// Serialize `Impact`.
    inline void to_json(json& j, const Impact& i) {
        j["timeSavedMs"] = i.time_saved_ms;
        j["timeSaved"] = i.time_saved_ms;
        j["percentage"] = i.percentage;
        j["filesAffected"] = i.files_affected;
        j["complexity"] = static_cast<int>(i.complexity);
    }

    /// Serialize `Suggestion`.
    inline void to_json(json& j, const Suggestion& s) {
        j["id"] = s.id;
        j["type"] = static_cast<int>(s.type);
        j["priority"] = static_cast<int>(s.priority);
        j["title"] = s.title;
        j["description"] = s.description;
        json impact_json;
        to_json(impact_json, s.estimated_impact);
        j["estimatedImpact"] = impact_json;
        j["confidence"] = s.confidence;
        j["autoApplicable"] = s.auto_applicable;
        if (s.application_mode) {
            j["applicationMode"] = *s.application_mode;
        }
        if (s.refactor_class_name) {
            j["refactorClassName"] = *s.refactor_class_name;
        }
        if (s.compile_commands_path) {
            j["compileCommandsPath"] = *s.compile_commands_path;
        }
        if (s.target_uri) {
            j["targetUri"] = *s.target_uri;
        }
        if (s.range) {
            json range_json;
            to_json(range_json, *s.range);
            j["range"] = range_json;
        }
        if (s.application_summary) {
            j["applicationSummary"] = *s.application_summary;
        }
        if (s.application_guidance) {
            j["applicationGuidance"] = *s.application_guidance;
        }
        if (s.auto_apply_blocked_reason) {
            j["autoApplyBlockedReason"] = *s.auto_apply_blocked_reason;
        }
        if (s.project_context) {
            j["projectContext"] = *s.project_context;
        }
        if (!s.module_rules_files.empty()) {
            j["moduleRulesFiles"] = s.module_rules_files;
        }
        if (!s.target_rules_files.empty()) {
            j["targetRulesFiles"] = s.target_rules_files;
        }
        if (s.safety_guard) {
            j["safetyGuard"] = *s.safety_guard;
        }
    }

    /// Serialize `BuildMetrics`.
    inline void to_json(json& j, const BuildMetrics& m) {
        j["totalDurationMs"] = m.total_duration_ms;
        j["totalDuration"] = m.total_duration_ms;
        j["filesCompiled"] = m.files_compiled;
        j["filesUpToDate"] = m.files_up_to_date;

        json slowest = json::array();
        for (const auto& f : m.slowest_files) {
            json file_metric;
            file_metric["file"] = f.file;
            file_metric["duration"] = f.duration_ms;
            file_metric["percentage"] = f.percentage;
            slowest.push_back(file_metric);
        }
        j["slowestFiles"] = slowest;
    }
}
