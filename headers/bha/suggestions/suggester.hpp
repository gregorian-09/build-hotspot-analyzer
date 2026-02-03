//
// Created by gregorian-rayne on 12/29/25.
//

#ifndef BHA_SUGGESTER_HPP
#define BHA_SUGGESTER_HPP

/**
 * @file suggester.hpp
 * @brief Interface for suggestion generators.
 *
 * Suggesters analyze build traces and analysis results to produce
 * actionable optimization suggestions. Each suggester focuses on
 * a specific optimization strategy:
 *
 * - PCHSuggester: Identifies candidates for precompiled headers
 * - ForwardDeclSuggester: Finds opportunities for forward declarations
 * - IncludeSuggester: Detects removable or reducible includes
 * - TemplateSuggester: Suggests explicit instantiations
 *
 * All suggesters follow the Result<T,E> error handling pattern.
 */

#include "bha/types.hpp"
#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <regex>
#include <sstream>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace bha::suggestions {

    /**
     * Context passed to suggesters containing all analysis data.
     */
    struct SuggestionContext {
        const BuildTrace& trace;
        const analyzers::AnalysisResult& analysis;
        const SuggesterOptions& options;

        /// Optional cancellation token. Suggesters should check this periodically
        /// in long-running loops and return early if canceled.
        std::atomic<bool>* cancelled = nullptr;

        /// Optional filter for incremental analysis. When set, only analyze
        /// files in this list. Empty means analyze all files.
        std::vector<fs::path> target_files{};

        /// Check if the operation has been canceled.
        [[nodiscard]] bool is_cancelled() const noexcept {
            return cancelled != nullptr && cancelled->load(std::memory_order_relaxed);
        }

        /// Check if a file should be analyzed (respects target_files filter).
        [[nodiscard]] bool should_analyze(const fs::path& file) const {
            if (target_files.empty()) {
                return true;
            }

            return std::ranges::any_of(
                target_files,
                [&](const fs::path& target) {
                    return file == target || file.filename() == target.filename();
                }
            );
        }
    };

    /**
     * Generates a unique suggestion ID from a prefix and file path.
     *
     * Uses hash of the full path to avoid collisions when multiple files
     * have the same filename in different directories.
     *
     * @param prefix Suggestion type prefix (e.g., "pch", "fwd", "unused")
     * @param path File path to include in the ID
     * @param suffix Optional additional suffix for disambiguation
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const fs::path& path,
        const std::string_view suffix = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-";

        const std::size_t path_hash = std::hash<std::string>{}(path.string());
        oss << std::hex << (path_hash & 0xFFFFFF);  // 24 bits (6 hex chars)

        // Readable filename for human identification
        oss << "-" << path.filename().string();

        if (!suffix.empty()) {
            oss << "-" << suffix;
        }

        return oss.str();
    }

    /**
     * Generates a unique suggestion ID from a prefix and counter.
     *
     * Use this variant when there's no natural file path (e.g., unity builds).
     *
     * @param prefix Suggestion type prefix
     * @param counter Unique counter value
     * @param name Optional descriptive name
     * @return Unique suggestion ID string
     */
    [[nodiscard]] inline std::string generate_suggestion_id(
        const std::string_view prefix,
        const std::size_t counter,
        const std::string_view name = ""
    ) {
        std::ostringstream oss;
        oss << prefix << "-" << counter;
        if (!name.empty()) {
            oss << "-" << name;
        }
        return oss.str();
    }

    struct IncludeDirective {
        std::size_t line = 0;
        std::size_t col_start = 0;
        std::size_t col_end = 0;
        std::string header_name;
        bool is_system = false;
    };

    [[nodiscard]] inline std::vector<IncludeDirective> find_include_directives(const fs::path& file) {
        std::vector<IncludeDirective> result;

        std::ifstream in(file);
        if (!in) return result;

        std::regex include_regex(R"(^\s*(#\s*include\s*([<"])([\w/\\.-]+)[>"]))", std::regex::ECMAScript);
        std::string line;
        std::size_t line_num = 0;

        while (std::getline(in, line)) {
            if (std::smatch match; std::regex_search(line, match, include_regex)) {
                IncludeDirective directive;
                directive.line = line_num;
                directive.col_start = static_cast<std::size_t>(match.position(1));
                directive.col_end = directive.col_start + static_cast<std::size_t>(match[1].length());
                directive.header_name = match[3].str();
                directive.is_system = (match[2].str() == "<");
                result.push_back(directive);
            }
            ++line_num;
        }

        return result;
    }

    [[nodiscard]] inline std::optional<IncludeDirective> find_include_for_header(
        const fs::path& file,
        const std::string& header_name
    ) {
        for (const auto directives = find_include_directives(file); const auto& dir : directives) {
            if (dir.header_name == header_name ||
                fs::path(dir.header_name).filename().string() == header_name ||
                dir.header_name.find(header_name) != std::string::npos) {
                return dir;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline TextEdit make_delete_line_edit(const fs::path& file, std::size_t line) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = "";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_replace_line_edit(
        const fs::path& file,
        const std::size_t line,
        const std::string& new_content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = new_content + "\n";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_insert_after_line_edit(
        const fs::path& file,
        const std::size_t line,
        const std::string& content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = line + 1;
        edit.start_col = 0;
        edit.end_line = line + 1;
        edit.end_col = 0;
        edit.new_text = content + "\n";
        return edit;
    }

    [[nodiscard]] inline TextEdit make_insert_at_start_edit(
        const fs::path& file,
        const std::string& content
    ) {
        TextEdit edit;
        edit.file = file;
        edit.start_line = 0;
        edit.start_col = 0;
        edit.end_line = 0;
        edit.end_col = 0;
        edit.new_text = content + "\n";
        return edit;
    }

    [[nodiscard]] inline std::size_t find_first_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) return 0;
        return directives.front().line;
    }

    [[nodiscard]] inline std::size_t find_last_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) return 0;
        return directives.back().line;
    }

    struct ClassMemberInfo {
        std::string name;
        std::string type;
        std::size_t line = 0;
        std::size_t col = 0;
        bool is_private = false;
        bool is_method = false;
        bool is_static = false;
        bool is_virtual = false;
    };

    struct ClassInfo {
        std::string name;
        std::string full_name;
        fs::path file;
        std::size_t class_start_line = 0;
        std::size_t class_end_line = 0;
        std::size_t private_section_line = 0;
        std::vector<ClassMemberInfo> members;
        std::vector<std::string> base_classes;
        bool has_destructor = false;
        bool has_copy_constructor = false;
        bool has_move_constructor = false;
    };

    [[nodiscard]] inline std::optional<ClassInfo> parse_class_with_clang(
        const fs::path& file,
        const std::string& class_name
    ) {
        static bool clang_checked = false;
        static bool clang_available = false;
        if (!clang_checked) {
            clang_checked = true;
#ifdef _WIN32
            FILE* test = popen("where clang 2>NUL", "r");
#else
            FILE* test = popen("which clang 2>/dev/null", "r");
#endif
            if (test) {
                char buf[256];
                clang_available = (fgets(buf, sizeof(buf), test) != nullptr);
                pclose(test);
            }
        }
        if (!clang_available) return std::nullopt;

#ifdef _WIN32
        std::string cmd = "clang -Xclang -ast-dump=json -fsyntax-only \"" + file.string() + "\" 2>NUL";
#else
        std::string cmd = "clang -Xclang -ast-dump=json -fsyntax-only \"" + file.string() + "\" 2>/dev/null";
#endif

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::nullopt;

        std::string output;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
            // Limit output to prevent hanging on large AST dumps
            if (output.size() > 10 * 1024 * 1024) {  // 10 MB limit
                pclose(pipe);
                return std::nullopt;
            }
        }
        if (int status = pclose(pipe); status != 0 || output.empty()) return std::nullopt;

        ClassInfo info;
        info.name = class_name;
        info.file = file;

        if (std::regex class_regex(R"("kind"\s*:\s*"CXXRecordDecl"[^}]*"name"\s*:\s*")" + class_name + R"(")"); !std::regex_search(output, class_regex)) {
            return std::nullopt;
        }

        std::regex line_regex(R"("line"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(output, match, line_regex)) {
            info.class_start_line = std::stoul(match[1].str());
        }

        std::regex field_regex(R"re("kind"\s*:\s*"FieldDecl"[^}]*"name"\s*:\s*"([^"]+)"[^}]*"line"\s*:\s*(\d+))re");
        auto search_start = output.cbegin();
        while (std::regex_search(search_start, output.cend(), match, field_regex)) {
            ClassMemberInfo member;
            member.name = match[1].str();
            member.line = std::stoul(match[2].str());
            member.is_private = true;
            info.members.push_back(member);
            search_start = match.suffix().first;
        }

        return info;
    }

    [[nodiscard]] inline std::optional<ClassInfo> parse_class_simple(
        const fs::path& file,
        const std::string& class_name
    ) {
        std::ifstream in(file);
        if (!in) return std::nullopt;

        ClassInfo info;
        info.name = class_name;
        info.file = file;

        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

        std::regex class_regex(R"(\bclass\s+)" + class_name + R"(\s*(?::\s*[^{]+)?\s*\{)");
        std::smatch class_match;
        if (!std::regex_search(content, class_match, class_regex)) {
            return std::nullopt;
        }

        auto class_pos = static_cast<std::size_t>(class_match.position());
        std::size_t line_num = 1;
        for (std::size_t i = 0; i < class_pos; ++i) {
            if (content[i] == '\n') ++line_num;
        }
        info.class_start_line = line_num;

        std::size_t brace_pos = content.find('{', class_pos);
        if (brace_pos == std::string::npos) return std::nullopt;

        int brace_count = 1;
        std::size_t end_pos = brace_pos + 1;
        while (end_pos < content.size() && brace_count > 0) {
            if (content[end_pos] == '{') ++brace_count;
            else if (content[end_pos] == '}') --brace_count;
            ++end_pos;
        }

        std::string class_body = content.substr(brace_pos + 1, end_pos - brace_pos - 2);

        line_num = info.class_start_line;
        for (std::size_t i = class_pos; i < brace_pos; ++i) {
            if (content[i] == '\n') ++line_num;
        }

        bool in_private = false;
        std::size_t body_line = line_num;
        std::istringstream body_stream(class_body);
        std::string line;

        while (std::getline(body_stream, line)) {
            ++body_line;

            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

            if (trimmed.find("private:") == 0 || trimmed.find("private :") == 0) {
                in_private = true;
                if (info.private_section_line == 0) {
                    info.private_section_line = body_line;
                }
                continue;
            }
            if (trimmed.find("public:") == 0 || trimmed.find("protected:") == 0) {
                in_private = false;
                continue;
            }

            if (in_private && !trimmed.empty() && trimmed[0] != '/' && trimmed.find("//") != 0) {
                std::regex member_regex(R"(^\s*([a-zA-Z_][a-zA-Z0-9_:<>,\s\*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[;=])");
                std::smatch member_match;
                if (std::regex_search(trimmed, member_match, member_regex)) {
                    ClassMemberInfo member;
                    member.type = member_match[1].str();
                    member.name = member_match[2].str();
                    member.line = body_line;
                    member.is_private = true;
                    member.is_method = false;
                    info.members.push_back(member);
                }

                if (std::regex method_regex(R"(^\s*(?:virtual\s+)?([a-zA-Z_][a-zA-Z0-9_:<>,\s\*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\()"); std::regex_search(trimmed, member_match, method_regex)) {
                    ClassMemberInfo member;
                    member.type = member_match[1].str();
                    member.name = member_match[2].str();
                    member.line = body_line;
                    member.is_private = true;
                    member.is_method = true;
                    info.members.push_back(member);
                }
            }

            if (trimmed.find("~" + class_name) != std::string::npos) {
                info.has_destructor = true;
            }
            if (trimmed.find(class_name + "(const " + class_name + "&") != std::string::npos ||
                trimmed.find(class_name + "( const " + class_name + " &") != std::string::npos) {
                info.has_copy_constructor = true;
            }
            if (trimmed.find(class_name + "(" + class_name + "&&") != std::string::npos ||
                trimmed.find(class_name + "( " + class_name + " &&") != std::string::npos) {
                info.has_move_constructor = true;
            }
        }

        info.class_end_line = body_line;

        return info;
    }

    [[nodiscard]] inline std::vector<TextEdit> generate_pimpl_edits(
        const ClassInfo& class_info,
        const fs::path& header_file,
        const fs::path& source_file
    ) {
        std::vector<TextEdit> edits;

        if (class_info.members.empty()) return edits;

        std::vector<ClassMemberInfo> private_data_members;
        std::vector<ClassMemberInfo> private_methods;

        for (const auto& member : class_info.members) {
            if (member.is_private && !member.is_static) {
                if (member.is_method) {
                    private_methods.push_back(member);
                } else {
                    private_data_members.push_back(member);
                }
            }
        }

        if (private_data_members.empty()) return edits;

        std::ostringstream pimpl_decl;
        pimpl_decl << "    struct Impl;\n";
        pimpl_decl << "    std::unique_ptr<Impl> pimpl_;\n";

        if (class_info.private_section_line > 0) {
            TextEdit replace_private;
            replace_private.file = header_file;
            replace_private.start_line = class_info.private_section_line;
            replace_private.start_col = 0;

            std::size_t last_member_line = class_info.private_section_line;
            for (const auto& member : private_data_members) {
                if (member.line > last_member_line) {
                    last_member_line = member.line;
                }
            }

            replace_private.end_line = last_member_line + 1;
            replace_private.end_col = 0;
            replace_private.new_text = "private:\n" + pimpl_decl.str();
            edits.push_back(replace_private);
        }

        std::size_t first_include = find_first_include_line(header_file);
        bool has_memory_include = false;

        for (auto includes = find_include_directives(header_file); const auto& inc : includes) {
            if (inc.header_name == "memory") {
                has_memory_include = true;
                break;
            }
        }

        if (!has_memory_include) {
            TextEdit add_memory;
            add_memory.file = header_file;
            add_memory.start_line = first_include;
            add_memory.start_col = 0;
            add_memory.end_line = first_include;
            add_memory.end_col = 0;
            add_memory.new_text = "#include <memory>\n";
            edits.push_back(add_memory);
        }

        std::ostringstream impl_definition;
        impl_definition << "\n// PIMPL Implementation\n";
        impl_definition << "struct " << class_info.name << "::Impl {\n";

        for (const auto& member : private_data_members) {
            impl_definition << "    " << member.type << " " << member.name << ";\n";
        }

        impl_definition << "};\n\n";

        impl_definition << class_info.name << "::" << class_info.name << "()\n";
        impl_definition << "    : pimpl_(std::make_unique<Impl>())\n";
        impl_definition << "{}\n\n";

        impl_definition << class_info.name << "::~" << class_info.name << "() = default;\n\n";

        impl_definition << class_info.name << "::" << class_info.name << "(" << class_info.name << "&&) noexcept = default;\n";
        impl_definition << class_info.name << "& " << class_info.name << "::operator=(" << class_info.name << "&&) noexcept = default;\n";

        if (fs::exists(source_file)) {
            std::ifstream src_in(source_file);
            std::string src_content((std::istreambuf_iterator<char>(src_in)),
                                    std::istreambuf_iterator<char>());
            src_in.close();

            std::size_t last_line = 0;
            for (char c : src_content) {
                if (c == '\n') ++last_line;
            }

            TextEdit add_impl;
            add_impl.file = source_file;
            add_impl.start_line = last_line;
            add_impl.start_col = 0;
            add_impl.end_line = last_line;
            add_impl.end_col = 0;
            add_impl.new_text = impl_definition.str();
            edits.push_back(add_impl);
        } else {
            TextEdit create_source;
            create_source.file = source_file;
            create_source.start_line = 0;
            create_source.start_col = 0;
            create_source.end_line = 0;
            create_source.end_col = 0;

            std::ostringstream new_source;
            new_source << "#include \"" << header_file.filename().string() << "\"\n";
            new_source << impl_definition.str();
            create_source.new_text = new_source.str();
            edits.push_back(create_source);
        }

        return edits;
    }

    /**
     * Result of suggestion generation.
     */
    struct SuggestionResult {
        std::vector<Suggestion> suggestions{};
        Duration generation_time = Duration::zero();
        std::size_t items_analyzed = 0;
        std::size_t items_skipped = 0;
    };

    /**
     * Interface for suggestion generators.
     *
     * Each suggester produces a specific type of optimization suggestion.
     * Suggesters are stateless and thread-safe for concurrent use.
     */
    class ISuggester {
    public:
        virtual ~ISuggester() = default;

        /**
         * Returns the suggester's unique identifier.
         */
        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        /**
         * Returns a human-readable description.
         */
        [[nodiscard]] virtual std::string_view description() const noexcept = 0;

        /**
         * Returns the type of suggestions this suggester produces.
         */
        [[nodiscard]] virtual SuggestionType suggestion_type() const noexcept = 0;

        /**
         * Generates suggestions from the analysis context.
         *
         * @param context The analysis context with trace and results
         * @return Suggestions or an error
         */
        [[nodiscard]] virtual Result<SuggestionResult, Error> suggest(
            const SuggestionContext& context
        ) const = 0;
    };

    /**
     * Registry for all available suggesters.
     */
    class SuggesterRegistry {
    public:
        static SuggesterRegistry& instance();

        void register_suggester(std::unique_ptr<ISuggester> suggester);

        [[nodiscard]] const std::vector<std::unique_ptr<ISuggester>>& suggesters() const noexcept {
            return suggesters_;
        }

        [[nodiscard]] const ISuggester* find(std::string_view name) const;

    private:
        SuggesterRegistry() = default;
        std::vector<std::unique_ptr<ISuggester>> suggesters_;
    };

    /**
     * Runs all registered suggesters and collects results.
     *
     * @param trace The build trace data
     * @param analysis The analysis results
     * @param options Suggester configuration
     * @return All suggestions sorted by priority and impact
     */
    [[nodiscard]] Result<std::vector<Suggestion>, Error> generate_all_suggestions(
        const BuildTrace& trace,
        const analyzers::AnalysisResult& analysis,
        const SuggesterOptions& options
    );

}  // namespace bha::suggestions

#endif //BHA_SUGGESTER_HPP