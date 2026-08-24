#include "bha/analyzers/module_analyzer.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace bha::analyzers::test {
    TEST(ModuleAnalyzerTest, ReportsExactResolvedAndUnresolvedDependencies) {
        BuildTrace trace;
        trace.module_dependency_graph = ModuleDependencyGraph{};

        ModuleDependencyRule module;
        module.primary_output = "M.o";
        module.provides.push_back({"M", "M.cppm", true});

        ModuleDependencyRule consumer;
        consumer.primary_output = "User.o";
        consumer.provides.push_back({"User", std::nullopt, std::nullopt});
        consumer.requirements.push_back({"M", std::nullopt, std::nullopt});

        ModuleDependencyRule unresolved;
        unresolved.primary_output = "Broken.o";
        unresolved.provides.push_back({"Broken", std::nullopt, std::nullopt});
        unresolved.requirements.push_back({"Missing", std::nullopt, std::nullopt});

        trace.module_dependency_graph->rules = {module, consumer, unresolved};
        trace.module_dependency_graph->metric_capabilities.push_back({
            "module.dependencies",
            MetricProvenance{EvidenceKind::Observed, "clang-scan-deps", "", "-format=p1689", "build",
                             TimingDomain::None, TimingAggregation::None, ""}
        });

        ModuleAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().modules;
        EXPECT_EQ(analysis.rules, 3u);
        EXPECT_EQ(analysis.provided_modules, 3u);
        EXPECT_EQ(analysis.required_modules, 2u);
        EXPECT_EQ(analysis.resolved_dependencies, 1u);
        EXPECT_EQ(analysis.unresolved_dependencies, 1u);
        EXPECT_EQ(analysis.unowned_dependencies, 0u);
        ASSERT_EQ(analysis.dependencies.size(), 1u);
        const std::pair<std::string, std::string> expected_edge{"M", "User"};
        EXPECT_EQ(analysis.dependencies.front(), expected_edge);

        const auto capability = std::ranges::find(
            analysis.metric_capabilities,
            "module.dependency_graph",
            &MetricCapability::metric
        );
        ASSERT_NE(capability, analysis.metric_capabilities.end());
        EXPECT_EQ(capability->provenance.evidence, EvidenceKind::Derived);
        EXPECT_FALSE(capability->provenance.limitation.empty());
    }

    TEST(ModuleAnalyzerTest, FailsClosedForRequirementsWithoutOwningRule) {
        BuildTrace trace;
        trace.module_dependency_graph = ModuleDependencyGraph{};

        ModuleDependencyRule consumer;
        consumer.primary_output = "User.o";
        consumer.requirements.push_back({"M", std::nullopt, std::nullopt});
        trace.module_dependency_graph->rules.push_back(consumer);

        ModuleAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().modules;
        EXPECT_EQ(analysis.required_modules, 1u);
        EXPECT_EQ(analysis.resolved_dependencies, 0u);
        EXPECT_EQ(analysis.unresolved_dependencies, 0u);
        EXPECT_EQ(analysis.unowned_dependencies, 1u);
        EXPECT_TRUE(analysis.dependencies.empty());
    }

    TEST(ModuleAnalyzerTest, FailsClosedForAmbiguousModuleOwnership) {
        BuildTrace trace;
        trace.module_dependency_graph = ModuleDependencyGraph{};

        ModuleDependencyRule first_owner;
        first_owner.primary_output = "First.o";
        first_owner.provides.push_back({"Shared", std::nullopt, std::nullopt});

        ModuleDependencyRule second_owner;
        second_owner.primary_output = "Second.o";
        second_owner.provides.push_back({"Shared", std::nullopt, std::nullopt});

        ModuleDependencyRule consumer;
        consumer.primary_output = "Consumer.o";
        consumer.provides.push_back({"Consumer", std::nullopt, std::nullopt});
        consumer.requirements.push_back({"Shared", std::nullopt, std::nullopt});

        trace.module_dependency_graph->rules = {first_owner, second_owner, consumer};

        ModuleAnalyzer analyzer;
        const auto result = analyzer.analyze(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value().modules;
        EXPECT_EQ(analysis.resolved_dependencies, 0u);
        EXPECT_EQ(analysis.unresolved_dependencies, 1u);
        EXPECT_TRUE(analysis.dependencies.empty());
        const auto capability = std::ranges::find(
            analysis.metric_capabilities,
            "module.dependency_graph",
            &MetricCapability::metric
        );
        ASSERT_NE(capability, analysis.metric_capabilities.end());
        EXPECT_FALSE(capability->provenance.limitation.empty());
    }
}  // namespace bha::analyzers::test
