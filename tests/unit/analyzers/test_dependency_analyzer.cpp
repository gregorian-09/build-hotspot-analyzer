//
// Created by gregorian-rayne on 12/30/25.
//


#include "bha/analyzers/dependency_analyzer.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>

namespace bha::analyzers
{
    class DependencyAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<DependencyAnalyzer>();
        }

        static BuildTrace create_test_trace() {
            BuildTrace trace;
            trace.id = "test-trace";

            CompilationUnit unit1;
            unit1.source_file = "/src/main.cpp";
            unit1.includes = {
                {"/include/header.h", std::chrono::milliseconds(100), 1, {}, {}},
                {"/include/utils.h", std::chrono::milliseconds(50), 1, {}, {}},
            };

            CompilationUnit unit2;
            unit2.source_file = "/src/other.cpp";
            unit2.includes = {
                {"/include/header.h", std::chrono::milliseconds(100), 1, {}, {}},
                {"/include/common.h", std::chrono::milliseconds(80), 2, {}, {}},
            };

            trace.units = {unit1, unit2};
            return trace;
        }

        std::unique_ptr<DependencyAnalyzer> analyzer_;
    };

    TEST_F(DependencyAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "DependencyAnalyzer");
    }

    TEST_F(DependencyAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace empty_trace;
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(empty_trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().dependencies.headers.empty());
    }

    TEST_F(DependencyAnalyzerTest, AnalyzeBasicTrace) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& deps = result.value().dependencies;

        EXPECT_EQ(deps.unique_headers, 3u);
        EXPECT_EQ(deps.total_includes, 4u);
    }

    TEST_F(DependencyAnalyzerTest, HeaderIncludedMultipleTimes) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        auto& headers = result.value().dependencies.headers;

        const auto it = std::ranges::find_if(headers,
                                             [](const auto& h) {
                                                 return h.path.filename() == "header.h";
                                             });

        ASSERT_NE(it, headers.end());
        EXPECT_EQ(it->inclusion_count, 2u);
        EXPECT_EQ(it->including_files, 2u);
    }

    TEST_F(DependencyAnalyzerTest, HeadersSortedByImpact) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& headers = result.value().dependencies.headers;

        for (std::size_t i = 1; i < headers.size(); ++i) {
            EXPECT_GE(headers[i - 1].impact_score, headers[i].impact_score);
        }
    }

    TEST_F(DependencyAnalyzerTest, StabilityFieldsInitialized) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());

        for (const auto& headers = result.value().dependencies.headers; const auto& header : headers) {
            EXPECT_GE(header.modification_count, 0u);
            EXPECT_FALSE(header.is_external);
        }
    }

    TEST_F(DependencyAnalyzerTest, ExternalHeadersDetected) {
        BuildTrace trace;
        trace.id = "test-trace-external";

        CompilationUnit unit;
        unit.source_file = "/src/main.cpp";
        unit.includes = {
            {"/usr/include/vector", std::chrono::milliseconds(50), 1, {}, {}},
            {"/opt/include/lib.h", std::chrono::milliseconds(30), 1, {}, {}},
            {"third_party/json.hpp", std::chrono::milliseconds(100), 1, {}, {}},
            {"src/internal.h", std::chrono::milliseconds(20), 1, {}, {}},
        };

        trace.units = {unit};
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& headers = result.value().dependencies.headers;

        const auto external_count = std::count_if(headers.begin(), headers.end(),
                                            [](const auto& h) { return h.is_external; });

        EXPECT_GE(external_count, 2);
    }

    TEST_F(DependencyAnalyzerTest, CircularDependencyDetection) {
        BuildTrace trace;
        trace.id = "test-circular";

        CompilationUnit unit1;
        unit1.source_file = "/src/a.cpp";
        unit1.includes = {
            {"/include/a.h", std::chrono::milliseconds(50), 1, {}, {}},
            {"/include/b.h", std::chrono::milliseconds(50), 1, {}, {}},
        };

        CompilationUnit unit2;
        unit2.source_file = "/include/b.h";
        unit2.includes = {
            {"/include/a.h", std::chrono::milliseconds(50), 1, {}, {}},
        };

        trace.units = {unit1, unit2};
        constexpr AnalysisOptions options;

        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
    }

    TEST_F(DependencyAnalyzerTest, DetectsCircularDependenciesFromHeaders) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-deps-cycle-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path a_header = temp_dir / "a.h";
        const fs::path b_header = temp_dir / "b.h";
        const fs::path source = temp_dir / "main.cpp";

        {
            std::ofstream out(a_header);
            ASSERT_TRUE(out.good());
            out << "#pragma once\n";
            out << "struct B;\n";
            out << "#include \"b.h\"\n";
            out << "struct A { B* ptr; };\n";
        }
        {
            std::ofstream out(b_header);
            ASSERT_TRUE(out.good());
            out << "#pragma once\n";
            out << "struct A;\n";
            out << "#include \"a.h\"\n";
            out << "struct B { A* ptr; };\n";
        }
        {
            std::ofstream out(source);
            ASSERT_TRUE(out.good());
            out << "#include \"a.h\"\n";
            out << "int main() { return 0; }\n";
        }

        BuildTrace trace;
        trace.id = "test-header-cycle";

        CompilationUnit unit;
        unit.source_file = source;
        unit.includes = {
            {a_header, std::chrono::milliseconds(10), 1, {}, {}},
            {b_header, std::chrono::milliseconds(10), 2, {}, {}},
        };
        trace.units = {unit};

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& cycles = result.value().dependencies.circular_dependencies;
        EXPECT_FALSE(cycles.empty());

        const bool has_expected_pair = std::any_of(cycles.begin(), cycles.end(), [&](const auto& edge) {
            const fs::path from = edge.first.lexically_normal();
            const fs::path to = edge.second.lexically_normal();
            return (from == a_header.lexically_normal() && to == b_header.lexically_normal()) ||
                   (from == b_header.lexically_normal() && to == a_header.lexically_normal());
        });
        EXPECT_TRUE(has_expected_pair);

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }
}
