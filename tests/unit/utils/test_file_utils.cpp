#include "bha/utils/file_utils.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace bha::utils {
    namespace {

        struct TempDir {
            fs::path path;

            TempDir() {
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                path = fs::temp_directory_path() / ("bha-file-utils-" + std::to_string(stamp));
                fs::create_directories(path);
            }

            ~TempDir() {
                std::error_code ec;
                fs::remove_all(path, ec);
            }
        };

        void write_test_file(const fs::path& path) {
            fs::create_directories(path.parent_path());
            std::ofstream output(path);
            output << "test";
        }

    }  // namespace

    TEST(FileUtilsTest, ListsMatchingFilesWithoutThrowing) {
        TempDir temp;
        write_test_file(temp.path / "root.cpp");
        write_test_file(temp.path / "root.hpp");
        write_test_file(temp.path / "nested" / "child.cpp");

        const auto top_level = list_files(temp.path, ".cpp");
        const auto recursive = list_files(temp.path, ".cpp", true);

        ASSERT_TRUE(top_level.is_ok());
        ASSERT_TRUE(recursive.is_ok());
        ASSERT_EQ(top_level.value().size(), 1u);
        ASSERT_EQ(recursive.value().size(), 2u);
        EXPECT_EQ(top_level.value().front().filename(), "root.cpp");
    }

    TEST(FileUtilsTest, RejectsMissingAndNonDirectoryRoots) {
        TempDir temp;
        write_test_file(temp.path / "file.cpp");

        EXPECT_TRUE(list_files(temp.path / "missing").is_err());
        EXPECT_TRUE(list_files(temp.path / "file.cpp").is_err());
    }

    TEST(FileUtilsTest, CreatesAndNamesTemporaryFiles) {
        const auto result = create_temp_file("bha", ".trace");

        ASSERT_TRUE(result.is_ok());
        EXPECT_TRUE(fs::is_regular_file(result.value()));
        EXPECT_EQ(result.value().extension(), ".trace");

        std::error_code ec;
        EXPECT_TRUE(fs::remove(result.value(), ec));
        EXPECT_FALSE(ec);
    }

}  // namespace bha::utils
