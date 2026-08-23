//
// Created by gregorian-rayne on 12/30/25.
//


#include "bha/analyzers/dependency_analyzer.hpp"
#include "bha/utils/include_parse_utils.hpp"

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
                {"/include/header.h", std::chrono::milliseconds(100), 1, {}, {}, std::nullopt},
                {"/include/utils.h", std::chrono::milliseconds(50), 1, {}, {}, std::nullopt},
            };

            CompilationUnit unit2;
            unit2.source_file = "/src/other.cpp";
            unit2.includes = {
                {"/include/header.h", std::chrono::milliseconds(100), 1, {}, {}, std::nullopt},
                {"/include/common.h", std::chrono::milliseconds(80), 2, {}, {}, std::nullopt},
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

    TEST_F(DependencyAnalyzerTest, PreservesObservedHeaderSelfTime) {
        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = "/src/main.cpp";
        unit.includes = {
            {"/include/header.h", std::chrono::milliseconds(100), 0, {}, {},
             std::chrono::milliseconds(60)},
            {"/include/header.h", std::chrono::milliseconds(40), 1, {}, {},
             std::chrono::milliseconds(20)},
        };
        trace.units = {unit};

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& deps = result.value().dependencies;
        ASSERT_EQ(deps.headers.size(), 1u);
        ASSERT_TRUE(deps.headers.front().self_parse_time.has_value());
        EXPECT_EQ(*deps.headers.front().self_parse_time, std::chrono::milliseconds(80));
        ASSERT_EQ(deps.metric_capabilities.size(), 2u);
        EXPECT_EQ(deps.metric_capabilities[0].metric, "frontend.header.consumer_fanout");
        EXPECT_EQ(deps.metric_capabilities[1].metric, "frontend.source_self_time");
    }

    TEST_F(DependencyAnalyzerTest, HeadersSortedByObservedParseTime) {
        const auto trace = create_test_trace();
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        const auto& headers = result.value().dependencies.headers;

        ASSERT_GE(headers.size(), 2u);
        for (std::size_t i = 1; i < headers.size(); ++i) {
            EXPECT_GE(headers[i - 1].total_parse_time, headers[i].total_parse_time);
        }
    }

    // ======================================================================
    // Char-scanner tests for parse_include_directives_from_file
    // ======================================================================

    TEST(IncludeParseUtilsTest, ParsesStandardInclude) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-incparse-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path header = temp_dir / "test.h";
        {
            std::ofstream out(header);
            out << "#include <vector>\n";
            out << "#include \"local.h\"\n";
            out << "  # include <algorithm>\n";
            out << "\t#include   \"tabbed.h\"\n";
            out << "int x = 1;\n";
        }

        const auto directives = bha::utils::parse_include_directives_from_file(header);
        ASSERT_EQ(directives.size(), 4u);
        EXPECT_TRUE(directives[0].is_system);
        EXPECT_EQ(directives[0].header_name, "vector");
        EXPECT_FALSE(directives[1].is_system);
        EXPECT_EQ(directives[1].header_name, "local.h");
        EXPECT_TRUE(directives[2].is_system);
        EXPECT_EQ(directives[2].header_name, "algorithm");
        EXPECT_FALSE(directives[3].is_system);
        EXPECT_EQ(directives[3].header_name, "tabbed.h");

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    TEST(IncludeParseUtilsTest, SkipsNonIncludeLines) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-incskip-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path header = temp_dir / "skip.h";
        {
            std::ofstream out(header);
            out << "int main() {}\n";
            out << "#define FOO 1\n";
            out << "// #include \"commented.h\"\n";
            out << "/* #include \"block_comment.h\" */\n";
            out << "#ifdef FOO\n";
            out << "#include <real.h>\n";
            out << "#endif\n";
        }

        const auto directives = bha::utils::parse_include_directives_from_file(header);
        ASSERT_EQ(directives.size(), 1u);
        EXPECT_TRUE(directives[0].is_system);
        EXPECT_EQ(directives[0].header_name, "real.h");

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    TEST(IncludeParseUtilsTest, HandlesNonexistentFile) {
        const auto directives = bha::utils::parse_include_directives_from_file(
            "/nonexistent/path/header.h"
        );
        EXPECT_TRUE(directives.empty());
    }

    TEST(IncludeParseUtilsTest, HandlesMultipleIncludesPerLine) {
        // Note: valid C/C++ only has one #include per line, but verify the parser
        // doesn't crash on unusual input.
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-incmulti-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path header = temp_dir / "multi.h";
        {
            std::ofstream out(header);
            out << "  \n";
            out << "\n";
            out << "#include <a.h>\n";
            out << "#include \"b.h\"\n";
            out << "// just a comment\n";
            out << "  #\tinclude\t<tabbed.h>\n";
        }

        const auto directives = bha::utils::parse_include_directives_from_file(header);
        ASSERT_EQ(directives.size(), 3u);
        EXPECT_TRUE(directives[0].is_system);
        EXPECT_EQ(directives[0].header_name, "a.h");
        EXPECT_FALSE(directives[1].is_system);
        EXPECT_EQ(directives[1].header_name, "b.h");
        EXPECT_TRUE(directives[2].is_system);
        EXPECT_EQ(directives[2].header_name, "tabbed.h");

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

}
