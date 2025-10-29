//
// Created by gregorian on 21/10/2025.
//

#include "bha/build_systems/msbuild_adapter.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include <regex>

namespace bha::build_systems {

    MSBuildAdapter::MSBuildAdapter(const std::string& build_dir)
        : solution_path_(std::filesystem::path(build_dir)),
          build_log_path_(std::filesystem::path(build_dir) / "msbuild.log")
    {
        build_dir_ = build_dir;
    }

    core::Result<BuildSystemInfo> MSBuildAdapter::detect_build_system(
        const std::string& build_dir
    ) {
        BuildSystemInfo info;
        info.type = BuildSystemType::MSBUILD;
        info.build_directory = build_dir;

        if (auto version_result = get_msbuild_version(); version_result.is_success()) {
            info.version = version_result.value();
        }

        info.source_directory = build_dir;

        return core::Result<BuildSystemInfo>::success(std::move(info));
    }

    core::Result<std::vector<CompileCommand>> MSBuildAdapter::extract_compile_commands() {
        auto projects_result = find_vcxproj_files();
        if (!projects_result.is_success()) {
            return core::Result<std::vector<CompileCommand>>::failure(
                projects_result.error()
            );
        }

        std::vector<CompileCommand> commands;
        const std::string build_dir = solution_path_.parent_path().string();

        for (const auto& proj_path : projects_result.value()) {
            auto project_result = parse_vcxproj(proj_path);
            if (!project_result.is_success()) {
                continue;
            }

            for (auto& project = project_result.value(); const auto& source_file : project.source_files) {
                CompileCommand cmd;
                cmd.directory = build_dir;
                cmd.file = source_file;
                cmd.command = "cl.exe /c " + source_file;
                commands.push_back(std::move(cmd));
            }
        }

        return core::Result<std::vector<CompileCommand>>::success(std::move(commands));
    }

    core::Result<std::vector<std::string>> MSBuildAdapter::get_trace_files(
        const std::string& build_dir
    ) {
        std::vector<std::string> trace_files;
        namespace fs = std::filesystem;

        for (const auto& entry : fs::recursive_directory_iterator(build_dir)) {
            if (entry.is_regular_file()) {
                if (auto ext = entry.path().extension(); ext == ".etl" || ext == ".json") {
                    trace_files.push_back(entry.path().string());
                }
            }
        }

        if (utils::file_exists(build_log_path_.string())) {
            trace_files.push_back(build_log_path_.string());
        }

        return core::Result<std::vector<std::string>>::success(std::move(trace_files));
    }

    core::Result<std::map<std::string, std::vector<std::string>>> MSBuildAdapter::get_targets()
    {
        std::map<std::string, std::vector<std::string>> targets;

        auto projects_result = find_vcxproj_files();
        if (!projects_result.is_success()) {
            return core::Result<std::map<std::string, std::vector<std::string>>>::success(
                std::move(targets)
            );
        }

        for (const auto& proj_path : projects_result.value()) {
            if (auto project_result = parse_vcxproj(proj_path); project_result.is_success()) {
                auto& project = project_result.value();
                targets[project.name] = {};
            }
        }

        return core::Result<std::map<std::string, std::vector<std::string>>>::success(
            std::move(targets)
        );
    }

    core::Result<std::vector<std::string>> MSBuildAdapter::get_build_order()
    {
        std::vector<std::string> build_order;

        auto projects_result = find_vcxproj_files();
        if (!projects_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(
                projects_result.error()
            );
        }

        for (const auto& proj_path : projects_result.value()) {
            if (auto project_result = parse_vcxproj(proj_path); project_result.is_success()) {
                build_order.push_back(project_result.value().name);
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(build_order));
    }

    core::Result<bool> MSBuildAdapter::enable_tracing(
        const std::string& build_dir,
        const std::string& compiler_type
    ) {
        if (compiler_type != "msvc") {
            return core::Result<bool>::failure(
                core::ErrorCode::UNSUPPORTED_FORMAT,
                "MSBuild only supports MSVC compiler"
            );
        }

        std::vector<std::filesystem::path> project_files;
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".vcxproj") {
                    project_files.push_back(entry.path());
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not iterate build directory: " + std::string(e.what())
            );
        }

        if (project_files.empty()) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "No .vcxproj files found in build directory"
            );
        }

        const std::string flags = "/Bt+ /d1reportTime";

        for (const auto& project_file : project_files) {
            auto lines_opt = utils::read_lines(project_file.string());
            if (!lines_opt) {
                return core::Result<bool>::failure(
                    core::ErrorCode::FILE_NOT_FOUND,
                    "Could not read " + project_file.filename().string()
                );
            }

            std::vector<std::string> updated_lines;
            bool modified = false;
            bool in_clcompile_section = false;

            for (auto& line : *lines_opt) {
                if (line.find("<ItemDefinitionGroup") != std::string::npos) {
                    in_clcompile_section = true;
                }
                else if (in_clcompile_section && line.find("<ClCompile>") != std::string::npos) {
                    updated_lines.push_back(line);
                    updated_lines.push_back("      <AdditionalOptions>" + flags + " %(AdditionalOptions)</AdditionalOptions>");
                    modified = true;
                    continue;
                }
                else if (in_clcompile_section && line.find("<AdditionalOptions>") != std::string::npos) {
                    if (line.find("/Bt+") == std::string::npos) {
                        if (const size_t percent_pos = line.find("%(AdditionalOptions)"); percent_pos != std::string::npos) {
                            line.insert(percent_pos, flags + " ");
                        } else {
                            if (const size_t close_tag = line.find("</AdditionalOptions>"); close_tag != std::string::npos) {
                                line.insert(close_tag, " " + flags);
                            }
                        }
                        modified = true;
                    }
                }
                else if (line.find("</ClCompile>") != std::string::npos) {
                    in_clcompile_section = false;
                }

                updated_lines.push_back(line);
            }

            if (modified) {
                if (!utils::write_lines(project_file.string(), updated_lines)) {
                    return core::Result<bool>::failure(
                        core::ErrorCode::FILE_NOT_FOUND,
                        "Could not write updated " + project_file.filename().string()
                    );
                }
            }
        }

        return core::Result<bool>::success(true);
    }

    core::Result<std::vector<MSBuildProject>> MSBuildAdapter::parse_solution(
        const std::string& solution_path
    ) {
        if (!utils::file_exists(solution_path)) {
            return core::Result<std::vector<MSBuildProject>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Solution file not found: " + solution_path
            );
        }

        const auto lines_opt = utils::read_lines(solution_path);
        if (!lines_opt) {
            return core::Result<std::vector<MSBuildProject>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read solution file"
            );
        }

        std::vector<MSBuildProject> projects;
        const std::regex project_regex(R"lit(Project\("\{[^}]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+)")lit");

        for (const auto& line : *lines_opt) {
            if (std::smatch match; std::regex_search(line, match, project_regex)) {
                MSBuildProject project;
                project.name = match[1].str();
                project.path = match[2].str();
                projects.push_back(std::move(project));
            }
        }

        return core::Result<std::vector<MSBuildProject>>::success(std::move(projects));
    }

    core::Result<std::string> MSBuildAdapter::get_msbuild_version() {
        return core::Result<std::string>::success("Unknown");
    }

    core::Result<std::vector<std::string>> MSBuildAdapter::find_vcxproj_files() const
    {
        std::vector<std::string> vcxproj_files;
        namespace fs = std::filesystem;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(build_dir_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".vcxproj") {
                    vcxproj_files.push_back(entry.path().string());
                }
            }
        } catch (const fs::filesystem_error& e) {
            return core::Result<std::vector<std::string>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Error scanning directory: " + std::string(e.what())
            );
        }

        return core::Result<std::vector<std::string>>::success(std::move(vcxproj_files));
    }

    core::Result<MSBuildProject> MSBuildAdapter::parse_vcxproj(
        const std::string& project_path
    ) {
        if (!utils::file_exists(project_path)) {
            return core::Result<MSBuildProject>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Project file not found: " + project_path
            );
        }

        const auto content_opt = utils::read_file(project_path);
        if (!content_opt) {
            return core::Result<MSBuildProject>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read project file"
            );
        }

        MSBuildProject project;
        project.path = project_path;

        namespace fs = std::filesystem;
        project.name = fs::path(project_path).stem().string();

        const std::regex compile_regex("<ClCompile\\s+Include=\"([^\"]+)\"\\s*/?>");
        const std::regex config_regex(R"(<Configuration>([^<]+)</Configuration>)");
        const std::regex platform_regex(R"(<Platform>([^<]+)</Platform>)");

        std::smatch match;
        auto search_start(content_opt->cbegin());

        while (std::regex_search(search_start, content_opt->cend(), match, compile_regex)) {
            project.source_files.push_back(match[1].str());
            search_start = match.suffix().first;
        }

        if (std::regex_search(*content_opt, match, config_regex)) {
            project.configuration = match[1].str();
        }

        if (std::regex_search(*content_opt, match, platform_regex)) {
            project.platform = match[1].str();
        }

        return core::Result<MSBuildProject>::success(std::move(project));
    }
}