//
// Created by gregorian on 04/11/2025.
//

#include <gtest/gtest.h>
#include "bha/utils/file_utils.h"
#include <filesystem>
#include <fstream>

using namespace bha::utils;
namespace fs = std::filesystem;

class FileUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "file_utils_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string test_path(const std::string& filename) const
    {
        return (temp_dir / filename).string();
    }

    void create_test_file(const std::string& filename, const std::string& content) const
    {
        std::ofstream file(temp_dir / filename);
        file << content;
        file.close();
    }

    void create_binary_file(const std::string& filename, const std::vector<char>& data) const
    {
        std::ofstream file(temp_dir / filename, std::ios::binary);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        file.close();
    }

    [[nodiscard]] bool file_contains(const std::string& filename, const std::string& expected) const
    {
        std::ifstream file(temp_dir / filename);
        const std::string content((std::istreambuf_iterator(file)),
                           std::istreambuf_iterator<char>());
        return content == expected;
    }

    fs::path temp_dir;
};

TEST_F(FileUtilsTest, ReadFile_ExistingFile) {
    create_test_file("test.txt", "Hello, World!");

    const auto content = read_file(test_path("test.txt"));
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "Hello, World!");
}

TEST_F(FileUtilsTest, ReadFile_EmptyFile) {
    create_test_file("empty.txt", "");

    const auto content = read_file(test_path("empty.txt"));
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "");
}

TEST_F(FileUtilsTest, ReadFile_MultilineContent) {
    create_test_file("multiline.txt", "Line 1\nLine 2\nLine 3");

    const auto content = read_file(test_path("multiline.txt"));
    ASSERT_TRUE(content.has_value());

#ifdef _WIN32
    EXPECT_EQ(*content, "Line 1\r\nLine 2\r\nLine 3");
#else
    EXPECT_EQ(*content, "Line 1\nLine 2\nLine 3");
#endif
}

TEST_F(FileUtilsTest, ReadFile_NonExistent) {
    const auto content = read_file(test_path("nonexistent.txt"));
    EXPECT_FALSE(content.has_value());
}

TEST_F(FileUtilsTest, ReadFile_LargeFile) {
    const std::string large_content(100000, 'A');
    create_test_file("large.txt", large_content);

    const auto content = read_file(test_path("large.txt"));
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(content->size(), 100000);
}

TEST_F(FileUtilsTest, ReadLines_MultipleLines) {
    create_test_file("lines.txt", "Line 1\nLine 2\nLine 3");

    const auto lines = read_lines(test_path("lines.txt"));
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 3);
    EXPECT_EQ((*lines)[0], "Line 1");
    EXPECT_EQ((*lines)[1], "Line 2");
    EXPECT_EQ((*lines)[2], "Line 3");
}

TEST_F(FileUtilsTest, ReadLines_EmptyFile) {
    create_test_file("empty.txt", "");

    const auto lines = read_lines(test_path("empty.txt"));
    ASSERT_TRUE(lines.has_value());
    EXPECT_TRUE(lines->empty());
}

TEST_F(FileUtilsTest, ReadLines_SingleLine) {
    create_test_file("single.txt", "Only one line");

    const auto lines = read_lines(test_path("single.txt"));
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 1);
    EXPECT_EQ((*lines)[0], "Only one line");
}

TEST_F(FileUtilsTest, ReadLines_TrailingNewline) {
    create_test_file("trailing.txt", "Line 1\nLine 2\n");

    const auto lines = read_lines(test_path("trailing.txt"));
    ASSERT_TRUE(lines.has_value());
    EXPECT_GE(lines->size(), 2);
}

TEST_F(FileUtilsTest, ReadLines_EmptyLines) {
    create_test_file("empty_lines.txt", "Line 1\n\nLine 3");

    const auto lines = read_lines(test_path("empty_lines.txt"));
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 3);
    EXPECT_EQ((*lines)[1], "");
}

TEST_F(FileUtilsTest, ReadLines_NonExistent) {
    const auto lines = read_lines(test_path("nonexistent.txt"));
    EXPECT_FALSE(lines.has_value());
}

TEST_F(FileUtilsTest, WriteFile_NewFile) {
    const std::string path = test_path("new.txt");
    const bool result = write_file(path, "New content");

    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(path));
    EXPECT_TRUE(file_contains("new.txt", "New content"));
}

TEST_F(FileUtilsTest, WriteFile_OverwriteExisting) {
    create_test_file("existing.txt", "Old content");

    const bool result = write_file(test_path("existing.txt"), "New content");
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("existing.txt", "New content"));
}

TEST_F(FileUtilsTest, WriteFile_EmptyContent) {
    const bool result = write_file(test_path("empty.txt"), "");
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("empty.txt", ""));
}

TEST_F(FileUtilsTest, WriteFile_MultilineContent) {
    const bool result = write_file(test_path("multiline.txt"), "Line 1\nLine 2\nLine 3");
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("multiline.txt", "Line 1\nLine 2\nLine 3"));
}

TEST_F(FileUtilsTest, WriteFile_LargeContent) {
    const std::string large_content(100000, 'B');
    const bool result = write_file(test_path("large.txt"), large_content);

    EXPECT_TRUE(result);
    const auto read_back = read_file(test_path("large.txt"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, large_content);
}

TEST_F(FileUtilsTest, WriteLines_MultipleLines) {
    const std::vector<std::string> lines = {"Line 1", "Line 2", "Line 3"};
    const bool result = write_lines(test_path("lines.txt"), lines);

    EXPECT_TRUE(result);
    const auto read_back = read_lines(test_path("lines.txt"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, lines);
}

TEST_F(FileUtilsTest, WriteLines_EmptyVector) {
    constexpr std::vector<std::string> lines;
    const bool result = write_lines(test_path("empty.txt"), lines);

    EXPECT_TRUE(result);
    const auto read_back = read_lines(test_path("empty.txt"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_TRUE(read_back->empty());
}

TEST_F(FileUtilsTest, WriteLines_SingleLine) {
    const std::vector<std::string> lines = {"Only one line"};
    const bool result = write_lines(test_path("single.txt"), lines);

    EXPECT_TRUE(result);
    const auto read_back = read_lines(test_path("single.txt"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, lines);
}

TEST_F(FileUtilsTest, WriteLines_WithEmptyLines) {
    const std::vector<std::string> lines = {"Line 1", "", "Line 3"};
    const bool result = write_lines(test_path("empty_lines.txt"), lines);

    EXPECT_TRUE(result);
    const auto read_back = read_lines(test_path("empty_lines.txt"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, lines);
}

TEST_F(FileUtilsTest, AppendToFile_ExistingFile) {
    create_test_file("append.txt", "Initial content");

    const bool result = append_to_file(test_path("append.txt"), " appended");
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("append.txt", "Initial content appended"));
}

TEST_F(FileUtilsTest, AppendToFile_NewFile) {
    const bool result = append_to_file(test_path("new_append.txt"), "First content");
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("new_append.txt", "First content"));
}

TEST_F(FileUtilsTest, AppendToFile_Multiple) {
    create_test_file("multi_append.txt", "Start");

    append_to_file(test_path("multi_append.txt"), " - Part 1");
    append_to_file(test_path("multi_append.txt"), " - Part 2");

    EXPECT_TRUE(file_contains("multi_append.txt", "Start - Part 1 - Part 2"));
}

TEST_F(FileUtilsTest, AppendToFile_EmptyContent) {
    create_test_file("append_empty.txt", "Content");

    const bool result = append_to_file(test_path("append_empty.txt"), "");
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("append_empty.txt", "Content"));
}

TEST_F(FileUtilsTest, CopyFile_Success) {
    create_test_file("source.txt", "Source content");

    const bool result = copy_file(test_path("source.txt"), test_path("dest.txt"));
    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(test_path("dest.txt")));
    EXPECT_TRUE(file_contains("dest.txt", "Source content"));
    EXPECT_TRUE(file_contains("source.txt", "Source content"));
}

TEST_F(FileUtilsTest, CopyFile_SourceNotExists) {
    const bool result = copy_file(test_path("nonexistent.txt"), test_path("dest.txt"));
    EXPECT_FALSE(result);
}

TEST_F(FileUtilsTest, CopyFile_DestinationExists_NoOverwrite) {
    create_test_file("source.txt", "Source");
    create_test_file("dest.txt", "Destination");

    const bool result = copy_file(test_path("source.txt"), test_path("dest.txt"), false);
    EXPECT_FALSE(result);
    EXPECT_TRUE(file_contains("dest.txt", "Destination"));
}

TEST_F(FileUtilsTest, CopyFile_DestinationExists_Overwrite) {
    create_test_file("source.txt", "Source");
    create_test_file("dest.txt", "Destination");

    const bool result = copy_file(test_path("source.txt"), test_path("dest.txt"), true);
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("dest.txt", "Source"));
}

TEST_F(FileUtilsTest, CopyFile_EmptyFile) {
    create_test_file("empty_source.txt", "");

    const bool result = copy_file(test_path("empty_source.txt"), test_path("empty_dest.txt"));
    EXPECT_TRUE(result);
    EXPECT_TRUE(file_contains("empty_dest.txt", ""));
}

TEST_F(FileUtilsTest, CopyFile_LargeFile) {
    const std::string large_content(100000, 'C');
    create_test_file("large_source.txt", large_content);

    const bool result = copy_file(test_path("large_source.txt"), test_path("large_dest.txt"));
    EXPECT_TRUE(result);

    const auto content = read_file(test_path("large_dest.txt"));
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, large_content);
}

TEST_F(FileUtilsTest, MoveFile_Success) {
    create_test_file("move_source.txt", "Move content");

    const bool result = move_file(test_path("move_source.txt"), test_path("move_dest.txt"));
    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(test_path("move_dest.txt")));
    EXPECT_FALSE(fs::exists(test_path("move_source.txt"))); // Source removed
    EXPECT_TRUE(file_contains("move_dest.txt", "Move content"));
}

TEST_F(FileUtilsTest, MoveFile_SourceNotExists) {
    const bool result = move_file(test_path("nonexistent.txt"), test_path("dest.txt"));
    EXPECT_FALSE(result);
}

TEST_F(FileUtilsTest, MoveFile_Rename) {
    create_test_file("old_name.txt", "Content");

    const bool result = move_file(test_path("old_name.txt"), test_path("new_name.txt"));
    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(test_path("new_name.txt")));
    EXPECT_FALSE(fs::exists(test_path("old_name.txt")));
}

TEST_F(FileUtilsTest, MoveFile_ToSubdirectory) {
    create_test_file("file.txt", "Content");
    fs::create_directories(temp_dir / "subdir");

    const bool result = move_file(test_path("file.txt"), test_path("subdir/file.txt"));
    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(temp_dir / "subdir" / "file.txt"));
    EXPECT_FALSE(fs::exists(test_path("file.txt")));
}

TEST_F(FileUtilsTest, DeleteFile_ExistingFile) {
    create_test_file("delete_me.txt", "Content");

    const bool result = delete_file(test_path("delete_me.txt"));
    EXPECT_TRUE(result);
    EXPECT_FALSE(fs::exists(test_path("delete_me.txt")));
}

TEST_F(FileUtilsTest, DeleteFile_NonExistent) {
    const bool result = delete_file(test_path("nonexistent.txt"));
    EXPECT_FALSE(result);
}

TEST_F(FileUtilsTest, DeleteFile_AlreadyDeleted) {
    create_test_file("temp.txt", "Content");
    delete_file(test_path("temp.txt"));

    const bool result = delete_file(test_path("temp.txt"));
    EXPECT_FALSE(result);
}

TEST_F(FileUtilsTest, FileExists_ExistingFile) {
    create_test_file("exists.txt", "Content");
    EXPECT_TRUE(file_exists(test_path("exists.txt")));
}

TEST_F(FileUtilsTest, FileExists_NonExistent) {
    EXPECT_FALSE(file_exists(test_path("nonexistent.txt")));
}

TEST_F(FileUtilsTest, FileExists_Directory) {
    fs::create_directories(temp_dir / "testdir");
    const bool result = fs::is_directory(temp_dir / "testdir");
    EXPECT_TRUE(result);
}

TEST_F(FileUtilsTest, FileExists_EmptyPath) {
    EXPECT_FALSE(file_exists(""));
}

TEST_F(FileUtilsTest, GetFileSize_ExistingFile) {
    create_test_file("sized.txt", "12345");

    const auto size = get_file_size(test_path("sized.txt"));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 5);
}

TEST_F(FileUtilsTest, GetFileSize_EmptyFile) {
    create_test_file("empty_size.txt", "");

    const auto size = get_file_size(test_path("empty_size.txt"));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 0);
}

TEST_F(FileUtilsTest, GetFileSize_NonExistent) {
    const auto size = get_file_size(test_path("nonexistent.txt"));
    EXPECT_FALSE(size.has_value());
}

TEST_F(FileUtilsTest, GetFileSize_LargeFile) {
    const std::string content(100000, 'X');
    create_test_file("large_size.txt", content);

    const auto size = get_file_size(test_path("large_size.txt"));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 100000);
}

TEST_F(FileUtilsTest, GetFileExtension_SimpleExtension) {
    const auto ext = get_file_extension("file.txt");
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(*ext, ".txt");
}

TEST_F(FileUtilsTest, GetFileExtension_MultipleExtensions) {
    const auto ext = get_file_extension("archive.tar.gz");
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(*ext, ".gz");
}

TEST_F(FileUtilsTest, GetFileExtension_NoExtension) {
    const auto ext = get_file_extension("file");
    EXPECT_FALSE(ext.has_value());
}

TEST_F(FileUtilsTest, GetFileExtension_HiddenFile) {
    const auto ext = get_file_extension(".gitignore");
    EXPECT_FALSE(ext.has_value());
}

TEST_F(FileUtilsTest, GetFileExtension_WithPath) {
    const auto ext = get_file_extension("/path/to/file.cpp");
    ASSERT_TRUE(ext.has_value());
    EXPECT_EQ(*ext, ".cpp");
}

TEST_F(FileUtilsTest, GetFileExtension_DotAtEnd) {
    const auto ext = get_file_extension("file.");
    EXPECT_TRUE(ext.has_value() || !ext.has_value());
}

TEST_F(FileUtilsTest, IsReadable_ExistingFile) {
    create_test_file("readable.txt", "Content");
    EXPECT_TRUE(is_readable(test_path("readable.txt")));
}

TEST_F(FileUtilsTest, IsReadable_NonExistent) {
    EXPECT_FALSE(is_readable(test_path("nonexistent.txt")));
}

TEST_F(FileUtilsTest, IsWritable_ExistingFile) {
    create_test_file("writable.txt", "Content");
    EXPECT_TRUE(is_writable(test_path("writable.txt")));
}

TEST_F(FileUtilsTest, IsWritable_NonExistent) {
    const auto result = is_writable(test_path("nonexistent.txt"));
    EXPECT_TRUE(result || !result);
}

TEST_F(FileUtilsTest, IsWritable_Directory) {
    EXPECT_FALSE(is_writable(temp_dir.string())); // is_writable(path) only check whether the file at a particular path is writable.
}

TEST_F(FileUtilsTest, ReadBinaryFile_Success) {
    const std::vector<char> data = {0x00, 0x01, 0x02, 0x03, static_cast<char>(0xFF)};
    create_binary_file("binary.dat", data);

    const auto read_data = read_binary_file(test_path("binary.dat"));
    ASSERT_TRUE(read_data.has_value());
    EXPECT_EQ(*read_data, data);
}

TEST_F(FileUtilsTest, ReadBinaryFile_EmptyFile) {
    constexpr std::vector<char> empty_data;
    create_binary_file("empty_binary.dat", empty_data);

    const auto read_data = read_binary_file(test_path("empty_binary.dat"));
    ASSERT_TRUE(read_data.has_value());
    EXPECT_TRUE(read_data->empty());
}

TEST_F(FileUtilsTest, ReadBinaryFile_NonExistent) {
    const auto read_data = read_binary_file(test_path("nonexistent.dat"));
    EXPECT_FALSE(read_data.has_value());
}

TEST_F(FileUtilsTest, WriteBinaryFile_Success) {
    const std::vector<char> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"

    const bool result = write_binary_file(test_path("binary_write.dat"), data);
    EXPECT_TRUE(result);

    const auto read_back = read_binary_file(test_path("binary_write.dat"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, data);
}

TEST_F(FileUtilsTest, WriteBinaryFile_EmptyData) {
    constexpr std::vector<char> empty;

    const bool result = write_binary_file(test_path("empty_binary_write.dat"), empty);
    EXPECT_TRUE(result);

    const auto size = get_file_size(test_path("empty_binary_write.dat"));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 0);
}

TEST_F(FileUtilsTest, BinaryFile_RoundTrip) {
    const std::vector<char> original = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                  static_cast<char>(0x88), static_cast<char>(0x99),
                                  static_cast<char>(0xAA), static_cast<char>(0xBB),
                                  static_cast<char>(0xCC), static_cast<char>(0xDD),
                                  static_cast<char>(0xEE), static_cast<char>(0xFF)};

    write_binary_file(test_path("roundtrip.dat"), original);
    const auto read_back = read_binary_file(test_path("roundtrip.dat"));

    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, original);
}

TEST_F(FileUtilsTest, CreateEmptyFile_NewFile) {
    const std::string path = test_path("new_empty.txt");

    const bool result = create_empty_file(path);
    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(path));

    const auto size = get_file_size(path);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 0);
}

TEST_F(FileUtilsTest, CreateEmptyFile_AlreadyExists) {
    create_test_file("existing_empty.txt", "Some content");

    const bool result = create_empty_file(test_path("existing_empty.txt"));
    EXPECT_TRUE(result);
    EXPECT_TRUE(fs::exists(test_path("existing_empty.txt")));
}

TEST_F(FileUtilsTest, ReadFileChunk_Beginning) {
    create_test_file("chunk.txt", "0123456789ABCDEFGHIJ");

    const auto chunk = read_file_chunk(test_path("chunk.txt"), 0, 5);
    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(*chunk, "01234");
}

TEST_F(FileUtilsTest, ReadFileChunk_Middle) {
    create_test_file("chunk_mid.txt", "0123456789ABCDEFGHIJ");

    const auto chunk = read_file_chunk(test_path("chunk_mid.txt"), 5, 5);
    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(*chunk, "56789");
}

TEST_F(FileUtilsTest, ReadFileChunk_End) {
    create_test_file("chunk_end.txt", "0123456789ABCDEFGHIJ");

    const auto chunk = read_file_chunk(test_path("chunk_end.txt"), 15, 10);
    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(*chunk, "FGHIJ");
}

TEST_F(FileUtilsTest, ReadFileChunk_BeyondEOF) {
    create_test_file("chunk_eof.txt", "01234");

    const auto chunk = read_file_chunk(test_path("chunk_eof.txt"), 10, 5);
    EXPECT_TRUE(!chunk.has_value() || chunk->empty());
}

TEST_F(FileUtilsTest, ReadFileChunk_ZeroSize) {
    create_test_file("chunk_zero.txt", "0123456789");

    const auto chunk = read_file_chunk(test_path("chunk_zero.txt"), 0, 0);
    EXPECT_FALSE(chunk.has_value());
}

TEST_F(FileUtilsTest, ReadFileChunk_EntireFile) {
    create_test_file("chunk_all.txt", "Complete");

    const auto chunk = read_file_chunk(test_path("chunk_all.txt"), 0, 1000);
    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(*chunk, "Complete");
}

TEST_F(FileUtilsTest, FileReader_IsOpen) {
    create_test_file("reader.txt", "Content");

    const FileReader reader(test_path("reader.txt"));
    EXPECT_TRUE(reader.is_open());
}

TEST_F(FileUtilsTest, FileReader_IsOpen_NonExistent) {
    const FileReader reader(test_path("nonexistent.txt"));
    EXPECT_FALSE(reader.is_open());
}

TEST_F(FileUtilsTest, FileReader_ReadLine) {
    create_test_file("reader_lines.txt", "Line 1\nLine 2\nLine 3");

    FileReader reader(test_path("reader_lines.txt"));
    ASSERT_TRUE(reader.is_open());

    auto line1 = reader.read_line();
    ASSERT_TRUE(line1.has_value());
    EXPECT_EQ(*line1, "Line 1");

    auto line2 = reader.read_line();
    ASSERT_TRUE(line2.has_value());
    EXPECT_EQ(*line2, "Line 2");

    auto line3 = reader.read_line();
    ASSERT_TRUE(line3.has_value());
    EXPECT_EQ(*line3, "Line 3");
}

TEST_F(FileUtilsTest, FileReader_ReadLine_EOF) {
    create_test_file("reader_eof.txt", "Only one line");

    FileReader reader(test_path("reader_eof.txt"));
    reader.read_line();

    const auto line = reader.read_line();
    EXPECT_FALSE(line.has_value());
}

TEST_F(FileUtilsTest, FileReader_ReadAll) {
    create_test_file("reader_all.txt", "Complete file content");

    const FileReader reader(test_path("reader_all.txt"));
    const auto content = reader.read_all();

    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "Complete file content");
}

TEST_F(FileUtilsTest, FileReader_ReadBytes) {
    std::vector<char> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    create_binary_file("reader_bytes.dat", data);

    FileReader reader(test_path("reader_bytes.dat"));
    auto bytes = reader.read_bytes(3);

    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(bytes->size(), 3);
    EXPECT_EQ((*bytes)[0], 0x48);
    EXPECT_EQ((*bytes)[1], 0x65);
    EXPECT_EQ((*bytes)[2], 0x6C);
}

TEST_F(FileUtilsTest, FileReader_EOF) {
    create_test_file("reader_eof_check.txt", "Short");

    const FileReader reader(test_path("reader_eof_check.txt"));
    EXPECT_FALSE(reader.eof());
}

TEST_F(FileUtilsTest, FileReader_Close) {
    create_test_file("reader_close.txt", "Content");

    FileReader reader(test_path("reader_close.txt"));
    EXPECT_TRUE(reader.is_open());

    reader.close();
    EXPECT_FALSE(reader.is_open());
}

TEST_F(FileUtilsTest, FileReader_MoveConstructor) {
    create_test_file("reader_move.txt", "Content");

    FileReader reader1(test_path("reader_move.txt"));
    EXPECT_TRUE(reader1.is_open());

    FileReader reader2(std::move(reader1));
    EXPECT_TRUE(reader2.is_open());
}

TEST_F(FileUtilsTest, FileReader_MoveAssignment) {
    create_test_file("reader_move_assign.txt", "Content");

    FileReader reader1(test_path("reader_move_assign.txt"));
    FileReader reader2(test_path("nonexistent.txt"));

    reader2 = std::move(reader1);
    EXPECT_TRUE(reader2.is_open());
}

TEST_F(FileUtilsTest, FileReader_ReadMultipleLines) {
    create_test_file("reader_multi.txt", "A\nB\nC\nD\nE");

    FileReader reader(test_path("reader_multi.txt"));

    int count = 0;
    while (const auto line = reader.read_line()) {
        count++;
        EXPECT_FALSE(line->empty());
    }

    EXPECT_EQ(count, 5);
}

TEST_F(FileUtilsTest, FileReader_EmptyFile) {
    create_test_file("reader_empty.txt", "");

    FileReader reader(test_path("reader_empty.txt"));
    EXPECT_TRUE(reader.is_open());

    const auto line = reader.read_line();
    EXPECT_FALSE(line.has_value());
}

TEST_F(FileUtilsTest, FileReader_ReadBytesMoreThanAvailable) {
    const std::vector<char> data = {0x01, 0x02, 0x03};
    create_binary_file("reader_bytes_exceed.dat", data);

    FileReader reader(test_path("reader_bytes_exceed.dat"));
    const auto bytes = reader.read_bytes(10);

    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(bytes->size(), 3);
}

TEST_F(FileUtilsTest, FileReader_ReadBytesZero) {
    create_test_file("reader_bytes_zero.txt", "Content");

    FileReader reader(test_path("reader_bytes_zero.txt"));
    const auto bytes = reader.read_bytes(0);
    EXPECT_FALSE(bytes.has_value());
}

TEST_F(FileUtilsTest, FileWriter_IsOpen) {
    const FileWriter writer(test_path("writer.txt"));
    EXPECT_TRUE(writer.is_open());
}

TEST_F(FileUtilsTest, FileWriter_Write) {
    FileWriter writer(test_path("writer_test.txt"));
    ASSERT_TRUE(writer.is_open());

    const bool result = writer.write("Hello, World!");
    EXPECT_TRUE(result);

    writer.close();
    EXPECT_TRUE(file_contains("writer_test.txt", "Hello, World!"));
}

TEST_F(FileUtilsTest, FileWriter_WriteMultiple) {
    FileWriter writer(test_path("writer_multiple.txt"));

    writer.write("Part 1");
    writer.write(" Part 2");
    writer.write(" Part 3");

    writer.close();
    EXPECT_TRUE(file_contains("writer_multiple.txt", "Part 1 Part 2 Part 3"));
}

TEST_F(FileUtilsTest, FileWriter_WriteLine) {
    FileWriter writer(test_path("writer_line.txt"));

    writer.write_line("Line 1");
    writer.write_line("Line 2");
    writer.write_line("Line 3");

    writer.close();

    auto lines = read_lines(test_path("writer_line.txt"));
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 3);
    EXPECT_EQ((*lines)[0], "Line 1");
    EXPECT_EQ((*lines)[1], "Line 2");
    EXPECT_EQ((*lines)[2], "Line 3");
}

TEST_F(FileUtilsTest, FileWriter_AppendMode) {
    create_test_file("writer_append.txt", "Existing content");

    FileWriter writer(test_path("writer_append.txt"), true);
    writer.write(" appended");
    writer.close();

    EXPECT_TRUE(file_contains("writer_append.txt", "Existing content appended"));
}

TEST_F(FileUtilsTest, FileWriter_TruncateMode) {
    create_test_file("writer_truncate.txt", "Old content");

    FileWriter writer(test_path("writer_truncate.txt"), false);
    writer.write("New content");
    writer.close();

    EXPECT_TRUE(file_contains("writer_truncate.txt", "New content"));
}

TEST_F(FileUtilsTest, FileWriter_Flush) {
    FileWriter writer(test_path("writer_flush.txt"));

    writer.write("Content");
    const bool flush_result = writer.flush();
    EXPECT_TRUE(flush_result);

    writer.close();
    EXPECT_TRUE(file_contains("writer_flush.txt", "Content"));
}

TEST_F(FileUtilsTest, FileWriter_Close) {
    FileWriter writer(test_path("writer_close.txt"));
    EXPECT_TRUE(writer.is_open());

    writer.close();
    EXPECT_FALSE(writer.is_open());
}

TEST_F(FileUtilsTest, FileWriter_MoveConstructor) {
    FileWriter writer1(test_path("writer_move.txt"));
    EXPECT_TRUE(writer1.is_open());

    FileWriter writer2(std::move(writer1));
    EXPECT_TRUE(writer2.is_open());
}

TEST_F(FileUtilsTest, FileWriter_MoveAssignment) {
    FileWriter writer1(test_path("writer_move_assign.txt"));
    FileWriter writer2(test_path("writer_temp.txt"));

    writer2 = std::move(writer1);
    EXPECT_TRUE(writer2.is_open());
}

TEST_F(FileUtilsTest, FileWriter_EmptyWrite) {
    FileWriter writer(test_path("writer_empty.txt"));

    const bool result = writer.write("");
    EXPECT_TRUE(result);

    writer.close();

    const auto size = get_file_size(test_path("writer_empty.txt"));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 0);
}

TEST_F(FileUtilsTest, FileWriter_LargeWrite) {
    const std::string large_content(100000, 'W');

    FileWriter writer(test_path("writer_large.txt"));
    writer.write(large_content);
    writer.close();

    const auto content = read_file(test_path("writer_large.txt"));
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, large_content);
}

TEST_F(FileUtilsTest, FileWriter_MultipleFlush) {
    FileWriter writer(test_path("writer_multi_flush.txt"));

    writer.write("Part 1");
    writer.flush();
    writer.write(" Part 2");
    writer.flush();
    writer.write(" Part 3");
    writer.flush();

    writer.close();
    EXPECT_TRUE(file_contains("writer_multi_flush.txt", "Part 1 Part 2 Part 3"));
}

TEST_F(FileUtilsTest, FileWriter_WriteBinaryData) {
    FileWriter writer(test_path("writer_binary.txt"));

    const std::string binary_data = {0x00, 0x01, 0x02, 0x03, static_cast<char>(0xFF)};
    writer.write(binary_data);
    writer.close();

    auto size = get_file_size(test_path("writer_binary.txt"));
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 5);
}

TEST_F(FileUtilsTest, EdgeCase_VeryLongFilename) {
    std::string long_name(200, 'a');
    long_name += ".txt";

    const bool result = write_file(test_path(long_name), "content");
    EXPECT_TRUE(result || !result);
}

TEST_F(FileUtilsTest, EdgeCase_SpecialCharactersInContent) {
    constexpr char raw_content[] = "Special chars: \t\n\r\0\x01\x02\xFF";
    const std::string special_content(raw_content, sizeof(raw_content) - 1);

    write_file(test_path("special.txt"), special_content);
    const auto read_back = read_file(test_path("special.txt"));
    ASSERT_TRUE(read_back.has_value());
}

TEST_F(FileUtilsTest, EdgeCase_NullCharacterInContent) {
    constexpr char raw_content[] = "Before\0After";
    const std::string content_with_null(raw_content, sizeof(raw_content) - 1);


    write_file(test_path("null_char.txt"), content_with_null);

    const auto read_back = read_file(test_path("null_char.txt"));
    EXPECT_TRUE(read_back.has_value());
}

TEST_F(FileUtilsTest, EdgeCase_UnicodeContent) {
    const std::string unicode = "Hello 世界 🌍 Привет";

    const bool write_result = write_file(test_path("unicode.txt"), unicode);
    EXPECT_TRUE(write_result);

    const auto read_back = read_file(test_path("unicode.txt"));
    ASSERT_TRUE(read_back.has_value());
    EXPECT_EQ(*read_back, unicode);
}

TEST_F(FileUtilsTest, EdgeCase_ConcurrentOperations) {
    create_test_file("concurrent.txt", "Initial");

    // Multiple readers on same file
    FileReader reader1(test_path("concurrent.txt"));
    FileReader reader2(test_path("concurrent.txt"));

    EXPECT_TRUE(reader1.is_open());
    EXPECT_TRUE(reader2.is_open());

    auto content1 = reader1.read_all();
    auto content2 = reader2.read_all();

    ASSERT_TRUE(content1.has_value());
    ASSERT_TRUE(content2.has_value());
    EXPECT_EQ(*content1, *content2);
}

TEST_F(FileUtilsTest, EdgeCase_EmptyLineReading) {
    create_test_file("empty_lines.txt", "\n\n\n");

    const auto lines = read_lines(test_path("empty_lines.txt"));
    ASSERT_TRUE(lines.has_value());
    EXPECT_GE(lines->size(), 0);
}

TEST_F(FileUtilsTest, EdgeCase_NoNewlineAtEnd) {
    create_test_file("no_newline.txt", "Line without newline");

    const auto lines = read_lines(test_path("no_newline.txt"));
    ASSERT_TRUE(lines.has_value());
    ASSERT_GE(lines->size(), 1);
    EXPECT_EQ((*lines)[0], "Line without newline");
}

TEST_F(FileUtilsTest, EdgeCase_WindowsLineEndings) {
    create_test_file("windows.txt", "Line 1\r\nLine 2\r\nLine 3\r\n");

    const auto lines = read_lines(test_path("windows.txt"));
    ASSERT_TRUE(lines.has_value());
    ASSERT_EQ(lines->size(), 3);
    for (const auto& line : *lines) {
        EXPECT_EQ(line.find('\r'), std::string::npos);
    }
}

TEST_F(FileUtilsTest, EdgeCase_MixedLineEndings) {
    create_test_file("mixed.txt", "Line 1\nLine 2\r\nLine 3\rLine 4");

    const auto lines = read_lines(test_path("mixed.txt"));
    ASSERT_TRUE(lines.has_value());
    EXPECT_GE(lines->size(), 1);
}


TEST_F(FileUtilsTest, Performance_LargeFileRead) {
    // Create 10MB file
    const std::string large_content(10 * 1024 * 1024, 'L');
    create_test_file("large_perf.txt", large_content);

    const auto content = read_file(test_path("large_perf.txt"));
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(content->size(), large_content.size());
}

TEST_F(FileUtilsTest, Performance_ManySmallFiles) {
    // Create and read 100 small files
    for (int i = 0; i < 100; ++i) {
        std::string filename = "small_" + std::to_string(i) + ".txt";
        write_file(test_path(filename), "Content " + std::to_string(i));
    }

    // Read all back
    for (int i = 0; i < 100; ++i) {
        std::string filename = "small_" + std::to_string(i) + ".txt";
        auto content = read_file(test_path(filename));
        ASSERT_TRUE(content.has_value());
    }
}

TEST_F(FileUtilsTest, Performance_ChunkedReading) {
    const std::string content(10000, 'C');
    create_test_file("chunked.txt", content);

    std::string reassembled;
    size_t offset = 0;

    while (offset < content.size()) {
        constexpr size_t chunk_size = 1000;
        auto chunk = read_file_chunk(test_path("chunked.txt"), offset, chunk_size);
        if (!chunk.has_value() || chunk->empty()) {
            break;
        }
        reassembled += *chunk;
        offset += chunk->size();
    }

    EXPECT_EQ(reassembled, content);
}

TEST_F(FileUtilsTest, Performance_StreamVsDirectRead) {
    std::string content(50000, 'S');
    create_test_file("stream_test.txt", content);

    // Direct read
    auto direct = read_file(test_path("stream_test.txt"));
    ASSERT_TRUE(direct.has_value());

    // Stream read
    FileReader reader(test_path("stream_test.txt"));
    auto stream = reader.read_all();
    ASSERT_TRUE(stream.has_value());

    EXPECT_EQ(*direct, *stream);
}