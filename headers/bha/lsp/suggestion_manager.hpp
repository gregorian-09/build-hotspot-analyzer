#pragma once

#include "types.hpp"
#include "bha/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace bha::lsp
{
    namespace fs = std::filesystem;

    struct AnalysisResult {
        std::string analysis_id;
        std::vector<Suggestion> suggestions;
        BuildMetrics baseline_metrics;
        int files_analyzed;
        int duration_ms;
    };

    struct ApplySuggestionResult {
        bool success;
        std::vector<std::string> changed_files;
        std::optional<BuildResult> build_result;
        std::optional<ValidationResult> validation;
        std::vector<Diagnostic> errors;
        std::optional<std::string> backup_id;
    };

    struct FileBackup {
        fs::path path;
        std::string content;
    };

    struct Backup {
        std::string id;
        std::vector<FileBackup> files;
        std::chrono::system_clock::time_point timestamp;
    };

    class SuggestionManager {
    public:
        SuggestionManager();

        AnalysisResult analyze_project(
            const fs::path& project_root,
            const std::optional<fs::path>& build_dir = std::nullopt,
            bool rebuild = false
        );

        DetailedSuggestion get_suggestion_details(const std::string& suggestion_id);

        ApplySuggestionResult apply_suggestion(
            const std::string& suggestion_id,
            bool skip_validation = false,
            bool skip_rebuild = false,
            bool create_backup = true
        );

        ApplySuggestionResult apply_all_suggestions(
            const std::vector<std::string>& suggestion_ids,
            bool stop_on_error = true
        );

        bool revert_changes(const std::string& backup_id);

        [[nodiscard]] std::vector<Suggestion> get_all_suggestions() const;
        [[nodiscard]] std::optional<Suggestion> get_suggestion(const std::string& id) const;

    private:
        static BuildMetrics extract_build_metrics(const BuildTrace& trace);

        static Priority calculate_priority(double improvement_percentage) ;

        static Suggestion convert_suggestion(const bha::Suggestion& bha_sug);
        static DetailedSuggestion convert_to_detailed(const bha::Suggestion& bha_sug);

        std::string generate_analysis_id();
        std::string generate_backup_id();

        std::string create_backup(const std::vector<fs::path>& files);
        static bool validate_files_exist(const std::vector<fs::path>& files);
        static bool apply_file_changes(const bha::Suggestion& suggestion, std::vector<fs::path>& changed_files);

        std::map<std::string, DetailedSuggestion> suggestions_;
        std::map<std::string, BuildTrace> analysis_cache_;
        std::map<std::string, bha::Suggestion> bha_suggestions_;
        std::map<std::string, Backup> backups_;
        std::string last_analysis_id_;

        int analysis_counter_{0};
        int backup_counter_{0};
    };
}
