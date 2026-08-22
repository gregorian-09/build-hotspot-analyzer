// Created by gregorian-rayne on 8/22/26.

#include "bha/build_sessions/cmake_file_api.hpp"

#include "bha/utils/file_utils.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace bha::build_sessions {
    namespace {

        using json = nlohmann::json;

        Result<std::string, Error> required_string(
            const json& object,
            const char* name,
            const fs::path& source_hint
        ) {
            if (!object.contains(name) || !object[name].is_string()) {
                return Result<std::string, Error>::failure(
                    Error::parse_error(
                        std::string("CMake File API object is missing string field: ") + name,
                        source_hint.string()
                    )
                );
            }
            return Result<std::string, Error>::success(object[name].get<std::string>());
        }

        Result<json, Error> read_json(
            const fs::path& path,
            const std::string_view description
        ) {
            const auto content = utils::read_file(path);
            if (content.is_err()) {
                return Result<json, Error>::failure(content.error());
            }
            try {
                return Result<json, Error>::success(json::parse(content.value()));
            } catch (const json::exception& exception) {
                return Result<json, Error>::failure(
                    Error::parse_error(
                        std::string("Failed to parse ") + std::string(description) + ": " + exception.what(),
                        path.string()
                    )
                );
            }
        }

        fs::path resolve_path(const fs::path& root, const std::string& value) {
            const fs::path path(value);
            const auto normalized = (path.is_absolute() ? path : root / path).lexically_normal();
            if (!normalized.root_path().empty() && normalized == normalized.root_path()) {
                return normalized;
            }

            auto normalized_text = normalized.generic_string();
            while (normalized_text.size() > 1 && normalized_text.back() == '/') {
                normalized_text.pop_back();
            }
            return fs::path(normalized_text);
        }

        Result<std::size_t, Error> select_configuration(
            const json& configurations,
            const std::string_view requested,
            const fs::path& source_hint
        ) {
            if (!configurations.is_array() || configurations.empty()) {
                return Result<std::size_t, Error>::failure(
                    Error::parse_error(
                        "CMake codemodel contains no configurations",
                        source_hint.string()
                    )
                );
            }

            if (!requested.empty()) {
                for (std::size_t index = 0; index < configurations.size(); ++index) {
                    const auto& configuration = configurations[index];
                    if (configuration.is_object() && configuration.contains("name") &&
                        configuration["name"].is_string() &&
                        configuration["name"].get<std::string>() == requested) {
                        return Result<std::size_t, Error>::success(index);
                    }
                }
                return Result<std::size_t, Error>::failure(
                    Error::not_found(
                        "Requested CMake codemodel configuration was not found",
                        std::string(requested)
                    )
                );
            }

            if (configurations.size() != 1) {
                return Result<std::size_t, Error>::failure(
                    Error::invalid_argument(
                        "CMake codemodel has multiple configurations; one must be selected explicitly",
                        source_hint.string()
                    )
                );
            }
            return Result<std::size_t, Error>::success(0);
        }

        Result<BuildTarget, Error> parse_target(
            const json& target,
            const json& reference,
            const fs::path& codemodel_path,
            const fs::path& source_root,
            const fs::path& build_root
        ) {
            if (!target.is_object()) {
                return Result<BuildTarget, Error>::failure(
                    Error::parse_error("CMake target reply is not an object", codemodel_path.string())
                );
            }

            const auto id = required_string(reference, "id", codemodel_path);
            if (id.is_err()) return Result<BuildTarget, Error>::failure(id.error());
            const auto name = required_string(reference, "name", codemodel_path);
            if (name.is_err()) return Result<BuildTarget, Error>::failure(name.error());
            const auto target_id = required_string(target, "id", codemodel_path);
            if (target_id.is_err()) return Result<BuildTarget, Error>::failure(target_id.error());
            const auto target_name = required_string(target, "name", codemodel_path);
            if (target_name.is_err()) return Result<BuildTarget, Error>::failure(target_name.error());
            const auto type = required_string(target, "type", codemodel_path);
            if (type.is_err()) return Result<BuildTarget, Error>::failure(type.error());

            if (id.value() != target_id.value() || name.value() != target_name.value()) {
                return Result<BuildTarget, Error>::failure(
                    Error::parse_error(
                        "CMake target reply does not match its codemodel reference",
                        codemodel_path.string()
                    )
                );
            }

            BuildTarget result;
            result.id = target_id.value();
            result.name = target_name.value();
            result.type = type.value();

            if (!target.contains("paths") || !target["paths"].is_object()) {
                return Result<BuildTarget, Error>::failure(
                    Error::parse_error("CMake target reply is missing paths", codemodel_path.string())
                );
            }
            const auto source_path = required_string(target["paths"], "source", codemodel_path);
            if (source_path.is_err()) return Result<BuildTarget, Error>::failure(source_path.error());
            const auto build_path = required_string(target["paths"], "build", codemodel_path);
            if (build_path.is_err()) return Result<BuildTarget, Error>::failure(build_path.error());
            result.source_directory = resolve_path(source_root, source_path.value());
            result.build_directory = resolve_path(build_root, build_path.value());

            if (target["paths"].contains("nameOnDisk")) {
                const auto disk_name = required_string(target["paths"], "nameOnDisk", codemodel_path);
                if (disk_name.is_err()) return Result<BuildTarget, Error>::failure(disk_name.error());
                result.name_on_disk = result.build_directory / disk_name.value();
            }
            if (target["paths"].contains("artifacts")) {
                if (!target["paths"]["artifacts"].is_array()) {
                    return Result<BuildTarget, Error>::failure(
                        Error::parse_error("CMake target artifacts is not an array", codemodel_path.string())
                    );
                }
                for (const auto& artifact : target["paths"]["artifacts"]) {
                    const auto artifact_path = required_string(artifact, "path", codemodel_path);
                    if (artifact_path.is_err()) return Result<BuildTarget, Error>::failure(artifact_path.error());
                    result.artifacts.push_back(resolve_path(build_root, artifact_path.value()));
                }
            }

            if (target.contains("link")) {
                if (!target["link"].is_object()) {
                    return Result<BuildTarget, Error>::failure(
                        Error::parse_error("CMake target link metadata is not an object", codemodel_path.string())
                    );
                }
                if (target["link"].contains("language")) {
                    const auto language = required_string(target["link"], "language", codemodel_path);
                    if (language.is_err()) return Result<BuildTarget, Error>::failure(language.error());
                    result.link_language = language.value();
                }
                if (target["link"].contains("lto")) {
                    if (!target["link"]["lto"].is_boolean()) {
                        return Result<BuildTarget, Error>::failure(
                            Error::parse_error("CMake target LTO field is not boolean", codemodel_path.string())
                        );
                    }
                    result.lto_enabled = target["link"]["lto"].get<bool>();
                }
            }

            if (target.contains("dependencies")) {
                if (!target["dependencies"].is_array()) {
                    return Result<BuildTarget, Error>::failure(
                        Error::parse_error("CMake target dependencies is not an array", codemodel_path.string())
                    );
                }
                for (const auto& dependency : target["dependencies"]) {
                    const auto dependency_id = required_string(dependency, "id", codemodel_path);
                    if (dependency_id.is_err()) return Result<BuildTarget, Error>::failure(dependency_id.error());
                    result.dependencies.push_back(dependency_id.value());
                }
            }

            return Result<BuildTarget, Error>::success(std::move(result));
        }

    }  // namespace

    Result<BuildTargetGraph, Error> CMakeFileApiParser::parse_reply_index(
        const fs::path& index_path,
        const std::string_view requested_configuration
    ) const {
        const auto index_result = read_json(index_path, "CMake File API reply index");
        if (index_result.is_err()) return Result<BuildTargetGraph, Error>::failure(index_result.error());
        const auto& index = index_result.value();

        if (!index.is_object() || !index.contains("objects") || !index["objects"].is_array()) {
            return Result<BuildTargetGraph, Error>::failure(
                Error::parse_error("CMake File API reply index is missing objects", index_path.string())
            );
        }

        json codemodel_reference;
        for (const auto& object : index["objects"]) {
            if (!object.is_object() || !object.contains("kind") ||
                !object["kind"].is_string() || object["kind"] != "codemodel") {
                continue;
            }
            if (!object.contains("version") || !object["version"].is_object() ||
                !object["version"].contains("major") ||
                !object["version"]["major"].is_number_integer() ||
                object["version"]["major"].get<int>() != 2) {
                continue;
            }
            if (!codemodel_reference.is_null()) {
                return Result<BuildTargetGraph, Error>::failure(
                    Error::parse_error("CMake File API reply contains multiple codemodel v2 objects", index_path.string())
                );
            }
            codemodel_reference = object;
        }

        if (codemodel_reference.is_null()) {
            return Result<BuildTargetGraph, Error>::failure(
                Error::not_found("CMake File API codemodel v2 object was not requested or generated", index_path.string())
            );
        }
        const auto codemodel_file = required_string(codemodel_reference, "jsonFile", index_path);
        if (codemodel_file.is_err()) return Result<BuildTargetGraph, Error>::failure(codemodel_file.error());
        const fs::path codemodel_path = resolve_path(index_path.parent_path(), codemodel_file.value());
        const auto codemodel_result = read_json(codemodel_path, "CMake codemodel");
        if (codemodel_result.is_err()) return Result<BuildTargetGraph, Error>::failure(codemodel_result.error());
        const auto& codemodel = codemodel_result.value();

        if (!codemodel.is_object() || !codemodel.contains("kind") ||
            codemodel["kind"] != "codemodel" || !codemodel.contains("version") ||
            !codemodel["version"].is_object() || !codemodel["version"].contains("major") ||
            !codemodel["version"]["major"].is_number_integer() ||
            codemodel["version"]["major"].get<int>() != 2 ||
            !codemodel.contains("paths") || !codemodel["paths"].is_object()) {
            return Result<BuildTargetGraph, Error>::failure(
                Error::parse_error("Invalid CMake codemodel v2 object", codemodel_path.string())
            );
        }

        const auto source_root_value = required_string(codemodel["paths"], "source", codemodel_path);
        if (source_root_value.is_err()) return Result<BuildTargetGraph, Error>::failure(source_root_value.error());
        const auto build_root_value = required_string(codemodel["paths"], "build", codemodel_path);
        if (build_root_value.is_err()) return Result<BuildTargetGraph, Error>::failure(build_root_value.error());
        if (!codemodel.contains("configurations")) {
            return Result<BuildTargetGraph, Error>::failure(
                Error::parse_error("CMake codemodel is missing configurations", codemodel_path.string())
            );
        }
        const auto configuration_index = select_configuration(
            codemodel["configurations"],
            requested_configuration,
            codemodel_path
        );
        if (configuration_index.is_err()) return Result<BuildTargetGraph, Error>::failure(configuration_index.error());
        const auto& configuration = codemodel["configurations"][configuration_index.value()];
        const auto configuration_name = required_string(configuration, "name", codemodel_path);
        if (configuration_name.is_err()) return Result<BuildTargetGraph, Error>::failure(configuration_name.error());
        if (!configuration.contains("targets") || !configuration["targets"].is_array()) {
            return Result<BuildTargetGraph, Error>::failure(
                Error::parse_error("Selected CMake configuration is missing targets", codemodel_path.string())
            );
        }

        BuildTargetGraph graph;
        graph.id = index_path.generic_string();
        graph.configuration = configuration_name.value();
        graph.source_root = source_root_value.value();
        graph.build_root = build_root_value.value();
        if (index.contains("cmake") && index["cmake"].is_object() &&
            index["cmake"].contains("version") && index["cmake"]["version"].is_object() &&
            index["cmake"]["version"].contains("string") &&
            index["cmake"]["version"]["string"].is_string()) {
            graph.producer_version = index["cmake"]["version"]["string"].get<std::string>();
        }

        for (const auto& reference : configuration["targets"]) {
            if (!reference.is_object()) {
                return Result<BuildTargetGraph, Error>::failure(
                    Error::parse_error("CMake codemodel target reference is not an object", codemodel_path.string())
                );
            }
            const auto target_file = required_string(reference, "jsonFile", codemodel_path);
            if (target_file.is_err()) return Result<BuildTargetGraph, Error>::failure(target_file.error());
            const fs::path target_path = resolve_path(codemodel_path.parent_path(), target_file.value());
            const auto target_result = read_json(target_path, "CMake target reply");
            if (target_result.is_err()) return Result<BuildTargetGraph, Error>::failure(target_result.error());
            const auto parsed_target = parse_target(
                target_result.value(),
                reference,
                target_path,
                graph.source_root,
                graph.build_root
            );
            if (parsed_target.is_err()) return Result<BuildTargetGraph, Error>::failure(parsed_target.error());
            graph.targets.push_back(parsed_target.value());
        }

        graph.complete = true;
        MetricCapability ownership;
        ownership.metric = "build.target.ownership";
        ownership.provenance.evidence = EvidenceKind::Observed;
        ownership.provenance.producer = "cmake-file-api";
        ownership.provenance.producer_version = graph.producer_version;
        ownership.provenance.capture_mode = "codemodel-v2";
        ownership.provenance.scope = graph.configuration;
        graph.metric_capabilities.push_back(std::move(ownership));
        return Result<BuildTargetGraph, Error>::success(std::move(graph));
    }

}  // namespace bha::build_sessions
