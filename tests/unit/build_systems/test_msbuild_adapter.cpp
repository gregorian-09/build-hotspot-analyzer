//
// Created by gregorian on 09/12/2025.
//

#include <gtest/gtest.h>
#include "bha/build_systems/msbuild_adapter.h"
#include <filesystem>
#include <fstream>

using namespace bha::build_systems;
namespace fs = std::filesystem;

class MSBuildAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_msbuild_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir / "solution");
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    fs::path temp_dir;

    void CreateSolutionFile() const
    {
        std::ofstream sln((temp_dir / "solution" / "project.sln"));
        sln << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
        sln << "# Visual Studio 16\n";
        sln << "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Project1\", \"Project1\\Project1.vcxproj\", \"{12345678-1234-1234-1234-123456789012}\"\n";
        sln << "EndProject\n";
        sln << "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Project2\", \"Project2\\Project2.vcxproj\", \"{87654321-4321-4321-4321-210987654321}\"\n";
        sln << "EndProject\n";
        sln.close();
    }

    void CreateProjectFile(const std::string& project_name) const
    {
        fs::create_directories(temp_dir / "solution" / project_name);
        std::ofstream vcxproj((temp_dir / "solution" / project_name / (project_name + ".vcxproj")));
        vcxproj << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        vcxproj << "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
        vcxproj << "  <ItemDefinitionGroup>\n";
        vcxproj << "    <ClCompile>\n";
        vcxproj << "      <WarningLevel>Level3</WarningLevel>\n";
        vcxproj << "    </ClCompile>\n";
        vcxproj << "  </ItemDefinitionGroup>\n";
        vcxproj << "  <ItemGroup>\n";
        vcxproj << "    <ClCompile Include=\"" << project_name << ".cpp\" />\n";
        vcxproj << "    <ClCompile Include=\"helper.cpp\" />\n";
        vcxproj << "  </ItemGroup>\n";
        vcxproj << "  <PropertyGroup>\n";
        vcxproj << "    <Configuration>Debug</Configuration>\n";
        vcxproj << "    <Platform>x64</Platform>\n";
        vcxproj << "  </PropertyGroup>\n";
        vcxproj << "</Project>\n";
        vcxproj.close();
    }

    void CreateMSBuildLog() const
    {
        std::ofstream log((temp_dir / "solution" / "msbuild.log"));
        log << "Project1.cpp\n";
        log << "Project2.cpp\n";
        log.close();
    }

    void CreateTraceFile() const
    {
        std::ofstream trace((temp_dir / "solution" / "build.json"));
        trace << "[]";
        trace.close();
    }
};

TEST_F(MSBuildAdapterTest, DetectMSBuildBuildSystem) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.detect_build_system((temp_dir / "solution").string());

    ASSERT_TRUE(result.is_success());
    const auto& info = result.value();
    EXPECT_EQ(info.type, BuildSystemType::MSBUILD);
    EXPECT_EQ(info.build_directory, (temp_dir / "solution").string());
}

TEST_F(MSBuildAdapterTest, ExtractCompileCommands) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    CreateProjectFile("Project2");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    auto commands = result.value();
    EXPECT_GT(commands.size(), 0);
    for (const auto& cmd : commands) {
        EXPECT_TRUE(cmd.directory.find("bha_msbuild_test") != std::string::npos);
        EXPECT_NE(cmd.command, "");
        EXPECT_TRUE(cmd.file.find(".cpp") != std::string::npos);
    }
}

TEST_F(MSBuildAdapterTest, ExtractCompileCommandsWithoutProjects) {
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    const auto& commands = result.value();
    EXPECT_EQ(commands.size(), 0);
}

TEST_F(MSBuildAdapterTest, ParseSolution) {
    CreateSolutionFile();

    auto result = MSBuildAdapter::parse_solution((temp_dir / "solution" / "project.sln").string());

    ASSERT_TRUE(result.is_success());
    const auto& projects = result.value();
    EXPECT_EQ(projects.size(), 2);
    EXPECT_EQ(projects[0].name, "Project1");
    EXPECT_EQ(projects[1].name, "Project2");
}

TEST_F(MSBuildAdapterTest, ParseSolutionNonexistent) {
    auto result = MSBuildAdapter::parse_solution((temp_dir / "solution" / "nonexistent.sln").string());

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(MSBuildAdapterTest, ExtractCompileCommandsFromMultipleProjects) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    CreateProjectFile("Project2");
    CreateProjectFile("Project3");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.extract_compile_commands();

    ASSERT_TRUE(result.is_success());
    const auto& commands = result.value();
    EXPECT_EQ(commands.size(), 6); // 2 files per project * 3 projects
}

TEST_F(MSBuildAdapterTest, GetTraceFiles) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    CreateMSBuildLog();
    CreateTraceFile();
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.get_trace_files((temp_dir / "solution").string());

    ASSERT_TRUE(result.is_success());
    auto files = result.value();
    EXPECT_GT(files.size(), 0);
    bool found_log = false;
    for (const auto& file : files) {
        if (file.find("msbuild.log") != std::string::npos) {
            found_log = true;
        }
    }
    EXPECT_TRUE(found_log);
}

TEST_F(MSBuildAdapterTest, GetTraceFilesWhenNoneExist) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.get_trace_files((temp_dir / "solution").string());

    ASSERT_TRUE(result.is_success());
    const auto& files = result.value();
    EXPECT_EQ(files.size(), 0);
}

TEST_F(MSBuildAdapterTest, GetTargets) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    CreateProjectFile("Project2");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_EQ(targets.size(), 2);
    EXPECT_TRUE(targets.contains("Project1"));
    EXPECT_TRUE(targets.contains("Project2"));
}

TEST_F(MSBuildAdapterTest, GetTargetsWithoutProjects) {
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.get_targets();

    ASSERT_TRUE(result.is_success());
    const auto& targets = result.value();
    EXPECT_EQ(targets.size(), 0);
}

TEST_F(MSBuildAdapterTest, GetBuildOrder) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    CreateProjectFile("Project2");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.get_build_order();

    ASSERT_TRUE(result.is_success());
    const auto& order = result.value();
    EXPECT_EQ(order.size(), 2);
    EXPECT_EQ(order[0], "Project1");
    EXPECT_EQ(order[1], "Project2");
}

TEST_F(MSBuildAdapterTest, GetBuildOrderWithoutProjects) {
    MSBuildAdapter adapter((temp_dir / "solution").string());

    if (auto result = adapter.get_build_order(); result.is_success()) {
        const auto& order = result.value();
        EXPECT_EQ(order.size(), 0);
    } else {
        EXPECT_TRUE(true);
    }
}

TEST_F(MSBuildAdapterTest, EnableTracingForMSVC) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.enable_tracing((temp_dir / "solution").string(), "msvc");

    ASSERT_TRUE(result.is_success());
    EXPECT_TRUE(result.value());

    // Verify the project file was modified
    std::ifstream project((temp_dir / "solution" / "Project1" / "Project1.vcxproj"));
    std::string content((std::istreambuf_iterator<char>(project)),
                       std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("/Bt+") != std::string::npos);
}

TEST_F(MSBuildAdapterTest, EnableTracingForNonMSVC) {
    CreateSolutionFile();
    CreateProjectFile("Project1");
    MSBuildAdapter adapter((temp_dir / "solution").string());

    auto result = adapter.enable_tracing((temp_dir / "solution").string(), "gcc");

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::UNSUPPORTED_FORMAT);
}

TEST_F(MSBuildAdapterTest, EnableTracingWithoutProjects) {
    MSBuildAdapter adapter((temp_dir / "solution").string());

    const auto result = adapter.enable_tracing((temp_dir / "solution").string(), "msvc");

    ASSERT_TRUE(result.is_failure());
}