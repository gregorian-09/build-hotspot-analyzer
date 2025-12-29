//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/unity_build_suggester.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
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
            GlobalVariable       // global variable with same name
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
            std::size_t line_count = 0;
            std::size_t preprocessed_size = 0;

            // Symbol information for conflict detection
            std::unordered_set<std::string> static_symbols;
            std::unordered_set<std::string> anon_namespace_symbols;
            std::unordered_set<std::string> defined_macros;

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

            double get(const std::size_t i, const std::size_t j) const {
                return distances_[i * n_ + j];
            }

            std::size_t size() const { return n_; }

        private:
            std::size_t n_;
            std::vector<double> distances_;
        };

        /**
         * Checks if a file is a C++ source file (not a header).
         */
        bool is_source_file(const fs::path& path) {
            const std::string ext = path.extension().string();
            return ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
                   ext == ".c" || ext == ".C" || ext == ".c++";
        }

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
         * - Global variable conflicts
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
            for (const auto& macro : file_a.defined_macros) {
                if (file_b.defined_macros.contains(macro)) {
                    SymbolConflict conflict;
                    conflict.symbol_name = macro;
                    conflict.type = ConflictType::MacroRedefinition;
                    conflict.file_a = file_a.path;
                    conflict.file_b = file_b.path;
                    conflict.description = "Macro '" + macro +
                        "' defined in both files - may cause unexpected behavior";
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
                case ConflictType::GlobalVariable:
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
            std::size_t max_cluster_size
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
                    if (!active[i]) continue;

                    for (std::size_t j = i + 1; j < clusters.size(); ++j) {
                        if (!active[j]) continue;

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
                for (std::size_t idx : clusters[best_j]) {
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

            std::unordered_map<std::string, std::unordered_set<std::string>> file_includes;
            for (const auto& header : deps.headers) {
                for (const auto& includer : header.included_by) {
                    file_includes[includer.string()].insert(header.path.string());
                }
            }

            // Build symbol map per file
            // Note: Using heuristics since SymbolInfo doesn't have linkage info
            std::unordered_map<std::string, std::unordered_set<std::string>> file_static_symbols;
            std::unordered_map<std::string, std::unordered_set<std::string>> file_anon_symbols;

            for (const auto& sym : symbols.symbols) {
                std::string file_key = sym.defined_in.string();

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
                if (!is_source_file(file.file)) {
                    continue;
                }

                FileMetadata meta;
                meta.path = file.file;
                meta.compile_time = file.compile_time;
                meta.line_count = file.lines_of_code;
                meta.preprocessed_size = file.lines_of_code;  // Approximation

                std::string file_key = file.file.string();

                if (file_includes.contains(file_key)) {
                    meta.includes = file_includes[file_key];
                }

                if (file_static_symbols.contains(file_key)) {
                    meta.static_symbols = file_static_symbols[file_key];
                }

                if (file_anon_symbols.contains(file_key)) {
                    meta.anon_namespace_symbols = file_anon_symbols[file_key];
                }

                // Memory estimate: use lines of code as proxy
                // Research shows ~10x expansion from source to memory
                meta.memory_estimate = meta.line_count * 10;

                meta.include_depth = file.include_count;  // Use include_count as proxy

                metadata.push_back(meta);
            }

            return metadata;
        }

        /**
         * Estimates savings from unity building based on research.
         *
         * Model based on measurements from Chromium and UE4:
         * - Header parsing: 40-60% of compile time
         * - Template instantiation: 10-20% of compile time
         * - Shared savings: (1 - 1/N) * shared_ratio
         *
         * Additional factors:
         * - Common include count (more = higher savings)
         * - Preprocessed size ratio (larger = higher savings)
         */
        Duration estimate_unity_savings(const UnityGroup& group) {
            if (group.files.size() < 2) {
                return Duration::zero();
            }

            const auto total_ns = group.total_compile_time.count();
            const double n = static_cast<double>(group.files.size());

            // Base shared ratio from header parsing
            // Research: headers are 40-60% of compile time
            double header_ratio = 0.50;

            // Adjust based on common include count
            // More shared includes = higher savings
            if (group.total_includes > 30) {
                header_ratio = 0.60;
            } else if (group.total_includes > 15) {
                header_ratio = 0.55;
            } else if (group.total_includes < 5) {
                header_ratio = 0.40;
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
            std::size_t max_memory_per_group
        ) {
            if (files.empty()) {
                return {};
            }

            std::unordered_map<std::string, std::vector<std::size_t>> dir_groups;
            for (std::size_t i = 0; i < files.size(); ++i) {
                std::string dir = get_module_name(files[i].path);
                dir_groups[dir].push_back(i);
            }

            std::vector<UnityGroup> result;

            for (auto& [dir, indices] : dir_groups) {
                if (indices.size() < 2) {
                    continue;
                }

                std::vector<FileMetadata> dir_files;
                for (std::size_t idx : indices) {
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
                    for (std::size_t idx : cluster) {
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

                    if (std::size_t peak_memory = estimate_memory_usage(group); peak_memory > max_memory_per_group) {
                        continue;  // Skip groups that use too much memory
                    }

                    for (std::size_t i = 0; i < group.files.size(); ++i) {
                        for (std::size_t j = i + 1; j < group.files.size(); ++j) {
                            for (auto conflicts = detect_conflicts(group.files[i], group.files[j]); auto& conflict : conflicts) {
                                group.potential_conflicts.push_back(std::move(conflict));
                            }
                        }
                    }

                    group.conflict_risk_score = calculate_conflict_risk(group.potential_conflicts);
                    group.total_includes = group.common_includes.size();

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

    }  // namespace

    Result<SuggestionResult, Error> UnityBuildSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& files = context.analysis.files;
        const auto& deps = context.analysis.dependencies;
        const auto& symbols = context.analysis.symbols;

        auto metadata = build_file_metadata(files, deps, symbols);

        std::size_t max_files = 10;
        Duration max_time = std::chrono::seconds(30);
        std::size_t max_memory = 4ULL * 1024 * 1024 * 1024;  // 4GB
        auto groups = create_unity_groups(metadata, max_files, max_time, max_memory);

        std::size_t analyzed = files.size();
        std::size_t skipped = 0;

        for (const auto& group : groups) {
            if (group.files.size() < 2) {
                ++skipped;
                continue;
            }

            if (group.conflict_risk_score > 0.9) {
                ++skipped;
                continue;
            }

            Suggestion suggestion;
            suggestion.id = "unity-" + group.suggested_name;
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

            suggestion.estimated_savings = estimate_unity_savings(group);

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            for (const auto& file : group.files) {
                FileTarget target;
                target.path = file.path;
                target.action = FileAction::Modify;
                target.note = "Include in unity build";
                suggestion.secondary_files.push_back(target);
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

            for (const auto& file : group.files) {
                unity_content << "#include \"" << file.path.string() << "\"\n";
            }

            suggestion.after_code.file = group.suggested_name + ".cpp";
            suggestion.after_code.code = unity_content.str();

            // CMake example with UNITY_BUILD_UNIQUE_ID for conflict resolution
            std::ostringstream cmake_example;
            cmake_example << "# CMakeLists.txt - Unity build configuration\n"
                          << "set(CMAKE_UNITY_BUILD ON)\n"
                          << "set(CMAKE_UNITY_BUILD_BATCH_SIZE "
                          << group.files.size() << ")\n\n"
                          << "# For conflict resolution, use unique IDs:\n"
                          << "set_source_files_properties(\n";
            for (const auto& file : group.files) {
                cmake_example << "    " << file.path.filename().string() << "\n";
            }
            cmake_example << "    PROPERTIES UNITY_GROUP \"" << group.suggested_name << "\"\n"
                          << ")\n\n"
                          << "# Enable UNITY_BUILD_UNIQUE_ID for static symbol conflicts:\n"
                          << "set(CMAKE_UNITY_BUILD_UNIQUE_ID ON)";

            suggestion.before_code.file = "CMakeLists.txt";
            suggestion.before_code.code = cmake_example.str();

            suggestion.implementation_steps = {
                "1. Review potential conflicts listed in the suggestion",
                "2. Resolve conflicts by:",
                "   - Renaming static/anonymous namespace symbols",
                "   - Using CMAKE_UNITY_BUILD_UNIQUE_ID",
                "   - Wrapping conflicting code in named namespaces",
                "3. Enable unity build in CMake:",
                "   set(CMAKE_UNITY_BUILD ON)",
                "4. Or create manual unity file with #includes",
                "5. Build and verify no compilation errors",
                "6. Run tests to ensure no behavioral changes",
                "7. Measure build time improvement"
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