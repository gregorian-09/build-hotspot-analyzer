//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/git/git_integration.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <ranges>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <utility>
#endif

namespace bha::git
{
    namespace {

        /**
         * Platform-specific command execution.
         */
#ifdef _WIN32
        CommandResult execute_command_impl(
            const std::string& command,
            const fs::path& working_dir,
            const Duration timeout
        ) {
            CommandResult result;
            const auto start_time = std::chrono::steady_clock::now();

            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;

            HANDLE stdout_read = nullptr, stdout_write = nullptr;
            HANDLE stderr_read = nullptr, stderr_write = nullptr;

            CreatePipe(&stdout_read, &stdout_write, &sa, 0);
            CreatePipe(&stderr_read, &stderr_write, &sa, 0);

            SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.hStdOutput = stdout_write;
            si.hStdError = stderr_write;
            si.dwFlags = STARTF_USESTDHANDLES;

            PROCESS_INFORMATION pi = {};

            std::string cmd = command;
            const std::string dir = working_dir.string();

            if (!CreateProcessA(
                nullptr,
                cmd.data(),
                nullptr,
                nullptr,
                TRUE,
                0,
                nullptr,
                dir.c_str(),
                &si,
                &pi
            )) {
                result.exit_code = -1;
                return result;
            }

            CloseHandle(stdout_write);
            CloseHandle(stderr_write);

            const auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);

            if (const DWORD wait_result = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout_ms.count())); wait_result == WAIT_TIMEOUT) {
                TerminateProcess(pi.hProcess, 1);
                result.exit_code = -2;  // Timeout
            } else {
                DWORD exit_code;
                GetExitCodeProcess(pi.hProcess, &exit_code);
                result.exit_code = static_cast<int>(exit_code);
            }

            char buffer[4096];
            DWORD bytes_read;
            while (ReadFile(stdout_read, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
                buffer[bytes_read] = '\0';
                result.stdout_output += buffer;
            }

            while (ReadFile(stderr_read, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0) {
                buffer[bytes_read] = '\0';
                result.stderr_output += buffer;
            }

            CloseHandle(stdout_read);
            CloseHandle(stderr_read);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            const auto end_time = std::chrono::steady_clock::now();
            result.execution_time = std::chrono::duration_cast<Duration>(end_time - start_time);

            return result;
        }
#else
        CommandResult execute_command_impl(
            const std::string& command,
            const fs::path& working_dir,
            const Duration timeout
        ) {
            CommandResult result;
            const auto start_time = std::chrono::steady_clock::now();

            int stdout_pipe[2];
            int stderr_pipe[2];

            if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
                result.exit_code = -1;
                return result;
            }

            const pid_t pid = fork();
            if (pid < 0) {
                result.exit_code = -1;
                return result;
            }

            if (pid == 0) {
                // Child process
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);

                dup2(stdout_pipe[1], STDOUT_FILENO);
                dup2(stderr_pipe[1], STDERR_FILENO);

                close(stdout_pipe[1]);
                close(stderr_pipe[1]);

                if (chdir(working_dir.c_str()) != 0) {
                    _exit(127);
                }

                execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
                _exit(127);
            }

            // Parent process
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);

            // Set non-blocking
            fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
            fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

            const auto timeout_point = std::chrono::steady_clock::now() + timeout;
            int status = 0;
            bool finished = false;

            while (!finished) {
                if (std::chrono::steady_clock::now() > timeout_point) {
                    kill(pid, SIGTERM);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    result.exit_code = -2;  // Timeout
                    finished = true;
                    continue;
                }

                // Read available output
                char buffer[4096];
                ssize_t n;

                while ((n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                    buffer[n] = '\0';
                    result.stdout_output += buffer;
                }

                while ((n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                    buffer[n] = '\0';
                    result.stderr_output += buffer;
                }

                // Has process finished?
                pid_t wpid = waitpid(pid, &status, WNOHANG);
                if (wpid > 0) {
                    while ((n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[n] = '\0';
                        result.stdout_output += buffer;
                    }
                    while ((n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[n] = '\0';
                        result.stderr_output += buffer;
                    }

                    if (WIFEXITED(status)) {
                        result.exit_code = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        result.exit_code = -WTERMSIG(status);
                    }
                    finished = true;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            close(stdout_pipe[0]);
            close(stderr_pipe[0]);

            const auto end_time = std::chrono::steady_clock::now();
            result.execution_time = std::chrono::duration_cast<Duration>(end_time - start_time);

            return result;
        }
#endif

        /**
         * Builds git command string from arguments.
         */
        std::string build_git_command(const std::vector<std::string>& args) {
            std::ostringstream cmd;
            cmd << "git";
            for (const auto& arg : args) {
                cmd << " ";
                // Quote arguments with spaces
                if (arg.find(' ') != std::string::npos ||
                    arg.find('"') != std::string::npos) {
                    cmd << "\"";
                    for (const char c : arg) {
                        if (c == '"' || c == '\\') {
                            cmd << '\\';
                        }
                        cmd << c;
                    }
                    cmd << "\"";
                    } else {
                        cmd << arg;
                    }
            }
            return cmd.str();
        }

        /**
         * Parses ISO 8601 date string to Timestamp.
         */
        Timestamp parse_iso_date(const std::string& date_str) {
            std::tm tm{};
            std::istringstream ss(date_str);

            ss >> (std::get_time)(&tm, "%Y-%m-%dT%H:%M:%S");
            if (ss.fail()) {
                try {
                    const auto epoch = std::stoll(date_str);
                    return std::chrono::system_clock::from_time_t(
                        epoch
                    );
                } catch (...) {
                    return {};
                }
            }

            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }

        /**
         * Trims whitespace from string.
         */
        std::string trim(const std::string& str) {
            const auto start = std::ranges::find_if_not(str,
                                                        [](unsigned char c) { return std::isspace(c); });
            const auto end = std::find_if_not(str.rbegin(), str.rend(),
                [](unsigned char c) { return std::isspace(c); }).base();
            return (start < end) ? std::string(start, end) : "";
        }

        /**
         * Splits string by delimiter.
         */
        std::vector<std::string> split(const std::string& str, const char delim) {
            std::vector<std::string> parts;
            std::istringstream ss(str);
            std::string part;
            while (std::getline(ss, part, delim)) {
                parts.push_back(part);
            }
            return parts;
        }

        /**
         * C++17-compatible starts_with helper.
         */
        bool starts_with(const std::string& str, const std::string& prefix) {
            return str.size() >= prefix.size() &&
                   str.compare(0, prefix.size(), prefix) == 0;
        }

    }  // namespace

    // =============================================================================
    // Core Git Functions
    // =============================================================================

    Result<CommandResult, Error> execute_git(
        const std::vector<std::string>& args,
        const fs::path& working_dir,
        const Duration timeout
    ) {
        if (!fs::exists(working_dir)) {
            return Result<CommandResult, Error>::failure(
                Error(ErrorCode::NotFound, "Working directory not found: " + working_dir.string())
            );
        }

        const std::string command = build_git_command(args);
        auto result = execute_command_impl(command, working_dir, timeout);

        if (result.exit_code == -2) {
            return Result<CommandResult, Error>::failure(
                Error(ErrorCode::GitError, "Git command timed out: " + command)
            );
        }

        return Result<CommandResult, Error>::success(std::move(result));
    }

    bool is_git_repository(const fs::path& dir) {
        auto result = execute_git(
            {"rev-parse", "--git-dir"},
            dir,
            std::chrono::seconds(5)
        );
        return result.is_ok() && result.value().exit_code == 0;
    }

    Result<fs::path, Error> get_repository_root(const fs::path& dir) {
        auto result = execute_git(
            {"rev-parse", "--show-toplevel"},
            dir,
            std::chrono::seconds(5)
        );

        if (result.is_err()) {
            return Result<fs::path, Error>::failure(result.error());
        }

        if (result.value().exit_code != 0) {
            return Result<fs::path, Error>::failure(
                Error(ErrorCode::GitError, "Not a git repository")
            );
        }

        return Result<fs::path, Error>::success(
            fs::path(trim(result.value().stdout_output))
        );
    }

    Result<CommitInfo, Error> parse_commit(std::string_view git_output) {
        CommitInfo info;
        std::string output(git_output);
        std::istringstream ss(output);
        std::string line;

        // Parse format: hash|short|author_name|author_email|author_date|
        //               committer_name|committer_email|commit_date|subject
        if (std::getline(ss, line)) {
            if (auto parts = split(line, '|'); parts.size() >= 9) {
                info.hash = parts[0];
                info.short_hash = parts[1];
                info.author_name = parts[2];
                info.author_email = parts[3];
                info.author_date = parse_iso_date(parts[4]);
                info.committer_name = parts[5];
                info.committer_email = parts[6];
                info.commit_date = parse_iso_date(parts[7]);
                info.subject = parts[8];
            }
        }

        std::ostringstream body;
        while (std::getline(ss, line)) {
            body << line << "\n";
        }
        info.body = body.str();

        return Result<CommitInfo, Error>::success(std::move(info));
    }

    Result<CommitInfo, Error> get_commit(
        const std::string& ref,
        const fs::path& repo_dir
    ) {
        auto result = execute_git(
            {"log", "-1",
             "--format=%H|%h|%an|%ae|%aI|%cn|%ce|%cI|%s",
             ref},
            repo_dir,
            std::chrono::seconds(10)
        );

        if (result.is_err()) {
            return Result<CommitInfo, Error>::failure(result.error());
        }

        if (result.value().exit_code != 0) {
            return Result<CommitInfo, Error>::failure(
                Error(ErrorCode::NotFound, "Commit not found: " + ref)
            );
        }

        return parse_commit(result.value().stdout_output);
    }

    Result<std::vector<CommitInfo>, Error> get_commits(
        const std::string& range,
        std::size_t max_count,
        const fs::path& repo_dir
    ) {
        std::vector<std::string> args = {
            "log",
            "--format=%H|%h|%an|%ae|%aI|%cn|%ce|%cI|%s",
            range
        };

        if (max_count > 0) {
            args.emplace_back("-n");
            args.push_back(std::to_string(max_count));
        }

        auto result = execute_git(args, repo_dir, std::chrono::seconds(30));

        if (result.is_err()) {
            return Result<std::vector<CommitInfo>, Error>::failure(result.error());
        }

        if (result.value().exit_code != 0) {
            return Result<std::vector<CommitInfo>, Error>::failure(
                Error(ErrorCode::GitError, "Failed to get commits: " + result.value().stderr_output)
            );
        }

        std::vector<CommitInfo> commits;
        std::istringstream ss(result.value().stdout_output);
        std::string line;

        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            if (auto commit_result = parse_commit(line); commit_result.is_ok()) {
                commits.push_back(std::move(commit_result.value()));
            }
        }

        return Result<std::vector<CommitInfo>, Error>::success(std::move(commits));
    }

    Result<std::string, Error> get_current_branch(const fs::path& repo_dir) {
        auto result = execute_git(
            {"rev-parse", "--abbrev-ref", "HEAD"},
            repo_dir,
            std::chrono::seconds(5)
        );

        if (result.is_err()) {
            return Result<std::string, Error>::failure(result.error());
        }

        if (result.value().exit_code != 0) {
            return Result<std::string, Error>::failure(
                Error(ErrorCode::GitError, "Failed to get current branch")
            );
        }

        return Result<std::string, Error>::success(trim(result.value().stdout_output));
    }

    Result<std::string, Error> get_head(const fs::path& repo_dir) {
        auto result = execute_git(
            {"rev-parse", "HEAD"},
            repo_dir,
            std::chrono::seconds(5)
        );

        if (result.is_err()) {
            return Result<std::string, Error>::failure(result.error());
        }

        if (result.value().exit_code != 0) {
            return Result<std::string, Error>::failure(
                Error(ErrorCode::GitError, "Failed to get HEAD")
            );
        }

        return Result<std::string, Error>::success(trim(result.value().stdout_output));
    }

    Result<bool, Error> has_uncommitted_changes(const fs::path& repo_dir) {
        auto result = execute_git(
            {"status", "--porcelain"},
            repo_dir,
            std::chrono::seconds(10)
        );

        if (result.is_err()) {
            return Result<bool, Error>::failure(result.error());
        }

        if (result.value().exit_code != 0) {
            return Result<bool, Error>::failure(
                Error(ErrorCode::GitError, "Failed to check git status")
            );
        }

        return Result<bool, Error>::success(!result.value().stdout_output.empty());
    }

    // =============================================================================
    // Blame Analyzer
    // =============================================================================

    class BlameAnalyzer : public IBlameAnalyzer {
    public:
        explicit BlameAnalyzer(fs::path repo_dir)
            : repo_dir_(std::move(repo_dir)) {}

        [[nodiscard]] Result<BlameResult, Error> blame_file(
            const fs::path& file,
            const std::string& ref
        ) const override {
            auto start_time = std::chrono::steady_clock::now();

            auto result = execute_git(
                {"blame", "--porcelain", ref, "--", file.string()},
                repo_dir_,
                std::chrono::seconds(60)
            );

            if (result.is_err()) {
                return Result<BlameResult, Error>::failure(result.error());
            }

            if (result.value().exit_code != 0) {
                return Result<BlameResult, Error>::failure(
                    Error(ErrorCode::NotFound, "Failed to blame file: " + file.string())
                );
            }

            BlameResult blame_result;
            blame_result.file = file;

            // Parse porcelain format
            std::istringstream ss(result.value().stdout_output);
            std::string line;
            BlameEntry current_entry;
            std::unordered_map<std::string, BlameEntry> commit_cache;

            while (std::getline(ss, line)) {
                if (line.empty()) continue;

                // New blame entry starts with commit hash
                if (line.size() >= 40 && std::isxdigit(static_cast<unsigned char>(line[0]))) {
                    // Parse: <hash> <orig-line> <final-line> [<count>]
                    if (auto parts = split(line, ' '); parts.size() >= 3) {
                        current_entry.commit_hash = parts[0];
                        current_entry.original_line = std::stoul(parts[1]);
                        current_entry.final_line = std::stoul(parts[2]);
                    }
                } else if (starts_with(line, "author ")) {
                    current_entry.author_name = line.substr(7);
                } else if (starts_with(line, "author-mail ")) {
                    std::string email = line.substr(12);
                    // Remove < and >
                    if (email.size() >= 2 && email.front() == '<' && email.back() == '>') {
                        email = email.substr(1, email.size() - 2);
                    }
                    current_entry.author_email = email;
                } else if (starts_with(line, "author-time ")) {
                    auto timestamp = std::stoll(line.substr(12));
                    current_entry.author_date =
                        std::chrono::system_clock::from_time_t(timestamp);
                } else if (starts_with(line, "filename ")) {
                    current_entry.original_file = line.substr(9);
                } else if (starts_with(line, "\t")) {
                    current_entry.line_content = line.substr(1);
                    blame_result.entries.push_back(current_entry);
                    blame_result.lines_per_author[current_entry.author_name]++;
                    current_entry = BlameEntry{};
                }
            }

            auto end_time = std::chrono::steady_clock::now();
            blame_result.analysis_time =
                std::chrono::duration_cast<Duration>(end_time - start_time);

            return Result<BlameResult, Error>::success(std::move(blame_result));
        }

        [[nodiscard]] Result<std::vector<BlameResult>, Error> blame_files(
            const std::vector<fs::path>& files,
            const std::string& ref
        ) const override {
            std::vector<BlameResult> results;
            results.reserve(files.size());

            for (const auto& file : files) {
                auto result = blame_file(file, ref);
                if (result.is_ok()) {
                    results.push_back(std::move(result.value()));
                }
            }

            return Result<std::vector<BlameResult>, Error>::success(std::move(results));
        }

        [[nodiscard]] Result<std::vector<AuthorStats>, Error> get_author_stats(
            const std::vector<BlameResult>& blame_results
        ) const override {
            std::unordered_map<std::string, AuthorStats> stats_map;

            for (const auto& blame : blame_results) {
                for (const auto& entry : blame.entries) {
                    auto& stats = stats_map[entry.author_email];
                    stats.name = entry.author_name;
                    stats.email = entry.author_email;
                    stats.lines_added++;

                    if (stats.first_commit == Timestamp{} ||
                        entry.author_date < stats.first_commit) {
                        stats.first_commit = entry.author_date;
                        }
                    if (entry.author_date > stats.last_commit) {
                        stats.last_commit = entry.author_date;
                    }
                }

                // Track files per author
                for (const auto& author : blame.lines_per_author | std::views::keys) {
                    auto it = std::ranges::find_if(stats_map,
                                                   [&author](const auto& pair) {
                                                       return pair.second.name == author;
                                                   });
                    if (it != stats_map.end()) {
                        it->second.files_touched++;
                    }
                }
            }

            std::vector<AuthorStats> results;
            results.reserve(stats_map.size());
            for (auto& stats : stats_map | std::views::values) {
                results.push_back(std::move(stats));
            }

            std::ranges::sort(results,
                              [](const AuthorStats& a, const AuthorStats& b) {
                                  return a.lines_added > b.lines_added;
                              });

            return Result<std::vector<AuthorStats>, Error>::success(std::move(results));
        }

    private:
        fs::path repo_dir_;
    };

    // =============================================================================
    // Bisect Runner
    // =============================================================================

    class BisectRunner : public IBisectRunner {
    public:
        explicit BisectRunner(fs::path repo_dir)
            : repo_dir_(std::move(repo_dir)), state_(BisectState::NotStarted) {}

        Result<BisectResult, Error> run(
            const std::string& good_commit,
            const std::string& bad_commit,
            TestFunction test_fn
        ) override {
            auto start_time = std::chrono::steady_clock::now();
            BisectResult result;
            result.good_commit = good_commit;
            result.bad_commit = bad_commit;

            if (auto changes_result = has_uncommitted_changes(repo_dir_); changes_result.is_ok() && changes_result.value()) {
                return Result<BisectResult, Error>::failure(
                    Error(ErrorCode::GitError, "Repository has uncommitted changes")
                );
            }

            if (auto start_result = execute_git({"bisect", "start"}, repo_dir_); start_result.is_err() || start_result.value().exit_code != 0) {
                return Result<BisectResult, Error>::failure(
                    Error(ErrorCode::InternalError, "Failed to start bisect")
                );
            }

            state_ = BisectState::InProgress;

            if (auto bad_result = execute_git({"bisect", "bad", bad_commit}, repo_dir_); bad_result.is_err() || bad_result.value().exit_code != 0) {
                abort();
                return Result<BisectResult, Error>::failure(
                    Error(ErrorCode::NotFound, "Bad commit not found: " + bad_commit)
                );
            }

            if (auto good_result = execute_git({"bisect", "good", good_commit}, repo_dir_); good_result.is_err() || good_result.value().exit_code != 0) {
                abort();
                return Result<BisectResult, Error>::failure(
                    Error(ErrorCode::NotFound, "Good commit not found: " + good_commit)
                );
            }

            while (state_ == BisectState::InProgress) {
                auto head_result = get_head(repo_dir_);
                if (head_result.is_err()) {
                    abort();
                    return Result<BisectResult, Error>::failure(head_result.error());
                }

                const std::string& current_commit = head_result.value();
                result.tested_commits.push_back(current_commit);
                result.steps_taken++;

                auto test_result = test_fn(current_commit);
                if (test_result.is_err()) {
                    abort();
                    return Result<BisectResult, Error>::failure(test_result.error());
                }

                std::string verdict = test_result.value() ? "good" : "bad";
                auto mark_result = execute_git({"bisect", verdict}, repo_dir_);

                if (mark_result.is_err()) {
                    abort();
                    return Result<BisectResult, Error>::failure(mark_result.error());
                }

                if (std::string output = mark_result.value().stdout_output; output.find("is the first bad commit") != std::string::npos) {
                    // Extract commit hash
                    std::regex hash_regex("([0-9a-f]{40}) is the first bad commit");
                    if (std::smatch match; std::regex_search(output, match, hash_regex)) {
                        result.first_bad_commit = match[1].str();
                    }
                    state_ = BisectState::Found;
                }
            }

            (void)execute_git({"bisect", "reset"}, repo_dir_);

            auto end_time = std::chrono::steady_clock::now();
            result.total_time = std::chrono::duration_cast<Duration>(end_time - start_time);
            result.state = state_;

            return Result<BisectResult, Error>::success(std::move(result));
        }

        Result<void, Error> abort() override {
            auto result = execute_git({"bisect", "reset"}, repo_dir_);
            state_ = BisectState::Aborted;

            if (result.is_err()) {
                return Result<void, Error>::failure(result.error());
            }

            return Result<void, Error>::success();
        }

        [[nodiscard]] BisectState state() const override {
            return state_;
        }

    private:
        fs::path repo_dir_;
        BisectState state_;
    };

    // =============================================================================
    // Commit Tracker
    // =============================================================================

    class CommitTracker : public ICommitTracker {
    public:
        explicit CommitTracker(fs::path db_path)
            : db_path_(std::move(db_path)) {
            load_database();
        }

        Result<void, Error> record(
            const std::string& commit_hash,
            const Duration build_time,
            const analyzers::AnalysisResult&
        ) override {
            build_times_[commit_hash] = build_time;
            save_database();
            return Result<void, Error>::success();
        }

        Result<Duration, Error> get_build_time(
            const std::string& commit_hash
        ) const override {
            const auto it = build_times_.find(commit_hash);
            if (it == build_times_.end()) {
                return Result<Duration, Error>::failure(
                    Error(ErrorCode::NotFound, "No build time recorded for commit")
                );
            }
            return Result<Duration, Error>::success(it->second);
        }

        Result<std::vector<std::pair<CommitInfo, Duration>>, Error>
        get_history(const std::size_t limit) const override {
            std::vector<std::pair<CommitInfo, Duration>> history;

            auto commits_result = get_commits("HEAD", limit, db_path_.parent_path());
            if (commits_result.is_err()) {
                return Result<std::vector<std::pair<CommitInfo, Duration>>, Error>::failure(
                    commits_result.error()
                );
            }

            for (const auto& commit : commits_result.value()) {
                auto it = build_times_.find(commit.hash);
                Duration build_time = (it != build_times_.end()) ? it->second : Duration::zero();
                history.emplace_back(commit, build_time);
            }

            return Result<std::vector<std::pair<CommitInfo, Duration>>, Error>::success(
                std::move(history)
            );
        }

        Result<std::vector<std::pair<CommitInfo, Duration>>, Error>
        find_regressions(const double threshold_percent) const override {
            std::vector<std::pair<CommitInfo, Duration>> regressions;

            auto history_result = get_history(100);
            if (history_result.is_err()) {
                return Result<std::vector<std::pair<CommitInfo, Duration>>, Error>::failure(
                    history_result.error()
                );
            }

            const auto& history = history_result.value();
            for (std::size_t i = 1; i < history.size(); ++i) {
                const auto prev_time = history[i].second.count();
                const auto curr_time = history[i - 1].second.count();

                if (prev_time > 0) {
                    const double change_percent =
                        100.0 * static_cast<double>(curr_time - prev_time) /
                        static_cast<double>(prev_time);

                    if (change_percent > threshold_percent) {
                        regressions.push_back(history[i - 1]);
                    }
                }
            }

            return Result<std::vector<std::pair<CommitInfo, Duration>>, Error>::success(
                std::move(regressions)
            );
        }

    private:
        void load_database() {
            if (!fs::exists(db_path_)) return;

            std::ifstream file(db_path_);
            std::string line;
            while (std::getline(file, line)) {
                if (auto parts = split(line, ','); parts.size() >= 2) {
                    build_times_[parts[0]] = Duration(std::stoll(parts[1]));
                }
            }
        }

        void save_database() {
            std::ofstream file(db_path_);
            for (const auto& [hash, time] : build_times_) {
                file << hash << "," << time.count() << "\n";
            }
        }

        fs::path db_path_;
        std::unordered_map<std::string, Duration> build_times_;
    };

    // =============================================================================
    // Branch Comparator
    // =============================================================================

    class BranchComparator : public IBranchComparator {
    public:
        explicit BranchComparator(fs::path repo_dir)
            : repo_dir_(std::move(repo_dir)) {}

        [[nodiscard]] Result<BranchComparison, Error> compare(
            const std::string& base_branch,
            const std::string& compare_branch
        ) const override {
            BranchComparison result;
            result.base_branch = base_branch;
            result.compare_branch = compare_branch;

            auto merge_base_result = execute_git(
                {"merge-base", base_branch, compare_branch},
                repo_dir_
            );

            if (merge_base_result.is_err() ||
                merge_base_result.value().exit_code != 0) {
                return Result<BranchComparison, Error>::failure(
                    Error(ErrorCode::NotFound, "Cannot find merge base")
                );
                }

            result.merge_base = trim(merge_base_result.value().stdout_output);

            // Count commits ahead/behind
            auto ahead_result = execute_git(
                {"rev-list", "--count", base_branch + ".." + compare_branch},
                repo_dir_
            );
            if (ahead_result.is_ok() && ahead_result.value().exit_code == 0) {
                result.commits_ahead = std::stoul(trim(ahead_result.value().stdout_output));
            }

            auto behind_result = execute_git(
                {"rev-list", "--count", compare_branch + ".." + base_branch},
                repo_dir_
            );
            if (behind_result.is_ok() && behind_result.value().exit_code == 0) {
                result.commits_behind = std::stoul(trim(behind_result.value().stdout_output));
            }

            auto diff_result = execute_git(
                {"diff", "--name-status", result.merge_base, compare_branch},
                repo_dir_
            );

            if (diff_result.is_ok() && diff_result.value().exit_code == 0) {
                std::istringstream ss(diff_result.value().stdout_output);
                std::string line;
                while (std::getline(ss, line)) {
                    if (line.empty()) continue;
                    char status = line[0];
                    std::string file = trim(line.substr(1));

                    switch (status) {
                    case 'A':
                        result.files_added.push_back(file);
                        break;
                    case 'M':
                        result.files_modified.push_back(file);
                        break;
                    case 'D':
                        result.files_deleted.push_back(file);
                        break;
                    default:
                        break;
                    }
                }
            }

            return Result<BranchComparison, Error>::success(std::move(result));
        }

        [[nodiscard]] Result<BranchComparison, Error> estimate_pr_impact(
            const std::string& pr_branch
        ) const override {
            auto default_result = execute_git(
                {"symbolic-ref", "refs/remotes/origin/HEAD"},
                repo_dir_
            );

            std::string base_branch = "main";
            if (default_result.is_ok() && default_result.value().exit_code == 0) {
                const std::string ref = trim(default_result.value().stdout_output);
                if (const auto pos = ref.rfind('/'); pos != std::string::npos) {
                    base_branch = ref.substr(pos + 1);
                }
            }

            return compare(base_branch, pr_branch);
        }

    private:
        fs::path repo_dir_;
    };

    // =============================================================================
    // Hook Manager
    // =============================================================================

    class HookManager : public IHookManager {
    public:
        explicit HookManager(fs::path repo_dir)
            : repo_dir_(std::move(repo_dir)) {
            hooks_dir_ = repo_dir_ / ".git" / "hooks";
        }

        Result<void, Error> install(HookType type) override {
            auto hook_name = std::string(hook_type_to_string(type));
            fs::path hook_path = hooks_dir_ / hook_name;

            if (fs::exists(hook_path)) {
                std::ifstream file(hook_path);
                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());

                if (content.find("# BHA Hook") != std::string::npos) {
                    return Result<void, Error>::success();
                }

                fs::rename(hook_path, hook_path.string() + ".bak");
            }

            std::ofstream hook_file(hook_path);
            hook_file << "#!/bin/sh\n";
            hook_file << "# BHA Hook - Build Hotspot Analyzer\n";
            hook_file << "# Version: 1.0.0\n";
            hook_file << "# Type: " << hook_name << "\n\n";

            switch (type) {
            case HookType::PreCommit:
                hook_file << "# Run BHA pre-commit check\n";
                hook_file << "bha check --pre-commit 2>/dev/null\n";
                hook_file << "exit_code=$?\n";
                hook_file << "if [ $exit_code -eq 1 ]; then\n";
                hook_file << "    echo \"[BHA] Warning: Build time may increase\"\n";
                hook_file << "fi\n";
                break;

            case HookType::PrePush:
                hook_file << "# Run BHA pre-push analysis\n";
                hook_file << "bha analyze --quick 2>/dev/null\n";
                break;

            case HookType::PostMerge:
                hook_file << "# Record build time after merge\n";
                hook_file << "bha record 2>/dev/null\n";
                break;

            case HookType::PostCheckout:
                hook_file << "# Update BHA cache after checkout\n";
                hook_file << "bha cache --update 2>/dev/null &\n";
                break;

            case HookType::PrepareCommitMsg:
                hook_file << "# Add build impact to commit message\n";
                hook_file << "bha impact --commit-msg \"$1\" 2>/dev/null\n";
                break;
            }

            hook_file << "\nexit 0\n";
            hook_file.close();

            // Make executable
#ifndef _WIN32
            fs::permissions(hook_path,
                fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                fs::perm_options::add);
#endif

            return Result<void, Error>::success();
        }

        Result<void, Error> uninstall(HookType type) override {
            auto hook_name = std::string(hook_type_to_string(type));
            fs::path hook_path = hooks_dir_ / hook_name;

            if (!fs::exists(hook_path)) {
                return Result<void, Error>::success();
            }

            std::ifstream file(hook_path);
            std::string content((std::istreambuf_iterator(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            if (content.find("# BHA Hook") == std::string::npos) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::GitError, "Hook is not a BHA hook")
                );
            }

            fs::remove(hook_path);

            // Restore backup if exists
            if (fs::path backup_path = hook_path.string() + ".bak"; fs::exists(backup_path)) {
                fs::rename(backup_path, hook_path);
            }

            return Result<void, Error>::success();
        }

        [[nodiscard]] Result<HookStatus, Error> status(HookType type) const override {
            HookStatus result;
            result.type = type;

            auto hook_name = std::string(hook_type_to_string(type));
            result.path = hooks_dir_ / hook_name;
            result.installed = fs::exists(result.path);

            if (result.installed) {
                std::ifstream file(result.path);
                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());

                result.is_bha_hook = content.find("# BHA Hook") != std::string::npos;

                std::regex version_regex("# Version: ([0-9.]+)");
                if (std::smatch match; std::regex_search(content, match, version_regex)) {
                    result.version = match[1].str();
                }
            }

            return Result<HookStatus, Error>::success(std::move(result));
        }

        [[nodiscard]] Result<std::vector<HookStatus>, Error> all_statuses() const override {
            std::vector<HookStatus> statuses;

            for (const auto type : {
                HookType::PreCommit,
                HookType::PrePush,
                HookType::PostMerge,
                HookType::PostCheckout,
                HookType::PrepareCommitMsg
            }) {
                if (auto result = status(type); result.is_ok()) {
                    statuses.push_back(std::move(result.value()));
                }
            }

            return Result<std::vector<HookStatus>, Error>::success(std::move(statuses));
        }

    private:
        fs::path repo_dir_;
        fs::path hooks_dir_;
    };

    // =============================================================================
    // Hook Type Conversion
    // =============================================================================

    std::string_view hook_type_to_string(HookType type) {
        switch (type) {
        case HookType::PreCommit: return "pre-commit";
        case HookType::PrePush: return "pre-push";
        case HookType::PostMerge: return "post-merge";
        case HookType::PostCheckout: return "post-checkout";
        case HookType::PrepareCommitMsg: return "prepare-commit-msg";
        }
        return "unknown";
    }

    std::optional<HookType> string_to_hook_type(std::string_view str) {
        if (str == "pre-commit") return HookType::PreCommit;
        if (str == "pre-push") return HookType::PrePush;
        if (str == "post-merge") return HookType::PostMerge;
        if (str == "post-checkout") return HookType::PostCheckout;
        if (str == "prepare-commit-msg") return HookType::PrepareCommitMsg;
        return std::nullopt;
    }

    // =============================================================================
    // Factory Functions
    // =============================================================================

    std::unique_ptr<IBlameAnalyzer> create_blame_analyzer(const fs::path& repo_dir) {
        return std::make_unique<BlameAnalyzer>(repo_dir);
    }

    std::unique_ptr<IBisectRunner> create_bisect_runner(const fs::path& repo_dir) {
        return std::make_unique<BisectRunner>(repo_dir);
    }

    std::unique_ptr<ICommitTracker> create_commit_tracker(const fs::path& db_path) {
        return std::make_unique<CommitTracker>(db_path);
    }

    std::unique_ptr<IBranchComparator> create_branch_comparator(const fs::path& repo_dir) {
        return std::make_unique<BranchComparator>(repo_dir);
    }

    std::unique_ptr<IHookManager> create_hook_manager(const fs::path& repo_dir) {
        return std::make_unique<HookManager>(repo_dir);
    }
} // bha::git