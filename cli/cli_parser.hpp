//
// Created by gregorian on 15/12/2025.
//

#ifndef CLI_PARSER_HPP
#define CLI_PARSER_HPP

#include <string>
#include <vector>
#include <optional>

namespace bha::cli {

    enum class Command {
        INIT,
        BUILD,
        ANALYZE,
        COMPARE,
        EXPORT,
        DASHBOARD,
        LIST,
        TRENDS,
        HISTORY,
        CLEAN,
        CI_CHECK,
        CI_REPORT,
        CI_BADGE,
        WATCH,
        BLAME,
        BUDGET,
        OPTIMIZE,
        TARGETS,
        DIFF,
        PROFILE,
        HELP,
        VERSION,
        UNKNOWN
    };

    struct Options {
        mutable Command command = Command::UNKNOWN;

        std::vector<std::string> input_files;
        std::string output_file;
        std::string format;
        std::string database_path = "./bha.db";

        int port = 8080;
        int top_n = 20;
        int days = 30;

        double threshold_ms = 1000.0;
        double min_confidence = 0.5;
        double ci_threshold_percent = 5.0;
        double budget_total_ms = 0.0;
        double alert_threshold_percent = 10.0;

        bool verbose = false;
        bool anonymize = false;
        bool no_suggestions = false;
        bool clean_build = false;
        bool interactive = false;
        bool json_output = false;
        bool auto_detect = true;
        bool force = false;
        bool apply_optimizations = false;
        bool show_critical_path = false;
        bool analyze_templates = false;
        bool include_graph = false;

        std::optional<std::string> baseline;
        std::optional<std::string> compiler_type;
        std::optional<std::string> build_system;
        std::optional<std::string> build_target;
        std::optional<std::string> project_dir;
        std::optional<std::string> ci_format;
        std::optional<std::string> badge_output;
        std::optional<std::string> budget_action;
        std::optional<std::string> budget_file;
        std::optional<std::string> git_ref;
        std::optional<std::string> author;
    };

    class CliParser {
    public:
        static Options parse(int argc, char** argv);
        static void print_help();
        static void print_command_help(Command cmd);
        static void print_version();

    private:
        static Command parse_command(const std::string& cmd);
        static Options parse_init_options(int argc, char** argv, int& index);
        static Options parse_build_options(int argc, char** argv, int& index);
        static Options parse_analyze_options(int argc, char** argv, int& index);
        static Options parse_compare_options(int argc, char** argv, int& index);
        static Options parse_export_options(int argc, char** argv, int& index);
        static Options parse_dashboard_options(int argc, char** argv, int& index);
        static Options parse_list_options(int argc, char** argv, int& index);
        static Options parse_trends_options(int argc, char** argv, int& index);
        static Options parse_history_options(int argc, char** argv, int& index);
        static Options parse_clean_options(int argc, char** argv, int& index);
        static Options parse_ci_check_options(int argc, char** argv, int& index);
        static Options parse_ci_report_options(int argc, char** argv, int& index);
        static Options parse_ci_badge_options(int argc, char** argv, int& index);
        static Options parse_watch_options(int argc, char** argv, int& index);
        static Options parse_blame_options(int argc, char** argv, int& index);
        static Options parse_budget_options(int argc, char** argv, int& index);
        static Options parse_optimize_options(int argc, char** argv, int& index);
        static Options parse_targets_options(int argc, char** argv, int& index);
        static Options parse_diff_options(int argc, char** argv, int& index);
        static Options parse_profile_options(int argc, char** argv, int& index);
    };

} // namespace bha::cli

#endif //CLI_PARSER_HPP
