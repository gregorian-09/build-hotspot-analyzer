#include "bha/project_index.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string_view>
#include <nlohmann/json.hpp>

namespace bha {
    namespace {

        std::string lowercase_extension(const fs::path& path) {
            std::string extension = path.extension().string();
            std::ranges::transform(
                extension,
                extension.begin(),
                [](const unsigned char value) { return static_cast<char>(std::tolower(value)); }
            );
            return extension;
        }

        std::vector<std::string> split_shell_command(const std::string& command) {
            std::vector<std::string> parts;
            std::string current;
            char quote = '\0';
            bool escaped = false;
            for (const char character : command) {
                if (escaped) {
                    current.push_back(character);
                    escaped = false;
                    continue;
                }
                if (character == '\\' && quote != '\'') {
                    escaped = true;
                    continue;
                }
                if (quote != '\0') {
                    if (character == quote) {
                        quote = '\0';
                    } else {
                        current.push_back(character);
                    }
                    continue;
                }
                if (character == '\'' || character == '"') {
                    quote = character;
                } else if (std::isspace(static_cast<unsigned char>(character))) {
                    if (!current.empty()) {
                        parts.push_back(std::move(current));
                        current.clear();
                    }
                } else {
                    current.push_back(character);
                }
            }
            if (escaped) {
                current.push_back('\\');
            }
            if (!current.empty()) {
                parts.push_back(std::move(current));
            }
            return parts;
        }

        std::string path_key(const fs::path& path) {
            return path.lexically_normal().generic_string();
        }

        fs::path absolute_from_directory(const fs::path& value, const fs::path& directory) {
            if (value.empty() || value.is_absolute()) {
                return value;
            }
            return (directory / value).lexically_normal();
        }

        void normalize_path_argument(
            std::vector<std::string>& arguments,
            const std::size_t index,
            const fs::path& directory
        ) {
            const std::string& argument = arguments[index];
            const auto normalize = [&](const std::size_t offset, const std::size_t length) {
                const fs::path value = argument.substr(offset, length);
                arguments[index] = argument.substr(0, offset) +
                    absolute_from_directory(value, directory).string();
            };

            if (argument == "-I" || argument == "-isystem" || argument == "-iquote" ||
                argument == "-idirafter" || argument == "-include" || argument == "-imacros") {
                if (index + 1 < arguments.size()) {
                    arguments[index + 1] = absolute_from_directory(arguments[index + 1], directory).string();
                }
                return;
            }
            if (argument.starts_with("-I") || argument.starts_with("-isystem") ||
                argument.starts_with("-iquote") || argument.starts_with("-idirafter") ||
                argument.starts_with("-include") || argument.starts_with("-imacros")) {
                const std::array<std::string_view, 6> prefixes = {
                    "-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros"
                };
                for (const auto prefix : prefixes) {
                    if (argument.starts_with(prefix)) {
                        normalize(prefix.size(), argument.size() - prefix.size());
                        break;
                    }
                }
                return;
            }
            if (argument == "/I" || argument == "/FI") {
                if (index + 1 < arguments.size()) {
                    arguments[index + 1] = absolute_from_directory(arguments[index + 1], directory).string();
                }
                return;
            }
            if (argument.starts_with("/I") || argument.starts_with("/FI")) {
                const std::size_t offset = argument.starts_with("/I") ? 2 : 3;
                normalize(offset, argument.size() - offset);
            }
        }

    }  // namespace

    ProjectIndex::ProjectIndex(fs::path project_root, std::optional<fs::path> compile_commands_path)
        : project_root_(std::move(project_root)),
          compile_commands_path_(std::move(compile_commands_path)) {
        if (!project_root_.empty() && project_root_.is_relative()) {
            project_root_ = fs::absolute(project_root_).lexically_normal();
        }
        if (compile_commands_path_.has_value() && compile_commands_path_->is_relative() && !project_root_.empty()) {
            *compile_commands_path_ = (project_root_ / *compile_commands_path_).lexically_normal();
        }
        if (project_root_.empty() && compile_commands_path_.has_value()) {
            project_root_ = compile_commands_path_->parent_path();
        }
    }

    const fs::path& ProjectIndex::project_root() const noexcept {
        return project_root_;
    }

    fs::path ProjectIndex::resolve(const fs::path& path) const {
        if (path.empty()) {
            return {};
        }
        if (path.is_absolute() || project_root_.empty()) {
            return path.lexically_normal();
        }
        return (project_root_ / path).lexically_normal();
    }

    std::optional<fs::path> ProjectIndex::find_file(const fs::path& path) const {
        if (path.empty()) {
            return std::nullopt;
        }
        const fs::path resolved = resolve(path);
        std::error_code ec;
        if (fs::is_regular_file(resolved, ec)) {
            return resolved;
        }
        if (path.has_parent_path()) {
            return std::nullopt;
        }

        ensure_files_indexed();
        const std::string filename = path.filename().generic_string();
        std::scoped_lock lock(mutex_);
        for (const auto& candidate : indexed_files_) {
            if (candidate.filename().generic_string() == filename) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> ProjectIndex::read_file(const fs::path& path) const {
        const auto resolved = find_file(path);
        if (!resolved.has_value()) {
            return std::nullopt;
        }
        const std::string key = path_key(*resolved);
        std::error_code metadata_ec;
        const auto size = fs::file_size(*resolved, metadata_ec);
        const auto timestamp = fs::last_write_time(*resolved, metadata_ec);
        {
            std::scoped_lock lock(mutex_);
            const auto cached = file_contents_.find(key);
            const auto metadata = file_metadata_.find(key);
            if (cached != file_contents_.end() && metadata != file_metadata_.end() &&
                metadata->second == std::pair{size, timestamp}) {
                return cached->second;
            }
        }

        std::ifstream input(*resolved);
        if (!input) {
            return std::nullopt;
        }
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::scoped_lock lock(mutex_);
        file_contents_[key] = content;
        file_metadata_[key] = {size, timestamp};
        return content;
    }

    std::vector<fs::path> ProjectIndex::files(const ProjectFileKind kind) const {
        ensure_files_indexed();
        std::scoped_lock lock(mutex_);
        std::vector<fs::path> result;
        for (const auto& path : indexed_files_) {
            if (kind == ProjectFileKind::Source && !is_source_file(path)) {
                continue;
            }
            if (kind == ProjectFileKind::Header && !is_header_file(path)) {
                continue;
            }
            result.push_back(path);
        }
        return result;
    }

    std::optional<CompilationUnit> ProjectIndex::compile_command_for(const fs::path& source_file) const {
        ensure_compile_commands_loaded();
        const auto resolved_source = source_file.has_parent_path()
            ? std::optional<fs::path>(resolve(source_file))
            : find_file(source_file);
        if (!resolved_source.has_value()) {
            return std::nullopt;
        }
        const std::string requested_key = path_key(*resolved_source);
        std::scoped_lock lock(mutex_);
        for (const auto& command : compile_commands_) {
            if (path_key(command.source_file) == requested_key) {
                return command;
            }
        }
        return std::nullopt;
    }

    std::vector<CompilationUnit> ProjectIndex::compile_commands() const {
        ensure_compile_commands_loaded();
        std::scoped_lock lock(mutex_);
        return compile_commands_;
    }

    CompilationDatabaseStatus ProjectIndex::compile_commands_status() const {
        ensure_compile_commands_loaded();
        std::scoped_lock lock(mutex_);
        return compile_commands_status_;
    }

    void ProjectIndex::ensure_files_indexed() const {
        std::call_once(files_once_, [this] {
            if (project_root_.empty()) {
                return;
            }
            std::vector<fs::path> discovered;
            std::error_code ec;
            const auto options = fs::directory_options::skip_permission_denied;
            for (fs::recursive_directory_iterator it(project_root_, options, ec), end;
                 it != end;
                 it.increment(ec)) {
                if (ec) {
                    ec.clear();
                    continue;
                }
                const auto& entry = *it;
                std::error_code entry_ec;
                if (entry.is_directory(entry_ec)) {
                    if (should_skip_directory(entry.path())) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (entry.is_regular_file(entry_ec) &&
                    (is_source_file(entry.path()) || is_header_file(entry.path()))) {
                    discovered.push_back(entry.path().lexically_normal());
                }
            }
            std::ranges::sort(discovered);
            std::scoped_lock lock(mutex_);
            indexed_files_ = std::move(discovered);
        });
    }

    void ProjectIndex::ensure_compile_commands_loaded() const {
        std::call_once(compile_commands_once_, [this] {
            fs::path database_path;
            if (compile_commands_path_.has_value()) {
                database_path = *compile_commands_path_;
            } else if (!project_root_.empty()) {
                std::vector<fs::path> candidates = {
                    project_root_ / "compile_commands.json",
                    project_root_ / "build" / "compile_commands.json",
                    project_root_ / "build" / "Debug" / "compile_commands.json",
                    project_root_ / "build" / "Release" / "compile_commands.json",
                    project_root_ / "out" / "build" / "compile_commands.json",
                    project_root_ / "out" / "build" / "Debug" / "compile_commands.json",
                    project_root_ / "out" / "build" / "Release" / "compile_commands.json",
                    project_root_ / "cmake-build-debug" / "compile_commands.json",
                    project_root_ / "cmake-build-release" / "compile_commands.json"
                };
                std::error_code iterator_ec;
                for (const auto& entry : fs::directory_iterator(project_root_, iterator_ec)) {
                    if (iterator_ec || !entry.is_directory()) {
                        continue;
                    }
                    candidates.push_back(entry.path() / "compile_commands.json");
                }
                std::ranges::sort(candidates, [](const fs::path& lhs, const fs::path& rhs) {
                    return lhs.generic_string() < rhs.generic_string();
                });
                candidates.erase(std::ranges::unique(candidates).begin(), candidates.end());
                for (const auto& candidate : candidates) {
                    std::error_code ec;
                    if (fs::is_regular_file(candidate, ec)) {
                        database_path = candidate;
                        break;
                    }
                }
            }
            if (database_path.empty()) {
                std::scoped_lock lock(mutex_);
                compile_commands_status_ = compile_commands_path_.has_value()
                    ? CompilationDatabaseStatus::NotFound
                    : CompilationDatabaseStatus::NotConfigured;
                return;
            }

            std::error_code database_ec;
            if (!fs::is_regular_file(database_path, database_ec)) {
                std::scoped_lock lock(mutex_);
                compile_commands_status_ = CompilationDatabaseStatus::NotFound;
                return;
            }

            std::ifstream input(database_path);
            if (!input) {
                std::scoped_lock lock(mutex_);
                compile_commands_status_ = CompilationDatabaseStatus::Invalid;
                return;
            }
            nlohmann::json database;
            try {
                input >> database;
            } catch (const nlohmann::json::exception&) {
                std::scoped_lock lock(mutex_);
                compile_commands_status_ = CompilationDatabaseStatus::Invalid;
                return;
            }
            if (!database.is_array()) {
                std::scoped_lock lock(mutex_);
                compile_commands_status_ = CompilationDatabaseStatus::Invalid;
                return;
            }

            std::vector<CompilationUnit> loaded;
            bool malformed_entry = false;
            for (const auto& entry : database) {
                if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) {
                    malformed_entry = true;
                    continue;
                }
                fs::path source = entry["file"].get<std::string>();
                fs::path directory = database_path.parent_path();
                if (entry.contains("directory") && entry["directory"].is_string()) {
                    directory = entry["directory"].get<std::string>();
                    if (directory.is_relative()) {
                        directory = database_path.parent_path() / directory;
                    }
                }
                directory = directory.lexically_normal();
                if (directory != directory.root_path() && directory.filename().empty()) {
                    directory = directory.parent_path();
                }
                if (source.is_relative()) {
                    source = directory / source;
                }

                std::vector<std::string> arguments;
                if (entry.contains("arguments") && entry.contains("command")) {
                    malformed_entry = true;
                    continue;
                }
                if (entry.contains("arguments") && entry["arguments"].is_array()) {
                    for (const auto& argument : entry["arguments"]) {
                        if (argument.is_string()) {
                            arguments.push_back(argument.get<std::string>());
                        }
                    }
                } else if (entry.contains("command") && entry["command"].is_string()) {
                    arguments = split_shell_command(entry["command"].get<std::string>());
                }
                if (arguments.empty()) {
                    malformed_entry = true;
                    continue;
                }
                for (auto& argument : arguments) {
                    const fs::path argument_path(argument);
                    const std::string extension = lowercase_extension(argument_path);
                    if (argument_path.is_relative() &&
                        (extension == ".c" || extension == ".cc" || extension == ".cpp" || extension == ".cxx")) {
                        argument = (directory / argument_path).lexically_normal().string();
                    }
                }
                for (std::size_t index = 0; index < arguments.size(); ++index) {
                    normalize_path_argument(arguments, index, directory);
                }
                CompilationUnit unit;
                unit.source_file = source.lexically_normal();
                unit.working_directory = directory.lexically_normal();
                unit.command_line = std::move(arguments);
                loaded.push_back(std::move(unit));
            }
            std::scoped_lock lock(mutex_);
            compile_commands_ = std::move(loaded);
            compile_commands_status_ = compile_commands_.empty() || malformed_entry
                ? CompilationDatabaseStatus::Invalid
                : CompilationDatabaseStatus::Loaded;
        });
    }

    bool ProjectIndex::is_source_file(const fs::path& path) {
        const std::string extension = lowercase_extension(path);
        return extension == ".c" || extension == ".cc" || extension == ".cpp" ||
               extension == ".cxx" || extension == ".m" || extension == ".mm";
    }

    bool ProjectIndex::is_header_file(const fs::path& path) {
        const std::string extension = lowercase_extension(path);
        return extension == ".h" || extension == ".hh" || extension == ".hpp" ||
               extension == ".hxx" || extension == ".inl" || extension == ".ipp";
    }

    bool ProjectIndex::should_skip_directory(const fs::path& path) {
        const std::string name = path.filename().string();
        return name == ".git" || name == ".lsp-optimization-backup" || name == "build" ||
               name == "out" || name.rfind("cmake-build-", 0) == 0 || name == "node_modules";
    }

}  // namespace bha
