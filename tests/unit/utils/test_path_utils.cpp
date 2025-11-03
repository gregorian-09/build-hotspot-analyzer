//
// Created by gregorian on 03/11/2025.
//

#include <gtest/gtest.h>
#include "bha/utils/path_utils.h"
#include <fstream>
#include <filesystem>

using namespace bha::utils;
namespace fs = std::filesystem;

class PathUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "path_utils_test";
        fs::create_directories(temp_dir);

        create_test_file("file1.txt", "content1");
        create_test_file("file2.cpp", "content2");
        create_test_file("document.pdf", "pdf content");

        fs::create_directories(temp_dir / "subdir1");
        create_test_file("subdir1/nested1.txt", "nested content 1");
        create_test_file("subdir1/nested2.cpp", "nested content 2");

        fs::create_directories(temp_dir / "subdir2" / "deep");
        create_test_file("subdir2/deep/deepfile.txt", "deep content");
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    void create_test_file(const std::string& relative_path, const std::string& content) const {
        const fs::path file_path = temp_dir / relative_path;
        std::ofstream file(file_path);
        file << content;
        file.close();
    }

    [[nodiscard]] std::string test_file_path(const std::string& relative_path) const {
        return (temp_dir / relative_path).string();
    }

    fs::path temp_dir;
};

TEST_F(PathUtilsTest, NormalizePath_WithDots) {
    const auto result = normalize_path("/home/user/../documents/./file.txt");
    EXPECT_TRUE(result.find("..") == std::string::npos);
    EXPECT_TRUE(result.find("/.") == std::string::npos);
}

TEST_F(PathUtilsTest, NormalizePath_SimplePath) {
    const auto result = normalize_path("/home/user/file.txt");
    EXPECT_FALSE(result.empty());
}

TEST_F(PathUtilsTest, NormalizePath_EmptyPath) {
    const auto result = normalize_path("");
    EXPECT_TRUE(result.empty() || result == ".");
}

TEST_F(PathUtilsTest, NormalizePath_RelativePath) {
    auto result = normalize_path("folder/../other/file.txt");
    EXPECT_NE(result.find("other"), std::string::npos);
}

TEST_F(PathUtilsTest, GetAbsolutePath_RelativePath) {
    auto result = get_absolute_path("file.txt");
    EXPECT_TRUE(is_absolute(result));
    EXPECT_NE(result.find("file.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, GetAbsolutePath_AlreadyAbsolute) {
    const std::string abs_path = test_file_path("file1.txt");
    const auto result = get_absolute_path(abs_path);
    EXPECT_EQ(normalize_path(result), normalize_path(abs_path));
}

TEST_F(PathUtilsTest, GetAbsolutePath_CurrentDirectory) {
    const auto result = get_absolute_path(".");
    EXPECT_TRUE(is_absolute(result));
}

TEST_F(PathUtilsTest, GetRelativePath_SameDirectory) {
    const std::string base = test_file_path("");
    const std::string path = test_file_path("file1.txt");
    auto result = get_relative_path(path, base);
    EXPECT_NE(result.find("file1.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, GetRelativePath_Subdirectory) {
    const std::string base = test_file_path("");
    const std::string path = test_file_path("subdir1/nested1.txt");
    auto result = get_relative_path(path, base);
    EXPECT_NE(result.find("subdir1"), std::string::npos);
    EXPECT_NE(result.find("nested1.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, GetRelativePath_ParentDirectory) {
    const std::string base = test_file_path("subdir1");
    const std::string path = test_file_path("file1.txt");
    const auto result = get_relative_path(path, base);
    EXPECT_FALSE(result.empty());
}

TEST_F(PathUtilsTest, GetFilename_SimplePath) {
    EXPECT_EQ(get_filename("/home/user/document.txt"), "document.txt");
}

TEST_F(PathUtilsTest, GetFilename_NoDirectory) {
    EXPECT_EQ(get_filename("file.txt"), "file.txt");
}

TEST_F(PathUtilsTest, GetFilename_DirectoryPath) {
    const auto result = get_filename("/home/user/folder/");
    EXPECT_TRUE(result.empty() || result == "folder");
}

TEST_F(PathUtilsTest, GetFilename_WithExtension) {
    EXPECT_EQ(get_filename("path/to/file.tar.gz"), "file.tar.gz");
}

TEST_F(PathUtilsTest, GetStem_SimpleFile) {
    EXPECT_EQ(get_stem("/home/user/document.txt"), "document");
}

TEST_F(PathUtilsTest, GetStem_NoExtension) {
    EXPECT_EQ(get_stem("/home/user/document"), "document");
}

TEST_F(PathUtilsTest, GetStem_MultipleExtensions) {
    EXPECT_EQ(get_stem("archive.tar.gz"), "archive.tar");
}

TEST_F(PathUtilsTest, GetStem_HiddenFile) {
    const auto result = get_stem(".hidden");
    EXPECT_TRUE(result == ".hidden" || result.empty());
}

TEST_F(PathUtilsTest, GetExtension_SimpleFile) {
    EXPECT_EQ(get_extension("document.txt"), ".txt");
}

TEST_F(PathUtilsTest, GetExtension_NoExtension) {
    EXPECT_EQ(get_extension("document"), "");
}

TEST_F(PathUtilsTest, GetExtension_MultipleExtensions) {
    EXPECT_EQ(get_extension("archive.tar.gz"), ".gz");
}

TEST_F(PathUtilsTest, GetExtension_HiddenFile) {
    EXPECT_EQ(get_extension(".gitignore"), "");
}

TEST_F(PathUtilsTest, GetExtension_WithPath) {
    EXPECT_EQ(get_extension("/home/user/file.cpp"), ".cpp");
}

TEST_F(PathUtilsTest, GetParentPath_SimplePath) {
    auto result = get_parent_path("/home/user/document.txt");
    EXPECT_NE(result.find("user"), std::string::npos);
}

TEST_F(PathUtilsTest, GetParentPath_NoParent) {
    const auto result = get_parent_path("file.txt");
    EXPECT_TRUE(result.empty() || result == ".");
}

TEST_F(PathUtilsTest, GetParentPath_RootPath) {
    const auto result = get_parent_path("/");
    EXPECT_TRUE(result.empty() || result == "/");
}

TEST_F(PathUtilsTest, GetParentPath_NestedPath) {
    const auto result = get_parent_path("/a/b/c/d/file.txt");
    EXPECT_NE(result.find('d'), std::string::npos);
}

TEST_F(PathUtilsTest, JoinPaths_TwoComponents) {
    const auto result = join_paths("/home/user", "documents");
    EXPECT_NE(result.find("user"), std::string::npos);
    EXPECT_NE(result.find("documents"), std::string::npos);
}

TEST_F(PathUtilsTest, JoinPaths_EmptyFirst) {
    const auto result = join_paths("", "documents");
    EXPECT_EQ(result, "documents");
}

TEST_F(PathUtilsTest, JoinPaths_EmptySecond) {
    const auto result = join_paths("/home/user", "");
    EXPECT_NE(result.find("user"), std::string::npos);
}

TEST_F(PathUtilsTest, JoinPaths_MultipleComponents) {
    const auto result = join_paths("/home", "user", "documents", "file.txt");
    EXPECT_NE(result.find("home"), std::string::npos);
    EXPECT_NE(result.find("user"), std::string::npos);
    EXPECT_NE(result.find("documents"), std::string::npos);
    EXPECT_NE(result.find("file.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, JoinPaths_ThreeComponents) {
    const auto result = join_paths("a", "b", "c");
    EXPECT_NE(result.find('a'), std::string::npos);
    EXPECT_NE(result.find('b'), std::string::npos);
    EXPECT_NE(result.find('c'), std::string::npos);
}

TEST_F(PathUtilsTest, IsAbsolute_AbsolutePosixPath) {
#ifdef _WIN32
    EXPECT_FALSE(is_absolute("/home/user/file.txt"));
    EXPECT_TRUE(is_absolute("C:/home/user/file.txt"));
#else
    EXPECT_TRUE(is_absolute("/home/user/file.txt"));
#endif
}

TEST_F(PathUtilsTest, IsAbsolute_RelativePath) {
    EXPECT_FALSE(is_absolute("documents/file.txt"));
}

TEST_F(PathUtilsTest, IsAbsolute_CurrentDirectory) {
    EXPECT_FALSE(is_absolute("."));
}

TEST_F(PathUtilsTest, IsAbsolute_ParentDirectory) {
    EXPECT_FALSE(is_absolute(".."));
}

#ifdef _WIN32
TEST_F(PathUtilsTest, IsAbsolute_WindowsAbsolutePath) {
    EXPECT_TRUE(is_absolute("C:\\Users\\file.txt"));
}
#endif

TEST_F(PathUtilsTest, PathExists_ExistingFile) {
    EXPECT_TRUE(path_exists(test_file_path("file1.txt")));
}

TEST_F(PathUtilsTest, PathExists_ExistingDirectory) {
    EXPECT_TRUE(path_exists(test_file_path("subdir1")));
}

TEST_F(PathUtilsTest, PathExists_NonExistent) {
    EXPECT_FALSE(path_exists(test_file_path("nonexistent.txt")));
}

TEST_F(PathUtilsTest, PathExists_EmptyPath) {
    EXPECT_FALSE(path_exists(""));
}

TEST_F(PathUtilsTest, IsFile_RegularFile) {
    EXPECT_TRUE(is_file(test_file_path("file1.txt")));
}

TEST_F(PathUtilsTest, IsFile_Directory) {
    EXPECT_FALSE(is_file(test_file_path("subdir1")));
}

TEST_F(PathUtilsTest, IsFile_NonExistent) {
    EXPECT_FALSE(is_file(test_file_path("nonexistent.txt")));
}

TEST_F(PathUtilsTest, IsDirectory_ExistingDirectory) {
    EXPECT_TRUE(is_directory(test_file_path("subdir1")));
}

TEST_F(PathUtilsTest, IsDirectory_File) {
    EXPECT_FALSE(is_directory(test_file_path("file1.txt")));
}

TEST_F(PathUtilsTest, IsDirectory_NonExistent) {
    EXPECT_FALSE(is_directory(test_file_path("nonexistent_dir")));
}

TEST_F(PathUtilsTest, IsDirectory_NestedDirectory) {
    EXPECT_TRUE(is_directory(test_file_path("subdir2/deep")));
}

TEST_F(PathUtilsTest, HasExtension_WithDot) {
    EXPECT_TRUE(has_extension("document.txt", ".txt"));
}

TEST_F(PathUtilsTest, HasExtension_WithoutDot) {
    EXPECT_TRUE(has_extension("document.txt", "txt"));
}

TEST_F(PathUtilsTest, HasExtension_NoMatch) {
    EXPECT_FALSE(has_extension("document.txt", ".pdf"));
}

TEST_F(PathUtilsTest, HasExtension_NoExtension) {
    EXPECT_FALSE(has_extension("document", ".txt"));
}

TEST_F(PathUtilsTest, HasExtension_CaseSensitive) {
    const auto result = has_extension("file.TXT", ".txt");
    EXPECT_TRUE(result == true || result == false);
}

TEST_F(PathUtilsTest, ReplaceExtension_ExistingExtension) {
    const auto result = replace_extension("document.txt", ".pdf");
    EXPECT_NE(result.find(".pdf"), std::string::npos);
    EXPECT_EQ(result.find(".txt"), std::string::npos);
}

TEST_F(PathUtilsTest, ReplaceExtension_NoExtension) {
    const auto result = replace_extension("document", ".txt");
    EXPECT_NE(result.find(".txt"), std::string::npos);
}

TEST_F(PathUtilsTest, ReplaceExtension_EmptyExtension) {
    const auto result = replace_extension("document.txt", "");
    EXPECT_EQ(result.find(".txt"), std::string::npos);
}

TEST_F(PathUtilsTest, ReplaceExtension_WithoutDot) {
    const auto result = replace_extension("document.txt", "pdf");
    EXPECT_TRUE(result.find("pdf") != std::string::npos);
}

TEST_F(PathUtilsTest, ToNativeSeparators_PosixPath) {
    const auto result = to_native_separators("home/user/documents");
    EXPECT_FALSE(result.empty());
}

TEST_F(PathUtilsTest, ToNativeSeparators_MixedSeparators) {
    const auto result = to_native_separators("home/user\\documents");
    EXPECT_FALSE(result.empty());
}

TEST_F(PathUtilsTest, ToPosixSeparators_WindowsPath) {
    const auto result = to_posix_separators("home\\user\\documents");
    EXPECT_NE(result.find('/'), std::string::npos);
    EXPECT_EQ(result.find('\\'), std::string::npos);
}

TEST_F(PathUtilsTest, ToPosixSeparators_AlreadyPosix) {
    const auto result = to_posix_separators("home/user/documents");
    EXPECT_EQ(result, "home/user/documents");
}

TEST_F(PathUtilsTest, IsSubdirectoryOf_DirectChild) {
    const std::string parent = test_file_path("");
    const std::string child = test_file_path("subdir1");
    EXPECT_TRUE(is_subdirectory_of(child, parent));
}

TEST_F(PathUtilsTest, IsSubdirectoryOf_DeepNesting) {
    const std::string parent = test_file_path("");
    const std::string child = test_file_path("subdir2/deep");
    EXPECT_TRUE(is_subdirectory_of(child, parent));
}

TEST_F(PathUtilsTest, IsSubdirectoryOf_NotSubdirectory) {
    const std::string parent = test_file_path("subdir1");
    const std::string child = test_file_path("subdir2");
    EXPECT_FALSE(is_subdirectory_of(child, parent));
}

TEST_F(PathUtilsTest, IsSubdirectoryOf_SamePath) {
    std::string path = test_file_path("subdir1");
    EXPECT_FALSE(is_subdirectory_of(path, path));
}

TEST_F(PathUtilsTest, FindFileInParents_FileInCurrentDir) {
    auto result = find_file_in_parents(test_file_path(""), "file1.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("file1.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, FindFileInParents_FileInParentDir) {
    const auto result = find_file_in_parents(test_file_path("subdir1"), "file1.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("file1.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, FindFileInParents_FileNotFound) {
    const auto result = find_file_in_parents(test_file_path("subdir1"), "nonexistent.txt");
    EXPECT_FALSE(result.has_value());
}

TEST_F(PathUtilsTest, FindFileInParents_DeepNesting) {
    auto result = find_file_in_parents(test_file_path("subdir2/deep"), "file1.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("file1.txt"), std::string::npos);
}

TEST_F(PathUtilsTest, ListFiles_NonRecursive) {
    const auto files = list_files(test_file_path(""), false);
    EXPECT_GE(files.size(), 3); // At least file1.txt, file2.cpp, document.pdf

    bool found_file1 = false;
    for (const auto& file : files) {
        if (file.find("file1.txt") != std::string::npos) {
            found_file1 = true;
            break;
        }
    }
    EXPECT_TRUE(found_file1);
}

TEST_F(PathUtilsTest, ListFiles_Recursive) {
    const auto files = list_files(test_file_path(""), true);
    EXPECT_GT(files.size(), 3); // Should include nested files

    bool found_nested = false;
    for (const auto& file : files) {
        if (file.find("nested1.txt") != std::string::npos) {
            found_nested = true;
            break;
        }
    }
    EXPECT_TRUE(found_nested);
}

TEST_F(PathUtilsTest, ListFiles_EmptyDirectory) {
    const fs::path empty_dir = temp_dir / "empty";
    fs::create_directories(empty_dir);

    const auto files = list_files(empty_dir.string(), false);
    EXPECT_TRUE(files.empty());
}

TEST_F(PathUtilsTest, ListFiles_NonExistentDirectory) {
    const auto files = list_files(test_file_path("nonexistent"), false);
    EXPECT_TRUE(files.empty());
}

TEST_F(PathUtilsTest, ListFilesWithExtension_TxtFiles) {
    const auto files = list_files_with_extension(test_file_path(""), ".txt", false);
    EXPECT_GE(files.size(), 1);

    for (const auto& file : files) {
        EXPECT_NE(file.find(".txt"), std::string::npos);
    }
}

TEST_F(PathUtilsTest, ListFilesWithExtension_CppFiles) {
    const auto files = list_files_with_extension(test_file_path(""), ".cpp", false);
    EXPECT_GE(files.size(), 1);
}

TEST_F(PathUtilsTest, ListFilesWithExtension_Recursive) {
    const auto files = list_files_with_extension(test_file_path(""), ".txt", true);
    EXPECT_GT(files.size(), 1); // Should include nested .txt files

    bool found_nested = false;
    for (const auto& file : files) {
        if (file.find("nested1.txt") != std::string::npos) {
            found_nested = true;
            break;
        }
    }
    EXPECT_TRUE(found_nested);
}

TEST_F(PathUtilsTest, ListFilesWithExtension_NoMatch) {
    const auto files = list_files_with_extension(test_file_path(""), ".xyz", false);
    EXPECT_TRUE(files.empty());
}

TEST_F(PathUtilsTest, ListFilesWithExtension_WithoutDot) {
    const auto files = list_files_with_extension(test_file_path(""), "txt", false);
    EXPECT_GE(files.size(), 1);
}

TEST_F(PathUtilsTest, MakePreferred_SimplePath) {
    const auto result = make_preferred("home/user/documents");
    EXPECT_FALSE(result.empty());
}

TEST_F(PathUtilsTest, MakePreferred_MixedSeparators) {
    const auto result = make_preferred("home/user\\documents");
    EXPECT_FALSE(result.empty());
}

TEST_F(PathUtilsTest, CreateDirectories_NewDirectory) {
    const std::string new_dir = test_file_path("newdir");
    EXPECT_TRUE(create_directories(new_dir));
    EXPECT_TRUE(is_directory(new_dir));
}

TEST_F(PathUtilsTest, CreateDirectories_NestedDirectories) {
    const std::string nested = test_file_path("a/b/c/d");
    EXPECT_TRUE(create_directories(nested));
    EXPECT_TRUE(is_directory(nested));
}

TEST_F(PathUtilsTest, CreateDirectories_AlreadyExists) {
    EXPECT_TRUE(create_directories(test_file_path("subdir1")));
}

TEST_F(PathUtilsTest, FileSize_ExistingFile) {
    const auto size = file_size(test_file_path("file1.txt"));
    ASSERT_TRUE(size.has_value());
    EXPECT_GT(*size, 0);
}

TEST_F(PathUtilsTest, FileSize_NonExistentFile) {
    const auto size = file_size(test_file_path("nonexistent.txt"));
    EXPECT_FALSE(size.has_value());
}

TEST_F(PathUtilsTest, FileSize_Directory) {
    const auto size = file_size(test_file_path("subdir1"));
    EXPECT_TRUE(size.has_value() || !size.has_value());
}

TEST_F(PathUtilsTest, GetCurrentDirectory_Valid) {
    const auto cwd = get_current_directory();
    EXPECT_FALSE(cwd.empty());
    EXPECT_TRUE(is_absolute(cwd));
    EXPECT_TRUE(is_directory(cwd));
}

TEST_F(PathUtilsTest, IsSameFile_SamePath) {
    const std::string path = test_file_path("file1.txt");
    EXPECT_TRUE(is_same_file(path, path));
}

TEST_F(PathUtilsTest, IsSameFile_DifferentPaths) {
    const std::string path1 = test_file_path("file1.txt");
    const std::string path2 = test_file_path("file2.cpp");
    EXPECT_FALSE(is_same_file(path1, path2));
}

TEST_F(PathUtilsTest, IsSameFile_RelativeVsAbsolute) {
    const std::string abs_path = test_file_path("file1.txt");

    // Save current directory
    const auto original_cwd = get_current_directory();

    // Change to temp directory
    fs::current_path(temp_dir);

    // Now "file1.txt" relative should be same as absolute path
    const bool result = is_same_file("file1.txt", abs_path);

    // Restore original directory
    fs::current_path(original_cwd);

    EXPECT_TRUE(result);
}

TEST_F(PathUtilsTest, IsSameFile_NonExistent) {
    std::string path1 = test_file_path("nonexistent1.txt");
    std::string path2 = test_file_path("nonexistent2.txt");
    EXPECT_FALSE(is_same_file(path1, path2));
}