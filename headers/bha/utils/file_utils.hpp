//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_FILE_UTILS_HPP
#define BUILDTIMEHOTSPOTANALYZER_FILE_UTILS_HPP

/**
 * @file file_utils.hpp
 * @brief File system utilities.
 *
 * Provides file operations like reading, writing, and querying file
 * properties. All operations use Result<T, Error> for error handling.
 */

#include "bha/result.hpp"
#include "bha/error.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bha::file_utils {

    namespace fs = std::filesystem;

    /**
     * Reads an entire file into a string.
     *
     * @param path Path to the file.
     * @return The file contents or an error.
     */
    inline Result<std::string, Error> read_file(const fs::path& path) {
        if (std::error_code ec; !fs::exists(path, ec)) {
            return Result<std::string, Error>::failure(
                Error::not_found("File not found", path.string())
            );
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return Result<std::string, Error>::failure(
                Error::io_error("Failed to open file", path.string())
            );
        }

        std::ostringstream oss;
        oss << file.rdbuf();

        if (file.bad()) {
            return Result<std::string, Error>::failure(
                Error::io_error("Failed to read file", path.string())
            );
        }

        return Result<std::string, Error>::success(oss.str());
    }

    /**
     * Reads a file line by line.
     *
     * @param path Path to the file.
     * @return Vector of lines or an error.
     */
    inline Result<std::vector<std::string>, Error> read_lines(const fs::path& path) {
        if (std::error_code ec; !fs::exists(path, ec)) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::not_found("File not found", path.string())
            );
        }

        std::ifstream file(path);
        if (!file) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::io_error("Failed to open file", path.string())
            );
        }

        std::vector<std::string> lines;
        std::string line;

        while (std::getline(file, line)) {
            lines.push_back(std::move(line));
        }

        if (file.bad()) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::io_error("Failed to read file", path.string())
            );
        }

        return Result<std::vector<std::string>, Error>::success(std::move(lines));
    }

    /**
     * Writes a string to a file.
     *
     * @param path Path to the file.
     * @param content Content to write.
     * @return Success or an error.
     */
    inline Result<void, Error> write_file(const fs::path& path, std::string_view content) {
        auto parent = path.parent_path();
        if (std::error_code ec; !parent.empty() && !fs::exists(parent, ec)) {
            fs::create_directories(parent, ec);
            if (ec) {
                return Result<void, Error>::failure(
                    Error::io_error("Failed to create directory", parent.string())
                );
            }
        }

        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return Result<void, Error>::failure(
                Error::io_error("Failed to open file for writing", path.string())
            );
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));

        if (!file) {
            return Result<void, Error>::failure(
                Error::io_error("Failed to write file", path.string())
            );
        }

        return Result<void, Error>::success();
    }

    /**
     * Gets the size of a file.
     *
     * @param path Path to the file.
     * @return File size in bytes or an error.
     */
    inline Result<std::uintmax_t, Error> file_size(const fs::path& path) {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);

        if (ec) {
            return Result<std::uintmax_t, Error>::failure(
                Error::io_error("Failed to get file size", path.string())
            );
        }

        return Result<std::uintmax_t, Error>::success(size);
    }

    /**
     * Gets the last modification time of a file.
     *
     * @param path Path to the file.
     * @return Last write time or an error.
     */
    inline Result<fs::file_time_type, Error> last_modified(const fs::path& path) {
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);

        if (ec) {
            return Result<fs::file_time_type, Error>::failure(
                Error::io_error("Failed to get modification time", path.string())
            );
        }

        return Result<fs::file_time_type, Error>::success(time);
    }

    /**
     * Lists files in a directory matching a pattern.
     *
     * @param dir Directory to search.
     * @param extension Optional extension filter (e.g., ".cpp").
     * @param recursive Whether to search subdirectories.
     * @return List of matching paths or an error.
     */
    inline Result<std::vector<fs::path>, Error> list_files(
        const fs::path& dir,
        const std::string_view extension = "",
        const bool recursive = false
    ) {
        std::error_code ec;

        if (!fs::exists(dir, ec)) {
            return Result<std::vector<fs::path>, Error>::failure(
                Error::not_found("Directory not found", dir.string())
            );
        }

        if (!fs::is_directory(dir, ec)) {
            return Result<std::vector<fs::path>, Error>::failure(
                Error::invalid_argument("Not a directory", dir.string())
            );
        }

        std::vector<fs::path> result;

        auto process_entry = [&](const fs::directory_entry& entry) {
            if (entry.is_regular_file()) {
                if (extension.empty() || entry.path().extension() == extension) {
                    result.push_back(entry.path());
                }
            }
        };

        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
                process_entry(entry);
            }
        } else {
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                process_entry(entry);
            }
        }

        if (ec) {
            return Result<std::vector<fs::path>, Error>::failure(
                Error::io_error("Failed to list directory", dir.string())
            );
        }

        return Result<std::vector<fs::path>, Error>::success(std::move(result));
    }

    /**
     * Creates a temporary file and returns its path.
     *
     * @param prefix Optional prefix for the filename.
     * @param extension Optional extension for the file.
     * @return Path to the temporary file or an error.
     */
    inline Result<fs::path, Error> create_temp_file(
        std::string_view prefix = "bha_",
        std::string_view extension = ".tmp"
    ) {
        std::error_code ec;
        auto temp_dir = fs::temp_directory_path(ec);

        if (ec) {
            return Result<fs::path, Error>::failure(
                Error::io_error("Failed to get temp directory")
            );
        }

        auto base = std::string(prefix) + "XXXXXX" + std::string(extension);
        auto temp_path = temp_dir / base;

        if (std::ofstream file(temp_path); !file) {
            return Result<fs::path, Error>::failure(
                Error::io_error("Failed to create temp file")
            );
        }

        return Result<fs::path, Error>::success(temp_path);
    }

    /**
     * Checks if a path is a C++ source file.
     *
     * @param path Path to check.
     * @return True if the file has a C++ source extension.
     */
    inline bool is_cpp_source(const fs::path& path) {
        const auto ext = path.extension().string();
        return ext == ".cpp" || ext == ".cxx" || ext == ".cc" ||
               ext == ".c++" || ext == ".C";
    }

    /**
     * Checks if a path is a C++ header file.
     *
     * @param path Path to check.
     * @return True if the file has a C++ header extension.
     */
    inline bool is_cpp_header(const fs::path& path) {
        const auto ext = path.extension().string();
        return ext == ".h" || ext == ".hpp" || ext == ".hxx" ||
               ext == ".h++" || ext == ".hh" || ext == ".H" || ext.empty();
    }

    /**
     * Checks if a path is a JSON file.
     *
     * @param path Path to check.
     * @return True if the file has a .json extension.
     */
    inline bool is_json_file(const fs::path& path) {
        return path.extension() == ".json";
    }

}  // namespace bha::file_utils

#endif //BUILDTIMEHOTSPOTANALYZER_FILE_UTILS_HPP