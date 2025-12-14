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
        ss << R"(  "id": ")" << utils::json_escape(unit.id) << "\",\n";
        ss << R"(  "file_path": ")" << utils::json_escape(unit.file_path) << "\",\n";

        ss << R"(  "compiler_type": ")" << utils::json_escape(unit.compiler_type) << "\",\n";
        ss << R"(  "compiler_version": ")" << utils::json_escape(unit.compiler_version) << "\",\n";

        ss << R"(  "configuration": ")" << utils::json_escape(unit.configuration) << "\",\n";

        ss << "  \"timings\": {\n";
        ss << "    \"total_ms\": " << unit.total_time_ms << ",\n";
        ss << "    \"preprocessing_ms\": " << unit.preprocessing_time_ms << ",\n";
        ss << "    \"parsing_ms\": " << unit.parsing_time_ms << ",\n";
        ss << "    \"codegen_ms\": " << unit.codegen_time_ms << ",\n";
        ss << "    \"optimization_ms\": " << unit.optimization_time_ms << "\n";
        ss << "  },\n";

        ss << "  \"compiler\": {\n";
        ss << R"(    "type": ")" << utils::json_escape(unit.compiler_type) << "\",\n";
        ss << R"(    "version": ")" << utils::json_escape(unit.compiler_version) << "\"\n";
        ss << "  },\n";

        ss << R"(  "compile_flags": )" << utils::serialize_to_json(unit.compile_flags) << ",\n";

        ss << "  \"dependencies\": {\n";
        ss << "    \"direct_includes\": " << utils::serialize_to_json(unit.direct_includes) << ",\n";
        ss << "    \"all_includes\": " << utils::serialize_to_json(unit.all_includes) << "\n";
        ss << "  },\n";

        ss << "  \"template_instantiations\": [";
        for (size_t i = 0; i < unit.template_instantiations.size(); ++i) {
            if (i > 0) ss << ",";
            ss << serialize_template_instantiation(unit.template_instantiations[i]);
        }
        ss << "],\n";

        ss << "  \"metadata\": {\n";
        ss << R"(    "commit_sha": ")" << utils::json_escape(unit.commit_sha) << "\",\n";
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

        ss << R"(  "trace_id": ")" << utils::json_escape(trace.trace_id) << "\",\n";
        ss << R"(  "build_system": ")" << utils::json_escape(trace.build_system) << "\",\n";

        ss << R"(  "version": ")" << UIF_VERSION << "\",\n";

        ss << "  \"metadata\": {\n";
        ss << R"(    "trace_id": ")" << utils::json_escape(trace.trace_id) << "\",\n";
        ss << R"(    "build_system": ")" << utils::json_escape(trace.build_system) << "\",\n";
        ss << R"(    "build_system_version": ")" << utils::json_escape(trace.build_system_version) << "\",\n";
        ss << R"(    "configuration": ")" << utils::json_escape(trace.configuration) << "\",\n";
        ss << R"(    "platform": ")" << utils::json_escape(trace.platform) << "\",\n";
        ss << "    \"total_build_time_ms\": " << trace.total_build_time_ms << ",\n";
        ss << R"(    "commit_sha": ")" << utils::json_escape(trace.commit_sha) << "\",\n";
        ss << R"(    "branch": ")" << utils::json_escape(trace.branch) << "\",\n";
        ss << "    \"is_clean_build\": " << (trace.is_clean_build ? "true" : "false") << "\n";
        ss << "  },\n";

        ss << "  \"compilation_units\": [";
        for (size_t i = 0; i < trace.compilation_units.size(); ++i) {
            if (i > 0) ss << ",";
            if (auto result = serialize_compilation_unit(trace.compilation_units[i]); result.is_success()) {
                ss << "\n    " << result.value();
            }
        }
        ss << "\n  ],\n";

        ss << "  \"dependency_graph\": {\n";
        const auto& adj = trace.dependency_graph.get_adjacency_list();
        size_t node_index = 0;
        for (const auto& [node, edges] : adj) {
            ss << "    \"" << utils::json_escape(node) << "\": [";
            for (size_t j = 0; j < edges.size(); ++j) {
                const auto& edge = edges[j];
                ss << "{";
                ss << R"("target": ")" << utils::json_escape(edge.target) << "\", ";
                ss << R"("type": ")" << utils::json_escape(to_string(edge.type)) << "\", ";
                ss << R"("line_number": )" << edge.line_number << ", ";
                ss << R"("is_system_header": )" << (edge.is_system_header ? "true" : "false") << ", ";
                ss << R"("weight": )" << edge.weight;
                ss << "}";
                if (j + 1 < edges.size()) ss << ", ";
            }
            ss << "]";
            if (++node_index < adj.size()) ss << ",\n";
        }
        ss << "\n  },\n";

        ss << "  \"metrics\": {\n";
        ss << "    \"total_files_compiled\": " << trace.metrics.total_files_compiled << ",\n";
        ss << "    \"total_headers_parsed\": " << trace.metrics.total_headers_parsed << ",\n";
        ss << "    \"average_file_time_ms\": " << trace.metrics.average_file_time_ms << ",\n";
        ss << "    \"median_file_time_ms\": " << trace.metrics.median_file_time_ms << ",\n";
        ss << "    \"p95_file_time_ms\": " << trace.metrics.p95_file_time_ms << ",\n";
        ss << "    \"p99_file_time_ms\": " << trace.metrics.p99_file_time_ms << "\n";
        ss << "  }\n";
        ss << "}";
        return core::Result<std::string>::success(ss.str());
    }

    core::Result<void> UnifiedFormatSerializer::save_to_file(
        const core::BuildTrace& trace,
        const std::string& file_path
    ) {
        auto json_result = serialize_build_trace(trace);
        if (!json_result.is_success()) return core::Result<void>::failure(json_result.error());
        if (!utils::write_file(file_path, json_result.value())) {
            return core::Result<void>::failure(core::ErrorCode::FILE_WRITE_ERROR, "Failed to write unified format to file: " + file_path);
        }
        return core::Result<void>::success();
    }

    core::Result<core::TemplateInstantiation> UnifiedFormatSerializer::deserialize_template_instantiation(const std::string& json) {
        utils::JsonDocument doc;
        if (!doc.parse(json)) return core::Result<core::TemplateInstantiation>::failure(core::ErrorCode::JSON_PARSE_ERROR, "Failed to parse template instantiation JSON");
        core::TemplateInstantiation inst;
        if (auto t = doc.get_string("template_name")) inst.template_name = *t;
        if (auto c = doc.get_string("instantiation_context")) inst.instantiation_context = *c;
        if (auto time = doc.get_double("time_ms")) inst.time_ms = *time;
        if (auto depth = doc.get_int("instantiation_depth")) inst.instantiation_depth = static_cast<int>(*depth);
        if (auto stack = doc.get_string("call_stack")) {
            if (auto vec = utils::deserialize_from_json<std::vector<std::string>>(*stack)) inst.call_stack = *vec;
        }
        return core::Result<core::TemplateInstantiation>::success(std::move(inst));
    }

    core::Result<core::CompilationUnit> UnifiedFormatSerializer::deserialize_compilation_unit(
        const std::string_view json
    ) {
        using namespace simdjson;

        dom::parser parser;
        padded_string padded = padded_string(json);
        dom::element doc;
        if (parser.parse(padded).get(doc)) {
            return core::Result<core::CompilationUnit>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                std::string("Failed to parse compilation unit JSON")
            );
        }

        core::CompilationUnit unit;

        if (auto v = doc["id"].get<std::string_view>(); !v.error()) {
            unit.id = std::string(v.value());
        }
        if (auto v = doc["file_path"].get<std::string_view>(); !v.error()) {
            unit.file_path = std::string(v.value());
        }
        if (auto v = doc["configuration"].get<std::string_view>(); !v.error()) {
            unit.configuration = std::string(v.value());
        }

        if (auto t = doc["timings"]; !t.error()) {
            auto t_obj = t.get<simdjson::dom::object>().value();
            if (auto v = t_obj["total_ms"].get<double>(); !v.error()) {
                unit.total_time_ms = v.value();
            }
            if (auto v = t_obj["preprocessing_ms"].get<double>(); !v.error()) {
                unit.preprocessing_time_ms = v.value();
            }
            if (auto v = t_obj["parsing_ms"].get<double>(); !v.error()) {
                unit.parsing_time_ms = v.value();
            }
            if (auto v = t_obj["codegen_ms"].get<double>(); !v.error()) {
                unit.codegen_time_ms = v.value();
            }
            if (auto v = t_obj["optimization_ms"].get<double>(); !v.error()) {
                unit.optimization_time_ms = v.value();
            }
        }

        if (auto c = doc["compiler"]; !c.error()) {
            auto c_obj = c.get<simdjson::dom::object>().value();
            if (auto v = c_obj["type"].get<std::string_view>(); !v.error()) {
                unit.compiler_type = std::string(v.value());
            }
            if (auto v = c_obj["version"].get<std::string_view>(); !v.error()) {
                unit.compiler_version = std::string(v.value());
            }
        }

        if (auto flags = doc["compile_flags"]; !flags.error()) {
            for (auto f : flags.get_array().value()) {
                if (auto v = f.get<std::string_view>(); !v.error()) {
                    unit.compile_flags.emplace_back(v.value());
                }
            }
        }

        if (auto deps = doc["dependencies"]; !deps.error()) {
            auto deps_obj = deps.get<simdjson::dom::object>().value();
            if (auto direct = deps_obj["direct_includes"]; !direct.error()) {
                for (auto f : direct.get_array().value()) {
                    if (auto v = f.get<std::string_view>(); !v.error()) {
                        unit.direct_includes.emplace_back(v.value());
                    }
                }
            }
            if (auto all = deps_obj["all_includes"]; !all.error()) {
                for (auto f : all.get_array().value()) {
                    if (auto v = f.get<std::string_view>(); !v.error()) {
                        unit.all_includes.emplace_back(v.value());
                    }
                }
            }
        }

        if (auto tmpl = doc["template_instantiations"]; !tmpl.error()) {
            for (auto elem : tmpl.get_array().value()) {
                auto obj = elem.get<simdjson::dom::object>().value();
                core::TemplateInstantiation inst;
                if (auto v = obj["template_name"].get<std::string_view>(); !v.error()) {
                    inst.template_name = std::string(v.value());
                }
                if (auto v = obj["instantiation_context"].get<std::string_view>(); !v.error()) {
                    inst.instantiation_context = std::string(v.value());
                }
                if (auto v = obj["time_ms"].get<double>(); !v.error()) {
                    inst.time_ms = v.value();
                }
                if (auto v = obj["instantiation_depth"].get<int64_t>(); !v.error()) {
                    inst.instantiation_depth = static_cast<int>(v.value());
                }
                if (auto call_arr = obj["call_stack"]; !call_arr.error()) {
                    for (auto cs : call_arr.get_array().value()) {
                        if (auto v = cs.get<std::string_view>(); !v.error()) {
                            inst.call_stack.emplace_back(v.value());
                        }
                    }
                }
                unit.template_instantiations.push_back(std::move(inst));
            }
        }

        if (auto m = doc["metadata"]; !m.error()) {
            auto meta_obj = m.get<simdjson::dom::object>().value();
            if (auto v = meta_obj["commit_sha"].get<std::string_view>(); !v.error()) {
                unit.commit_sha = std::string(v.value());
            }
            if (auto v = meta_obj["file_size_bytes"].get<int64_t>(); !v.error()) {
                unit.file_size_bytes = static_cast<size_t>(v.value());
            }
            if (auto v = meta_obj["preprocessed_size_bytes"].get<int64_t>(); !v.error()) {
                unit.preprocessed_size_bytes = static_cast<size_t>(v.value());
            }
        }

        return core::Result<core::CompilationUnit>::success(std::move(unit));
    }

    core::Result<core::BuildTrace> UnifiedFormatSerializer::deserialize_build_trace(
        const std::string_view json
    ) {
        using namespace simdjson;

        dom::parser parser;
        padded_string padded = padded_string(json);
        dom::element doc;
        if (parser.parse(padded).get(doc)) {
            return core::Result<core::BuildTrace>::failure(
                core::ErrorCode::JSON_PARSE_ERROR,
                "Failed to parse build trace JSON"
            );
        }

        core::BuildTrace trace;

        if (auto mval = doc["metadata"]; !mval.error()) {
            auto m = mval.get_object().value();
            if (auto v = m["trace_id"].get<std::string_view>(); !v.error()) {
                trace.trace_id = std::string(v.value());
            }
            if (auto v = m["build_system"].get<std::string_view>(); !v.error()) {
                trace.build_system = std::string(v.value());
            }
            if (auto v = m["build_system_version"].get<std::string_view>(); !v.error()) {
                trace.build_system_version = std::string(v.value());
            }
            if (auto v = m["configuration"].get<std::string_view>(); !v.error()) {
                trace.configuration = std::string(v.value());
            }
            if (auto v = m["platform"].get<std::string_view>(); !v.error()) {
                trace.platform = std::string(v.value());
            }
            if (auto v = m["total_build_time_ms"].get<double>(); !v.error()) {
                trace.total_build_time_ms = v.value();
            }
            if (auto v = m["commit_sha"].get<std::string_view>(); !v.error()) {
                trace.commit_sha = std::string(v.value());
            }
            if (auto v = m["branch"].get<std::string_view>(); !v.error()) {
                trace.branch = std::string(v.value());
            }
            if (auto v = m["is_clean_build"].get<bool>(); !v.error()) {
                trace.is_clean_build = v.value();
            }
        }

        if (auto units = doc["compilation_units"]; !units.error()) {
            for (dom::element elem : units.get_array().value()) {
                std::ostringstream os;
                os << elem;
                auto res = deserialize_compilation_unit(os.str());
                if (res.is_success()) {
                    trace.compilation_units.push_back(std::move(res).value());
                }
            }
        }

        if (auto dep_obj = doc["dependency_graph"]; !dep_obj.error()) {
            for (auto field : dep_obj.get_object().value()) {
                std::string source = std::string(field.key);
                trace.dependency_graph.add_node(source);
                if (auto edges = field.value.get_array(); !edges.error()) {
                    for (dom::element edge_val : edges.value()) {
                        if (auto edge_obj = edge_val.get_object(); !edge_obj.error()) {
                            auto obj = edge_obj.value();
                            std::string target;
                            if (auto v = obj["target"].get<std::string_view>(); !v.error()) {
                                target = std::string(v.value());
                            }
                            std::string etype_str;
                            if (auto v = obj["type"].get<std::string_view>(); !v.error()) {
                                etype_str = std::string(v.value());
                            }
                            auto etype = core::edge_type_from_string(etype_str);
                            core::DependencyEdge edge(target, etype);
                            if (auto v = obj["line_number"].get<int64_t>(); !v.error()) {
                                edge.line_number = static_cast<int>(v.value());
                            }
                            if (auto v = obj["is_system_header"].get<bool>(); !v.error()) {
                                edge.is_system_header = v.value();
                            }
                            if (auto v = obj["weight"].get<double>(); !v.error()) {
                                edge.weight = v.value();
                            }
                            trace.dependency_graph.add_edge(source, edge);
                        }
                    }
                }
            }
        }

        if (auto metrics_obj = doc["metrics"]; !metrics_obj.error()) {
            auto m = metrics_obj.get_object().value();
            if (auto v = m["total_files_compiled"].get<int64_t>(); !v.error()) {
                trace.metrics.total_files_compiled = static_cast<int>(v.value());
            }
            if (auto v = m["total_headers_parsed"].get<int64_t>(); !v.error()) {
                trace.metrics.total_headers_parsed = static_cast<int>(v.value());
            }
            if (auto v = m["average_file_time_ms"].get<double>(); !v.error()) {
                trace.metrics.average_file_time_ms = v.value();
            }
            if (auto v = m["median_file_time_ms"].get<double>(); !v.error()) {
                trace.metrics.median_file_time_ms = v.value();
            }
            if (auto v = m["p95_file_time_ms"].get<double>(); !v.error()) {
                trace.metrics.p95_file_time_ms = v.value();
            }
            if (auto v = m["p99_file_time_ms"].get<double>(); !v.error()) {
                trace.metrics.p99_file_time_ms = v.value();
            }
        }

        return core::Result<core::BuildTrace>::success(std::move(trace));
    }

    core::Result<core::BuildTrace> UnifiedFormatSerializer::load_from_file(const std::string& file_path) {
        if (const auto content = utils::read_file(file_path)) return deserialize_build_trace(*content);
        return core::Result<core::BuildTrace>::failure(core::ErrorCode::FILE_NOT_FOUND, "Failed to read unified format file: " + file_path);
    }

    std::string UnifiedFormatSerializer::serialize_template_instantiation(
        const core::TemplateInstantiation& inst
    ) {
        std::ostringstream ss;
        ss << "{";
        ss << R"("template_name":)" << utils::serialize_to_json(inst.template_name) << ",";
        ss << R"("instantiation_context":)" << utils::serialize_to_json(inst.instantiation_context) << ",";
        ss << R"("time_ms":)" << inst.time_ms << ",";
        ss << R"("instantiation_depth":)" << inst.instantiation_depth << ",";
        ss << R"("call_stack":)" << utils::serialize_to_json(inst.call_stack);
        ss << "}";
        return ss.str();
    }

    std::string UnifiedFormatSerializer::serialize_dependency_graph(
        const core::DependencyGraph& graph
    ) {
        std::ostringstream ss;
        ss << "{";
        ss << R"("nodes":)" << utils::serialize_to_json(graph.get_all_nodes()) << ",";
        ss << R"("edges":[)";
        bool first = true;
        for (const auto& node : graph.get_all_nodes()) {
            for (const auto& edge : graph.get_edges(node)) {
                if (!first) ss << ",";
                first = false;
                ss << "{";
                ss << R"("source":)" << utils::serialize_to_json(node) << ",";
                ss << R"("target":)" << utils::serialize_to_json(edge.target) << ",";
                ss << R"("type":)" << utils::serialize_to_json(core::to_string(edge.type)) << ",";
                ss << R"("weight":)" << edge.weight;
                ss << "}";
            }
        }
        ss << "]}";
        return ss.str();
    }

    core::DependencyGraph UnifiedFormatSerializer::deserialize_dependency_graph(
        const std::string& json
    ) {
        core::DependencyGraph graph;
        utils::JsonDocument doc;
        if (!doc.parse(json)) return graph;

        if (auto nodes_str = doc.get_string("nodes")) {
            if (auto nodes = utils::deserialize_from_json<std::vector<std::string>>(*nodes_str)) {
                for (auto& node : *nodes) graph.add_node(node);
            }
        }

        if (auto edges_str = doc.get_string("edges")) {
            if (auto edges = utils::deserialize_from_json<std::vector<std::string>>(*edges_str)) {
                for (auto& ejson : *edges) {
                    utils::JsonDocument edoc;
                    if (!edoc.parse(ejson)) continue;
                    std::string src, tgt;
                    auto type = core::EdgeType::DIRECT_INCLUDE;
                    int line_number = 0;
                    bool is_system_header = false;
                    double weight = 0.0;

                    if (auto s = edoc.get_string("source")) src = *s;
                    if (auto t = edoc.get_string("target")) tgt = *t;
                    if (auto ty = edoc.get_string("type")) type = core::edge_type_from_string(*ty);
                    if (auto ln = edoc.get_int("line_number")) line_number = static_cast<int>(*ln);
                    if (auto sys = edoc.get_bool("is_system_header")) is_system_header = *sys;
                    if (auto w = edoc.get_double("weight")) weight = *w;

                    core::DependencyEdge edge{tgt, type};
                    edge.line_number = line_number;
                    edge.is_system_header = is_system_header;
                    edge.weight = weight;
                    graph.add_edge(src, edge);
                }
            }
        }
        return graph;
    }

    std::string UnifiedFormatSerializer::serialize_metrics_summary(
        const core::MetricsSummary& metrics
    ) {
        std::ostringstream ss;
        ss << "{";
        ss << R"("total_files_compiled":)" << metrics.total_files_compiled << ",";
        ss << R"("total_headers_parsed":)" << metrics.total_headers_parsed << ",";
        ss << R"("average_file_time_ms":)" << metrics.average_file_time_ms << ",";
        ss << R"("median_file_time_ms":)" << metrics.median_file_time_ms << ",";
        ss << R"("p95_file_time_ms":)" << metrics.p95_file_time_ms << ",";
        ss << R"("p99_file_time_ms":)" << metrics.p99_file_time_ms << ",";
        ss << R"("total_dependencies":)" << metrics.total_dependencies << ",";
        ss << R"("average_include_depth":)" << metrics.average_include_depth << ",";
        ss << R"("max_include_depth":)" << metrics.max_include_depth << ",";
        ss << R"("circular_dependency_count":)" << metrics.circular_dependency_count;
        ss << "}";
        return ss.str();
    }

    core::MetricsSummary UnifiedFormatSerializer::deserialize_metrics_summary(
        const std::string& json
    ) {
        core::MetricsSummary m;
        utils::JsonDocument doc;
        if (!doc.parse(json)) return m;

        if (auto v = doc.get_int("total_files_compiled")) m.total_files_compiled = static_cast<int>(*v);
        if (auto v = doc.get_int("total_headers_parsed")) m.total_headers_parsed = static_cast<int>(*v);
        if (auto v = doc.get_double("average_file_time_ms")) m.average_file_time_ms = static_cast<int>(*v);
        if (auto v = doc.get_double("median_file_time_ms")) m.median_file_time_ms = static_cast<int>(*v);
        if (auto v = doc.get_double("p95_file_time_ms")) m.p95_file_time_ms = static_cast<int>(*v);
        if (auto v = doc.get_double("p99_file_time_ms")) m.p99_file_time_ms = static_cast<int>(*v);
        if (auto v = doc.get_int("total_dependencies")) m.total_dependencies = static_cast<int>(*v);
        if (auto v = doc.get_int("average_include_depth")) m.average_include_depth = static_cast<int>(*v);
        if (auto v = doc.get_int("max_include_depth")) m.max_include_depth = static_cast<int>(*v);
        if (auto v = doc.get_int("circular_dependency_count")) m.circular_dependency_count = static_cast<int>(*v);

        return m;
    }

    core::Hotspot UnifiedFormatSerializer::deserialize_hotspot(
        const std::string& json
    ) {
        core::Hotspot h;
        utils::JsonDocument doc;
        if (!doc.parse(json)) return h;

        if (auto fp = doc.get_string("file_path")) h.file_path = *fp;
        if (auto t = doc.get_double("time_ms")) h.time_ms = *t;
        if (auto imp = doc.get_double("impact_score")) h.impact_score = *imp;
        if (auto n = doc.get_int("num_dependent_files")) h.num_dependent_files = static_cast<int>(*n);
        if (auto cat = doc.get_string("category")) h.category = *cat;

        return h;
    }

    core::TemplateHotspot UnifiedFormatSerializer::deserialize_template_hotspot(
        const std::string& json
    ) {
        core::TemplateHotspot th;
        utils::JsonDocument doc;
        if (!doc.parse(json)) return th;

        if (auto tn = doc.get_string("template_name")) th.template_name = *tn;
        if (auto ctx = doc.get_string("instantiation_context")) th.instantiation_context = *ctx;
        if (auto t = doc.get_double("time_ms")) th.time_ms = *t;
        if (auto cnt = doc.get_int("instantiation_count")) th.instantiation_count = static_cast<int>(*cnt);
        if (auto stack = doc.get_string("instantiation_stack")) {
            if (auto vec = utils::deserialize_from_json<std::vector<std::string>>(*stack)) th.instantiation_stack = *vec;
        }

        return th;
    }

    core::Suggestion UnifiedFormatSerializer::deserialize_suggestion(
        const std::string& json
    ) {
        core::Suggestion s;
        utils::JsonDocument doc;
        if (!doc.parse(json)) return s;

        if (auto id = doc.get_string("id")) s.id = *id;
        if (auto t = doc.get_string("type")) s.type = core::suggestion_type_from_string(*t);
        if (auto pri = doc.get_string("priority")) s.priority = core::priority_from_string(*pri);
        if (auto conf = doc.get_double("confidence")) s.confidence = *conf;
        if (auto title = doc.get_string("title")) s.title = *title;
        if (auto desc = doc.get_string("description")) s.description = *desc;
        if (auto fp = doc.get_string("file_path")) s.file_path = *fp;
        if (auto rf = doc.get_string("related_files")) {
            if (auto vec = utils::deserialize_from_json<std::vector<std::string>>(*rf)) s.related_files = *vec;
        }
        if (auto ets = doc.get_double("estimated_time_savings_ms")) s.estimated_time_savings_ms = *ets;
        if (auto etp = doc.get_double("estimated_time_savings_percent")) s.estimated_time_savings_percent = *etp;
        if (auto af = doc.get_string("affected_files")) {
            if (auto vec = utils::deserialize_from_json<std::vector<std::string>>(*af)) s.affected_files = *vec;
        }

        if (auto safe = doc.get_bool("is_safe")) s.is_safe = *safe;
        if (auto link = doc.get_string("documentation_link")) s.documentation_link = *link;

        return s;
    }

    template<typename T>
    std::vector<T> UnifiedFormatSerializer::deserialize_array(
        const std::string& json,
        core::Result<T> (*deserializer)(const std::string&)
    ) {
        std::vector<T> result;
        if (const auto arr = utils::deserialize_from_json<std::vector<std::string>>(json)) {
            for (auto& elem : *arr) {
                if (auto r = deserializer(elem); r.is_success()) result.push_back(r.value());
            }
        }
        return result;
    }

    std::string UnifiedFormatSerializer::get_current_version() { return UIF_VERSION; }

} // namespace bha::parsers