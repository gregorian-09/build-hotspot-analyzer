//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pimpl_suggester.hpp"
#include "bha/refactor/pimpl_eligibility.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace bha::suggestions
{
    namespace {

        /**
         * Checks if a path is a C++ source file (not a header).
         */
        bool is_source_file(const fs::path& path) {
            const std::string ext = path.extension().string();
            return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
                   ext == ".C" || ext == ".c++";
        }

        /**
         * Gets possible header paths for a source file.
         * Returns multiple candidates since naming conventions vary.
         */
        std::vector<fs::path> get_possible_headers(const fs::path& source) {
            std::vector<fs::path> headers;
            std::unordered_set<std::string> seen;

            const auto add_candidate = [&](fs::path candidate) {
                const std::string key = candidate.lexically_normal().string();
                if (seen.insert(key).second) {
                    headers.push_back(std::move(candidate));
                }
            };

            const fs::path base = source.parent_path() / source.stem();
            std::vector<std::string> exts = {".h", ".hpp", ".hxx", ".H", ".hh"};

            for (const auto& ext : exts) {
                add_candidate(fs::path(base.string() + ext));
            }

            const std::vector<std::string> src_dirs = {"/src/", "/source/", "/sources/"};
            const std::vector<std::string> include_dirs = {"/include/", "/header/", "/headers/"};

            std::string path_str = source.string();

            for (const auto& src_dir : src_dirs) {
                if (const auto src_pos = path_str.find(src_dir); src_pos != std::string::npos) {
                    for (const auto& inc_dir : include_dirs) {
                        std::string include_path = path_str.substr(0, src_pos) + inc_dir +
                                                   path_str.substr(src_pos + src_dir.size());
                        for (const auto& ext : exts) {
                            fs::path h = include_path;
                            h.replace_extension(ext);
                            add_candidate(std::move(h));
                        }
                    }
                    break;
                }
            }

            std::vector<fs::path> components;
            for (const auto& part : source.parent_path()) {
                components.push_back(part);
            }
            constexpr std::array<std::string_view, 3> src_names = {"src", "source", "sources"};
            constexpr std::array<std::string_view, 3> include_names = {"include", "header", "headers"};

            for (std::size_t index = 0; index < components.size(); ++index) {
                const auto component_name = components[index].string();
                const bool is_source_dir = std::ranges::any_of(
                    src_names,
                    [&component_name](const std::string_view candidate) { return component_name == candidate; }
                );
                if (!is_source_dir) {
                    continue;
                }

                for (const auto include_name : include_names) {
                    fs::path candidate_root;
                    for (std::size_t prefix = 0; prefix < index; ++prefix) {
                        candidate_root /= components[prefix];
                    }
                    candidate_root /= include_name;
                    for (std::size_t suffix = index + 1; suffix < components.size(); ++suffix) {
                        candidate_root /= components[suffix];
                    }
                    candidate_root /= source.stem();
                    for (const auto& ext : exts) {
                        add_candidate(fs::path(candidate_root.string() + ext));
                    }
                }
                break;
            }

            return headers;
        }

        std::vector<std::string> split_shell_command(const std::string& command) {
            std::vector<std::string> parts;
            std::string current;
            char quote = '\0';
            bool escaped = false;

            for (const char ch : command) {
                if (escaped) {
                    current.push_back(ch);
                    escaped = false;
                    continue;
                }
                if (ch == '\\') {
                    escaped = true;
                    continue;
                }
                if (quote != '\0') {
                    if (ch == quote) {
                        quote = '\0';
                    } else {
                        current.push_back(ch);
                    }
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    quote = ch;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(ch))) {
                    if (!current.empty()) {
                        parts.push_back(std::move(current));
                        current.clear();
                    }
                    continue;
                }
                current.push_back(ch);
            }

            if (!current.empty()) {
                parts.push_back(std::move(current));
            }
            return parts;
        }

        std::string shell_quote(const std::string& input) {
#ifdef _WIN32
            std::string escaped = "\"";
            for (const char c : input) {
                if (c == '"') {
                    escaped += "\\\"";
                } else {
                    escaped.push_back(c);
                }
            }
            escaped.push_back('"');
            return escaped;
#else
            std::string escaped;
            escaped.reserve(input.size() + 2);
            escaped.push_back('\'');
            for (const char c : input) {
                if (c == '\'') {
                    escaped += "'\\''";
                } else {
                    escaped.push_back(c);
                }
            }
            escaped.push_back('\'');
            return escaped;
#endif
        }

        struct PIMPLRefactorReadiness {
            bool has_compile_context = false;
            bool has_template_declaration = false;
            bool has_inheritance = false;
            bool has_virtual_members = false;
            bool has_private_methods = false;
            bool has_private_inline_method_bodies = false;
            bool has_macro_generated_private_declarations = false;
            bool has_copy_constructor = false;
            bool has_preprocessor_in_class = false;
            std::size_t private_data_members = 0;

            [[nodiscard]] bool has_blockers() const noexcept {
                return has_template_declaration ||
                       has_inheritance ||
                       has_virtual_members ||
                       has_private_inline_method_bodies ||
                       has_macro_generated_private_declarations ||
                       has_private_methods ||
                       has_preprocessor_in_class;
            }

            [[nodiscard]] bool is_strict_candidate() const noexcept {
                return has_compile_context &&
                       private_data_members > 0 &&
                       !has_blockers();
            }
        };

        [[nodiscard]] bha::refactor::PimplEligibilityState to_pimpl_eligibility_state(
            const PIMPLRefactorReadiness& readiness,
            const bool has_explicit_copy_definition = false
        ) {
            bha::refactor::PimplEligibilityState state;
            state.has_compile_context = readiness.has_compile_context;
            state.has_template_declaration = readiness.has_template_declaration;
            state.has_inheritance = readiness.has_inheritance;
            state.has_virtual_members = readiness.has_virtual_members;
            state.has_private_methods = readiness.has_private_methods;
            state.has_private_inline_method_bodies = readiness.has_private_inline_method_bodies;
            state.has_macro_generated_private_declarations = readiness.has_macro_generated_private_declarations;
            state.has_copy_constructor = readiness.has_copy_constructor;
            state.has_explicit_copy_definition = has_explicit_copy_definition;
            state.has_preprocessor_in_class = readiness.has_preprocessor_in_class;
            state.private_data_members = readiness.private_data_members;
            return state;
        }

        struct ASTClassExtraction {
            ClassInfo info;
            bool has_template_declaration = false;
            bool has_inheritance = false;
            bool has_virtual_members = false;
            bool has_private_inline_method_bodies = false;
            bool has_macro_generated_private_declarations = false;
        };

        std::vector<std::string> load_compile_command_args(
            const SuggestionContext& context,
            const fs::path& source_file
        ) {
            std::vector<std::string> args;
            if (!context.options.compile_commands_path.has_value()) {
                return args;
            }

            std::ifstream in(*context.options.compile_commands_path);
            if (!in) {
                return args;
            }

            nlohmann::json compile_db;
            try {
                in >> compile_db;
            } catch (const nlohmann::json::exception&) {
                return args;
            }
            if (!compile_db.is_array()) {
                return args;
            }

            const fs::path needle = source_file.lexically_normal();
            for (const auto& entry : compile_db) {
                if (!entry.is_object()) {
                    continue;
                }

                fs::path candidate;
                if (entry.contains("file") && entry["file"].is_string()) {
                    candidate = entry["file"].get<std::string>();
                }
                if (entry.contains("directory") && entry["directory"].is_string() && candidate.is_relative()) {
                    candidate = fs::path(entry["directory"].get<std::string>()) / candidate;
                }
                candidate = candidate.lexically_normal();
                if (candidate != needle && candidate.filename() != needle.filename()) {
                    continue;
                }

                if (entry.contains("arguments") && entry["arguments"].is_array()) {
                    for (const auto& arg : entry["arguments"]) {
                        if (arg.is_string()) {
                            args.push_back(arg.get<std::string>());
                        }
                    }
                } else if (entry.contains("command") && entry["command"].is_string()) {
                    args = split_shell_command(entry["command"].get<std::string>());
                }

                if (!args.empty() && entry.contains("directory") && entry["directory"].is_string()) {
                    const fs::path directory = entry["directory"].get<std::string>();
                    for (auto& arg : args) {
                        fs::path path_arg(arg);
                        if (path_arg.is_relative() &&
                            (path_arg.extension() == ".c" || path_arg.extension() == ".cc" ||
                             path_arg.extension() == ".cpp" || path_arg.extension() == ".cxx")) {
                            arg = (directory / path_arg).lexically_normal().string();
                        }
                    }
                }
                return args;
            }

            return args;
        }

        bool has_compile_command_for_source(
            const SuggestionContext& context,
            const fs::path& source_file
        ) {
            return !load_compile_command_args(context, source_file).empty();
        }

        std::vector<std::string> filter_compile_args_for_ast_probe(
            const std::vector<std::string>& args
        ) {
            std::vector<std::string> filtered;
            filtered.reserve(args.size());

            for (std::size_t i = 0; i < args.size(); ++i) {
                const std::string& arg = args[i];
                if (i == 0) {
                    continue;
                }
                if (arg == "-c" || arg == "-Winvalid-pch") {
                    continue;
                }
                if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ" || arg == "-x") {
                    ++i;
                    continue;
                }
                if (arg.starts_with("-o") && arg.size() > 2) {
                    continue;
                }
                const bool keep_prefixed =
                    arg.starts_with("-I") || arg.starts_with("-isystem") || arg.starts_with("-iquote") ||
                    arg.starts_with("-include") || arg.starts_with("-D") || arg.starts_with("-U") ||
                    arg.starts_with("-std=") || arg.starts_with("-stdlib=");
                if (keep_prefixed) {
                    filtered.push_back(arg);
                    continue;
                }
                if (arg == "-I" || arg == "-isystem" || arg == "-iquote" || arg == "-include" ||
                    arg == "-D" || arg == "-U" || arg == "-std" || arg == "-stdlib") {
                    filtered.push_back(arg);
                    if (i + 1 < args.size()) {
                        filtered.push_back(args[++i]);
                    }
                    continue;
                }
            }

            return filtered;
        }

        std::optional<ASTClassExtraction> extract_class_info_from_compile_db_ast(
            const SuggestionContext& context,
            const fs::path& source_file,
            const fs::path& header_file,
            const std::string& class_name
        ) {
            auto args = load_compile_command_args(context, source_file);
            if (args.empty()) {
                return std::nullopt;
            }

            std::string compiler = args.front();
            auto filtered = filter_compile_args_for_ast_probe(args);

            const auto tmp_name = "bha-pimpl-ast-" +
                std::to_string(static_cast<long long>(
                    std::chrono::steady_clock::now().time_since_epoch().count())) + ".txt";
            const fs::path ast_path = fs::temp_directory_path() / tmp_name;

            std::ostringstream cmd;
            cmd << shell_quote(compiler);
            for (const auto& arg : filtered) {
                cmd << ' ' << shell_quote(arg);
            }
            cmd << " -x c++-header -Xclang -ast-dump -fsyntax-only "
                << shell_quote(header_file.string())
                << " > " << shell_quote(ast_path.string())
                << " 2>/dev/null";

            struct Cleanup {
                fs::path path;
                ~Cleanup() {
                    if (!path.empty()) {
                        std::error_code ec;
                        fs::remove(path, ec);
                    }
                }
            } cleanup{ast_path};

            if (std::system(cmd.str().c_str()) != 0 || !fs::exists(ast_path)) {
                return std::nullopt;
            }

            std::ifstream in(ast_path);
            if (!in) {
                return std::nullopt;
            }

            ASTClassExtraction extraction;
            extraction.info.name = class_name;
            extraction.info.file = header_file;

            const std::string class_marker = "class " + class_name + " definition";
            const std::string struct_marker = "struct " + class_name + " definition";
            const std::string template_marker = "ClassTemplateDecl";
            bool in_class = false;
            std::size_t class_indent = 0;
            bool in_private = false;
            std::optional<std::size_t> nested_record_indent;
            std::string line;

            auto line_indent = [](const std::string& text) {
                return text.find_first_not_of("| `-");
            };
            auto has_line_location = [](const std::string& text) {
                static const std::regex line_location_regex(
                    R"(<(?:[^:>]+:)?line:\d+:\d+)"
                );
                return std::regex_search(text, line_location_regex);
            };

            while (std::getline(in, line)) {
                if (!in_class) {
                    if (line.find(template_marker) != std::string::npos &&
                        line.find(' ' + class_name) != std::string::npos) {
                        extraction.has_template_declaration = true;
                    }
                    if (line.find("CXXRecordDecl") == std::string::npos) {
                        continue;
                    }
                    if (line.find(class_marker) == std::string::npos &&
                        line.find(struct_marker) == std::string::npos) {
                        continue;
                    }

                    const auto indent = line_indent(line);
                    if (indent == std::string::npos) {
                        continue;
                    }
                    class_indent = indent;
                    in_class = true;

                    if (std::smatch match;
                        std::regex_search(line, match, std::regex(R"(<(?:[^:>]+:)?line:(\d+):\d+,\s*line:(\d+):\d+>)"))) {
                        extraction.info.class_start_line = std::stoul(match[1].str());
                        extraction.info.class_end_line = std::stoul(match[2].str());
                    } else if (std::regex_search(line, match, std::regex(R"(<line:(\d+):\d+,\s*line:(\d+):\d+>)"))) {
                        extraction.info.class_start_line = std::stoul(match[1].str());
                        extraction.info.class_end_line = std::stoul(match[2].str());
                    }
                    continue;
                }

                const auto indent = line_indent(line);
                if (indent == std::string::npos || indent <= class_indent) {
                    break;
                }

                if (nested_record_indent) {
                    if (indent > *nested_record_indent) {
                        continue;
                    }
                    nested_record_indent.reset();
                }

                if ((line.find("CXXRecordDecl") != std::string::npos ||
                     line.find("ClassTemplateDecl") != std::string::npos) &&
                    indent > class_indent) {
                    nested_record_indent = indent;
                    continue;
                }

                if (line.find("AccessSpecDecl") != std::string::npos) {
                    if (line.find(" private") != std::string::npos || line.ends_with(" private")) {
                        in_private = true;
                        if (extraction.info.private_section_line == 0) {
                            if (std::smatch match;
                                std::regex_search(line, match, std::regex(R"(<(?:[^:>]+:)?line:(\d+):\d+)"))) {
                                extraction.info.private_section_line = std::stoul(match[1].str());
                            } else if (std::regex_search(line, match, std::regex(R"(<line:(\d+):\d+)"))) {
                                extraction.info.private_section_line = std::stoul(match[1].str());
                            }
                        }
                    } else if (line.find(" public") != std::string::npos || line.find(" protected") != std::string::npos) {
                        in_private = false;
                    }
                    continue;
                }

                const bool is_inheritance_line =
                    line.find("CXXBaseSpecifier") != std::string::npos ||
                    ((line.find("|-public '") != std::string::npos ||
                      line.find("|-protected '") != std::string::npos ||
                      line.find("|-private '") != std::string::npos) &&
                     line.find("AccessSpecDecl") == std::string::npos);
                if (is_inheritance_line) {
                    extraction.has_inheritance = true;
                    continue;
                }

                if (line.find("CXXMethodDecl") != std::string::npos) {
                    const bool is_virtual = line.find(" virtual ") != std::string::npos || line.ends_with(" virtual");
                    const bool is_implicit = line.find(" implicit ") != std::string::npos || line.ends_with(" implicit");
                    if (is_virtual) {
                        extraction.has_virtual_members = true;
                    }

                    if (in_private && !is_implicit && !has_line_location(line)) {
                        extraction.has_macro_generated_private_declarations = true;
                    }

                    if (in_private) {
                        if (!is_implicit &&
                            line.find(" inline ") != std::string::npos &&
                            line.find(" defaulted ") == std::string::npos &&
                            line.find(" deleted ") == std::string::npos) {
                            extraction.has_private_inline_method_bodies = true;
                        }

                        if (is_implicit) {
                            continue;
                        }
                        ClassMemberInfo member;
                        member.is_private = true;
                        member.is_method = true;
                        member.is_virtual = is_virtual;
                        if (std::smatch match;
                            std::regex_search(line, match, std::regex(R"(CXXMethodDecl [^ ]+ <(?:[^:>]+:)?line:(\d+):\d+.* col:\d+ ([A-Za-z_][A-Za-z0-9_]*) ')"))) {
                            member.line = std::stoul(match[1].str());
                            member.name = match[2].str();
                        } else if (std::regex_search(line, match, std::regex(R"(CXXMethodDecl [^ ]+ <line:(\d+):\d+.* col:\d+ ([A-Za-z_][A-Za-z0-9_]*) ')"))) {
                            member.line = std::stoul(match[1].str());
                            member.name = match[2].str();
                        }
                        extraction.info.members.push_back(std::move(member));
                    }
                    continue;
                }

                if (line.find("CXXConstructorDecl") != std::string::npos && line.find(class_name) != std::string::npos) {
                    if (line.find("const " + class_name + " &") != std::string::npos ||
                        line.find("const " + class_name + "&") != std::string::npos) {
                        extraction.info.has_copy_constructor = true;
                    }
                    continue;
                }

                if (line.find("CXXDestructorDecl") != std::string::npos && line.find("~" + class_name) != std::string::npos) {
                    extraction.info.has_destructor = true;
                    continue;
                }

                if (line.find("FieldDecl") != std::string::npos && in_private) {
                    if (!has_line_location(line)) {
                        extraction.has_macro_generated_private_declarations = true;
                    }
                    ClassMemberInfo member;
                    member.is_private = true;
                    member.is_method = false;
                    std::smatch match;
                    if (std::regex_search(line, match, std::regex(R"(FieldDecl [^ ]+ <(?:[^:>]+:)?line:(\d+):\d+.* col:\d+ ([A-Za-z_][A-Za-z0-9_]*) '([^']+)')"))) {
                        member.line = std::stoul(match[1].str());
                        member.name = match[2].str();
                        member.type = match[3].str();
                    } else if (std::regex_search(line, match, std::regex(R"(FieldDecl [^ ]+ <line:(\d+):\d+.* col:\d+ ([A-Za-z_][A-Za-z0-9_]*) '([^']+)')"))) {
                        member.line = std::stoul(match[1].str());
                        member.name = match[2].str();
                        member.type = match[3].str();
                    } else {
                        continue;
                    }
                    extraction.info.members.push_back(std::move(member));
                }
            }

            if (extraction.info.class_start_line == 0 || extraction.info.members.empty()) {
                return std::nullopt;
            }

            return extraction;
        }

        std::optional<bool> detect_explicit_copy_definition_from_compile_db_ast(
            const SuggestionContext& context,
            const fs::path& source_file,
            const std::string& class_name
        ) {
            auto args = load_compile_command_args(context, source_file);
            if (args.empty()) {
                return std::nullopt;
            }

            const std::string compiler = args.front();
            const auto filtered = filter_compile_args_for_ast_probe(args);

            const auto tmp_name = "bha-pimpl-copy-ast-" +
                std::to_string(static_cast<long long>(
                    std::chrono::steady_clock::now().time_since_epoch().count())) + ".txt";
            const fs::path ast_path = fs::temp_directory_path() / tmp_name;

            std::ostringstream cmd;
            cmd << shell_quote(compiler);
            for (const auto& arg : filtered) {
                cmd << ' ' << shell_quote(arg);
            }
            cmd << " -Xclang -ast-dump -fsyntax-only "
                << shell_quote(source_file.string())
                << " > " << shell_quote(ast_path.string())
                << " 2>/dev/null";

            struct Cleanup {
                fs::path path;
                ~Cleanup() {
                    if (!path.empty()) {
                        std::error_code ec;
                        fs::remove(path, ec);
                    }
                }
            } cleanup{ast_path};

            if (std::system(cmd.str().c_str()) != 0 || !fs::exists(ast_path)) {
                return std::nullopt;
            }

            std::ifstream in(ast_path);
            if (!in) {
                return std::nullopt;
            }

            const auto contains_copy_param = [&class_name](const std::string& line) {
                return line.find("const " + class_name + " &") != std::string::npos ||
                       line.find("const " + class_name + "&") != std::string::npos;
            };

            std::string line;
            while (std::getline(in, line)) {
                if (line.find("<line:") == std::string::npos ||
                    line.find("implicit") != std::string::npos ||
                    line.find("defaulted") != std::string::npos) {
                    continue;
                }

                const bool is_copy_ctor =
                    line.find("CXXConstructorDecl") != std::string::npos &&
                    line.find(" " + class_name + " '") != std::string::npos &&
                    contains_copy_param(line);
                const bool is_copy_assign =
                    line.find("CXXMethodDecl") != std::string::npos &&
                    line.find(" operator=") != std::string::npos &&
                    contains_copy_param(line);

                if (is_copy_ctor || is_copy_assign) {
                    return true;
                }
            }

            return false;
        }

        PIMPLRefactorReadiness assess_pimpl_readiness(
            const SuggestionContext& context,
            const fs::path& source_file,
            const fs::path& header_file,
            const std::string& class_name
        ) {
            PIMPLRefactorReadiness readiness;
            readiness.has_compile_context = has_compile_command_for_source(context, source_file);

            std::optional<ClassInfo> class_info;
            bool used_ast_extraction = false;
            if (readiness.has_compile_context) {
                if (auto extraction = extract_class_info_from_compile_db_ast(context, source_file, header_file, class_name)) {
                    class_info = extraction->info;
                    used_ast_extraction = true;
                    readiness.has_template_declaration = extraction->has_template_declaration;
                    readiness.has_inheritance = extraction->has_inheritance;
                    readiness.has_virtual_members = extraction->has_virtual_members;
                    readiness.has_private_inline_method_bodies = extraction->has_private_inline_method_bodies;
                    readiness.has_macro_generated_private_declarations = extraction->has_macro_generated_private_declarations;
                }
            }
            if (!class_info) {
                class_info = parse_class_simple(header_file, class_name);
            }
            if (!class_info) {
                return readiness;
            }

            readiness.has_copy_constructor = class_info->has_copy_constructor;
            readiness.private_data_members = static_cast<std::size_t>(std::count_if(
                class_info->members.begin(),
                class_info->members.end(),
                [](const ClassMemberInfo& member) {
                    return member.is_private && !member.is_method && !member.is_static;
                }
            ));
            readiness.has_private_methods = std::ranges::any_of(
                class_info->members,
                [](const ClassMemberInfo& member) {
                    return member.is_private && member.is_method;
                }
            );
            readiness.has_virtual_members = std::ranges::any_of(
                class_info->members,
                [](const ClassMemberInfo& member) {
                    return member.is_method && member.is_virtual;
                }
            );

            std::ifstream in(header_file);
            if (!in) {
                return readiness;
            }
            const std::string content(
                (std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>()
            );

            if (!used_ast_extraction && !readiness.has_template_declaration) {
                if (const std::regex class_decl_regex(
                        R"(\btemplate\s*<[^>]+>\s*class\s+)" + class_name + R"(\b)");
                    std::regex_search(content, class_decl_regex)) {
                    readiness.has_template_declaration = true;
                }
            }

            if (!used_ast_extraction && !readiness.has_inheritance) {
                if (const std::regex inheritance_regex(
                        R"(\bclass\s+)" + class_name + R"(\s*(?:final\s*)?:\s*[^{]+)");
                    std::regex_search(content, inheritance_regex)) {
                    readiness.has_inheritance = true;
                }
            }

            if (!used_ast_extraction && !readiness.has_virtual_members) {
                if (const std::regex virtual_regex(R"(\bvirtual\b)");
                    std::regex_search(content, virtual_regex)) {
                    readiness.has_virtual_members = true;
                }
            }

            if (class_info->class_start_line != 0 && class_info->class_end_line >= class_info->class_start_line) {
                std::istringstream lines(content);
                std::string line;
                std::size_t line_no = 0;
                bool in_private_section = false;
                while (std::getline(lines, line)) {
                    ++line_no;
                    if (line_no < class_info->class_start_line || line_no > class_info->class_end_line) {
                        continue;
                    }
                    std::string trimmed = line;
                    if (const auto first = trimmed.find_first_not_of(" \t"); first != std::string::npos) {
                        trimmed.erase(0, first);
                    } else {
                        trimmed.clear();
                    }
                    if (const auto last = trimmed.find_last_not_of(" \t\r\n"); last != std::string::npos) {
                        trimmed.erase(last + 1);
                    }

                    if (trimmed.starts_with("private:") || trimmed.starts_with("private :")) {
                        in_private_section = true;
                        if (const auto first = line.find_first_not_of(" \t"); first != std::string::npos &&
                            line[first] == '#') {
                            readiness.has_preprocessor_in_class = true;
                        }
                        continue;
                    }
                    if (trimmed.starts_with("public:") || trimmed.starts_with("protected:")) {
                        in_private_section = false;
                        if (const auto first = line.find_first_not_of(" \t"); first != std::string::npos &&
                            line[first] == '#') {
                            readiness.has_preprocessor_in_class = true;
                        }
                        continue;
                    }
                    if (const auto first = line.find_first_not_of(" \t"); first != std::string::npos &&
                        line[first] == '#') {
                        readiness.has_preprocessor_in_class = true;
                    }
                    if (in_private_section &&
                        !trimmed.empty() &&
                        trimmed.find("class ") != 0 &&
                        trimmed.find("struct ") != 0 &&
                        trimmed.find("friend ") != 0 &&
                        trimmed.find("//") != 0 &&
                        trimmed.find('(') != std::string::npos &&
                        trimmed.find(')') != std::string::npos &&
                        trimmed.find('{') != std::string::npos) {
                        readiness.has_private_inline_method_bodies = true;
                    }
                    if (in_private_section &&
                        !trimmed.empty() &&
                        trimmed.find("//") != 0 &&
                        std::regex_search(trimmed, std::regex(R"(^[A-Z_][A-Z0-9_]*\s*\()"))) {
                        readiness.has_macro_generated_private_declarations = true;
                    }
                }
            } else if (const std::regex preprocessor_regex(R"(^\s*#)", std::regex_constants::multiline);
                       std::regex_search(content, preprocessor_regex)) {
                readiness.has_preprocessor_in_class = true;
            }

            return readiness;
        }

        std::vector<std::string> collect_public_api_lines(
            const ClassInfo& class_info,
            const fs::path& file
        );

        std::vector<std::string> collect_private_non_field_declarations(
            const ClassInfo& class_info,
            const fs::path& file
        );

        struct StrictPimplEligibility;

        StrictPimplEligibility analyze_strict_pimpl_eligibility(
            const ClassInfo& class_info,
            const fs::path& header_file,
            const fs::path& source_file
        );

        std::optional<std::vector<TextEdit>> generate_strict_pimpl_refactor_edits(
            const ClassInfo& class_info,
            const fs::path& header_file,
            const fs::path& source_file
        );

        std::size_t find_private_section_end_line(
            const ClassInfo& class_info,
            const fs::path& header_file
        );

        std::string build_pimpl_prototype_preview(
            const ClassInfo& class_info,
            const fs::path& header_file,
            const fs::path& source_file
        ) {
            std::ostringstream out;
            out << "// Prototype header sketch for " << header_file.filename().string() << "\n";
            out << "class " << class_info.name << " {\n";
            out << "public:\n";
            const auto public_api = collect_public_api_lines(class_info, header_file);
            bool has_move_ctor_decl = false;
            bool has_move_assign_decl = false;
            bool has_dtor_decl = false;
            bool has_copy_ctor_decl = false;
            bool has_copy_assign_decl = false;
            for (const auto& decl : public_api) {
                out << "    " << decl << "\n";
                if (decl.find("~" + class_info.name + "(") != std::string::npos) {
                    has_dtor_decl = true;
                }
                if (decl.find(class_info.name + "(const " + class_info.name + "&") != std::string::npos ||
                    decl.find(class_info.name + "( const " + class_info.name + " &") != std::string::npos) {
                    has_copy_ctor_decl = true;
                }
                if (decl.find(class_info.name + "(" + class_info.name + "&&") != std::string::npos ||
                    decl.find(class_info.name + "( " + class_info.name + " &&") != std::string::npos) {
                    has_move_ctor_decl = true;
                }
                if (decl.find("operator=(") != std::string::npos &&
                    (decl.find("const " + class_info.name + "&") != std::string::npos ||
                     decl.find("const " + class_info.name + " &") != std::string::npos)) {
                    has_copy_assign_decl = true;
                }
                if (decl.find("operator=(") != std::string::npos &&
                    decl.find(class_info.name + "&&") != std::string::npos) {
                    has_move_assign_decl = true;
                }
            }
            if (public_api.empty()) {
                out << "    " << class_info.name << "();\n";
            }
            if (!has_dtor_decl) {
                out << "    ~" << class_info.name << "();\n";
            }
            if (!has_move_ctor_decl) {
                out << "    " << class_info.name << "(" << class_info.name << "&&) noexcept;\n";
            }
            if (!has_move_assign_decl) {
                out << "    " << class_info.name << "& operator=(" << class_info.name << "&&) noexcept;\n";
            }
            if (class_info.has_copy_constructor) {
                if (!has_copy_ctor_decl) {
                    out << "    " << class_info.name << "(const " << class_info.name << "&) = delete;\n";
                }
                if (!has_copy_assign_decl) {
                    out << "    " << class_info.name << "& operator=(const " << class_info.name << "&) = delete;\n";
                }
            }
            out << "\nprivate:\n";
            for (const auto& decl : collect_private_non_field_declarations(class_info, header_file)) {
                out << decl << "\n";
            }
            out << "    struct Impl;\n";
            out << "    std::unique_ptr<Impl> pimpl_;\n";
            out << "};\n\n";

            out << "// Prototype source sketch for " << source_file.filename().string() << "\n";
            out << "struct " << class_info.name << "::Impl {\n";
            for (const auto& member : class_info.members) {
                if (!member.is_private || member.is_method || member.type.empty() || member.name.empty()) {
                    continue;
                }
                out << "    " << member.type << ' ' << member.name << ";\n";
            }
            out << "};\n\n";
            out << class_info.name << "::" << class_info.name << "()\n";
            out << "    : pimpl_(std::make_unique<Impl>()) {}\n";
            out << class_info.name << "::~" << class_info.name << "() = default;\n";
            out << class_info.name << "::" << class_info.name << "(" << class_info.name << "&&) noexcept = default;\n";
            out << class_info.name << "& " << class_info.name << "::operator=("
                << class_info.name << "&&) noexcept = default;\n";

            return out.str();
        }

        struct PublicApiEntry {
            std::size_t line = 0;
            std::string text;
        };

        std::vector<PublicApiEntry> collect_public_api_entries(
            const ClassInfo& class_info,
            const fs::path& file
        ) {
            std::vector<PublicApiEntry> lines;
            std::ifstream in(file);
            if (!in || class_info.class_start_line == 0 || class_info.class_end_line == 0) {
                return lines;
            }

            std::string line;
            std::size_t line_no = 0;
            bool in_public = false;
            while (std::getline(in, line)) {
                ++line_no;
                if (line_no < class_info.class_start_line || line_no > class_info.class_end_line) {
                    continue;
                }

                std::string trimmed = line;
                if (const auto start = trimmed.find_first_not_of(" \t"); start != std::string::npos) {
                    trimmed.erase(0, start);
                } else {
                    trimmed.clear();
                }
                if (!trimmed.empty()) {
                    if (const auto end = trimmed.find_last_not_of(" \t\r\n"); end != std::string::npos) {
                        trimmed.erase(end + 1);
                    }
                }

                if (trimmed.starts_with("class ") || trimmed.starts_with("struct ") ||
                    trimmed == "{" || trimmed == "};" || trimmed == "}") {
                    continue;
                }
                if (trimmed == "public:" || trimmed == "public :") {
                    in_public = true;
                    continue;
                }
                if (trimmed == "private:" || trimmed == "private :" ||
                    trimmed == "protected:" || trimmed == "protected :") {
                    in_public = false;
                    continue;
                }
                if (!in_public || trimmed.empty()) {
                    continue;
                }
                lines.push_back(PublicApiEntry{
                    .line = line_no,
                    .text = trimmed,
                });
            }
            return lines;
        }

        std::vector<std::string> collect_public_api_lines(
            const ClassInfo& class_info,
            const fs::path& file
        ) {
            std::vector<std::string> lines;
            for (const auto& entry : collect_public_api_entries(class_info, file)) {
                lines.push_back(entry.text);
            }
            return lines;
        }

        std::vector<std::string> collect_private_non_field_declarations(
            const ClassInfo& class_info,
            const fs::path& file
        ) {
            std::vector<std::string> declarations;
            std::ifstream in(file);
            if (!in) {
                return declarations;
            }

            struct SourceLine {
                std::string raw;
                std::string trimmed;
            };

            std::unordered_map<std::size_t, SourceLine> file_lines;
            std::string line;
            std::size_t line_no = 0;
            while (std::getline(in, line)) {
                ++line_no;

                std::string trimmed = line;
                if (const auto start = trimmed.find_first_not_of(" \t"); start != std::string::npos) {
                    trimmed.erase(0, start);
                } else {
                    trimmed.clear();
                }
                if (!trimmed.empty()) {
                    if (const auto end = trimmed.find_last_not_of(" \t\r\n"); end != std::string::npos) {
                        trimmed.erase(end + 1);
                    }
                }

                file_lines.emplace(line_no, SourceLine{
                    .raw = std::move(line),
                    .trimmed = std::move(trimmed),
                });
            }

            std::unordered_set<std::size_t> field_lines;
            std::unordered_set<std::size_t> method_lines;
            for (const auto& member : class_info.members) {
                if (!member.is_private || member.line == 0) {
                    continue;
                }
                if (member.is_method) {
                    method_lines.insert(member.line);
                } else {
                    field_lines.insert(member.line);
                }
            }

            enum class AccessSection {
                None,
                Public,
                Protected,
                Private
            };

            auto update_nesting = [](const std::string& text, int& paren_depth, int& brace_depth) {
                for (const char ch : text) {
                    if (ch == '(') {
                        ++paren_depth;
                    } else if (ch == ')' && paren_depth > 0) {
                        --paren_depth;
                    } else if (ch == '{') {
                        ++brace_depth;
                    } else if (ch == '}' && brace_depth > 0) {
                        --brace_depth;
                    }
                }
            };

            auto is_chunk_complete = [](const std::string& text, const int paren_depth, const int brace_depth) {
                return paren_depth == 0 &&
                       brace_depth == 0 &&
                       !text.empty() &&
                       text.find(';') != std::string::npos;
            };

            AccessSection access = AccessSection::None;
            bool collecting = false;
            bool chunk_contains_field = false;
            int paren_depth = 0;
            int brace_depth = 0;
            std::vector<std::string> chunk_lines;

            auto flush_chunk = [&]() {
                if (!collecting || chunk_lines.empty()) {
                    collecting = false;
                    chunk_contains_field = false;
                    paren_depth = 0;
                    brace_depth = 0;
                    chunk_lines.clear();
                    return;
                }

                while (!chunk_lines.empty() && chunk_lines.back().find_first_not_of(" \t\r\n") == std::string::npos) {
                    chunk_lines.pop_back();
                }
                if (!chunk_contains_field && !chunk_lines.empty()) {
                    std::ostringstream joined;
                    for (std::size_t idx = 0; idx < chunk_lines.size(); ++idx) {
                        joined << chunk_lines[idx];
                        if (idx + 1 < chunk_lines.size()) {
                            joined << '\n';
                        }
                    }
                    declarations.push_back(joined.str());
                }

                collecting = false;
                chunk_contains_field = false;
                paren_depth = 0;
                brace_depth = 0;
                chunk_lines.clear();
            };

            for (std::size_t current = class_info.class_start_line; current <= class_info.class_end_line; ++current) {
                const auto it = file_lines.find(current);
                if (it == file_lines.end()) {
                    continue;
                }
                const auto& current_line = it->second;

                if (current_line.trimmed == "public:" || current_line.trimmed == "public :") {
                    flush_chunk();
                    access = AccessSection::Public;
                    continue;
                }
                if (current_line.trimmed == "protected:" || current_line.trimmed == "protected :") {
                    flush_chunk();
                    access = AccessSection::Protected;
                    continue;
                }
                if (current_line.trimmed == "private:" || current_line.trimmed == "private :") {
                    flush_chunk();
                    access = AccessSection::Private;
                    continue;
                }

                if (access != AccessSection::Private) {
                    continue;
                }
                if (!collecting && current_line.trimmed.empty()) {
                    continue;
                }
                if (!collecting &&
                    (current_line.trimmed == "};" || current_line.trimmed == "}" || current_line.trimmed == "{")) {
                    continue;
                }

                if (!collecting) {
                    collecting = true;
                }
                chunk_lines.push_back(current_line.raw);
                if (field_lines.contains(current)) {
                    chunk_contains_field = true;
                }
                if (method_lines.contains(current)) {
                    chunk_contains_field = false;
                }
                update_nesting(current_line.raw, paren_depth, brace_depth);

                std::string combined;
                combined.reserve(128);
                for (const auto& part : chunk_lines) {
                    combined += part;
                    combined.push_back('\n');
                }
                if (is_chunk_complete(combined, paren_depth, brace_depth)) {
                    flush_chunk();
                }
            }

            flush_chunk();

            std::vector<std::string> unique;
            unique.reserve(declarations.size());
            std::unordered_set<std::string> seen;
            for (auto& declaration : declarations) {
                if (seen.insert(declaration).second) {
                    unique.push_back(std::move(declaration));
                }
            }
            return unique;
        }

        struct StrictPimplEligibility {
            bool copy_ctor_declared = false;
            bool copy_assign_declared = false;
            bool copy_ctor_deleted = false;
            bool copy_assign_deleted = false;
            bool copy_ctor_defaulted_in_class = false;
            bool copy_assign_defaulted_in_class = false;
            bool defaulted_out_of_line_ctor = false;
            bool defaulted_out_of_line_dtor = false;
            bool has_explicit_copy_definition = false;
            std::size_t ctor_line = 0;
            std::size_t dtor_line = 0;
            std::string ctor_signature;
            std::string copy_ctor_exception_spec;
            std::string copy_assign_exception_spec;
        };

        StrictPimplEligibility analyze_strict_pimpl_eligibility(
            const ClassInfo& class_info,
            const fs::path& header_file,
            const fs::path& source_file
        ) {
            StrictPimplEligibility eligibility;

            const auto trim_trailing = [](std::string value) {
                while (!value.empty() &&
                       std::isspace(static_cast<unsigned char>(value.back()))) {
                    value.pop_back();
                }
                return value;
            };

            const std::regex copy_ctor_decl_regex(
                "^\\s*" + class_info.name +
                R"(\s*\(\s*const\s+)" + class_info.name +
                R"(\s*&(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\)\s*)" +
                R"((noexcept(?:\s*\([^)]*\))?)?)" +
                R"(\s*(?:=\s*(default|delete)\s*)?;\s*$)"
            );
            const std::regex copy_assign_decl_regex(
                "^\\s*" + class_info.name +
                R"(\s*&\s*operator=\s*\(\s*const\s+)" + class_info.name +
                R"(\s*&(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\)\s*)" +
                R"((noexcept(?:\s*\([^)]*\))?)?)" +
                R"(\s*(?:=\s*(default|delete)\s*)?;\s*$)"
            );

            for (const auto& decl : collect_public_api_lines(class_info, header_file)) {
                std::smatch ctor_match;
                if (!eligibility.copy_ctor_declared &&
                    std::regex_match(decl, ctor_match, copy_ctor_decl_regex)) {
                    eligibility.copy_ctor_declared = true;
                    const std::string exception_spec = trim_trailing(ctor_match[1].str());
                    if (!exception_spec.empty()) {
                        eligibility.copy_ctor_exception_spec = " " + exception_spec;
                    }
                    if (ctor_match.size() > 2) {
                        const std::string spec = trim_trailing(ctor_match[2].str());
                        if (spec == "delete") {
                            eligibility.copy_ctor_deleted = true;
                        } else if (spec == "default") {
                            eligibility.copy_ctor_defaulted_in_class = true;
                        }
                    }
                }
                std::smatch assign_match;
                if (!eligibility.copy_assign_declared &&
                    std::regex_match(decl, assign_match, copy_assign_decl_regex)) {
                    eligibility.copy_assign_declared = true;
                    const std::string exception_spec = trim_trailing(assign_match[1].str());
                    if (!exception_spec.empty()) {
                        eligibility.copy_assign_exception_spec = " " + exception_spec;
                    }
                    if (assign_match.size() > 2) {
                        const std::string spec = trim_trailing(assign_match[2].str());
                        if (spec == "delete") {
                            eligibility.copy_assign_deleted = true;
                        } else if (spec == "default") {
                            eligibility.copy_assign_defaulted_in_class = true;
                        }
                    }
                }
            }

            std::ifstream in(source_file);
            if (!in) {
                return eligibility;
            }

            const std::string exception_spec = R"((?:\s+noexcept(?:\s*\([^)]*\))?)?)";
            const std::regex ctor_defaulted_regex(
                "^\\s*(" + class_info.name + "::" + class_info.name +
                R"(\(\))" + exception_spec + R"()\s*=\s*default\s*;\s*$)"
            );
            const std::regex ctor_empty_body_regex(
                "^\\s*(" + class_info.name + "::" + class_info.name +
                R"(\(\))" + exception_spec + R"()\s*\{\s*\}\s*$)"
            );
            const std::regex dtor_defaulted_regex(
                "^\\s*(" + class_info.name + R"(::~)" + class_info.name +
                R"(\(\))" + exception_spec + R"()\s*=\s*default\s*;\s*$)"
            );
            const std::regex dtor_empty_body_regex(
                "^\\s*(" + class_info.name + R"(::~)" + class_info.name +
                R"(\(\))" + exception_spec + R"()\s*\{\s*\}\s*$)"
            );
            const std::regex copy_ctor_definition_regex(
                "^\\s*" + class_info.name + "::" + class_info.name +
                R"(\s*\(\s*const\s+)" + class_info.name +
                R"(\s*&(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\)\s*(?:noexcept(?:\s*\([^)]*\))?)?)"
            );
            const std::regex copy_assign_definition_regex(
                "^\\s*" + class_info.name + R"(::operator=\s*\(\s*const\s+)" +
                class_info.name +
                R"(\s*&(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\)\s*(?:noexcept(?:\s*\([^)]*\))?)?)"
            );

            std::string line;
            std::size_t line_no = 0;
            while (std::getline(in, line)) {
                ++line_no;
                if (!eligibility.defaulted_out_of_line_ctor) {
                    std::smatch match;
                    if (std::regex_match(line, match, ctor_defaulted_regex) ||
                        std::regex_match(line, match, ctor_empty_body_regex)) {
                        eligibility.defaulted_out_of_line_ctor = true;
                        eligibility.ctor_line = line_no;
                        eligibility.ctor_signature = trim_trailing(match[1].str());
                    }
                }
                if (!eligibility.defaulted_out_of_line_dtor) {
                    if (std::regex_match(line, dtor_defaulted_regex) ||
                        std::regex_match(line, dtor_empty_body_regex)) {
                        eligibility.defaulted_out_of_line_dtor = true;
                        eligibility.dtor_line = line_no;
                    }
                }
                if (!eligibility.has_explicit_copy_definition &&
                    (std::regex_search(line, copy_ctor_definition_regex) ||
                     std::regex_search(line, copy_assign_definition_regex))) {
                    eligibility.has_explicit_copy_definition = true;
                }
            }

            return eligibility;
        }

        std::vector<std::string> discover_defined_class_names(
            const fs::path& source_file
        ) {
            std::vector<std::string> names;
            std::unordered_set<std::string> seen;

            std::ifstream in(source_file);
            if (!in) {
                return names;
            }

            const std::regex ctor_regex(
                R"(\b([A-Za-z_][A-Za-z0-9_]*)::\1\s*\()"
            );
            const std::regex dtor_regex(
                R"(\b([A-Za-z_][A-Za-z0-9_]*)::~\1\s*\()"
            );

            std::string line;
            while (std::getline(in, line)) {
                for (const auto* pattern : {&ctor_regex, &dtor_regex}) {
                    std::smatch match;
                    if (!std::regex_search(line, match, *pattern)) {
                        continue;
                    }
                    const std::string name = match[1].str();
                    if (seen.insert(name).second) {
                        names.push_back(name);
                    }
                }
            }

            return names;
        }

        struct ResolvedPimplTarget {
            std::string class_name;
            PIMPLRefactorReadiness readiness;
            ClassInfo class_info;
        };

        std::optional<ResolvedPimplTarget> resolve_pimpl_target(
            const SuggestionContext& context,
            const fs::path& source_file,
            const fs::path& header_file
        ) {
            if (!fs::exists(header_file)) {
                return std::nullopt;
            }

            std::vector<std::string> candidate_names;
            std::unordered_set<std::string> seen;

            std::string stem_name = source_file.stem().string();
            if (!stem_name.empty()) {
                stem_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem_name[0])));
                if (seen.insert(stem_name).second) {
                    candidate_names.push_back(std::move(stem_name));
                }
            }

            for (auto discovered = discover_defined_class_names(source_file); const auto& name : discovered) {
                if (seen.insert(name).second) {
                    candidate_names.push_back(name);
                }
            }

            std::optional<ResolvedPimplTarget> best;
            std::size_t best_score = 0;

            for (const auto& class_name : candidate_names) {
                auto readiness = assess_pimpl_readiness(context, source_file, header_file, class_name);

                std::optional<ClassInfo> class_info;
                if (readiness.has_compile_context) {
                    if (auto extraction = extract_class_info_from_compile_db_ast(context, source_file, header_file, class_name)) {
                        class_info = extraction->info;
                    }
                }
                if (!class_info) {
                    class_info = parse_class_simple(header_file, class_name);
                }
                if (!class_info) {
                    continue;
                }

                const std::size_t score =
                    (readiness.is_strict_candidate() ? 1000U : 0U) +
                    (readiness.has_compile_context ? 100U : 0U) +
                    (readiness.private_data_members * 10U);

                if (!best || score > best_score) {
                    best = ResolvedPimplTarget{
                        .class_name = class_name,
                        .readiness = readiness,
                        .class_info = *class_info,
                    };
                    best_score = score;
                }
            }

            return best;
        }

        std::optional<std::vector<TextEdit>> generate_strict_pimpl_refactor_edits(
            const ClassInfo& class_info,
            const fs::path& header_file,
            const fs::path& source_file
        ) {
            const auto eligibility = analyze_strict_pimpl_eligibility(class_info, header_file, source_file);
            if (!eligibility.defaulted_out_of_line_ctor ||
                !eligibility.defaulted_out_of_line_dtor ||
                class_info.private_section_line == 0 ||
                eligibility.has_explicit_copy_definition ||
                eligibility.copy_ctor_defaulted_in_class ||
                eligibility.copy_assign_defaulted_in_class) {
                return std::nullopt;
            }
            if (eligibility.copy_ctor_deleted != eligibility.copy_assign_deleted) {
                return std::nullopt;
            }

            std::vector<TextEdit> edits;
            const bool preserve_copy = !eligibility.copy_ctor_deleted && !eligibility.copy_assign_deleted;

            bool has_memory_include = false;
            for (const auto includes = find_include_directives(header_file); const auto& inc : includes) {
                if (inc.header_name == "memory") {
                    has_memory_include = true;
                    break;
                }
            }
            if (!has_memory_include) {
                if (auto insert_line = find_include_insertion_line(header_file)) {
                    edits.push_back(make_insert_after_line_edit(header_file, *insert_line, "#include <memory>"));
                } else {
                    edits.push_back(make_insert_at_start_edit(header_file, "#include <memory>"));
                }
            }

            const auto private_non_field_declarations = collect_private_non_field_declarations(class_info, header_file);

            const std::size_t private_section_end_line = find_private_section_end_line(
                class_info,
                header_file
            );
            TextEdit replace_private;
            replace_private.file = header_file;
            replace_private.start_line = class_info.private_section_line - 1;
            replace_private.start_col = 0;
            replace_private.end_line = private_section_end_line;
            replace_private.end_col = 0;
            std::string private_block;
            if (preserve_copy && !eligibility.copy_ctor_declared) {
                private_block += "    " + class_info.name + "(const " + class_info.name + "&);\n";
            }
            if (preserve_copy && !eligibility.copy_assign_declared) {
                private_block += "    " + class_info.name + "& operator=(const " + class_info.name + "&);\n";
            }
            private_block += "private:\n";
            for (const auto& decl : private_non_field_declarations) {
                private_block += decl + "\n";
            }
            private_block += "    struct Impl;\n    std::unique_ptr<Impl> pimpl_;\n";
            replace_private.new_text = private_block;
            edits.push_back(std::move(replace_private));

            TextEdit replace_ctor;
            replace_ctor.file = source_file;
            replace_ctor.start_line = eligibility.ctor_line - 1;
            replace_ctor.start_col = 0;
            replace_ctor.end_line = eligibility.ctor_line;
            replace_ctor.end_col = 0;
            const std::string ctor_signature = eligibility.ctor_signature.empty()
                ? class_info.name + "::" + class_info.name + "()"
                : eligibility.ctor_signature;
            replace_ctor.new_text = ctor_signature + " : pimpl_(std::make_unique<Impl>()) {}\n";
            edits.push_back(std::move(replace_ctor));

            std::ifstream src_in(source_file);
            if (!src_in) {
                return std::nullopt;
            }
            std::vector<std::string> src_lines;
            for (std::string line; std::getline(src_in, line); ) {
                src_lines.push_back(std::move(line));
            }

            std::ostringstream impl_def;
            impl_def << "struct " << class_info.name << "::Impl {\n";
            for (const auto& member : class_info.members) {
                if (!member.is_private || member.is_method || member.type.empty() || member.name.empty()) {
                    continue;
                }
                impl_def << "    " << member.type << ' ' << member.name << ";\n";
            }
            impl_def << "};\n\n";
            if (preserve_copy) {
                impl_def << class_info.name << "::" << class_info.name << "(const " << class_info.name
                         << "& other)" << eligibility.copy_ctor_exception_spec << "\n";
                impl_def << "    : pimpl_(other.pimpl_ ? std::make_unique<Impl>(*other.pimpl_) : nullptr) {}\n\n";
                impl_def << class_info.name << "& " << class_info.name << "::operator=(const " << class_info.name
                         << "& other)" << eligibility.copy_assign_exception_spec << " {\n";
                impl_def << "    if (this == &other) {\n";
                impl_def << "        return *this;\n";
                impl_def << "    }\n";
                impl_def << "    if (!other.pimpl_) {\n";
                impl_def << "        pimpl_.reset();\n";
                impl_def << "        return *this;\n";
                impl_def << "    }\n";
                impl_def << "    if (pimpl_) {\n";
                impl_def << "        *pimpl_ = *other.pimpl_;\n";
                impl_def << "    } else {\n";
                impl_def << "        pimpl_ = std::make_unique<Impl>(*other.pimpl_);\n";
                impl_def << "    }\n";
                impl_def << "    return *this;\n";
                impl_def << "}\n\n";
            }
            TextEdit add_impl;
            add_impl.file = source_file;
            add_impl.start_line = eligibility.ctor_line - 1;
            add_impl.start_col = 0;
            add_impl.end_line = eligibility.ctor_line - 1;
            add_impl.end_col = 0;
            add_impl.new_text = impl_def.str();
            edits.push_back(std::move(add_impl));

            bool in_class_method = false;
            int brace_depth = 0;

            const auto brace_delta = [](const std::string& text) {
                int delta = 0;
                for (const char ch : text) {
                    if (ch == '{') {
                        ++delta;
                    } else if (ch == '}') {
                        --delta;
                    }
                }
                return delta;
            };

            for (std::size_t i = 0; i < src_lines.size(); ++i) {
                std::string updated = src_lines[i];
                if (i + 1 == eligibility.ctor_line || i + 1 == eligibility.dtor_line) {
                    continue;
                }

                bool entering_method_signature = false;
                if (!in_class_method) {
                    if (updated.find(class_info.name + "::") == std::string::npos ||
                        updated.find('(') == std::string::npos ||
                        updated.find('{') == std::string::npos) {
                        continue;
                    }
                    in_class_method = true;
                    entering_method_signature = true;
                }

                if (updated.find("//") == std::string::npos &&
                    (updated.empty() || updated.front() != '#') &&
                    !entering_method_signature) {
                    bool changed = false;
                    for (const auto& member : class_info.members) {
                        if (!member.is_private || member.is_method || member.name.empty()) {
                            continue;
                        }
                        const std::regex member_regex("\\b" + member.name + "\\b");
                        const std::string replaced = std::regex_replace(updated, member_regex, "pimpl_->" + member.name);
                        if (replaced != updated) {
                            updated = replaced;
                            changed = true;
                        }
                    }
                    if (changed) {
                        edits.push_back(make_replace_line_edit(source_file, i, updated));
                    }
                }

                brace_depth += brace_delta(updated);
                if (brace_depth <= 0) {
                    in_class_method = false;
                    brace_depth = 0;
                }
            }

            return edits;
        }

        std::size_t find_private_section_end_line(
            const ClassInfo& class_info,
            const fs::path& header_file
        ) {
            if (class_info.private_section_line == 0) {
                return 0;
            }

            std::ifstream in(header_file);
            if (!in) {
                return class_info.private_section_line;
            }

            auto trim = [](std::string text) {
                if (const auto first = text.find_first_not_of(" \t"); first != std::string::npos) {
                    text.erase(0, first);
                } else {
                    text.clear();
                }
                if (!text.empty()) {
                    if (const auto last = text.find_last_not_of(" \t\r\n"); last != std::string::npos) {
                        text.erase(last + 1);
                    }
                }
                return text;
            };

            std::size_t line_no = 0;
            bool in_private = false;
            std::size_t last_private_line = class_info.private_section_line;
            std::string line;
            while (std::getline(in, line)) {
                ++line_no;
                if (line_no < class_info.class_start_line || line_no > class_info.class_end_line) {
                    continue;
                }

                const std::string trimmed = trim(line);
                const bool is_private_label = trimmed == "private:" || trimmed == "private :";
                const bool is_other_access =
                    trimmed == "public:" || trimmed == "public :" ||
                    trimmed == "protected:" || trimmed == "protected :";

                if (line_no == class_info.private_section_line || is_private_label) {
                    in_private = true;
                    last_private_line = std::max(last_private_line, line_no);
                    continue;
                }

                if (!in_private) {
                    continue;
                }

                if (is_other_access) {
                    break;
                }
                if (trimmed == "};" || trimmed == "}") {
                    break;
                }
                last_private_line = line_no;
            }

            return std::max(last_private_line, class_info.private_section_line);
        }


        /**
         * PIMPL candidate analysis result.
         *
         * Captures all relevant metrics for deciding if a class benefits from PIMPL.
         */
        struct PIMPLCandidate {
            fs::path source_file;
            fs::path header_file;

            // Compilation metrics
            Duration compile_time = Duration::zero();
            Duration frontend_time = Duration::zero();
            Duration backend_time = Duration::zero();

            // Dependency metrics
            std::size_t direct_includes = 0;
            std::size_t template_instantiations = 0;
            std::size_t dependent_files = 0;  // Files that include the header

            // Computed scores
            double complexity_score = 0.0;
            double impact_score = 0.0;
            double confidence = 0.0;

            Priority priority = Priority::Low;
        };

        /**
         * Calculates a heuristic complexity score for a source file.
         *
         * Combines empirical indicators of build cost into a single metric:
         * - `frontend_ms`: frontend compile time (larger suggests heavier template/include work)
         * - `includes`: number of include dependencies
         * - `templates`: count of template instantiations
         *
         * Logs dampen the influence of large values, while the template factor
         * adds linear scaling for higher metaprogramming overhead.
         *
         * This is a heuristic score for prioritization and hotspot ranking,
         * not a formal algorithmic complexity class.
         */
        double calculate_complexity_score(
            const Duration frontend_time,
            const std::size_t direct_includes,
            const std::size_t template_count
        ) {
            const auto frontend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                frontend_time).count();

            const double frontend_factor = std::log(static_cast<double>(frontend_ms) + 1.0);
            const double include_factor = std::log(static_cast<double>(direct_includes) + 1.0);
            const double template_factor = 1.0 + 0.1 * static_cast<double>(template_count);

            return frontend_factor * include_factor * template_factor;
        }

        /**
         * Calculates the impact of applying PIMPL to this class.
         *
         * Impact is based on:
         * - Number of files that would benefit (dependents)
         * - Current compile time (potential savings)
         * - Transitive dependency depth
         *
         * Higher impact = more worthwhile to apply PIMPL.
         */
        double calculate_impact_score(
            const Duration compile_time,
            const std::size_t dependent_files,
            const std::size_t transitive_includes
        ) {
            const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                compile_time).count();

            // Each dependent file benefits from reduced header parsing
            const double dependent_factor = std::log(static_cast<double>(dependent_files) + 1.0);

            // Higher compile time = more to potentially save
            const double time_factor = std::log(static_cast<double>(compile_ms) + 1.0);

            // Deep transitive chains benefit more from PIMPL
            const double depth_factor = 1.0 + 0.05 * static_cast<double>(transitive_includes);

            return dependent_factor * time_factor * depth_factor;
        }

        /**
         * Calculates confidence that PIMPL will help.
         *
         * Based on:
         * - Frontend/backend time ratio (high frontend = good candidate)
         * - Include count relative to compile time
         * - Absence of patterns that don't work well with PIMPL
         *
         * Returns 0.0 to 1.0.
         */
        double calculate_confidence(
            const Duration frontend_time,
            const Duration backend_time,
            const Duration compile_time,
            const std::size_t include_count
        ) {
            const auto frontend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                frontend_time).count();
            const auto backend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                backend_time).count();
            const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                compile_time).count();

            if (total_ms <= 0) {
                return 0.3;  // Base confidence when no time data
            }

            // Frontend-heavy files benefit most from PIMPL
            // (It is important to note that PIMPL reduces parsing, not code generation)
            const double frontend_ratio = (frontend_ms + backend_ms > 0) ?
                static_cast<double>(frontend_ms) / static_cast<double>(frontend_ms + backend_ms) :
                0.5;

            // High include count with high compile time = good fit
            double include_time_factor = 0.5;
            if (include_count > 10 && total_ms > 1000) {
                include_time_factor = 0.8;
            } else if (include_count > 5 && total_ms > 500) {
                include_time_factor = 0.65;
            }

            const double confidence = (frontend_ratio * 0.5 + include_time_factor * 0.5);
            return std::max(0.3, std::min(0.95, confidence));
        }

        /**
         * Determines priority based on compile time and include count.
         *
         * Uses thresholds based on industry experience:
         * - Critical: > 5000ms and >= 20 includes (severe build impact)
         * - High: > 2000ms and >= 10 includes (significant impact)
         * - Medium: > 1000ms and >= 5 includes (moderate impact)
         * - Low: below thresholds but still worth considering
         */
        Priority calculate_priority(Duration compile_time, std::size_t include_count) {
            const auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                compile_time).count();

            if (compile_ms > 5000 && include_count >= 20) {
                return Priority::Critical;
            }
            if (compile_ms > 2000 && include_count >= 10) {
                return Priority::High;
            }
            if (compile_ms > 1000 && include_count >= 5) {
                return Priority::Medium;
            }

            // Fallback for borderline cases
            if (compile_ms > 3000) {
                return Priority::High;
            }
            if (compile_ms > 1500) {
                return Priority::Medium;
            }

            return Priority::Low;
        }

        /**
         * Estimates compile time savings from applying PIMPL.
         *
         * Research basis:
         * [1] PIMPL typically saves 20-40% of header parsing time per dependent
         * [2] Microsoft C++ Build Insights: 12-40% PCH improvement (similar for PIMPL)
         *
         * Model:
         * - Savings = frontend_time * reduction_ratio * log(dependents)
         * - reduction_ratio = 0.25 (25%) based on empirical data
         */
        Duration estimate_savings(
            const Duration frontend_time,
            const std::size_t dependent_files
        ) {
            // PIMPL typically reduces header parsing time by 20-30%
            constexpr double compile_time_reduction = 0.25;

            const auto frontend_ns = frontend_time.count();
            const auto savings_per_dependent = static_cast<Duration::rep>(
                static_cast<double>(frontend_ns) * compile_time_reduction
            );

            // More dependents = more aggregate savings
            // But diminishing returns after many dependents
            const double scaling_factor = std::log(static_cast<double>(dependent_files) + 1.0);

            return Duration(static_cast<Duration::rep>(
                static_cast<double>(savings_per_dependent) * scaling_factor
            ));
        }

    }  // namespace

    Result<SuggestionResult, Error> PIMPLSuggester::suggest(
        const SuggestionContext& context
    ) const {
        SuggestionResult result;
        auto start_time = std::chrono::steady_clock::now();

        const auto& files = context.analysis.files;
        const auto& headers = context.analysis.dependencies.headers;

        // Build a map of header -> files that include it
        std::unordered_map<std::string, std::unordered_set<std::string>> header_dependents;
        for (const auto& header : headers) {
            std::string header_path = header.path.string();
            for (const auto& includer : header.included_by) {
                header_dependents[header_path].insert(includer.string());
            }
        }

        constexpr auto min_compile_time = std::chrono::milliseconds(500);

        std::size_t analyzed = 0;
        std::size_t skipped = 0;

        for (const auto& file : files) {
            if (context.is_cancelled()) {
                break;
            }
            ++analyzed;

            if (!context.should_analyze(file.file)) {
                ++skipped;
                continue;
            }

            if (!is_source_file(file.file)) {
                ++skipped;
                continue;
            }

            if (file.compile_time < min_compile_time) {
                ++skipped;
                continue;
            }

            // Check if already using PIMPL pattern
            std::string filename = file.file.filename().string();
            std::string lower_filename;
            lower_filename.reserve(filename.size());
            for (char c : filename) {
                lower_filename += static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            std::string lower_stem = file.file.stem().string();
            std::ranges::transform(
                lower_stem,
                lower_stem.begin(),
                [](const unsigned char c) { return static_cast<char>(std::tolower(c)); }
            );

            if (lower_stem.find("_impl") != std::string::npos ||
                lower_stem.starts_with("impl_") ||
                lower_stem.find("_pimpl") != std::string::npos ||
                lower_filename.find("_p.") != std::string::npos ||    // Qt-style
                lower_stem.find("private") != std::string::npos) {
                ++skipped;
                continue;
                }

            // Find corresponding header and its dependents
            auto possible_headers = get_possible_headers(file.file);
            fs::path header_path;
            fs::path existing_header_path;
            std::size_t dependent_count = 0;

            for (const auto& h : possible_headers) {
                fs::path candidate_path = h;
                if (candidate_path.is_relative() && !context.project_root.empty()) {
                    candidate_path = (context.project_root / candidate_path).lexically_normal();
                }

                if (existing_header_path.empty() && fs::exists(candidate_path)) {
                    existing_header_path = candidate_path;
                }
                if (std::string h_str = h.string(); header_dependents.contains(h_str)) {
                    header_path = candidate_path;
                    dependent_count = header_dependents[h_str].size();
                    break;
                }

                if (std::string absolute_h_str = candidate_path.string();
                    header_dependents.contains(absolute_h_str)) {
                    header_path = candidate_path;
                    dependent_count = header_dependents[absolute_h_str].size();
                    break;
                }

                if (header_dependents.contains(h.filename().string()) ||
                    header_dependents.contains(candidate_path.filename().string())) {
                    header_path = candidate_path;
                    const std::string key = header_dependents.contains(h.filename().string())
                        ? h.filename().string()
                        : candidate_path.filename().string();
                    dependent_count = header_dependents[key].size();
                    break;
                }
            }

            if (existing_header_path.empty() && !context.project_root.empty()) {
                static const std::array<std::string_view, 3> include_roots = {"include", "header", "headers"};
                static const std::array<std::string_view, 5> header_exts = {".h", ".hpp", ".hxx", ".H", ".hh"};
                const std::string stem = file.file.stem().string();

                for (const auto root_name : include_roots) {
                    const fs::path include_root = context.project_root / root_name;
                    if (!fs::exists(include_root) || !fs::is_directory(include_root)) {
                        continue;
                    }

                    for (const auto ext : header_exts) {
                        const fs::path direct_candidate = include_root / (stem + std::string(ext));
                        if (fs::exists(direct_candidate)) {
                            existing_header_path = direct_candidate.lexically_normal();
                            break;
                        }
                    }
                    if (!existing_header_path.empty()) {
                        break;
                    }

                    std::error_code iter_error;
                    for (fs::recursive_directory_iterator it(include_root, iter_error), end; it != end && !iter_error; it.increment(iter_error)) {
                        if (!it->is_regular_file()) {
                            continue;
                        }
                        const auto candidate_filename = it->path().filename().string();
                        const bool matches = std::ranges::any_of(
                            header_exts,
                            [&stem, &candidate_filename](const std::string_view ext) {
                                return candidate_filename == stem + std::string(ext);
                            }
                        );
                        if (matches) {
                            existing_header_path = it->path().lexically_normal();
                            break;
                        }
                    }
                    if (!existing_header_path.empty()) {
                        break;
                    }
                }
            }

            if (header_path.empty()) {
                if (!existing_header_path.empty()) {
                    header_path = existing_header_path;
                } else {
                    header_path = file.file;
                    header_path.replace_extension(".h");
                }
            }

            // Count includes for this source file
            std::size_t total_includes = file.include_count;

            // Also count from dependency headers (for backward compatibility with tests)
            std::string source_filename = file.file.filename().string();
            for (const auto& header : headers) {
                for (const auto& includer : header.included_by) {
                    if (fs::path(includer).filename().string() == source_filename) {
                        ++total_includes;
                        break;
                    }
                }
            }

            if (constexpr std::size_t min_include_count = 3; total_includes < min_include_count) {
                ++skipped;
                continue;
            }

            PIMPLCandidate candidate;
            candidate.source_file = file.file;
            candidate.header_file = header_path;
            candidate.compile_time = file.compile_time;
            candidate.frontend_time = file.frontend_time;
            candidate.backend_time = file.backend_time;
            candidate.direct_includes = total_includes;
            candidate.template_instantiations = file.template_count;
            candidate.dependent_files = dependent_count;

            candidate.complexity_score = calculate_complexity_score(
                candidate.frontend_time,
                candidate.direct_includes,
                candidate.template_instantiations
            );

            candidate.impact_score = calculate_impact_score(
                candidate.compile_time,
                candidate.dependent_files,
                candidate.direct_includes  // Using as proxy for transitive
            );

            candidate.confidence = calculate_confidence(
                candidate.frontend_time,
                candidate.backend_time,
                candidate.compile_time,
                candidate.direct_includes
            );

            candidate.priority = calculate_priority(
                candidate.compile_time,
                candidate.direct_includes
            );

            // Skip low-confidence suggestions
            if (candidate.confidence < 0.4 && candidate.priority == Priority::Low) {
                ++skipped;
                continue;
            }

            std::optional<ClassInfo> class_info;
            PIMPLRefactorReadiness readiness;
            if (const auto resolved_target = resolve_pimpl_target(context, file.file, header_path)) {
                readiness = resolved_target->readiness;
                class_info = resolved_target->class_info;
            } else {
                if (context.options.compile_commands_path.has_value() || fs::exists(header_path)) {
                    ++skipped;
                    continue;
                }
                std::string fallback_class_name = file.file.stem().string();
                if (!fallback_class_name.empty()) {
                    fallback_class_name[0] = static_cast<char>(
                        std::toupper(static_cast<unsigned char>(fallback_class_name[0]))
                    );
                }
                readiness = assess_pimpl_readiness(context, file.file, header_path, fallback_class_name);
            }

            Suggestion suggestion;
            suggestion.id = generate_suggestion_id("pimpl", file.file);
            suggestion.type = SuggestionType::PIMPLPattern;
            suggestion.priority = candidate.priority;
            suggestion.confidence = candidate.confidence;
            if (!readiness.has_compile_context) {
                suggestion.confidence = std::min(suggestion.confidence, 0.6);
            }
            if (readiness.has_blockers()) {
                suggestion.confidence = std::min(suggestion.confidence, 0.55);
            }

            std::ostringstream title;
            title << "Consider PIMPL pattern for " << file.file.filename().string();
            suggestion.title = title.str();

            auto compile_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                file.compile_time).count();
            auto frontend_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                file.frontend_time).count();

            std::ostringstream desc;
            desc << "File '" << file.file.string() << "' takes "
                 << compile_ms << "ms to compile";
            if (frontend_ms > 0) {
                desc << " (" << frontend_ms << "ms frontend)";
            }
            desc << " and has " << total_includes << " direct includes";
            if (dependent_count > 0) {
                desc << ". Its header is included by " << dependent_count << " other files";
            }
            desc << ". The PIMPL idiom could reduce compile-time coupling and "
                 << "improve incremental build times.";
            suggestion.description = desc.str();

            suggestion.rationale = "The PIMPL (Pointer to Implementation) pattern "
                "hides class implementation details behind an opaque pointer. "
                "Benefits include:\n"
                "1. Reduced compile-time dependencies - changes to private members "
                "don't trigger recompilation of dependents\n"
                "2. Faster incremental builds - header changes don't cascade\n"
                "3. Binary compatibility - implementation changes don't break ABI\n"
                "4. Reduced header pollution - heavy includes move to .cpp\n\n"
                "This file has a high frontend-to-total compile time ratio, "
                "indicating significant time spent on parsing and template "
                "instantiation that PIMPL can help reduce.";

            // Estimate savings
            // Use frontend_time if available, otherwise use a portion of compile_time
            Duration time_for_savings = candidate.frontend_time;
            if (time_for_savings.count() == 0) {
                // Assume frontend is about 60% of compile time if not specified
                time_for_savings = candidate.compile_time * 6 / 10;
            }
            suggestion.estimated_savings = estimate_savings(
                time_for_savings,
                std::max(candidate.dependent_files, static_cast<std::size_t>(1))
            );

            if (context.trace.total_time.count() > 0) {
                suggestion.estimated_savings_percent =
                    100.0 * static_cast<double>(suggestion.estimated_savings.count()) /
                    static_cast<double>(context.trace.total_time.count());
            }

            suggestion.target_file.path = file.file;
            suggestion.target_file.action = FileAction::Modify;
            suggestion.target_file.note = "Review this class before applying a PIMPL refactor";
            if (class_info) {
                suggestion.refactor_class_name = class_info->name;
                if (context.options.compile_commands_path) {
                    suggestion.refactor_compile_commands_path = context.options.compile_commands_path;
                }
                suggestion.target_file.line_start = class_info->class_start_line;
                suggestion.target_file.line_end = class_info->class_end_line;
                suggestion.target_file.note = "Review the class definition before applying a PIMPL refactor";
            }

            // Add secondary file for header
            FileTarget header_target;
            header_target.path = header_path;
            header_target.action = FileAction::Modify;
            header_target.note = "Review header layout before introducing std::unique_ptr<Impl>";
            if (class_info) {
                header_target.line_start = class_info->class_start_line;
                header_target.line_end = class_info->class_end_line;
            }
            suggestion.secondary_files.push_back(header_target);

            // Keep the "before" snippet for context. The "after" preview is a
            // prototype unless the strict direct-edit subset can generate
            // compile-validated edits for this class shape.
            std::ostringstream before;
            before << "// " << header_path.filename().string() << "\n"
                   << "#pragma once\n"
                   << "#include <heavy_dependency.h>\n"
                   << "#include <another_heavy_dep.h>\n\n"
                   << "class MyClass {\n"
                   << "public:\n"
                   << "    void do_something();\n\n"
                   << "private:\n"
                   << "    HeavyDep member1_;\n"
                   << "    AnotherDep member2_;\n"
                   << "};";
            suggestion.before_code.file = header_path;
            suggestion.before_code.code = before.str();
            if (class_info) {
                suggestion.after_code.file = header_path;
                suggestion.after_code.code = build_pimpl_prototype_preview(*class_info, header_path, file.file);
                if (readiness.is_strict_candidate()) {
                    if (auto strict_edits = generate_strict_pimpl_refactor_edits(*class_info, header_path, file.file)) {
                        suggestion.edits = std::move(*strict_edits);
                    }
                }
            }

            suggestion.implementation_steps = {
                "Review the class API and confirm it is a good PIMPL candidate",
                "Declare an out-of-line destructor before introducing std::unique_ptr<Impl>",
                "Move private data members into an Impl type defined in the source file",
                "Update member functions to access private state via the implementation object",
                "Preserve or explicitly redefine copy and move semantics as needed",
                "Rebuild and verify all dependent files compile correctly"
            };

            suggestion.impact.total_files_affected = dependent_count + 1;
            suggestion.impact.cumulative_savings = suggestion.estimated_savings;
            suggestion.impact.rebuild_files_count = 1;  // Only this .cpp needs rebuild

            suggestion.caveats = {
                "Adds heap allocation (minor memory and CPU overhead)",
                "std::unique_ptr<Impl> requires the implementation type to be complete where destruction happens",
                "Class becomes non-copyable by default (implement or explicitly delete copy operations as needed)",
                "Debugging requires stepping into Impl (use debugger pretty-printers)",
                "Automatic refactoring is only enabled for a narrow, compile-validated subset of classes",
                "Not suitable for header-only libraries",
                "Performance-critical inner loops may prefer direct access"
            };
            StrictPimplEligibility strict_eligibility;
            if (class_info) {
                strict_eligibility = analyze_strict_pimpl_eligibility(
                    *class_info,
                    header_path,
                    file.file
                );
            }
            bool has_explicit_copy_definition = strict_eligibility.has_explicit_copy_definition;
            if (readiness.has_compile_context && class_info) {
                if (auto semantic_copy_definition =
                        detect_explicit_copy_definition_from_compile_db_ast(context, file.file, class_info->name)) {
                    has_explicit_copy_definition = *semantic_copy_definition;
                }
            }
            const auto eligibility = to_pimpl_eligibility_state(readiness, has_explicit_copy_definition);
            for (const auto& caveat : bha::refactor::describe_pimpl_advisory_conditions(eligibility)) {
                suggestion.caveats.push_back(caveat);
            }
            if (readiness.is_strict_candidate()) {
                if (suggestion.edits.empty()) {
                    suggestion.caveats.push_back(
                        "This class is close to the strict automation subset, but it still misses explicit deleted copy operations or simple single-line out-of-line ctor/dtor definitions"
                    );
                } else {
                    suggestion.caveats.push_back(
                        "This class matches the current strict compile-validated automatic refactor subset"
                    );
                }
            }

            suggestion.verification =
                "1. Rebuild the project and verify compilation succeeds\n"
                "2. Run the test suite to verify functionality\n"
                "3. Measure incremental build time after changing a private member\n"
                "4. Profile runtime performance if this is a hot code path";

            const bool supports_external_refactor =
                class_info.has_value() && bha::refactor::supports_pimpl_external_refactor(eligibility);

            suggestion.is_safe = !suggestion.edits.empty();
            if (suggestion.is_safe) {
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
            } else if (supports_external_refactor) {
                suggestion.application_mode = SuggestionApplicationMode::ExternalRefactor;
                suggestion.target_file.note = "Semantic refactor tool required: this class is eligible for an AST-driven PIMPL rewrite";
                if (!suggestion.secondary_files.empty()) {
                    suggestion.secondary_files.front().note =
                        "Semantic refactor tool required: preserve the public API while moving private state behind Impl";
                }
            } else {
                suggestion.application_mode = SuggestionApplicationMode::Advisory;
            }

            result.suggestions.push_back(std::move(suggestion));
        }

        result.items_analyzed = analyzed;
        result.items_skipped = skipped;

        std::ranges::sort(result.suggestions,
                          [](const Suggestion& a, const Suggestion& b) {
                              return a.estimated_savings > b.estimated_savings;
                          });

        auto end_time = std::chrono::steady_clock::now();
        result.generation_time = std::chrono::duration_cast<Duration>(end_time - start_time);

        return Result<SuggestionResult, Error>::success(std::move(result));
    }

    Result<std::vector<TextEdit>, Error> generate_pimpl_refactor_edits(
        const fs::path& compile_commands_path,
        const fs::path& source_file,
        const fs::path& header_file,
        std::string_view class_name
    ) {
        if (compile_commands_path.empty()) {
            return Result<std::vector<TextEdit>, Error>::failure(
                Error::invalid_argument("compile_commands.json path is required")
            );
        }
        if (source_file.empty() || header_file.empty() || class_name.empty()) {
            return Result<std::vector<TextEdit>, Error>::failure(
                Error::invalid_argument("source, header, and class name are required")
            );
        }

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = compile_commands_path;
        const SuggestionContext context{
            .trace = trace,
            .analysis = analysis,
            .options = options,
            .project_root = header_file.parent_path()
        };

        const std::string class_name_str(class_name);
        std::optional<ClassInfo> class_info;
        if (auto extraction = extract_class_info_from_compile_db_ast(
                context, source_file, header_file, class_name_str)) {
            class_info = extraction->info;
        }
        if (!class_info) {
            class_info = parse_class_simple(header_file, class_name_str);
        }
        if (!class_info) {
            return Result<std::vector<TextEdit>, Error>::failure(
                Error(ErrorCode::ParseError, "Failed to locate the requested class in the header")
            );
        }

        auto edits = generate_strict_pimpl_refactor_edits(*class_info, header_file, source_file);
        if (!edits || edits->empty()) {
            return Result<std::vector<TextEdit>, Error>::failure(
                Error(ErrorCode::AnalysisError, "No compile-validated PIMPL edits could be generated")
            );
        }

        return Result<std::vector<TextEdit>, Error>::success(std::move(*edits));
    }

    void register_pimpl_pattern_suggester() {
        SuggesterRegistry::instance().register_suggester(
            std::make_unique<PIMPLSuggester>()
        );
    }
}  // namespace bha::suggestions
