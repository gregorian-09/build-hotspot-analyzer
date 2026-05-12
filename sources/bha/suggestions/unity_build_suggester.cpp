//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/unity_build_suggester.hpp"
#include "bha/suggestions/cmake_classification_utils.hpp"
#include "bha/suggestions/unreal_context.hpp"
#include "bha/utils/path_utils.hpp"
#include "bha/utils/regex_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <numeric>
#include <queue>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        /**
         * Symbol conflict types that can occur in unity builds.
         *
         * Based on research from Chromium and LLVM unity build implementations:
         * - Static functions/variables with same name across files
         * - Anonymous namespace collisions
         * - Macro redefinitions
         */
        enum class ConflictType {
            None,
            StaticSymbol,        // static function/variable name collision
            AnonymousNamespace,  // anonymous namespace symbol collision
            MacroRedefinition,   // #define collision
            UnguardedHeader,     // header without include guards included by both
            IncludedSource       // source file included by another source
        };

        /**
         * Detected conflict between files.
         */
        struct SymbolConflict {
            std::string symbol_name;
            ConflictType type = ConflictType::None;
            fs::path file_a;
            fs::path file_b;
            std::string description;
        };

        /**
         * File metadata for unity build analysis.
         *
         * Includes symbol information for conflict detection.
         */
        struct FileMetadata {
            fs::path path;
            Duration compile_time = Duration::zero();

            // Symbol information for conflict detection
            std::unordered_set<std::string> static_symbols;
            std::unordered_set<std::string> anon_namespace_symbols;
            std::unordered_map<std::string, std::string> defined_macros;
            std::unordered_set<std::string> unguarded_headers;
            std::unordered_set<std::string> included_sources;

            // Include information
            std::unordered_set<std::string> includes;
            std::size_t include_depth = 0;

            // Memory estimate (preprocessed size as proxy)
            std::size_t memory_estimate = 0;
        };

        /**
         * Represents a group of source files for unity building.
         */
        struct UnityGroup {
            std::vector<FileMetadata> files;
            std::unordered_set<std::string> common_includes;
            Duration total_compile_time = Duration::zero();
            std::size_t total_includes = 0;
            std::size_t total_memory_estimate = 0;
            std::string suggested_name;
            std::vector<SymbolConflict> potential_conflicts;
            double conflict_risk_score = 0.0;  // 0-1, higher = more conflicts
        };

        /**
         * Distance matrix for hierarchical clustering.
         *
         * Uses Jaccard distance (1 - similarity) based on include patterns.
         * Also factors in compile time similarity for better grouping.
         */
        class DistanceMatrix {
        public:
            explicit DistanceMatrix(const std::size_t n)
                : n_(n), distances_(n * n, 0.0) {}

            void set(const std::size_t i, const std::size_t j, const double distance) {
                distances_[i * n_ + j] = distance;
                distances_[j * n_ + i] = distance;
            }

            [[nodiscard]] double get(const std::size_t i, const std::size_t j) const {
                return distances_[i * n_ + j];
            }

            [[nodiscard]] std::size_t size() const { return n_; }

        private:
            std::size_t n_;
            std::vector<double> distances_;
        };

        /**
         * Extracts the directory/module name from a path for grouping.
         */
        std::string get_module_name(const fs::path& path) {
            const fs::path parent = path.parent_path();
            if (parent.empty() || parent == path.root_path()) {
                return "root";
            }
            return parent.filename().string();
        }

        /**
         * Calculates Jaccard similarity between two include sets.
         *
         * Jaccard similarity: |A ∩ B| / |A ∪ B|
         * Range: [0, 1] where 1 = identical sets
         */
        double calculate_jaccard_similarity(
            const std::unordered_set<std::string>& set_a,
            const std::unordered_set<std::string>& set_b
        ) {
            if (set_a.empty() && set_b.empty()) {
                return 1.0;
            }
            if (set_a.empty() || set_b.empty()) {
                return 0.0;
            }

            std::size_t intersection = 0;
            for (const auto& item : set_a) {
                if (set_b.contains(item)) {
                    ++intersection;
                }
            }

            const std::size_t union_size = set_a.size() + set_b.size() - intersection;
            return static_cast<double>(intersection) / static_cast<double>(union_size);
        }

        /**
         * Calculates compile time similarity.
         *
         * Files with similar compile times are better candidates for unity builds
         * as they balance the workload better.
         *
         * Uses normalized difference: 1 - |a-b| / max(a,b)
         */
        double calculate_time_similarity(const Duration time_a, const Duration time_b) {
            const auto a = time_a.count();
            const auto b = time_b.count();

            if (a == 0 && b == 0) {
                return 1.0;
            }

            const auto max_time = std::max(a, b);
            const auto diff = std::abs(a - b);

            return 1.0 - static_cast<double>(diff) / static_cast<double>(max_time);
        }

        /**
         * Calculates composite distance between two files.
         *
         * Weighted combination of:
         * - Include similarity (60%): Files sharing headers benefit most
         * - Compile time similarity (20%): Balance workload
         * - Directory distance (20%): Files in same directory often related
         *
         * Based on research from Chromium's jumbo build implementation.
         */
        double calculate_file_distance(
            const FileMetadata& file_a,
            const FileMetadata& file_b
        ) {
            const double include_sim = calculate_jaccard_similarity(
                file_a.includes, file_b.includes);

            const double time_sim = calculate_time_similarity(
                file_a.compile_time, file_b.compile_time);

            const double dir_sim = (file_a.path.parent_path() == file_b.path.parent_path())
                             ? 1.0 : 0.0;

            const double similarity = 0.6 * include_sim + 0.2 * time_sim + 0.2 * dir_sim;

            return 1.0 - similarity; // Distance = 1 - similarity
        }

        /**
         * Detects potential symbol conflicts between two files.
         *
         * Checks for:
         * - Static symbol collisions
         * - Anonymous namespace collisions
         * - Macro redefinitions
         * - Unguarded header re-inclusion
         *
         * Based on common issues found in Chromium and UE4 unity builds.
         */
        std::vector<SymbolConflict> detect_conflicts(
            const FileMetadata& file_a,
            const FileMetadata& file_b
        ) {
            std::vector<SymbolConflict> conflicts;

            // Check static symbol collisions
            for (const auto& sym : file_a.static_symbols) {
                if (file_b.static_symbols.contains(sym)) {
                    SymbolConflict conflict;
                    conflict.symbol_name = sym;
                    conflict.type = ConflictType::StaticSymbol;
                    conflict.file_a = file_a.path;
                    conflict.file_b = file_b.path;
                    conflict.description = "Static symbol '" + sym +
                        "' defined in both files - will cause linker error in unity build";
                    conflicts.push_back(conflict);
                }
            }

            // Check anonymous namespace collisions
            for (const auto& sym : file_a.anon_namespace_symbols) {
                if (file_b.anon_namespace_symbols.contains(sym)) {
                    SymbolConflict conflict;
                    conflict.symbol_name = sym;
                    conflict.type = ConflictType::AnonymousNamespace;
                    conflict.file_a = file_a.path;
                    conflict.file_b = file_b.path;
                    conflict.description = "Anonymous namespace symbol '" + sym +
                        "' in both files - will cause ODR violation";
                    conflicts.push_back(conflict);
                }
            }

            // Check macro redefinitions
            for (const auto& [macro, value_a] : file_a.defined_macros) {
                const auto it = file_b.defined_macros.find(macro);
                if (it != file_b.defined_macros.end()) {
                    const std::string& value_b = it->second;
                    if (value_a == value_b) {
                        continue;
                    }
                    SymbolConflict conflict;
                    conflict.symbol_name = macro;
                    conflict.type = ConflictType::MacroRedefinition;
                    conflict.file_a = file_a.path;
                    conflict.file_b = file_b.path;
                    conflict.description = "Macro '" + macro +
                        "' defined with different values in both files - may cause unexpected behavior";
                    conflicts.push_back(conflict);
                }
            }

            for (const auto& header : file_a.unguarded_headers) {
                if (file_b.unguarded_headers.contains(header)) {
                    SymbolConflict conflict;
                    conflict.symbol_name = header;
                    conflict.type = ConflictType::UnguardedHeader;
                    conflict.file_a = file_a.path;
                    conflict.file_b = file_b.path;
                    conflict.description = "Header '" + header +
                        "' lacks include guards and is included by both files";
                    conflicts.push_back(conflict);
                }
            }

            return conflicts;
        }

        /**
         * Calculates conflict risk score for a group.
         *
         * Returns a score from 0 to 1 where:
         * - 0: No detected conflicts
         * - 0.5: Some potential conflicts (macros, anonymous namespace)
         * - 1.0: Definite conflicts (static symbols)
         */
        double calculate_conflict_risk(const std::vector<SymbolConflict>& conflicts) {
            if (conflicts.empty()) {
                return 0.0;
            }

            double risk = 0.0;
            for (const auto& conflict : conflicts) {
                switch (conflict.type) {
                case ConflictType::StaticSymbol:
                    risk = std::max(risk, 1.0);  // Definite error
                    break;
                case ConflictType::AnonymousNamespace:
                    risk = std::max(risk, 0.8);  // Likely error
                    break;
                case ConflictType::MacroRedefinition:
                    risk = std::max(risk, 0.5);  // Potential issue
                    break;
                case ConflictType::UnguardedHeader:
                    risk = std::max(risk, 0.7);  // Likely redefinition
                    break;
                case ConflictType::IncludedSource:
                    risk = std::max(risk, 0.9);  // Very likely error
                    break;
                default:
                    break;
                }
            }

            return std::min(risk, 1.0);
        }

        /**
         * Agglomerative hierarchical clustering with complete linkage.
         *
         * Groups files based on include similarity and compile time characteristics.
         * Complete linkage ensures all files in a cluster are similar to each other.
         *
         * Based on clustering approach used in LLVM's unity builds.
         */
        std::vector<std::vector<std::size_t>> hierarchical_clustering(
            const std::vector<FileMetadata>& files,
            double distance_threshold,
            const std::size_t max_cluster_size
        ) {
            const std::size_t n = files.size();
            if (n == 0) {
                return {};
            }

            DistanceMatrix distances(n);
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = i + 1; j < n; ++j) {
                    const double dist = calculate_file_distance(files[i], files[j]);
                    distances.set(i, j, dist);
                }
            }

            std::vector<std::vector<std::size_t>> clusters;
            clusters.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                clusters.push_back({i});
            }

            std::vector active(n, true);

            // Agglomerative clustering loop
            while (true) {
                // Find the closest pair of active clusters
                double min_distance = std::numeric_limits<double>::max();
                std::size_t best_i = 0, best_j = 0;
                bool found = false;

                for (std::size_t i = 0; i < clusters.size(); ++i) {
                    if (!active[i]) {
                        continue;
                    }

                    for (std::size_t j = i + 1; j < clusters.size(); ++j) {
                        if (!active[j]) {
                            continue;
                        }

                        // Check size constraint
                        if (clusters[i].size() + clusters[j].size() > max_cluster_size) {
                            continue;
                        }

                        // Complete linkage: max distance between any two points
                        double max_dist = 0.0;
                        for (const std::size_t idx_i : clusters[i]) {
                            for (const std::size_t idx_j : clusters[j]) {
                                max_dist = std::max(max_dist, distances.get(idx_i, idx_j));
                            }
                        }

                        if (max_dist < min_distance) {
                            min_distance = max_dist;
                            best_i = i;
                            best_j = j;
                            found = true;
                        }
                    }
                }

                if (!found || min_distance > distance_threshold) {
                    break;
                }

                // Merge clusters
                for (const std::size_t idx : clusters[best_j]) {
                    clusters[best_i].push_back(idx);
                }
                active[best_j] = false;
            }

            // Collect final clusters (only those with 2+ files)
            std::vector<std::vector<std::size_t>> result;
            for (std::size_t i = 0; i < clusters.size(); ++i) {
                if (active[i] && clusters[i].size() >= 2) {
                    result.push_back(clusters[i]);
                }
            }

            return result;
        }

        std::string strip_comments(const std::string& line, bool& in_block) {
            std::string out;
            out.reserve(line.size());
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (!in_block && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
                    in_block = true;
                    ++i;
                    continue;
                }
                if (in_block && i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
                    in_block = false;
                    ++i;
                    continue;
                }
                if (!in_block && i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
                    break;
                }
                if (!in_block) {
                    out.push_back(line[i]);
                }
            }
            return out;
        }

        std::string trim_left(std::string s) {
            std::size_t i = 0;
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
                ++i;
            }
            s.erase(0, i);
            return s;
        }

        struct CMakeCommandStart {
            std::string name;
            std::size_t open_pos = 0;
        };

        struct CMakeTargetInfo {
            std::string name;
            std::size_t start_line = 0;
            std::size_t end_line = 0;
            bool is_macro = false;
            std::vector<std::string> source_tokens;
        };

        struct CMakeTargetSelection {
            fs::path cmake_path;
            CMakeTargetInfo target;
            int score = 0;
            std::size_t exact_hits = 0;
        };

        std::string to_lower_ascii(std::string_view input) {
            std::string out(input);
            std::ranges::transform(out, out.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        bool contains_ci(std::string_view haystack, std::string_view needle) {
            if (needle.empty()) {
                return true;
            }
            return to_lower_ascii(haystack).find(to_lower_ascii(needle)) != std::string::npos;
        }

        bool path_has_component_ci(const fs::path& path, std::string_view component) {
            const std::string needle = to_lower_ascii(component);
            for (const auto& part : path) {
                if (to_lower_ascii(part.string()) == needle) {
                    return true;
                }
            }
            return false;
        }

        bool is_probable_generated_unity_source(const fs::path& path) {
            const std::string filename = to_lower_ascii(path.filename().string());
            if (filename.rfind("unity_", 0) == std::string::npos &&
                filename.find("_unity_") == std::string::npos) {
                return false;
            }
            return path_has_component_ci(path, "unity") ||
                   path_has_component_ci(path, "cmakefiles") ||
                   path_has_component_ci(path, "build");
        }

        std::optional<CMakeCommandStart> parse_cmake_command_start(std::string_view line) {
            if (line.empty()) {
                return std::nullopt;
            }
            const unsigned char first = static_cast<unsigned char>(line.front());
            if (!std::isalpha(first) && line.front() != '_') {
                return std::nullopt;
            }
            std::size_t i = 1;
            while (i < line.size()) {
                const unsigned char ch = static_cast<unsigned char>(line[i]);
                if (std::isalnum(ch) || line[i] == '_') {
                    ++i;
                    continue;
                }
                break;
            }
            std::size_t j = i;
            while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
                ++j;
            }
            if (j >= line.size() || line[j] != '(') {
                return std::nullopt;
            }
            return CMakeCommandStart{std::string(line.substr(0, i)), j};
        }

        int count_paren_delta_outside_quotes(std::string_view text) {
            int delta = 0;
            bool in_quote = false;
            char quote = '\0';
            for (const char c : text) {
                if (in_quote) {
                    if (c == quote) {
                        in_quote = false;
                    }
                    continue;
                }
                if (c == '"' || c == '\'') {
                    in_quote = true;
                    quote = c;
                    continue;
                }
                if (c == '(') {
                    ++delta;
                } else if (c == ')') {
                    --delta;
                }
            }
            return delta;
        }

        std::vector<std::string> tokenize_cmake_args(std::string_view args) {
            std::vector<std::string> tokens;
            std::string current;
            bool in_quote = false;
            char quote = '\0';

            auto flush = [&]() {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            };

            for (std::size_t i = 0; i < args.size(); ++i) {
                const char c = args[i];
                if (in_quote) {
                    if (c == quote) {
                        in_quote = false;
                    } else {
                        current.push_back(c);
                    }
                    continue;
                }
                if (c == '"' || c == '\'') {
                    in_quote = true;
                    quote = c;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(c)) || c == ';') {
                    flush();
                    continue;
                }
                current.push_back(c);
            }
            flush();
            return tokens;
        }

        bool is_probable_source_token(std::string_view token) {
            return suggestions::is_probable_source_token(token, CMakeSourceTokenMode::Strict);
        }

        bool is_scope_or_target_keyword(std::string_view token) {
            const std::string key = to_lower_ascii(token);
            static const std::unordered_set<std::string> kKeywords = {
                "public", "private", "interface",
                "before", "after",
                "static", "shared", "module", "object", "interface", "alias",
                "exclude_from_all", "win32", "macosx_bundle"
            };
            return kKeywords.contains(key);
        }

        bool is_probable_cmake_target_name(std::string_view name) {
            return suggestions::is_probable_cmake_target_name(name, CMakeTargetNameMode::Strict);
        }

        bool is_target_like_macro(std::string_view name) {
            const std::string lower = to_lower_ascii(name);
            return lower.find("library") != std::string::npos ||
                   lower.find("executable") != std::string::npos ||
                   lower.find("binary") != std::string::npos ||
                   lower.find("target") != std::string::npos;
        }

        bool is_macro_keyword(std::string_view token) {
            return suggestions::is_macro_keyword_lower(token);
        }

        std::optional<std::string> extract_macro_target_name(const std::vector<std::string>& tokens) {
            if (tokens.empty()) {
                return std::nullopt;
            }
            for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
                if (to_lower_ascii(tokens[i]) != "name") {
                    continue;
                }
                if (is_probable_cmake_target_name(tokens[i + 1])) {
                    return tokens[i + 1];
                }
                return std::nullopt;
            }
            if (is_probable_cmake_target_name(tokens.front())) {
                return tokens.front();
            }
            return std::nullopt;
        }

        std::vector<std::string> extract_macro_sources(const std::vector<std::string>& tokens) {
            std::vector<std::string> sources;
            for (std::size_t i = 0; i < tokens.size(); ++i) {
                const std::string key = to_lower_ascii(tokens[i]);
                if (key != "srcs" && key != "sources" && key != "src" && key != "source") {
                    continue;
                }
                for (std::size_t j = i + 1; j < tokens.size(); ++j) {
                    if (is_macro_keyword(tokens[j])) {
                        break;
                    }
                    if (is_probable_source_token(tokens[j])) {
                        sources.push_back(tokens[j]);
                    }
                }
            }
            return sources;
        }

        std::optional<std::string> extract_builtin_target_name(
            std::string_view command,
            const std::vector<std::string>& tokens
        ) {
            if (tokens.empty()) {
                return std::nullopt;
            }
            const std::string lower_command = to_lower_ascii(command);
            if (lower_command == "add_library" || lower_command == "add_executable" ||
                lower_command == "target_sources") {
                if (is_probable_cmake_target_name(tokens.front())) {
                    return tokens.front();
                }
            }
            return std::nullopt;
        }

        std::vector<std::string> extract_builtin_sources(
            std::string_view command,
            const std::vector<std::string>& tokens
        ) {
            std::vector<std::string> sources;
            if (tokens.size() < 2) {
                return sources;
            }
            const std::string lower_command = to_lower_ascii(command);
            std::size_t i = 1;
            if (lower_command == "add_library" || lower_command == "add_executable") {
                while (i < tokens.size() && is_scope_or_target_keyword(tokens[i])) {
                    ++i;
                }
                if (i < tokens.size() && to_lower_ascii(tokens[i]) == "alias") {
                    return sources;
                }
                for (; i < tokens.size(); ++i) {
                    if (is_probable_source_token(tokens[i])) {
                        sources.push_back(tokens[i]);
                    }
                }
                return sources;
            }
            if (lower_command == "target_sources") {
                for (; i < tokens.size(); ++i) {
                    if (is_scope_or_target_keyword(tokens[i])) {
                        continue;
                    }
                    if (is_probable_source_token(tokens[i])) {
                        sources.push_back(tokens[i]);
                    }
                }
            }
            return sources;
        }

        std::vector<CMakeTargetInfo> parse_cmake_targets(const std::string& content) {
            std::vector<CMakeTargetInfo> targets;
            std::unordered_map<std::string, std::size_t> by_name;

            auto upsert_target = [&](CMakeTargetInfo&& candidate) {
                if (!is_probable_cmake_target_name(candidate.name)) {
                    return;
                }
                if (auto it = by_name.find(candidate.name); it != by_name.end()) {
                    auto& existing = targets[it->second];
                    existing.end_line = std::max(existing.end_line, candidate.end_line);
                    existing.source_tokens.insert(
                        existing.source_tokens.end(),
                        candidate.source_tokens.begin(),
                        candidate.source_tokens.end()
                    );
                    if (!existing.is_macro && candidate.is_macro) {
                        existing.is_macro = true;
                    }
                    return;
                }
                by_name.emplace(candidate.name, targets.size());
                targets.push_back(std::move(candidate));
            };

            std::istringstream input(content);
            std::string line;
            std::size_t line_num = 0;
            std::string pending;
            std::size_t pending_line = 0;
            int paren_depth = 0;
            bool collecting = false;

            while (std::getline(input, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (trimmed.empty() || trimmed.rfind("#", 0) == 0) {
                    ++line_num;
                    continue;
                }

                if (!collecting) {
                    const auto start = parse_cmake_command_start(trimmed);
                    if (!start) {
                        ++line_num;
                        continue;
                    }
                    collecting = true;
                    pending = trimmed;
                    pending_line = line_num;
                    paren_depth = 0;
                } else {
                    pending += " " + trimmed;
                }

                paren_depth += count_paren_delta_outside_quotes(trimmed);
                if (collecting && paren_depth <= 0) {
                    const auto open = pending.find('(');
                    const auto close = pending.rfind(')');
                    if (open != std::string::npos && close != std::string::npos && close > open) {
                        const std::string command = to_lower_ascii(pending.substr(0, open));
                        const std::string args = pending.substr(open + 1, close - open - 1);
                        const auto tokens = tokenize_cmake_args(args);

                        if (command == "add_library" || command == "add_executable" || command == "target_sources") {
                            if (auto target_name = extract_builtin_target_name(command, tokens)) {
                                CMakeTargetInfo target;
                                target.name = *target_name;
                                target.start_line = pending_line;
                                target.end_line = line_num;
                                target.source_tokens = extract_builtin_sources(command, tokens);
                                upsert_target(std::move(target));
                            }
                        } else if (is_target_like_macro(command)) {
                            if (auto target_name = extract_macro_target_name(tokens)) {
                                CMakeTargetInfo target;
                                target.name = *target_name;
                                target.start_line = pending_line;
                                target.end_line = line_num;
                                target.is_macro = true;
                                target.source_tokens = extract_macro_sources(tokens);
                                upsert_target(std::move(target));
                            }
                        }
                    }
                    collecting = false;
                    pending.clear();
                }

                ++line_num;
            }

            for (auto& target : targets) {
                std::ranges::sort(target.source_tokens);
                target.source_tokens.erase(
                    std::unique(target.source_tokens.begin(), target.source_tokens.end()),
                    target.source_tokens.end()
                );
            }

            return targets;
        }

        bool is_excluded_cmake_path(const fs::path& path) {
            return suggestions::is_excluded_cmake_path(path);
        }

        bool cmake_has_global_unity_enabled(const std::string& content) {
            static const std::regex global_re(R"(set\s*\(\s*CMAKE_UNITY_BUILD\s+ON\b)", std::regex::icase);
            return std::regex_search(content, global_re);
        }

        bool cmake_target_has_unity_enabled(const std::string& content, const std::string& target_name) {
            const std::string escaped = utils::regex_escape(target_name);
            const std::regex set_prop_re(
                "set_property\\s*\\(\\s*TARGET\\s+" + escaped + "\\s+PROPERTY\\s+UNITY_BUILD\\s+ON\\b",
                std::regex::icase
            );
            if (std::regex_search(content, set_prop_re)) {
                return true;
            }
            const std::regex target_props_re(
                "set_target_properties\\s*\\(\\s*" + escaped + "\\b[^\\)]*UNITY_BUILD\\s+ON",
                std::regex::icase
            );
            return std::regex_search(content, target_props_re);
        }

        std::string normalized_key(const fs::path& path) {
            std::error_code ec;
            fs::path normalized = path;
            if (normalized.is_relative()) {
                normalized = fs::absolute(normalized, ec);
                if (ec) {
                    normalized = path.lexically_normal();
                }
            } else {
                normalized = normalized.lexically_normal();
            }
            return normalized.generic_string();
        }

        fs::path resolve_cmake_source_token(
            const std::string& token,
            const fs::path& cmake_dir,
            const fs::path& project_root
        ) {
            if (token.empty()) {
                return {};
            }
            const fs::path candidate(token);
            if (candidate.is_absolute()) {
                return candidate.lexically_normal();
            }

            std::vector<fs::path> probes;
            probes.push_back((cmake_dir / candidate).lexically_normal());
            if (!project_root.empty()) {
                probes.push_back((project_root / candidate).lexically_normal());
            }

            std::error_code ec;
            for (const auto& probe : probes) {
                if (fs::exists(probe, ec) && !ec) {
                    return probe;
                }
                ec.clear();
            }
            return probes.front();
        }

        struct CMakeTargetScore {
            int score = 0;
            std::size_t exact_hits = 0;
            std::size_t name_hits = 0;
        };

        CMakeTargetScore score_cmake_target_for_group(
            const CMakeTargetInfo& target,
            const fs::path& cmake_path,
            const fs::path& project_root,
            const std::unordered_set<std::string>& group_keys,
            const std::unordered_set<std::string>& group_filenames
        ) {
            CMakeTargetScore result;
            const fs::path cmake_dir = cmake_path.parent_path();

            for (const auto& token : target.source_tokens) {
                const fs::path resolved = resolve_cmake_source_token(token, cmake_dir, project_root);
                if (resolved.empty()) {
                    continue;
                }
                if (const std::string key = normalized_key(resolved); group_keys.contains(key)) {
                    result.score += 60;
                    ++result.exact_hits;
                    continue;
                }
                if (group_filenames.contains(to_lower_ascii(resolved.filename().string()))) {
                    result.score += 8;
                    ++result.name_hits;
                }
            }

            const std::string lower_target = to_lower_ascii(target.name);
            if (contains_ci(lower_target, "test") ||
                contains_ci(lower_target, "bench") ||
                contains_ci(lower_target, "mock") ||
                contains_ci(lower_target, "example")) {
                result.score -= 20;
            }

            if (result.exact_hits > 0) {
                result.score += 15;
            } else if (result.name_hits == 0 && target.source_tokens.empty()) {
                result.score -= 5;
            }

            return result;
        }

        bool cmake_tree_has_global_unity_enabled(const fs::path& project_root, const std::function<bool()>& should_cancel) {
            if (project_root.empty() || !fs::exists(project_root)) {
                return false;
            }
            std::error_code ec;
            fs::recursive_directory_iterator it(project_root, ec);
            const fs::recursive_directory_iterator end;
            for (; it != end && !ec; ++it) {
                if (should_cancel && should_cancel()) {
                    break;
                }
                const fs::path path = it->path();
                if (it->is_directory(ec) && is_excluded_cmake_path(path)) {
                    it.disable_recursion_pending();
                    ec.clear();
                    continue;
                }
                if (!it->is_regular_file(ec) || path.filename() != "CMakeLists.txt") {
                    ec.clear();
                    continue;
                }
                std::ifstream in(path);
                if (!in) {
                    continue;
                }
                const std::string content((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
                if (cmake_has_global_unity_enabled(content)) {
                    return true;
                }
            }
            return false;
        }

        std::optional<CMakeTargetSelection> find_best_cmake_target_for_group(
            const fs::path& project_root,
            const std::vector<fs::path>& group_files,
            const std::function<bool()>& should_cancel
        ) {
            if (project_root.empty() || !fs::exists(project_root)) {
                return std::nullopt;
            }

            std::unordered_set<std::string> group_keys;
            std::unordered_set<std::string> group_filenames;
            for (const auto& path : group_files) {
                const fs::path resolved = resolve_source_path(path);
                group_keys.insert(normalized_key(resolved));
                group_filenames.insert(to_lower_ascii(resolved.filename().string()));
            }

            std::optional<CMakeTargetSelection> best;
            std::error_code ec;
            fs::recursive_directory_iterator it(project_root, ec);
            const fs::recursive_directory_iterator end;
            for (; it != end && !ec; ++it) {
                if (should_cancel && should_cancel()) {
                    break;
                }
                const fs::path path = it->path();
                if (it->is_directory(ec) && is_excluded_cmake_path(path)) {
                    it.disable_recursion_pending();
                    ec.clear();
                    continue;
                }
                if (!it->is_regular_file(ec) || path.filename() != "CMakeLists.txt") {
                    ec.clear();
                    continue;
                }

                std::ifstream in(path);
                if (!in) {
                    continue;
                }
                const std::string content((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
                auto targets = parse_cmake_targets(content);
                for (const auto& target : targets) {
                    const auto target_score = score_cmake_target_for_group(
                        target,
                        path,
                        project_root,
                        group_keys,
                        group_filenames
                    );
                    if (target_score.score <= 0) {
                        continue;
                    }
                    if (!best || target_score.score > best->score ||
                        (target_score.score == best->score && path.generic_string() < best->cmake_path.generic_string())) {
                        CMakeTargetSelection selection;
                        selection.cmake_path = path;
                        selection.target = target;
                        selection.score = target_score.score;
                        selection.exact_hits = target_score.exact_hits;
                        best = std::move(selection);
                    }
                }
            }

            if (best && best->exact_hits != group_files.size()) {
                return std::nullopt;
            }

            return best;
        }

        bool header_has_guard(const fs::path& header_path, std::unordered_map<std::string, bool>& cache) {
            const std::string key = header_path.string();
            if (auto it = cache.find(key); it != cache.end()) {
                return it->second;
            }

            std::ifstream in(header_path);
            if (!in) {
                cache.emplace(key, true);
                return true;
            }

            std::string content;
            content.reserve(4096);
            std::string line;
            std::size_t lines = 0;
            while (std::getline(in, line) && lines++ < 200) {
                content.append(line);
                content.push_back('\n');
            }

            if (content.find("#pragma once") != std::string::npos) {
                cache.emplace(key, true);
                return true;
            }

            const std::regex guard_ifndef(R"(^\s*#\s*ifndef\s+([A-Za-z_]\w+))");
            const std::regex guard_define(R"(^\s*#\s*define\s+([A-Za-z_]\w+))");
            std::smatch match;
            std::string guard_name;

            std::istringstream input(content);
            lines = 0;
            while (std::getline(input, line) && lines++ < 200) {
                if (guard_name.empty() && std::regex_search(line, match, guard_ifndef)) {
                    guard_name = match[1].str();
                    continue;
                }
                if (!guard_name.empty() && std::regex_search(line, match, guard_define)) {
                    if (match[1].str() == guard_name) {
                        cache.emplace(key, true);
                        return true;
                    }
                }
            }

            cache.emplace(key, false);
            return false;
        }

        void scan_source_for_conflicts(const fs::path& path, FileMetadata& meta) {
            std::ifstream in(path);
            if (!in) {
                return;
            }

            const std::regex static_func(R"(^\s*static\s+(?:inline\s+)?(?:constexpr\s+)?[\w:\<\>\*\&\s]+\s+([A-Za-z_]\w*)\s*\()");
            const std::regex static_var(R"(^\s*static\s+(?:const\s+)?[\w:\<\>\*\&\s]+\s+([A-Za-z_]\w*)\s*(=|;|\[))");
            const std::regex func_decl(R"(^\s*(?:inline\s+)?(?:constexpr\s+)?[\w:\<\>\*\&\s]+\s+([A-Za-z_]\w*)\s*\()");
            const std::regex macro_def(R"(^\s*#\s*define\s+([A-Za-z_]\w*)(?:\s+(.*))?$)");
            const std::regex macro_undef(R"(^\s*#\s*undef\s+([A-Za-z_]\w+))");
            const std::regex anon_ns(R"(\bnamespace\s*\{)");
            const std::regex include_re(R"(^\s*#\s*include\s+[<\"]([^>\"]+)[>\"])");

            bool in_block = false;
            int brace_depth = 0;
            int anon_start_depth = -1;
            std::string line;

            while (std::getline(in, line)) {
                const std::string cleaned = strip_comments(line, in_block);
                const std::string trimmed = trim_left(cleaned);
                if (trimmed.empty()) {
                    continue;
                }

                if (std::regex_search(trimmed, macro_def)) {
                    std::smatch match;
                    if (std::regex_search(trimmed, match, macro_def)) {
                        std::string value;
                        if (match.size() >= 3) {
                            value = match[2].str();
                            value.erase(0, value.find_first_not_of(" \t\r\n"));
                            const auto last = value.find_last_not_of(" \t\r\n");
                            if (last == std::string::npos) {
                                value.clear();
                            } else {
                                value.erase(last + 1);
                            }
                        }
                        meta.defined_macros[match[1].str()] = value;
                    }
                }
                if (std::regex_search(trimmed, macro_undef)) {
                    std::smatch match;
                    if (std::regex_search(trimmed, match, macro_undef)) {
                        meta.defined_macros.erase(match[1].str());
                    }
                }

                if (trimmed.find("static_assert") == std::string::npos) {
                    std::smatch match;
                    if (std::regex_search(trimmed, match, static_func) ||
                        std::regex_search(trimmed, match, static_var)) {
                        meta.static_symbols.insert(match[1].str());
                    }
                }

                if (std::regex_search(trimmed, include_re)) {
                    std::smatch match;
                    if (std::regex_search(trimmed, match, include_re)) {
                        const fs::path include_path(match[1].str());
                        const std::string ext = include_path.extension().string();
                        if (ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++") {
                            meta.included_sources.insert(match[1].str());
                        }
                    }
                }

                if (std::regex_search(trimmed, anon_ns)) {
                    anon_start_depth = brace_depth;
                }

                for (const char c : trimmed) {
                    if (c == '{') {
                        ++brace_depth;
                    } else if (c == '}') {
                        --brace_depth;
                        if (anon_start_depth >= 0 && brace_depth <= anon_start_depth) {
                            anon_start_depth = -1;
                        }
                    }
                }

                if (anon_start_depth >= 0) {
                    std::smatch match;
                    if (trimmed.find("static_assert") == std::string::npos &&
                        std::regex_search(trimmed, match, func_decl)) {
                        meta.anon_namespace_symbols.insert(match[1].str());
                    }
                }
            }
        }

        /**
         * Builds file metadata from analysis results.
         *
         * Extracts symbol information for conflict detection.
         */
        std::vector<FileMetadata> build_file_metadata(
            const std::vector<analyzers::FileAnalysisResult>& files,
            const analyzers::DependencyAnalysisResult& deps,
            const analyzers::SymbolAnalysisResult& symbols
        ) {
            std::vector<FileMetadata> metadata;
            std::unordered_map<std::string, bool> guard_cache;

            std::unordered_map<std::string, std::unordered_set<std::string>> file_includes;
            for (const auto& header : deps.headers) {
                for (const auto& includer : header.included_by) {
                    file_includes[resolve_source_path(includer).string()].insert(header.path.string());
                }
            }

            // Build symbol map per file
            // Note: Using heuristics since SymbolInfo doesn't have linkage info
            std::unordered_map<std::string, std::unordered_set<std::string>> file_static_symbols;
            std::unordered_map<std::string, std::unordered_set<std::string>> file_anon_symbols;

            for (const auto& sym : symbols.symbols) {
                const std::string file_key = sym.defined_in.string();

                bool likely_internal = false;
                bool likely_anon_namespace = false;

                if (!sym.name.empty()) {
                    likely_internal =
                        sym.name[0] == '_' ||
                        (sym.name.find("::") != std::string::npos &&
                         std::islower(static_cast<unsigned char>(sym.name[0]))) ||
                        sym.name.find("_L") == 0 ||
                        sym.name.find("_Z") == 0;

                    likely_anon_namespace =
                        sym.name.find("_GLOBAL__N") != std::string::npos ||
                        sym.name.find("(anonymous namespace)") != std::string::npos ||
                        sym.name.find("::$") != std::string::npos ||
                        sym.name.find("anonymous") != std::string::npos;
                }

                if (likely_internal && !likely_anon_namespace) {
                    file_static_symbols[file_key].insert(sym.name);
                }

                if (likely_anon_namespace) {
                    file_anon_symbols[file_key].insert(sym.name);
                }
            }

            for (const auto& file : files) {
                if (!is_source_file_path(file.file)) {
                    continue;
                }
                const fs::path resolved_path = resolve_source_path(file.file);
                if (is_probable_generated_unity_source(resolved_path)) {
                    continue;
                }

                FileMetadata meta;
                meta.path = resolved_path;
                meta.compile_time = file.compile_time;

                const std::string file_key = resolved_path.string();

                if (file_includes.contains(file_key)) {
                    meta.includes = file_includes[file_key];
                }

                if (file_static_symbols.contains(file_key)) {
                    meta.static_symbols = file_static_symbols[file_key];
                }

                if (file_anon_symbols.contains(file_key)) {
                    meta.anon_namespace_symbols = file_anon_symbols[file_key];
                }

                scan_source_for_conflicts(resolved_path, meta);

                for (const auto& include_path : meta.includes) {
                    const fs::path header_path(include_path);
                    if (header_path.extension() != ".h" &&
                        header_path.extension() != ".hpp" &&
                        header_path.extension() != ".hh" &&
                        header_path.extension() != ".hxx") {
                        continue;
                    }
                    if (!fs::exists(header_path)) {
                        continue;
                    }
                    if (!header_has_guard(header_path, guard_cache)) {
                        meta.unguarded_headers.insert(header_path.string());
                    }
                }

                // Note: Memory estimation not currently implemented
                // Would require line count data from compiler trace

                meta.include_depth = file.include_count;  // Use include_count as proxy

                metadata.push_back(meta);
            }

            return metadata;
        }

        /**
         * Estimates savings from unity building based on research.
         *
         * Model based on measurements from Chromium and UE4:
         * - Header parsing: 30-60% of compile time [1][2]
         * - Template instantiation: 10-20% of compile time
         * - Shared savings: (1 - 1/N) * shared_ratio
         *
         * Research sources:
         * [1] Microsoft C++ Build Insights
         * [2] Aras Pranckevičius compile time investigation
         * [3] JetBrains Unity Build Study (18-54% reduction)
         * [5] Chromium Jumbo Builds (50 files per unit)
         */
        Duration estimate_unity_savings(
            const UnityGroup& group,
            const heuristics::UnityBuildConfig& config
        ) {
            if (group.files.size() < 2) {
                return Duration::zero();
            }

            const auto total_ns = group.total_compile_time.count();
            const auto n = static_cast<double>(group.files.size());

            // Base shared ratio from header parsing
            // Research: headers are 40-60% of compile time
            double header_ratio = config.header_parsing_ratio;

            // Adjust based on common include count
            // More shared includes = higher savings
            if (group.total_includes > 30) {
                header_ratio = std::min(0.60, header_ratio + 0.15);
            } else if (group.total_includes > 15) {
                header_ratio = std::min(0.55, header_ratio + 0.10);
            } else if (group.total_includes < 5) {
                header_ratio = std::max(0.30, header_ratio - 0.10);
            }

            // Template instantiation sharing (~10% additional)
            constexpr double template_ratio = 0.10;

            // Total shared ratio
            const double shared_ratio = header_ratio + template_ratio;

            // Savings = shared_ratio * (N-1) / N
            // (parsing once instead of N times)
            double savings_ratio = shared_ratio * (n - 1.0) / n;

            // Reduce savings estimate for conflict risk
            savings_ratio *= (1.0 - group.conflict_risk_score * 0.5);

            return Duration(static_cast<Duration::rep>(
                static_cast<double>(total_ns) * savings_ratio
            ));
        }

        /// Overload using default config for sorting/comparison
        Duration estimate_unity_savings(const UnityGroup& group) {
            static const auto default_config = heuristics::HeuristicsConfig::defaults().unity_build;
            return estimate_unity_savings(group, default_config);
        }

        /**
         * Estimates peak memory usage for a unity group.
         *
         * Unity builds use more memory as all files are parsed together.
         * Based on measurements, peak memory is roughly:
         * max(individual) + 0.3 * sum(others)
         */
        std::size_t estimate_memory_usage(const UnityGroup& group) {
            if (group.files.empty()) {
                return 0;
            }

            std::size_t max_mem = 0;
            std::size_t total_mem = 0;

            for (const auto& file : group.files) {
                max_mem = std::max(max_mem, file.memory_estimate);
                total_mem += file.memory_estimate;
            }

            // Peak = largest + 30% of others (overlap from shared headers)
            return max_mem + static_cast<std::size_t>(
                0.3 * static_cast<double>(total_mem - max_mem));
        }

        /**
         * Creates unity groups from file metadata.
         */
        std::vector<UnityGroup> create_unity_groups(
            const std::vector<FileMetadata>& files,
            std::size_t max_files_per_group,
            Duration max_time_per_group,
            std::size_t max_memory_per_group,
            double max_conflict_risk
        ) {
            if (files.empty()) {
                return {};
            }

            std::unordered_map<std::string, std::vector<std::size_t>> dir_groups;
            for (std::size_t i = 0; i < files.size(); ++i) {
                const std::string dir = get_module_name(files[i].path);
                dir_groups[dir].push_back(i);
            }

            std::vector<UnityGroup> result;

            for (auto& [dir, indices] : dir_groups) {
                if (indices.size() < 2) {
                    continue;
                }

                std::vector<FileMetadata> dir_files;
                for (const std::size_t idx : indices) {
                    dir_files.push_back(files[idx]);
                }

                // Distance threshold 0.5 means at least 50% similarity required
                auto clusters = hierarchical_clustering(
                    dir_files,
                    0.5,  // distance threshold
                    max_files_per_group
                );

                for (const auto& cluster : clusters) {
                    UnityGroup group;
                    group.suggested_name = dir + "_unity_" + std::to_string(result.size());

                    // Compute common includes (intersection)
                    bool first = true;
                    for (const std::size_t idx : cluster) {
                        const auto& file = dir_files[idx];
                        group.files.push_back(file);
                        group.total_compile_time += file.compile_time;
                        group.total_memory_estimate += file.memory_estimate;

                        if (first) {
                            group.common_includes = file.includes;
                            first = false;
                        } else {
                            std::unordered_set<std::string> intersection;
                            for (const auto& inc : group.common_includes) {
                                if (file.includes.contains(inc)) {
                                    intersection.insert(inc);
                                }
                            }
                            group.common_includes = std::move(intersection);
                        }
                    }

                    if (group.total_compile_time > max_time_per_group) {
                        continue;  // Skip groups that are too expensive
                    }

                    if (const std::size_t peak_memory = estimate_memory_usage(group);
                        peak_memory > max_memory_per_group) {
                        continue;  // Skip groups that use too much memory
                    }

                    for (std::size_t i = 0; i < group.files.size(); ++i) {
                        for (std::size_t j = i + 1; j < group.files.size(); ++j) {
                            for (auto conflicts = detect_conflicts(group.files[i], group.files[j]); auto& conflict : conflicts) {
                                group.potential_conflicts.push_back(std::move(conflict));
                            }
                        }
                    }

                    for (const auto& file : group.files) {
                        if (file.included_sources.empty()) {
                            continue;
                        }
                        for (const auto& include : file.included_sources) {
                            SymbolConflict conflict;
                            conflict.symbol_name = include;
                            conflict.type = ConflictType::IncludedSource;
                            conflict.file_a = file.path;
                            conflict.file_b = file.path;
                            conflict.description = "Source file include '" + include +
                                "' inside " + file.path.filename().string() +
                                " - unity build likely causes multiple definitions";
                            group.potential_conflicts.push_back(std::move(conflict));
                        }
                    }

                    group.conflict_risk_score = calculate_conflict_risk(group.potential_conflicts);
                    group.total_includes = group.common_includes.size();

                    if (group.conflict_risk_score >= max_conflict_risk) {
                        continue;
                    }

                    result.push_back(std::move(group));
                }
            }

            std::ranges::sort(result,
                              [](const UnityGroup& a, const UnityGroup& b) {
                                  return estimate_unity_savings(a) > estimate_unity_savings(b);
                              });

            return result;
        }

        /**
         * Calculates priority based on group characteristics.
         */
        Priority calculate_priority(const UnityGroup& group) {
            const auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                group.total_compile_time).count();

            if (group.conflict_risk_score > 0.8) {
                return Priority::Low;  // High conflict risk = low priority
            }

            double score = static_cast<double>(group.files.size()) *
                           std::log(static_cast<double>(time_ms) + 1.0);

            // Reduce score for conflict risk
            score *= (1.0 - group.conflict_risk_score);

            if (score > 50.0 && group.files.size() >= 5) {
                return Priority::High;
            }
            if (score > 20.0 && group.files.size() >= 3) {
                return Priority::Medium;
            }
            return Priority::Low;
        }

        std::optional<Suggestion> build_unreal_module_unity_suggestion(const SuggestionContext& context) {
            if (!context.options.heuristics.unreal.emit_unity) {
                return std::nullopt;
            }

            const auto modules = collect_unreal_module_context(context);
            const auto targets = discover_unreal_target_rules(context.project_root);
            std::vector<UnrealModuleContext> candidates;
            candidates.reserve(modules.size());
            for (const auto& module : modules) {
                if (module.rules.build_cs_path.empty()) {
                    continue;
                }
                if (module.stats.source_files < context.options.heuristics.unreal.min_module_files_for_unity) {
                    continue;
                }
                if (!module.rules.use_unity.has_value() || module.rules.use_unity.value()) {
                    continue;
                }
                candidates.push_back(module);
            }

            if (candidates.empty()) {
                return std::nullopt;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("unreal-unity", candidates.front().rules.build_cs_path);
            suggestion.type = SuggestionType::UnityBuild;
            suggestion.priority = candidates.size() >= 2 ? Priority::High : Priority::Medium;
            suggestion.confidence = 0.78;
            suggestion.title = "Unreal Module Unity Build (UBT) Configuration (" + std::to_string(candidates.size()) + " modules)";

            std::ostringstream desc;
            desc << "Apply ModuleRules/TargetRules UnrealBuildTool (UBT) unity toggles for modules that explicitly disabled unity:\n";
            for (const auto& module : candidates) {
                const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    module.stats.total_compile_time
                ).count();
                desc << "  - " << module.rules.module_name
                     << " (" << make_repo_relative(module.rules.build_cs_path)
                     << ", " << module.stats.source_files << " source files"
                     << ", compile " << compile_ms << "ms)\n";
                desc << "    Set: bUseUnity = true;\n";
            }
            std::vector<UnrealTargetRules> target_overrides;
            for (const auto& target : targets) {
                if (target.use_unity.has_value() && !target.use_unity.value()) {
                    target_overrides.push_back(target);
                }
            }
            std::vector<UnrealTargetRules> adaptive_overrides;
            for (const auto& target : targets) {
                if (target.use_adaptive_unity.has_value() && !target.use_adaptive_unity.value()) {
                    adaptive_overrides.push_back(target);
                }
            }
            if (!target_overrides.empty()) {
                desc << "Also update Unreal target overrides that disable unity globally:\n";
                for (const auto& target : target_overrides) {
                    desc << "  - " << target.target_name << " ("
                         << make_repo_relative(target.target_cs_path)
                         << ")\n"
                         << "    Set: bUseUnity = true;\n";
                }
            }
            if (!adaptive_overrides.empty()) {
                desc << "Adaptive unity is disabled in these targets (consider enabling for incremental build throughput):\n";
                for (const auto& target : adaptive_overrides) {
                    desc << "  - " << target.target_name << " ("
                         << make_repo_relative(target.target_cs_path)
                         << ")\n"
                         << "    Set: bUseAdaptiveUnityBuild = true;\n";
                }
            }
            suggestion.description = desc.str();

            suggestion.rationale =
                "This changes only UBT settings in ModuleRules/TargetRules and avoids ad-hoc source amalgamation. "
                "That keeps rollout reversible and aligned with Unreal build ownership.";

            Duration total_compile_time = Duration::zero();
            std::size_t total_files = 0;
            for (const auto& module : candidates) {
                total_compile_time += module.stats.total_compile_time;
                total_files += module.stats.source_files;
            }
            suggestion.estimated_savings = total_compile_time / 8;
            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }
            suggestion.impact.total_files_affected = total_files;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            suggestion.target_file.path = candidates.front().rules.build_cs_path;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Enable bUseUnity in Unreal ModuleRules";
            if (candidates.front().rules.use_unity_line.has_value()) {
                suggestion.target_file.line_start = *candidates.front().rules.use_unity_line;
                suggestion.target_file.line_end = *candidates.front().rules.use_unity_line;
            }
            for (std::size_t i = 1; i < candidates.size(); ++i) {
                FileTarget secondary;
                secondary.path = candidates[i].rules.build_cs_path;
                secondary.action = FileAction::Modify;
                secondary.note = "Enable bUseUnity in Unreal ModuleRules";
                if (candidates[i].rules.use_unity_line.has_value()) {
                    secondary.line_start = *candidates[i].rules.use_unity_line;
                    secondary.line_end = *candidates[i].rules.use_unity_line;
                }
                suggestion.secondary_files.push_back(std::move(secondary));
            }
            for (const auto& target : target_overrides) {
                FileTarget secondary;
                secondary.path = target.target_cs_path;
                secondary.action = FileAction::Modify;
                secondary.note = "Enable bUseUnity in Unreal TargetRules";
                if (target.use_unity_line.has_value()) {
                    secondary.line_start = *target.use_unity_line;
                    secondary.line_end = *target.use_unity_line;
                }
                suggestion.secondary_files.push_back(std::move(secondary));
            }
            for (const auto& target : adaptive_overrides) {
                FileTarget secondary;
                secondary.path = target.target_cs_path;
                secondary.action = FileAction::Modify;
                secondary.note = "Enable bUseAdaptiveUnityBuild in Unreal TargetRules";
                if (target.use_adaptive_unity_line.has_value()) {
                    secondary.line_start = *target.use_adaptive_unity_line;
                    secondary.line_end = *target.use_adaptive_unity_line;
                }
                suggestion.secondary_files.push_back(std::move(secondary));
            }

            suggestion.implementation_steps = {
                "Set bUseUnity = true; in each listed <Module>.Build.cs file",
                "Set bUseUnity = true; in listed <Target>.Target.cs files that override unity",
                "Set bUseAdaptiveUnityBuild = true; in listed <Target>.Target.cs files where disabled",
                "Run UnrealBuildTool for impacted targets",
                "If any module has macro/ODR issues, keep unity disabled only for that module"
            };
            suggestion.caveats = {
                "Keep unity disabled for modules with known UHT-generated-code sensitivity",
                "Prefer module-level overrides instead of global unity toggles",
                "Adaptive unity's writable-file working-set heuristic is source-control sensitive and may behave differently on Git than Perforce"
            };
            suggestion.verification =
                "Build editor/game targets that consume these modules and compare clean build wall time.";

            const auto module_name_collisions = find_unreal_module_name_collisions(modules);
            const auto target_name_collisions = find_unreal_target_name_collisions(targets);
            const bool has_name_collisions =
                !module_name_collisions.empty() || !target_name_collisions.empty();

            if (!has_name_collisions) {
                for (const auto& module : candidates) {
                    if (auto edit = make_unreal_assignment_edit(
                        module.rules.build_cs_path,
                        "bUseUnity",
                        "true",
                        module.rules.use_unity_line
                    )) {
                        suggestion.edits.push_back(std::move(*edit));
                    }
                }

                for (const auto& target : target_overrides) {
                    if (auto edit = make_unreal_assignment_edit(
                        target.target_cs_path,
                        "bUseUnity",
                        "true",
                        target.use_unity_line
                    )) {
                        suggestion.edits.push_back(std::move(*edit));
                    }
                }

                for (const auto& target : adaptive_overrides) {
                    if (auto edit = make_unreal_assignment_edit(
                        target.target_cs_path,
                        "bUseAdaptiveUnityBuild",
                        "true",
                        target.use_adaptive_unity_line
                    )) {
                        suggestion.edits.push_back(std::move(*edit));
                    }
                }
            }

            if (has_name_collisions) {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
                suggestion.is_safe = false;
                suggestion.application_summary = "Manual review only";
                suggestion.application_guidance =
                    "Duplicate Unreal module/target rule names were detected. Resolve rule ownership ambiguity before enabling unity toggles automatically.";
                std::ostringstream reason;
                bool wrote_any = false;
                if (!module_name_collisions.empty()) {
                    const auto& first = module_name_collisions.front();
                    reason << "Ambiguous Unreal module rules for '" << first.name << "'";
                    wrote_any = true;
                }
                if (!target_name_collisions.empty()) {
                    const auto& first = target_name_collisions.front();
                    if (wrote_any) {
                        reason << "; ";
                    }
                    reason << "ambiguous Unreal target rules for '" << first.name << "'";
                }
                suggestion.auto_apply_blocked_reason = reason.str();
            } else if (suggestion.edits.size() == candidates.size() + target_overrides.size() + adaptive_overrides.size()) {
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
                suggestion.is_safe = true;
                suggestion.application_summary = "Auto-apply via direct text edits";
                suggestion.application_guidance =
                    "BHA can set module/target unity toggles directly, including adaptive-unity target settings. Rebuild affected Unreal targets to validate behavior.";
            } else {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
                suggestion.is_safe = false;
                suggestion.application_summary = "Manual review only";
                suggestion.application_guidance =
                    "Automatic edit placement failed for one or more ModuleRules/TargetRules files. Apply listed Unity settings manually.";
                suggestion.auto_apply_blocked_reason =
                    "At least one ModuleRules or TargetRules constructor block could not be located for safe edit insertion.";
            }

            return suggestion;
        }

    }  // namespace

    Result<SuggestionResult, Error> UnityBuildSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        if (is_unreal_mode_active(context)) {
            if (auto unreal_unity = build_unreal_module_unity_suggestion(context)) {
                result.suggestions.push_back(std::move(*unreal_unity));
                result.items_analyzed = 1;
            }
            auto end_time = std::chrono::steady_clock::now();
            result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            return Result<SuggestionResult, Error>::success(std::move(result));
        }

        const auto& files = context.analysis.files;
        const auto& deps = context.analysis.dependencies;
        const auto& symbols = context.analysis.symbols;
        const auto& unity_config = context.options.heuristics.unity_build;

        auto metadata = build_file_metadata(files, deps, symbols);
        if (!context.project_root.empty()) {
            const fs::path root = context.project_root.is_relative()
                ? fs::absolute(context.project_root)
                : context.project_root;
            metadata.erase(
                std::remove_if(metadata.begin(), metadata.end(),
                    [&](const FileMetadata& meta) {
                        const fs::path resolved = resolve_source_path(meta.path);
                        return !path_utils::is_under(resolved, root);
                    }),
                metadata.end()
            );
        }

        const std::size_t max_files = unity_config.files_per_unit;
        const Duration max_time = std::chrono::seconds(30);
        const std::size_t max_memory = 4ULL * 1024 * 1024 * 1024;
        auto groups = create_unity_groups(metadata, max_files, max_time, max_memory, unity_config.max_conflict_risk);

        const std::size_t analyzed = files.size();
        std::size_t skipped = 0;
        std::size_t group_counter = 0;
        std::unordered_set<std::string> seen_group_fingerprints;
        std::unordered_map<std::string, bool> global_unity_cache;

        for (const auto& group : groups) {
            if (context.is_cancelled()) {
                break;
            }
            const std::size_t min_group_size = std::max<std::size_t>(2, unity_config.min_files_threshold);
            if (group.files.size() < min_group_size) {
                ++skipped;
                continue;
            }

            if (group.conflict_risk_score > unity_config.max_conflict_risk) {
                ++skipped;
                continue;
            }

            if (group.total_compile_time < unity_config.min_group_total_time) {
                ++skipped;
                continue;
            }

            std::vector<std::string> fingerprint_parts;
            fingerprint_parts.reserve(group.files.size());
            for (const auto& file : group.files) {
                fingerprint_parts.push_back(normalized_key(resolve_source_path(file.path)));
            }
            std::ranges::sort(fingerprint_parts);
            std::ostringstream fingerprint_stream;
            for (const auto& value : fingerprint_parts) {
                fingerprint_stream << value << '\n';
            }
            if (!seen_group_fingerprints.insert(fingerprint_stream.str()).second) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("unity", group_counter++, group.suggested_name);
            suggestion.type = SuggestionType::UnityBuild;
            suggestion.priority = calculate_priority(group);
            suggestion.confidence = 0.85 - group.conflict_risk_score * 0.5;

            std::ostringstream title;
            title << "Create unity build group: " << group.suggested_name;
            suggestion.title = title.str();

            auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                group.total_compile_time).count();
            auto memory_mb = estimate_memory_usage(group) / (1024 * 1024);

            std::ostringstream desc;
            desc << "Group " << group.files.size() << " source files into a unity build.\n"
                 << "• Combined compile time: " << time_ms << "ms\n"
                 << "• Shared includes: " << group.total_includes << "\n"
                 << "• Estimated peak memory: " << memory_mb << "MB\n";

            if (!group.potential_conflicts.empty()) {
                desc << "• WARNING: " << group.potential_conflicts.size()
                     << " potential symbol conflicts detected";
            }

            suggestion.description = desc.str();

            std::ostringstream rationale;
            rationale << "Unity builds combine multiple source files into a single "
                      << "translation unit, reducing overall compile time by:\n\n"
                      << "1. **Parsing shared headers once** instead of per-file "
                      << "(typically 40-60% of compile time)\n"
                      << "2. **Sharing template instantiations** across files\n"
                      << "3. **Reducing linker workload** (fewer object files)\n"
                      << "4. **Improving cache utilization** during compilation\n\n"
                      << "This group shares " << group.total_includes
                      << " headers, making it a good candidate.\n\n"
                      << "**Research basis**: Based on techniques from Chromium's "
                      << "jumbo builds and Unreal Engine 4's unity builds.";

            suggestion.rationale = rationale.str();

            suggestion.estimated_savings = estimate_unity_savings(group, unity_config);

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            for (const auto& file : group.files) {
                FileTarget target;
                target.path = resolve_source_path(file.path);
                target.action = FileAction::Modify;
                target.note = "Include in unity build";
                suggestion.secondary_files.push_back(target);
            }

            std::ostringstream cmake_example;
            cmake_example << "if(TARGET <target>)\n"
                          << "  set_property(TARGET <target> PROPERTY UNITY_BUILD ON)\n"
                          << "  set_property(TARGET <target> PROPERTY UNITY_BUILD_BATCH_SIZE "
                          << group.files.size() << ")\n"
                          << "  set_property(TARGET <target> PROPERTY UNITY_BUILD_UNIQUE_ID BHA_UNITY_BUILD_FILE_ID)\n"
                          << "endif()";

            suggestion.before_code.file = "CMakeLists.txt";
            suggestion.before_code.code = cmake_example.str();

            suggestion.implementation_steps = {
                "1. Review potential conflicts listed in the suggestion",
                "2. Enable UNITY_BUILD on the owning target (not globally)",
                "3. Set UNITY_BUILD_BATCH_SIZE for that target",
                "4. Set UNITY_BUILD_UNIQUE_ID to avoid anonymous namespace collisions",
                "5. Exclude problematic files via SKIP_UNITY_BUILD_INCLUSION when needed",
                "6. Build and verify no compilation errors",
                "7. Run tests to ensure no behavioral changes",
                "8. Measure build time improvement"
            };

            suggestion.impact.total_files_affected = group.files.size();
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;

            std::vector<std::string> caveats = {
                "Static/anonymous namespace symbols may conflict",
                "Incremental builds slower (entire unity file rebuilds)",
                "Debug symbols harder to navigate",
                "Peak memory usage increases (~" + std::to_string(memory_mb) + "MB)",
                "Include order dependencies may cause issues"
            };

            if (!group.potential_conflicts.empty()) {
                caveats.insert(caveats.begin(),
                    "WARNING: " + std::to_string(group.potential_conflicts.size()) +
                    " potential conflicts must be resolved first");
            }

            suggestion.caveats = caveats;

            suggestion.verification =
                "1. Build with unity configuration and verify no errors\n"
                "2. Check for ODR violations with -fsanitize=undefined\n"
                "3. Run full test suite\n"
                "4. Measure full build time improvement\n"
                "5. Measure incremental build time impact\n"
                "6. Monitor peak memory usage during build";

            suggestion.is_safe = group.potential_conflicts.empty();

            fs::path project_root = context.project_root;
            if (project_root.empty() && !group.files.empty()) {
                const fs::path source_path = resolve_source_path(group.files[0].path);
                project_root = find_repository_root(source_path);
                if (project_root.empty()) {
                    project_root = source_path.parent_path();
                }
            }

            std::vector<fs::path> group_source_paths;
            group_source_paths.reserve(group.files.size());
            for (const auto& file : group.files) {
                group_source_paths.push_back(resolve_source_path(file.path));
            }

            bool edits_added = false;
            bool used_cmake = false;
            if (!project_root.empty() && fs::exists(project_root / "CMakeLists.txt")) {
                used_cmake = true;
                const std::string root_key = project_root.generic_string();
                bool has_global_unity = false;
                if (auto it = global_unity_cache.find(root_key); it != global_unity_cache.end()) {
                    has_global_unity = it->second;
                } else {
                    has_global_unity = cmake_tree_has_global_unity_enabled(
                        project_root,
                        [&]() { return context.is_cancelled(); }
                    );
                    global_unity_cache.emplace(root_key, has_global_unity);
                }
                if (has_global_unity) {
                    ++skipped;
                    continue;
                }

                auto selected_target = find_best_cmake_target_for_group(
                    project_root,
                    group_source_paths,
                    [&]() { return context.is_cancelled(); }
                );
                if (!selected_target) {
                    ++skipped;
                    continue;
                }

                std::ifstream cmake_in(selected_target->cmake_path);
                const std::string cmake_content((std::istreambuf_iterator<char>(cmake_in)),
                                                std::istreambuf_iterator<char>());
                if (cmake_target_has_unity_enabled(cmake_content, selected_target->target.name)) {
                    ++skipped;
                    continue;
                }

                const std::size_t insert_line = selected_target->target.end_line + 1;
                TextEdit cmake_edit;
                cmake_edit.file = selected_target->cmake_path;
                cmake_edit.start_line = insert_line;
                cmake_edit.start_col = 0;
                cmake_edit.end_line = insert_line;
                cmake_edit.end_col = 0;
                cmake_edit.new_text =
                    "\nif(TARGET " + selected_target->target.name + ")\n"
                    "  set_property(TARGET " + selected_target->target.name + " PROPERTY UNITY_BUILD ON)\n"
                    "  set_property(TARGET " + selected_target->target.name + " PROPERTY UNITY_BUILD_BATCH_SIZE " + std::to_string(group.files.size()) + ")\n"
                    "  set_property(TARGET " + selected_target->target.name + " PROPERTY UNITY_BUILD_UNIQUE_ID BHA_UNITY_BUILD_FILE_ID)\n"
                    "endif()\n";
                suggestion.edits.push_back(cmake_edit);

                suggestion.target_file.path = selected_target->cmake_path;
                suggestion.target_file.action = FileAction::Modify;
                suggestion.target_file.note = "Enable UNITY_BUILD on target " + selected_target->target.name;

                FileTarget cmake_target;
                cmake_target.path = selected_target->cmake_path;
                cmake_target.action = FileAction::Modify;
                cmake_target.line_start = insert_line + 1;
                cmake_target.line_end = insert_line + 4;
                cmake_target.note = "Enable UNITY_BUILD for target " + selected_target->target.name;
                suggestion.secondary_files.push_back(cmake_target);

                suggestion.after_code.file = selected_target->cmake_path.filename().string();
                suggestion.after_code.code = cmake_edit.new_text;
                edits_added = true;
            }

            if (!used_cmake) {
                fs::path unity_file_path = project_root / "src" / (group.suggested_name + ".cpp");
                if (!fs::exists(unity_file_path.parent_path())) {
                    unity_file_path = project_root / (group.suggested_name + ".cpp");
                }

                std::ostringstream unity_content;
                unity_content << "// " << group.suggested_name << ".cpp\n"
                              << "// Unity build file - auto-generated by BHA\n"
                              << "// Combines " << group.files.size() << " source files\n"
                              << "// Estimated savings: "
                              << std::chrono::duration_cast<std::chrono::milliseconds>(
                                     suggestion.estimated_savings).count()
                              << "ms\n\n";

                if (!group.potential_conflicts.empty()) {
                    unity_content << "// WARNING: Potential conflicts detected:\n";
                    for (const auto& conflict : group.potential_conflicts) {
                        unity_content << "//   - " << conflict.description << "\n";
                    }
                    unity_content << "\n";
                }

                const fs::path unity_dir = unity_file_path.parent_path();
                for (const auto& source_path : group_source_paths) {
                    std::error_code ec;
                    fs::path rel = fs::relative(source_path, unity_dir, ec);
                    if (ec || rel.empty()) {
                        rel = make_repo_relative(source_path);
                    }
                    unity_content << "#include \"" << rel.generic_string() << "\"\n";
                }

                TextEdit create_unity;
                create_unity.file = unity_file_path;
                create_unity.start_line = 0;
                create_unity.start_col = 0;
                create_unity.end_line = 0;
                create_unity.end_col = 0;
                create_unity.new_text = unity_content.str();
                suggestion.edits.push_back(create_unity);

                suggestion.target_file.path = unity_file_path;
                suggestion.target_file.action = FileAction::Create;
                suggestion.target_file.note = "Create unity build file";
                suggestion.after_code.file = group.suggested_name + ".cpp";
                suggestion.after_code.code = unity_content.str();
                edits_added = true;
            }

            if (!edits_added || suggestion.edits.empty()) {
                ++skipped;
                continue;
            }
            result.suggestions.push_back(std::move(suggestion));
        }

        result.items_analyzed = analyzed;
        result.items_skipped = skipped;

        auto end_time = std::chrono::steady_clock::now();
        result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    void register_unity_build_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<UnityBuildSuggester>()
        );
    }
}  // namespace bha::suggestions
