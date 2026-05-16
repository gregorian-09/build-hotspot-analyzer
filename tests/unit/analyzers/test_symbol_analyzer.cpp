//
// Created by gregorian-rayne on 12/30/25.
//

#include "bha/analyzers/symbol_analyzer.hpp"

#include <gtest/gtest.h>

namespace bha::analyzers {

    class SymbolAnalyzerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            analyzer_ = std::make_unique<SymbolAnalyzer>();
        }

        std::unique_ptr<SymbolAnalyzer> analyzer_;
    };

    TEST_F(SymbolAnalyzerTest, Name) {
        EXPECT_EQ(analyzer_->name(), "SymbolAnalyzer");
    }

    TEST_F(SymbolAnalyzerTest, Description) {
        EXPECT_FALSE(analyzer_->description().empty());
    }

    TEST_F(SymbolAnalyzerTest, AnalyzeEmptyTrace) {
        const BuildTrace trace;
        constexpr AnalysisOptions options;

        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 0u);
        EXPECT_EQ(result.value().symbols.unused_symbols, 0u);
    }

    TEST_F(SymbolAnalyzerTest, AnalyzesSymbolDefinitions) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(10);

        CompilationUnit unit1;
        unit1.source_file = "main.cpp";
        unit1.symbols_defined = {"main", "helper_function", "MyClass::method"};
        trace.units.push_back(unit1);

        CompilationUnit unit2;
        unit2.source_file = "utils.cpp";
        unit2.symbols_defined = {"utility_function"};
        trace.units.push_back(unit2);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 4u);
    }

    TEST_F(SymbolAnalyzerTest, ClassifiesSymbolTypes) {
        BuildTrace trace;

        CompilationUnit unit;
        unit.source_file = "test.cpp";
        unit.symbols_defined = {
            "simple_function(int)",
            "MyClass::method()",
            "MyClass::member",
            "class MyType"
        };
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());

        const auto& symbols = result.value().symbols.symbols;
        EXPECT_EQ(symbols.size(), 4u);

        for (const auto& sym : symbols) {
            EXPECT_FALSE(sym.type.empty());
        }
    }

    TEST_F(SymbolAnalyzerTest, TracksTemplateInstantiations) {
        BuildTrace trace;

        CompilationUnit unit1;
        unit1.source_file = "template.cpp";
        TemplateInstantiation tmpl;
        tmpl.name = "std::vector<int>";
        tmpl.time = std::chrono::milliseconds(10);
        unit1.templates.push_back(tmpl);
        trace.units.push_back(unit1);

        CompilationUnit unit2;
        unit2.source_file = "user.cpp";
        TemplateInstantiation tmpl2;
        tmpl2.name = "std::vector<int>";  // Same template, different file
        tmpl2.time = std::chrono::milliseconds(5);
        unit2.templates.push_back(tmpl2);
        trace.units.push_back(unit2);

        AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().symbols.total_symbols, 1u);

        bool found = false;
        for (const auto& sym : result.value().symbols.symbols) {
            if (sym.name == "std::vector<int>") {
                found = true;
                EXPECT_GE(sym.usage_count, 2u);  // Used in both files
            }
        }
        EXPECT_TRUE(found);
    }

    TEST_F(SymbolAnalyzerTest, IdentifiesUnusedSymbols) {
        BuildTrace trace;

        CompilationUnit unit;
        unit.source_file = "orphan.cpp";
        unit.symbols_defined = {"unused_function"};
        // No includes or references to this symbol
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 1u);
        EXPECT_EQ(result.value().symbols.unused_symbols, 1u);
    }

    TEST_F(SymbolAnalyzerTest, HandlesMultipleSymbols) {
        BuildTrace trace;

        CompilationUnit unit;
        unit.source_file = "many_symbols.cpp";
        for (int i = 0; i < 100; ++i) {
            unit.symbols_defined.push_back("symbol_" + std::to_string(i));
        }
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.symbols.size(), 100u);
        EXPECT_EQ(result.value().symbols.total_symbols, 100u);
    }

    TEST_F(SymbolAnalyzerTest, SortsSymbolsByUsage) {
        BuildTrace trace;

        CompilationUnit unit1;
        unit1.source_file = "defs.cpp";
        unit1.symbols_defined = {"rarely_used", "frequently_used"};
        trace.units.push_back(unit1);

        CompilationUnit unit2;
        unit2.source_file = "user1.cpp";
        TemplateInstantiation tmpl;
        tmpl.name = "frequently_used";
        unit2.templates.push_back(tmpl);
        trace.units.push_back(unit2);

        CompilationUnit unit3;
        unit3.source_file = "user2.cpp";
        unit3.templates.push_back(tmpl);  // Same template
        trace.units.push_back(unit3);

        CompilationUnit unit4;
        unit4.source_file = "user3.cpp";
        unit4.templates.push_back(tmpl);  // Same template
        trace.units.push_back(unit4);

        AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        ASSERT_GE(result.value().symbols.symbols.size(), 2u);

        // Most frequently used should be first
        EXPECT_GE(result.value().symbols.symbols[0].usage_count,
                  result.value().symbols.symbols[1].usage_count);
    }

    // ======================================================================
    // Edge-case tests for the O(N^4) -> O(N) reverse-map optimization
    // ======================================================================

    TEST_F(SymbolAnalyzerTest, TracksSymbolUsageThroughIncludes) {
        BuildTrace trace;

        CompilationUnit header_unit;
        header_unit.source_file = "/project/src/foo.h";
        header_unit.symbols_defined = {"FooClass", "foo_helper"};
        trace.units.push_back(header_unit);

        CompilationUnit user_unit;
        user_unit.source_file = "/project/src/main.cpp";
        IncludeInfo inc;
        inc.header = "/project/src/foo.h";
        inc.parse_time = std::chrono::milliseconds(5);
        user_unit.includes.push_back(inc);
        trace.units.push_back(user_unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 2u);

        for (const auto& sym : result.value().symbols.symbols) {
            EXPECT_EQ(sym.usage_count, 1u);
            bool found_main = false;
            for (const auto& used_in : sym.used_in) {
                if (used_in == "/project/src/main.cpp") {
                    found_main = true;
                    break;
                }
            }
            EXPECT_TRUE(found_main);
        }
    }

    TEST_F(SymbolAnalyzerTest, MultipleFilesIncludeSameHeader) {
        BuildTrace trace;

        CompilationUnit header_unit;
        header_unit.source_file = "/project/include/api.h";
        header_unit.symbols_defined = {"api_function", "ApiClass"};
        trace.units.push_back(header_unit);

        for (int i = 0; i < 50; ++i) {
            CompilationUnit unit;
            unit.source_file = "/project/src/file_" + std::to_string(i) + ".cpp";
            IncludeInfo inc;
            inc.header = "/project/include/api.h";
            inc.parse_time = std::chrono::milliseconds(1);
            unit.includes.push_back(inc);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 2u);

        for (const auto& sym : result.value().symbols.symbols) {
            EXPECT_EQ(sym.usage_count, 50u);
            EXPECT_EQ(sym.used_in.size(), 50u);
        }
    }

    TEST_F(SymbolAnalyzerTest, IncludeWithNoMatchingDefinitionsDoesNotAddUsage) {
        BuildTrace trace;

        CompilationUnit unit;
        unit.source_file = "/project/src/work.cpp";
        unit.symbols_defined = {"local_func"};
        IncludeInfo inc;
        inc.header = "/project/include/external_lib.h";
        inc.parse_time = std::chrono::milliseconds(3);
        unit.includes.push_back(inc);
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 1u);
        for (const auto& sym : result.value().symbols.symbols) {
            EXPECT_EQ(sym.usage_count, 0u);
            EXPECT_TRUE(sym.used_in.empty());
        }
    }

    TEST_F(SymbolAnalyzerTest, SameIncludeDoesNotDoubleCountUsage) {
        BuildTrace trace;

        CompilationUnit header_unit;
        header_unit.source_file = "/project/src/header.h";
        header_unit.symbols_defined = {"SharedSymbol"};
        trace.units.push_back(header_unit);

        CompilationUnit user;
        user.source_file = "/project/src/user.cpp";
        IncludeInfo inc1;
        inc1.header = "/project/src/header.h";
        user.includes.push_back(inc1);
        IncludeInfo inc2;
        inc2.header = "/project/src/header.h";
        user.includes.push_back(inc2);
        trace.units.push_back(user);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        for (const auto& sym : result.value().symbols.symbols) {
            if (sym.name == "SharedSymbol") {
                EXPECT_EQ(sym.usage_count, 1u);
                EXPECT_EQ(sym.used_in.size(), 1u);
            }
        }
    }

    TEST_F(SymbolAnalyzerTest, HeaderDefinesMultipleSymbolsAllTracked) {
        BuildTrace trace;

        CompilationUnit header;
        header.source_file = "/project/src/big_header.h";
        for (int i = 0; i < 200; ++i) {
            header.symbols_defined.push_back("big_symbol_" + std::to_string(i));
        }
        trace.units.push_back(header);

        for (int u = 0; u < 3; ++u) {
            CompilationUnit unit;
            unit.source_file = "/project/src/user_" + std::to_string(u) + ".cpp";
            IncludeInfo inc;
            inc.header = "/project/src/big_header.h";
            unit.includes.push_back(inc);
            trace.units.push_back(unit);
        }

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 200u);

        for (const auto& sym : result.value().symbols.symbols) {
            EXPECT_EQ(sym.usage_count, 3u);
            EXPECT_EQ(sym.used_in.size(), 3u);
        }
    }

    TEST_F(SymbolAnalyzerTest, EmptySymbolStringsAreSkipped) {
        BuildTrace trace;

        CompilationUnit unit;
        unit.source_file = "/project/src/skip.cpp";
        unit.symbols_defined = {"valid_sym", "", "another_valid", ""};
        trace.units.push_back(unit);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.value().symbols.total_symbols, 2u);
    }

    TEST_F(SymbolAnalyzerTest, NormalizedPathMatching) {
        BuildTrace trace;

        CompilationUnit header_unit;
        header_unit.source_file = "/project/./src/./../src/header.h";
        header_unit.symbols_defined = {"NormalizedSym"};
        trace.units.push_back(header_unit);

        CompilationUnit user;
        user.source_file = "/project/src/main.cpp";
        IncludeInfo inc;
        inc.header = "/project/src/header.h";
        user.includes.push_back(inc);
        trace.units.push_back(user);

        constexpr AnalysisOptions options;
        auto result = analyzer_->analyze(trace, options);

        ASSERT_TRUE(result.is_ok());
        for (const auto& sym : result.value().symbols.symbols) {
            if (sym.name == "NormalizedSym") {
                EXPECT_EQ(sym.usage_count, 1u);
            }
        }
    }
}