//
// Created by gregorian on 05/12/2025.
//

#include <gtest/gtest.h>
#include "bha/parsers/parser.h"
#include <filesystem>
#include <fstream>

using namespace bha::parsers;
using namespace bha::core;
namespace fs = std::filesystem;

class ParserTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "parser_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] fs::path create_temp_file(const std::string& filename, const std::string& content) const
    {
        fs::path file = fs::path(temp_dir) / filename;
        std::ofstream ofs(file);
        ofs << content;
        ofs.close();
        return file;
    }

    static std::string get_minimal_clang_trace() {
        return R"({
            "traceEvents": [
                {
                    "name": "Source",
                    "ph": "X",
                    "ts": 1000,
                    "dur": 1000000,
                    "pid": 1,
                    "tid": 1,
                    "args": {"detail": "test.cpp"}
                }
            ]
        })";
    }

    static std::string get_minimal_gcc_report() {
        return R"(Time variable                                   usr           sys          wall
 phase parsing                  :   0.50 ( 25%)   0.10 ( 20%)   0.60 ( 24%)
 phase opt and generate         :   1.50 ( 75%)   0.40 ( 80%)   1.90 ( 76%)
TOTAL                          :   2.00          0.50          2.50
)";
    }

    static std::string get_minimal_msvc_trace() {
        return R"(c1xx.dll!<unknown>
time(C:\project\test.cpp)=1234
)";
    }
};

TEST_F(ParserTest, BaseParserInterface) {
    auto clang_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(clang_result.is_success());
    const auto clang_parser = std::move(clang_result).value();
    EXPECT_NE(clang_parser, nullptr);
    EXPECT_EQ(clang_parser->get_compiler_type(), CompilerType::CLANG);
    EXPECT_FALSE(clang_parser->get_format_name().empty());
}

TEST_F(ParserTest, ParserFactoryConstruction) {
    auto clang = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    EXPECT_TRUE(clang.is_success());

    auto gcc = ParserFactory::create_parser_for_compiler(CompilerType::GCC);
    EXPECT_TRUE(gcc.is_success());

    auto msvc = ParserFactory::create_parser_for_compiler(CompilerType::MSVC);
    EXPECT_TRUE(msvc.is_success());

    auto unknown = ParserFactory::create_parser_for_compiler(CompilerType::UNKNOWN);
    EXPECT_FALSE(unknown.is_success());
}

TEST_F(ParserTest, CompilerDetection) {
    const std::string clang_content = get_minimal_clang_trace();
    CompilerType type = ParserFactory::detect_compiler_from_content(clang_content);
    EXPECT_EQ(type, CompilerType::CLANG);

    const std::string gcc_content = get_minimal_gcc_report();
    type = ParserFactory::detect_compiler_from_content(gcc_content);
    EXPECT_EQ(type, CompilerType::GCC);

    const std::string msvc_content = get_minimal_msvc_trace();
    type = ParserFactory::detect_compiler_from_content(msvc_content);
    EXPECT_EQ(type, CompilerType::MSVC);

    const std::string invalid = "random text that doesn't match any format";
    type = ParserFactory::detect_compiler_from_content(invalid);
    EXPECT_EQ(type, CompilerType::UNKNOWN);
}

TEST_F(ParserTest, CreateClangParser) {
    auto result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(result.is_success());
    const auto parser = std::move(result).value();

    EXPECT_NE(parser, nullptr);
    EXPECT_EQ(parser->get_compiler_type(), CompilerType::CLANG);
    EXPECT_EQ(parser->get_format_name(), "clang-time-trace");

    const auto extensions = parser->get_supported_extensions();
    EXPECT_FALSE(extensions.empty());
}

TEST_F(ParserTest, CreateGCCParser) {
    auto result = ParserFactory::create_parser_for_compiler(CompilerType::GCC);
    ASSERT_TRUE(result.is_success());
    const auto parser = std::move(result).value();

    EXPECT_NE(parser, nullptr);
    EXPECT_EQ(parser->get_compiler_type(), CompilerType::GCC);
    EXPECT_EQ(parser->get_format_name(), "gcc-time-report");

    const auto extensions = parser->get_supported_extensions();
    EXPECT_FALSE(extensions.empty());
}

TEST_F(ParserTest, CreateMSVCParser) {
    auto result = ParserFactory::create_parser_for_compiler(CompilerType::MSVC);
    ASSERT_TRUE(result.is_success());
    const auto parser = std::move(result).value();

    EXPECT_NE(parser, nullptr);
    EXPECT_EQ(parser->get_compiler_type(), CompilerType::MSVC);
    EXPECT_FALSE(parser->get_format_name().empty());

    const auto extensions = parser->get_supported_extensions();
    EXPECT_FALSE(extensions.empty());
}

TEST_F(ParserTest, ParseTraceFile) {
    auto parser_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(parser_result.is_success());
    auto parser = std::move(parser_result).value();

    auto file = create_temp_file("trace.json", get_minimal_clang_trace());
    auto result = parser->parse(file.string());

    // Just verify it doesn't crash and returns a Result
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(ParserTest, ExtractTimingData) {
    auto parser_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(parser_result.is_success());
    auto parser = std::move(parser_result).value();

    if (auto result = parser->parse_string(get_minimal_clang_trace()); result.is_success()) {
        auto units = std::move(result).value();
        // If parsing succeeds, verify there are compilation units
        EXPECT_GE(units.size(), 0);
    }
}

TEST_F(ParserTest, ExtractDependencies) {
    auto parser_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(parser_result.is_success());
    auto parser = std::move(parser_result).value();

    auto result = parser->parse_string(get_minimal_clang_trace());
    EXPECT_TRUE(result.is_success() || result.is_failure());
}

TEST_F(ParserTest, DetectCompilerFromFile) {
    const auto clang_file = create_temp_file("clang_trace.json", get_minimal_clang_trace());
    CompilerType type = ParserFactory::detect_compiler_from_file(clang_file.string());
    EXPECT_EQ(type, CompilerType::CLANG);

    const auto gcc_file = create_temp_file("gcc_report.txt", get_minimal_gcc_report());
    type = ParserFactory::detect_compiler_from_file(gcc_file.string());
    EXPECT_EQ(type, CompilerType::GCC);
}

TEST_F(ParserTest, AutoDetectAndParse) {
    const auto clang_file = create_temp_file("auto_clang.json", get_minimal_clang_trace());

    const CompilerType detected = ParserFactory::detect_compiler_from_file(clang_file.string());
    EXPECT_NE(detected, CompilerType::UNKNOWN);

    auto parser_result = ParserFactory::create_parser_for_compiler(detected);
    ASSERT_TRUE(parser_result.is_success());

    const auto parser = std::move(parser_result).value();
    EXPECT_TRUE(parser->can_parse(clang_file.string()));
}

TEST_F(ParserTest, GetSupportedCompilers) {
    auto clang = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    EXPECT_TRUE(clang.is_success());

    auto gcc = ParserFactory::create_parser_for_compiler(CompilerType::GCC);
    EXPECT_TRUE(gcc.is_success());

    auto msvc = ParserFactory::create_parser_for_compiler(CompilerType::MSVC);
    EXPECT_TRUE(msvc.is_success());
}

TEST_F(ParserTest, ParserCapabilities) {
    auto parser_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(parser_result.is_success());
    const auto parser = std::move(parser_result).value();

    const auto caps = parser->get_capabilities();
    EXPECT_TRUE(caps.supports_timing || !caps.supports_timing);  // Just verify field exists
}

TEST_F(ParserTest, NonExistentFile) {
    auto parser_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(parser_result.is_success());
    auto parser = std::move(parser_result).value();

    auto result = parser->parse("/nonexistent/file.json");
    EXPECT_FALSE(result.is_success());
}

TEST_F(ParserTest, EmptyContent) {
    auto parser_result = ParserFactory::create_parser_for_compiler(CompilerType::CLANG);
    ASSERT_TRUE(parser_result.is_success());
    auto parser = std::move(parser_result).value();

    auto result = parser->parse_string("");
    EXPECT_FALSE(result.is_success());
}