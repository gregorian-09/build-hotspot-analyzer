//
// Created by gregorian on 08/11/2025.
//

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "bha/parsers/parser.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;
using namespace bha::core;
using namespace bha::parsers;

class ParserFactoryIntegrationTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "bha_parser_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] fs::path create_temp_file(const std::string& filename, const std::string& content) const
    {
        fs::path file_path = temp_dir / filename;
        std::ofstream ofs(file_path);
        ofs << content;
        ofs.close();
        return file_path;
    }
};

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_DetectsClangFromContent) {
    const auto file = create_temp_file("trace.json", R"({"traceEvents":[]})");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(file.string()),
              CompilerType::CLANG);
}

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_DetectsGCCFromContent) {
    const auto file = create_temp_file("timereport.txt", "Time variable used:");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(file.string()),
              CompilerType::GCC);
}

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_DetectsMSVCFromContent) {
    const auto file = create_temp_file("trace.log", "c1xx.dll loaded");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(file.string()),
              CompilerType::MSVC);
}

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_FallsBackToExtensionForEmpty) {
    const auto json_file = create_temp_file("empty.json", "");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(json_file.string()),
              CompilerType::CLANG);

    const auto txt_file = create_temp_file("empty.txt", "");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(txt_file.string()),
              CompilerType::GCC);

    const auto log_file = create_temp_file("empty.log", "");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(log_file.string()),
              CompilerType::GCC);
}

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_ReturnsUnknownForNonexistentFile) {
    EXPECT_EQ(ParserFactory::detect_compiler_from_file("nonexistent.xyz"),
              CompilerType::UNKNOWN);
}

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_ReturnsUnknownForUnsupportedExtension) {
    const auto file = create_temp_file("unknown.xyz", "");
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(file.string()),
              CompilerType::UNKNOWN);
}


TEST_F(ParserFactoryIntegrationTest, CreateParser_CreatesParserFromClangFile) {
    const auto file = create_temp_file("trace.json", R"({"traceEvents":[]})");
    auto result = ParserFactory::create_parser(file.string());

    ASSERT_TRUE(result.is_success());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_CreatesParserFromGCCFile) {
    const auto file = create_temp_file("timereport.txt", "Time variable used:");
    auto result = ParserFactory::create_parser(file.string());

    ASSERT_TRUE(result.is_success());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_CreatesParserFromMSVCFile) {
    const auto file = create_temp_file("trace.log", "Microsoft (R) C/C++ Compiler");
    auto result = ParserFactory::create_parser(file.string());

    ASSERT_TRUE(result.is_success());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_FailsForUndetectableFile) {
    const auto file = create_temp_file("unknown.xyz", "random content");
    auto result = ParserFactory::create_parser(file.string());

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, ErrorCode::UNSUPPORTED_FORMAT);
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_FailsForNonexistentFile) {
    auto result = ParserFactory::create_parser("nonexistent.file");

    ASSERT_TRUE(result.is_failure());
    EXPECT_EQ(result.error().code, ErrorCode::UNSUPPORTED_FORMAT);
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_HandlesMultipleClangPatterns) {
    auto file1 = create_temp_file("trace1.json", R"({"name":"Parse"})");
    auto result1 = ParserFactory::create_parser(file1.string());
    ASSERT_TRUE(result1.is_success());

    auto file2 = create_temp_file("trace2.txt", "ftime-trace results");
    auto result2 = ParserFactory::create_parser(file2.string());
    ASSERT_TRUE(result2.is_success());
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_HandlesMultipleGCCPatterns) {
    auto file1 = create_temp_file("gcc1.txt", "phase parsing : 0.50");
    auto result1 = ParserFactory::create_parser(file1.string());
    ASSERT_TRUE(result1.is_success());

    auto file2 = create_temp_file("gcc2.log", "TOTAL : 2.34");
    auto result2 = ParserFactory::create_parser(file2.string());
    ASSERT_TRUE(result2.is_success());
}

TEST_F(ParserFactoryIntegrationTest, CreateParser_HandlesMultipleMSVCPatterns) {
    auto file1 = create_temp_file("msvc1.log", "time(source.cpp)");
    auto result1 = ParserFactory::create_parser(file1.string());
    ASSERT_TRUE(result1.is_success());

    auto file2 = create_temp_file("msvc2.txt", "c1xx.dll initialization");
    auto result2 = ParserFactory::create_parser(file2.string());
    ASSERT_TRUE(result2.is_success());
}

TEST_F(ParserFactoryIntegrationTest, DetectCompilerFromFile_HandlesLargeFiles) {
    // Create a large file with compiler signature
    std::string large_content(10000, 'x');
    large_content += "Time variable used:"; // GCC signature

    const auto file = create_temp_file("large.txt", large_content);
    EXPECT_EQ(ParserFactory::detect_compiler_from_file(file.string()),
              CompilerType::GCC);
}

TEST(ParserFactoryCompilerVersionTest, DetectCompilerVersion_DetectsGCC) {
    std::string version;

    if (auto result = ParserFactory::detect_compiler_version("gcc", version); result.is_success()) {
        EXPECT_EQ(result.value(), CompilerType::GCC);
        EXPECT_FALSE(version.empty());
        EXPECT_THAT(version, ::testing::AnyOf(
            ::testing::HasSubstr("gcc"),
            ::testing::HasSubstr("GCC")
        ));
    }
}

TEST(ParserFactoryCompilerVersionTest, DetectCompilerVersion_DetectsGPlusPlus) {
    std::string version;

    if (auto result = ParserFactory::detect_compiler_version("g++", version); result.is_success()) {
        EXPECT_EQ(result.value(), CompilerType::GCC);
        EXPECT_FALSE(version.empty());
        EXPECT_THAT(version, ::testing::AnyOf(
            ::testing::HasSubstr("g++"),
            ::testing::HasSubstr("GCC")
        ));
    }
}

TEST(ParserFactoryCompilerVersionTest, DetectCompilerVersion_DetectsClang) {
    std::string version;

    if (auto result = ParserFactory::detect_compiler_version("clang", version); result.is_success()) {
        EXPECT_EQ(result.value(), CompilerType::CLANG);
        EXPECT_FALSE(version.empty());
        EXPECT_THAT(version, ::testing::HasSubstr("clang"));
    }
}

TEST(ParserFactoryCompilerVersionTest, DetectCompilerVersion_DetectsClangPlusPlus) {
    std::string version;

    if (auto result = ParserFactory::detect_compiler_version("clang++", version); result.is_success()) {
        EXPECT_EQ(result.value(), CompilerType::CLANG);
        EXPECT_FALSE(version.empty());
        EXPECT_THAT(version, ::testing::HasSubstr("clang"));
    }
}
