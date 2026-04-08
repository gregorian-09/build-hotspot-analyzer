//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_TYPES_HPP
#define BUILDTIMEHOTSPOTANALYZER_TYPES_HPP

/**
 * @file types.hpp
 * @brief Core data structures for build trace analysis.
 *
 * This header defines all fundamental types used throughout the Build Hotspot
 * Analyzer. Types are organized into categories:
 *
 * - Basic Types: Duration, Timestamp, SourceLocation
 * - Build Trace Data: CompilationUnit, BuildTrace, IncludeInfo, etc.
 * - Suggestion Data: Suggestion, FileTarget, CodeExample, Impact
 * - Git Integration: GitInfo, CommitImpact, AuthorStats, Blame data
 *
 * All types are designed to be:
 * - Move-friendly for efficient transfer
 * - Serializable to JSON
 * - Suitable for parallel processing
 */

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <filesystem>
#include <string_view>

#include "heuristics/config.hpp"

namespace bha {

    namespace fs = std::filesystem;

    // ============================================================================
    // Basic Types
    // ============================================================================

    /**
     * Duration in nanoseconds for high-precision timing measurements.
     *
     * Using nanoseconds as the base unit allows accurate representation
     * of very short operations while still supporting durations up to
     * several hundred years.
     */
    using Duration = std::chrono::nanoseconds;

    /**
     * Timestamp for absolute time points.
     */
    using Timestamp = std::chrono::system_clock::time_point;

    /**
     * Source code location.
     */
    struct SourceLocation {
        fs::path file{};
        std::size_t line = 0;
        std::size_t column = 0;

        [[nodiscard]] bool has_location() const noexcept {
            return !file.empty() && line > 0;
        }
    };

    // ============================================================================
    // Compiler and Build System Types
    // ============================================================================

    /**
     * Compiler identification.
     */
    enum class CompilerType {
        Unknown,
        Clang,
        GCC,
        MSVC,
        IntelClassic,   // icc/icpc
        IntelOneAPI,    // icx/icpx
        NVCC,
        ArmClang,
        AppleClang
    };

    /**
     * Build system identification.
     */
    enum class BuildSystemType {
        Unknown,
        CMake,
        Ninja,
        Make,
        MSBuild,
        Bazel,
        Buck2,
        Meson,
        SCons,
        XCode
    };

    /**
     * Converts CompilerType to string.
     */
    inline const char* to_string(CompilerType type) noexcept {
        switch (type) {
            case CompilerType::Unknown:      return "Unknown";
            case CompilerType::Clang:        return "Clang";
            case CompilerType::GCC:          return "GCC";
            case CompilerType::MSVC:         return "MSVC";
            case CompilerType::IntelClassic: return "Intel ICC";
            case CompilerType::IntelOneAPI:  return "Intel ICX";
            case CompilerType::NVCC:         return "NVCC";
            case CompilerType::ArmClang:     return "ARM Clang";
            case CompilerType::AppleClang:   return "Apple Clang";
        }
        return "Unknown";
    }

    /**
     * Converts BuildSystemType to string.
     */
    inline const char* to_string(BuildSystemType type) noexcept {
        switch (type) {
            case BuildSystemType::Unknown: return "Unknown";
            case BuildSystemType::CMake:   return "CMake";
            case BuildSystemType::Ninja:   return "Ninja";
            case BuildSystemType::Make:    return "Make";
            case BuildSystemType::MSBuild: return "MSBuild";
            case BuildSystemType::Bazel:   return "Bazel";
            case BuildSystemType::Buck2:   return "Buck2";
            case BuildSystemType::Meson:   return "Meson";
            case BuildSystemType::SCons:   return "SCons";
            case BuildSystemType::XCode:   return "XCode";
        }
        return "Unknown";
    }

    // ============================================================================
    // Build Trace Data
    // ============================================================================

    /**
     * Breakdown of compilation time by phase.
     *
     * Not all compilers provide all phases. Fields may be zero if
     * the compiler doesn't report that particular metric.
     */
    struct TimeBreakdown {
        Duration preprocessing = Duration::zero();
        Duration parsing = Duration::zero();
        Duration semantic_analysis = Duration::zero();
        Duration template_instantiation = Duration::zero();
        Duration code_generation = Duration::zero();
        Duration optimization = Duration::zero();

        [[nodiscard]] Duration total() const noexcept {
            return preprocessing + parsing + semantic_analysis +
                   template_instantiation + code_generation + optimization;
        }
    };

    /**
     * Memory metrics from compiler stack usage output.
     *
     * Compilers like GCC/Clang with -fstack-usage generate .su files containing
     * per-function stack usage. This is the only reliable memory metric available
     * from standard compiler outputs.
     */
    struct MemoryMetrics {
        std::size_t max_stack_bytes = 0;

        [[nodiscard]] bool has_data() const noexcept {
            return max_stack_bytes > 0;
        }
    };

    /**
     * Metrics for a single source file.
     */
    struct FileMetrics {
        fs::path path{};
        Duration total_time = Duration::zero();
        Duration frontend_time = Duration::zero();
        Duration backend_time = Duration::zero();
        TimeBreakdown breakdown;
        MemoryMetrics memory;

        std::size_t preprocessed_lines = 0;
        double expansion_ratio = 0.0;

        std::size_t direct_includes = 0;
        std::size_t transitive_includes = 0;
        std::size_t max_include_depth = 0;
    };

    /**
     * Information about a template instantiation.
     */
    struct TemplateInstantiation {
        std::string name;
        std::string full_signature;
        std::vector<std::string> type_arguments;
        Duration time = Duration::zero();
        SourceLocation location;
        std::size_t count = 1;
    };

    /**
     * Information about an included header.
     */
    struct IncludeInfo {
        fs::path header;
        Duration parse_time = Duration::zero();
        std::size_t depth = 0;
        std::vector<fs::path> included_by;
        std::vector<std::string> symbols_used;
    };

    /**
     * A single compilation unit (source file + all its data).
     */
    struct CompilationUnit {
        fs::path source_file;
        FileMetrics metrics;
        std::vector<IncludeInfo> includes;
        std::vector<TemplateInstantiation> templates;
        std::vector<std::string> symbols_defined;
        std::vector<std::string> command_line;
    };

    /**
     * Git repository information at build time.
     */
    struct GitInfo {
        std::string commit_hash;
        std::string branch;
        std::string author;
        std::string author_email;
        Timestamp commit_time;
        std::string message;
        bool is_dirty = false;
    };

    /**
     * Complete build trace data from a single build.
     */
    struct BuildTrace {
        std::string id;
        Timestamp timestamp;
        Duration total_time = Duration::zero();

        CompilerType compiler = CompilerType::Unknown;
        std::string compiler_version;
        BuildSystemType build_system = BuildSystemType::Unknown;
        std::string configuration;
        std::string platform;

        std::optional<GitInfo> git_info;

        std::vector<CompilationUnit> units;

        [[nodiscard]] std::size_t file_count() const noexcept {
            return units.size();
        }
    };

    // ============================================================================
    // Suggestion Data
    // ============================================================================

    /**
     * Types of optimization suggestions.
     */
    enum class SuggestionType {
        ForwardDeclaration,
        HeaderSplit,
        PCHOptimization,
        PIMPLPattern,
        IncludeRemoval,
        MoveToCpp,
        ExplicitTemplate,
        UnityBuild
    };

    /**
     * Converts SuggestionType to string.
     */
    inline const char* to_string(SuggestionType type) noexcept {
        switch (type) {
            case SuggestionType::ForwardDeclaration:    return "Forward Declaration";
            case SuggestionType::HeaderSplit:           return "Header Split";
            case SuggestionType::PCHOptimization:       return "PCH Optimization";
            case SuggestionType::PIMPLPattern:          return "PIMPL Pattern";
            case SuggestionType::IncludeRemoval:        return "Include Removal";
            case SuggestionType::MoveToCpp:             return "Move to CPP";
            case SuggestionType::ExplicitTemplate:      return "Explicit Template";
            case SuggestionType::UnityBuild:            return "Unity Build";
        }
        return "Unknown";
    }

    /**
     * Priority level for suggestions.
     */
    enum class Priority {
        Critical,
        High,
        Medium,
        Low
    };

    /**
     * Converts Priority to string.
     */
    inline const char* to_string(Priority priority) noexcept {
        switch (priority) {
            case Priority::Critical: return "Critical";
            case Priority::High:     return "High";
            case Priority::Medium:   return "Medium";
            case Priority::Low:      return "Low";
        }
        return "Unknown";
    }

    /**
     * How a suggestion should be applied.
     */
    enum class SuggestionApplicationMode {
        Advisory,
        DirectEdits,
        ExternalRefactor
    };

    /**
     * Converts SuggestionApplicationMode to string.
     */
    inline const char* to_string(SuggestionApplicationMode mode) noexcept {
        switch (mode) {
            case SuggestionApplicationMode::Advisory:         return "advisory";
            case SuggestionApplicationMode::DirectEdits:      return "direct-edits";
            case SuggestionApplicationMode::ExternalRefactor: return "external-refactor";
        }
        return "advisory";
    }

    /**
     * Parses SuggestionApplicationMode from string.
     */
    inline SuggestionApplicationMode suggestion_application_mode_from_string(
        const std::string_view value
    ) noexcept {
        if (value == "direct-edits") {
            return SuggestionApplicationMode::DirectEdits;
        }
        if (value == "external-refactor") {
            return SuggestionApplicationMode::ExternalRefactor;
        }
        return SuggestionApplicationMode::Advisory;
    }

    /**
     * Action type for file modifications.
     */
    enum class FileAction {
        Modify,      ///< Modify existing code
        AddInclude,  ///< Add an include directive
        Remove,      ///< Remove code or file
        Create       ///< Create a new file
    };

    /**
     * Converts FileAction to string.
     */
    inline const char* to_string(FileAction action) noexcept {
        switch (action) {
            case FileAction::Modify:     return "MODIFY";
            case FileAction::AddInclude: return "ADD_INCLUDE";
            case FileAction::Remove:     return "REMOVE";
            case FileAction::Create:     return "CREATE";
        }
        return "UNKNOWN";
    }

    /**
     * Identifies a specific location in a file that requires modification.
     *
     * This provides exact targeting so users know precisely which file
     * and lines need to be changed to implement a suggestion.
     * Line and column numbers are 1-based for human readability.
     */
    struct FileTarget {
        fs::path path{};
        std::size_t line_start = 0;
        std::size_t line_end = 0;
        std::size_t col_start = 0;  ///< 1-based column start (0 = unknown)
        std::size_t col_end = 0;    ///< 1-based column end (0 = end of line)
        FileAction action = FileAction::Modify;
        std::optional<std::string> note{};

        [[nodiscard]] bool has_line_range() const noexcept {
            return line_start > 0;
        }

        [[nodiscard]] bool has_column_range() const noexcept {
            return col_start > 0;
        }
    };

    /**
     * LSP-compatible text edit for automated code modifications.
     *
     * Represents a single edit operation with precise location.
     * Line and column numbers are 0-based to match LSP protocol.
     */
    struct TextEdit {
        fs::path file{};
        std::size_t start_line = 0;    ///< 0-based line number
        std::size_t start_col = 0;     ///< 0-based column (UTF-16 offset)
        std::size_t end_line = 0;      ///< 0-based line number
        std::size_t end_col = 0;       ///< 0-based column (UTF-16 offset)
        std::string new_text{};        ///< Replacement text

        [[nodiscard]] bool is_valid() const noexcept {
            return !file.empty() && (start_line < end_line ||
                   (start_line == end_line && start_col <= end_col));
        }

        [[nodiscard]] bool is_insertion() const noexcept {
            return start_line == end_line && start_col == end_col;
        }
    };

    /**
     * LSP Diagnostic Severity levels.
     */
    enum class DiagnosticSeverity {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    /**
     * Converts Priority to LSP DiagnosticSeverity.
     *
     * Mapping:
     * - Critical -> Warning (not Error, as suggestions aren't failures)
     * - High -> Warning
     * - Medium -> Information
     * - Low -> Hint
     */
    [[nodiscard]] inline DiagnosticSeverity to_diagnostic_severity(const Priority priority) noexcept {
        switch (priority) {
            case Priority::Critical:
            case Priority::High:
                return DiagnosticSeverity::Warning;

            case Priority::Medium:
                return DiagnosticSeverity::Information;

            case Priority::Low:
                return DiagnosticSeverity::Hint;
            }
        return DiagnosticSeverity::Information;
    }

    /**
     * Code example showing before/after state.
     */
    struct CodeExample {
        fs::path file;
        std::size_t line = 0;
        std::string code;
    };

    /**
     * Impact assessment of applying a suggestion.
     */
    struct Impact {
        std::vector<fs::path> files_benefiting{};
        std::size_t total_files_affected = 0;
        Duration cumulative_savings = Duration::zero();
        std::size_t rebuild_files_count = 0;
    };

    /**
     * Evidence describing why a suggestion is expensive.
     */
    struct HotspotOrigin {
        std::string kind;  // include_chain | template_origin
        fs::path source;
        fs::path target;
        Duration estimated_cost = Duration::zero();
        std::vector<std::string> chain;
        std::string note;
    };

    /**
     * A complete optimization suggestion.
     *
     * Suggestions are designed to be actionable with explicit file targeting.
     * Users should know exactly which file(s) to modify, what code to change,
     * and what the expected impact will be.
     */
    struct Suggestion {
        std::string id;
        SuggestionType type = SuggestionType::ForwardDeclaration;
        Priority priority = Priority::Medium;
        double confidence = 0.0;

        std::string title;
        std::string description;
        std::string rationale;

        Duration estimated_savings = Duration::zero();
        double estimated_savings_percent = 0.0;

        FileTarget target_file;
        std::vector<FileTarget> secondary_files;

        CodeExample before_code;
        CodeExample after_code;

        /// LSP-compatible text edits for automated application.
        /// When populated, IDEs can directly apply these edits.
        std::vector<TextEdit> edits;

        std::vector<std::string> implementation_steps;
        Impact impact;

        std::vector<std::string> caveats;
        std::string verification;
        std::optional<std::string> documentation_link;
        std::vector<HotspotOrigin> hotspot_origins;

        bool is_safe = false;
        SuggestionApplicationMode application_mode = SuggestionApplicationMode::Advisory;
        std::optional<std::string> refactor_class_name;
        std::optional<fs::path> refactor_compile_commands_path;
        std::optional<std::string> application_summary;
        std::optional<std::string> application_guidance;
        std::optional<std::string> auto_apply_blocked_reason;
    };

    /**
     * Resolves the effective application mode for a suggestion.
     *
     * Existing auto-applicable suggestions that already expose concrete edits
     * are treated as direct-edits even if they did not explicitly set the mode.
     */
    inline SuggestionApplicationMode resolve_application_mode(
        const Suggestion& suggestion
    ) noexcept {
        if (!suggestion.edits.empty()) {
            return SuggestionApplicationMode::DirectEdits;
        }
        return suggestion.application_mode;
    }

    // ============================================================================
    // Git Integration Data
    // ============================================================================

    /**
     * Impact of a specific commit on build times.
     */
    struct CommitImpact {
        std::string commit_hash;
        std::string author;
        Timestamp timestamp;
        std::string message;

        Duration time_delta = Duration::zero();
        std::vector<fs::path> files_changed;
        std::vector<Suggestion> suggested_fixes;
    };

    /**
     * Build time statistics per author.
     */
    struct AuthorStats {
        std::string author;
        std::string email;
        std::size_t commits = 0;
        std::size_t files_changed = 0;
        Duration time_added = Duration::zero();
        Duration time_saved = Duration::zero();
        Duration net_impact = Duration::zero();
    };

    /**
     * Blame information for a single line of code.
     */
    struct LineBlame {
        std::size_t line_number = 0;
        Duration time_contribution = Duration::zero();
        std::string author;
        std::string commit_hash;
        std::string code;
    };

    /**
     * Blame information for an entire file.
     */
    struct FileBlame {
        fs::path file;
        Duration total_time = Duration::zero();
        std::vector<LineBlame> lines;
        std::vector<Suggestion> suggestions;
    };

    // ============================================================================
    // Configuration Types
    // ============================================================================

    /**
     * Analysis configuration options.
     */
    struct AnalysisOptions {
        std::size_t max_threads = 0;  ///< 0 means auto-detect
        Duration min_duration_threshold = std::chrono::milliseconds(10);
        bool analyze_templates = true;
        bool analyze_includes = true;
        bool analyze_symbols = true;
        bool verbose = false;
        Duration max_total_time = Duration::zero();
        Duration max_analyzer_time = Duration::zero();
    };

    /**
     * Suggestion generation options.
     */
    struct SuggesterOptions {
        std::size_t max_suggestions = 100;
        Priority min_priority = Priority::Low;
        double min_confidence = 0.5;
        bool include_unsafe = false;
        bool enable_consolidation = true;
        bool restrict_to_trace = true;
        Duration max_total_time = Duration::zero();
        Duration max_suggester_time = Duration::zero();
        std::optional<fs::path> compile_commands_path;
        std::vector<std::string> protected_include_patterns;
        std::vector<SuggestionType> enabled_types;
        heuristics::HeuristicsConfig heuristics = heuristics::HeuristicsConfig::defaults();
    };

}  // namespace bha

#endif //BUILDTIMEHOTSPOTANALYZER_TYPES_HPP
