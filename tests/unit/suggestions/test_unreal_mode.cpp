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
        EXPECT_TRUE(suggestion.title.find("UBT") != std::string::npos);
        EXPECT_TRUE(suggestion.description.find("UnrealBuildTool (UBT)") != std::string::npos);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_TRUE(suggestion.edits.front().new_text.find("bEnforceIWYU = true;") != std::string::npos);
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
        EXPECT_TRUE(suggestion.title.find("UBT") != std::string::npos);
        EXPECT_TRUE(suggestion.description.find("UnrealBuildTool (UBT)") != std::string::npos);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_TRUE(suggestion.edits.front().new_text.find("PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;") != std::string::npos);
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
        EXPECT_TRUE(suggestion.title.find("UBT") != std::string::npos);
        EXPECT_TRUE(suggestion.description.find("UnrealBuildTool (UBT)") != std::string::npos);
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 1u);
        EXPECT_TRUE(suggestion.edits.front().new_text.find("bUseUnity = true;") != std::string::npos);
    }

    TEST(UnrealModeSuggesterTest, IncludeSuggesterBlocksAutoApplyForGeneratedIncludeOrderViolation) {
        TempDir temp("bha-unreal-iwyu-generated-order-");
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
        write_file(
            temp.root / "Source" / "CoreModule" / "Public" / "CoreModule.h",
            "#pragma once\n"
            "#include \"CoreMinimal.h\"\n"
            "#include \"CoreModule.generated.h\"\n"
            "#include \"AfterGenerated.h\"\n"
        );
        write_file(
            temp.root / "Source" / "CoreModule" / "Public" / "AfterGenerated.h",
            "#pragma once\n"
        );

        IncludeSuggester suggester;
        auto context = make_context(temp.root);
        auto result = suggester.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_TRUE(suggestion.auto_apply_blocked_reason->find("generated.h") != std::string::npos);
    }

    TEST(UnrealModeSuggesterTest, UnitySuggesterAlsoEditsTargetOverridesWhenDisabled) {
        TempDir temp("bha-unreal-unity-target-");
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
        write_file(
            temp.root / "Source" / "GameEditor.Target.cs",
            "using UnrealBuildTool;\n"
            "public class GameEditorTarget : TargetRules {\n"
            "  public GameEditorTarget(ReadOnlyTargetRules Target) : base(Target) {\n"
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
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 2u);
        EXPECT_TRUE(suggestion.description.find("GameEditor") != std::string::npos);
    }

    TEST(UnrealModeSuggesterTest, UnitySuggesterEditsAdaptiveUnityTargetOverridesWhenDisabled) {
        TempDir temp("bha-unreal-unity-adaptive-target-");
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
        write_file(
            temp.root / "Source" / "GameEditor.Target.cs",
            "using UnrealBuildTool;\n"
            "public class GameEditorTarget : TargetRules {\n"
            "  public GameEditorTarget(ReadOnlyTargetRules Target) : base(Target) {\n"
            "    bUseAdaptiveUnityBuild = false;\n"
            "  }\n"
            "}\n"
        );

        UnityBuildSuggester suggester;
        auto context = make_context(temp.root);
        auto result = suggester.suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& suggestion = result.value().suggestions.front();
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::DirectEdits);
        EXPECT_TRUE(suggestion.is_safe);
        ASSERT_EQ(suggestion.edits.size(), 2u);
        EXPECT_TRUE(suggestion.description.find("bUseAdaptiveUnityBuild = true;") != std::string::npos);
    }

    TEST(UnrealModeSuggesterTest, IncludeSuggesterBlocksAutoApplyForAmbiguousModuleRules) {
        TempDir temp("bha-unreal-iwyu-ambiguous-module-");
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
        write_file(
            temp.root / "Plugins" / "Mirror" / "Source" / "CoreModule" / "CoreModule.Build.cs",
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
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_TRUE(suggestion.auto_apply_blocked_reason->find("Ambiguous Unreal module rules") != std::string::npos);
    }

    TEST(UnrealModeSuggesterTest, PCHSuggesterBlocksAutoApplyForAmbiguousModuleRules) {
        TempDir temp("bha-unreal-pch-ambiguous-module-");
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
        write_file(
            temp.root / "Plugins" / "Mirror" / "Source" / "CoreModule" / "CoreModule.Build.cs",
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
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_TRUE(suggestion.auto_apply_blocked_reason->find("Ambiguous Unreal module rules") != std::string::npos);
    }

    TEST(UnrealModeSuggesterTest, UnitySuggesterBlocksAutoApplyForAmbiguousTargetRules) {
        TempDir temp("bha-unreal-unity-ambiguous-target-");
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
        write_file(
            temp.root / "Source" / "GameEditor.Target.cs",
            "using UnrealBuildTool;\n"
            "public class GameEditorTarget : TargetRules {\n"
            "  public GameEditorTarget(ReadOnlyTargetRules Target) : base(Target) {\n"
            "    bUseUnity = false;\n"
            "  }\n"
            "}\n"
        );
        write_file(
            temp.root / "Plugins" / "Mirror" / "Source" / "GameEditor.Target.cs",
            "using UnrealBuildTool;\n"
            "public class GameEditorTarget : TargetRules {\n"
            "  public GameEditorTarget(ReadOnlyTargetRules Target) : base(Target) {\n"
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
        EXPECT_EQ(suggestion.application_mode, SuggestionApplicationMode::Advisory);
        EXPECT_FALSE(suggestion.is_safe);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_TRUE(suggestion.auto_apply_blocked_reason->find("ambiguous Unreal target rules") != std::string::npos);
    }
}
