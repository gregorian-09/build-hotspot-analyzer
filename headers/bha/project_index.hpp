#ifndef BHA_PROJECT_INDEX_HPP
#define BHA_PROJECT_INDEX_HPP

#include "bha/types.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bha {

    enum class ProjectFileKind {
        Any,
        Source,
        Header
    };

    enum class CompilationDatabaseStatus {
        NotConfigured,
        NotFound,
        Invalid,
        Loaded
    };

    /** Lazy, shared project metadata cache used by suggesters. */
    class ProjectIndex {
    public:
        explicit ProjectIndex(
            fs::path project_root = {},
            std::optional<fs::path> compile_commands_path = std::nullopt
        );

        [[nodiscard]] const fs::path& project_root() const noexcept;
        [[nodiscard]] fs::path resolve(const fs::path& path) const;
        [[nodiscard]] std::optional<fs::path> find_file(const fs::path& path) const;
        [[nodiscard]] std::optional<std::string> read_file(const fs::path& path) const;
        [[nodiscard]] std::vector<fs::path> files(ProjectFileKind kind = ProjectFileKind::Any) const;
        [[nodiscard]] std::optional<CompilationUnit> compile_command_for(
            const fs::path& source_file
        ) const;
        [[nodiscard]] std::vector<CompilationUnit> compile_commands() const;
        [[nodiscard]] CompilationDatabaseStatus compile_commands_status() const;

    private:
        void ensure_files_indexed() const;
        void ensure_compile_commands_loaded() const;

        [[nodiscard]] static bool is_source_file(const fs::path& path);
        [[nodiscard]] static bool is_header_file(const fs::path& path);
        [[nodiscard]] static bool should_skip_directory(const fs::path& path);

        fs::path project_root_;
        std::optional<fs::path> compile_commands_path_;
        mutable std::once_flag files_once_;
        mutable std::once_flag compile_commands_once_;
        mutable std::mutex mutex_;
        mutable std::vector<fs::path> indexed_files_;
        mutable std::unordered_map<std::string, std::string> file_contents_;
        mutable std::vector<CompilationUnit> compile_commands_;
        mutable CompilationDatabaseStatus compile_commands_status_ =
            CompilationDatabaseStatus::NotConfigured;
    };

}  // namespace bha

#endif  // BHA_PROJECT_INDEX_HPP
