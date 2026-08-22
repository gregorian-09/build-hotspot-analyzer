#include "bha/refactor/pimpl_tooling.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace bha::refactor {
    namespace {
        namespace fs = std::filesystem;

        class PimplToolingTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                root_ = fs::temp_directory_path() / ("bha-pimpl-tooling-" + std::to_string(stamp));
                std::error_code error;
                fs::create_directories(root_ / "include", error);
                fs::create_directories(root_ / "src", error);
                ASSERT_FALSE(error);
            }

            void TearDown() override {
                std::error_code error;
                fs::remove_all(root_, error);
            }

            void write_project(const bool explicitly_deleted_copy_operations) {
                std::ofstream(root_ / "include" / "widget.hpp")
                    << "#pragma once\n"
                    << "namespace demo {\n"
                    << "class Widget {\n"
                    << "private:\n"
                    << "    int value_;\n"
                    << "public:\n"
                    << "    Widget();\n"
                    << "    ~Widget();\n"
                    << (explicitly_deleted_copy_operations
                        ? "    Widget(const Widget&) = delete;\n"
                          "    Widget& operator=(const Widget&) = delete;\n"
                        : "")
                    << "    int value_copy() const;\n"
                    << "};\n"
                    << "}\n";

                std::ofstream(root_ / "src" / "widget.cpp")
                    << "#include \"widget.hpp\"\n"
                    << "demo::Widget::Widget() {}\n"
                    << "demo::Widget::~Widget() = default;\n"
                    << "int demo::Widget::value_copy() const { return value_; }\n";

                nlohmann::json entry;
                entry["directory"] = root_.string();
                entry["file"] = (root_ / "src" / "widget.cpp").string();
                entry["arguments"] = {
                    "clang++",
                    "-std=c++20",
                    "-I" + (root_ / "include").string(),
                    "-c",
                    (root_ / "src" / "widget.cpp").string()
                };
                nlohmann::json database = nlohmann::json::array();
                database.push_back(std::move(entry));
                std::ofstream(root_ / "compile_commands.json") << database.dump();
            }

            static std::string read(const fs::path& path) {
                std::ifstream input(path, std::ios::binary);
                return std::string(
                    std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()
                );
            }

            static std::string apply(std::string content, std::vector<Replacement> replacements) {
                std::ranges::sort(replacements, [](const Replacement& left, const Replacement& right) {
                    return left.offset > right.offset;
                });
                for (const auto& replacement : replacements) {
                    content.replace(replacement.offset, replacement.length, replacement.replacement_text);
                }
                return content;
            }

            fs::path root_;
        };
    }

    TEST_F(PimplToolingTest, EmitsAndValidatesStructuralAstReplacementSet) {
        write_project(true);
        const PimplRequest request{
            .compile_commands_path = root_ / "compile_commands.json",
            .source_file = root_ / "src" / "widget.cpp",
            .header_file = root_ / "include" / "widget.hpp",
            .class_name = "demo::Widget"
        };

        const auto result = run_pimpl_refactor_with_clang_tooling(request);

#if BHA_HAVE_CLANG_TOOLING
        ASSERT_TRUE(result.success);
        EXPECT_TRUE(result.validated_structure);
        EXPECT_FALSE(result.allow_fallback);
        ASSERT_EQ(result.files.size(), 2u);
        EXPECT_EQ(result.summary.moved_private_fields, 1u);
        EXPECT_EQ(result.summary.rewritten_methods, 1u);

        std::vector<Replacement> header_replacements;
        std::vector<Replacement> source_replacements;
        for (const auto& replacement : result.replacements) {
            if (replacement.file.filename() == "widget.hpp") {
                header_replacements.push_back(replacement);
            } else {
                source_replacements.push_back(replacement);
            }
        }
        const auto transformed_header = apply(read(request.header_file), header_replacements);
        const auto transformed_source = apply(read(request.source_file), source_replacements);
        EXPECT_NE(transformed_header.find("#include <memory>"), std::string::npos);
        EXPECT_NE(transformed_header.find("struct Impl;"), std::string::npos);
        EXPECT_NE(transformed_header.find("std::unique_ptr<Impl> pimpl_"), std::string::npos);
        EXPECT_EQ(transformed_header.find("int value_;"), std::string::npos);
        EXPECT_NE(transformed_source.find("struct demo::Widget::Impl"), std::string::npos);
        EXPECT_NE(transformed_source.find("pimpl_->value_"), std::string::npos);
        EXPECT_NE(transformed_source.find("pimpl_(std::unique_ptr<Impl>(new Impl()))"), std::string::npos);
#else
        EXPECT_FALSE(result.success);
#endif
    }

    TEST_F(PimplToolingTest, RejectsImplicitCopySemantics) {
        write_project(false);
        const PimplRequest request{
            .compile_commands_path = root_ / "compile_commands.json",
            .source_file = root_ / "src" / "widget.cpp",
            .header_file = root_ / "include" / "widget.hpp",
            .class_name = "demo::Widget"
        };

        const auto result = run_pimpl_refactor_with_clang_tooling(request);

#if BHA_HAVE_CLANG_TOOLING
        EXPECT_FALSE(result.success);
        ASSERT_FALSE(result.diagnostics.empty());
        EXPECT_NE(result.diagnostics.front().message.find("explicitly delete"), std::string::npos);
#else
        EXPECT_FALSE(result.success);
#endif
    }
}  // namespace bha::refactor
