//
// Created by gregorian on 21/10/2025.
//

#include "bha/build_systems/make_adapter.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include <regex>

namespace bha::build_systems {

    MakeAdapter::MakeAdapter(const std::string& build_dir)
        : makefile_path_(std::filesystem::path(build_dir) / "Makefile"),
          make_log_path_(std::filesystem::path(build_dir) / "make.log")
    {
        build_dir_ = build_dir;
    }

    core::Result<BuildSystemInfo> MakeAdapter::detect_build_system(
        const std::string& build_dir
    ) {
        BuildSystemInfo info;
        info.type = BuildSystemType::MAKE;
        info.build_directory = build_dir;

        if (auto version_result = get_make_version(); version_result.is_success()) {
            info.version = version_result.value();
        }

        info.source_directory = build_dir;

        return core::Result<BuildSystemInfo>::success(std::move(info));
    }

    core::Result<std::vector<CompileCommand>> MakeAdapter::extract_compile_commands(
        const std::string& build_dir
    ) {
        auto dry_run_result = run_make_dry_run();
        if (!dry_run_result.is_success()) {
            return core::Result<std::vector<CompileCommand>>::failure(
                dry_run_result.error()
            );
        }

        auto commands = extract_compile_commands_from_output(dry_run_result.value());

        std::vector<CompileCommand> compile_commands;
        for (const auto& cmd_str : commands) {
            CompileCommand cmd;
            cmd.directory = build_dir;
            cmd.command = cmd_str;

            for (auto parts = utils::split(cmd_str, ' '); const auto& part : parts) {
                if (utils::ends_with(part, ".c") || utils::ends_with(part, ".cpp") ||
                    utils::ends_with(part, ".cc") || utils::ends_with(part, ".cxx")) {
                    cmd.file = part;
                    break;
                }
            }

            compile_commands.push_back(std::move(cmd));
        }

        return core::Result<std::vector<CompileCommand>>::success(std::move(compile_commands));
    }

    core::Result<std::vector<std::string>> MakeAdapter::get_trace_files(
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

        if (utils::file_exists(make_log_path_.string())) {
            trace_files.push_back(make_log_path_.string());
        }

        return core::Result<std::vector<std::string>>::success(std::move(trace_files));
    }

    core::Result<std::map<std::string, std::vector<std::string>>> MakeAdapter::get_targets(
        const std::string& build_dir
    ) {
        std::map<std::string, std::vector<std::string>> targets;

        auto makefile_result = parse_makefile(makefile_path_.string());
        if (!makefile_result.is_success()) {
            return core::Result<std::map<std::string, std::vector<std::string>>>::success(
                std::move(targets)
            );
        }

        for (const auto& target : makefile_result.value()) {
            targets[target.name] = target.dependencies;
        }

        return core::Result<std::map<std::string, std::vector<std::string>>>::success(
            std::move(targets)
        );
    }

    core::Result<std::vector<std::string>> MakeAdapter::get_build_order(
        const std::string& build_dir
    ) {
        std::vector<std::string> build_order;

        auto makefile_result = parse_makefile(makefile_path_.string());
        if (!makefile_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(
                makefile_result.error()
            );
        }

        for (const auto& target : makefile_result.value()) {
            build_order.push_back(target.name);
        }

        return core::Result<std::vector<std::string>>::success(std::move(build_order));
    }

    core::Result<bool> MakeAdapter::enable_tracing(
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

        const std::filesystem::path makefile_path = std::filesystem::path(build_dir) / "Makefile";

        if (!utils::file_exists(makefile_path.string())) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Makefile not found in build directory"
            );
        }

        auto lines_opt = utils::read_lines(makefile_path.string());
        if (!lines_opt) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read Makefile"
            );
        }

        std::vector<std::string> updated_lines;
        bool found_cxxflags = false;
        bool found_cflags = false;

        for (auto& line : *lines_opt) {
            if (line.find("CXXFLAGS") != std::string::npos &&
                (line.find("=") != std::string::npos || line.find(":=") != std::string::npos)) {
                if (line.find(flag) == std::string::npos) {
                    line += " " + flag;
                }
                found_cxxflags = true;
            }

            else if (line.find("CFLAGS") != std::string::npos &&
                     (line.find("=") != std::string::npos || line.find(":=") != std::string::npos)) {
                if (line.find(flag) == std::string::npos) {
                    line += " " + flag;
                }
                found_cflags = true;
            }
            updated_lines.push_back(line);
        }

        if (!found_cxxflags) {
            updated_lines.insert(updated_lines.begin(), "CXXFLAGS += " + flag);
        }
        if (!found_cflags) {
            updated_lines.insert(updated_lines.begin(), "CFLAGS += " + flag);
        }

        if (!utils::write_lines(makefile_path.string(), updated_lines)) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not write updated Makefile"
            );
        }

        return core::Result<bool>::success(true);
    }

    core::Result<std::vector<MakeTarget>> MakeAdapter::parse_makefile(
        const std::string& makefile_path
    ) {
        if (!utils::file_exists(makefile_path)) {
            return core::Result<std::vector<MakeTarget>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Makefile not found: " + makefile_path
            );
        }

        const auto lines_opt = utils::read_lines(makefile_path);
        if (!lines_opt) {
            return core::Result<std::vector<MakeTarget>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read Makefile"
            );
        }

        std::vector<MakeTarget> targets;
        MakeTarget current_target;
        bool in_target = false;

        const std::regex target_regex(R"(^([a-zA-Z0-9_\-\.]+)\s*:\s*(.*)$)");

        for (const auto& line : *lines_opt) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            if (std::smatch match; std::regex_match(line, match, target_regex)) {
                if (in_target && !current_target.name.empty()) {
                    targets.push_back(std::move(current_target));
                    current_target = MakeTarget{};
                }

                current_target.name = match[1].str();
                std::string deps_str = match[2].str();

                if (!deps_str.empty()) {
                    for (auto deps = utils::split(deps_str, ' '); const auto& dep : deps) {
                        if (auto trimmed = utils::trim(dep); !trimmed.empty()) {
                            current_target.dependencies.push_back(trimmed);
                        }
                    }
                }

                in_target = true;
            } else if (in_target && line[0] == '\t') {
                current_target.commands.push_back(utils::trim(line));
            } else if (in_target) {
                targets.push_back(std::move(current_target));
                current_target = MakeTarget{};
                in_target = false;
            }
        }

        if (in_target && !current_target.name.empty()) {
            targets.push_back(std::move(current_target));
        }

        return core::Result<std::vector<MakeTarget>>::success(std::move(targets));
    }

    core::Result<std::string> MakeAdapter::get_make_version() {
        return core::Result<std::string>::success("Unknown");
    }

    core::Result<std::string> MakeAdapter::run_make_dry_run() {
        return core::Result<std::string>::success("");
    }

    std::vector<std::string> MakeAdapter::extract_compile_commands_from_output(
        const std::string& make_output
    ) {
        std::vector<std::string> commands;
        const auto lines = utils::split(make_output, '\n');

        const std::regex compile_regex(R"((gcc|g\+\+|clang|clang\+\+|cc|c\+\+)\s+.*)");

        for (const auto& line : lines) {
            if (std::smatch match; std::regex_search(line, match, compile_regex)) {
                commands.push_back(utils::trim(line));
            }
        }

        return commands;
    }
}