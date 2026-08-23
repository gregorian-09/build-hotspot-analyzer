// Created by gregorian-rayne on 8/22/26.

#include "bha/parsers/p1689_module_parser.hpp"

#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

namespace bha::parsers {
    namespace {

        using json = nlohmann::json;

        Result<std::string, Error> required_string(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.is_object() || !object.contains(name) || !object[name].is_string()) {
                return Result<std::string, Error>::failure(
                    Error::parse_error(
                        std::string("P1689 object is missing string field: ") + name,
                        source_hint.string()
                    )
                );
            }
            const auto value = object[name].get<std::string>();
            if (value.empty()) {
                return Result<std::string, Error>::failure(
                    Error::parse_error(
                        std::string("P1689 object has empty string field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::string, Error>::success(value);
        }

        Result<ModuleDependencyReference, Error> parse_reference(
            const json& object,
            const fs::path& source_hint,
            const bool allow_interface
        ) {
            const auto logical_name = required_string(object, "logical-name", source_hint);
            if (logical_name.is_err()) {
                return Result<ModuleDependencyReference, Error>::failure(logical_name.error());
            }

            ModuleDependencyReference result;
            result.logical_name = logical_name.value();
            if (object.contains("source-path")) {
                if (!object["source-path"].is_string() || object["source-path"].get<std::string>().empty()) {
                    return Result<ModuleDependencyReference, Error>::failure(
                        Error::parse_error(
                            "P1689 source-path must be a non-empty string",
                            source_hint.string()
                        )
                    );
                }
                result.source_path = fs::path(object["source-path"].get<std::string>());
            }
            if (object.contains("is-interface")) {
                if (!allow_interface || !object["is-interface"].is_boolean()) {
                    return Result<ModuleDependencyReference, Error>::failure(
                        Error::parse_error(
                            "P1689 is-interface must be a boolean on provides entries",
                            source_hint.string()
                        )
                    );
                }
                result.is_interface = object["is-interface"].get<bool>();
            }
            return Result<ModuleDependencyReference, Error>::success(std::move(result));
        }

        Result<std::vector<ModuleDependencyReference>, Error> parse_references(
            const json& object,
            const char* field,
            const fs::path& source_hint,
            const bool allow_interface
        ) {
            std::vector<ModuleDependencyReference> result;
            if (!object.contains(field)) {
                return Result<std::vector<ModuleDependencyReference>, Error>::success(std::move(result));
            }
            if (!object[field].is_array()) {
                return Result<std::vector<ModuleDependencyReference>, Error>::failure(
                    Error::parse_error(
                        std::string("P1689 field is not an array: ") + field,
                        source_hint.string()
                    )
                );
            }

            std::unordered_set<std::string> names;
            result.reserve(object[field].size());
            for (const auto& entry : object[field]) {
                const auto parsed = parse_reference(entry, source_hint, allow_interface);
                if (parsed.is_err()) {
                    return Result<std::vector<ModuleDependencyReference>, Error>::failure(parsed.error());
                }
                if (!names.insert(parsed.value().logical_name).second) {
                    return Result<std::vector<ModuleDependencyReference>, Error>::failure(
                        Error::parse_error(
                            std::string("P1689 field contains duplicate logical-name: ") + field,
                            source_hint.string()
                        )
                    );
                }
                result.push_back(parsed.value());
            }
            return Result<std::vector<ModuleDependencyReference>, Error>::success(std::move(result));
        }

        MetricCapability observed_capability() {
            MetricCapability capability;
            capability.metric = "module.dependencies";
            capability.provenance.evidence = EvidenceKind::Observed;
            capability.provenance.producer = "clang-scan-deps";
            capability.provenance.capture_mode = "-format=p1689";
            capability.provenance.scope = "build";
            capability.provenance.timing_domain = TimingDomain::None;
            capability.provenance.timing_aggregation = TimingAggregation::None;
            return capability;
        }

    }  // namespace

    Result<ModuleDependencyGraph, Error> P1689ModuleParser::parse_content(
        const std::string_view content,
        const fs::path& source_hint
    ) const {
        try {
            const auto document = json::parse(content);
            if (!document.is_object() || !document.contains("revision") ||
                !document["revision"].is_number_integer() ||
                !document.contains("version") || !document["version"].is_number_integer() ||
                !document.contains("rules") || !document["rules"].is_array()) {
                return Result<ModuleDependencyGraph, Error>::failure(
                    Error::parse_error("Invalid P1689 document shape", source_hint.string())
                );
            }

            const int revision = document["revision"].get<int>();
            const int version = document["version"].get<int>();
            if (revision != 0 || version != 1) {
                return Result<ModuleDependencyGraph, Error>::failure(
                    Error::parse_error(
                        "Unsupported P1689 revision or version",
                        source_hint.string()
                    )
                );
            }

            ModuleDependencyGraph result;
            result.revision = revision;
            result.format_version = version;
            std::unordered_set<std::string> primary_outputs;
            std::unordered_set<std::string> provided_modules;

            result.rules.reserve(document["rules"].size());
            for (const auto& rule_json : document["rules"]) {
                if (!rule_json.is_object()) {
                    return Result<ModuleDependencyGraph, Error>::failure(
                        Error::parse_error("P1689 rule is not an object", source_hint.string())
                    );
                }
                const auto primary_output = required_string(rule_json, "primary-output", source_hint);
                if (primary_output.is_err()) {
                    return Result<ModuleDependencyGraph, Error>::failure(primary_output.error());
                }
                if (!primary_outputs.insert(primary_output.value()).second) {
                    return Result<ModuleDependencyGraph, Error>::failure(
                        Error::parse_error(
                            "P1689 contains duplicate primary-output entries",
                            source_hint.string()
                        )
                    );
                }

                const auto provides = parse_references(rule_json, "provides", source_hint, true);
                if (provides.is_err()) {
                    return Result<ModuleDependencyGraph, Error>::failure(provides.error());
                }
                for (const auto& provided : provides.value()) {
                    if (!provided_modules.insert(provided.logical_name).second) {
                        return Result<ModuleDependencyGraph, Error>::failure(
                            Error::parse_error(
                                "P1689 contains duplicate provided logical-name: " + provided.logical_name,
                                source_hint.string()
                            )
                        );
                    }
                }

                const auto requirements = parse_references(rule_json, "requires", source_hint, false);
                if (requirements.is_err()) {
                    return Result<ModuleDependencyGraph, Error>::failure(requirements.error());
                }

                ModuleDependencyRule rule;
                rule.primary_output = primary_output.value();
                rule.provides = provides.value();
                rule.requirements = requirements.value();
                result.rules.push_back(std::move(rule));
            }

            result.metric_capabilities.push_back(observed_capability());
            return Result<ModuleDependencyGraph, Error>::success(std::move(result));
        } catch (const json::exception& exception) {
            return Result<ModuleDependencyGraph, Error>::failure(
                Error::parse_error(
                    std::string("Failed to parse P1689 module dependencies: ") + exception.what(),
                    source_hint.string()
                )
            );
        }
    }

    Result<ModuleDependencyGraph, Error> P1689ModuleParser::parse_file(const fs::path& path) const {
        const auto content = utils::read_file(path);
        if (content.is_err()) {
            return Result<ModuleDependencyGraph, Error>::failure(content.error());
        }
        return parse_content(content.value(), path);
    }

    Result<void, Error> P1689ModuleParser::attach_to_trace(
        BuildTrace& trace,
        const fs::path& path
    ) const {
        const auto parsed = parse_file(path);
        if (parsed.is_err()) {
            return Result<void, Error>::failure(parsed.error());
        }
        trace.module_dependency_graph = parsed.value();
        for (const auto& capability : parsed.value().metric_capabilities) {
            const auto existing = std::ranges::find(
                trace.metric_capabilities,
                capability.metric,
                &MetricCapability::metric
            );
            if (existing == trace.metric_capabilities.end()) {
                trace.metric_capabilities.push_back(capability);
            }
        }
        return Result<void, Error>::success();
    }

}  // namespace bha::parsers
