//
// Created by gregorian on 16/10/2025.
//

#include "bha/parsers/unified_format.h"
#include "bha/utils/json_utils.h"
#include "bha/utils/file_utils.h"
#include <sstream>
#include <chrono>

namespace bha::parsers {

    core::Result<std::string> UnifiedFormatSerializer::serialize_compilation_unit(
        const core::CompilationUnit& unit
    ) {
        std::ostringstream ss;

        ss << "{\n";
        ss << R"(  "id": ")" << unit.id << "\",\n";
        ss << R"(  "file_path": ")" << utils::json_escape(unit.file_path) << "\",\n";
        ss << R"(  "configuration": ")" << utils::json_escape(unit.configuration) << "\",\n";

        ss << "  \"timings\": {\n";
        ss << "    \"total_ms\": " << unit.total_time_ms << ",\n";
        ss << "    \"preprocessing_ms\": " << unit.preprocessing_time_ms << ",\n";
        ss << "    \"parsing_ms\": " << unit.parsing_time_ms << ",\n";
        ss << "    \"codegen_ms\": " << unit.codegen_time_ms << ",\n";
        ss << "    \"optimization_ms\": " << unit.optimization_time_ms << "\n";
        ss << "  },\n";

        ss << "  \"compiler\": {\n";
        ss << R"(    "type": ")" << unit.compiler_type << "\",\n";
        ss << R"(    "version": ")" << unit.compiler_version << "\"\n";
        ss << "  },\n";

        ss << "  \"dependencies\": {\n";
        ss << "    \"direct_includes\": [";
        for (size_t i = 0; i < unit.direct_includes.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << utils::json_escape(unit.direct_includes[i]) << "\"";
        }
        ss << "],\n";
        ss << "    \"all_includes\": [";
        for (size_t i = 0; i < unit.all_includes.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << utils::json_escape(unit.all_includes[i]) << "\"";
        }
        ss << "]\n";
        ss << "  },\n";

        ss << "  \"template_instantiations\": [";
        for (size_t i = 0; i < unit.template_instantiations.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << serialize_template_instantiation(unit.template_instantiations[i]);
        }
        ss << "],\n";

        ss << "  \"metadata\": {\n";
        ss << R"(    "commit_sha": ")" << unit.commit_sha << "\",\n";
        ss << "    \"file_size_bytes\": " << unit.file_size_bytes << ",\n";
        ss << "    \"preprocessed_size_bytes\": " << unit.preprocessed_size_bytes << "\n";
        ss << "  }\n";

        ss << "}";

        return core::Result<std::string>::success(ss.str());
    }

    core::Result<std::string> UnifiedFormatSerializer::serialize_build_trace(
        const core::BuildTrace& trace
    ) {
        std::ostringstream ss;

        ss << "{\n";
        ss << R"(  "version": ")" << UIF_VERSION << "\",\n";
        ss << "  \"metadata\": {\n";
        ss << R"(    "trace_id": ")" << trace.trace_id << "\",\n";
        ss << R"(    "build_system": ")" << trace.build_system << "\",\n";
        ss << R"(    "build_system_version": ")" << trace.build_system_version << "\",\n";
        ss << R"(    "configuration": ")" << trace.configuration << "\",\n";
        ss << R"(    "platform": ")" << trace.platform << "\",\n";
        ss << "    \"total_build_time_ms\": " << trace.total_build_time_ms << ",\n";
        ss << R"(    "commit_sha": ")" << trace.commit_sha << "\",\n";
        ss << R"(    "branch": ")" << trace.branch << "\",\n";
        ss << "    \"is_clean_build\": " << (trace.is_clean_build ? "true" : "false") << "\n";
        ss << "  },\n";

        ss << "  \"compilation_units\": [";
        for (size_t i = 0; i < trace.compilation_units.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "\n    ";
            if (auto result = serialize_compilation_unit(trace.compilation_units[i]); result.is_success()) {
                ss << result.value();
            }
        }
        ss << "\n  ]";

        ss << "\n}";

        return core::Result<std::string>::success(ss.str());
    }

    core::Result<void> UnifiedFormatSerializer::save_to_file(
        const core::BuildTrace& trace,
        const std::string& file_path
    ) {
        auto json_result = serialize_build_trace(trace);
        if (!json_result.is_success()) {
            return core::Result<void>::failure(json_result.error());
        }

        if (!utils::write_file(file_path, json_result.value())) {
            return core::Result<void>::failure(
                core::ErrorCode::FILE_WRITE_ERROR,
                "Failed to write unified format to file: " + file_path
            );
        }

        return core::Result<void>::success();
    }

    core::Result<core::CompilationUnit> UnifiedFormatSerializer::deserialize_compilation_unit(
        const std::string_view json
    ) {
        utils::JsonDocument doc;
        if (!doc.parse(json)) {
            return core::Result<core::CompilationUnit>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                "Failed to parse compilation unit JSON"
            );
        }

        core::CompilationUnit unit;

        if (const auto id = doc.get_string("id")) {
            unit.id = *id;
        }

        if (const auto path = doc.get_string("file_path")) {
            unit.file_path = *path;
        }

        if (const auto config = doc.get_string("configuration")) {
            unit.configuration = *config;
        }

        return core::Result<core::CompilationUnit>::success(std::move(unit));
    }

    core::Result<core::BuildTrace> UnifiedFormatSerializer::deserialize_build_trace(
        const std::string_view json
    ) {
        utils::JsonDocument doc;
        if (!doc.parse(json)) {
            return core::Result<core::BuildTrace>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                "Failed to parse build trace JSON"
            );
        }

        core::BuildTrace trace;

        if (const auto id = doc.get_string("metadata.trace_id")) {
            trace.trace_id = *id;
        }

        if (const auto build_sys = doc.get_string("metadata.build_system")) {
            trace.build_system = *build_sys;
        }

        if (const auto time = doc.get_double("metadata.total_build_time_ms")) {
            trace.total_build_time_ms = *time;
        }

        return core::Result<core::BuildTrace>::success(std::move(trace));
    }

    core::Result<core::BuildTrace> UnifiedFormatSerializer::load_from_file(
        const std::string& file_path
    ) {
        const auto content = utils::read_file(file_path);
        if (!content) {
            return core::Result<core::BuildTrace>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Failed to read unified format file: " + file_path
            );
        }

        return deserialize_build_trace(*content);
    }

    std::string UnifiedFormatSerializer::get_current_version() {
        return UIF_VERSION;
    }

    std::string UnifiedFormatSerializer::serialize_template_instantiation(
        const core::TemplateInstantiation& inst
    ) {
        std::ostringstream ss;

        ss << "{\n";
        ss << R"(      "template_name": ")" << utils::json_escape(inst.template_name) << "\",\n";
        ss << R"(      "instantiation_context": ")" << utils::json_escape(inst.instantiation_context) << "\",\n";
        ss << "      \"time_ms\": " << inst.time_ms << ",\n";
        ss << "      \"instantiation_depth\": " << inst.instantiation_depth << ",\n";
        ss << "      \"call_stack\": [";
        for (size_t i = 0; i < inst.call_stack.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << utils::json_escape(inst.call_stack[i]) << "\"";
        }
        ss << "]\n";
        ss << "    }";

        return ss.str();
    }

    std::string UnifiedFormatSerializer::serialize_dependency_graph(
        const core::DependencyGraph& graph
    ) {
        std::ostringstream ss;

        ss << "{\n";
        ss << "  \"nodes\": [";

        const auto nodes = graph.get_all_nodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << "\"" << utils::json_escape(nodes[i]) << "\"";
        }

        ss << "],\n";
        ss << "  \"edges\": [";

        bool first_edge = true;
        for (const auto& node : nodes) {
            for (auto edges = graph.get_edges(node); const auto& edge : edges) {
                if (!first_edge) ss << ",";
                first_edge = false;

                ss << "\n    {";
                ss << R"("source": ")" << utils::json_escape(node) << "\", ";
                ss << R"("target": ")" << utils::json_escape(edge.target) << "\", ";
                ss << R"("type": ")" << core::to_string(edge.type) << "\", ";
                ss << "\"weight\": " << edge.weight;
                ss << "}";
            }
        }

        ss << "\n  ]\n";
        ss << "}";

        return ss.str();
    }

    std::string UnifiedFormatSerializer::serialize_metrics_summary(
        const core::MetricsSummary& metrics
    ) {
        std::ostringstream ss;

        ss << "{\n";
        ss << "  \"total_files_compiled\": " << metrics.total_files_compiled << ",\n";
        ss << "  \"total_headers_parsed\": " << metrics.total_headers_parsed << ",\n";
        ss << "  \"average_file_time_ms\": " << metrics.average_file_time_ms << ",\n";
        ss << "  \"median_file_time_ms\": " << metrics.median_file_time_ms << ",\n";
        ss << "  \"total_dependencies\": " << metrics.total_dependencies << ",\n";
        ss << "  \"max_include_depth\": " << metrics.max_include_depth << "\n";
        ss << "}";

        return ss.str();
    }

    core::Result<core::TemplateInstantiation> UnifiedFormatSerializer::deserialize_template_instantiation(
        const std::string& json
    ) {
        utils::JsonDocument doc;
        if (!doc.parse(json)) {
            return core::Result<core::TemplateInstantiation>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                "Failed to parse template instantiation json"
            );
        }
        core::TemplateInstantiation inst;
        if (const auto temp_name = doc.get_string("template_name")) {
            inst.template_name = *temp_name;
        }

        if (const auto inst_context = doc.get_string("instantiation_context")) {
            inst.instantiation_context = *inst_context;
        }

        if (const auto time = doc.get_double("time_ms")) {
            inst.time_ms = *time;
        }

        if (auto call_stack = utils::deserialize_from_json<std::vector<std::string>>(doc.get_string("call_stack").value_or(""))) {
            inst.call_stack = *call_stack;
        }

        if (const auto inst_dept = doc.get_int("instantiation_depth")) {
            inst.instantiation_depth = static_cast<long>(*inst_dept);
        }

        return core::Result<core::TemplateInstantiation>::success(std::move(inst));
    }

} // namespace bha::parsers