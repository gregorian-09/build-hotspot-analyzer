#include "bha/suggestions/include_suggester.hpp"
#include "bha/suggestions/pch_suggester.hpp"
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

        BuildTrace make_unreal_trace(const fs::path& project_root, const std::size_t file_count) {
            BuildTrace trace;
            for (std::size_t i = 0; i < file_count; ++i) {
                CompilationUnit unit;
                unit.source_file = project_root / "Source" / "CoreModule" / "Private" /
                    ("File" + std::to_string(i) + ".cpp");
                unit.metrics.total_time = std::chrono::milliseconds(180);
                IncludeInfo include;
                include.header = project_root / "Source" / "CoreModule" / "Public" / "CoreModule.h";
                include.parse_time = std::chrono::milliseconds(90);
                unit.includes.push_back(include);
                trace.total_time += unit.metrics.total_time;
                trace.units.push_back(std::move(unit));
            }
            return trace;
        }

        SuggestionContext make_context(const fs::path& project_root) {
            static analyzers::AnalysisResult analysis;
            static SuggesterOptions options;
            options = SuggesterOptions{};
            options.heuristics.unreal.enabled = true;
            options.heuristics.unreal.auto_detect = false;

            static BuildTrace trace;
            trace = make_unreal_trace(project_root, 10);

            return SuggestionContext{trace, analysis, options, project_root};
        }
    }

    TEST(UnrealModeSuggesterTest, IncludeSuggesterEmitsModuleLevelIWYUSuggestion) {
        TempDir temp("bha-unreal-iwyu-");
        write_file(temp.root / "Game.uproject", "{\n}");
        write_file(
            temp.root / "Source" / "CoreModule" / "CoreModule.Build.cs",
            "using UnrealBuildTool;\n"
            "public class CoreModule : ModuleRules {\n"
            "  public CoreModule(ReadOnlyTargetRules Target) : base(Target) {\n"
            "    bEnforceIWYU = false;\n"
            "  }\n"
            "}\n"
        );

        IncludeSuggester suggester;
        auto context = make_context(temp.root);
        auto result = suggester.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::IncludeRemoval);
        EXPECT_TRUE(suggestion.title.find("Unreal Module IWYU") != std::string::npos);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
    }

    TEST(UnrealModeSuggesterTest, PCHSuggesterEmitsModuleLevelPCHSuggestion) {
        TempDir temp("bha-unreal-pch-");
        write_file(temp.root / "Game.uproject", "{\n}");
        write_file(
            temp.root / "Source" / "CoreModule" / "CoreModule.Build.cs",
            "using UnrealBuildTool;\n"
            "public class CoreModule : ModuleRules {\n"
            "  public CoreModule(ReadOnlyTargetRules Target) : base(Target) {\n"
            "    PCHUsage = PCHUsageMode.NoPCHs;\n"
            "  }\n"
            "}\n"
        );

        PCHSuggester suggester;
        auto context = make_context(temp.root);
        auto result = suggester.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::PCHOptimization);
        EXPECT_TRUE(suggestion.title.find("Unreal Module PCH") != std::string::npos);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
    }

    TEST(UnrealModeSuggesterTest, UnitySuggesterEmitsModuleLevelUnitySuggestion) {
        TempDir temp("bha-unreal-unity-");
        write_file(temp.root / "Game.uproject", "{\n}");
        write_file(
            temp.root / "Source" / "CoreModule" / "CoreModule.Build.cs",
            "using UnrealBuildTool;\n"
            "public class CoreModule : ModuleRules {\n"
            "  public CoreModule(ReadOnlyTargetRules Target) : base(Target) {\n"
            "    bUseUnity = false;\n"
            "  }\n"
            "}\n"
        );

        UnityBuildSuggester suggester;
        auto context = make_context(temp.root);
        auto result = suggester.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.type, SuggestionType::UnityBuild);
        EXPECT_TRUE(suggestion.title.find("Unreal Module Unity") != std::string::npos);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
    }
}
