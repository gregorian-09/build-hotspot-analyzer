#include "bha/suggestions/all_suggesters.hpp"
#include "bha/suggestions/suggester_catalog.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace bha::suggestions {

    namespace {

        class AbiSensitiveStubSuggester final : public ISuggester {
        public:
            AbiSensitiveStubSuggester(std::string id, std::filesystem::path target)
                : id_(std::move(id)), target_(std::move(target)) {}

            [[nodiscard]] std::string_view name() const noexcept override {
                return "AbiSensitiveStubSuggester";
            }

            [[nodiscard]] std::string_view description() const noexcept override {
                return "Emits a deterministic header-edit suggestion for ABI policy tests.";
            }

            [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
                return SuggestionType::ForwardDeclaration;
            }

            [[nodiscard]] SuggesterPolicy policy() const noexcept override {
                return {
                    .language_support = SuggesterLanguageSupport::CAndCXX,
                    .abi_sensitivity = SuggesterAbiSensitivity::HeaderSurface
                };
            }

            [[nodiscard]] Result<SuggestionResult, Error> suggest(
                const SuggestionContext&
            ) const override {
                Suggestion suggestion;
                suggestion.id = id_;
                suggestion.type = SuggestionType::ForwardDeclaration;
                suggestion.priority = Priority::Medium;
                suggestion.confidence = 0.9;
                suggestion.is_safe = true;
                suggestion.target_file.path = target_;
                suggestion.application_mode = SuggestionApplicationMode::DirectEdits;
                suggestion.edits.push_back(TextEdit{
                    .file = target_,
                    .start_line = 0,
                    .start_col = 0,
                    .end_line = 0,
                    .end_col = 0,
                    .new_text = "// patched\n"
                });

                SuggestionResult result;
                result.suggestions.push_back(std::move(suggestion));
                return Result<SuggestionResult, Error>::success(std::move(result));
            }

        private:
            std::string id_;
            std::filesystem::path target_;
        };

        void write_file(const std::filesystem::path& path, const std::string& content) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path);
            ASSERT_TRUE(out.good());
            out << content;
        }

    }  // namespace

    TEST(SuggesterCatalogTest, ListsRegisteredSuggesters) {
        register_all_suggesters();
        const auto descriptors = list_suggester_descriptors();
        ASSERT_FALSE(descriptors.empty());

        const auto has_pch = std::ranges::any_of(descriptors, [](const SuggesterDescriptor& descriptor) {
            return descriptor.id == "pch";
        });
        const auto has_include = std::ranges::any_of(descriptors, [](const SuggesterDescriptor& descriptor) {
            return descriptor.id == "include";
        });

        EXPECT_TRUE(has_pch);
        EXPECT_TRUE(has_include);
    }

    TEST(SuggesterCatalogTest, FindsByAliasAndSuggestionType) {
        register_all_suggesters();

        const auto by_class = find_suggester_descriptor("PCHSuggester");
        ASSERT_TRUE(by_class.has_value());
        EXPECT_EQ(by_class->id, "pch");

        const auto by_type = find_suggester_descriptor("move-to-cpp");
        ASSERT_TRUE(by_type.has_value());
        EXPECT_EQ(by_type->id, "include");
    }

    TEST(SuggesterCatalogTest, ParsesSuggestionTypeTokens) {
        EXPECT_EQ(parse_suggestion_type_id("explicit-template"), SuggestionType::ExplicitTemplate);
        EXPECT_EQ(parse_suggestion_type_id("unity"), SuggestionType::UnityBuild);
        EXPECT_FALSE(parse_suggestion_type_id("not-a-type").has_value());
    }

    TEST(SuggesterCatalogTest, ExposesLanguageAndAbiPolicyMetadata) {
        register_all_suggesters();

        const auto template_descriptor = find_suggester_descriptor("template-instantiation");
        ASSERT_TRUE(template_descriptor.has_value());
        EXPECT_EQ(template_descriptor->language_support, SuggesterLanguageSupport::CXXOnly);
        EXPECT_EQ(template_descriptor->abi_sensitivity, SuggesterAbiSensitivity::HeaderSurface);

        const auto pch_descriptor = find_suggester_descriptor("pch");
        ASSERT_TRUE(pch_descriptor.has_value());
        EXPECT_EQ(pch_descriptor->language_support, SuggesterLanguageSupport::BuildSystemLevel);
        EXPECT_EQ(pch_descriptor->abi_sensitivity, SuggesterAbiSensitivity::BuildConfiguration);
    }

    TEST(SuggesterCatalogTest, DetectsSourceLanguageFromCompileCommands) {
        CompilationUnit c_unit;
        c_unit.source_file = "src/example.c";
        c_unit.command_line = {"clang", "-x", "c", "-c", "src/example.c"};
        EXPECT_EQ(detect_source_language_mode(c_unit), SourceLanguageMode::C);

        CompilationUnit cxx_unit;
        cxx_unit.source_file = "src/example.cpp";
        cxx_unit.command_line = {"clang++", "-c", "src/example.cpp"};
        EXPECT_EQ(detect_source_language_mode(cxx_unit), SourceLanguageMode::CXX);

        CompilationUnit msvc_cxx_unit;
        msvc_cxx_unit.source_file = "src/example.c";
        msvc_cxx_unit.command_line = {"cl.exe", "/TP", "/c", "src/example.c"};
        EXPECT_EQ(detect_source_language_mode(msvc_cxx_unit), SourceLanguageMode::CXX);
    }

    TEST(SuggesterCatalogTest, AppliesLanguageSupportRulesToProjectProfiles) {
        ProjectLanguageProfile c_only_profile;
        c_only_profile.c_units = 4;

        ProjectLanguageProfile mixed_profile;
        mixed_profile.c_units = 2;
        mixed_profile.cxx_units = 3;

        EXPECT_FALSE(language_support_matches(SuggesterLanguageSupport::CXXOnly, c_only_profile));
        EXPECT_TRUE(language_support_matches(SuggesterLanguageSupport::CAndCXX, c_only_profile));
        EXPECT_TRUE(language_support_matches(SuggesterLanguageSupport::BuildSystemLevel, c_only_profile));
        EXPECT_TRUE(language_support_matches(SuggesterLanguageSupport::CXXOnly, mixed_profile));
    }

    TEST(SuggesterCatalogTest, DowngradesAbiSensitivePublicHeaderSuggestions) {
        const auto unique_suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        const auto root = std::filesystem::temp_directory_path() / ("bha-abi-policy-" + unique_suffix);
        const auto public_header = root / "include" / "widget.h";
        write_file(public_header, "#pragma once\nstruct widget;\n");

        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = root / "src" / "widget.c";
        unit.command_line = {"clang", "-x", "c", "-c", unit.source_file.string()};
        trace.units.push_back(unit);

        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.enable_consolidation = false;

        SuggesterRegistry::instance().register_suggester(
            std::make_unique<AbiSensitiveStubSuggester>("abi-public", public_header)
        );

        const auto result = generate_all_suggestions(trace, analysis, options, root);
        ASSERT_TRUE(result.is_ok());
        const auto it = std::ranges::find_if(result.value(), [](const Suggestion& suggestion) {
            return suggestion.id == "abi-public";
        });
        ASSERT_NE(it, result.value().end());
        const auto& suggestion = *it;
        EXPECT_EQ(resolve_application_mode(suggestion), SuggestionApplicationMode::Advisory);
        EXPECT_TRUE(suggestion.edits.empty());
        ASSERT_TRUE(suggestion.auto_apply_blocked_reason.has_value());
        EXPECT_NE(suggestion.auto_apply_blocked_reason->find("public or extern \"C\" header surface"), std::string::npos);

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    TEST(SuggesterCatalogTest, DowngradesAbiSensitiveExternCHeaderSuggestions) {
        const auto unique_suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        const auto root = std::filesystem::temp_directory_path() / ("bha-externc-policy-" + unique_suffix);
        const auto header = root / "src" / "ffi.hpp";
        write_file(
            header,
            "#pragma once\n"
            "#ifdef __cplusplus\n"
            "extern \"C\" {\n"
            "#endif\n"
            "int ffi_call(void);\n"
            "#ifdef __cplusplus\n"
            "}\n"
            "#endif\n"
        );

        BuildTrace trace;
        CompilationUnit unit;
        unit.source_file = root / "src" / "ffi.cpp";
        unit.command_line = {"clang++", "-c", unit.source_file.string()};
        trace.units.push_back(unit);

        analyzers::AnalysisResult analysis;
        SuggesterOptions options;
        options.enable_consolidation = false;

        SuggesterRegistry::instance().register_suggester(
            std::make_unique<AbiSensitiveStubSuggester>("abi-extern-c", header)
        );

        const auto result = generate_all_suggestions(trace, analysis, options, root);
        ASSERT_TRUE(result.is_ok());
        const auto it = std::ranges::find_if(result.value(), [](const Suggestion& suggestion) {
            return suggestion.id == "abi-extern-c";
        });
        ASSERT_NE(it, result.value().end());
        const auto& suggestion = *it;
        EXPECT_EQ(resolve_application_mode(suggestion), SuggestionApplicationMode::Advisory);
        EXPECT_TRUE(suggestion.edits.empty());

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

}  // namespace bha::suggestions
