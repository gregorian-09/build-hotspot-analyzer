#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <nlohmann/json.hpp>

namespace bha::lsp
{
    using json = nlohmann::json;

    struct Position {
        int line;
        int character;
    };

    struct Range {
        Position start;
        Position end;
    };

    struct Location {
        std::string uri;
        Range range;
    };

    struct TextEdit {
        Range range;
        std::string new_text;
    };

    enum class MessageType {
        Error = 1,
        Warning = 2,
        Info = 3,
        Log = 4
    };

    enum class DiagnosticSeverity {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    struct Diagnostic {
        Range range;
        DiagnosticSeverity severity;
        std::optional<std::string> code;
        std::optional<std::string> source;
        std::string message;
    };

    inline void to_json(json& j, const Diagnostic& d);

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

    enum class Priority {
        High,
        Medium,
        Low
    };

    enum class Complexity {
        Trivial,
        Simple,
        Moderate,
        Complex
    };

    struct Impact {
        int time_saved_ms;
        double percentage;
        int files_affected;
        Complexity complexity;
    };

    struct Suggestion {
        std::string id;
        SuggestionType type;
        Priority priority;
        std::string title;
        std::string description;
        Impact estimated_impact;
        double confidence;
        bool auto_applicable;
    };

    struct DetailedSuggestion : Suggestion {
        std::string rationale;
        std::vector<std::string> files_to_create;
        std::vector<std::string> files_to_modify;
        std::vector<std::string> dependencies;
    };

    struct FileChange {
        std::string file;
        enum class Type { Create, Modify, Delete } type;
        std::optional<std::string> content;
        std::vector<TextEdit> edits;
    };

    struct BuildMetrics {
        int total_duration_ms;
        int files_compiled;
        int files_up_to_date;
        struct FileMetric {
            std::string file;
            int duration_ms;
            double percentage;
        };
        std::vector<FileMetric> slowest_files;
    };

    struct ValidationResult {
        bool valid;
        bool syntax_valid;
        bool semantics_valid;
        bool build_system_valid;
        std::vector<Diagnostic> errors;
        std::vector<Diagnostic> warnings;
    };

    struct BuildResult {
        bool success;
        BuildMetrics metrics;
        std::optional<std::string> output;
        std::vector<Diagnostic> errors;
        std::vector<Diagnostic> warnings;
    };

    inline void to_json(json& j, const Position& p) {
        j["line"] = p.line;
        j["character"] = p.character;
    }

    inline void from_json(const json& j, Position& p) {
        j.at("line").get_to(p.line);
        j.at("character").get_to(p.character);
    }

    inline void to_json(json& j, const Range& r) {
        json start_json;
        to_json(start_json, r.start);
        j["start"] = start_json;
        json end_json;
        to_json(end_json, r.end);
        j["end"] = end_json;
    }

    inline void from_json(const json& j, Range& r) {
        j.at("start").get_to(r.start);
        j.at("end").get_to(r.end);
    }

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

    inline void to_json(json& j, const Impact& i) {
        j["timeSaved"] = i.time_saved_ms;
        j["percentage"] = i.percentage;
        j["filesAffected"] = i.files_affected;
        j["complexity"] = static_cast<int>(i.complexity);
    }

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
    }

    inline void to_json(json& j, const BuildMetrics& m) {
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
