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
        fs::path file;
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

    struct MemoryMetrics {
        std::size_t peak_memory_bytes = 0;
        std::size_t frontend_peak_bytes = 0;
        std::size_t backend_peak_bytes = 0;
        std::size_t max_stack_bytes = 0;

        std::size_t parsing_bytes = 0;
        std::size_t semantic_bytes = 0;
        std::size_t codegen_bytes = 0;
        std::size_t ggc_memory = 0;

        [[nodiscard]] bool has_data() const noexcept {
            return peak_memory_bytes > 0 || frontend_peak_bytes > 0 ||
                   backend_peak_bytes > 0 || max_stack_bytes > 0;
        }
    };

    /**
     * Metrics for a single source file.
     */
    struct FileMetrics {
        fs::path path;
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
        UnityBuild,
        ModuleMigration,
        InlineReduction,
        CompilationFirewall,
        DependencyInversion,
        SymbolVisibility
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
            case SuggestionType::ModuleMigration:       return "Module Migration";
            case SuggestionType::InlineReduction:       return "Inline Reduction";
            case SuggestionType::CompilationFirewall:   return "Compilation Firewall";
            case SuggestionType::DependencyInversion:   return "Dependency Inversion";
            case SuggestionType::SymbolVisibility:      return "Symbol Visibility";
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
     */
    struct FileTarget {
        fs::path path;
        std::size_t line_start = 0;
        std::size_t line_end = 0;
        FileAction action = FileAction::Modify;
        std::optional<std::string> note;

        [[nodiscard]] bool has_line_range() const noexcept {
            return line_start > 0;
        }
    };

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
        std::vector<fs::path> files_benefiting;
        std::size_t total_files_affected = 0;
        Duration cumulative_savings = Duration::zero();
        std::size_t rebuild_files_count = 0;
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

        std::vector<std::string> implementation_steps;
        Impact impact;

        std::vector<std::string> caveats;
        std::string verification;
        std::optional<std::string> documentation_link;

        bool is_safe = false;
    };

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
    };

    /**
     * Suggestion generation options.
     */
    struct SuggesterOptions {
        std::size_t max_suggestions = 100;
        Priority min_priority = Priority::Low;
        double min_confidence = 0.5;
        bool include_unsafe = false;
        std::vector<SuggestionType> enabled_types;
        heuristics::HeuristicsConfig heuristics = heuristics::HeuristicsConfig::defaults();
    };

    /**
     * Build options for triggering builds with tracing.
     */
    struct BuildOptions {
        std::string configuration = "Release";
        std::string target;
        bool clean_first = false;
        std::vector<std::string> extra_args;
        fs::path output_dir;
    };

}  // namespace bha

#endif //BUILDTIMEHOTSPOTANALYZER_TYPES_HPP