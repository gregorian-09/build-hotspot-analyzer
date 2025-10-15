//
// Created by gregorian on 15/10/2025.
//

#include "bha/core/types.h"
#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace bha::core {

    void DependencyGraph::add_node(const std::string& file) {
        if (!has_node(file)) {
            adjacency_list_[file] = {};
            reverse_adjacency_list_[file] = {};
        }
    }

    void DependencyGraph::add_edge(const std::string& source, const std::string& target, const EdgeType type) {
        add_node(source);
        add_node(target);

        const DependencyEdge edge(target, type);
        adjacency_list_[source].push_back(edge);
        reverse_adjacency_list_[target].push_back(source);
    }

    void DependencyGraph::add_edge(const std::string& source, const DependencyEdge& edge) {
        add_node(source);
        add_node(edge.target);

        adjacency_list_[source].push_back(edge);
        reverse_adjacency_list_[edge.target].push_back(source);
    }

    bool DependencyGraph::has_node(const std::string& file) const {
        return adjacency_list_.contains(file);
    }

    bool DependencyGraph::has_edge(const std::string& source, const std::string& target) const {
        const auto it = adjacency_list_.find(source);
        if (it == adjacency_list_.end()) {
            return false;
        }

        const auto& edges = it->second;
        return std::ranges::any_of(edges,
                                   [&target](const DependencyEdge& edge) {
                                       return edge.target == target;});
    }

    std::vector<std::string> DependencyGraph::get_dependencies(const std::string& file) const {
        const auto it = adjacency_list_.find(file);
        if (it == adjacency_list_.end()) {
            return {};
        }

        std::vector<std::string> dependencies;
        dependencies.reserve(it->second.size());

        for (const auto& edge : it->second) {
            dependencies.push_back(edge.target);
        }

        return dependencies;
    }

    std::vector<std::string> DependencyGraph::get_reverse_dependencies(const std::string& file) const {
        const auto it = reverse_adjacency_list_.find(file);
        if (it == reverse_adjacency_list_.end()) {
            return {};
        }

        return it->second;
    }

    std::vector<DependencyEdge> DependencyGraph::get_edges(const std::string& file) const {
        const auto it = adjacency_list_.find(file);
        if (it == adjacency_list_.end()) {
            return {};
        }

        return it->second;
    }

    size_t DependencyGraph::node_count() const {
        return adjacency_list_.size();
    }

    size_t DependencyGraph::edge_count() const {
        size_t count = 0;
        for (const auto& edges : adjacency_list_ | std::views::values) {
            count += edges.size();
        }
        return count;
    }

    std::vector<std::string> DependencyGraph::get_all_nodes() const {
        std::vector<std::string> nodes;
        nodes.reserve(adjacency_list_.size());

        for (const auto& node : adjacency_list_ | std::views::keys) {
            nodes.push_back(node);
        }

        return nodes;
    }

    void DependencyGraph::clear() {
        adjacency_list_.clear();
        reverse_adjacency_list_.clear();
    }

    std::string to_string(const EdgeType type) {
        switch (type) {
            case EdgeType::DIRECT_INCLUDE: return "DIRECT_INCLUDE";
            case EdgeType::TRANSITIVE: return "TRANSITIVE";
            case EdgeType::PCH_REFERENCE: return "PCH_REFERENCE";
            default: return "UNKNOWN";
        }
    }

    std::string to_string(const SuggestionType type) {
        switch (type) {
            case SuggestionType::FORWARD_DECLARATION: return "FORWARD_DECLARATION";
            case SuggestionType::HEADER_SPLIT: return "HEADER_SPLIT";
            case SuggestionType::PIMPL_PATTERN: return "PIMPL_PATTERN";
            case SuggestionType::PCH_ADDITION: return "PCH_ADDITION";
            case SuggestionType::PCH_REMOVAL: return "PCH_REMOVAL";
            case SuggestionType::INCLUDE_REMOVAL: return "INCLUDE_REMOVAL";
            case SuggestionType::MOVE_TO_CPP: return "MOVE_TO_CPP";
            case SuggestionType::EXPLICIT_TEMPLATE_INSTANTIATION: return "EXPLICIT_TEMPLATE_INSTANTIATION";
            default: return "UNKNOWN";
        }
    }

    std::string to_string(const Priority priority) {
        switch (priority) {
            case Priority::CRITICAL: return "CRITICAL";
            case Priority::HIGH: return "HIGH";
            case Priority::MEDIUM: return "MEDIUM";
            case Priority::LOW: return "LOW";
            default: return "UNKNOWN";
        }
    }

    std::string to_string(const ChangeType type) {
        switch (type) {
            case ChangeType::ADD: return "ADD";
            case ChangeType::REMOVE: return "REMOVE";
            case ChangeType::REPLACE: return "REPLACE";
            default: return "UNKNOWN";
        }
    }

    EdgeType edge_type_from_string(const std::string& str) {
        if (str == "DIRECT_INCLUDE") return EdgeType::DIRECT_INCLUDE;
        if (str == "TRANSITIVE") return EdgeType::TRANSITIVE;
        if (str == "PCH_REFERENCE") return EdgeType::PCH_REFERENCE;
        throw std::invalid_argument("Unknown EdgeType: " + str);
    }

    SuggestionType suggestion_type_from_string(const std::string& str) {
        if (str == "FORWARD_DECLARATION") return SuggestionType::FORWARD_DECLARATION;
        if (str == "HEADER_SPLIT") return SuggestionType::HEADER_SPLIT;
        if (str == "PIMPL_PATTERN") return SuggestionType::PIMPL_PATTERN;
        if (str == "PCH_ADDITION") return SuggestionType::PCH_ADDITION;
        if (str == "PCH_REMOVAL") return SuggestionType::PCH_REMOVAL;
        if (str == "INCLUDE_REMOVAL") return SuggestionType::INCLUDE_REMOVAL;
        if (str == "MOVE_TO_CPP") return SuggestionType::MOVE_TO_CPP;
        if (str == "EXPLICIT_TEMPLATE_INSTANTIATION") return SuggestionType::EXPLICIT_TEMPLATE_INSTANTIATION;
        throw std::invalid_argument("Unknown SuggestionType: " + str);
    }

    Priority priority_from_string(const std::string& str) {
        if (str == "CRITICAL") return Priority::CRITICAL;
        if (str == "HIGH") return Priority::HIGH;
        if (str == "MEDIUM") return Priority::MEDIUM;
        if (str == "LOW") return Priority::LOW;
        throw std::invalid_argument("Unknown Priority: " + str);
    }

    ChangeType change_type_from_string(const std::string& str) {
        if (str == "ADD") return ChangeType::ADD;
        if (str == "REMOVE") return ChangeType::REMOVE;
        if (str == "REPLACE") return ChangeType::REPLACE;
        throw std::invalid_argument("Unknown ChangeType: " + str);
    }

} // namespace bha::core