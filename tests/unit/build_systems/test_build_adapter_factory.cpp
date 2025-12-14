//
// Created by gregorian on 10/12/2025.
//

#include <gtest/gtest.h>
#include "bha/build_systems/build_adapter.h"
#include <filesystem>
#include <fstream>

using namespace bha::build_systems;
namespace fs = std::filesystem;

class BuildAdapterFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_factory_test";
        fs::remove_all(temp_dir);
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }

    fs::path temp_dir;

    void CreateCMakeEnvironment() const
    {
        fs::create_directories(temp_dir / "build");
        std::ofstream cache(temp_dir / "build" / "CMakeCache.txt");
        cache << "CMAKE_VERSION:UNINITIALIZED=3.22.0\n";
        cache << "CMAKE_HOME_DIRECTORY:INTERNAL=/path/to/source\n";
        cache.close();

        std::ofstream commands(temp_dir / "build" / "compile_commands.json");
        commands << "[]";
        commands.close();
    }

    void CreateNinjaEnvironment() const
    {
        fs::create_directories(temp_dir / "build");
        std::ofstream ninja_file(temp_dir / "build" / "build.ninja");
        ninja_file << "rule cc\n";
        ninja_file << "  command = g++ -c $in -o $out\n";
        ninja_file.close();
    }

    void CreateMakeEnvironment() const
    {
        fs::create_directories(temp_dir / "build");
        std::ofstream makefile(temp_dir / "build" / "Makefile");
        makefile << ".PHONY: all\n";
        makefile << "all: target.o\n";
        makefile.close();
    }

    void CreateMSBuildEnvironment() const
    {
        fs::create_directories(temp_dir / "build");
        std::ofstream sln(temp_dir / "build" / "project.sln");
        sln << "Microsoft Visual Studio Solution File\n";
        sln.close();
    }
};

TEST_F(BuildAdapterFactoryTest, DetectCMakeBuildSystem) {
    CreateCMakeEnvironment();

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), BuildSystemType::CMAKE);
}

TEST_F(BuildAdapterFactoryTest, DetectNinjaBuildSystem) {
    CreateNinjaEnvironment();

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), BuildSystemType::NINJA);
}

TEST_F(BuildAdapterFactoryTest, DetectMakeBuildSystem) {
    CreateMakeEnvironment();

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), BuildSystemType::MAKE);
}

TEST_F(BuildAdapterFactoryTest, DetectMSBuildBuildSystem) {
    CreateMSBuildEnvironment();

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), BuildSystemType::MSBUILD);
}

TEST_F(BuildAdapterFactoryTest, DetectUnknownBuildSystem) {
    fs::create_directories(temp_dir / "build");

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(BuildAdapterFactoryTest, CreateCMakeAdapter) {
    CreateCMakeEnvironment();

    auto result = BuildAdapterFactory::create_adapter((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto adapter = std::move(result).value();
    EXPECT_NE(adapter.get(), nullptr);
}

TEST_F(BuildAdapterFactoryTest, CreateNinjaAdapter) {
    CreateNinjaEnvironment();

    auto result = BuildAdapterFactory::create_adapter((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto adapter = std::move(result).value();
    EXPECT_NE(adapter.get(), nullptr);
}

TEST_F(BuildAdapterFactoryTest, CreateMakeAdapter) {
    CreateMakeEnvironment();

    auto result = BuildAdapterFactory::create_adapter((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto adapter = std::move(result).value();
    EXPECT_NE(adapter.get(), nullptr);
}

TEST_F(BuildAdapterFactoryTest, CreateMSBuildAdapter) {
    CreateMSBuildEnvironment();

    auto result = BuildAdapterFactory::create_adapter((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    const auto adapter = std::move(result).value();
    EXPECT_NE(adapter.get(), nullptr);
}

TEST_F(BuildAdapterFactoryTest, CreateAdapterForUnknownBuildSystem) {
    fs::create_directories(temp_dir / "build");

    auto result = BuildAdapterFactory::create_adapter((temp_dir / "build").string());

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, bha::core::ErrorCode::FILE_NOT_FOUND);
}

TEST_F(BuildAdapterFactoryTest, CMakePriorityOverNinja) {
    CreateCMakeEnvironment();
    std::ofstream ninja(temp_dir / "build" / "build.ninja");
    ninja << "rule cc\n";
    ninja.close();

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), BuildSystemType::CMAKE);
}

TEST_F(BuildAdapterFactoryTest, NinjaPriorityOverMake) {
    CreateMakeEnvironment();
    std::ofstream ninja(temp_dir / "build" / "build.ninja");
    ninja << "rule cc\n";
    ninja.close();

    auto result = BuildAdapterFactory::detect_build_system_type((temp_dir / "build").string());

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), BuildSystemType::NINJA);
}