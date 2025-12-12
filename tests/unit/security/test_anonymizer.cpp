//
// Created by gregorian on 12/12/2025.
//

#include <gtest/gtest.h>
#include "bha/security/anonymizer.h"

using namespace bha::security;
using namespace bha::core;

TEST(AnonymizerTest, DefaultConfig) {
    const Anonymizer::AnonymizationConfig config;
    EXPECT_TRUE(config.anonymize_paths);
    EXPECT_TRUE(config.anonymize_commit_info);
    EXPECT_TRUE(config.preserve_extensions);
    EXPECT_TRUE(config.preserve_directory_structure);
    EXPECT_EQ(config.replacement_root, "/project");
}

TEST(AnonymizerTest, AnonymizePath_Simple) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string original = "/home/user/project/src/main.cpp";
    const std::string anonymized = anonymizer.anonymize_path(original);

    EXPECT_NE(anonymized, original);
    EXPECT_FALSE(anonymized.empty());
}

TEST(AnonymizerTest, AnonymizePath_Consistency) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string path = "/home/user/project/file.cpp";
    const std::string anon1 = anonymizer.anonymize_path(path);
    const std::string anon2 = anonymizer.anonymize_path(path);

    EXPECT_EQ(anon1, anon2);
}

TEST(AnonymizerTest, AnonymizePath_PreserveExtension) {
    Anonymizer::AnonymizationConfig config;
    config.preserve_extensions = true;
    Anonymizer anonymizer(config);

    const std::string cpp_path = "/home/user/file.cpp";
    const std::string h_path = "/home/user/file.h";

    const std::string anon_cpp = anonymizer.anonymize_path(cpp_path);
    const std::string anon_h = anonymizer.anonymize_path(h_path);

    EXPECT_TRUE(anon_cpp.ends_with(".cpp") || anon_cpp.find(".cpp") != std::string::npos);
    EXPECT_TRUE(anon_h.ends_with(".h") || anon_h.find(".h") != std::string::npos);
}

TEST(AnonymizerTest, AnonymizePath_DifferentPaths) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string path1 = "/home/user/file1.cpp";
    const std::string path2 = "/home/user/file2.cpp";

    const std::string anon1 = anonymizer.anonymize_path(path1);
    const std::string anon2 = anonymizer.anonymize_path(path2);

    EXPECT_NE(anon1, anon2);
}

TEST(AnonymizerTest, AnonymizeCommitSHA_Simple) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string sha = "abc123def456";
    const std::string anonymized = anonymizer.anonymize_commit_sha(sha);

    EXPECT_NE(anonymized, sha);
    EXPECT_FALSE(anonymized.empty());
}

TEST(AnonymizerTest, AnonymizeCommitSHA_Consistency) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string sha = "abc123def456";
    const std::string anon1 = anonymizer.anonymize_commit_sha(sha);
    const std::string anon2 = anonymizer.anonymize_commit_sha(sha);

    EXPECT_EQ(anon1, anon2);
}

TEST(AnonymizerTest, AnonymizeCommitSHA_DifferentSHAs) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string sha1 = "abc123";
    const std::string sha2 = "def456";

    const std::string anon1 = anonymizer.anonymize_commit_sha(sha1);
    const std::string anon2 = anonymizer.anonymize_commit_sha(sha2);

    EXPECT_NE(anon1, anon2);
}

TEST(AnonymizerTest, ClearMapping) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string path = "/home/user/file.cpp";
    const std::string anon1 = anonymizer.anonymize_path(path);

    anonymizer.clear_mapping();

    const std::string anon2 = anonymizer.anonymize_path(path);
    ASSERT_EQ(anon1, anon2);
}

TEST(AnonymizerTest, GetPathMapping) {
    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const std::string path1 = "/home/user/file1.cpp";
    const std::string path2 = "/home/user/file2.cpp";

    anonymizer.anonymize_path(path1);
    anonymizer.anonymize_path(path2);

    const auto& mapping = anonymizer.get_path_mapping();
    EXPECT_GE(mapping.size(), 2);
    EXPECT_TRUE(mapping.contains(path1));
    EXPECT_TRUE(mapping.contains(path2));
}

TEST(AnonymizerTest, AnonymizeTrace_Basic) {
    BuildTrace trace;
    trace.trace_id = "trace_001";
    trace.commit_sha = "abc123def456";
    trace.branch = "main";

    CompilationUnit cu;
    cu.file_path = "/home/user/project/src/main.cpp";
    cu.commit_sha = "abc123def456";
    trace.compilation_units.push_back(cu);

    Anonymizer::AnonymizationConfig config;
    config.anonymize_paths = true;
    config.anonymize_commit_info = true;
    Anonymizer anonymizer(config);

    const BuildTrace anonymized = anonymizer.anonymize_trace(trace);

    EXPECT_NE(anonymized.commit_sha, trace.commit_sha);
    EXPECT_NE(anonymized.compilation_units[0].file_path, trace.compilation_units[0].file_path);
}

TEST(AnonymizerTest, AnonymizeTrace_DisablePathAnonymization) {
    BuildTrace trace;
    trace.commit_sha = "abc123";

    CompilationUnit cu;
    cu.file_path = "/home/user/file.cpp";
    trace.compilation_units.push_back(cu);

    Anonymizer::AnonymizationConfig config;
    config.anonymize_paths = false;
    config.anonymize_commit_info = true;
    Anonymizer anonymizer(config);

    const BuildTrace anonymized = anonymizer.anonymize_trace(trace);

    // Paths should remain unchanged
    EXPECT_EQ(anonymized.compilation_units[0].file_path, trace.compilation_units[0].file_path);
    // Commit should be anonymized
    EXPECT_NE(anonymized.commit_sha, trace.commit_sha);
}

TEST(AnonymizerTest, AnonymizeTrace_DisableCommitAnonymization) {
    BuildTrace trace;
    trace.commit_sha = "abc123";

    CompilationUnit cu;
    cu.file_path = "/home/user/file.cpp";
    trace.compilation_units.push_back(cu);

    Anonymizer::AnonymizationConfig config;
    config.anonymize_paths = true;
    config.anonymize_commit_info = false;
    Anonymizer anonymizer(config);

    const BuildTrace anonymized = anonymizer.anonymize_trace(trace);

    // Paths should be anonymized
    EXPECT_NE(anonymized.compilation_units[0].file_path, trace.compilation_units[0].file_path);
    // Commit should remain unchanged
    EXPECT_EQ(anonymized.commit_sha, trace.commit_sha);
}

TEST(AnonymizerTest, AnonymizeTrace_MultipleCompilationUnits) {
    BuildTrace trace;

    for (int i = 0; i < 5; ++i) {
        CompilationUnit cu;
        cu.file_path = "/home/user/file" + std::to_string(i) + ".cpp";
        cu.direct_includes = {"/home/user/header" + std::to_string(i) + ".h"};
        trace.compilation_units.push_back(cu);
    }

    const Anonymizer::AnonymizationConfig config;
    Anonymizer anonymizer(config);

    const BuildTrace anonymized = anonymizer.anonymize_trace(trace);

    ASSERT_EQ(anonymized.compilation_units.size(), 5);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_NE(anonymized.compilation_units[i].file_path,
                  trace.compilation_units[i].file_path);
    }
}

TEST(AnonymizerTest, PreservePatterns) {
    Anonymizer::AnonymizationConfig config;
    config.preserve_patterns = {"/usr/include/*", "*/system/*"};
    Anonymizer anonymizer(config);

    const std::string system_path = "/usr/include/stdio.h";
    const std::string user_path = "/home/user/file.cpp";

    const std::string anon_system = anonymizer.anonymize_path(system_path);
    const std::string anon_user = anonymizer.anonymize_path(user_path);

    EXPECT_NE(anon_user, user_path);
}

TEST(AnonymizerTest, ReplacementRoot) {
    Anonymizer::AnonymizationConfig config;
    config.replacement_root = "/custom_root";
    Anonymizer anonymizer(config);

    const std::string path = "/home/user/project/file.cpp";
    const std::string anonymized = anonymizer.anonymize_path(path);

    EXPECT_FALSE(anonymized.empty());
}

TEST(AnonymizerTest, ComplexTrace) {
    BuildTrace trace;
    trace.trace_id = "complex_001";
    trace.commit_sha = "1a2b3c4d5e6f";
    trace.branch = "feature/new-feature";
    trace.build_system = "cmake";
    trace.changed_files = {"/home/user/file1.cpp", "/home/user/file2.h"};

    CompilationUnit cu1;
    cu1.file_path = "/home/user/src/main.cpp";
    cu1.direct_includes = {"/home/user/include/header.h", "/usr/include/vector"};
    cu1.commit_sha = "1a2b3c4d5e6f";
    trace.compilation_units.push_back(cu1);

    Anonymizer::AnonymizationConfig config;
    config.anonymize_paths = true;
    config.anonymize_commit_info = true;
    Anonymizer anonymizer(config);

    const BuildTrace anonymized = anonymizer.anonymize_trace(trace);

    EXPECT_NE(anonymized.commit_sha, trace.commit_sha);
    EXPECT_NE(anonymized.compilation_units[0].file_path, trace.compilation_units[0].file_path);
    EXPECT_EQ(anonymized.compilation_units[0].commit_sha, trace.compilation_units[0].commit_sha);
}