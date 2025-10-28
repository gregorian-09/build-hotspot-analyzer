//
// Created by gregorian on 21/10/2025.
//

#include "bha/build_systems/ninja_adapter.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include <algorithm>
#include "bha/utils/json_utils.h"

namespace bha::build_systems {

    NinjaAdapter::NinjaAdapter(const std::string& build_dir)
        : ninja_log_path_(std::filesystem::path(build_dir) / ".ninja_log"),
          ninja_build_path_(std::filesystem::path(build_dir) / "build.ninja"),
          ninja_deps_path_(std::filesystem::path(build_dir) / ".ninja_deps")
    {
        build_dir_ = build_dir;
    }

    core::Result<BuildSystemInfo> NinjaAdapter::detect_build_system(
        const std::string& build_dir
    ) {
        BuildSystemInfo info;
        info.type = BuildSystemType::NINJA;
        info.build_directory = build_dir;

        if (auto version_result = get_ninja_version(); version_result.is_success()) {
            info.version = version_result.value();
        }

        info.source_directory = build_dir;

        return core::Result<BuildSystemInfo>::success(std::move(info));
    }

    core::Result<std::vector<CompileCommand>> NinjaAdapter::extract_compile_commands(
        const std::string& build_dir
    ) {
        std::filesystem::path compile_commands = std::filesystem::path(build_dir) / "compile_commands.json";

        if (!utils::file_exists(compile_commands.string())) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "compile_commands.json not found. Generate with: ninja -t compdb > compile_commands.json"
            );
        }

        auto content_opt = utils::read_file(compile_commands.string());
        if (!content_opt) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read compile_commands.json"
            );
        }

        utils::JsonDocument doc;
        if (!doc.parse(*content_opt)) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::ErrorCode::PARSE_ERROR,
                "Failed to parse compile_commands.json"
            );
        }

        if (!doc.is_array()) {
            return core::Result<std::vector<CompileCommand>>::failure(
                core::ErrorCode::PARSE_ERROR,
                "compile_commands.json must be an array"
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
                core::ErrorCode::PARSE_ERROR,
                "Error parsing compile_commands.json: " + std::string(e.what())
            );
        }

        return core::Result<std::vector<CompileCommand>>::success(std::move(commands));
    }

    core::Result<std::vector<std::string>> NinjaAdapter::get_trace_files(
        const std::string& build_dir
    ) {
        std::vector<std::string> trace_files;
        namespace fs = std::filesystem;

        for (const auto& entry : fs::recursive_directory_iterator(build_dir)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension();
                if (ext == ".json" && entry.path().stem().string().find("time-trace") != std::string::npos) {
                    trace_files.push_back(entry.path().string());
                }
            }
        }

        if (utils::file_exists(ninja_log_path_.string())) {
            trace_files.push_back(ninja_log_path_.string());
        }

        return core::Result<std::vector<std::string>>::success(std::move(trace_files));
    }

    core::Result<std::map<std::string, std::vector<std::string>>> NinjaAdapter::get_targets(
        const std::string& build_dir
    ) {
        std::map<std::string, std::vector<std::string>> targets;

        if (!utils::file_exists(ninja_build_path_.string())) {
            return core::Result<std::map<std::string, std::vector<std::string>>>::success(
                std::move(targets)
            );
        }

        const auto lines_opt = utils::read_lines(ninja_build_path_.string());
        if (!lines_opt) {
            return core::Result<std::map<std::string, std::vector<std::string>>>::success(
                std::move(targets)
            );
        }

        std::string current_target;
        for (const auto& line : *lines_opt) {
            if (auto trimmed = utils::trim(line); trimmed.starts_with("build ")) {
                auto parts = utils::split(trimmed.substr(6), ':');
                if (!parts.empty()) {
                    current_target = utils::trim(parts[0]);
                    targets[current_target] = {};
                }
            } else if (!current_target.empty() && trimmed.starts_with("$")) {
                continue;
            }
        }

        return core::Result<std::map<std::string, std::vector<std::string>>>::success(
            std::move(targets)
        );
    }

    core::Result<std::vector<std::string>> NinjaAdapter::get_build_order(
        const std::string& build_dir
    ) {
        std::vector<std::string> build_order;

        auto log_result = parse_ninja_log();
        if (!log_result.is_success()) {
            return core::Result<std::vector<std::string>>::failure(
                log_result.error()
            );
        }

        auto entries = log_result.value();
        std::ranges::sort(entries,
                          [](const NinjaBuildEntry& a, const NinjaBuildEntry& b) {
                              return a.start_time_ms < b.start_time_ms;
                          });

        for (const auto& entry : entries) {
            build_order.push_back(entry.target);
        }

        return core::Result<std::vector<std::string>>::success(std::move(build_order));
    }

    core::Result<bool> NinjaAdapter::enable_tracing(
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

        const std::filesystem::path ninja_file = std::filesystem::path(build_dir) / "build.ninja";

        if (!utils::file_exists(ninja_file.string())) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "build.ninja not found in build directory"
            );
        }

        auto lines_opt = utils::read_lines(ninja_file.string());
        if (!lines_opt) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read build.ninja"
            );
        }

        std::vector<std::string> updated_lines;
        bool modified = false;

        for (auto& line : *lines_opt) {
            if ((line.find("FLAGS =") != std::string::npos ||
                 line.find("FLAGS=") != std::string::npos) &&
                (line.find("CXX") != std::string::npos ||
                 line.find("C_") != std::string::npos ||
                 line.find("cc") != std::string::npos)) {
                if (line.find(flag) == std::string::npos) {
                    line += " " + flag;
                    modified = true;
                }
            }
            updated_lines.push_back(line);
        }

        if (!modified) {
            return core::Result<bool>::failure(
                core::ErrorCode::UNSUPPORTED_FORMAT,
                "No compiler flags found in build.ninja"
            );
        }

        if (!utils::write_lines(ninja_file.string(), updated_lines)) {
            return core::Result<bool>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not write updated build.ninja"
            );
        }

        return core::Result<bool>::success(true);
    }

    core::Result<std::vector<NinjaBuildEntry>> NinjaAdapter::parse_ninja_log() const {
        if (!utils::file_exists(ninja_log_path_.string())) {
            return core::Result<std::vector<NinjaBuildEntry>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                ".ninja_log not found"
            );
        }

        const auto lines_opt = utils::read_lines(ninja_log_path_.string());
        if (!lines_opt) {
            return core::Result<std::vector<NinjaBuildEntry>>::failure(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not read .ninja_log"
            );
        }

        std::vector<NinjaBuildEntry> entries;

        for (size_t i = 0; i < lines_opt->size(); ++i) {
            const auto& line = (*lines_opt)[i];

            if (i == 0 && line.starts_with("# ninja log")) {
                continue;
            }

            auto parts = utils::split(line, '\t');
            if (parts.size() < 5) {
                continue;
            }

            NinjaBuildEntry entry;
            entry.start_time_ms = std::stoull(parts[0]);
            entry.end_time_ms = std::stoull(parts[1]);
            entry.restat = std::stoul(parts[2]);
            entry.target = parts[3];
            entry.duration_ms = entry.end_time_ms - entry.start_time_ms;

            entries.push_back(entry);
        }

        return core::Result<std::vector<NinjaBuildEntry>>::success(std::move(entries));
    }

    core::Result<std::string> NinjaAdapter::get_ninja_version() {
        return core::Result<std::string>::success("Unknown");
    }

    core::Result<std::vector<std::string>> NinjaAdapter::parse_build_file() {
        return core::Result<std::vector<std::string>>::success(std::vector<std::string>{});
    }

    core::Result<std::map<std::string, std::string>> NinjaAdapter::parse_deps_log() {
        return core::Result<std::map<std::string, std::string>>::success(
            std::map<std::string, std::string>{}
        );
    }
}
