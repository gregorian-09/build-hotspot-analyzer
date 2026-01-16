//
// Created by gregorian-rayne on 1/2/26.
//

#include "bha/storage.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace bha::storage
{

    namespace {

        /**
         * Formats a timestamp to ISO 8601.
         */
        std::string format_timestamp(const Timestamp ts) {
            const auto time_t_val = std::chrono::system_clock::to_time_t(ts);
            std::ostringstream ss;

#ifdef _WIN32
            std::tm time_info;
            gmtime_s(&time_info, &time_t_val);
            ss << std::put_time(&time_info, "%Y-%m-%dT%H:%M:%SZ");
#else
            ss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");
#endif

            return ss.str();
        }

        /**
         * Parses ISO 8601 timestamp.
         */

        Timestamp parse_timestamp(const std::string& str) {
            std::tm tm = {};
            std::istringstream ss(str);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            if (ss.fail()) {
                return std::chrono::system_clock::now();
            }
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        /**
         * Converts Duration to milliseconds for JSON.
         */
        double duration_to_ms(const Duration d) {
            return static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(d).count()
            ) / 1000.0;
        }

        /**
         * Converts milliseconds to Duration.
         */
        Duration ms_to_duration(const double ms) {
            return std::chrono::duration_cast<Duration>(
                std::chrono::microseconds(static_cast<int64_t>(ms * 1000))
            );
        }

        /**
         * Executes a command and returns output.
         */
        std::string execute_command(const std::string& cmd) {
            std::string result;

#ifdef _WIN32
            FILE* pipe = _popen(cmd.c_str(), "r");
#else
            FILE* pipe = popen(cmd.c_str(), "r");
#endif

            if (!pipe) return "";

            char buffer[128];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }

#ifdef _WIN32
            _pclose(pipe);
#else
            pclose(pipe);
#endif

            while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
                result.pop_back();
            }

            return result;
        }

        /**
         * Serializes a file analysis result to JSON.
         */
        nlohmann::json serialize_file_result(const analyzers::FileAnalysisResult& file) {
            nlohmann::json j;
            j["path"] = file.file.string();
            j["compile_time_ms"] = duration_to_ms(file.compile_time);
            j["frontend_time_ms"] = duration_to_ms(file.frontend_time);
            j["backend_time_ms"] = duration_to_ms(file.backend_time);
            j["time_percent"] = file.time_percent;
            j["rank"] = file.rank;
            j["include_count"] = file.include_count;
            j["template_count"] = file.template_count;
            return j;
        }

        /**
         * Deserializes a file analysis result from JSON.
         */
        analyzers::FileAnalysisResult deserialize_file_result(const nlohmann::json& j) {
            analyzers::FileAnalysisResult file;
            file.file = j.value("path", "");
            file.compile_time = ms_to_duration(j.value("compile_time_ms", 0.0));
            file.frontend_time = ms_to_duration(j.value("frontend_time_ms", 0.0));
            file.backend_time = ms_to_duration(j.value("backend_time_ms", 0.0));
            file.time_percent = j.value("time_percent", 0.0);
            file.rank = j.value("rank", std::size_t{0});
            file.include_count = j.value("include_count", std::size_t{0});
            file.template_count = j.value("template_count", std::size_t{0});
            return file;
        }

        /**
         * Serializes dependency analysis to JSON.
         */
        nlohmann::json serialize_dependencies(const analyzers::DependencyAnalysisResult& deps) {
            nlohmann::json j;
            j["total_includes"] = deps.total_includes;
            j["unique_headers"] = deps.unique_headers;
            j["max_include_depth"] = deps.max_include_depth;
            j["total_include_time_ms"] = duration_to_ms(deps.total_include_time);

            nlohmann::json headers = nlohmann::json::array();
            for (const auto& h : deps.headers) {
                nlohmann::json hj;
                hj["path"] = h.path.string();
                hj["total_parse_time_ms"] = duration_to_ms(h.total_parse_time);
                hj["inclusion_count"] = h.inclusion_count;
                hj["including_files"] = h.including_files;
                hj["impact_score"] = h.impact_score;
                headers.push_back(hj);
            }
            j["headers"] = headers;

            return j;
        }

        /**
         * Deserializes dependency analysis from JSON.
         */
        analyzers::DependencyAnalysisResult deserialize_dependencies(const nlohmann::json& j) {
            analyzers::DependencyAnalysisResult deps;
            deps.total_includes = j.value("total_includes", std::size_t{0});
            deps.unique_headers = j.value("unique_headers", std::size_t{0});
            deps.max_include_depth = j.value("max_include_depth", std::size_t{0});
            deps.total_include_time = ms_to_duration(j.value("total_include_time_ms", 0.0));

            if (j.contains("headers")) {
                for (const auto& hj : j["headers"]) {
                    analyzers::DependencyAnalysisResult::HeaderInfo h;
                    h.path = hj.value("path", "");
                    h.total_parse_time = ms_to_duration(hj.value("total_parse_time_ms", 0.0));
                    h.inclusion_count = hj.value("inclusion_count", std::size_t{0});
                    h.including_files = hj.value("including_files", std::size_t{0});
                    h.impact_score = hj.value("impact_score", 0.0);
                    deps.headers.push_back(h);
                }
            }

            return deps;
        }

        /**
         * Serializes template analysis to JSON.
         */
        nlohmann::json serialize_templates(const analyzers::TemplateAnalysisResult& tmpl) {
            nlohmann::json j;
            j["total_template_time_ms"] = duration_to_ms(tmpl.total_template_time);
            j["template_time_percent"] = tmpl.template_time_percent;
            j["total_instantiations"] = tmpl.total_instantiations;

            nlohmann::json templates = nlohmann::json::array();
            for (const auto& t : tmpl.templates) {
                nlohmann::json tj;
                tj["name"] = t.name;
                tj["full_signature"] = t.full_signature;
                tj["total_time_ms"] = duration_to_ms(t.total_time);
                tj["instantiation_count"] = t.instantiation_count;
                tj["time_percent"] = t.time_percent;
                templates.push_back(tj);
            }
            j["templates"] = templates;

            return j;
        }

        /**
         * Deserializes template analysis from JSON.
         */
        analyzers::TemplateAnalysisResult deserialize_templates(const nlohmann::json& j) {
            analyzers::TemplateAnalysisResult tmpl;
            tmpl.total_template_time = ms_to_duration(j.value("total_template_time_ms", 0.0));
            tmpl.template_time_percent = j.value("template_time_percent", 0.0);
            tmpl.total_instantiations = j.value("total_instantiations", std::size_t{0});

            if (j.contains("templates")) {
                for (const auto& tj : j["templates"]) {
                    analyzers::TemplateAnalysisResult::TemplateInfo t;
                    t.name = tj.value("name", "");
                    t.full_signature = tj.value("full_signature", "");
                    t.total_time = ms_to_duration(tj.value("total_time_ms", 0.0));
                    t.instantiation_count = tj.value("instantiation_count", std::size_t{0});
                    t.time_percent = tj.value("time_percent", 0.0);
                    tmpl.templates.push_back(t);
                }
            }

            return tmpl;
        }

        /**
         * Serializes performance analysis to JSON.
         */
        nlohmann::json serialize_performance(const analyzers::PerformanceAnalysisResult& perf) {
            nlohmann::json j;
            j["total_build_time_ms"] = duration_to_ms(perf.total_build_time);
            j["sequential_time_ms"] = duration_to_ms(perf.sequential_time);
            j["parallel_time_ms"] = duration_to_ms(perf.parallel_time);
            j["parallelism_efficiency"] = perf.parallelism_efficiency;
            j["total_files"] = perf.total_files;
            j["avg_file_time_ms"] = duration_to_ms(perf.avg_file_time);
            j["median_file_time_ms"] = duration_to_ms(perf.median_file_time);
            j["p90_file_time_ms"] = duration_to_ms(perf.p90_file_time);
            j["p99_file_time_ms"] = duration_to_ms(perf.p99_file_time);
            return j;
        }

        /**
         * Deserializes performance analysis from JSON.
         */
        analyzers::PerformanceAnalysisResult deserialize_performance(const nlohmann::json& j) {
            analyzers::PerformanceAnalysisResult perf;
            perf.total_build_time = ms_to_duration(j.value("total_build_time_ms", 0.0));
            perf.sequential_time = ms_to_duration(j.value("sequential_time_ms", 0.0));
            perf.parallel_time = ms_to_duration(j.value("parallel_time_ms", 0.0));
            perf.parallelism_efficiency = j.value("parallelism_efficiency", 0.0);
            perf.total_files = j.value("total_files", std::size_t{0});
            perf.avg_file_time = ms_to_duration(j.value("avg_file_time_ms", 0.0));
            perf.median_file_time = ms_to_duration(j.value("median_file_time_ms", 0.0));
            perf.p90_file_time = ms_to_duration(j.value("p90_file_time_ms", 0.0));
            perf.p99_file_time = ms_to_duration(j.value("p99_file_time_ms", 0.0));
            return perf;
        }

        /**
         * Serializes a suggestion to JSON.
         */
        nlohmann::json serialize_suggestion(const Suggestion& sugg) {
            nlohmann::json j;
            j["type"] = sugg.type;
            j["title"] = sugg.title;
            j["description"] = sugg.description;
            j["target_file"] = sugg.target_file.path.string();
            j["target_line"] = sugg.target_file.line_start;
            j["confidence"] = sugg.confidence;
            j["priority"] = static_cast<int>(sugg.priority);
            j["estimated_savings_ms"] = duration_to_ms(sugg.estimated_savings);
            j["is_safe"] = sugg.is_safe;
            return j;
        }

        /**
         * Deserializes a suggestion from JSON.
         */
        Suggestion deserialize_suggestion(const nlohmann::json& j) {
            Suggestion sugg;
            sugg.type = static_cast<SuggestionType>(j.value("type", 0));
            sugg.title = j.value("title", "");
            sugg.description = j.value("description", "");
            sugg.target_file.path = j.value("target_file", "");
            sugg.target_file.line_start = j.value("target_line", std::size_t{0});
            sugg.confidence = j.value("confidence", 0.0);
            sugg.priority = static_cast<Priority>(j.value("priority", 0));
            sugg.estimated_savings = ms_to_duration(j.value("estimated_savings_ms", 0.0));
            sugg.is_safe = j.value("is_safe", false);
            return sugg;
        }

    }  // namespace

    // =============================================================================
    // SnapshotStore Implementation
    // =============================================================================

    SnapshotStore::SnapshotStore(const fs::path& root)
        : root_(root) {}

    Result<void, Error> SnapshotStore::ensure_directory() const {
        try {
            if (!fs::exists(root_)) {
                fs::create_directories(root_);
            }
            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to create storage directory: ") + e.what())
            );
        }
    }

    std::string SnapshotStore::get_git_commit()
    {
        return execute_command("git rev-parse HEAD 2>/dev/null");
    }

    std::string SnapshotStore::get_git_branch()
    {
        return execute_command("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    }

    Result<void, Error> SnapshotStore::save(
        const std::string& name,
        const analyzers::AnalysisResult& analysis,
        const std::vector<Suggestion>& suggestions,
        const std::string& description,
        const std::vector<std::string>& tags
    ) const
    {
        if (auto dir_result = ensure_directory(); dir_result.is_err()) {
            return dir_result;
        }

        nlohmann::json j;

        // Metadata
        j["version"] = "2.0";
        j["name"] = name;
        j["description"] = description;
        j["created_at"] = format_timestamp(std::chrono::system_clock::now());
        j["git_commit"] = get_git_commit();
        j["git_branch"] = get_git_branch();
        j["file_count"] = analysis.files.size();
        j["total_build_time_ms"] = duration_to_ms(analysis.performance.total_build_time);
        j["tags"] = tags;

        j["performance"] = serialize_performance(analysis.performance);

        nlohmann::json files = nlohmann::json::array();
        for (const auto& file : analysis.files) {
            files.push_back(serialize_file_result(file));
        }
        j["files"] = files;

        j["dependencies"] = serialize_dependencies(analysis.dependencies);

        j["templates"] = serialize_templates(analysis.templates);

        nlohmann::json sugg_array = nlohmann::json::array();
        for (const auto& sugg : suggestions) {
            sugg_array.push_back(serialize_suggestion(sugg));
        }
        j["suggestions"] = sugg_array;

        fs::path path = snapshot_path(name);
        try {
            std::ofstream file(path);
            if (!file.is_open()) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::IoError, "Failed to open snapshot file for writing: " + path.string())
                );
            }
            file << std::setw(2) << j << std::endl;
            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to write snapshot: ") + e.what())
            );
        }
    }

    Result<Snapshot, Error> SnapshotStore::load(const std::string& name) const {
        fs::path path = snapshot_path(name);

        if (!fs::exists(path)) {
            return Result<Snapshot, Error>::failure(
                Error(ErrorCode::NotFound, "Snapshot not found: " + name)
            );
        }

        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return Result<Snapshot, Error>::failure(
                    Error(ErrorCode::IoError, "Failed to open snapshot file: " + path.string())
                );
            }

            nlohmann::json j;
            file >> j;

            Snapshot snapshot;

            snapshot.metadata.name = j.value("name", name);
            snapshot.metadata.description = j.value("description", "");
            snapshot.metadata.created_at = parse_timestamp(j.value("created_at", ""));
            snapshot.metadata.git_commit = j.value("git_commit", "");
            snapshot.metadata.git_branch = j.value("git_branch", "");
            snapshot.metadata.file_count = j.value("file_count", std::size_t{0});
            snapshot.metadata.total_build_time = ms_to_duration(j.value("total_build_time_ms", 0.0));

            if (j.contains("tags")) {
                for (const auto& tag : j["tags"]) {
                    snapshot.metadata.tags.push_back(tag.get<std::string>());
                }
            }

            if (j.contains("performance")) {
                snapshot.analysis.performance = deserialize_performance(j["performance"]);
            }

            if (j.contains("files")) {
                for (const auto& fj : j["files"]) {
                    snapshot.analysis.files.push_back(deserialize_file_result(fj));
                }
            }

            if (j.contains("dependencies")) {
                snapshot.analysis.dependencies = deserialize_dependencies(j["dependencies"]);
            }

            if (j.contains("templates")) {
                snapshot.analysis.templates = deserialize_templates(j["templates"]);
            }

            if (j.contains("suggestions")) {
                for (const auto& sj : j["suggestions"]) {
                    snapshot.suggestions.push_back(deserialize_suggestion(sj));
                }
            }

            return Result<Snapshot, Error>::success(std::move(snapshot));
        } catch (const nlohmann::json::exception& e) {
            return Result<Snapshot, Error>::failure(
                Error(ErrorCode::ParseError, std::string("Failed to parse snapshot JSON: ") + e.what())
            );
        } catch (const std::exception& e) {
            return Result<Snapshot, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to load snapshot: ") + e.what())
            );
        }
    }

    Result<std::vector<SnapshotMetadata>, Error> SnapshotStore::list() const {
        std::vector<SnapshotMetadata> snapshots;

        if (!fs::exists(root_)) {
            return Result<std::vector<SnapshotMetadata>, Error>::success(snapshots);
        }

        try {
            for (const auto& entry : fs::directory_iterator(root_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::string name = entry.path().stem().string();

                    if (name == ".baseline") continue;

                    if (auto result = load(name); result.is_ok()) {
                        snapshots.push_back(result.value().metadata);
                    }
                }
            }

            std::ranges::sort(snapshots,
                              [](const SnapshotMetadata& a, const SnapshotMetadata& b) {
                                  return a.created_at > b.created_at;
                              });

            return Result<std::vector<SnapshotMetadata>, Error>::success(std::move(snapshots));
        } catch (const std::exception& e) {
            return Result<std::vector<SnapshotMetadata>, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to list snapshots: ") + e.what())
            );
        }
    }

    Result<void, Error> SnapshotStore::remove(const std::string& name) const
    {
        const fs::path path = snapshot_path(name);

        if (!fs::exists(path)) {
            return Result<void, Error>::failure(
                Error(ErrorCode::NotFound, "Snapshot not found: " + name)
            );
        }

        try {
            fs::remove(path);

            // Clear baseline if this was the baseline
            auto baseline = get_baseline();
            if (baseline && *baseline == name) {
                (void)clear_baseline();
            }

            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to remove snapshot: ") + e.what())
            );
        }
    }

    bool SnapshotStore::exists(const std::string& name) const {
        return fs::exists(snapshot_path(name));
    }

    fs::path SnapshotStore::snapshot_path(const std::string& name) const {
        return root_ / (name + ".json");
    }

    Result<void, Error> SnapshotStore::set_baseline(const std::string& name) const
    {
        if (!exists(name)) {
            return Result<void, Error>::failure(
                Error(ErrorCode::NotFound, "Snapshot not found: " + name)
            );
        }

        if (auto dir_result = ensure_directory(); dir_result.is_err()) {
            return dir_result;
        }

        try {
            std::ofstream file(baseline_file());
            file << name;
            return Result<void, Error>::success();
        } catch (const std::exception& e) {
            return Result<void, Error>::failure(
                Error(ErrorCode::IoError, std::string("Failed to set baseline: ") + e.what())
            );
        }
    }

    std::optional<std::string> SnapshotStore::get_baseline() const {
        if (!fs::exists(baseline_file())) {
            return std::nullopt;
        }

        try {
            std::ifstream file(baseline_file());
            std::string name;
            std::getline(file, name);
            if (!name.empty() && exists(name)) {
                return name;
            }
            return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

    Result<void, Error> SnapshotStore::clear_baseline() const
    {
        if (fs::exists(baseline_file())) {
            try {
                fs::remove(baseline_file());
            } catch (const std::exception& e) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::IoError, std::string("Failed to clear baseline: ") + e.what())
                );
            }
        }
        return Result<void, Error>::success();
    }

    Result<ComparisonResult, Error> SnapshotStore::compare(
        const std::string& old_name,
        const std::string& new_name
    ) const {
        auto old_result = load(old_name);
        if (old_result.is_err()) {
            return Result<ComparisonResult, Error>::failure(old_result.error());
        }

        auto new_result = load(new_name);
        if (new_result.is_err()) {
            return Result<ComparisonResult, Error>::failure(new_result.error());
        }

        return Result<ComparisonResult, Error>::success(
            compare_analyses(old_result.value().analysis, new_result.value().analysis)
        );
    }

    Result<ComparisonResult, Error> SnapshotStore::compare_with_analysis(
        const std::string& snapshot_name,
        const analyzers::AnalysisResult& current
    ) const {
        auto snapshot_result = load(snapshot_name);
        if (snapshot_result.is_err()) {
            return Result<ComparisonResult, Error>::failure(snapshot_result.error());
        }

        return Result<ComparisonResult, Error>::success(
            compare_analyses(snapshot_result.value().analysis, current)
        );
    }

    // =============================================================================
    // Comparison Functions
    // =============================================================================

    ComparisonResult compare_analyses(
        const analyzers::AnalysisResult& old_result,
        const analyzers::AnalysisResult& new_result,
        double significance_threshold
    ) {
        ComparisonResult result;

        // Overall build time change
        auto old_time = old_result.performance.total_build_time;
        auto new_time = new_result.performance.total_build_time;
        result.build_time_delta = new_time - old_time;

        if (old_time.count() > 0) {
            result.build_time_percent_change =
                100.0 * static_cast<double>(result.build_time_delta.count()) /
                static_cast<double>(old_time.count());
        } else {
            result.build_time_percent_change = 0.0;
        }

        // File count change
        result.file_count_delta =
            static_cast<int64_t>(new_result.files.size()) -
            static_cast<int64_t>(old_result.files.size());

        std::unordered_map<std::string, const analyzers::FileAnalysisResult*> old_files;
        std::unordered_map<std::string, const analyzers::FileAnalysisResult*> new_files;

        for (const auto& file : old_result.files) {
            old_files[file.file.string()] = &file;
        }
        for (const auto& file : new_result.files) {
            new_files[file.file.string()] = &file;
        }

        // Find regressions, improvements, new files, removed files
        for (const auto& [path, old_file] : old_files) {
            if (auto it = new_files.find(path); it == new_files.end()) {
                result.removed_files.emplace_back(path);
            } else {
                const auto* new_file = it->second;
                auto delta = new_file->compile_time - old_file->compile_time;
                double percent = 0.0;
                if (old_file->compile_time.count() > 0) {
                    percent = 100.0 * static_cast<double>(delta.count()) /
                        static_cast<double>(old_file->compile_time.count());
                }

                if (std::abs(percent) > significance_threshold * 100.0) {
                    ComparisonResult::FileChange change;
                    change.file = path;
                    change.old_time = old_file->compile_time;
                    change.new_time = new_file->compile_time;
                    change.delta = delta;
                    change.percent_change = percent;

                    if (delta.count() > 0) {
                        result.regressions.push_back(change);
                    } else {
                        result.improvements.push_back(change);
                    }
                }
            }
        }

        for (const auto& path : new_files | std::views::keys) {
            if (!old_files.contains(path)) {
                result.new_files.emplace_back(path);
            }
        }

        auto sort_by_magnitude = [](const ComparisonResult::FileChange& a,
                                    const ComparisonResult::FileChange& b) {
            return std::abs(a.delta.count()) > std::abs(b.delta.count());
        };
        std::ranges::sort(result.regressions, sort_by_magnitude);
        std::ranges::sort(result.improvements, sort_by_magnitude);

        std::unordered_map<std::string, const analyzers::DependencyAnalysisResult::HeaderInfo*> old_headers;
        std::unordered_map<std::string, const analyzers::DependencyAnalysisResult::HeaderInfo*> new_headers;

        for (const auto& h : old_result.dependencies.headers) {
            old_headers[h.path.string()] = &h;
        }
        for (const auto& h : new_result.dependencies.headers) {
            new_headers[h.path.string()] = &h;
        }

        for (const auto& [path, old_h] : old_headers) {
            if (auto it = new_headers.find(path); it != new_headers.end()) {
                if (const auto* new_h = it->second; old_h->inclusion_count != new_h->inclusion_count ||
                    old_h->total_parse_time != new_h->total_parse_time) {
                    ComparisonResult::HeaderChange change;
                    change.header = path;
                    change.old_inclusions = old_h->inclusion_count;
                    change.new_inclusions = new_h->inclusion_count;
                    change.old_time = old_h->total_parse_time;
                    change.new_time = new_h->total_parse_time;

                    if (new_h->inclusion_count > old_h->inclusion_count ||
                        new_h->total_parse_time > old_h->total_parse_time) {
                        result.header_regressions.push_back(change);
                    } else {
                        result.header_improvements.push_back(change);
                    }
                }
            }
        }

        std::unordered_map<std::string, const analyzers::TemplateAnalysisResult::TemplateInfo*> old_templates;
        std::unordered_map<std::string, const analyzers::TemplateAnalysisResult::TemplateInfo*> new_templates;

        for (const auto& t : old_result.templates.templates) {
            old_templates[t.name] = &t;
        }
        for (const auto& t : new_result.templates.templates) {
            new_templates[t.name] = &t;
        }

        for (const auto& [name, old_t] : old_templates) {
            if (auto it = new_templates.find(name); it != new_templates.end()) {
                if (const auto* new_t = it->second; old_t->instantiation_count != new_t->instantiation_count ||
                    old_t->total_time != new_t->total_time) {
                    ComparisonResult::TemplateChange change;
                    change.name = name;
                    change.old_count = old_t->instantiation_count;
                    change.new_count = new_t->instantiation_count;
                    change.old_time = old_t->total_time;
                    change.new_time = new_t->total_time;

                    if (new_t->instantiation_count > old_t->instantiation_count ||
                        new_t->total_time > old_t->total_time) {
                        result.template_regressions.push_back(change);
                    } else {
                        result.template_improvements.push_back(change);
                    }
                }
            }
        }

        return result;
    }
}
