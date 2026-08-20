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

        std::string json_escape(const std::string& value) {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value) {
                if (character == '\\' || character == '"') {
                    escaped.push_back('\\');
                }
                escaped.push_back(character);
            }
            return escaped;
        }

        void write_compile_database(
            const fs::path& root,
            const std::vector<fs::path>& sources,
            const std::vector<std::string>& extra_arguments = {}
        ) {
            std::ofstream out(root / "compile_commands.json");
            out << '[';
            for (std::size_t index = 0; index < sources.size(); ++index) {
                if (index != 0) {
                    out << ',';
                }
                const fs::path relative = fs::relative(sources[index], root);
                out << "{\"directory\":\"" << json_escape(root.string())
                    << "\",\"file\":\"" << json_escape(relative.generic_string())
                    << "\",\"arguments\":[\"clang++\",\"-std=c++20\"";
                for (const auto& argument : extra_arguments) {
                    out << ",\"" << json_escape(argument) << "\"";
                }
                out << ",\"-c\",\"" << json_escape(relative.generic_string()) << "\"]}";
            }
            out << ']';
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

    TEST_F(UnityBuildSuggesterTest, RequiresCompileCommandsForTargetSources) {
        TempDir temp("bha-unity-compile-commands-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCompileCommands)\n"
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
        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "unity.compile_commands.required");
    }

    TEST_F(UnityBuildSuggesterTest, GeneratesTargetScopedCMakeEditForBuiltinTarget) {
        TempDir temp("bha-unity-cmake-target-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityTarget)\n"
                   "add_library(corelib src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "static int sa() { return 1; }\nint a() { return sa(); }\n");
        write_file(temp.root / "src" / "b.cpp", "static int sb() { return 2; }\nint b() { return sb(); }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

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
        options.compile_commands_path = temp.root / "compile_commands.json";

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
                }
                if (file.find("_unity_") != std::string::npos && edit.file.extension() == ".cpp") {
                    found_manual_unity_file = true;
                }
            }
        }

        EXPECT_TRUE(found_cmake_edit);
        EXPECT_FALSE(found_global_edit);
        EXPECT_FALSE(found_manual_unity_file);
        EXPECT_EQ(result.value().suggestions.front().estimated_savings, Duration::zero());
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
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

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
        options.compile_commands_path = temp.root / "compile_commands.json";

        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetIsInMultiTargetUnityProperty) {
        TempDir temp("bha-unity-multi-target-property-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityMultiTargetProperty)\n"
                   "add_library(other src/other.cpp)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_property(TARGET other core PROPERTY UNITY_BUILD ON)\n");
        write_file(temp.root / "src" / "other.cpp", "int other() { return 0; }\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {
            temp.root / "src" / "other.cpp",
            temp.root / "src" / "a.cpp",
            temp.root / "src" / "b.cpp"
        });

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetUsesGroupUnityMode) {
        TempDir temp("bha-unity-group-mode-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityGroupMode)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_target_properties(core PROPERTIES UNITY_BUILD_MODE GROUP)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetInjectsCodeAroundUnityIncludes) {
        TempDir temp("bha-unity-injected-code-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityInjectedCode)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_target_properties(core PROPERTIES\n"
                   "  UNITY_BUILD_CODE_BEFORE_INCLUDE \"#define BHA_UNITY_MARKER 1\"\n"
                   ")\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenUnityPropertyHasNoValue) {
        TempDir temp("bha-unity-missing-property-value-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityMissingPropertyValue)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_property(TARGET core PROPERTY UNITY_BUILD)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetPropertiesUnityPropertyHasNoValue) {
        TempDir temp("bha-unity-missing-target-property-value-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityMissingTargetPropertyValue)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_target_properties(core PROPERTIES UNITY_BUILD)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, RejectsTargetSourcesWithoutBuiltinDeclaration) {
        TempDir temp("bha-unity-source-extension-only-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnitySourceExtensionOnly)\n"
                   "target_sources(core PRIVATE src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, RejectsTargetSourcesSplitAcrossCMakeFiles) {
        TempDir temp("bha-unity-cross-cmake-file-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCrossCMakeFile)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "add_subdirectory(extra)\n");
        write_file(temp.root / "extra" / "CMakeLists.txt",
                   "target_sources(core PRIVATE ../src/c.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_file(temp.root / "src" / "c.cpp", "int c() { return 3; }\n");
        write_compile_database(temp.root, {
            temp.root / "src" / "a.cpp",
            temp.root / "src" / "b.cpp",
            temp.root / "src" / "c.cpp"
        });

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {
                 temp.root / "src" / "a.cpp",
                 temp.root / "src" / "b.cpp",
                 temp.root / "src" / "c.cpp"
             }) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, IgnoresInterfaceSourcesWhenResolvingTargetSources) {
        TempDir temp("bha-unity-interface-sources-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityInterfaceSources)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "target_sources(core INTERFACE src/interface_a.cpp src/interface_b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_file(temp.root / "src" / "interface_a.cpp", "int interface_a() { return 3; }\n");
        write_file(temp.root / "src" / "interface_b.cpp", "int interface_b() { return 4; }\n");
        write_compile_database(temp.root, {
            temp.root / "src" / "a.cpp",
            temp.root / "src" / "b.cpp",
            temp.root / "src" / "interface_a.cpp",
            temp.root / "src" / "interface_b.cpp"
        });

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {
                 temp.root / "src" / "a.cpp",
                 temp.root / "src" / "b.cpp",
                 temp.root / "src" / "interface_a.cpp",
                 temp.root / "src" / "interface_b.cpp"
             }) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& sources = result.value().suggestions.front().secondary_files;
        ASSERT_EQ(sources.size(), 2u);
        EXPECT_EQ(sources[0].path, temp.root / "src" / "a.cpp");
        EXPECT_EQ(sources[1].path, temp.root / "src" / "b.cpp");
    }

    TEST_F(UnityBuildSuggesterTest, PreservesCMakeSourceOrderInValidatedTarget) {
        TempDir temp("bha-unity-source-order-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnitySourceOrder)\n"
                   "add_library(core src/z.cpp)\n"
                   "target_sources(core PRIVATE src/a.cpp)\n");
        write_file(temp.root / "src" / "z.cpp", "int z() { return 1; }\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "z.cpp", temp.root / "src" / "a.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "z.cpp", temp.root / "src" / "a.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        ASSERT_EQ(result.value().suggestions.size(), 1u);
        const auto& sources = result.value().suggestions.front().secondary_files;
        ASSERT_EQ(sources.size(), 2u);
        EXPECT_EQ(sources[0].path, temp.root / "src" / "z.cpp");
        EXPECT_EQ(sources[1].path, temp.root / "src" / "a.cpp");
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
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

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
        options.compile_commands_path = temp.root / "compile_commands.json";

        const SuggestionContext context{trace, analysis, options, temp.root};
        auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, RejectsMergedTranslationUnitWithStaticDefinitionCollision) {
        TempDir temp("bha-unity-static-collision-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCollision)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp",
                   "static int collision() { return 1; }\n"
                   "int a() { return collision(); }\n");
        write_file(temp.root / "src" / "b.cpp",
                   "static int collision() { return 2; }\n"
                   "int b() { return collision(); }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_GT(result.value().items_skipped, 0u);
    }

    TEST_F(UnityBuildSuggesterTest, RejectsMacroTargetWithoutExactCMakeSourceOwnership) {
        TempDir temp("bha-unity-macro-target-");
        write_file(temp.root / "CMakeLists.txt",
                   "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityMacro)\n"
                   "function(my_library)\n"
                   "  add_library(${ARGV0} ${ARGN})\n"
                   "endfunction()\n"
                   "my_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}) {
            analyzers::FileAnalysisResult file;
            file.file = source;
            file.compile_time = std::chrono::milliseconds(200);
            analysis.files.push_back(file);
        }
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
    }
}  // namespace bha::suggestions
