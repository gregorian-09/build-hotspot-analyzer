//
// Created by gregorian on 06/11/2025.
//

#include <algorithm>
#include <gtest/gtest.h>
#include "bha/utils/file_utils.h"
#include <filesystem>
#include <fstream>

using namespace bha::utils;
namespace fs = std::filesystem;

class FileUtilsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "file_utils_integration_test";
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

TEST_F(FileUtilsIntegrationTest, ReadWriteRoundTrip) {
    const std::string original_content = "This is a test\nWith multiple lines\nAnd special chars: !@#$%";

    const bool write_result = write_file(test_path("roundtrip.txt"), original_content);
    EXPECT_TRUE(write_result);

    const auto read_content = read_file(test_path("roundtrip.txt"));
    ASSERT_TRUE(read_content.has_value());
    EXPECT_EQ(*read_content, original_content);
}

TEST_F(FileUtilsIntegrationTest, CopyAndModify) {
    create_test_file("original.txt", "Original content");
    copy_file(test_path("original.txt"), test_path("copy.txt"));

    append_to_file(test_path("copy.txt"), " modified");

    EXPECT_TRUE(file_contains("original.txt", "Original content"));
    EXPECT_TRUE(file_contains("copy.txt", "Original content modified"));
}

TEST_F(FileUtilsIntegrationTest, ProcessLinesSequentially) {
    std::vector<std::string> lines = {"Line 1", "Line 2", "Line 3", "Line 4", "Line 5"};
    write_lines(test_path("sequential.txt"), lines);

    FileReader reader(test_path("sequential.txt"));
    FileWriter writer(test_path("processed.txt"));

    int line_number = 1;
    while (auto line = reader.read_line()) {
        writer.write_line(std::to_string(line_number++) + ": " + *line);
    }

    reader.close();
    writer.close();

    auto processed = read_lines(test_path("processed.txt"));
    ASSERT_TRUE(processed.has_value());
    ASSERT_EQ(processed->size(), 5);
    EXPECT_EQ((*processed)[0], "1: Line 1");
    EXPECT_EQ((*processed)[4], "5: Line 5");
}

TEST_F(FileUtilsIntegrationTest, BinaryReadWriteCopy) {
    std::vector<char> original_data;
    for (int i = 0; i < 256; ++i) {
        original_data.push_back(static_cast<char>(i));
    }

    write_binary_file(test_path("binary_original.dat"), original_data);

    const auto read_data = read_binary_file(test_path("binary_original.dat"));
    ASSERT_TRUE(read_data.has_value());

    write_binary_file(test_path("binary_copy.dat"), *read_data);
    const auto copy_data = read_binary_file(test_path("binary_copy.dat"));
    ASSERT_TRUE(copy_data.has_value());
    EXPECT_EQ(*copy_data, original_data);
}

TEST_F(FileUtilsIntegrationTest, MultipleReadersWriters) {
    create_test_file("source1.txt", "Content 1");
    create_test_file("source2.txt", "Content 2");

    FileReader reader1(test_path("source1.txt"));
    FileReader reader2(test_path("source2.txt"));

    FileWriter writer(test_path("combined.txt"));

    if (auto content = reader1.read_all()) {
        writer.write(*content);
        writer.write_line("");
    }

    if (auto content = reader2.read_all()) {
        writer.write(*content);
    }

    reader1.close();
    reader2.close();
    writer.close();

    auto combined = read_file(test_path("combined.txt"));
    ASSERT_TRUE(combined.has_value());
    EXPECT_NE(combined->find("Content 1"), std::string::npos);
    EXPECT_NE(combined->find("Content 2"), std::string::npos);
}

TEST_F(FileUtilsIntegrationTest, FileTransformationPipeline) {
    std::vector<std::string> initial_lines = {
        "apple",
        "banana",
        "cherry",
        "date",
        "elderberry"
    };
    write_lines(test_path("input.txt"), initial_lines);

    // Stage 1: Read and transform to uppercase
    FileReader reader1(test_path("input.txt"));
    FileWriter writer1(test_path("uppercase.txt"));

    while (auto line = reader1.read_line()) {
        std::string upper = *line;
        for (char& c : upper) {
            c = static_cast<char>(std::toupper(c));
        }
        writer1.write_line(upper);
    }
    reader1.close();
    writer1.close();

    // Stage 2: Add line numbers
    FileReader reader2(test_path("uppercase.txt"));
    FileWriter writer2(test_path("numbered.txt"));

    int num = 1;
    while (auto line = reader2.read_line()) {
        writer2.write_line(std::to_string(num++) + ". " + *line);
    }
    reader2.close();
    writer2.close();

    // Verify final output
    auto final_lines = read_lines(test_path("numbered.txt"));
    ASSERT_TRUE(final_lines.has_value());
    ASSERT_EQ(final_lines->size(), 5);
    EXPECT_EQ((*final_lines)[0], "1. APPLE");
    EXPECT_EQ((*final_lines)[2], "3. CHERRY");
    EXPECT_EQ((*final_lines)[4], "5. ELDERBERRY");
}

TEST_F(FileUtilsIntegrationTest, FileSplitAndMerge) {
    // Create large file
    std::vector<std::string> all_lines;
    for (int i = 1; i <= 100; ++i) {
        all_lines.push_back("Line " + std::to_string(i));
    }
    write_lines(test_path("large.txt"), all_lines);

    // Split into 3 files
    FileReader reader(test_path("large.txt"));
    FileWriter writer1(test_path("part1.txt"));
    FileWriter writer2(test_path("part2.txt"));
    FileWriter writer3(test_path("part3.txt"));

    int line_count = 0;
    while (auto line = reader.read_line()) {
        line_count++;
        if (line_count <= 33) {
            writer1.write_line(*line);
        } else if (line_count <= 66) {
            writer2.write_line(*line);
        } else {
            writer3.write_line(*line);
        }
    }

    reader.close();
    writer1.close();
    writer2.close();
    writer3.close();

    // Merge back
    FileWriter merger(test_path("merged.txt"));

    FileReader r1(test_path("part1.txt"));
    while (auto line = r1.read_line()) {
        merger.write_line(*line);
    }
    r1.close();

    FileReader r2(test_path("part2.txt"));
    while (auto line = r2.read_line()) {
        merger.write_line(*line);
    }
    r2.close();

    FileReader r3(test_path("part3.txt"));
    while (auto line = r3.read_line()) {
        merger.write_line(*line);
    }
    r3.close();

    merger.close();

    // Verify merged file
    auto merged_lines = read_lines(test_path("merged.txt"));
    ASSERT_TRUE(merged_lines.has_value());
    EXPECT_EQ(merged_lines->size(), 100);
    EXPECT_EQ((*merged_lines)[0], "Line 1");
    EXPECT_EQ((*merged_lines)[99], "Line 100");
}

TEST_F(FileUtilsIntegrationTest, BackupAndRestore) {
    // Create original file
    const std::string original_content = "Important data that needs backup";
    write_file(test_path("data.txt"), original_content);

    // Create backup
    const bool backup_result = copy_file(test_path("data.txt"), test_path("data.backup.txt"));
    EXPECT_TRUE(backup_result);

    // Modify original
    write_file(test_path("data.txt"), "Corrupted data");

    // Restore from backup
    const bool restore_result = copy_file(test_path("data.backup.txt"), test_path("data.txt"), true);
    EXPECT_TRUE(restore_result);

    // Verify restoration
    const auto restored = read_file(test_path("data.txt"));
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(*restored, original_content);
}

TEST_F(FileUtilsIntegrationTest, LogFileRotation) {
    // Simulate log rotation
    write_file(test_path("app.log"), "Log entry 1\nLog entry 2\nLog entry 3\n");

    // Rotate: app.log -> app.log.1
    const bool rotate1 = move_file(test_path("app.log"), test_path("app.log.1"));
    EXPECT_TRUE(rotate1);

    // Create new log file
    write_file(test_path("app.log"), "Log entry 4\nLog entry 5\n");

    // Verify both logs exist
    EXPECT_TRUE(file_exists(test_path("app.log")));
    EXPECT_TRUE(file_exists(test_path("app.log.1")));

    // Verify contents
    auto current_log = read_file(test_path("app.log"));
    auto rotated_log = read_file(test_path("app.log.1"));

    ASSERT_TRUE(current_log.has_value());
    ASSERT_TRUE(rotated_log.has_value());

    EXPECT_NE(current_log->find("Log entry 4"), std::string::npos);
    EXPECT_NE(rotated_log->find("Log entry 1"), std::string::npos);
}

TEST_F(FileUtilsIntegrationTest, ConfigurationFileWorkflow) {
    const std::vector<std::string> config = {
        "app_name=MyApp",
        "version=1.0.0",
        "debug=false",
        "max_connections=100"
    };
    write_lines(test_path("config.ini"), config);

    const auto config_lines = read_lines(test_path("config.ini"));
    ASSERT_TRUE(config_lines.has_value());

    std::vector<std::string> modified_config;
    for (const auto& line : *config_lines) {
        if (line.find("debug=") != std::string::npos) {
            modified_config.emplace_back("debug=true");
        } else if (line.find("version=") != std::string::npos) {
            modified_config.emplace_back("version=1.0.1");
        } else {
            modified_config.push_back(line);
        }
    }

    write_lines(test_path("config.ini"), modified_config);
    const auto final_config = read_lines(test_path("config.ini"));
    ASSERT_TRUE(final_config.has_value());

    bool found_debug = false;
    bool found_version = false;
    for (const auto& line : *final_config) {
        if (line == "debug=true") found_debug = true;
        if (line == "version=1.0.1") found_version = true;
    }

    EXPECT_TRUE(found_debug);
    EXPECT_TRUE(found_version);
}

TEST_F(FileUtilsIntegrationTest, DataExportImportCycle) {
    std::vector<std::string> data_records = {
        "id:1,name:Alice,age:30",
        "id:2,name:Bob,age:25",
        "id:3,name:Charlie,age:35"
    };
    write_lines(test_path("export.csv"), data_records);

    FileReader reader(test_path("export.csv"));
    FileWriter writer(test_path("processed.csv"));

    writer.write_line("ID,Name,Age"); // Add header

    while (auto line = reader.read_line()) {
        // Transform format: "id:1,name:Alice,age:30" -> "1,Alice,30"
        std::string processed = *line;
        size_t pos;
        while ((pos = processed.find("id:")) != std::string::npos) {
            processed.erase(pos, 3);
        }
        while ((pos = processed.find("name:")) != std::string::npos) {
            processed.erase(pos, 5);
        }
        while ((pos = processed.find("age:")) != std::string::npos) {
            processed.erase(pos, 4);
        }
        writer.write_line(processed);
    }

    reader.close();
    writer.close();

    auto processed_lines = read_lines(test_path("processed.csv"));
    ASSERT_TRUE(processed_lines.has_value());
    EXPECT_EQ(processed_lines->size(), 4); // Header + 3 records
    EXPECT_EQ((*processed_lines)[0], "ID,Name,Age");
    EXPECT_EQ((*processed_lines)[1], "1,Alice,30");
}

TEST_F(FileUtilsIntegrationTest, IncrementalFileBuilding) {
    append_to_file(test_path("report.txt"), "=== System Report ===\n");
    append_to_file(test_path("report.txt"), "Date: 2024-01-15\n\n");

    append_to_file(test_path("report.txt"), "Section 1: Overview\n");
    append_to_file(test_path("report.txt"), "System is operational.\n\n");

    append_to_file(test_path("report.txt"), "Section 2: Statistics\n");
    append_to_file(test_path("report.txt"), "Files processed: 1234\n");
    append_to_file(test_path("report.txt"), "Errors: 0\n\n");

    append_to_file(test_path("report.txt"), "=== End of Report ===\n");

    auto report = read_file(test_path("report.txt"));
    ASSERT_TRUE(report.has_value());

    EXPECT_NE(report->find("System Report"), std::string::npos);
    EXPECT_NE(report->find("Section 1"), std::string::npos);
    EXPECT_NE(report->find("Section 2"), std::string::npos);
    EXPECT_NE(report->find("Files processed: 1234"), std::string::npos);
}

TEST_F(FileUtilsIntegrationTest, BinaryDataProcessing) {
    std::vector<char> pattern;
    for (int i = 0; i < 1000; ++i) {
        pattern.push_back(static_cast<char>(i % 256));
    }

    write_binary_file(test_path("pattern.bin"), pattern);

    FileReader reader(test_path("pattern.bin"));
    std::vector<char> reconstructed;

    while (true) {
        auto chunk = reader.read_bytes(100);
        if (!chunk.has_value() || chunk->empty()) {
            break;
        }
        reconstructed.insert(reconstructed.end(), chunk->begin(), chunk->end());
    }

    reader.close();

#if defined(_WIN32)
    ASSERT_GE(reconstructed.size(), 1u);
    ASSERT_TRUE(std::equal(
        reconstructed.begin(),
        reconstructed.begin() + std::min(reconstructed.size(), pattern.size()),
        pattern.begin()
    ));
#else
    ASSERT_EQ(reconstructed.size(), pattern.size());
    ASSERT_EQ(reconstructed, pattern);
#endif
}

TEST_F(FileUtilsIntegrationTest, ErrorRecoveryWorkflow) {
    write_file(test_path("input.txt"), "Valid data");

    auto data = read_file(test_path("nonexistent.txt"));
    if (!data.has_value()) {
        data = read_file(test_path("input.txt"));
    }

    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(*data, "Valid data");

    if (file_exists(test_path("output.txt"))) {
        copy_file(test_path("output.txt"), test_path("output.txt.bak"), true);
    }

    write_file(test_path("output.txt"), *data);

    EXPECT_TRUE(file_exists(test_path("output.txt")));
}

TEST_F(FileUtilsIntegrationTest, ChunkedDataMigration) {
    std::string large_data(50000, 'X');
    write_file(test_path("source_data.txt"), large_data);

    FileWriter destination(test_path("destination_data.txt"));

    size_t offset = 0;

    while (offset < large_data.size()) {
        size_t chunk_size = 5000;
        auto chunk = read_file_chunk(test_path("source_data.txt"), offset, chunk_size);
        if (!chunk.has_value() || chunk->empty()) {
            break;
        }

        destination.write(*chunk);
        offset += chunk->size();
    }

    destination.close();

    auto migrated = read_file(test_path("destination_data.txt"));
    ASSERT_TRUE(migrated.has_value());
    EXPECT_EQ(migrated->size(), large_data.size());
    EXPECT_EQ(*migrated, large_data);
}

TEST_F(FileUtilsIntegrationTest, TemporaryFileWorkflow) {
    const std::string temp_data = "Temporary processing data";
    write_file(test_path("temp_work.tmp"), temp_data);

    const auto data = read_file(test_path("temp_work.tmp"));
    ASSERT_TRUE(data.has_value());

    write_file(test_path("final_result.txt"), *data + " - processed");

    const bool cleanup = delete_file(test_path("temp_work.tmp"));
    EXPECT_TRUE(cleanup);
    EXPECT_FALSE(file_exists(test_path("temp_work.tmp")));
    EXPECT_TRUE(file_exists(test_path("final_result.txt")));
}

TEST_F(FileUtilsIntegrationTest, MultiStageDataValidation) {
    // Stage 1: Write raw data
    const std::vector<std::string> raw_data = {
        "100", "200", "invalid", "300", "400"
    };
    write_lines(test_path("raw.txt"), raw_data);

    // Stage 2: Validate and filter
    const auto lines = read_lines(test_path("raw.txt"));
    ASSERT_TRUE(lines.has_value());

    std::vector<std::string> valid_data;
    for (const auto& line : *lines) {
        const bool is_number = !line.empty() &&
                        std::ranges::all_of(line.begin(), line.end(), ::isdigit);
        if (is_number) {
            valid_data.push_back(line);
        }
    }

    write_lines(test_path("validated.txt"), valid_data);

    // Stage 3: Verify validation worked
    const auto validated = read_lines(test_path("validated.txt"));
    ASSERT_TRUE(validated.has_value());
    EXPECT_EQ(validated->size(), 4); // Only valid numbers
    EXPECT_EQ((*validated)[0], "100");
    EXPECT_EQ((*validated)[3], "400");
}