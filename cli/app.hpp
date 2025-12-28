//
// Created by gregorian on 15/12/2025.
//

#ifndef APP_HPP
#define APP_HPP

#include "cli_parser.hpp"
#include "bha/core/types.h"
#include "bha/core/result.h"
#include "bha/storage/database.h"
#include "bha/security/input_validator.h"
#include "bha/security/resource_limiter.h"
#include "bha/security/anonymizer.h"
#include "bha/analysis/analysis_engine.h"
#include <memory>

namespace bha::cli {

    class App {
    public:
        explicit App(Options options);
        ~App() = default;

        int run();

    private:
        int run_init() const;
        int run_build() const;
        int run_analyze();
        int run_compare() const;
        int run_export() const;
        int run_dashboard() const;
        int run_history();
        int run_clean();
        int run_list();
        int run_trends();
        int run_ci_check();
        int run_ci_report();
        int run_ci_badge();
        int run_watch();
        int run_blame();
        int run_budget();
        int run_optimize();
        int run_targets() const;
        int run_diff();
        int run_profile() const;

        core::Result<core::BuildTrace> load_trace(const std::string& file_path) const;
        core::Result<core::BuildTrace> parse_trace_file(const std::string& file_path) const;
        static core::Result<std::vector<std::string>> auto_find_trace_files();
        static core::Result<std::string> get_latest_trace_file();

        void print_analysis_summary(const core::BuildTrace& trace) const;
        void print_suggestions(const std::vector<core::Suggestion>& suggestions) const;
        static void print_comparison(const core::ComparisonReport& report);
        static void print_comparison_json(const core::ComparisonReport& report);

        static void populate_metrics_from_analysis(core::BuildTrace& trace, const analysis::AnalysisReport& report);
        static core::ComparisonReport create_comparison_report(const core::BuildTrace& baseline, const core::BuildTrace& current);

        core::Result<void> validate_inputs() const;
        core::Result<void> initialize_storage();

        void apply_anonymization(core::BuildTrace& trace) const;
        void check_resource_limits() const;

        Options options_;
        std::unique_ptr<storage::Database> database_{};
        std::unique_ptr<security::InputValidator> validator_{};
        std::unique_ptr<security::ResourceLimiter> limiter_{};
        std::unique_ptr<security::Anonymizer> anonymizer_{};
    };

} // namespace bha::cli

#endif //APP_HPP
