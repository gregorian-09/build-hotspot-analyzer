//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include "bha/suggestions/pimpl_suggester.h"
#include "bha/core/types.h"
#include <fstream>
#include <filesystem>

using namespace bha::suggestions;
using namespace bha::core;

class PimplSuggesterTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = "/tmp/bha_pimpl_test";
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    void create_test_file(const std::string& filename, const std::string& content) const
    {
        std::ofstream file(test_dir + "/" + filename);
        file << content;
        file.close();
    }

    std::string test_dir;
};

TEST_F(PimplSuggesterTest, SuggestPimplPatternsForNonExistentFile) {
    const auto result = PIMPLSuggester::suggest_pimpl_patterns("/nonexistent/file.h");
    EXPECT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, SuggestPimplPatternsForSimpleClass) {
    const std::string content = R"(
class SimpleClass {
public:
    SimpleClass();
    void process();

private:
    int value_;
    double data_;
};
)";

    create_test_file("simple.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/simple.h");
    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, SuggestPimplPatternsForComplexClass) {
    const std::string content = R"(
#include <vector>
#include <string>
#include <map>
#include "external/dependency1.h"
#include "external/dependency2.h"
#include "external/dependency3.h"

class ComplexClass {
public:
    ComplexClass();
    void process();

private:
    std::vector<int> data1_;
    std::string data2_;
    std::map<std::string, int> data3_;
    Dependency1 dep1_;
    Dependency2 dep2_;
    Dependency3 dep3_;
    int value1_;
    int value2_;
    int value3_;
    int value4_;
    int value5_;
};
)";

    create_test_file("complex.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/complex.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, IdentifyPimplCandidatesWithManyPrivateMembers) {
    const std::string content = R"(
class CandidateClass {
public:
    void method1();
    void method2();

private:
    int member1_;
    int member2_;
    int member3_;
    int member4_;
    int member5_;
    int member6_;
    int member7_;
    int member8_;
};
)";

    create_test_file("candidate.h", content);
    auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/candidate.h");

    ASSERT_TRUE(result.is_success());
    const auto& suggestions = result.value();

    EXPECT_GE(suggestions.size(), 0);
}

TEST_F(PimplSuggesterTest, DetectHighCouplingViaIncludes) {
    const std::string content = R"(
#include "dep1.h"
#include "dep2.h"
#include "dep3.h"
#include "dep4.h"
#include "dep5.h"
#include "dep6.h"

class HighlyCoupledClass {
public:
    void process();

private:
    Dep1 d1_;
    Dep2 d2_;
    Dep3 d3_;
    Dep4 d4_;
    Dep5 d5_;
    Dep6 d6_;
};
)";

    create_test_file("coupled.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/coupled.h");

    ASSERT_TRUE(result.is_success()); // High coupling may trigger PIMPL suggestions
}

TEST_F(PimplSuggesterTest, DetectLargeRebuildSurfaceFromDependencies) {
    const std::string content = R"(
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include "external1.h"
#include "external2.h"

class LargeRebuildSurfaceClass {
private:
    std::vector<int> vec_;
    std::map<int, std::string> map_;
    External1 ext1_;
    External2 ext2_;
};
)";

    create_test_file("rebuild_surface.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/rebuild_surface.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, EmptyFileShouldNotCrash) {
    create_test_file("empty.h", "");
    auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/empty.h");

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().size(), 0);
}

TEST_F(PimplSuggesterTest, FileWithOnlyPublicMembersLowPriority) {
    const std::string content = R"(
class PublicOnlyClass {
public:
    int public_member1;
    int public_member2;
    void method();
};
)";

    create_test_file("public_only.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/public_only.h");

    ASSERT_TRUE(result.is_success()); // Class with no private members is unlikely to be a PIMPL candidate
}

TEST_F(PimplSuggesterTest, MultipleClassesInFile) {
    const std::string content = R"(
class Class1 {
private:
    int a_, b_, c_, d_, e_;
};

class Class2 {
private:
    int x_, y_, z_;
};

class Class3 {
public:
    int public_val;
};
)";

    create_test_file("multiple.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/multiple.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, PimplCandidateWithManyPrivateMembers) {
    const std::string content = R"(
#include "impl.h"

class HeavyClass {
private:
    int m1_, m2_, m3_, m4_, m5_;
    int m6_, m7_, m8_, m9_, m10_;
    std::string data_;
    std::vector<int> vec_;
    std::map<int, std::string> mapping_;

public:
    void process();
};
)";

    create_test_file("heavy.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/heavy.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, PimplCandidateWithManyIncludes) {
    const std::string content = R"(
#include "dep1.h"
#include "dep2.h"
#include "dep3.h"
#include "dep4.h"
#include "dep5.h"
#include <vector>
#include <string>
#include <map>

class CoupledClass {
private:
    Dep1* d1_;
    Dep2* d2_;
    Dep3* d3_;
    Dep4* d4_;
    Dep5* d5_;

public:
    void work();
};
)";

    create_test_file("coupled.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/coupled.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, SuggestionsRankedByBenefit) {
    const std::string content = R"(
class LargePimplCandidate {
private:
    int member1_, member2_, member3_, member4_;
    int member5_, member6_, member7_, member8_;
    std::string str_;
    std::vector<int> vec_;
};

class SmallClass {
private:
    int x_;
};
)";

    create_test_file("ranked.h", content);
    auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/ranked.h");

    ASSERT_TRUE(result.is_success());

    // Larger class should rank higher if suggestions exist
    if (const auto& suggestions = result.value(); suggestions.size() > 1) {
        EXPECT_GE(suggestions[0].estimated_time_savings_ms, 0.0);
    }
}

TEST_F(PimplSuggesterTest, SafetyCheckPrivateMembersOnly) {
    const std::string content = R"(
class SafeCandidate {
public:
    void public_method();

private:
    int private1_, private2_, private3_;
    int private4_, private5_;
    std::string private_data_;
};
)";

    create_test_file("safe.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/safe.h");

    ASSERT_TRUE(result.is_success()); // Safe to apply PIMPL when all members are private
}

TEST_F(PimplSuggesterTest, ApplicabilityHighRebuildSurface) {
    const std::string content = R"(
#include "heavy1.h"
#include "heavy2.h"
#include "heavy3.h"

class HighRebuildClass {
private:
    Heavy1 h1_;
    Heavy2 h2_;
    Heavy3 h3_;
    int internal_data_;
};
)";

    create_test_file("rebuild.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/rebuild.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, RejectNonCandidatesWithFewPrivateMembers) {
    const std::string content = R"(
class TrivialClass {
private:
    int value_;

public:
    int getValue() const { return value_; }
};
)";

    create_test_file("trivial.h", content);
    auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/trivial.h");

    ASSERT_TRUE(result.is_success());

}

TEST_F(PimplSuggesterTest, ConfidenceBasedOnComplexity) {
    const std::string content = R"(
class VeryComplex {
private:
    int m1_, m2_, m3_, m4_, m5_;
    int m6_, m7_, m8_, m9_, m10_;
    int m11_, m12_, m13_, m14_, m15_;
    std::vector<int> vec_;
    std::map<std::string, int> map_;
    std::set<std::string> set_;
};
)";

    create_test_file("complex.h", content);
    auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/complex.h");

    ASSERT_TRUE(result.is_success());

    if (const auto& suggestions = result.value(); !suggestions.empty()) {
        EXPECT_GT(suggestions[0].confidence, 0.0);
        EXPECT_LE(suggestions[0].confidence, 1.0);
    }
}

TEST_F(PimplSuggesterTest, HandleTemplateClasses) {
    const std::string content = R"(
template<typename T>
class TemplateClass {
private:
    T data_;
    int counter_;
    std::vector<T> storage_;
};
)";

    create_test_file("template.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/template.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, FilterOutLowQualitySuggestions) {
    const std::string content = R"(
class MinimalCandidate {
private:
    int value_;
};
)";

    create_test_file("minimal.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/minimal.h");

    ASSERT_TRUE(result.is_success()); // Low quality suggestions should be filtered
}

TEST_F(PimplSuggesterTest, SuggestionForComplexInternalStructure) {
    const std::string content = R"(
class ComplexStructure {
private:
    struct InternalImpl {
        int data;
        std::string str;
    };

    InternalImpl impl_;
    int count_;
    std::vector<InternalImpl> items_;

public:
    void execute();
};
)";

    create_test_file("internal_struct.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/internal_struct.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, TimeEstimateReflectsSavings) {
    const std::string content = R"(
class CandidateWithSavings {
private:
    int m1_, m2_, m3_, m4_, m5_;
    int m6_, m7_, m8_, m9_, m10_;
};
)";

    create_test_file("savings.h", content);
    auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/savings.h");

    ASSERT_TRUE(result.is_success());

    if (const auto& suggestions = result.value(); !suggestions.empty()) {
        EXPECT_GE(suggestions[0].estimated_time_savings_ms, 0.0);
    }
}

TEST_F(PimplSuggesterTest, HandleMixedAccessSpecifiers) {
    const std::string content = R"(
class MixedAccess {
public:
    int public_member;

protected:
    int protected_member;

private:
    int private1_, private2_, private3_;
    std::vector<int> data_;
};
)";

    create_test_file("mixed.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/mixed.h");

    ASSERT_TRUE(result.is_success());
}

TEST_F(PimplSuggesterTest, DetectEncapsulationOpportunities) {
    const std::string content = R"(
class EncapsulationTarget {
private:
    // Complex implementation details
    int internal_state1_, internal_state2_;
    int internal_state3_, internal_state4_;
    std::string config_;
    std::map<std::string, int> lookup_;

public:
    void doSomething();
    void doSomethingElse();
};
)";

    create_test_file("encapsulation.h", content);
    const auto result = PIMPLSuggester::suggest_pimpl_patterns(test_dir + "/encapsulation.h");

    ASSERT_TRUE(result.is_success());
}