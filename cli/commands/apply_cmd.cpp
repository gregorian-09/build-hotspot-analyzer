#include "bha/cli/commands/command.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace bha::cli {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    namespace {
        struct TextEdit {
            fs::path file;
            std::size_t start_line = 0;
            std::size_t start_col = 0;
            std::size_t end_line = 0;
            std::size_t end_col = 0;
            std::string new_text;
        };

        struct BackupEntry {
            fs::path original;
            fs::path copy;
            bool existed = false;
        };

        std::optional<std::size_t> line_col_to_offset(const std::string& content, std::size_t line, std::size_t col) {
            std::size_t line_start = 0;
            std::size_t current_line = 0;

            for (std::size_t i = 0; i < content.size(); ++i) {
                if (current_line == line) {
                    std::size_t pos = line_start;
                    std::size_t current_col = 0;
                    while (pos < content.size() && content[pos] != '\n' && content[pos] != '\r') {
                        if (current_col >= col) {
                            return pos;
                        }
                        ++current_col;
                        ++pos;
                    }
                    return pos;
                }

                if (content[i] == '\n') {
                    ++current_line;
                    line_start = i + 1;
                } else if (content[i] == '\r') {
                    ++current_line;
                    if (i + 1 < content.size() && content[i + 1] == '\n') {
                        ++i;
                    }
                    line_start = i + 1;
                }
            }

            if (current_line == line) {
                return content.size();
            }
            return std::nullopt;
        }

        bool apply_single_edit(std::string& content, const TextEdit& edit) {
            auto start_offset = line_col_to_offset(content, edit.start_line, edit.start_col);
            auto end_offset = line_col_to_offset(content, edit.end_line, edit.end_col);
            if (!start_offset || !end_offset) {
                return false;
            }
            if (*start_offset > *end_offset) {
                std::swap(*start_offset, *end_offset);
            }
            *start_offset = std::min(*start_offset, content.size());
            *end_offset = std::min(*end_offset, content.size());
            content.replace(*start_offset, *end_offset - *start_offset, edit.new_text);
            return true;
        }

        bool apply_edits_to_file(const fs::path& file_path, std::vector<TextEdit> edits) {
            std::ifstream in(file_path, std::ios::binary);
            if (!in) {
                return false;
            }
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();

            std::ranges::sort(edits, [](const TextEdit& a, const TextEdit& b) {
                if (a.start_line != b.start_line) {
                    return a.start_line > b.start_line;
                }
                return a.start_col > b.start_col;
            });

            for (const auto& edit : edits) {
                if (!apply_single_edit(content, edit)) {
                    return false;
                }
            }

            std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                return false;
            }
            out << content;
            return out.good();
        }

        std::vector<TextEdit> parse_edits_array(
            const json& array,
            const fs::path& project_root
        ) {
            std::vector<TextEdit> parsed;
            if (!array.is_array()) {
                return parsed;
            }

            auto read_size = [](const json& item, const char* snake, const char* camel, std::size_t fallback = 0) {
                if (item.contains(snake) && item[snake].is_number_unsigned()) {
                    return item[snake].get<std::size_t>();
                }
                if (item.contains(camel) && item[camel].is_number_unsigned()) {
                    return item[camel].get<std::size_t>();
                }
                return fallback;
            };

            parsed.reserve(array.size());
            for (const auto& item : array) {
                if (!item.is_object()) {
                    continue;
                }

                std::string file_str;
                if (item.contains("file") && item["file"].is_string()) {
                    file_str = item["file"].get<std::string>();
                } else if (item.contains("path") && item["path"].is_string()) {
                    file_str = item["path"].get<std::string>();
                } else {
                    continue;
                }

                fs::path file = file_str;
                if (file.is_relative() && !project_root.empty()) {
                    file = (project_root / file).lexically_normal();
                }

                TextEdit edit;
                edit.file = file;
                edit.start_line = read_size(item, "start_line", "startLine");
                edit.start_col = read_size(item, "start_col", "startCol");
                edit.end_line = read_size(item, "end_line", "endLine", edit.start_line);
                edit.end_col = read_size(item, "end_col", "endCol", edit.start_col);
                if (item.contains("new_text") && item["new_text"].is_string()) {
                    edit.new_text = item["new_text"].get<std::string>();
                } else if (item.contains("newText") && item["newText"].is_string()) {
                    edit.new_text = item["newText"].get<std::string>();
                }
                parsed.push_back(std::move(edit));
            }

            return parsed;
        }

        void collect_edits_from_json(
            const json& value,
            const fs::path& project_root,
            std::vector<TextEdit>& out
        ) {
            if (value.is_array()) {
                auto direct = parse_edits_array(value, project_root);
                if (!direct.empty()) {
                    out.insert(out.end(), direct.begin(), direct.end());
                    return;
                }
                for (const auto& item : value) {
                    collect_edits_from_json(item, project_root, out);
                }
                return;
            }

            if (!value.is_object()) {
                return;
            }

            if (value.contains("edits")) {
                auto edits = parse_edits_array(value["edits"], project_root);
                out.insert(out.end(), edits.begin(), edits.end());
            }
            if (value.contains("text_edits")) {
                auto edits = parse_edits_array(value["text_edits"], project_root);
                out.insert(out.end(), edits.begin(), edits.end());
            }
            if (value.contains("textEdits")) {
                auto edits = parse_edits_array(value["textEdits"], project_root);
                out.insert(out.end(), edits.begin(), edits.end());
            }
            if (value.contains("suggestions")) {
                collect_edits_from_json(value["suggestions"], project_root, out);
            }
        }

        std::string safe_path_component(const std::string& input) {
            std::string out;
            out.reserve(input.size());
            for (const char c : input) {
                if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-') {
                    out.push_back(c);
                } else {
                    out.push_back('_');
                }
            }
            return out;
        }
    }  // namespace

    class ApplyCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "apply";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Apply an edit bundle directly (without generating suggestions)";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha apply --edits-file <FILE> [OPTIONS]\n"
                   "\n"
                   "Examples:\n"
                   "  bha apply --edits-file edits.json\n"
                   "  bha apply --edits-file details.json --validate-build --build-cmd \"cmake --build build -j\"\n"
                   "  bha apply --edits-file report.json --project-root /path/to/repo";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"edits-file", 'e', "Path to JSON file containing text edits", true, true, "", "FILE"},
                {"project-root", 0, "Project root for resolving relative edit paths", false, true, "", "DIR"},
                {"validate-build", 0, "Run a build command after applying edits", false, false, "", ""},
                {"build-cmd", 0, "Build command used with --validate-build", false, true, "", "CMD"},
                {"no-rollback", 0, "Do not rollback if build validation fails", false, false, "", ""},
                {"no-backup", 0, "Skip pre-apply backup", false, false, "", ""},
                {"backup-dir", 0, "Directory for temporary backups", false, true, ".bha-apply-backups", "DIR"}
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs& args) const override {
            if (!args.get("edits-file").has_value()) {
                return "Missing --edits-file";
            }
            if (args.get_flag("validate-build") && args.get_or("build-cmd", "").empty()) {
                return "--build-cmd is required when --validate-build is set";
            }
            return "";
        }

        [[nodiscard]] int execute(const ParsedArgs& args) override {
            if (args.get_flag("help")) {
                print_help();
                return 0;
            }

            if (args.get_flag("verbose")) {
                set_verbosity(Verbosity::Verbose);
            } else if (args.get_flag("quiet")) {
                set_verbosity(Verbosity::Quiet);
            }
            if (args.get_flag("json")) {
                set_output_format(OutputFormat::JSON);
            }

            const fs::path edits_file = fs::path(*args.get("edits-file"));
            if (!fs::exists(edits_file)) {
                print_error("Edits file not found: " + edits_file.string());
                return 1;
            }

            fs::path project_root;
            if (auto root = args.get("project-root"); root.has_value()) {
                project_root = fs::absolute(*root).lexically_normal();
            } else {
                project_root = fs::current_path();
            }

            std::ifstream in(edits_file);
            if (!in) {
                print_error("Failed to open edits file: " + edits_file.string());
                return 1;
            }

            json payload;
            try {
                in >> payload;
            } catch (const std::exception& e) {
                print_error(std::string("Invalid JSON: ") + e.what());
                return 1;
            }

            std::vector<TextEdit> edits;
            collect_edits_from_json(payload, project_root, edits);
            if (edits.empty()) {
                print_error("No text edits found in input payload");
                return 1;
            }

            std::unordered_map<std::string, std::vector<TextEdit>> edits_by_file;
            for (const auto& edit : edits) {
                edits_by_file[edit.file.lexically_normal().string()].push_back(edit);
            }

            const bool create_backup = !args.get_flag("no-backup");
            const bool validate_build = args.get_flag("validate-build");
            const bool rollback_on_failure = !args.get_flag("no-rollback");

            std::vector<BackupEntry> backup_entries;
            fs::path backup_root;
            if (create_backup) {
                const fs::path backup_base = fs::path(args.get_or("backup-dir", ".bha-apply-backups"));
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tm_buf{};
#ifdef _WIN32
                localtime_s(&tm_buf, &time_t);
#else
                localtime_r(&time_t, &tm_buf);
#endif
                std::ostringstream stamp;
                stamp << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
                backup_root = (project_root / backup_base / stamp.str()).lexically_normal();
                fs::create_directories(backup_root);

                backup_entries.reserve(edits_by_file.size());
                for (const auto& [file_str, _] : edits_by_file) {
                    const fs::path original(file_str);
                    BackupEntry entry;
                    entry.original = original;
                    entry.existed = fs::exists(original);
                    if (entry.existed) {
                        const std::string rel = safe_path_component(original.lexically_normal().string());
                        entry.copy = backup_root / rel;
                        if (const fs::path parent = entry.copy.parent_path(); !parent.empty()) {
                            fs::create_directories(parent);
                        }
                        std::error_code ec;
                        fs::copy_file(original, entry.copy, fs::copy_options::overwrite_existing, ec);
                        if (ec) {
                            print_error("Failed to backup file: " + original.string());
                            return 1;
                        }
                    }
                    backup_entries.push_back(std::move(entry));
                }
            }

            std::vector<std::string> changed_files;
            for (auto& [file_str, file_edits] : edits_by_file) {
                const fs::path file_path(file_str);
                if (!fs::exists(file_path)) {
                    if (const fs::path parent = file_path.parent_path(); !parent.empty()) {
                        fs::create_directories(parent);
                    }
                    std::ofstream out(file_path);
                    if (!out) {
                        print_error("Failed to create file: " + file_path.string());
                        return 1;
                    }
                }
                if (!apply_edits_to_file(file_path, std::move(file_edits))) {
                    print_error("Failed applying edits to: " + file_path.string());
                    return 1;
                }
                changed_files.push_back(file_path.string());
            }

            bool build_ok = true;
            int build_code = 0;
            if (validate_build) {
                const std::string cmd = args.get_or("build-cmd", "");
                print_verbose("Validating build: " + cmd);
                if (!project_root.empty()) {
                    std::error_code ec;
                    fs::current_path(project_root, ec);
                }
                build_code = std::system(cmd.c_str());
                build_ok = (build_code == 0);
                if (!build_ok && rollback_on_failure && create_backup) {
                    for (const auto& entry : backup_entries) {
                        if (entry.existed) {
                            std::error_code ec;
                            fs::copy_file(entry.copy, entry.original, fs::copy_options::overwrite_existing, ec);
                        } else {
                            std::error_code ec;
                            fs::remove(entry.original, ec);
                        }
                    }
                }
            }

            if (is_json()) {
                json out{
                    {"success", build_ok},
                    {"editsApplied", changed_files.size()},
                    {"changedFiles", changed_files},
                    {"buildValidation", {
                        {"requested", validate_build},
                        {"success", build_ok},
                        {"exitCode", build_code}
                    }},
                    {"backup", {
                        {"created", create_backup},
                        {"path", create_backup ? backup_root.string() : ""}
                    }}
                };
                std::cout << out.dump(2) << "\n";
            } else {
                print("Applied " + std::to_string(changed_files.size()) + " file(s)");
                if (create_backup) {
                    print("Backup: " + backup_root.string());
                }
                if (validate_build) {
                    if (build_ok) {
                        print("Build validation: success");
                    } else {
                        print_error("Build validation failed (exit code " + std::to_string(build_code) + ")");
                        if (rollback_on_failure && create_backup) {
                            print("Rollback: completed");
                        }
                    }
                }
            }

            return build_ok ? 0 : 1;
        }
    };

    namespace {
        struct ApplyCommandRegistrar {
            ApplyCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<ApplyCommand>()
                );
            }
        } apply_registrar;
    }
}  // namespace bha::cli
