#include <algorithm>
#include <gtest/gtest.h>

#include "bha/analyzers/all_analyzers.hpp"

namespace bha::analyzers::test {

    class AnalyzerRegistryTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            register_all_analyzers();
        }
    };

    TEST_F(AnalyzerRegistryTest, RetainsPerFileMetricsWithoutBuildWallTime) {

        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = "main.cpp";
        unit.metrics.total_time = std::chrono::seconds(3);
        trace.units.push_back(std::move(unit));

        const auto result = run_full_analysis(trace, {});

        ASSERT_TRUE(result.is_ok());
        const auto& analysis = result.value();
        EXPECT_EQ(analysis.performance.total_build_time, Duration::zero());
        EXPECT_EQ(analysis.performance.total_files, 1u);
        EXPECT_EQ(analysis.performance.sequential_time, std::chrono::seconds(3));
        ASSERT_EQ(analysis.files.size(), 1u);
        EXPECT_EQ(analysis.files.front().file, "main.cpp");
        EXPECT_EQ(analysis.files.front().compile_time, std::chrono::seconds(3));
    }

    TEST_F(AnalyzerRegistryTest, UsesDedicatedPerformanceSummary) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(10);

        CompilationUnit unit;
        unit.source_file = "main.cpp";
        unit.metrics.total_time = std::chrono::seconds(3);
        trace.units.push_back(std::move(unit));

        const auto result = run_full_analysis(trace, {});

        ASSERT_TRUE(result.is_ok());
        EXPECT_DOUBLE_EQ(result.value().performance.parallelism_efficiency, 0.3);
    }

    TEST_F(AnalyzerRegistryTest, RetainsRoleScopedCapabilities) {
        BuildTrace trace;
        trace.build_session = BuildSession{};

        BuildCommandEvent configure;
        configure.id = "configure";
        configure.role = BuildStepRole::Configure;
        configure.start_time = Timestamp(std::chrono::system_clock::duration(std::chrono::seconds(0)));
        configure.duration = std::chrono::seconds(1);
        configure.timing_provenance.evidence = EvidenceKind::Observed;

        BuildCommandEvent test;
        test.id = "test";
        test.role = BuildStepRole::Test;
        test.start_time = Timestamp(std::chrono::system_clock::duration(std::chrono::seconds(1)));
        test.duration = std::chrono::seconds(1);
        test.timing_provenance.evidence = EvidenceKind::Observed;

        trace.build_session->commands = {configure, test};

        const auto result = run_full_analysis(trace, {});

        ASSERT_TRUE(result.is_ok());
        std::vector<std::string> scopes;
        for (const auto& capability : result.value().metric_capabilities) {
            if (capability.metric == "build.step.wall_time") {
                scopes.push_back(capability.provenance.scope);
            }
        }

        ASSERT_EQ(scopes.size(), 2u);
        EXPECT_NE(std::ranges::find(scopes, "role:configure"), scopes.end());
        EXPECT_NE(std::ranges::find(scopes, "role:test"), scopes.end());
    }

}  // namespace bha::analyzers::test
