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
}