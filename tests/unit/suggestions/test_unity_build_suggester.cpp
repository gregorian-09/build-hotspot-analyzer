//
// Created by gregorian-rayne on 03/09/26.
//

#include "bha/suggestions/unity_build_suggester.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace bha::suggestions {
    namespace {
        struct TempDir {
            fs::path root;
            explicit TempDir(const std::string& prefix) {
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                root = fs::temp_directory_path() / (prefix + std::to_string(stamp));
                fs::create_directories(root);
            }
            ~TempDir() {
                std::error_code ec;
                fs::remove_all(root, ec);
            }
        };

        void write_file(const fs::path& path, const std::string& content) {
            fs::create_directories(path.parent_path());
            std::ofstream out(path);
            out << content;
        }
    }  // namespace

    class UnityBuildSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override {
            suggester_ = std::make_unique<UnityBuildSuggester>();
        }

        std::unique_ptr<UnityBuildSuggester> suggester_;
    };

    TEST_F(UnityBuildSuggesterTest, NameAndType) {
        EXPECT_EQ(suggester_->name(), "UnityBuildSuggester");
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::UnityBuild);
    }

    TEST_F(UnityBuildSuggesterTest, RespectsMinFilesThreshold) {
        TempDir temp("bha-unity-min-files-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityMinFiles)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(2);

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult a;
        a.file = temp.root / "src" / "a.cpp";
        a.compile_time = std::chrono::milliseconds(120);
        analysis.files.push_back(a);
        analyzers::FileAnalysisResult b;
        b.file = temp.root / "src" / "b.cpp";
        b.compile_time = std::chrono::milliseconds(130);
        analysis.files.push_back(b);

        SuggesterOptions options;
        options.heuristics.unity_build.min_files_threshold = 3;
        options.heuristics.unity_build.files_per_unit = 10;

        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, GeneratesTargetScopedCMakeEditForMacroTarget) {
        TempDir temp("bha-unity-cmake-target-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityTarget)\n"
                   "include(CMakeParseArguments)\n"
                   "function(my_cc_library)\n"
                   "  set(options)\n"
                   "  set(oneValueArgs NAME)\n"
                   "  set(multiValueArgs SRCS)\n"
                   "  cmake_parse_arguments(MY \"${options}\" \"${oneValueArgs}\" \"${multiValueArgs}\" ${ARGN})\n"
                   "  add_library(${MY_NAME} ${MY_SRCS})\n"
                   "endfunction()\n"
                   "my_cc_library(\n"
                   "  NAME corelib\n"
                   "  SRCS\n"
                   "    src/a.cpp\n"
                   "    src/b.cpp\n"
                   ")\n");
        write_file(temp.root / "src" / "a.cpp", "static int sa() { return 1; }\nint a() { return sa(); }\n");
        write_file(temp.root / "src" / "b.cpp", "static int sb() { return 2; }\nint b() { return sb(); }\n");

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(3);

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult a;
        a.file = temp.root / "src" / "a.cpp";
        a.compile_time = std::chrono::milliseconds(200);
        analysis.files.push_back(a);
        analyzers::FileAnalysisResult b;
        b.file = temp.root / "src" / "b.cpp";
        b.compile_time = std::chrono::milliseconds(220);
        analysis.files.push_back(b);

        SuggesterOptions options;
        options.heuristics.unity_build.min_files_threshold = 2;
        options.heuristics.unity_build.files_per_unit = 10;

        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_FALSE(result.value().suggestions.empty());

        bool found_cmake_edit = false;
        bool found_global_edit = false;
        bool found_manual_unity_file = false;
        for (const auto& suggestion : result.value().suggestions) {
            for (const auto& edit : suggestion.edits) {
                const std::string file = edit.file.generic_string();
                if (file == (temp.root / "CMakeLists.txt").generic_string()) {
                    if (edit.new_text.find("set_property(TARGET corelib PROPERTY UNITY_BUILD ON)") != std::string::npos) {
                        found_cmake_edit = true;
                    }
                    if (edit.new_text.find("set(CMAKE_UNITY_BUILD ON)") != std::string::npos) {
                        found_global_edit = true;
                    }
                }
                if (file.find("_unity_") != std::string::npos && edit.file.extension() == ".cpp") {
                    found_manual_unity_file = true;
                }
            }
        }

        EXPECT_TRUE(found_cmake_edit);
        EXPECT_FALSE(found_global_edit);
        EXPECT_FALSE(found_manual_unity_file);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetAlreadyHasUnityEnabled) {
        TempDir temp("bha-unity-cmake-existing-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityExisting)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_property(TARGET core PROPERTY UNITY_BUILD ON)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(2);

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult a;
        a.file = temp.root / "src" / "a.cpp";
        a.compile_time = std::chrono::milliseconds(120);
        analysis.files.push_back(a);
        analyzers::FileAnalysisResult b;
        b.file = temp.root / "src" / "b.cpp";
        b.compile_time = std::chrono::milliseconds(140);
        analysis.files.push_back(b);

        SuggesterOptions options;
        options.heuristics.unity_build.min_files_threshold = 2;
        options.heuristics.unity_build.files_per_unit = 10;

        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsCrossTargetGroupsWhenNoSingleTargetOwnsAllFiles) {
        TempDir temp("bha-unity-cross-target-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCrossTarget)\n"
                   "add_library(alpha src/a.cpp)\n"
                   "add_library(beta src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(2);

        analyzers::AnalysisResult analysis;
        analyzers::FileAnalysisResult a;
        a.file = temp.root / "src" / "a.cpp";
        a.compile_time = std::chrono::milliseconds(120);
        analysis.files.push_back(a);
        analyzers::FileAnalysisResult b;
        b.file = temp.root / "src" / "b.cpp";
        b.compile_time = std::chrono::milliseconds(130);
        analysis.files.push_back(b);

        SuggesterOptions options;
        options.heuristics.unity_build.min_files_threshold = 2;
        options.heuristics.unity_build.files_per_unit = 10;

        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }
}  // namespace bha::suggestions
