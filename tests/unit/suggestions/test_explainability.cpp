#include <gtest/gtest.h>

#include "bha/suggestions/suggester.hpp"

#include <chrono>
#include <fstream>

namespace bha::suggestions::test {

namespace fs = std::filesystem;

namespace {

class StubTemplateSuggester final : public ISuggester {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "StubTemplateExplainabilitySuggester";
    }

    [[nodiscard]] std::string_view description() const noexcept override {
        return "Generates a deterministic template suggestion for explainability tests.";
    }

    [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
        return SuggestionType::ExplicitTemplate;
    }

    [[nodiscard]] Result<SuggestionResult, Error> suggest(
        const SuggestionContext&
    ) const override {
        Suggestion suggestion;
        suggestion.id = "stub-template-origin";
        suggestion.type = SuggestionType::ExplicitTemplate;
        suggestion.priority = Priority::Medium;
        suggestion.confidence = 0.95;
        suggestion.is_safe = true;
        suggestion.title = "Explicitly instantiate std::vector<int>";
        suggestion.description = "std::vector<int> instantiates repeatedly in this target.";

        SuggestionResult result;
        result.suggestions.push_back(std::move(suggestion));
        return Result<SuggestionResult, Error>::success(std::move(result));
    }
};

class StubForwardDeclSuggester final : public ISuggester {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "StubForwardDeclExplainabilitySuggester";
    }

    [[nodiscard]] std::string_view description() const noexcept override {
        return "Generates a deterministic include-driven suggestion for explainability tests.";
    }

    [[nodiscard]] SuggestionType suggestion_type() const noexcept override {
        return SuggestionType::ForwardDeclaration;
    }

    [[nodiscard]] Result<SuggestionResult, Error> suggest(
        const SuggestionContext& context
    ) const override {
        Suggestion suggestion;
        suggestion.id = "stub-include-chain";
        suggestion.type = SuggestionType::ForwardDeclaration;
        suggestion.priority = Priority::Medium;
        suggestion.confidence = 0.95;
        suggestion.is_safe = true;
        suggestion.title = "Forward declare C";
        suggestion.description = "Class C is only used by pointer in this header.";
        suggestion.target_file.path = context.project_root / "src/c.hpp";
        suggestion.impact.files_benefiting.push_back(context.project_root / "src/a.cpp");

        SuggestionResult result;
        result.suggestions.push_back(std::move(suggestion));
        return Result<SuggestionResult, Error>::success(std::move(result));
    }
};

void ensure_stub_suggesters_registered() {
    static bool registered = false;
    if (registered) {
        return;
    }
    SuggesterRegistry::instance().register_suggester(std::make_unique<StubTemplateSuggester>());
    SuggesterRegistry::instance().register_suggester(std::make_unique<StubForwardDeclSuggester>());
    registered = true;
}

fs::path create_temp_project_root() {
    const auto stamp = std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    const fs::path root = fs::temp_directory_path() / ("bha-explainability-" + stamp);
    fs::create_directories(root / "src");
    return root;
}

}  // namespace

TEST(SuggesterExplainabilityTest, AddsTemplateOriginEvidence) {
    ensure_stub_suggesters_registered();

    BuildTrace trace;
    analyzers::AnalysisResult analysis;

    analyzers::TemplateAnalysisResult::TemplateInfo info;
    info.name = "InstantiateClass";
    info.full_signature = "std::vector<int>";
    info.total_time = std::chrono::milliseconds(42);
    info.instantiation_count = 7;
    info.files_using.push_back("src/tu.cpp");
    SourceLocation location;
    location.file = "src/tu.cpp";
    location.line = 17;
    info.locations.push_back(location);
    analysis.templates.templates.push_back(info);
    analysis.templates.total_template_time = info.total_time;

    SuggesterOptions options;
    options.enable_consolidation = false;
    options.max_suggestions = 4;
    options.enabled_types = {SuggestionType::ExplicitTemplate};

    const auto result = generate_all_suggestions(trace, analysis, options, fs::current_path());
    ASSERT_TRUE(result.is_ok());
    ASSERT_FALSE(result.value().empty());

    const auto& suggestion = result.value().front();
    ASSERT_FALSE(suggestion.hotspot_origins.empty());
    EXPECT_EQ(suggestion.hotspot_origins.front().kind, "template_origin");
    EXPECT_FALSE(suggestion.hotspot_origins.front().chain.empty());
    EXPECT_NE(suggestion.hotspot_origins.front().chain.front().find("template:"), std::string::npos);
}

TEST(SuggesterExplainabilityTest, AddsExactIncludeChainEvidence) {
    ensure_stub_suggesters_registered();

    const fs::path root = create_temp_project_root();
    const fs::path source = root / "src/a.cpp";
    const fs::path header_b = root / "src/b.hpp";
    const fs::path header_c = root / "src/c.hpp";

    {
        std::ofstream out(source);
        out << "#include \"b.hpp\"\nint main() { return 0; }\n";
    }
    {
        std::ofstream out(header_b);
        out << "#pragma once\n#include \"c.hpp\"\n";
    }
    {
        std::ofstream out(header_c);
        out << "#pragma once\nclass C {};\n";
    }

    BuildTrace trace;
    CompilationUnit unit;
    unit.source_file = source;
    unit.command_line = {
        "clang++", "-I", (root / "src").string(), "-c", source.string(), "-o", (root / "build/a.o").string()
    };

    IncludeInfo include_b;
    include_b.header = header_b;
    include_b.parse_time = std::chrono::milliseconds(5);
    unit.includes.push_back(include_b);

    IncludeInfo include_c;
    include_c.header = header_c;
    include_c.parse_time = std::chrono::milliseconds(8);
    unit.includes.push_back(include_c);

    trace.units.push_back(unit);

    analyzers::AnalysisResult analysis;
    analyzers::FileAnalysisResult file;
    file.file = source;
    file.compile_time = std::chrono::milliseconds(30);
    analysis.files.push_back(file);

    SuggesterOptions options;
    options.enable_consolidation = false;
    options.max_suggestions = 4;
    options.enabled_types = {SuggestionType::ForwardDeclaration};

    const auto result = generate_all_suggestions(trace, analysis, options, root);
    ASSERT_TRUE(result.is_ok());
    ASSERT_FALSE(result.value().empty());

    const auto& suggestion = result.value().front();
    auto include_it = std::find_if(
        suggestion.hotspot_origins.begin(),
        suggestion.hotspot_origins.end(),
        [](const HotspotOrigin& origin) { return origin.kind == "include_chain"; }
    );
    ASSERT_NE(include_it, suggestion.hotspot_origins.end());
    ASSERT_GE(include_it->chain.size(), 3u);
    EXPECT_NE(include_it->chain[0].find("a.cpp"), std::string::npos);
    EXPECT_NE(include_it->chain[1].find("b.hpp"), std::string::npos);
    EXPECT_NE(include_it->chain[2].find("c.hpp"), std::string::npos);

    std::error_code ec;
    fs::remove_all(root, ec);
}

}  // namespace bha::suggestions::test
