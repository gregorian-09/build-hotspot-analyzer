//
// Created by gregorian on 28/10/2025.
//

#include "bha/security/anonymizer.h"
#include "bha/utils/hash_utils.h"
#include <filesystem>
#include <algorithm>
#include <ranges>
#include <utility>

namespace fs = std::filesystem;

namespace bha::security {

    Anonymizer::Anonymizer(AnonymizationConfig config)
        : config_(std::move(config)) {}

    core::BuildTrace Anonymizer::anonymize_trace(const core::BuildTrace& trace) {
        core::BuildTrace anonymized = trace;

        if (config_.anonymize_paths) {
            for (auto& unit : anonymized.compilation_units) {
                unit.file_path = anonymize_path(unit.file_path);

                for (auto& include : unit.direct_includes) {
                    include = anonymize_path(include);
                }

                for (auto& include : unit.all_includes) {
                    include = anonymize_path(include);
                }
            }

            core::DependencyGraph new_graph;
            const auto& old_adjacency = anonymized.dependency_graph.get_adjacency_list();

            for (const auto& source : old_adjacency | std::views::keys) {
                new_graph.add_node(anonymize_path(source));
            }

            for (const auto& [source, edges] : old_adjacency) {
                std::string anon_source = anonymize_path(source);

                for (const auto& edge : edges) {
                    core::DependencyEdge anon_edge = edge;
                    anon_edge.target = anonymize_path(edge.target);
                    new_graph.add_edge(anon_source, anon_edge);
                }
            }

            anonymized.dependency_graph = std::move(new_graph);

            for (auto& hotspot : anonymized.metrics.top_slow_files) {
                hotspot.file_path = anonymize_path(hotspot.file_path);
            }
        }

        if (config_.anonymize_commit_info) {
            anonymized.commit_sha = anonymize_commit_sha(trace.commit_sha);
            anonymized.branch = "branch_" + std::to_string(commit_counter_++);
        }

        return anonymized;
    }

    std::string Anonymizer::anonymize_path(const std::string& path) {
        if (should_preserve_path(path)) {
            return path;
        }

        if (const auto it = path_mapping_.find(path); it != path_mapping_.end()) {
            return it->second;
        }

        std::string anonymous = generate_anonymous_path(path);
        path_mapping_[path] = anonymous;

        return anonymous;
    }

    std::string Anonymizer::anonymize_commit_sha(const std::string& sha) {
        if (sha.empty()) {
            return "";
        }

        if (const auto it = commit_mapping_.find(sha); it != commit_mapping_.end()) {
            return it->second;
        }

        std::string anonymous = generate_anonymous_commit();
        commit_mapping_[sha] = anonymous;

        return anonymous;
    }

    void Anonymizer::clear_mapping() {
        path_mapping_.clear();
        commit_mapping_.clear();
        path_counter_ = 0;
        commit_counter_ = 0;
    }

    const std::unordered_map<std::string, std::string>&
    Anonymizer::get_path_mapping() const {
        return path_mapping_;
    }

    std::string Anonymizer::hash_string(const std::string& input)
    {
        return utils::compute_sha256(input).substr(0, 16);
    }

    bool Anonymizer::should_preserve_path(const std::string& path) const {
        return std::ranges::any_of(config_.preserve_patterns, [&path](const auto& pattern) {
            return path.find(pattern) != std::string::npos;
        });
    }

    std::string Anonymizer::generate_anonymous_path(const std::string& original) const
    {
        const fs::path p(original);

        std::string extension;
        if (config_.preserve_extensions && p.has_extension()) {
            extension = p.extension().string();
        }

        const std::string filename = "file_" + hash_string(original).substr(0, 8) + extension;

        if (config_.preserve_directory_structure && p.has_parent_path()) {
            const std::string dir_hash = hash_string(p.parent_path().string());
            return config_.replacement_root + "/dir_" + dir_hash.substr(0, 8) + "/" + filename;
        }

        return config_.replacement_root + "/" + filename;
    }

    std::string Anonymizer::generate_anonymous_commit() {
        const std::string short_id = utils::generate_short_id(8);
        return short_id + std::string(32, '0');
    }

} // namespace bha::security