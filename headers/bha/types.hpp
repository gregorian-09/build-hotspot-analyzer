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
#include <functional>
#include <string_view>
#include <cstdint>

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
     * Verification/support status for externally visible platform features.
     */
    enum class SupportTier {
        Unknown,
        Core,
        Experimental,
        Deferred
    };

    /**
     * Evidence status for an analytics metric.
     *
     * Metrics without producer evidence remain explicitly unavailable. The
     * status is intentionally separate from suggestion confidence: confidence
     * cannot turn an unavailable metric into evidence.
     */
    enum class EvidenceKind : std::uint8_t {
        Observed,
        Derived,
        Unavailable
    };

    /**
     * Time domain for a metric that carries timing information.
     */
    enum class TimingDomain : std::uint8_t {
        None,
        WallClock,
        Cpu
    };

    /**
     * Aggregation semantics for a timing metric.
     */
    enum class TimingAggregation : std::uint8_t {
        None,
        Exclusive,
        Inclusive,
        WallClockResponsibility
    };

    inline const char* to_string(const EvidenceKind kind) noexcept {
        switch (kind) {
            case EvidenceKind::Observed:    return "observed";
            case EvidenceKind::Derived:     return "derived";
            case EvidenceKind::Unavailable: return "unavailable";
        }
        return "unavailable";
    }

    inline const char* to_string(const TimingDomain domain) noexcept {
        switch (domain) {
            case TimingDomain::None:      return "none";
            case TimingDomain::WallClock: return "wall-clock";
            case TimingDomain::Cpu:       return "cpu";
        }
        return "none";
    }

    inline const char* to_string(const TimingAggregation aggregation) noexcept {
        switch (aggregation) {
            case TimingAggregation::None:                  return "none";
            case TimingAggregation::Exclusive:             return "exclusive";
            case TimingAggregation::Inclusive:             return "inclusive";
            case TimingAggregation::WallClockResponsibility: return "wall-clock-responsibility";
        }
        return "none";
    }

    /**
     * Provenance and timing semantics for one metric.
     */
    struct MetricProvenance {
        EvidenceKind evidence = EvidenceKind::Unavailable;
        std::string producer;
        std::string producer_version;
        std::string capture_mode;
        std::string scope;
        TimingDomain timing_domain = TimingDomain::None;
        TimingAggregation timing_aggregation = TimingAggregation::None;
        std::string limitation;

        [[nodiscard]] bool has_evidence() const noexcept {
            return evidence != EvidenceKind::Unavailable;
        }
    };

    /**
     * Capability state for a named analytics metric.
     */
    struct MetricCapability {
        std::string metric;
        MetricProvenance provenance;
    };

    /**
     * Coarse compiler families used for support-matrix decisions.
     */
    enum class CompilerFamily {
        Unknown,
        Clang,
        GCC,
        MSVC
    };

    /**
     * Evidence level available for template optimization decisions.
     *
     * Aggregate timing can identify that templates are expensive, but it cannot
     * identify a safe explicit-instantiation edit.
     */
    enum class TemplateEvidence : std::uint8_t {
        None,
        AggregateTiming,
        PerSpecializationTiming,
        PerSpecializationTimingWithLocations
    };

    /**
     * Coarse build-system families used for support-matrix decisions.
     */
    enum class BuildSystemFamily {
        Unknown,
        CMake,
        MSBuild,
        CompileDatabase
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

    inline const char* to_string(const SupportTier tier) noexcept {
        switch (tier) {
            case SupportTier::Unknown:      return "unknown";
            case SupportTier::Core:         return "core";
            case SupportTier::Experimental: return "experimental";
            case SupportTier::Deferred:     return "deferred";
        }
        return "unknown";
    }

    inline const char* to_string(const CompilerFamily family) noexcept {
        switch (family) {
            case CompilerFamily::Unknown: return "unknown";
            case CompilerFamily::Clang:   return "clang-family";
            case CompilerFamily::GCC:     return "gcc-family";
            case CompilerFamily::MSVC:    return "msvc-family";
        }
        return "unknown";
    }

    inline const char* to_string(const BuildSystemFamily family) noexcept {
        switch (family) {
            case BuildSystemFamily::Unknown:         return "unknown";
            case BuildSystemFamily::CMake:           return "cmake-family";
            case BuildSystemFamily::MSBuild:         return "msbuild-family";
            case BuildSystemFamily::CompileDatabase: return "compile-db-family";
        }
        return "unknown";
    }

    /**
     * Role of one build-system command event.
     */
    enum class BuildStepRole : std::uint8_t {
        Unknown,
        Configure,
        Generate,
        Build,
        Compile,
        Link,
        Custom,
        Test,
        Install
    };

    inline const char* to_string(const BuildStepRole role) noexcept {
        switch (role) {
            case BuildStepRole::Unknown:   return "unknown";
            case BuildStepRole::Configure: return "configure";
            case BuildStepRole::Generate:  return "generate";
            case BuildStepRole::Build:     return "build";
            case BuildStepRole::Compile:   return "compile";
            case BuildStepRole::Link:      return "link";
            case BuildStepRole::Custom:    return "custom";
            case BuildStepRole::Test:      return "test";
            case BuildStepRole::Install:   return "install";
        }
        return "unknown";
    }

    [[nodiscard]] inline CompilerFamily compiler_family(const CompilerType type) noexcept {
        switch (type) {
            case CompilerType::Clang:
            case CompilerType::AppleClang:
            case CompilerType::ArmClang:
            case CompilerType::IntelOneAPI:
                return CompilerFamily::Clang;
            case CompilerType::GCC:
                return CompilerFamily::GCC;
            case CompilerType::MSVC:
                return CompilerFamily::MSVC;
            case CompilerType::Unknown:
            case CompilerType::IntelClassic:
            case CompilerType::NVCC:
                return CompilerFamily::Unknown;
        }
        return CompilerFamily::Unknown;
    }

    [[nodiscard]] inline SupportTier support_tier(const CompilerType type) noexcept {
        switch (type) {
            case CompilerType::Clang:
            case CompilerType::AppleClang:
            case CompilerType::ArmClang:
            case CompilerType::GCC:
            case CompilerType::MSVC:
                return SupportTier::Core;
            case CompilerType::IntelOneAPI:
            case CompilerType::NVCC:
                return SupportTier::Experimental;
            case CompilerType::IntelClassic:
                return SupportTier::Deferred;
            case CompilerType::Unknown:
                return SupportTier::Unknown;
        }
        return SupportTier::Unknown;
    }

    [[nodiscard]] inline bool is_core_supported(const CompilerType type) noexcept {
        return support_tier(type) == SupportTier::Core;
    }

    /**
     * Source language mode inferred from a compile command.
     */
    enum class SourceLanguageMode {
        Unknown,
        C,
        CXX,
        ObjectiveC,
        ObjectiveCXX
    };

    /**
     * Declares which language families a suggester is designed to support.
     */
    enum class SuggesterLanguageSupport {
        COnly,
        CXXOnly,
        CAndCXX,
        BuildSystemLevel
    };

    /**
     * Describes how conservatively a suggester should be treated around ABI-facing code.
     */
    enum class SuggesterAbiSensitivity {
        Low,
        HeaderSurface,
        BuildConfiguration
    };

    /**
     * Summarized language profile for a recorded project build.
     */
    struct ProjectLanguageProfile {
        std::size_t c_units = 0;
        std::size_t cxx_units = 0;
        std::size_t objc_units = 0;
        std::size_t objcxx_units = 0;
        std::size_t unknown_units = 0;

        [[nodiscard]] bool empty() const noexcept {
            return c_units == 0 && cxx_units == 0 && objc_units == 0 &&
                   objcxx_units == 0 && unknown_units == 0;
        }

        [[nodiscard]] bool has_c_family() const noexcept {
            return c_units > 0 || objc_units > 0;
        }

        [[nodiscard]] bool has_cxx_family() const noexcept {
            return cxx_units > 0 || objcxx_units > 0;
        }
    };

    inline const char* to_string(const SourceLanguageMode mode) noexcept {
        switch (mode) {
            case SourceLanguageMode::Unknown:     return "unknown";
            case SourceLanguageMode::C:           return "c";
            case SourceLanguageMode::CXX:         return "c++";
            case SourceLanguageMode::ObjectiveC:  return "objective-c";
            case SourceLanguageMode::ObjectiveCXX:return "objective-c++";
        }
        return "unknown";
    }

    inline const char* to_string(const SuggesterLanguageSupport support) noexcept {
        switch (support) {
            case SuggesterLanguageSupport::COnly:            return "c-only";
            case SuggesterLanguageSupport::CXXOnly:          return "c++-only";
            case SuggesterLanguageSupport::CAndCXX:          return "c-and-c++";
            case SuggesterLanguageSupport::BuildSystemLevel: return "build-system";
        }
        return "c-and-c++";
    }

    inline const char* to_string(const SuggesterAbiSensitivity sensitivity) noexcept {
        switch (sensitivity) {
            case SuggesterAbiSensitivity::Low:                return "low";
            case SuggesterAbiSensitivity::HeaderSurface:      return "header-surface";
            case SuggesterAbiSensitivity::BuildConfiguration: return "build-configuration";
        }
        return "low";
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

    [[nodiscard]] inline BuildSystemFamily build_system_family(const BuildSystemType type) noexcept {
        switch (type) {
            case BuildSystemType::CMake:
                return BuildSystemFamily::CMake;
            case BuildSystemType::MSBuild:
                return BuildSystemFamily::MSBuild;
            case BuildSystemType::Ninja:
            case BuildSystemType::Make:
            case BuildSystemType::Bazel:
            case BuildSystemType::Buck2:
            case BuildSystemType::Meson:
            case BuildSystemType::SCons:
            case BuildSystemType::XCode:
                return BuildSystemFamily::CompileDatabase;
            case BuildSystemType::Unknown:
                return BuildSystemFamily::Unknown;
        }
        return BuildSystemFamily::Unknown;
    }

    [[nodiscard]] inline SupportTier support_tier(const BuildSystemType type) noexcept {
        switch (type) {
            case BuildSystemType::CMake:
            case BuildSystemType::MSBuild:
                return SupportTier::Core;
            case BuildSystemType::Ninja:
            case BuildSystemType::Make:
            case BuildSystemType::Bazel:
            case BuildSystemType::Buck2:
            case BuildSystemType::Meson:
            case BuildSystemType::SCons:
            case BuildSystemType::XCode:
                return SupportTier::Experimental;
            case BuildSystemType::Unknown:
                return SupportTier::Unknown;
        }
        return SupportTier::Unknown;
    }

    [[nodiscard]] inline bool is_core_supported(const BuildSystemType type) noexcept {
        return support_tier(type) == SupportTier::Core;
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
        /// Time reported by the producer without a safe normalized category.
        Duration unclassified = Duration::zero();

        [[nodiscard]] Duration total() const noexcept {
            return preprocessing + parsing + semantic_analysis +
                   template_instantiation + code_generation + optimization +
                   unclassified;
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
        std::size_t count = 0;
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
        /// Exclusive source-interval time after subtracting nested Source events.
        std::optional<Duration> self_parse_time;
    };

    /**
     * A single compilation unit (source file + all its data).
     */
    struct CompilationUnit {
        fs::path source_file;
        fs::path working_directory;
        FileMetrics metrics;
        std::vector<IncludeInfo> includes;
        std::vector<TemplateInstantiation> templates;
        TemplateEvidence template_evidence = TemplateEvidence::None;
        std::vector<MetricCapability> metric_capabilities;
        std::vector<std::string> symbols_defined;
        std::vector<std::string> command_line;
    };

    /**
     * One build-system command with producer-provided timing.
     */
    struct BuildCommandEvent {
        std::string id;
        BuildStepRole role = BuildStepRole::Unknown;
        std::string command;
        fs::path working_directory;
        std::string target;
        std::string language;
        fs::path source;
        /// Producer reference to a copied Clang -ftime-trace JSON file.
        std::optional<fs::path> trace_file;
        std::vector<fs::path> outputs;
        std::vector<std::uintmax_t> output_sizes;
        std::string test_name;
        std::string configuration;
        /// Host memory used immediately before the command, in KiB.
        std::optional<std::uint64_t> before_host_memory_used_kib;
        /// Host memory used immediately after the command, in KiB.
        std::optional<std::uint64_t> after_host_memory_used_kib;
        /// Host CPU load average immediately before the command.
        std::optional<double> before_cpu_load_average;
        /// Host CPU load average immediately after the command.
        std::optional<double> after_cpu_load_average;
        std::optional<Timestamp> start_time;
        Duration duration = Duration::zero();
        std::optional<int> result;
        /// Transient producer-captured standard output for this command.
        std::optional<std::string> standard_output;
        /// Transient producer-captured standard error for this command.
        std::optional<std::string> standard_error;
        std::vector<std::string> dependency_ids;
        MetricProvenance timing_provenance;

        [[nodiscard]] bool has_exact_timing() const noexcept {
            return start_time.has_value() && duration >= Duration::zero() &&
                   timing_provenance.evidence == EvidenceKind::Observed;
        }
    };

    /**
     * Static host context emitted by CMake Instrumentation API v1.
     *
     * Hostname and other identifying fields are intentionally not retained.
     */
    struct BuildHostSystemInfo {
        std::optional<std::string> os_name;
        std::optional<std::string> os_platform;
        std::optional<std::string> os_release;
        std::optional<std::string> os_version;
        std::optional<bool> is_64_bits;
        std::optional<std::uint64_t> logical_cpu_count;
        std::optional<std::uint64_t> physical_cpu_count;
        std::optional<std::uint64_t> total_physical_memory_mib;
        std::optional<std::uint64_t> total_virtual_memory_mib;
        std::optional<std::string> processor_name;
        std::optional<std::string> vendor_string;
    };

    /**
     * Build-system event session independent of compiler-specific trace files.
     */
    struct BuildSession {
        std::string id;
        BuildSystemType build_system = BuildSystemType::Unknown;
        std::string build_system_version;
        std::string configuration;
        std::string platform;
        std::string instrumentation_hook;
        bool dependency_graph_complete = false;
        std::vector<BuildCommandEvent> commands;
        std::optional<BuildHostSystemInfo> host_system;
        std::vector<MetricCapability> metric_capabilities;
    };

    /**
     * One exact event from a linker Chrome Trace Event profile.
     */
    struct LinkerTraceEvent {
        std::string name;
        Duration start_offset = Duration::zero();
        Duration duration = Duration::zero();
        std::string detail;
    };

    /**
     * Exact linker metrics extracted from a producer time trace.
     */
    struct LinkerTrace {
        std::string id;
        std::string producer = "lld";
        std::string producer_version;
        std::vector<LinkerTraceEvent> events;
        std::optional<Duration> execute_linker_time;
        std::optional<Duration> lto_time;
        std::vector<MetricCapability> metric_capabilities;
    };

    /**
     * Target metadata read from a build-system semantic model.
     */
    struct BuildTarget {
        std::string id;
        std::string name;
        std::string type;
        fs::path source_directory;
        fs::path build_directory;
        fs::path name_on_disk;
        std::vector<fs::path> artifacts;
        std::string link_language;
        bool lto_enabled = false;
        std::vector<std::string> dependencies;
        std::vector<fs::path> precompile_headers;
    };

    /**
     * Complete target ownership graph for one build configuration.
     */
    struct BuildTargetGraph {
        std::string id;
        std::string producer_version;
        std::string configuration;
        fs::path source_root;
        fs::path build_root;
        bool complete = false;
        std::vector<BuildTarget> targets;
        std::vector<MetricCapability> metric_capabilities;
    };

    /**
     * Exact cache counters emitted by a cache producer.
     */
    struct CacheStatistics {
        std::string producer;
        std::string producer_version;
        std::uint64_t compile_requests = 0;
        std::uint64_t executed_requests = 0;
        std::uint64_t non_compilation_requests = 0;
        std::uint64_t unsupported_compiler_requests = 0;
        std::uint64_t non_cacheable_requests = 0;
        std::uint64_t compilations = 0;
        std::uint64_t cache_hits = 0;
        std::uint64_t cache_misses = 0;
        std::uint64_t cache_errors = 0;
        std::uint64_t cache_timeouts = 0;
        std::uint64_t cache_read_errors = 0;
        std::uint64_t non_cacheable_compilations = 0;
        std::uint64_t forced_recaches = 0;
        std::uint64_t cache_write_errors = 0;
        std::uint64_t cache_writes = 0;
        std::uint64_t compilation_failures = 0;
        std::vector<MetricCapability> metric_capabilities;
    };

    /**
     * One process resource row emitted by Clang's -fproc-stat-report.
     *
     * The producer reports time in microseconds and peak memory in KiB. The
     * normalized duration fields preserve those exact producer measurements.
     */
    struct ProcessResourceObservation {
        fs::path tool;
        fs::path output;
        Duration total_time = Duration::zero();
        Duration user_time = Duration::zero();
        std::uint64_t peak_memory_kib = 0;
    };

    /**
     * Process resource rows collected from a compiler resource sidecar.
     */
    struct ProcessResourceReport {
        std::string producer = "clang";
        std::string producer_version;
        std::vector<ProcessResourceObservation> observations;
        std::vector<MetricCapability> metric_capabilities;
    };

    /**
     * One module declaration or import from a P1689 dependency rule.
     */
    struct ModuleDependencyReference {
        std::string logical_name;
        std::optional<fs::path> source_path;
        std::optional<bool> is_interface;
    };

    /**
     * One producer-defined P1689 module dependency rule.
     */
    struct ModuleDependencyRule {
        fs::path primary_output;
        std::vector<ModuleDependencyReference> provides;
        std::vector<ModuleDependencyReference> requirements;
    };

    /**
     * Exact module dependency data emitted by clang-scan-deps.
     */
    struct ModuleDependencyGraph {
        std::string producer = "clang-scan-deps";
        std::string producer_version;
        int format_version = 0;
        int revision = 0;
        std::vector<ModuleDependencyRule> rules;
        std::vector<MetricCapability> metric_capabilities;
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
        TemplateEvidence template_evidence = TemplateEvidence::None;
        bool template_semantic_validated = false;
        std::vector<MetricCapability> metric_capabilities;
        std::optional<BuildSession> build_session;
        std::optional<LinkerTrace> linker_trace;
        std::optional<BuildTargetGraph> target_graph;
        std::optional<CacheStatistics> cache_statistics;
        std::optional<ModuleDependencyGraph> module_dependency_graph;
        std::optional<ProcessResourceReport> process_resource_report;

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
        std::string kind;  // producer/AST-backed origin kind, for example template_origin
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
        /// Provenance for the savings fields; unavailable means no estimate was made.
        EvidenceKind estimated_savings_evidence = EvidenceKind::Unavailable;

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
    // Configuration Types
    // ============================================================================

    /**
     * Analysis configuration options.
     */
    struct AnalysisOptions {
        std::size_t max_threads = 0;  ///< 0 means auto-detect
        /// Filters detailed slow-file output; it does not change build aggregates.
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
        bool conservative_abi_sensitive_headers = true;
        bool enable_consolidation = true;
        bool restrict_to_trace = true;
        Duration max_total_time = Duration::zero();
        Duration max_suggester_time = Duration::zero();
        std::function<void(std::string_view, Duration, std::size_t)> on_suggester_completed;
        std::function<void(std::string_view, std::string_view, std::string_view)> on_suggester_diagnostic;
        std::optional<fs::path> compile_commands_path;
        std::vector<SuggestionType> enabled_types;
    };

}  // namespace bha

#endif //BUILDTIMEHOTSPOTANALYZER_TYPES_HPP
