#include "bha/suggestions/pch_build_planner.hpp"
#include "bha/suggestions/suggester.hpp"
#include "bha/utils/cmake_parse_utils.hpp"

#include <fstream>
#include <cctype>
#include <regex>
#include <sstream>

namespace bha::suggestions {
    namespace {

        std::optional<std::size_t> find_cmake_block_end(
            const std::string& content,
            const std::size_t start_line
        ) {
            std::istringstream input(content);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(input, line)) {
                lines.push_back(line);
            }
            if (start_line >= lines.size()) {
                return std::nullopt;
            }

            int paren_depth = 0;
            bool seen_open = false;
            for (std::size_t i = start_line; i < lines.size(); ++i) {
                const int delta = utils::count_paren_delta_outside_quotes(lines[i]);
                if (delta > 0) {
                    seen_open = true;
                }
                paren_depth += delta;
                if (seen_open && paren_depth <= 0) {
                    return i;
                }
            }
            return std::nullopt;
        }

        bool has_cmake_pch_for_target(const std::string& content, const std::string& target) {
            std::istringstream input(content);
            std::string line;
            while (std::getline(input, line)) {
                std::string trimmed = line;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (trimmed.rfind("target_precompile_headers", 0) != 0) {
                    continue;
                }

                std::string command = trimmed;
                int paren_balance = 0;
                for (const char ch : trimmed) {
                    if (ch == '(') {
                        ++paren_balance;
                    } else if (ch == ')') {
                        --paren_balance;
                    }
                }
                while (paren_balance > 0 && std::getline(input, line)) {
                    command += '\n';
                    command += line;
                    for (const char ch : line) {
                        if (ch == '(') {
                            ++paren_balance;
                        } else if (ch == ')') {
                            --paren_balance;
                        }
                    }
                }

                const auto open = command.find('(');
                if (open == std::string::npos) {
                    continue;
                }
                std::size_t pos = open + 1;
                while (pos < command.size() && std::isspace(static_cast<unsigned char>(command[pos]))) {
                    ++pos;
                }
                const auto end = command.find_first_of(" \t\r\n)", pos);
                const std::string parsed = command.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                if (parsed == target) {
                    return true;
                }
            }
            return false;
        }

        std::string relative_pch_path(const fs::path& pch_path, const fs::path& base) {
            fs::path relative = pch_path;
            std::error_code ec;
            if (relative.is_absolute()) {
                const auto candidate = fs::relative(pch_path, base, ec);
                if (!ec && !candidate.empty()) {
                    relative = candidate;
                }
            }
            return relative.generic_string();
        }

    }  // namespace

    PCHBuildPlan plan_cmake_pch_edits(const CMakePCHTargetPlan& plan) {
        PCHBuildPlan result;
        if (plan.file.empty() || plan.target_name.empty() || plan.pch_path.empty() ||
            has_cmake_pch_for_target(plan.content, plan.target_name)) {
            return result;
        }

        std::size_t insert_line = plan.target_start_line;
        if (auto end_line = find_cmake_block_end(plan.content, plan.target_start_line)) {
            insert_line = *end_line;
        }

        std::ostringstream cmake_line;
        cmake_line << "target_precompile_headers(" << plan.target_name
                   << " PRIVATE \"" << relative_pch_path(plan.pch_path, plan.file.parent_path()) << "\")";
        result.edits.push_back(make_insert_after_line_edit(plan.file, insert_line, cmake_line.str()));

        FileTarget target;
        target.path = plan.file;
        target.action = FileAction::Modify;
        target.line_start = insert_line + 2;
        target.line_end = insert_line + 2;
        target.note = "Add target_precompile_headers for PCH";
        result.files.push_back(std::move(target));
        return result;
    }

    PCHBuildPlan plan_msbuild_pch_edits(
        const fs::path& project_root,
        const fs::path& pch_path,
        const std::function<bool()>& should_cancel
    ) {
        PCHBuildPlan result;
        if (project_root.empty() || pch_path.empty() || !fs::exists(project_root)) {
            return result;
        }

        for (const auto& entry : fs::directory_iterator(project_root)) {
            if (should_cancel && should_cancel()) {
                break;
            }
            if (!entry.is_regular_file() || entry.path().extension() != ".vcxproj") {
                continue;
            }

            std::ifstream input(entry.path());
            const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (content.find("PrecompiledHeaderFile") != std::string::npos) {
                continue;
            }

            const std::string relative = relative_pch_path(pch_path, entry.path().parent_path());
            const std::string block =
                "  <ItemDefinitionGroup>\n"
                "    <ClCompile>\n"
                "      <PrecompiledHeader>Use</PrecompiledHeader>\n"
                "      <PrecompiledHeaderFile>" + relative + "</PrecompiledHeaderFile>\n"
                "    </ClCompile>\n"
                "  </ItemDefinitionGroup>\n"
                "  <ItemGroup>\n"
                "    <ClCompile Include=\"pch.cpp\">\n"
                "      <PrecompiledHeader>Create</PrecompiledHeader>\n"
                "    </ClCompile>\n"
                "  </ItemGroup>\n";

            std::size_t insert_line = end_of_file_insert_line(content);
            std::istringstream lines(content);
            std::string line;
            std::size_t line_num = 0;
            while (std::getline(lines, line)) {
                if (std::regex_match(line, std::regex(R"(^\s*</Project>)"))) {
                    insert_line = line_num;
                    break;
                }
                ++line_num;
            }

            TextEdit project_edit;
            project_edit.file = entry.path();
            project_edit.start_line = insert_line;
            project_edit.end_line = insert_line;
            project_edit.new_text = "\n" + block;
            result.edits.push_back(std::move(project_edit));

            FileTarget project_target;
            project_target.path = entry.path();
            project_target.action = FileAction::Modify;
            project_target.line_start = insert_line + 1;
            project_target.line_end = insert_line + 8;
            project_target.note = "Enable PCH in MSBuild project";
            result.files.push_back(std::move(project_target));

            const fs::path pch_cpp_path = entry.path().parent_path() / "pch.cpp";
            if (!fs::exists(pch_cpp_path)) {
                TextEdit source_edit;
                source_edit.file = pch_cpp_path;
                source_edit.end_line = 0;
                source_edit.new_text = "#include \"" + relative + "\"\n";
                result.edits.push_back(std::move(source_edit));

                FileTarget source_target;
                source_target.path = pch_cpp_path;
                source_target.action = FileAction::Create;
                source_target.note = "Create PCH source file for MSBuild";
                result.files.push_back(std::move(source_target));
            }
        }
        return result;
    }
}  // namespace bha::suggestions
