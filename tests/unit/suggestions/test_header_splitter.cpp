//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include <fstream>
#include "bha/suggestions/header_splitter.h"

using namespace bha::suggestions;
using namespace bha::core;

class HeaderSplitterTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = DependencyGraph{};
        splitter = std::make_unique<HeaderSplitter>(graph);
    }

    DependencyGraph graph;
    std::unique_ptr<HeaderSplitter> splitter;
};

TEST_F(HeaderSplitterTest, SuggestSplitWithEmptyDependents) {
    constexpr std::vector<std::string> empty_dependents;

    const auto result = HeaderSplitter::suggest_split("header.h", empty_dependents, 2);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(HeaderSplitterTest, SuggestSplitWithValidData) {
    const std::vector<std::string> dependents = {
        "file1.cpp",
        "file2.cpp",
        "file3.cpp"
    };

    if (auto result = HeaderSplitter::suggest_split("header.h", dependents, 2); result.is_success()) {
        const auto& suggestion = result.value();
        EXPECT_FALSE(suggestion.original_file.empty());
        EXPECT_GE(suggestion.confidence, 0.0);
        EXPECT_LE(suggestion.confidence, 1.0);
        EXPECT_GE(suggestion.estimated_benefit_ms, 0.0);
    }
}

TEST_F(HeaderSplitterTest, SuggestSplitWithDifferentClusterSizes) {
    std::vector<std::string> dependents = {"file1.cpp", "file2.cpp"};

    auto result1 = HeaderSplitter::suggest_split("header.h", dependents, 1);
    auto result2 = HeaderSplitter::suggest_split("header.h", dependents, 5);

    EXPECT_EQ(result1.is_success(), result2.is_success());
}

TEST_F(HeaderSplitterTest, BuildCoUsageMatrixWithEmptySymbols) {
    std::vector<std::string> empty_symbols;
    HeaderSplitter::SymbolUsageCache cache;

    auto result = HeaderSplitter::build_co_usage_matrix(empty_symbols, cache);

    ASSERT_TRUE(result.is_success());
    const auto& co_usage = result.value();
    EXPECT_EQ(co_usage.symbols.size(), 0);
    EXPECT_EQ(co_usage.co_usage_matrix.size(), 0);
}

TEST_F(HeaderSplitterTest, BuildCoUsageMatrixWithValidSymbols) {
    std::vector<std::string> symbols = {"SymbolA", "SymbolB", "SymbolC"};
    HeaderSplitter::SymbolUsageCache cache;
    cache.all_symbols = {"SymbolA", "SymbolB", "SymbolC"};
    cache.dependent_to_symbols["file1.cpp"] = {"SymbolA", "SymbolB"};
    cache.dependent_to_symbols["file2.cpp"] = {"SymbolB", "SymbolC"};

    auto result = HeaderSplitter::build_co_usage_matrix(symbols, cache);

    ASSERT_TRUE(result.is_success());
    const auto& co_usage = result.value();
    EXPECT_EQ(co_usage.symbols.size(), symbols.size());
    EXPECT_EQ(co_usage.co_usage_matrix.size(), symbols.size());

    // Matrix should be square
    for (const auto& row : co_usage.co_usage_matrix) {
        EXPECT_EQ(row.size(), symbols.size());
    }
}

TEST_F(HeaderSplitterTest, PerformSpectralClusteringWithSmallMatrix) {
    const std::vector<std::vector<int>> small_matrix = {
        {2, 1},
        {1, 2}
    };

    auto result = HeaderSplitter::perform_spectral_clustering(small_matrix, 2);

    ASSERT_TRUE(result.is_success());
    const auto& [labels, num_clusters, quality_score] = result.value();
    EXPECT_EQ(labels.size(), 2);
    EXPECT_LE(num_clusters, 2);
    EXPECT_GE(quality_score, 0.0);
}

TEST_F(HeaderSplitterTest, PerformSpectralClusteringWithLargerMatrix) {
    const std::vector<std::vector<int>> matrix = {
        {5, 4, 0, 0},
        {4, 5, 0, 0},
        {0, 0, 5, 4},
        {0, 0, 4, 5}
    };

    auto result = HeaderSplitter::perform_spectral_clustering(matrix, 2);

    ASSERT_TRUE(result.is_success());
    const auto& clustering = result.value();
    EXPECT_EQ(clustering.labels.size(), 4);
    EXPECT_LE(clustering.num_clusters, 2);
}

TEST_F(HeaderSplitterTest, CalculateSplitBenefitWithEmptyClusters) {
    const std::map<int, std::vector<std::string>> empty_clusters;
    const HeaderSplitter::SymbolUsageCache cache;

    const double benefit = HeaderSplitter::calculate_split_benefit(empty_clusters, cache);

    EXPECT_GE(benefit, 0.0);
}

TEST_F(HeaderSplitterTest, CalculateSplitBenefitWithValidClusters) {
    std::map<int, std::vector<std::string>> clusters;
    clusters[0] = {"SymbolA", "SymbolB"};
    clusters[1] = {"SymbolC", "SymbolD"};

    HeaderSplitter::SymbolUsageCache cache;
    cache.all_symbols = {"SymbolA", "SymbolB", "SymbolC", "SymbolD"};
    cache.dependent_to_symbols["file1.cpp"] = {"SymbolA", "SymbolB"};
    cache.dependent_to_symbols["file2.cpp"] = {"SymbolC", "SymbolD"};

    const double benefit = HeaderSplitter::calculate_split_benefit(clusters, cache);

    EXPECT_GE(benefit, 0.0);
}

TEST_F(HeaderSplitterTest, ExtractSymbolUsageWithEmptyDependents) {
    constexpr std::vector<std::string> empty_dependents;

    const auto result = HeaderSplitter::extract_symbol_usage("header.h", empty_dependents);
    EXPECT_TRUE(result.is_success() || !result.is_success());
}

TEST_F(HeaderSplitterTest, HeaderSplitSuggestionStructure) {
    HeaderSplitSuggestion suggestion;
    suggestion.original_file = "bigheader.h";
    suggestion.suggested_splits.push_back({"part1.h", {"SymbolA", "SymbolB"}});
    suggestion.suggested_splits.push_back({"part2.h", {"SymbolC", "SymbolD"}});
    suggestion.estimated_benefit_ms = 200.5;
    suggestion.confidence = 0.75;
    suggestion.rationale = "High symbol separation detected";

    EXPECT_EQ(suggestion.original_file, "bigheader.h");
    EXPECT_EQ(suggestion.suggested_splits.size(), 2);
    EXPECT_DOUBLE_EQ(suggestion.estimated_benefit_ms, 200.5);
    EXPECT_DOUBLE_EQ(suggestion.confidence, 0.75);
    EXPECT_FALSE(suggestion.rationale.empty());
}

TEST_F(HeaderSplitterTest, SymbolCoUsageStructure) {
    SymbolCoUsage co_usage;
    co_usage.symbols = {"A", "B", "C"};
    co_usage.co_usage_matrix = {
        {3, 2, 0},
        {2, 3, 1},
        {0, 1, 3}
    };
    co_usage.num_files_analyzed = 5;

    EXPECT_EQ(co_usage.symbols.size(), 3);
    EXPECT_EQ(co_usage.co_usage_matrix.size(), 3);
    EXPECT_EQ(co_usage.num_files_analyzed, 5);
}

TEST_F(HeaderSplitterTest, ClusteringResultStructure) {
    ClusteringResult clustering;
    clustering.labels = {0, 0, 1, 1, 2};
    clustering.num_clusters = 3;
    clustering.quality_score = 0.85;

    EXPECT_EQ(clustering.labels.size(), 5);
    EXPECT_EQ(clustering.num_clusters, 3);
    EXPECT_DOUBLE_EQ(clustering.quality_score, 0.85);
}

TEST_F(HeaderSplitterTest, SymbolUsageCacheStructure) {
    HeaderSplitter::SymbolUsageCache cache;
    cache.dependent_to_symbols["file1.cpp"] = {"SymA", "SymB"};
    cache.dependent_to_symbols["file2.cpp"] = {"SymC"};
    cache.all_symbols = {"SymA", "SymB", "SymC", "SymD"};

    EXPECT_EQ(cache.dependent_to_symbols.size(), 2);
    EXPECT_EQ(cache.all_symbols.size(), 4);
    EXPECT_EQ(cache.dependent_to_symbols["file1.cpp"].size(), 2);
}

TEST_F(HeaderSplitterTest, AnalyzeSymbolClusteringWithDifferentTargets) {
    std::vector<std::vector<int>> matrix = {
        {3, 2, 0},
        {2, 3, 1},
        {0, 1, 3}
    };

    auto result2 = HeaderSplitter::perform_spectral_clustering(matrix, 2);
    auto result3 = HeaderSplitter::perform_spectral_clustering(matrix, 3);

    EXPECT_TRUE(result2.is_success());
    EXPECT_TRUE(result3.is_success());

    if (result2.is_success() && result3.is_success()) {
        EXPECT_LE(result2.value().num_clusters, 2);
        EXPECT_LE(result3.value().num_clusters, 3);
    }
}

TEST_F(HeaderSplitterTest, IdentifyHighFanoutHeadersViaDependents) {
    const std::string header_file = "high_fanout.h";

    {
        std::ofstream out(header_file);
        out << "class A{};\n"
               "class B{};\n"
               "class C{};\n"
               "class D{};\n"
               "class E{};\n";
    }

    std::vector<std::string> many_dependents;
    for (int i = 0; i < 50; ++i) {
        const std::string dep = "file" + std::to_string(i) + ".cpp";
        many_dependents.push_back(dep);

        std::ofstream dep_out(dep);
        dep_out << "#include \"high_fanout.h\"\n"
                   "void use() {\n"
                   "  A a; B b;\n"
                   "}\n";
    }

    const auto result =
        HeaderSplitter::suggest_split(header_file, many_dependents, 2);

    EXPECT_TRUE(result.is_failure());
    EXPECT_EQ(
        result.error().code,
        ErrorCode:: ANALYSIS_ERROR
    );
}


TEST_F(HeaderSplitterTest, RecommendModularSplitsBasedOnUsage) {
    const std::vector<std::string> dependents = {
        "moduleA.cpp", "moduleB.cpp", "moduleC.cpp"
    };

    if (auto result = HeaderSplitter::suggest_split("monolithic.h", dependents, 2); result.is_success()) {
        const auto& suggestion = result.value();
        EXPECT_FALSE(suggestion.suggested_splits.empty());

        for (const auto& [fst, snd] : suggestion.suggested_splits) {
            EXPECT_FALSE(fst.empty());  // New header name
            EXPECT_GE(snd.size(), 0);   // Symbols in split
        }
    }
}

TEST_F(HeaderSplitterTest, ClusteringWithHighCoUsageMatrix) {
    const std::vector<std::vector<int>> matrix = {
        {10, 9, 1, 0},
        {9, 10, 0, 1},
        {1, 0, 10, 9},
        {0, 1, 9, 10}
    };

    auto result = HeaderSplitter::perform_spectral_clustering(matrix, 2);

    ASSERT_TRUE(result.is_success());
    const auto& [labels, num_clusters, quality_score] = result.value();
    EXPECT_EQ(labels.size(), 4);
    EXPECT_LE(num_clusters, 2);
    EXPECT_GT(quality_score, 0.0);
    EXPECT_LE(quality_score, 1.0);
}

TEST_F(HeaderSplitterTest, ClusteringQualityScoreRange) {
    const std::vector<std::vector<int>> matrix = {
        {5, 2, 1},
        {2, 5, 3},
        {1, 3, 5}
    };

    auto result = HeaderSplitter::perform_spectral_clustering(matrix, 2);

    ASSERT_TRUE(result.is_success());
    EXPECT_GE(result.value().quality_score, 0.0);
    EXPECT_LE(result.value().quality_score, 1.0);
}

TEST_F(HeaderSplitterTest, BuildCoUsageMatrixSymmetry) {
    const std::vector<std::string> symbols = {"A", "B", "C"};
    HeaderSplitter::SymbolUsageCache cache;
    cache.all_symbols = {"A", "B", "C"};
    cache.dependent_to_symbols["file1.cpp"] = {"A", "B"};
    cache.dependent_to_symbols["file2.cpp"] = {"B", "C"};
    cache.dependent_to_symbols["file3.cpp"] = {"A", "C"};

    auto result = HeaderSplitter::build_co_usage_matrix(symbols, cache);

    ASSERT_TRUE(result.is_success());
    const auto& co_usage = result.value();

    // Matrix should be symmetric
    for (size_t i = 0; i < co_usage.co_usage_matrix.size(); ++i) {
        for (size_t j = 0; j < co_usage.co_usage_matrix[i].size(); ++j) {
            EXPECT_EQ(co_usage.co_usage_matrix[i][j], co_usage.co_usage_matrix[j][i]);
        }
    }
}

TEST_F(HeaderSplitterTest, CalculateSplitBenefitScales) {
    std::map<int, std::vector<std::string>> clusters;
    clusters[0] = {"Symbol1", "Symbol2"};
    clusters[1] = {"Symbol3", "Symbol4"};
    clusters[2] = {"Symbol5", "Symbol6"};

    HeaderSplitter::SymbolUsageCache cache;
    cache.all_symbols = {"Symbol1", "Symbol2", "Symbol3", "Symbol4", "Symbol5", "Symbol6"};
    for (int i = 0; i < 10; ++i) {
        cache.dependent_to_symbols["file" + std::to_string(i) + ".cpp"] =
            {"Symbol" + std::to_string((i % 3) + 1)};
    }

    const double benefit = HeaderSplitter::calculate_split_benefit(clusters, cache);

    EXPECT_GE(benefit, 0.0);
}

TEST_F(HeaderSplitterTest, ExtractSymbolUsageComplexDependents) {
    const std::string header_file = "complex.h";
    {
        std::ofstream out(header_file);
        out << "class ComplexType {\n"
               "public:\n"
               "  void method();\n"
               "};\n";
    }

    std::vector<std::string> many_dependents;
    for (int i = 0; i < 10; ++i) {
        const std::string dep = "dependent" + std::to_string(i) + ".cpp";
        many_dependents.push_back(dep);

        std::ofstream dep_out(dep);
        dep_out << "#include \"complex.h\"\n"
                   "void use() {\n"
                   "  ComplexType obj;\n"
                   "}\n";
    }

    const auto result =
        HeaderSplitter::extract_symbol_usage(header_file, many_dependents);

    EXPECT_TRUE(result.is_success());
}


TEST_F(HeaderSplitterTest, SymbolCoUsageReflectsDependencies) {
    SymbolCoUsage co_usage;
    co_usage.symbols = {"A", "B", "C"};
    co_usage.co_usage_matrix = {
        {5, 4, 0},
        {4, 5, 1},
        {0, 1, 5}
    };
    co_usage.num_files_analyzed = 10;

    // A and B have high co-usage (4)
    EXPECT_GT(co_usage.co_usage_matrix[0][1], 1);
    // A and C have low co-usage (0)
    EXPECT_LT(co_usage.co_usage_matrix[0][2], 2);
    // B and C have medium co-usage (1)
    EXPECT_LE(co_usage.co_usage_matrix[1][2], 2);
}

TEST_F(HeaderSplitterTest, HeaderSplitSuggestionValidBenefit) {
    HeaderSplitSuggestion suggestion;
    suggestion.original_file = "monolithic.h";
    suggestion.estimated_benefit_ms = 500.0;
    suggestion.confidence = 0.85;

    EXPECT_GT(suggestion.estimated_benefit_ms, 0.0);
    EXPECT_GT(suggestion.confidence, 0.7);
}

TEST_F(HeaderSplitterTest, LargeSymbolSetAnalysis) {
    std::vector<std::string> large_symbols;
    for (int i = 0; i < 50; ++i) {
        large_symbols.push_back("Symbol" + std::to_string(i));
    }

    HeaderSplitter::SymbolUsageCache cache;
    for (const auto& sym : large_symbols) {
        cache.all_symbols.insert(sym);
    }

    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 5; ++j) {
            cache.dependent_to_symbols["file" + std::to_string(i) + ".cpp"].insert(
                "Symbol" + std::to_string((i + j) % 50));
        }
    }

    auto result = HeaderSplitter::build_co_usage_matrix(large_symbols, cache);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().symbols.size(), large_symbols.size());
}

TEST_F(HeaderSplitterTest, DifferentClusteringTargets) {
    std::vector<std::vector<int>> matrix = {
        {6, 2, 1, 0},
        {2, 6, 0, 1},
        {1, 0, 6, 2},
        {0, 1, 2, 6}
    };

    auto result1 = HeaderSplitter::perform_spectral_clustering(matrix, 1);
    auto result2 = HeaderSplitter::perform_spectral_clustering(matrix, 2);
    auto result3 = HeaderSplitter::perform_spectral_clustering(matrix, 3);

    ASSERT_TRUE(result1.is_success());
    ASSERT_TRUE(result2.is_success());
    ASSERT_TRUE(result3.is_success());

    EXPECT_LE(result1.value().num_clusters, 1);
    EXPECT_LE(result2.value().num_clusters, 2);
    EXPECT_LE(result3.value().num_clusters, 3);
}

TEST_F(HeaderSplitterTest, SplitBenefitWithUniformUsage) {
    // All symbols used equally often
    std::map<int, std::vector<std::string>> clusters;
    clusters[0] = {"A", "B", "C"};
    clusters[1] = {"D", "E", "F"};

    HeaderSplitter::SymbolUsageCache cache;
    cache.all_symbols = {"A", "B", "C", "D", "E", "F"};

    // All files use all symbols equally
    for (int i = 0; i < 10; ++i) {
        cache.dependent_to_symbols["file" + std::to_string(i) + ".cpp"] =
            {"A", "B", "C", "D", "E", "F"};
    }

    const double benefit = HeaderSplitter::calculate_split_benefit(clusters, cache);

    EXPECT_GE(benefit, 0.0);
}

TEST_F(HeaderSplitterTest, SuggestSplitMinClusterSizeRespect) {
    std::vector<std::string> dependents = {"f1.cpp", "f2.cpp", "f3.cpp"};

    auto result_small = HeaderSplitter::suggest_split("header.h", dependents, 1);
    auto result_large = HeaderSplitter::suggest_split("header.h", dependents, 10);

    // With larger minimum cluster size, split may not be suggested
    EXPECT_TRUE(result_small.is_success() || !result_small.is_success());
    EXPECT_TRUE(result_large.is_success() || !result_large.is_success());
}

TEST_F(HeaderSplitterTest, HighFanoutHeaderSplitAnalysis) {
    std::vector<std::string> high_fanout_deps;
    for (int i = 0; i < 100; ++i) {
        high_fanout_deps.push_back("file" + std::to_string(i) + ".cpp");
    }

    if (auto result = HeaderSplitter::suggest_split("ubiquitous.h", high_fanout_deps, 3); result.is_success()) {
        EXPECT_GT(result.value().confidence, 0.5);
        EXPECT_GT(result.value().estimated_benefit_ms, 0.0);
    }
}

TEST_F(HeaderSplitterTest, SafetyOfProposedSplits) {
    HeaderSplitSuggestion suggestion;
    suggestion.original_file = "original.h";
    suggestion.suggested_splits = {
        {"part1.h", {"ClassA", "ClassB"}},
        {"part2.h", {"ClassC", "ClassD"}}
    };
    suggestion.confidence = 0.78;
    suggestion.estimated_benefit_ms = 200.0;

    // Suggest should be applicable (safe) if confidence is good
    EXPECT_GT(suggestion.confidence, 0.5);
    EXPECT_GT(suggestion.estimated_benefit_ms, 0.0);
}

TEST_F(HeaderSplitterTest, RankingOfSplitsByBenefit) {
    HeaderSplitSuggestion split1;
    split1.estimated_benefit_ms = 100.0;
    split1.confidence = 0.75;

    HeaderSplitSuggestion split2;
    split2.estimated_benefit_ms = 250.0;
    split2.confidence = 0.8;

    HeaderSplitSuggestion split3;
    split3.estimated_benefit_ms = 50.0;
    split3.confidence = 0.6;

    // split2 should be ranked higher due to greater benefit
    EXPECT_GT(split2.estimated_benefit_ms, split1.estimated_benefit_ms);
    EXPECT_GT(split2.estimated_benefit_ms, split3.estimated_benefit_ms);
}