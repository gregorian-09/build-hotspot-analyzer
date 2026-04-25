//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/suggester.hpp"
#include "bha/suggestions/consolidator.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <queue>
#include <ranges>
#include <regex>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bha::suggestions
{
    namespace fs = std::filesystem;
    namespace {

        constexpr std::array<std::string_view, 9> kHeaderExts = {
            ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".ipp", ".tpp", ".pch"
        };

        constexpr std::array<std::string_view, 7> kSourceExts = {
            ".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".ixx"
        };

        std::string to_lower(std::string text) {
            std::ranges::transform(text, text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool has_extension(const fs::path& path, const std::span<const std::string_view> exts) {
            const std::string ext = to_lower(path.extension().string());
            return std::ranges::any_of(exts, [&](const std::string_view candidate) {
                return ext == candidate;
            });
        }

        bool is_header_file(const fs::path& path) {
            return has_extension(path, kHeaderExts);
        }

        bool is_source_file(const fs::path& path) {
            return has_extension(path, kSourceExts);
        }

        fs::path normalize_path(const fs::path& input, const fs::path& project_root) {
            fs::path path = input;
            if (path.empty()) {
                return path;
            }
            if (path.is_relative() && !project_root.empty()) {
                path = project_root / path;
            }
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(path, ec);
            if (!ec) {
                return canonical.lexically_normal();
            }
            return path.lexically_normal();
        }

        std::string normalize_key(const fs::path& input, const fs::path& project_root) {
            return normalize_path(input, project_root).generic_string();
        }

        std::vector<fs::path> parse_include_dirs(const CompilationUnit& unit, const fs::path& project_root) {
            std::vector<fs::path> dirs;
            if (!unit.source_file.empty()) {
                dirs.push_back(normalize_path(unit.source_file.parent_path(), project_root));
            }

            auto push_dir = [&](const std::string& value) {
                if (value.empty()) {
                    return;
                }
                fs::path dir = value;
                if (dir.is_relative()) {
                    if (!project_root.empty()) {
                        dir = project_root / dir;
                    } else if (!unit.source_file.empty()) {
                        dir = unit.source_file.parent_path() / dir;
                    }
                }
                dirs.push_back(dir.lexically_normal());
            };

            for (std::size_t i = 0; i < unit.command_line.size(); ++i) {
                const std::string& arg = unit.command_line[i];
                if (arg == "-I" || arg == "-isystem" || arg == "-iquote" || arg == "/I") {
                    if (i + 1 < unit.command_line.size()) {
                        push_dir(unit.command_line[i + 1]);
                        ++i;
                    }
                    continue;
                }
                if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
                    push_dir(arg.substr(2));
                    continue;
                }
                if (arg.rfind("/I", 0) == 0 && arg.size() > 2) {
                    push_dir(arg.substr(2));
                    continue;
                }
            }

            if (!project_root.empty()) {
                dirs.push_back(project_root);
            }

            std::unordered_set<std::string> seen;
            std::vector<fs::path> deduped;
            deduped.reserve(dirs.size());
            for (const auto& dir : dirs) {
                const std::string key = dir.generic_string();
                if (!key.empty() && seen.insert(key).second) {
                    deduped.push_back(dir);
                }
            }
            return deduped;
        }

        struct IncludeDirective {
            std::string name;
            bool is_system = false;
        };

        struct ExplainabilityCache {
            std::unordered_map<std::string, std::vector<IncludeDirective>> directives_by_file;
            std::unordered_map<std::string, std::vector<fs::path>> include_chain_cache;
        };

        bool deadline_reached(const std::optional<std::chrono::steady_clock::time_point>& deadline) {
            return deadline.has_value() && std::chrono::steady_clock::now() >= *deadline;
        }

        std::vector<IncludeDirective> parse_include_directives(const fs::path& file_path) {
            std::ifstream in(file_path);
            if (!in.is_open()) {
                return {};
            }

            static const std::regex include_regex(R"(^\s*#\s*include\s*([<"])([^">]+)[">])");
            std::vector<IncludeDirective> directives;
            std::string line;
            while (std::getline(in, line)) {
                std::smatch match;
                if (!std::regex_search(line, match, include_regex)) {
                    continue;
                }
                IncludeDirective directive;
                directive.is_system = match[1].str() == "<";
                directive.name = fs::path(match[2].str()).lexically_normal().generic_string();
                directives.push_back(std::move(directive));
            }
            return directives;
        }

        const std::vector<IncludeDirective>& get_include_directives_cached(
            const fs::path& file_path,
            ExplainabilityCache& cache
        ) {
            const std::string key = file_path.generic_string();
            auto it = cache.directives_by_file.find(key);
            if (it != cache.directives_by_file.end()) {
                return it->second;
            }
            return cache.directives_by_file.emplace(key, parse_include_directives(file_path)).first->second;
        }

        std::optional<fs::path> resolve_include(
            const IncludeDirective& include,
            const fs::path& including_file,
            const std::vector<fs::path>& include_dirs,
            const fs::path& project_root
        ) {
            if (include.name.empty()) {
                return std::nullopt;
            }

            const fs::path include_path = include.name;
            std::error_code ec;
            if (include_path.is_absolute()) {
                if (fs::exists(include_path, ec) && fs::is_regular_file(include_path, ec)) {
                    return include_path.lexically_normal();
                }
                return std::nullopt;
            }

            if (!include.is_system) {
                const fs::path local = including_file.parent_path() / include_path;
                if (fs::exists(local, ec) && fs::is_regular_file(local, ec)) {
                    return local.lexically_normal();
                }
            }

            for (const auto& dir : include_dirs) {
                const fs::path candidate = dir / include_path;
                if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
                    return candidate.lexically_normal();
                }
            }

            if (!project_root.empty()) {
                const fs::path repo_candidate = project_root / include_path;
                if (fs::exists(repo_candidate, ec) && fs::is_regular_file(repo_candidate, ec)) {
                    return repo_candidate.lexically_normal();
                }
            }

            return std::nullopt;
        }

        Duration estimate_include_cost(
            const CompilationUnit* unit,
            const fs::path& header,
            const analyzers::AnalysisResult& analysis,
            const fs::path& project_root
        ) {
            const std::string header_key = normalize_key(header, project_root);
            if (unit != nullptr) {
                for (const auto& include : unit->includes) {
                    if (normalize_key(include.header, project_root) == header_key) {
                        return include.parse_time;
                    }
                }
            }

            for (const auto& info : analysis.dependencies.headers) {
                if (normalize_key(info.path, project_root) == header_key) {
                    if (info.inclusion_count == 0) {
                        return info.total_parse_time;
                    }
                    return info.total_parse_time / info.inclusion_count;
                }
            }

            return Duration::zero();
        }

        std::vector<fs::path> find_include_chain(
            const fs::path& source_file,
            const fs::path& target_header,
            const std::vector<fs::path>& include_dirs,
            const fs::path& project_root,
            ExplainabilityCache& cache,
            const std::optional<std::chrono::steady_clock::time_point>& deadline
        ) {
            const fs::path source = normalize_path(source_file, project_root);
            const fs::path target = normalize_path(target_header, project_root);
            if (source.empty() || target.empty()) {
                return {};
            }

            const std::string source_key = source.generic_string();
            const std::string target_key = target.generic_string();
            if (source_key.empty() || target_key.empty()) {
                return {};
            }

            const std::string chain_key = source_key + "=>" + target_key;
            if (const auto chain_it = cache.include_chain_cache.find(chain_key); chain_it != cache.include_chain_cache.end()) {
                return chain_it->second;
            }

            std::unordered_set<std::string> visited;
            std::unordered_map<std::string, std::string> parent;
            std::unordered_map<std::string, fs::path> nodes;
            std::unordered_map<std::string, std::vector<fs::path>> include_cache;

            std::queue<fs::path> queue;
            queue.push(source);
            visited.insert(source_key);
            nodes[source_key] = source;

            std::size_t explored_nodes = 0;
            constexpr std::size_t kMaxNodes = 1000;

            while (!queue.empty() && explored_nodes < kMaxNodes) {
                if (deadline_reached(deadline)) {
                    cache.include_chain_cache.emplace(chain_key, std::vector<fs::path>{});
                    return {};
                }

                const fs::path current = queue.front();
                queue.pop();
                ++explored_nodes;

                const std::string current_key = current.generic_string();
                if (current_key == target_key) {
                    break;
                }

                auto cache_it = include_cache.find(current_key);
                if (cache_it == include_cache.end()) {
                    std::vector<fs::path> resolved;
                    for (const auto& include : get_include_directives_cached(current, cache)) {
                        auto resolved_include = resolve_include(include, current, include_dirs, project_root);
                        if (resolved_include.has_value()) {
                            resolved.push_back(normalize_path(*resolved_include, project_root));
                        }
                    }
                    cache_it = include_cache.emplace(current_key, std::move(resolved)).first;
                }

                for (const auto& child : cache_it->second) {
                    const std::string child_key = child.generic_string();
                    if (child_key.empty()) {
                        continue;
                    }
                    if (!visited.insert(child_key).second) {
                        continue;
                    }
                    parent[child_key] = current_key;
                    nodes[child_key] = child;
                    if (child_key == target_key) {
                        queue = {};
                        break;
                    }
                    queue.push(child);
                }
            }

            if (!visited.contains(target_key)) {
                cache.include_chain_cache.emplace(chain_key, std::vector<fs::path>{});
                return {};
            }

            std::vector<fs::path> chain;
            std::string current = target_key;
            while (!current.empty()) {
                auto node_it = nodes.find(current);
                if (node_it == nodes.end()) {
                    break;
                }
                chain.push_back(node_it->second);
                auto parent_it = parent.find(current);
                if (parent_it == parent.end()) {
                    break;
                }
                current = parent_it->second;
            }
            std::ranges::reverse(chain);
            cache.include_chain_cache.emplace(chain_key, chain);
            return chain;
        }

        std::vector<fs::path> gather_source_candidates(
            const Suggestion& suggestion,
            const analyzers::AnalysisResult& analysis,
            const fs::path& project_root
        ) {
            std::vector<fs::path> candidates;
            auto add_candidate = [&](const fs::path& path) {
                if (path.empty()) {
                    return;
                }
                const fs::path normalized = normalize_path(path, project_root);
                if (normalized.empty()) {
                    return;
                }
                if (!is_source_file(normalized)) {
                    return;
                }
                candidates.push_back(std::move(normalized));
            };

            for (const auto& file : suggestion.impact.files_benefiting) {
                add_candidate(file);
            }
            add_candidate(suggestion.target_file.path);
            add_candidate(suggestion.before_code.file);
            add_candidate(suggestion.after_code.file);
            for (const auto& secondary : suggestion.secondary_files) {
                add_candidate(secondary.path);
            }

            if (candidates.empty()) {
                for (std::size_t i = 0; i < std::min<std::size_t>(3, analysis.files.size()); ++i) {
                    add_candidate(analysis.files[i].file);
                }
            }

            std::unordered_set<std::string> seen;
            std::vector<fs::path> deduped;
            deduped.reserve(candidates.size());
            for (const auto& candidate : candidates) {
                const std::string key = candidate.generic_string();
                if (seen.insert(key).second) {
                    deduped.push_back(candidate);
                }
            }
            return deduped;
        }

        std::vector<fs::path> extract_candidate_headers(
            const Suggestion& suggestion,
            const CompilationUnit* unit,
            const fs::path& project_root
        ) {
            std::vector<fs::path> headers;

            auto add_header = [&](const fs::path& path) {
                if (path.empty()) {
                    return;
                }
                const fs::path normalized = normalize_path(path, project_root);
                if (!normalized.empty() && is_header_file(normalized)) {
                    headers.push_back(normalized);
                }
            };

            add_header(suggestion.target_file.path);

            const auto parse_headers_from_text = [&](const std::string& text) {
                if (text.empty()) {
                    return;
                }
                static const std::regex include_regex(R"(#\s*include\s*[<"]([^">]+)[">])");
                for (std::sregex_iterator it(text.begin(), text.end(), include_regex), end; it != end; ++it) {
                    add_header(fs::path((*it)[1].str()));
                }
            };
            parse_headers_from_text(suggestion.before_code.code);
            parse_headers_from_text(suggestion.after_code.code);

            if (unit != nullptr) {
                std::vector<const IncludeInfo*> ordered;
                ordered.reserve(unit->includes.size());
                for (const auto& include : unit->includes) {
                    ordered.push_back(&include);
                }
                std::ranges::sort(ordered, [](const IncludeInfo* lhs, const IncludeInfo* rhs) {
                    return lhs->parse_time > rhs->parse_time;
                });
                for (std::size_t i = 0; i < std::min<std::size_t>(3, ordered.size()); ++i) {
                    add_header(ordered[i]->header);
                }
            }

            std::unordered_set<std::string> seen;
            std::vector<fs::path> deduped;
            deduped.reserve(headers.size());
            for (const auto& header : headers) {
                const std::string key = header.generic_string();
                if (seen.insert(key).second) {
                    deduped.push_back(header);
                }
            }
            return deduped;
        }

        std::vector<HotspotOrigin> derive_template_origins(
            const Suggestion& suggestion,
            const analyzers::AnalysisResult& analysis
        ) {
            if (suggestion.type != SuggestionType::ExplicitTemplate) {
                return {};
            }

            const std::string searchable = suggestion.title + "\n" + suggestion.description + "\n" +
                suggestion.before_code.code + "\n" + suggestion.after_code.code;
            const std::string searchable_lower = to_lower(searchable);

            struct Candidate {
                const analyzers::TemplateAnalysisResult::TemplateInfo* info;
                int score;
            };
            std::vector<Candidate> candidates;
            for (const auto& tmpl : analysis.templates.templates) {
                int score = 0;
                const std::string signature_lower = to_lower(tmpl.full_signature);
                const std::string name_lower = to_lower(tmpl.name);
                if (!signature_lower.empty() && searchable_lower.find(signature_lower) != std::string::npos) {
                    score += 5;
                }
                if (!name_lower.empty() && searchable_lower.find(name_lower) != std::string::npos) {
                    score += 3;
                }
                if (score > 0) {
                    candidates.push_back({&tmpl, score});
                }
            }

            if (candidates.empty()) {
                for (std::size_t i = 0; i < std::min<std::size_t>(3, analysis.templates.templates.size()); ++i) {
                    candidates.push_back({&analysis.templates.templates[i], 1});
                }
            } else {
                std::ranges::sort(candidates, [](const Candidate& lhs, const Candidate& rhs) {
                    if (lhs.score != rhs.score) {
                        return lhs.score > rhs.score;
                    }
                    return lhs.info->total_time > rhs.info->total_time;
                });
                if (candidates.size() > 3) {
                    candidates.resize(3);
                }
            }

            std::vector<HotspotOrigin> origins;
            for (const auto& candidate : candidates) {
                const auto& tmpl = *candidate.info;
                HotspotOrigin origin;
                origin.kind = "template_origin";
                origin.estimated_cost = tmpl.total_time;
                origin.note = "Template instantiation hotspot captured from trace events.";
                if (!tmpl.locations.empty()) {
                    origin.source = tmpl.locations.front().file;
                } else if (!tmpl.files_using.empty()) {
                    origin.source = tmpl.files_using.front();
                }
                origin.target = tmpl.full_signature.empty() ? tmpl.name : tmpl.full_signature;
                origin.chain.push_back("template: " + (tmpl.full_signature.empty() ? tmpl.name : tmpl.full_signature));

                for (std::size_t i = 0; i < std::min<std::size_t>(3, tmpl.locations.size()); ++i) {
                    const auto& loc = tmpl.locations[i];
                    if (!loc.file.empty() && loc.line > 0) {
                        origin.chain.push_back(loc.file.string() + ":" + std::to_string(loc.line));
                    }
                }
                if (origin.chain.size() == 1) {
                    for (std::size_t i = 0; i < std::min<std::size_t>(3, tmpl.files_using.size()); ++i) {
                        origin.chain.push_back("used in: " + tmpl.files_using[i]);
                    }
                }
                origins.push_back(std::move(origin));
            }

            return origins;
        }

        void enrich_hotspot_origins(
            std::vector<Suggestion>& suggestions,
            const BuildTrace& trace,
            const analyzers::AnalysisResult& analysis,
            const fs::path& project_root,
            const std::optional<std::chrono::steady_clock::time_point>& deadline
        ) {
            std::unordered_map<std::string, const CompilationUnit*> units_by_source;
            units_by_source.reserve(trace.units.size());
            for (const auto& unit : trace.units) {
                units_by_source.emplace(normalize_key(unit.source_file, project_root), &unit);
            }

            std::unordered_map<std::string, std::vector<fs::path>> include_dirs_by_source;
            include_dirs_by_source.reserve(trace.units.size());
            for (const auto& unit : trace.units) {
                include_dirs_by_source.emplace(
                    normalize_key(unit.source_file, project_root),
                    parse_include_dirs(unit, project_root)
                );
            }

            ExplainabilityCache cache;
            cache.directives_by_file.reserve(trace.units.size() * 4);
            cache.include_chain_cache.reserve(suggestions.size() * 2 + 8);

            for (auto& suggestion : suggestions) {
                if (deadline_reached(deadline)) {
                    break;
                }

                if (!suggestion.hotspot_origins.empty()) {
                    continue;
                }

                auto template_origins = derive_template_origins(suggestion, analysis);
                suggestion.hotspot_origins.insert(
                    suggestion.hotspot_origins.end(),
                    std::make_move_iterator(template_origins.begin()),
                    std::make_move_iterator(template_origins.end())
                );

                const auto sources = gather_source_candidates(suggestion, analysis, project_root);
                for (const auto& source : sources) {
                    if (deadline_reached(deadline)) {
                        return;
                    }

                    const std::string source_key = source.generic_string();
                    const CompilationUnit* unit = nullptr;
                    if (const auto unit_it = units_by_source.find(source_key); unit_it != units_by_source.end()) {
                        unit = unit_it->second;
                    }

                    const auto headers = extract_candidate_headers(suggestion, unit, project_root);
                    if (headers.empty()) {
                        continue;
                    }

                    auto dirs_it = include_dirs_by_source.find(source_key);
                    std::vector<fs::path> include_dirs;
                    if (dirs_it != include_dirs_by_source.end()) {
                        include_dirs = dirs_it->second;
                    } else if (unit != nullptr) {
                        include_dirs = parse_include_dirs(*unit, project_root);
                    } else {
                        include_dirs.push_back(source.parent_path());
                        if (!project_root.empty()) {
                            include_dirs.push_back(project_root);
                        }
                    }

                    bool found_chain_for_source = false;
                    for (const auto& header : headers) {
                        if (deadline_reached(deadline)) {
                            return;
                        }

                        auto chain = find_include_chain(source, header, include_dirs, project_root, cache, deadline);
                        if (chain.empty()) {
                            continue;
                        }

                        HotspotOrigin origin;
                        origin.kind = "include_chain";
                        origin.source = source;
                        origin.target = header;
                        origin.estimated_cost = estimate_include_cost(unit, header, analysis, project_root);
                        origin.note = "Exact include chain reconstructed from source/header directives.";
                        origin.chain.reserve(chain.size());
                        for (const auto& node : chain) {
                            origin.chain.push_back(node.generic_string());
                        }
                        suggestion.hotspot_origins.push_back(std::move(origin));
                        found_chain_for_source = true;
                        break;
                    }

                    if (found_chain_for_source && suggestion.hotspot_origins.size() >= 3) {
                        break;
                    }
                }
            }
        }

    }  // namespace

    SuggesterRegistry& SuggesterRegistry::instance() {
        static SuggesterRegistry instance;
        return instance;
    }

    void SuggesterRegistry::register_suggester(std::unique_ptr<ISuggester> suggester) {
        if (suggester) {
            suggesters_.push_back(std::move(suggester));
        }
    }

    const ISuggester* SuggesterRegistry::find(const std::string_view name) const {
        for (const auto& suggester : suggesters_) {
            if (suggester->name() == name) {
                return suggester.get();
            }
        }
        return nullptr;
    }

    Result<std::vector<Suggestion>, Error> generate_all_suggestions(
        const BuildTrace& trace,
        const analyzers::AnalysisResult& analysis,
        const SuggesterOptions& options,
        const fs::path& project_root
    ) {
        std::vector<Suggestion> all_suggestions;

        std::atomic<bool> cancelled{false};
        SuggestionContext context{trace, analysis, options, project_root};
        const ProjectLanguageProfile project_languages = summarize_project_language_profile(trace);
        context.cancelled = &cancelled;
        if (options.restrict_to_trace) {
            std::unordered_set<std::string> targets;
            targets.reserve(analysis.files.size() * 2 + 64);

            auto normalize = [&](const fs::path& path) {
                fs::path resolved = path;
                if (resolved.is_relative()) {
                    if (!project_root.empty()) {
                        resolved = project_root / resolved;
                    } else {
                        std::error_code ec;
                        resolved = fs::absolute(resolved, ec);
                    }
                }
                resolved = resolved.lexically_normal();
                if (resolved.parent_path().empty()) {
                    return resolved.filename().string();
                }
                return resolved.generic_string();
            };

            std::vector<std::string> queue;
            queue.reserve(analysis.files.size());
            for (const auto& file : analysis.files) {
                const std::string key = normalize(file.file);
                if (targets.insert(key).second) {
                    queue.push_back(std::move(key));
                }
            }

            std::unordered_map<std::string, std::vector<fs::path>> included_by_map;
            included_by_map.reserve(analysis.dependencies.headers.size());
            for (const auto& header : analysis.dependencies.headers) {
                for (const auto& includer : header.included_by) {
                    const std::string key = normalize(includer);
                    included_by_map[key].push_back(header.path);
                }
            }

            std::size_t queue_index = 0;
            while (queue_index < queue.size()) {
                const std::string& includer_key = queue[queue_index++];
                auto it = included_by_map.find(includer_key);
                if (it == included_by_map.end()) {
                    continue;
                }
                for (const auto& header_path : it->second) {
                    const std::string header_key = normalize(header_path);
                    if (targets.insert(header_key).second) {
                        queue.push_back(header_key);
                    }
                }
            }

            context.target_files.reserve(targets.size());
            context.target_files_lookup = std::move(targets);
            for (const auto& entry : context.target_files_lookup) {
                context.target_files.emplace_back(entry);
            }
        }

        const auto total_start = std::chrono::steady_clock::now();
        const auto total_deadline = options.max_total_time != Duration::zero()
            ? std::optional<std::chrono::steady_clock::time_point>(total_start + options.max_total_time)
            : std::optional<std::chrono::steady_clock::time_point>();

        for (const auto& suggester : SuggesterRegistry::instance().suggesters()) {
            if (!language_support_matches(suggester->policy().language_support, project_languages)) {
                continue;
            }

            if (options.max_total_time != Duration::zero()) {
                const auto total_elapsed = std::chrono::steady_clock::now() - total_start;
                if (total_elapsed >= options.max_total_time) {
                    break;
                }
            }

            if (!options.enabled_types.empty()) {
                bool enabled = false;
                const auto supported_types = suggester->supported_types();
                for (const auto type : options.enabled_types) {
                    if (std::ranges::find(supported_types, type) != supported_types.end()) {
                        enabled = true;
                        break;
                    }
                }
                if (!enabled) {
                    continue;
                }
            }

            const auto suggester_start = std::chrono::steady_clock::now();
            if (options.max_suggester_time != Duration::zero()) {
                auto deadline = suggester_start + options.max_suggester_time;
                if (total_deadline.has_value() && *total_deadline < deadline) {
                    deadline = *total_deadline;
                }
                context.deadline = deadline;
            } else {
                context.deadline = total_deadline;
            }

            auto result = suggester->suggest(context);
            const auto suggester_elapsed = std::chrono::steady_clock::now() - suggester_start;
            if (!result.is_ok()) {
                continue;
            }

            auto result_value = std::move(result.value());
            if (result_value.generation_time == Duration::zero()) {
                result_value.generation_time =
                    std::chrono::duration_cast<Duration>(suggester_elapsed);
            }
            if (options.on_suggester_completed) {
                options.on_suggester_completed(
                    suggester->name(),
                    result_value.generation_time,
                    result_value.suggestions.size()
                );
            }

            if (options.max_suggester_time != Duration::zero() &&
                suggester_elapsed >= options.max_suggester_time) {
                continue;
            }

            for (auto& suggestion : result_value.suggestions) {
                if (options.conservative_abi_sensitive_headers &&
                    suggester->policy().abi_sensitivity == SuggesterAbiSensitivity::HeaderSurface &&
                    suggestion_touches_abi_sensitive_header(suggestion, project_root)) {
                    downgrade_suggestion_to_manual_review(
                        suggestion,
                        "Touches a public or extern \"C\" header surface",
                        "Review and apply this change manually after validating exported or C-ABI headers across supported consumers."
                    );
                }

                if (suggestion.priority > options.min_priority) {
                    continue;
                }
                if (suggestion.confidence < options.min_confidence) {
                    continue;
                }
                if (!suggestion.is_safe &&
                    resolve_application_mode(suggestion) != SuggestionApplicationMode::ExternalRefactor &&
                    !options.include_unsafe) {
                    continue;
                }

                all_suggestions.push_back(std::move(suggestion));

                if (all_suggestions.size() >= options.max_suggestions) {
                    break;
                }
            }

            if (all_suggestions.size() >= options.max_suggestions) {
                break;
            }
        }

        if (options.enable_consolidation) {
            ConsolidationOptions consol_opts;
            consol_opts.enable_consolidation = true;
            consol_opts.max_items_per_suggestion = 50;

            const SuggestionConsolidator consolidator(consol_opts);
            all_suggestions = consolidator.consolidate(std::move(all_suggestions));
        }

        std::ranges::sort(all_suggestions,
                          [](const Suggestion& a, const Suggestion& b) {
                              if (a.priority != b.priority) {
                                  return a.priority < b.priority;
                              }
                              return a.estimated_savings > b.estimated_savings;
                          });

        std::optional<std::chrono::steady_clock::time_point> explainability_deadline = total_deadline;
        if (!explainability_deadline.has_value()) {
            const auto explain_start = std::chrono::steady_clock::now();
            const std::size_t budget_ms = std::clamp<std::size_t>(
                all_suggestions.size() * 25,
                200,
                2000
            );
            explainability_deadline = explain_start + std::chrono::milliseconds(budget_ms);
        }

        if (!deadline_reached(explainability_deadline)) {
            enrich_hotspot_origins(
                all_suggestions,
                trace,
                analysis,
                project_root,
                explainability_deadline
            );
        }

        return Result<std::vector<Suggestion>, Error>::success(std::move(all_suggestions));
    }
}  // namespace bha::suggestions
