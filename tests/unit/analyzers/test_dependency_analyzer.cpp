//
// Created by gregorian-rayne on 12/30/25.
//


#include "bha/analyzers/dependency_analyzer.hpp"
#include "bha/utils/include_parse_utils.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <regex>

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

    TEST_F(DependencyAnalyzerTest, SelfIncludingHeaderDetected) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-self-cycle-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path self_header = temp_dir / "self.h";
        {
            std::ofstream out(self_header);
            ASSERT_TRUE(out.good());
            out << "#pragma once\n";
            out << "#include \"self.h\"\n";
            out << "struct SelfRef { int x; };\n";
        }

        BuildTrace trace;
        trace.id = "test-self-cycle";
        CompilationUnit unit;
        unit.source_file = temp_dir / "main.cpp";
        unit.includes = {{self_header, std::chrono::milliseconds(10), 1, {}, {}}};
        trace.units = {unit};

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);
        ASSERT_TRUE(result.is_ok());
        const auto& cycles = result.value().dependencies.circular_dependencies;
        EXPECT_FALSE(cycles.empty());

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    TEST_F(DependencyAnalyzerTest, DiamondDependencyNoCycle) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-diamond-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path d_header = temp_dir / "d.h";
        const fs::path b_header = temp_dir / "b.h";
        const fs::path c_header = temp_dir / "c.h";
        const fs::path a_header = temp_dir / "a.h";

        {
            std::ofstream out(d_header);
            out << "#pragma once\nstruct D { int d; };\n";
        }
        {
            std::ofstream out(b_header);
            out << "#pragma once\n#include \"d.h\"\nstruct B { D d; };\n";
        }
        {
            std::ofstream out(c_header);
            out << "#pragma once\n#include \"d.h\"\nstruct C { D d; };\n";
        }
        {
            std::ofstream out(a_header);
            out << "#pragma once\n#include \"b.h\"\n#include \"c.h\"\nstruct A { B b; C c; };\n";
        }

        BuildTrace trace;
        trace.id = "test-diamond";
        CompilationUnit unit;
        unit.source_file = temp_dir / "main.cpp";
        unit.includes = {
            {a_header, std::chrono::milliseconds(10), 1, {}, {}},
            {b_header, std::chrono::milliseconds(5), 2, {}, {}},
            {c_header, std::chrono::milliseconds(5), 2, {}, {}},
            {d_header, std::chrono::milliseconds(3), 3, {}, {}},
        };
        trace.units = {unit};

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().dependencies.circular_dependencies.empty());

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
    }

    TEST_F(DependencyAnalyzerTest, LeafHeaderNoCycle) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path temp_dir = fs::temp_directory_path() / ("bha-leaf-" + std::to_string(stamp));
        ASSERT_TRUE(fs::create_directories(temp_dir));

        const fs::path leaf = temp_dir / "leaf.h";
        {
            std::ofstream out(leaf);
            out << "#pragma once\nstruct Leaf { int x; };\n";
        }

        BuildTrace trace;
        trace.id = "test-leaf";
        CompilationUnit unit;
        unit.source_file = temp_dir / "main.cpp";
        unit.includes = {{leaf, std::chrono::milliseconds(5), 1, {}, {}}};
        trace.units = {unit};

        constexpr AnalysisOptions options;
        const auto result = analyzer_->analyze(trace, options);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().dependencies.circular_dependencies.empty());

        std::error_code ec;
        fs::remove_all(temp_dir, ec);
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
