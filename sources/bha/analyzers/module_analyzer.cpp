// Created by gregorian-rayne on 8/22/26.

#include "bha/analyzers/module_analyzer.hpp"
#include "bha/utils/numeric_utils.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace bha::analyzers {
    namespace {

        void add_capability(ModuleAnalysisResult& analysis, MetricCapability capability) {
            const auto existing = std::ranges::find(
                analysis.metric_capabilities,
                capability.metric,
                &MetricCapability::metric
            );
            if (existing == analysis.metric_capabilities.end()) {
                analysis.metric_capabilities.push_back(std::move(capability));
            }
        }

        MetricCapability derived_capability(
            const std::string_view metric,
            const std::string_view limitation = {}
        ) {
            MetricCapability capability;
            capability.metric = metric;
            capability.provenance.evidence = EvidenceKind::Derived;
            capability.provenance.producer = "ModuleAnalyzer";
            capability.provenance.capture_mode = "-format=p1689";
            capability.provenance.scope = "build";
            capability.provenance.timing_domain = TimingDomain::None;
            capability.provenance.timing_aggregation = TimingAggregation::None;
            capability.provenance.limitation = limitation;
            return capability;
        }

    }  // namespace

    Result<AnalysisResult, Error> ModuleAnalyzer::analyze(
        const BuildTrace& trace,
        [[maybe_unused]] const AnalysisOptions& options
    ) const {
        AnalysisResult result;
        if (!trace.module_dependency_graph.has_value()) {
            return Result<AnalysisResult, Error>::success(std::move(result));
        }

        const auto& graph = *trace.module_dependency_graph;
        auto& analysis = result.modules;
        analysis.rules = graph.rules.size();
        const auto add_count = [](std::size_t& total, const std::size_t value) {
            const auto sum = utils::checked_add(total, value);
            if (!sum.has_value()) {
                return false;
            }
            total = *sum;
            return true;
        };

        std::unordered_map<std::string, std::size_t> owners;
        std::unordered_set<std::string> ambiguous_owners;
        for (std::size_t rule_index = 0; rule_index < graph.rules.size(); ++rule_index) {
            const auto& rule = graph.rules[rule_index];
            if (!add_count(analysis.provided_modules, rule.provides.size())) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Provided module count overflowed")
                );
            }
            for (const auto& provided : rule.provides) {
                if (!owners.emplace(provided.logical_name, rule_index).second) {
                    ambiguous_owners.insert(provided.logical_name);
                }
            }
        }

        for (const auto& rule : graph.rules) {
            if (!add_count(analysis.required_modules, rule.requirements.size())) {
                return Result<AnalysisResult, Error>::failure(
                    Error::analysis_error("Required module count overflowed")
                );
            }
            if (rule.provides.empty()) {
                if (!add_count(analysis.unowned_dependencies, rule.requirements.size())) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Unowned module dependency count overflowed")
                    );
                }
                continue;
            }

            for (const auto& required : rule.requirements) {
                if (!owners.contains(required.logical_name) ||
                    ambiguous_owners.contains(required.logical_name)) {
                    if (!add_count(analysis.unresolved_dependencies, 1)) {
                        return Result<AnalysisResult, Error>::failure(
                            Error::analysis_error("Unresolved module dependency count overflowed")
                        );
                    }
                    continue;
                }
                if (!add_count(analysis.resolved_dependencies, 1)) {
                    return Result<AnalysisResult, Error>::failure(
                        Error::analysis_error("Resolved module dependency count overflowed")
                    );
                }
                for (const auto& provided : rule.provides) {
                    analysis.dependencies.emplace_back(
                        required.logical_name,
                        provided.logical_name
                    );
                }
            }
        }

        for (const auto& capability : graph.metric_capabilities) {
            add_capability(analysis, capability);
        }
        const bool complete = analysis.unresolved_dependencies == 0 &&
                              analysis.unowned_dependencies == 0;
        add_capability(
            analysis,
            derived_capability(
                "module.dependency_graph",
                complete
                    ? std::string{}
                    : "Some P1689 requirements have no unique producer-provided owner; no path or command inference was used"
            )
        );
        add_capability(analysis, derived_capability("module.rule_count"));
        add_capability(analysis, derived_capability("module.requirement_count"));

        return Result<AnalysisResult, Error>::success(std::move(result));
    }

    void register_module_analyzer() {
        AnalyzerRegistry::instance().register_analyzer(
            std::make_unique<ModuleAnalyzer>()
        );
    }

}  // namespace bha::analyzers
