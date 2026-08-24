//
// Created by gregorian-rayne on 03/09/26.
//

#include "bha/suggestions/unity_build_suggester.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

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

        using FileApiTargetSpec = std::pair<std::string, std::vector<fs::path>>;

        void write_file_api(const fs::path& root, const std::vector<FileApiTargetSpec>& targets,
                            const bool has_glob_dependency = false) {
            const fs::path reply = root / ".cmake" / "api" / "v1" / "reply";
            fs::create_directories(reply);

            std::ofstream index_file(reply / "index-000000.json");
            index_file << "{\"objects\":["
                       << "{\"kind\":\"codemodel\",\"version\":{\"major\":2,\"minor\":0},"
                          "\"jsonFile\":\"codemodel-v2.json\"},"
                       << "{\"kind\":\"cmakeFiles\",\"version\":{\"major\":1,\"minor\":0},"
                          "\"jsonFile\":\"cmakeFiles-v1.json\"}]}";

            std::ofstream codemodel(reply / "codemodel-v2.json");
            codemodel << "{\"kind\":\"codemodel\",\"version\":{\"major\":2,\"minor\":0},"
                         "\"paths\":{\"source\":\""
                      << json_escape(root.generic_string()) << "\",\"build\":\""
                      << json_escape((root / "build").generic_string())
                      << "\"},\"configurations\":[{\"name\":\"default\",\"targets\":[";
            for (std::size_t index = 0; index < targets.size(); ++index) {
                if (index != 0) {
                    codemodel << ',';
                }
                const std::string file_name = "target-" + targets[index].first + ".json";
                codemodel << "{\"name\":\"" << json_escape(targets[index].first)
                          << "\",\"directoryIndex\":0,\"projectIndex\":0,\"jsonFile\":\"" << file_name << "\"}";
            }
            codemodel << "]}]}";

            std::ofstream cmake_files(reply / "cmakeFiles-v1.json");
            cmake_files << "{\"kind\":\"cmakeFiles\",\"version\":{\"major\":1,\"minor\":0},"
                           "\"paths\":{\"source\":\""
                        << json_escape(root.generic_string()) << "\",\"build\":\""
                        << json_escape((root / "build").generic_string())
                        << "\"},\"inputs\":[{\"path\":\"CMakeLists.txt\"}]";
            if (has_glob_dependency) {
                cmake_files << ",\"globsDependent\":[{\"expression\":\"src/*.cpp\","
                               "\"recurse\":false,\"files\":[\"src/a.cpp\"]}]";
            }
            cmake_files << "}";

            std::ifstream cmake_input(root / "CMakeLists.txt");
            const std::string cmake_content{std::istreambuf_iterator<char>(cmake_input),
                                            std::istreambuf_iterator<char>()};
            for (const auto& [target_name, sources] : targets) {
                const fs::path target_file = reply / ("target-" + target_name + ".json");
                std::ofstream target(target_file);
                const std::string target_declaration = "add_library(" + target_name;
                const std::size_t exact_declaration = cmake_content.find(target_declaration);
                const std::size_t declaration_offset =
                    exact_declaration == std::string::npos ? cmake_content.find("add_library(") : exact_declaration;
                const std::size_t target_line_for_file_api =
                    declaration_offset == std::string::npos
                        ? 1
                        : static_cast<std::size_t>(std::count(
                              cmake_content.begin(),
                              cmake_content.begin() + static_cast<std::ptrdiff_t>(declaration_offset), '\n')) +
                              1;
                target << "{\"name\":\"" << json_escape(target_name)
                       << "\",\"type\":\"STATIC_LIBRARY\",\"backtrace\":1,"
                          "\"backtraceGraph\":{\"commands\":[\"add_library\"],"
                          "\"files\":[\"CMakeLists.txt\"],\"nodes\":[{\"file\":0},{"
                          "\"file\":0,\"line\":"
                       << target_line_for_file_api
                       << ",\"command\":0}]},"
                          "\"compileGroups\":[{\"sourceIndexes\":[";
                for (std::size_t index = 0; index < sources.size(); ++index) {
                    if (index != 0) {
                        target << ',';
                    }
                    target << index;
                }
                target << "],\"language\":\"CXX\"}],\"sources\":[";
                for (std::size_t index = 0; index < sources.size(); ++index) {
                    if (index != 0) {
                        target << ',';
                    }
                    target << "{\"path\":\"" << json_escape(fs::relative(sources[index], root).generic_string())
                           << "\",\"compileGroupIndex\":0}";
                }
                target << "]}";
            }
        }

        void write_compile_database(const fs::path& root, const std::vector<fs::path>& sources,
                                    const std::vector<std::string>& extra_arguments = {},
                                    const std::vector<FileApiTargetSpec>& file_api_targets = {}) {
            std::ofstream out(root / "compile_commands.json");
            out << '[';
            for (std::size_t index = 0; index < sources.size(); ++index) {
                if (index != 0) {
                    out << ',';
                }
                const fs::path relative = fs::relative(sources[index], root);
                out << "{\"directory\":\"" << json_escape(root.string()) << "\",\"file\":\""
                    << json_escape(relative.generic_string()) << "\",\"arguments\":[\"clang++\",\"-std=c++20\"";
                for (const auto& argument : extra_arguments) {
                    out << ",\"" << json_escape(argument) << "\"";
                }
                out << ",\"-c\",\"" << json_escape(relative.generic_string()) << "\"]}";
            }
            out << ']';
            out.close();
            std::string default_target_name = "core";
            std::ifstream cmake_input(root / "CMakeLists.txt");
            std::string line;
            while (std::getline(cmake_input, line)) {
                const std::size_t open = line.find("add_library(");
                if (open == std::string::npos) {
                    continue;
                }
                const std::size_t name_start = open + std::string("add_library(").size();
                const std::size_t name_end = line.find_first_of(" \t)\r", name_start);
                if (name_end != std::string::npos && name_end > name_start) {
                    const std::string candidate = line.substr(name_start, name_end - name_start);
                    if (candidate.find('$') == std::string::npos && candidate.find('<') == std::string::npos) {
                        default_target_name = candidate;
                    }
                }
                break;
            }
            write_file_api(root, file_api_targets.empty()
                                     ? std::vector<FileApiTargetSpec>{{default_target_name, sources}}
                                     : file_api_targets);
        }
    }  // namespace

    class UnityBuildSuggesterTest : public ::testing::Test {
    protected:
        void SetUp() override { suggester_ = std::make_unique<UnityBuildSuggester>(); }

        std::unique_ptr<UnityBuildSuggester> suggester_;
    };

    TEST_F(UnityBuildSuggesterTest, NameAndType) {
        EXPECT_EQ(suggester_->name(), "UnityBuildSuggester");
        EXPECT_EQ(suggester_->suggestion_type(), SuggestionType::UnityBuild);
    }

    TEST_F(UnityBuildSuggesterTest, RequiresCompileCommandsForTargetSources) {
        TempDir temp("bha-unity-compile-commands-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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

    TEST_F(UnityBuildSuggesterTest, RequiresConfiguredCMakeFileApiModel) {
        TempDir temp("bha-unity-file-api-required-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                                                 "project(UnityFileApiRequired)\n"
                                                 "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});
        std::error_code error;
        fs::remove_all(temp.root / ".cmake", error);

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
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "unity.file_api.required");
    }

    TEST_F(UnityBuildSuggesterTest, RejectsStaleCMakeFileApiReply) {
        TempDir temp("bha-unity-file-api-stale-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                                                 "project(UnityFileApiStale)\n"
                                                 "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        const fs::path index = temp.root / ".cmake" / "api" / "v1" / "reply" / "index-000000.json";
        std::error_code error;
        fs::last_write_time(index, fs::file_time_type::clock::now() - std::chrono::hours(1), error);
        ASSERT_FALSE(error);

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "unity.file_api.required");
    }

    TEST_F(UnityBuildSuggesterTest, RejectsUnverifiedGlobDependentModel) {
        TempDir temp("bha-unity-file-api-glob-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                                                 "project(UnityFileApiGlob)\n"
                                                 "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});
        write_file_api(temp.root, {{"core", {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}}}, true);

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        const SuggestionContext context{trace, analysis, options, temp.root};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        ASSERT_EQ(result.value().diagnostics.size(), 1u);
        EXPECT_EQ(result.value().diagnostics.front().code, "unity.file_api.required");
    }

    TEST_F(UnityBuildSuggesterTest, CancellationDoesNotReportFileApiFailure) {
        TempDir temp("bha-unity-cancelled-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                                                 "project(UnityCancelled)\n"
                                                 "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.compile_commands_path = temp.root / "compile_commands.json";
        std::atomic<bool> cancelled{true};
        const SuggestionContext context{trace, analysis, options, temp.root, &cancelled};

        const auto result = suggester_->suggest(context);

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(result.value().suggestions.empty());
        EXPECT_TRUE(result.value().diagnostics.empty());
    }

    TEST_F(UnityBuildSuggesterTest, GeneratesTargetScopedCMakeEditForBuiltinTarget) {
        TempDir temp("bha-unity-cmake-target-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
                    if (edit.new_text.find("set_property(TARGET corelib PROPERTY UNITY_BUILD ON)") !=
                        std::string::npos) {
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

    TEST_F(UnityBuildSuggesterTest, ParsesBracketCommentsAndMultilineCommands) {
        TempDir temp("bha-unity-cmake-lexical-state-");
        write_file(temp.root / "CMakeLists.txt", "#[=[\n"
                                                  "add_library(fake src/fake.cpp)\n"
                                                  "set_property(TARGET core PROPERTY UNITY_BUILD ON)\n"
                                                  "]=]\n"
                                                  "project(UnityLexicalState)\n"
                                                  "add_library(core\n"
                                                  "  src/a.cpp\n"
                                                  "  src/b.cpp\n"
                                                  ")\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}, {},
                               {{"core", {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}}});

        BuildTrace trace;
        trace.total_time = std::chrono::seconds(2);

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
        ASSERT_FALSE(result.value().suggestions.empty());
        EXPECT_TRUE(std::ranges::any_of(result.value().suggestions, [&](const auto& suggestion) {
            return std::ranges::any_of(suggestion.edits, [&](const auto& edit) {
                return edit.file == temp.root / "CMakeLists.txt" &&
                       edit.new_text.find("set_property(TARGET core PROPERTY UNITY_BUILD ON)") !=
                           std::string::npos;
            });
        }));
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetAlreadyHasUnityEnabled) {
        TempDir temp("bha-unity-cmake-existing-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
    }

    TEST_F(UnityBuildSuggesterTest, SkipsWhenTargetIsInMultiTargetUnityProperty) {
        TempDir temp("bha-unity-multi-target-property-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityMultiTargetProperty)\n"
                   "add_library(other src/other.cpp)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_property(TARGET other core PROPERTY UNITY_BUILD ON)\n");
        write_file(temp.root / "src" / "other.cpp", "int other() { return 0; }\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(
            temp.root, {temp.root / "src" / "other.cpp", temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}, {},
            {{"other", {temp.root / "src" / "other.cpp"}},
             {"core", {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}}});

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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityGroupMode)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "set_target_properties(core PROPERTIES UNITY_BUILD_MODE GROUP)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}, {});

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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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

    TEST_F(UnityBuildSuggesterTest, SkipsWhenUnityPropertyTargetIsDynamic) {
        TempDir temp("bha-unity-dynamic-property-target-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                                                 "project(UnityDynamicPropertyTarget)\n"
                                                 "add_library(core src/a.cpp src/b.cpp)\n"
                                                 "set(BHA_TARGET core)\n"
                                                 "set_property(TARGET ${BHA_TARGET} PROPERTY UNITY_BUILD ON)\n");
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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
    }

    TEST_F(UnityBuildSuggesterTest, AcceptsTargetSourcesSplitAcrossCMakeFilesFromConfiguredModel) {
        TempDir temp("bha-unity-cross-cmake-file-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCrossCMakeFile)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                   "add_subdirectory(extra)\n");
        write_file(temp.root / "extra" / "CMakeLists.txt", "target_sources(core PRIVATE ../src/c.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_file(temp.root / "src" / "c.cpp", "int c() { return 3; }\n");
        write_compile_database(temp.root,
                               {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp", temp.root / "src" / "c.cpp"});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source :
             {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp", temp.root / "src" / "c.cpp"}) {
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
        EXPECT_EQ(result.value().suggestions.front().secondary_files.size(), 3u);
    }

    TEST_F(UnityBuildSuggesterTest, IgnoresInterfaceSourcesWhenResolvingTargetSources) {
        TempDir temp("bha-unity-interface-sources-");
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityInterfaceSources)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n"
                                                 "target_sources(core INTERFACE src/interface_a.cpp "
                                                 "src/interface_b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_file(temp.root / "src" / "interface_a.cpp", "int interface_a() { return 3; }\n");
        write_file(temp.root / "src" / "interface_b.cpp", "int interface_b() { return 4; }\n");
        write_compile_database(temp.root,
                               {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp",
                                temp.root / "src" / "interface_a.cpp", temp.root / "src" / "interface_b.cpp"},
                               {}, {{"core", {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}}});

        BuildTrace trace;
        analyzers::AnalysisResult analysis;
        for (const auto& source : {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp",
                                   temp.root / "src" / "interface_a.cpp", temp.root / "src" / "interface_b.cpp"}) {
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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCrossTarget)\n"
                   "add_library(alpha src/a.cpp)\n"
                   "add_library(beta src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "int a() { return 1; }\n");
        write_file(temp.root / "src" / "b.cpp", "int b() { return 2; }\n");
        write_compile_database(temp.root, {temp.root / "src" / "a.cpp", temp.root / "src" / "b.cpp"}, {},
                               {{"alpha", {temp.root / "src" / "a.cpp"}}, {"beta", {temp.root / "src" / "b.cpp"}}});

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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
                   "project(UnityCollision)\n"
                   "add_library(core src/a.cpp src/b.cpp)\n");
        write_file(temp.root / "src" / "a.cpp", "static int collision() { return 1; }\n"
                   "int a() { return collision(); }\n");
        write_file(temp.root / "src" / "b.cpp", "static int collision() { return 2; }\n"
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
        write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n"
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
