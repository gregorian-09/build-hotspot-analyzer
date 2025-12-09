//
// Created by gregorian on 21/10/2025.
//

#include "bha/build_systems/cmake_adapter.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/json_utils.h"
#include <regex>

namespace bha::build_systems {

    CMakeAdapter::CMakeAdapter(const std::string& build_dir)
    : compile_commands_path_(std::filesystem::path(build_dir) / "compile_commands.json"),
      cmake_cache_path_(std::filesystem::path(build_dir) / "CMakeCache.txt"),
      build_dir_(build_dir)
    {}

    core::Result<BuildSystemInfo> CMakeAdapter::detect_build_system(
        const std::string& build_dir
    ) {
        BuildSystemInfo info;
        info.type = BuildSystemType::CMAKE;
        info.build_directory = build_dir;

        if (auto version_result = get_cmake_version(); version_result.is_success()) {
            info.version = version_result.value();
        }

        if (auto source_dir_result = read_cache_variable("CMAKE_HOME_DIRECTORY"); source_dir_result.is_success()) {
            info.source_directory = source_dir_result.value();
        }

        return core::Result<BuildSystemInfo>::success(std::move(info));
    }

    core::Result<std::vector<CompileCommand>> CMakeAdapter::extract_compile_commands()
    {
        if (!has_compile_commands_json()) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::Error{
                    core::ErrorCode::FILE_NOT_FOUND,
                    "compile_commands.json not found. "
                              "Enable CMAKE_EXPORT_COMPILE_COMMANDS in CMake."
                }
            );
        }

        auto content_opt = utils::read_file(compile_commands_path_.string());
        if (!content_opt) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::Error{
                    core::ErrorCode::FILE_NOT_FOUND,
                    "Could not read compile_commands.json"
                }
            );
        }

        utils::JsonDocument doc;
        if (!doc.parse(*content_opt)) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::Error{
                    core::ErrorCode::PARSE_ERROR,
                    "Failed to parse compile_commands.json"
                }
            );
        }

        if (!doc.is_array()) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::Error{
                    core::ErrorCode::PARSE_ERROR,
                    "compile_commands.json must be an array"
                }
            );
        }

        std::vector<CompileCommand> commands;

        try {
            auto& json_doc = doc.get_document();

            for (auto array = json_doc.get_array(); auto entry : array) {
                CompileCommand cmd;
                auto obj = entry.get_object();

                if (auto dir = obj.find_field("directory"); !dir.error()) {
                    cmd.directory = std::string(dir.get_string().value());
                }
                if (auto file = obj.find_field("file"); !file.error()) {
                    cmd.file = std::string(file.get_string().value());
                }
                if (auto command = obj.find_field("command"); !command.error()) {
                    cmd.command = std::string(command.get_string().value());
                }
                if (auto args = obj.find_field("arguments"); !args.error()) {
                    for (auto arg : args.get_array()) {
                        cmd.arguments.emplace_back(arg.get_string().value());
                    }
                }
                if (auto output = obj.find_field("output"); !output.error()) {
                    cmd.output = std::string(output.get_string().value());
                }

                commands.push_back(std::move(cmd));
            }
        } catch (const simdjson::simdjson_error& e) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::Error{
                    core::ErrorCode::PARSE_ERROR,
                    "Error parsing compile_commands.json: " + std::string(e.what())
                }
            );
        }

        return core::Result<std::vector<CompileCommand>>::success(std::move(commands));
    }

    core::Result<std::vector<std::string>> CMakeAdapter::get_trace_files(
        const std::string& build_dir
    ) {
        std::vector<std::string> trace_files;
        namespace fs = std::filesystem;

        for (const auto& entry : fs::recursive_directory_iterator(build_dir)) {
            if (entry.is_regular_file()) {
                if (auto ext = entry.path().extension(); ext == ".json" && entry.path().stem().string().find("time-trace") != std::string::npos) {
                    trace_files.push_back(entry.path().string());
                }
            }
        }

        return core::Result<std::vector<std::string>>::success(std::move(trace_files));
    }

    core::Result<std::map<std::string, std::vector<std::string>>> CMakeAdapter::get_targets()
    {
        std::map<std::string, std::vector<std::string>> targets;
        namespace fs = std::filesystem;
        const fs::path targets_file = compile_commands_path_.parent_path() / "CMakeFiles" / "TargetDirectories.txt";

        if (!utils::file_exists(targets_file.string())) {
            return core::Result<std::map<std::string, std::vector<std::string>>>::success(
                std::move(targets)
            );
        }

        const auto lines_opt = utils::read_lines(targets_file.string());
        if (!lines_opt) {
            return core::Result<std::map<std::string, std::vector<std::string>>>::success(
                std::move(targets)
            );
        }

        for (const auto& line : *lines_opt) {
            if (!line.empty() && line[0] != '#') {
                targets[line] = {};
            }
        }

        return core::Result<std::map<std::string, std::vector<std::string>>>::success(
            std::move(targets)
        );
    }

    core::Result<std::vector<std::string>> CMakeAdapter::get_build_order()
    {
        std::vector<std::string> build_order;

        auto commands_result = extract_compile_commands();
        if (!commands_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(
                commands_result.error()
            );
        }

        for (const auto& cmd : commands_result.value()) {
            build_order.push_back(cmd.file);
        }

        return core::Result<std::vector<std::string>>::success(std::move(build_order));
    }

    core::Result<bool> CMakeAdapter::enable_tracing(
        const std::string& build_dir,
        const std::string& compiler_type
    ) {
        std::string flag;
        if (compiler_type == "clang") {
            flag = "-ftime-trace";
        } else if (compiler_type == "gcc") {
            flag = "-ftime-report";
        } else if (compiler_type == "msvc") {
            flag = "/Bt+ /d1reportTime";
        } else {
            return core::Result<bool>::failure(
                core::ErrorCode::UNSUPPORTED_FORMAT,
                "Unsupported compiler type: " + compiler_type
            );
        }

        const std::filesystem::path cmake_cache = std::filesystem::path(build_dir) / "CMakeCache.txt";

        if (!utils::file_exists(cmake_cache.string())) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "CMakeCache.txt not found in build directory"
            );
        }

        auto lines_opt = utils::read_lines(cmake_cache.string());
        if (!lines_opt) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read CMakeCache.txt"
            );
        }

        std::vector<std::string> updated_lines;
        bool found_cxx_flags = false;
        bool found_c_flags = false;

        for (auto& line : *lines_opt) {
            if (line.find("CMAKE_CXX_FLAGS:STRING=") != std::string::npos) {
                if (line.find(flag) == std::string::npos) {
                    line += " " + flag;
                }
                found_cxx_flags = true;
            } else if (line.find("CMAKE_C_FLAGS:STRING=") != std::string::npos) {
                if (line.find(flag) == std::string::npos) {
                    line += " " + flag;
                }
                found_c_flags = true;
            }
            updated_lines.push_back(line);
        }

        if (!found_cxx_flags) {
            updated_lines.push_back("CMAKE_CXX_FLAGS:STRING=" + flag);
        }
        if (!found_c_flags) {
            updated_lines.push_back("CMAKE_C_FLAGS:STRING=" + flag);
        }

        if (!utils::write_lines(cmake_cache.string(), updated_lines)) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not write updated CMakeCache.txt"
            );
        }

        return core::Result<bool>::success(true);
    }

    core::Result<std::string> CMakeAdapter::get_cmake_version() const
    {
        if (!utils::file_exists(cmake_cache_path_.string())) {
            return core::Result<std::string>::failure(
                core::Error{
                    core::ErrorCode::FILE_NOT_FOUND,
                    "CMakeCache.txt not found"
                }
            );
        }

        return read_cache_variable("CMAKE_VERSION");
    }

    core::Result<std::string> CMakeAdapter::read_cache_variable(
        const std::string& var_name
    ) const
    {
        const auto lines_opt = utils::read_lines(cmake_cache_path_.string());
        if (!lines_opt) {
            return core::Result<std::string>::failure(
                core::Error{
                    core::ErrorCode::FILE_NOT_FOUND,
                    "Could not read CMakeCache.txt"
                }
            );
        }

        const std::regex pattern(var_name + R"(:.*=(.+))");

        for (const auto& line : *lines_opt) {
            if (std::smatch match; std::regex_search(line, match, pattern)) {
                return core::Result<std::string>::success(match[1].str());
            }
        }

        return core::Result<std::string>::failure(
            core::Error{
                core::ErrorCode::PARSE_ERROR,
                "Variable not found in CMakeCache.txt: " + var_name
            }
        );
    }

    bool CMakeAdapter::has_compile_commands_json() const {
        return utils::file_exists(compile_commands_path_.string());
    }
}