//
// Created by gregorian-rayne on 12/30/25.
//

#ifndef BHA_GIT_INTEGRATION_HPP
#define BHA_GIT_INTEGRATION_HPP

/**
 * @file git_integration.hpp
 * @brief Git integration for build performance tracking.
 *
 * Provides Git functionality for:
 * - Executing git commands safely
 * - Parsing commit information
 * - Tracking build performance per commit
 * - Author attribution for build hotspots
 * - Automated bisection for performance regressions
 *
 * Design principles:
 * - Safe command execution with proper escaping
 * - Support for large repositories
 * - Cross-platform compatibility
 * - Integration with CI/CD systems
 */

#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/types.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bha::git
{
    /**
     * Git commit information.
     */
    struct CommitInfo {
        std::string hash;             // Full 40-character SHA
        std::string short_hash;       // Short 7-character SHA
        std::string author_name;
        std::string author_email;
        Timestamp author_date;
        std::string committer_name;
        std::string committer_email;
        Timestamp commit_date;
        std::string subject;          // First line of commit message
        std::string body;             // Rest of commit message
        std::vector<std::string> parent_hashes;
        std::vector<std::string> files_changed;
        std::size_t insertions = 0;
        std::size_t deletions = 0;
    };

    /**
     * Blame entry for a single line.
     */
    struct BlameEntry {
        std::string commit_hash;
        std::string author_name;
        std::string author_email;
        Timestamp author_date;
        std::string original_file;
        std::size_t original_line = 0;
        std::size_t final_line = 0;
        std::string line_content;
    };

    /**
     * Blame result for a file.
     */
    struct BlameResult {
        fs::path file;
        std::vector<BlameEntry> entries;
        std::unordered_map<std::string, std::size_t> lines_per_author;
        Duration analysis_time = Duration::zero();
    };

    /**
     * Author statistics.
     */
    struct AuthorStats {
        std::string name;
        std::string email;
        std::size_t commit_count = 0;
        std::size_t files_touched = 0;
        std::size_t lines_added = 0;
        std::size_t lines_removed = 0;
        Timestamp first_commit;
        Timestamp last_commit;

        // Build impact
        Duration total_compile_time_impact = Duration::zero();
        std::vector<std::string> hotspot_files;
    };

    /**
     * Branch comparison result.
     */
    struct BranchComparison {
        std::string base_branch;
        std::string compare_branch;
        std::string merge_base;  // Common ancestor

        std::size_t commits_ahead = 0;
        std::size_t commits_behind = 0;

        std::vector<std::string> files_added;
        std::vector<std::string> files_modified;
        std::vector<std::string> files_deleted;

        // Build impact
        Duration estimated_time_change = Duration::zero();
        std::vector<std::string> new_hotspots;
        std::vector<std::string> resolved_hotspots;
    };

    /**
     * Bisect state.
     */
    enum class BisectState {
        NotStarted,
        InProgress,
        Found,
        NotFound,
        Aborted
    };

    /**
     * Bisect result.
     */
    struct BisectResult {
        BisectState state = BisectState::NotStarted;
        std::string first_bad_commit;
        std::string good_commit;
        std::string bad_commit;
        std::size_t steps_taken = 0;
        Duration total_time = Duration::zero();
        std::vector<std::string> tested_commits;
    };

    /**
     * Hook type.
     */
    enum class HookType {
        PreCommit,
        PrePush,
        PostMerge,
        PostCheckout,
        PrepareCommitMsg
    };

    /**
     * Hook status.
     */
    struct HookStatus {
        HookType type;
        bool installed = false;
        fs::path path;
        bool is_bha_hook = false;  // True if installed by BHA
        std::string version;       // BHA version that installed the hook
    };

    /**
     * Command execution result.
     */
    struct CommandResult {
        int exit_code = 0;
        std::string stdout_output;
        std::string stderr_output;
        Duration execution_time = Duration::zero();
    };

    /**
     * Executes a git command safely.
     *
     * @param args Command arguments (without "git" prefix).
     * @param working_dir Working directory for the command.
     * @param timeout Maximum execution time.
     * @return Command result or error.
     */
    [[nodiscard]] Result<CommandResult, Error> execute_git(
        const std::vector<std::string>& args,
        const fs::path& working_dir = fs::current_path(),
        Duration timeout = std::chrono::seconds(30)
    );

    /**
     * Checks if a directory is a git repository.
     */
    [[nodiscard]] bool is_git_repository(const fs::path& dir);

    /**
     * Gets the root directory of a git repository.
     */
    [[nodiscard]] Result<fs::path, Error> get_repository_root(
        const fs::path& dir = fs::current_path()
    );

    /**
     * Parses a commit from git log output.
     */
    [[nodiscard]] Result<CommitInfo, Error> parse_commit(
        std::string_view git_output
    );

    /**
     * Gets information about a specific commit.
     */
    [[nodiscard]] Result<CommitInfo, Error> get_commit(
        const std::string& ref,
        const fs::path& repo_dir = fs::current_path()
    );

    /**
     * Gets commits in a range.
     */
    [[nodiscard]] Result<std::vector<CommitInfo>, Error> get_commits(
        const std::string& range,
        std::size_t max_count = 0,
        const fs::path& repo_dir = fs::current_path()
    );

    /**
     * Gets the current branch name.
     */
    [[nodiscard]] Result<std::string, Error> get_current_branch(
        const fs::path& repo_dir = fs::current_path()
    );

    /**
     * Gets the current HEAD commit.
     */
    [[nodiscard]] Result<std::string, Error> get_head(
        const fs::path& repo_dir = fs::current_path()
    );

    /**
     * Checks if there are uncommitted changes.
     */
    [[nodiscard]] Result<bool, Error> has_uncommitted_changes(
        const fs::path& repo_dir = fs::current_path()
    );

    /**
     * Interface for git blame operations.
     */
    class IBlameAnalyzer {
    public:
        virtual ~IBlameAnalyzer() = default;

        /**
         * Analyzes blame for a single file.
         */
        [[nodiscard]] virtual Result<BlameResult, Error> blame_file(
            const fs::path& file,
            const std::string& ref = "HEAD"
        ) const = 0;

        /**
         * Analyzes blame for multiple files.
         */
        [[nodiscard]] virtual Result<std::vector<BlameResult>, Error> blame_files(
            const std::vector<fs::path>& files,
            const std::string& ref = "HEAD"
        ) const = 0;

        /**
         * Gets author statistics.
         */
        [[nodiscard]] virtual Result<std::vector<AuthorStats>, Error> get_author_stats(
            const std::vector<BlameResult>& blame_results
        ) const = 0;
    };

    /**
     * Interface for git bisect operations.
     */
    class IBisectRunner {
    public:
        virtual ~IBisectRunner() = default;

        using TestFunction = std::function<Result<bool, Error>(const std::string& commit)>;

        /**
         * Runs an automated bisect.
         *
         * @param good_commit Known good commit.
         * @param bad_commit Known bad commit.
         * @param test_fn Function that returns true for "good", false for "bad".
         * @return Bisect result with first bad commit.
         */
        [[nodiscard]] virtual Result<BisectResult, Error> run(
            const std::string& good_commit,
            const std::string& bad_commit,
            TestFunction test_fn
        ) = 0;

        /**
         * Aborts an in-progress bisect.
         */
        virtual Result<void, Error> abort() = 0;

        /**
         * Gets current bisect state.
         */
        [[nodiscard]] virtual BisectState state() const = 0;
    };

    /**
     * Interface for tracking build times per commit.
     */
    class ICommitTracker {
    public:
        virtual ~ICommitTracker() = default;

        /**
         * Records build time for a commit.
         */
        virtual Result<void, Error> record(
            const std::string& commit_hash,
            Duration build_time,
            const analyzers::AnalysisResult& analysis
        ) = 0;

        /**
         * Gets build time for a commit.
         */
        [[nodiscard]] virtual Result<Duration, Error> get_build_time(
            const std::string& commit_hash
        ) const = 0;

        /**
         * Gets build history.
         */
        [[nodiscard]] virtual Result<std::vector<std::pair<CommitInfo, Duration>>, Error>
        get_history(std::size_t limit = 100) const = 0;

        /**
         * Finds commits with significant build time changes.
         */
        [[nodiscard]] virtual Result<std::vector<std::pair<CommitInfo, Duration>>, Error>
        find_regressions(double threshold_percent = 10.0) const = 0;
    };

    /**
     * Interface for branch comparison.
     */
    class IBranchComparator {
    public:
        virtual ~IBranchComparator() = default;

        /**
         * Compares two branches.
         */
        [[nodiscard]] virtual Result<BranchComparison, Error> compare(
            const std::string& base_branch,
            const std::string& compare_branch
        ) const = 0;

        /**
         * Estimates PR impact.
         */
        [[nodiscard]] virtual Result<BranchComparison, Error> estimate_pr_impact(
            const std::string& pr_branch
        ) const = 0;
    };

    /**
     * Interface for managing git hooks.
     */
    class IHookManager {
    public:
        virtual ~IHookManager() = default;

        /**
         * Installs a BHA hook.
         */
        virtual Result<void, Error> install(HookType type) = 0;

        /**
         * Uninstalls a BHA hook.
         */
        virtual Result<void, Error> uninstall(HookType type) = 0;

        /**
         * Gets hook status.
         */
        [[nodiscard]] virtual Result<HookStatus, Error> status(HookType type) const = 0;

        /**
         * Gets all hook statuses.
         */
        [[nodiscard]] virtual Result<std::vector<HookStatus>, Error> all_statuses() const = 0;
    };

    /**
     * Converts hook type to string.
     */
    std::string_view hook_type_to_string(HookType type);

    /**
     * Converts string to hook type.
     */
    std::optional<HookType> string_to_hook_type(std::string_view str);

} // namespace bha::git

#endif //BHA_GIT_INTEGRATION_HPP