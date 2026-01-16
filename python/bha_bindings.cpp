//
// Created by gregorian-rayne on 12/31/25.
//

/**
 * @file bha_bindings.cpp
 * @brief Python bindings for Build Hotspot Analyzer using pybind11.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/functional.h>
#include <pybind11/chrono.h>

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/exporters/exporter.hpp"

namespace py = pybind11;

/**
 * Helper to convert Result<T, Error> to Python exception on error.
 */
template<typename T>
T unwrap_result(bha::Result<T, bha::Error>&& result) {
    if (result.is_err()) {
        throw std::runtime_error(result.error().message());
    }
    return std::move(result.value());
}

template<>
void unwrap_result(bha::Result<void, bha::Error>&& result) {
    if (result.is_err()) {
        throw std::runtime_error(result.error().message());
    }
}

/**
 * Helper to convert Duration to milliseconds for Python.
 */
double duration_to_ms(const bha::Duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

/**
 * Helper to convert milliseconds to Duration.
 */
bha::Duration ms_to_duration(const double ms) {
    return std::chrono::duration_cast<bha::Duration>(
        std::chrono::duration<double, std::milli>(ms)
    );
}

PYBIND11_MODULE(_bha_native, m) {
    m.doc() = "Build Hotspot Analyzer - Python bindings for C++ build performance analysis";

    // Version information
    m.attr("__version__") = "1.0.0";

    // ========================================================================
    // Enumerations
    // ========================================================================

    py::enum_<bha::CompilerType>(m, "CompilerType", "Compiler identification")
        .value("Unknown", bha::CompilerType::Unknown)
        .value("Clang", bha::CompilerType::Clang)
        .value("GCC", bha::CompilerType::GCC)
        .value("MSVC", bha::CompilerType::MSVC)
        .value("IntelClassic", bha::CompilerType::IntelClassic)
        .value("IntelOneAPI", bha::CompilerType::IntelOneAPI)
        .value("NVCC", bha::CompilerType::NVCC)
        .value("ArmClang", bha::CompilerType::ArmClang)
        .value("AppleClang", bha::CompilerType::AppleClang)
        .export_values();

    py::enum_<bha::BuildSystemType>(m, "BuildSystemType", "Build system identification")
        .value("Unknown", bha::BuildSystemType::Unknown)
        .value("CMake", bha::BuildSystemType::CMake)
        .value("Ninja", bha::BuildSystemType::Ninja)
        .value("Make", bha::BuildSystemType::Make)
        .value("MSBuild", bha::BuildSystemType::MSBuild)
        .value("Bazel", bha::BuildSystemType::Bazel)
        .value("Buck2", bha::BuildSystemType::Buck2)
        .value("Meson", bha::BuildSystemType::Meson)
        .value("SCons", bha::BuildSystemType::SCons)
        .value("XCode", bha::BuildSystemType::XCode)
        .export_values();

    py::enum_<bha::SuggestionType>(m, "SuggestionType", "Types of optimization suggestions")
        .value("ForwardDeclaration", bha::SuggestionType::ForwardDeclaration)
        .value("HeaderSplit", bha::SuggestionType::HeaderSplit)
        .value("PCHOptimization", bha::SuggestionType::PCHOptimization)
        .value("PIMPLPattern", bha::SuggestionType::PIMPLPattern)
        .value("IncludeRemoval", bha::SuggestionType::IncludeRemoval)
        .value("MoveToCpp", bha::SuggestionType::MoveToCpp)
        .value("ExplicitTemplate", bha::SuggestionType::ExplicitTemplate)
        .value("UnityBuild", bha::SuggestionType::UnityBuild)
        .value("ModuleMigration", bha::SuggestionType::ModuleMigration)
        .value("InlineReduction", bha::SuggestionType::InlineReduction)
        .value("CompilationFirewall", bha::SuggestionType::CompilationFirewall)
        .value("DependencyInversion", bha::SuggestionType::DependencyInversion)
        .value("SymbolVisibility", bha::SuggestionType::SymbolVisibility)
        .export_values();

    py::enum_<bha::Priority>(m, "Priority", "Priority level for suggestions")
        .value("Critical", bha::Priority::Critical)
        .value("High", bha::Priority::High)
        .value("Medium", bha::Priority::Medium)
        .value("Low", bha::Priority::Low)
        .export_values();

    py::enum_<bha::FileAction>(m, "FileAction", "Action type for file modifications")
        .value("Modify", bha::FileAction::Modify)
        .value("AddInclude", bha::FileAction::AddInclude)
        .value("Remove", bha::FileAction::Remove)
        .value("Create", bha::FileAction::Create)
        .export_values();

    py::enum_<bha::exporters::ExportFormat>(m, "ExportFormat", "Export format")
        .value("JSON", bha::exporters::ExportFormat::JSON)
        .value("HTML", bha::exporters::ExportFormat::HTML)
        .value("CSV", bha::exporters::ExportFormat::CSV)
        .value("SARIF", bha::exporters::ExportFormat::SARIF)
        .value("Markdown", bha::exporters::ExportFormat::Markdown)
        .export_values();

    // ========================================================================
    // Basic Types
    // ========================================================================

    py::class_<bha::SourceLocation>(m, "SourceLocation", "Source code location")
        .def(py::init<>())
        .def_readwrite("file", &bha::SourceLocation::file)
        .def_readwrite("line", &bha::SourceLocation::line)
        .def_readwrite("column", &bha::SourceLocation::column)
        .def("has_location", &bha::SourceLocation::has_location);

    py::class_<bha::TimeBreakdown>(m, "TimeBreakdown", "Breakdown of compilation time by phase")
        .def(py::init<>())
        .def_property("preprocessing_ms",
            [](const bha::TimeBreakdown& t) { return duration_to_ms(t.preprocessing); },
            [](bha::TimeBreakdown& t, const double ms) { t.preprocessing = ms_to_duration(ms); })
        .def_property("parsing_ms",
            [](const bha::TimeBreakdown& t) { return duration_to_ms(t.parsing); },
            [](bha::TimeBreakdown& t, const double ms) { t.parsing = ms_to_duration(ms); })
        .def_property("semantic_analysis_ms",
            [](const bha::TimeBreakdown& t) { return duration_to_ms(t.semantic_analysis); },
            [](bha::TimeBreakdown& t, const double ms) { t.semantic_analysis = ms_to_duration(ms); })
        .def_property("template_instantiation_ms",
            [](const bha::TimeBreakdown& t) { return duration_to_ms(t.template_instantiation); },
            [](bha::TimeBreakdown& t, const double ms) { t.template_instantiation = ms_to_duration(ms); })
        .def_property("code_generation_ms",
            [](const bha::TimeBreakdown& t) { return duration_to_ms(t.code_generation); },
            [](bha::TimeBreakdown& t, const double ms) { t.code_generation = ms_to_duration(ms); })
        .def_property("optimization_ms",
            [](const bha::TimeBreakdown& t) { return duration_to_ms(t.optimization); },
            [](bha::TimeBreakdown& t, const double ms) { t.optimization = ms_to_duration(ms); })
        .def("total_ms", [](const bha::TimeBreakdown& t) { return duration_to_ms(t.total()); });

    py::class_<bha::MemoryMetrics>(m, "MemoryMetrics", "Memory usage metrics")
        .def(py::init<>())
        .def_readwrite("peak_memory_bytes", &bha::MemoryMetrics::peak_memory_bytes)
        .def_readwrite("frontend_peak_bytes", &bha::MemoryMetrics::frontend_peak_bytes)
        .def_readwrite("backend_peak_bytes", &bha::MemoryMetrics::backend_peak_bytes)
        .def_readwrite("max_stack_bytes", &bha::MemoryMetrics::max_stack_bytes)
        .def_readwrite("parsing_bytes", &bha::MemoryMetrics::parsing_bytes)
        .def_readwrite("semantic_bytes", &bha::MemoryMetrics::semantic_bytes)
        .def_readwrite("codegen_bytes", &bha::MemoryMetrics::codegen_bytes)
        .def_readwrite("ggc_memory", &bha::MemoryMetrics::ggc_memory)
        .def("has_data", &bha::MemoryMetrics::has_data);

    py::class_<bha::FileMetrics>(m, "FileMetrics", "Metrics for a single source file")
        .def(py::init<>())
        .def_readwrite("path", &bha::FileMetrics::path)
        .def_property("total_time_ms",
            [](const bha::FileMetrics& f) { return duration_to_ms(f.total_time); },
            [](bha::FileMetrics& f, const double ms) { f.total_time = ms_to_duration(ms); })
        .def_property("frontend_time_ms",
            [](const bha::FileMetrics& f) { return duration_to_ms(f.frontend_time); },
            [](bha::FileMetrics& f, const double ms) { f.frontend_time = ms_to_duration(ms); })
        .def_property("backend_time_ms",
            [](const bha::FileMetrics& f) { return duration_to_ms(f.backend_time); },
            [](bha::FileMetrics& f, const double ms) { f.backend_time = ms_to_duration(ms); })
        .def_readwrite("breakdown", &bha::FileMetrics::breakdown)
        .def_readwrite("memory", &bha::FileMetrics::memory)
        .def_readwrite("preprocessed_lines", &bha::FileMetrics::preprocessed_lines)
        .def_readwrite("expansion_ratio", &bha::FileMetrics::expansion_ratio)
        .def_readwrite("direct_includes", &bha::FileMetrics::direct_includes)
        .def_readwrite("transitive_includes", &bha::FileMetrics::transitive_includes)
        .def_readwrite("max_include_depth", &bha::FileMetrics::max_include_depth);

    // ========================================================================
    // Build Trace Types
    // ========================================================================

    py::class_<bha::TemplateInstantiation>(m, "TemplateInstantiation", "Template instantiation info")
        .def(py::init<>())
        .def_readwrite("name", &bha::TemplateInstantiation::name)
        .def_readwrite("full_signature", &bha::TemplateInstantiation::full_signature)
        .def_readwrite("type_arguments", &bha::TemplateInstantiation::type_arguments)
        .def_property("time_ms",
            [](const bha::TemplateInstantiation& t) { return duration_to_ms(t.time); },
            [](bha::TemplateInstantiation& t, const double ms) { t.time = ms_to_duration(ms); })
        .def_readwrite("location", &bha::TemplateInstantiation::location)
        .def_readwrite("count", &bha::TemplateInstantiation::count);

    py::class_<bha::IncludeInfo>(m, "IncludeInfo", "Information about an included header")
        .def(py::init<>())
        .def_readwrite("header", &bha::IncludeInfo::header)
        .def_property("parse_time_ms",
            [](const bha::IncludeInfo& i) { return duration_to_ms(i.parse_time); },
            [](bha::IncludeInfo& i, const double ms) { i.parse_time = ms_to_duration(ms); })
        .def_readwrite("depth", &bha::IncludeInfo::depth)
        .def_readwrite("included_by", &bha::IncludeInfo::included_by)
        .def_readwrite("symbols_used", &bha::IncludeInfo::symbols_used);

    py::class_<bha::CompilationUnit>(m, "CompilationUnit", "A single compilation unit")
        .def(py::init<>())
        .def_readwrite("source_file", &bha::CompilationUnit::source_file)
        .def_readwrite("metrics", &bha::CompilationUnit::metrics)
        .def_readwrite("includes", &bha::CompilationUnit::includes)
        .def_readwrite("templates", &bha::CompilationUnit::templates)
        .def_readwrite("symbols_defined", &bha::CompilationUnit::symbols_defined)
        .def_readwrite("command_line", &bha::CompilationUnit::command_line);

    py::class_<bha::GitInfo>(m, "GitInfo", "Git repository information")
        .def(py::init<>())
        .def_readwrite("commit_hash", &bha::GitInfo::commit_hash)
        .def_readwrite("branch", &bha::GitInfo::branch)
        .def_readwrite("author", &bha::GitInfo::author)
        .def_readwrite("author_email", &bha::GitInfo::author_email)
        .def_readwrite("message", &bha::GitInfo::message)
        .def_readwrite("is_dirty", &bha::GitInfo::is_dirty);

    py::class_<bha::BuildTrace>(m, "BuildTrace", "Complete build trace data")
        .def(py::init<>())
        .def_readwrite("id", &bha::BuildTrace::id)
        .def_property("total_time_ms",
            [](const bha::BuildTrace& t) { return duration_to_ms(t.total_time); },
            [](bha::BuildTrace& t, const double ms) { t.total_time = ms_to_duration(ms); })
        .def_readwrite("compiler", &bha::BuildTrace::compiler)
        .def_readwrite("compiler_version", &bha::BuildTrace::compiler_version)
        .def_readwrite("build_system", &bha::BuildTrace::build_system)
        .def_readwrite("configuration", &bha::BuildTrace::configuration)
        .def_readwrite("platform", &bha::BuildTrace::platform)
        .def_readwrite("git_info", &bha::BuildTrace::git_info)
        .def_readwrite("units", &bha::BuildTrace::units)
        .def("file_count", &bha::BuildTrace::file_count);

    // ========================================================================
    // Suggestion Types
    // ========================================================================

    py::class_<bha::FileTarget>(m, "FileTarget", "Target file for modification")
        .def(py::init<>())
        .def_readwrite("path", &bha::FileTarget::path)
        .def_readwrite("line_start", &bha::FileTarget::line_start)
        .def_readwrite("line_end", &bha::FileTarget::line_end)
        .def_readwrite("action", &bha::FileTarget::action)
        .def_readwrite("note", &bha::FileTarget::note)
        .def("has_line_range", &bha::FileTarget::has_line_range);

    py::class_<bha::CodeExample>(m, "CodeExample", "Code example showing before/after state")
        .def(py::init<>())
        .def_readwrite("file", &bha::CodeExample::file)
        .def_readwrite("line", &bha::CodeExample::line)
        .def_readwrite("code", &bha::CodeExample::code);

    py::class_<bha::Impact>(m, "Impact", "Impact assessment of applying a suggestion")
        .def(py::init<>())
        .def_readwrite("files_benefiting", &bha::Impact::files_benefiting)
        .def_readwrite("total_files_affected", &bha::Impact::total_files_affected)
        .def_property("cumulative_savings_ms",
            [](const bha::Impact& i) { return duration_to_ms(i.cumulative_savings); },
            [](bha::Impact& i, const double ms) { i.cumulative_savings = ms_to_duration(ms); })
        .def_readwrite("rebuild_files_count", &bha::Impact::rebuild_files_count);

    py::class_<bha::Suggestion>(m, "Suggestion", "A complete optimization suggestion")
        .def(py::init<>())
        .def_readwrite("id", &bha::Suggestion::id)
        .def_readwrite("type", &bha::Suggestion::type)
        .def_readwrite("priority", &bha::Suggestion::priority)
        .def_readwrite("confidence", &bha::Suggestion::confidence)
        .def_readwrite("title", &bha::Suggestion::title)
        .def_readwrite("description", &bha::Suggestion::description)
        .def_readwrite("rationale", &bha::Suggestion::rationale)
        .def_property("estimated_savings_ms",
            [](const bha::Suggestion& s) { return duration_to_ms(s.estimated_savings); },
            [](bha::Suggestion& s, const double ms) { s.estimated_savings = ms_to_duration(ms); })
        .def_readwrite("estimated_savings_percent", &bha::Suggestion::estimated_savings_percent)
        .def_readwrite("target_file", &bha::Suggestion::target_file)
        .def_readwrite("secondary_files", &bha::Suggestion::secondary_files)
        .def_readwrite("before_code", &bha::Suggestion::before_code)
        .def_readwrite("after_code", &bha::Suggestion::after_code)
        .def_readwrite("implementation_steps", &bha::Suggestion::implementation_steps)
        .def_readwrite("impact", &bha::Suggestion::impact)
        .def_readwrite("caveats", &bha::Suggestion::caveats)
        .def_readwrite("verification", &bha::Suggestion::verification)
        .def_readwrite("documentation_link", &bha::Suggestion::documentation_link)
        .def_readwrite("is_safe", &bha::Suggestion::is_safe);

    // ========================================================================
    // Configuration Types
    // ========================================================================

    py::class_<bha::AnalysisOptions>(m, "AnalysisOptions", "Analysis configuration options")
        .def(py::init<>())
        .def_readwrite("max_threads", &bha::AnalysisOptions::max_threads)
        .def_property("min_duration_threshold_ms",
            [](const bha::AnalysisOptions& o) { return duration_to_ms(o.min_duration_threshold); },
            [](bha::AnalysisOptions& o, const double ms) { o.min_duration_threshold = ms_to_duration(ms); })
        .def_readwrite("analyze_templates", &bha::AnalysisOptions::analyze_templates)
        .def_readwrite("analyze_includes", &bha::AnalysisOptions::analyze_includes)
        .def_readwrite("analyze_symbols", &bha::AnalysisOptions::analyze_symbols)
        .def_readwrite("verbose", &bha::AnalysisOptions::verbose);

    py::class_<bha::SuggesterOptions>(m, "SuggesterOptions", "Suggestion generation options")
        .def(py::init<>())
        .def_readwrite("max_suggestions", &bha::SuggesterOptions::max_suggestions)
        .def_readwrite("min_priority", &bha::SuggesterOptions::min_priority)
        .def_readwrite("min_confidence", &bha::SuggesterOptions::min_confidence)
        .def_readwrite("include_unsafe", &bha::SuggesterOptions::include_unsafe)
        .def_readwrite("enabled_types", &bha::SuggesterOptions::enabled_types);

    py::class_<bha::exporters::ExportOptions>(m, "ExportOptions", "Export options")
        .def(py::init<>())
        .def_readwrite("pretty_print", &bha::exporters::ExportOptions::pretty_print)
        .def_readwrite("include_metadata", &bha::exporters::ExportOptions::include_metadata)
        .def_readwrite("compress", &bha::exporters::ExportOptions::compress)
        .def_readwrite("include_file_details", &bha::exporters::ExportOptions::include_file_details)
        .def_readwrite("include_dependencies", &bha::exporters::ExportOptions::include_dependencies)
        .def_readwrite("include_templates", &bha::exporters::ExportOptions::include_templates)
        .def_readwrite("include_symbols", &bha::exporters::ExportOptions::include_symbols)
        .def_readwrite("include_suggestions", &bha::exporters::ExportOptions::include_suggestions)
        .def_readwrite("include_timing", &bha::exporters::ExportOptions::include_timing)
        .def_property("min_compile_time_ms",
            [](const bha::exporters::ExportOptions& o) { return duration_to_ms(o.min_compile_time); },
            [](bha::exporters::ExportOptions& o, const double ms) { o.min_compile_time = ms_to_duration(ms); })
        .def_readwrite("min_confidence", &bha::exporters::ExportOptions::min_confidence)
        .def_readwrite("max_files", &bha::exporters::ExportOptions::max_files)
        .def_readwrite("max_suggestions", &bha::exporters::ExportOptions::max_suggestions)
        .def_readwrite("html_interactive", &bha::exporters::ExportOptions::html_interactive)
        .def_readwrite("html_offline", &bha::exporters::ExportOptions::html_offline)
        .def_readwrite("html_dark_mode", &bha::exporters::ExportOptions::html_dark_mode)
        .def_readwrite("html_title", &bha::exporters::ExportOptions::html_title)
        .def_readwrite("json_schema_version", &bha::exporters::ExportOptions::json_schema_version)
        .def_readwrite("json_streaming", &bha::exporters::ExportOptions::json_streaming);

    // ========================================================================
    // Analysis Results
    // ========================================================================

    py::class_<bha::analyzers::FileAnalysisResult, std::shared_ptr<bha::analyzers::FileAnalysisResult>>(m, "FileAnalysisResult", "File analysis result")
        .def(py::init<>())
        .def_readwrite("file", &bha::analyzers::FileAnalysisResult::file)
        .def_property("compile_time_ms",
            [](const std::shared_ptr<bha::analyzers::FileAnalysisResult>& f) { return duration_to_ms(f->compile_time); },
            [](const std::shared_ptr<bha::analyzers::FileAnalysisResult>& f, const double ms) { f->compile_time = ms_to_duration(ms); })
        .def_property("frontend_time_ms",
            [](const std::shared_ptr<bha::analyzers::FileAnalysisResult>& f) { return duration_to_ms(f->frontend_time); },
            [](const std::shared_ptr<bha::analyzers::FileAnalysisResult>& f, const double ms) { f->frontend_time = ms_to_duration(ms); })
        .def_property("backend_time_ms",
            [](const std::shared_ptr<bha::analyzers::FileAnalysisResult>& f) { return duration_to_ms(f->backend_time); },
            [](const std::shared_ptr<bha::analyzers::FileAnalysisResult>& f, const double ms) { f->backend_time = ms_to_duration(ms); })
        .def_readwrite("breakdown", &bha::analyzers::FileAnalysisResult::breakdown)
        .def_readwrite("time_percent", &bha::analyzers::FileAnalysisResult::time_percent)
        .def_readwrite("rank", &bha::analyzers::FileAnalysisResult::rank)
        .def_readwrite("include_count", &bha::analyzers::FileAnalysisResult::include_count)
        .def_readwrite("template_count", &bha::analyzers::FileAnalysisResult::template_count);

    py::class_<bha::analyzers::DependencyAnalysisResult::HeaderInfo>(m, "HeaderInfo", "Header analysis info")
        .def(py::init<>())
        .def_readwrite("path", &bha::analyzers::DependencyAnalysisResult::HeaderInfo::path)
        .def_property("total_parse_time_ms",
            [](const bha::analyzers::DependencyAnalysisResult::HeaderInfo& h) { return duration_to_ms(h.total_parse_time); },
            [](bha::analyzers::DependencyAnalysisResult::HeaderInfo& h, const double ms) { h.total_parse_time = ms_to_duration(ms); })
        .def_readwrite("inclusion_count", &bha::analyzers::DependencyAnalysisResult::HeaderInfo::inclusion_count)
        .def_readwrite("including_files", &bha::analyzers::DependencyAnalysisResult::HeaderInfo::including_files)
        .def_readwrite("included_by", &bha::analyzers::DependencyAnalysisResult::HeaderInfo::included_by)
        .def_readwrite("impact_score", &bha::analyzers::DependencyAnalysisResult::HeaderInfo::impact_score);

    py::class_<bha::analyzers::DependencyAnalysisResult>(m, "DependencyAnalysisResult", "Dependency analysis result")
        .def(py::init<>())
        .def_readwrite("headers", &bha::analyzers::DependencyAnalysisResult::headers)
        .def_readwrite("total_includes", &bha::analyzers::DependencyAnalysisResult::total_includes)
        .def_readwrite("unique_headers", &bha::analyzers::DependencyAnalysisResult::unique_headers)
        .def_readwrite("max_include_depth", &bha::analyzers::DependencyAnalysisResult::max_include_depth)
        .def_property("total_include_time_ms",
            [](const bha::analyzers::DependencyAnalysisResult& d) { return duration_to_ms(d.total_include_time); },
            [](bha::analyzers::DependencyAnalysisResult& d, const double ms) { d.total_include_time = ms_to_duration(ms); })
        .def_readwrite("circular_dependencies", &bha::analyzers::DependencyAnalysisResult::circular_dependencies);

    py::class_<bha::analyzers::TemplateAnalysisResult::TemplateInfo>(m, "TemplateInfo", "Template info")
        .def(py::init<>())
        .def_readwrite("name", &bha::analyzers::TemplateAnalysisResult::TemplateInfo::name)
        .def_readwrite("full_signature", &bha::analyzers::TemplateAnalysisResult::TemplateInfo::full_signature)
        .def_property("total_time_ms",
            [](const bha::analyzers::TemplateAnalysisResult::TemplateInfo& t) { return duration_to_ms(t.total_time); },
            [](bha::analyzers::TemplateAnalysisResult::TemplateInfo& t, const double ms) { t.total_time = ms_to_duration(ms); })
        .def_readwrite("instantiation_count", &bha::analyzers::TemplateAnalysisResult::TemplateInfo::instantiation_count)
        .def_readwrite("locations", &bha::analyzers::TemplateAnalysisResult::TemplateInfo::locations)
        .def_readwrite("files_using", &bha::analyzers::TemplateAnalysisResult::TemplateInfo::files_using)
        .def_readwrite("time_percent", &bha::analyzers::TemplateAnalysisResult::TemplateInfo::time_percent);

    py::class_<bha::analyzers::TemplateAnalysisResult>(m, "TemplateAnalysisResult", "Template analysis result")
        .def(py::init<>())
        .def_readwrite("templates", &bha::analyzers::TemplateAnalysisResult::templates)
        .def_property("total_template_time_ms",
            [](const bha::analyzers::TemplateAnalysisResult& t) { return duration_to_ms(t.total_template_time); },
            [](bha::analyzers::TemplateAnalysisResult& t, const double ms) { t.total_template_time = ms_to_duration(ms); })
        .def_readwrite("template_time_percent", &bha::analyzers::TemplateAnalysisResult::template_time_percent)
        .def_readwrite("total_instantiations", &bha::analyzers::TemplateAnalysisResult::total_instantiations);

    py::class_<bha::analyzers::PerformanceAnalysisResult>(m, "PerformanceAnalysisResult", "Performance analysis result")
        .def(py::init<>())
        .def_property("total_build_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.total_build_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.total_build_time = ms_to_duration(ms); })
        .def_property("sequential_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.sequential_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.sequential_time = ms_to_duration(ms); })
        .def_property("parallel_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.parallel_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.parallel_time = ms_to_duration(ms); })
        .def_readwrite("parallelism_efficiency", &bha::analyzers::PerformanceAnalysisResult::parallelism_efficiency)
        .def_readwrite("total_files", &bha::analyzers::PerformanceAnalysisResult::total_files)
        .def_readwrite("slowest_file_count", &bha::analyzers::PerformanceAnalysisResult::slowest_file_count)
        .def_property("avg_file_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.avg_file_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.avg_file_time = ms_to_duration(ms); })
        .def_property("median_file_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.median_file_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.median_file_time = ms_to_duration(ms); })
        .def_property("p90_file_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.p90_file_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.p90_file_time = ms_to_duration(ms); })
        .def_property("p99_file_time_ms",
            [](const bha::analyzers::PerformanceAnalysisResult& p) { return duration_to_ms(p.p99_file_time); },
            [](bha::analyzers::PerformanceAnalysisResult& p, const double ms) { p.p99_file_time = ms_to_duration(ms); })
        .def_readwrite("slowest_files", &bha::analyzers::PerformanceAnalysisResult::slowest_files)
        .def_readwrite("critical_path", &bha::analyzers::PerformanceAnalysisResult::critical_path);

    py::class_<bha::analyzers::AnalysisResult>(m, "AnalysisResult", "Combined analysis result")
        .def(py::init<>())
        .def_readwrite("performance", &bha::analyzers::AnalysisResult::performance)
        .def_readwrite("files", &bha::analyzers::AnalysisResult::files)
        .def_readwrite("dependencies", &bha::analyzers::AnalysisResult::dependencies)
        .def_readwrite("templates", &bha::analyzers::AnalysisResult::templates)
        .def_readwrite("symbols", &bha::analyzers::AnalysisResult::symbols)
        .def_property("analysis_duration_ms",
            [](const bha::analyzers::AnalysisResult& a) { return duration_to_ms(a.analysis_duration); },
            [](bha::analyzers::AnalysisResult& a, const double ms) { a.analysis_duration = ms_to_duration(ms); });

    // ========================================================================
    // High-Level Functions
    // ========================================================================

    m.def("parse_trace_file", [](const std::string& path) {
        return unwrap_result(bha::parsers::parse_trace_file(path));
    }, py::arg("path"), "Parse a trace file with auto-detection");

    m.def("run_full_analysis", [](const bha::BuildTrace& trace, const bha::AnalysisOptions& options) {
        return unwrap_result(bha::analyzers::run_full_analysis(trace, options));
    }, py::arg("trace"), py::arg("options") = bha::AnalysisOptions{},
    "Run full analysis on a build trace");

    m.def("generate_suggestions", [](const bha::BuildTrace& trace,
                                      const bha::analyzers::AnalysisResult& analysis,
                                      const bha::SuggesterOptions& options) {
        return unwrap_result(bha::suggestions::generate_all_suggestions(trace, analysis, options));
    }, py::arg("trace"), py::arg("analysis"), py::arg("options") = bha::SuggesterOptions{},
    "Generate optimization suggestions");

    m.def(
        "export_to_file",
        [](const std::string& path,
           const bha::analyzers::AnalysisResult& analysis,
           const std::vector<bha::Suggestion>& suggestions,
           const bha::exporters::ExportFormat format,
           const bha::exporters::ExportOptions& options,
           const std::function<void(size_t, size_t, std::string_view)>& progress)
        {
            auto exporter_result = bha::exporters::ExporterFactory::create(format);
            if (exporter_result.is_err()) {
                throw std::runtime_error(exporter_result.error().message());
            }
            const auto& exporter = exporter_result.value();
            unwrap_result(
                exporter->export_to_file(path, analysis, suggestions, options, progress));
        },
        py::arg("path"),
        py::arg("analysis"),
        py::arg("suggestions") = std::vector<bha::Suggestion>{},
        py::arg("format") = bha::exporters::ExportFormat::JSON,
        py::arg("options") = bha::exporters::ExportOptions{},
        py::arg("progress") = nullptr,
        "Export analysis results to a file"
    );

    m.def("export_to_string", [](const bha::analyzers::AnalysisResult& analysis,
                                  const std::vector<bha::Suggestion>& suggestions,
                                  const bha::exporters::ExportFormat format,
                                  const bha::exporters::ExportOptions& options) {
        auto exporter_result = bha::exporters::ExporterFactory::create(format);
        if (exporter_result.is_err()) {
            throw std::runtime_error(exporter_result.error().message());
        }
        const auto& exporter = exporter_result.value();
        return unwrap_result(exporter->export_to_string(analysis, suggestions, options));
    }, py::arg("analysis"), py::arg("suggestions") = std::vector<bha::Suggestion>{},
       py::arg("format") = bha::exporters::ExportFormat::JSON,
       py::arg("options") = bha::exporters::ExportOptions{},
    "Export analysis results to a string");
}