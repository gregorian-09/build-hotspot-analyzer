//
// Created by gregorian-rayne on 12/29/25.
//

#include "bha/suggestions/pch_suggester.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions
{
    class PCHSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<PCHSuggester>();
        }

        std::unique_ptr<PCHSuggester> suggester_;
    };

    TEST_F(PCHSuggesterTest, Name) {
        EXPECT_EQ(suggester_->name(), "PCHSuggester");
    }

    TEST_F(PCHSuggesterTest, Description) {
        EXPECT_FALSE(suggester_->description().empty());
    }

    TEST_F(PCHSuggesterTest, SuggestionType) {
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::PCHOptimization);
    }

    TEST_F(PCHSuggesterTest, EmptyAnalysis) {
        const BuildTrace trace;
        const analyzers::AnalysisResult analysis;
        const SuggesterOptions options;

        const SuggestionContext context{trace, analysis, options, {}};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }

    TEST_F(PCHSuggesterTest, SuggestsForExpensiveHeader) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(10);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "expensive_header.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 20;
        header.including_files = 15;
        header.is_stable = true;
        header.is_external = false;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_GE(result.value().suggestions.size(), 1u);

        if (!result.value().suggestions.empty()) {
            const auto& suggestion = result.value().suggestions[0];
            EXPECT_EQ(suggestion.type, SuggestionType::PCHOptimization);
            EXPECT_TRUE(suggestion.is_safe);
            EXPECT_GT(suggestion.estimated_savings.count(), 0);
        }
    }

    TEST_F(PCHSuggesterTest, EstimatedSavingsRemainBoundedByAggregateParseTime) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(30);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "pch_candidate.h";
        header.total_parse_time = std::chrono::seconds(4);
        header.inclusion_count = 200;
        header.including_files = 160;
        header.is_stable = true;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());
        EXPECT_LT(result.value().suggestions.front().estimated_savings,
                  std::chrono::milliseconds(1500));
    }

    TEST_F(PCHSuggesterTest, SkipsLowInclusionCount) {
        BuildTrace trace;
        analyzers::AnalysisResult analysis;

        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "rarely_included.h";
        header.total_parse_time = std::chrono::milliseconds(500);
        header.inclusion_count = 2;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(PCHSuggesterTest, CMakeTargetSelectionUsesCompileUnitHints) {
        namespace fs = std::filesystem;
        const fs::path project_root = fs::temp_directory_path() / "bha_pch_target_selection_test";
        std::error_code ec;
        fs::remove_all(project_root, ec);
        fs::create_directories(project_root / "include");
        fs::create_directories(project_root / "src");
        fs::create_directories(project_root / "tests");

        {
            std::ofstream cmake(project_root / "CMakeLists.txt");
            cmake << "cmake_minimum_required(VERSION 3.20)\n"
                  << "project(pch_target_selection LANGUAGES CXX)\n"
                  << "add_executable(GTest tests/test_main.cpp)\n"
                  << "add_library(core src/core.cpp)\n";
        }
        {
            std::ofstream core_src(project_root / "src" / "core.cpp");
            core_src << "#include \"heavy.hpp\"\n";
        }
        {
            std::ofstream test_src(project_root / "tests" / "test_main.cpp");
            test_src << "int main(){return 0;}\n";
        }

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(8);
        trace.build_system = BuildSystemType::CMake;

        CompilationUnit core_unit;
        core_unit.source_file = project_root / "src" / "core.cpp";
        core_unit.command_line = {
            "clang++",
            "-c",
            "-o",
            (project_root / "build" / "CMakeFiles" / "core.dir" / "src" / "core.cpp.o").string(),
            core_unit.source_file.string()
        };
        trace.units.push_back(core_unit);

        CompilationUnit test_unit;
        test_unit.source_file = project_root / "tests" / "test_main.cpp";
        test_unit.command_line = {
            "clang++",
            "-c",
            "-o",
            (project_root / "build" / "CMakeFiles" / "GTest.dir" / "tests" / "test_main.cpp.o").string(),
            test_unit.source_file.string()
        };
        trace.units.push_back(test_unit);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = project_root / "include" / "heavy.hpp";
        header.total_parse_time = std::chrono::milliseconds(1200);
        header.inclusion_count = 20;
        header.including_files = 12;
        header.is_stable = true;
        header.is_external = false;
        header.included_by = {project_root / "src" / "core.cpp"};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, project_root};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        const auto cmake_edit = std::find_if(
            suggestion.edits.begin(),
            suggestion.edits.end(),
            [&](const TextEdit& edit) {
                return edit.file == (project_root / "CMakeLists.txt");
            }
        );
        ASSERT_NE(cmake_edit, suggestion.edits.end());
        EXPECT_NE(cmake_edit->new_text.find("target_precompile_headers(core PRIVATE"),
                  std::string::npos);
        EXPECT_EQ(cmake_edit->new_text.find("target_precompile_headers(GTest PRIVATE"),
                  std::string::npos);

        fs::remove_all(project_root, ec);
    }

    TEST_F(PCHSuggesterTest, CMakeTargetSelectionPreservesTargetSuffixExpressions) {
        namespace fs = std::filesystem;
        const fs::path project_root = fs::temp_directory_path() / "bha_pch_target_suffix_test";
        std::error_code ec;
        fs::remove_all(project_root, ec);
        fs::create_directories(project_root / "include");
        fs::create_directories(project_root / "tools");

        {
            std::ofstream cmake(project_root / "CMakeLists.txt");
            cmake << "cmake_minimum_required(VERSION 3.20)\n"
                  << "project(pch_target_suffix LANGUAGES CXX)\n"
                  << "set(ARTIFACT_SUFFIX \"\")\n"
                  << "add_executable(block_cache_trace_analyzer${ARTIFACT_SUFFIX}\n"
                  << "  tools/block_cache_trace_analyzer_tool.cc)\n";
        }
        {
            std::ofstream tool_src(project_root / "tools" / "block_cache_trace_analyzer_tool.cc");
            tool_src << "#include \"../include/heavy.hpp\"\n"
                     << "int main(){return 0;}\n";
        }

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(8);
        trace.build_system = BuildSystemType::CMake;

        CompilationUnit tool_unit;
        tool_unit.source_file = project_root / "tools" / "block_cache_trace_analyzer_tool.cc";
        tool_unit.command_line = {
            "clang++",
            "-c",
            "-o",
            (project_root / "build" / "CMakeFiles" / "block_cache_trace_analyzer.dir" / "tools" /
             "block_cache_trace_analyzer_tool.cc.o")
                .string(),
            tool_unit.source_file.string()
        };
        trace.units.push_back(tool_unit);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = project_root / "include" / "heavy.hpp";
        header.total_parse_time = std::chrono::milliseconds(1200);
        header.inclusion_count = 20;
        header.including_files = 12;
        header.is_stable = true;
        header.is_external = false;
        header.included_by = {tool_unit.source_file};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, project_root};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        const auto cmake_edit = std::find_if(
            suggestion.edits.begin(),
            suggestion.edits.end(),
            [&](const TextEdit& edit) {
                return edit.file == (project_root / "CMakeLists.txt");
            }
        );
        ASSERT_NE(cmake_edit, suggestion.edits.end());
        EXPECT_NE(
            cmake_edit->new_text.find(
                "target_precompile_headers(block_cache_trace_analyzer${ARTIFACT_SUFFIX} PRIVATE"),
            std::string::npos
        );

        fs::remove_all(project_root, ec);
    }

    TEST_F(PCHSuggesterTest, CMakeTargetSelectionSkipsBackupDirectories) {
        namespace fs = std::filesystem;
        const fs::path project_root = fs::temp_directory_path() / "bha_pch_backup_filter_test";
        std::error_code ec;
        fs::remove_all(project_root, ec);
        fs::create_directories(project_root / "include");
        fs::create_directories(project_root / "src");
        fs::create_directories(project_root / ".lsp-optimization-backup" / "run-1" / "files" / "src");

        {
            std::ofstream root_cmake(project_root / "CMakeLists.txt");
            root_cmake << "cmake_minimum_required(VERSION 3.20)\n"
                       << "project(pch_backup_filter LANGUAGES CXX)\n"
                       << "add_subdirectory(src)\n";
        }
        {
            std::ofstream src_cmake(project_root / "src" / "CMakeLists.txt");
            src_cmake << "add_library(core core.cpp)\n";
        }
        {
            std::ofstream backup_cmake(project_root / ".lsp-optimization-backup" / "run-1" / "files" / "src" / "CMakeLists.txt");
            backup_cmake << "add_library(core core.cpp)\n";
        }
        {
            std::ofstream core_src(project_root / "src" / "core.cpp");
            core_src << "#include \"heavy.hpp\"\n";
        }

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(6);
        trace.build_system = BuildSystemType::CMake;

        CompilationUnit core_unit;
        core_unit.source_file = project_root / "src" / "core.cpp";
        core_unit.command_line = {
            "clang++",
            "-c",
            "-o",
            (project_root / "build" / "CMakeFiles" / "core.dir" / "src" / "core.cpp.o").string(),
            core_unit.source_file.string()
        };
        trace.units.push_back(core_unit);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = project_root / "include" / "heavy.hpp";
        header.total_parse_time = std::chrono::milliseconds(900);
        header.inclusion_count = 15;
        header.including_files = 8;
        header.is_stable = true;
        header.is_external = false;
        header.included_by = {project_root / "src" / "core.cpp"};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, project_root};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        const auto cmake_edit = std::find_if(
            suggestion.edits.begin(),
            suggestion.edits.end(),
            [&](const TextEdit& edit) {
                return edit.new_text.find("target_precompile_headers(core PRIVATE") != std::string::npos;
            }
        );
        ASSERT_NE(cmake_edit, suggestion.edits.end());
        EXPECT_EQ(cmake_edit->file, project_root / "src" / "CMakeLists.txt");
        EXPECT_EQ(cmake_edit->file.string().find(".lsp-optimization-backup"), std::string::npos);

        fs::remove_all(project_root, ec);
    }

    TEST_F(PCHSuggesterTest, CMakePchEditUsesEndOfMultilineTargetBlock) {
        namespace fs = std::filesystem;
        const fs::path project_root = fs::temp_directory_path() / "bha_pch_multiline_target_test";
        std::error_code ec;
        fs::remove_all(project_root, ec);
        fs::create_directories(project_root / "include");
        fs::create_directories(project_root / "src");

        {
            std::ofstream root_cmake(project_root / "CMakeLists.txt");
            root_cmake << "cmake_minimum_required(VERSION 3.20)\n"
                       << "project(pch_multiline LANGUAGES CXX)\n"
                       << "add_library(core\n"
                       << "    src/core.cpp\n"
                       << ")\n";
        }
        {
            std::ofstream core_src(project_root / "src" / "core.cpp");
            core_src << "#include \"heavy.hpp\"\n";
        }

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(6);
        trace.build_system = BuildSystemType::CMake;

        CompilationUnit core_unit;
        core_unit.source_file = project_root / "src" / "core.cpp";
        core_unit.command_line = {
            "clang++",
            "-c",
            "-o",
            (project_root / "build" / "CMakeFiles" / "core.dir" / "src" / "core.cpp.o").string(),
            core_unit.source_file.string()
        };
        trace.units.push_back(core_unit);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = project_root / "include" / "heavy.hpp";
        header.total_parse_time = std::chrono::milliseconds(1000);
        header.inclusion_count = 16;
        header.including_files = 9;
        header.is_stable = true;
        header.is_external = false;
        header.included_by = {project_root / "src" / "core.cpp"};
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, project_root};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        const auto& suggestion = result.value().suggestions.front();
        const auto cmake_edit = std::find_if(
            suggestion.edits.begin(),
            suggestion.edits.end(),
            [&](const TextEdit& edit) {
                return edit.file == (project_root / "CMakeLists.txt") &&
                       edit.new_text.find("target_precompile_headers(core PRIVATE") != std::string::npos;
            }
        );
        ASSERT_NE(cmake_edit, suggestion.edits.end());
        EXPECT_EQ(cmake_edit->start_line, 5u);

        fs::remove_all(project_root, ec);
    }

    TEST_F(PCHSuggesterTest, COnlyTraceSkipsExternalPchSuggestions) {
        BuildTrace trace;
        trace.total_time = std::chrono::seconds(6);
        trace.build_system = BuildSystemType::CMake;

        CompilationUnit c_unit;
        c_unit.source_file = "src/wl_init.c";
        c_unit.command_line = {"clang", "-c", "src/wl_init.c"};
        trace.units.push_back(c_unit);

        analyzers::AnalysisResult analysis;
        analyzers::DependencyAnalysisResult::HeaderInfo header;
        header.path = "/usr/include/xkbcommon/xkbcommon.h";
        header.total_parse_time = std::chrono::milliseconds(900);
        header.inclusion_count = 15;
        header.including_files = 8;
        header.is_stable = true;
        header.is_external = true;
        analysis.dependencies.headers.push_back(header);

        SuggesterOptions options;
        SuggestionContext context{trace, analysis, options, {}};

        auto result = suggester_->suggest(context);
        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}
