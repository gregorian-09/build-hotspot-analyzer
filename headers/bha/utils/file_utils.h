//
// Created by gregorian on 14/10/2025.
//

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>
#include <optional>

namespace bha::utils
{
    /**
     * Read the entire file at `path` as a string.
     *
     * @param path Path of the file to read.
     * @return An optional containing the file contents if read succeeded, or std::nullopt on failure.
     */
    std::optional<std::string> read_file(std::string_view path);

    /**
     * Read the file at `path` as lines (splitting on newline).
     *
     * @param path Path of the file to read.
     * @return An optional containing a vector of lines if successful, or std::nullopt on error.
     */
    std::optional<std::vector<std::string>> read_lines(std::string_view path);

    /**
     * Write `content` to the file at `path`, replacing any existing content.
     *
     * @param path    Path of the file to write.
     * @param content The text content to write.
     * @return True on success, false on failure.
     */
    bool write_file(std::string_view path, std::string_view content);

    /**
     * Write multiple lines to the file at `path`.
     *
     * @param path  Path of the file to write.
     * @param lines Vector of lines (strings) to write; typically adds newline separators.
     * @return True on success, false on failure.
     */
    bool write_lines(std::string_view path, const std::vector<std::string>& lines);

    /**
     * Append `content` to the end of the file at `path`.
     *
     * @param path    Path of the file to append to.
     * @param content The text content to append.
     * @return True on success, false on failure.
     */
    bool append_to_file(std::string_view path, std::string_view content);

    /**
     * Copy a file from `source` to `destination`.
     *
     * @param source      Path of the source file.
     * @param destination Path of the destination file.
     * @param overwrite   If true, overwrite existing destination file; otherwise fail if exists.
     * @return True on success, false on failure.
     */
    bool copy_file(std::string_view source, std::string_view destination, bool overwrite = false);

    /**
     * Move (rename) a file from `source` to `destination`.
     *
     * @param source      Path of the source file.
     * @param destination New path/location for the file.
     * @return True on success, false on failure.
     */
    bool move_file(std::string_view source, std::string_view destination);

    /**
     * Delete (remove) the file at `path`.
     *
     * @param path Path of the file to delete.
     * @return True if deletion succeeded, false otherwise.
     */
    bool delete_file(std::string_view path);

    /**
     * Check whether a file exists at `path`.
     *
     * @param path Path to test.
     * @return True if a file (or directory) exists at `path`, false if not.
     */
    bool file_exists(std::string_view path);

    /**
     * Get the size (in bytes) of the file at `path`.
     *
     * @param path Path of the file.
     * @return Optional containing the file size if successful, or std::nullopt on error.
     */
    std::optional<std::uintmax_t> get_file_size(std::string_view path);

    /**
     * Get the file extension of `path`.
     *
     * @param path Path string.
     * @return Optional string containing the extension (including dot), or std::nullopt if none.
     */
    std::optional<std::string> get_file_extension(std::string_view path);

    /**
     * Check whether the file at `path` is readable.
     *
     * @param path Path to test.
     * @return True if readable, false otherwise.
     */
    bool is_readable(std::string_view path);

    /**
     * Check whether the file at `path` is writable.
     *
     * @param path Path to test.
     * @return True if writable, false otherwise.
     */
    bool is_writable(std::string_view path);

    /**
     * Read a binary file at `path` into a vector of bytes.
     *
     * @param path Path of the file.
     * @return Optional containing the byte vector if successful, or std::nullopt on failure.
     */
    std::optional<std::vector<char>> read_binary_file(std::string_view path);

    /**
     * Write binary data to file at `path`.
     *
     * @param path Path of the file.
     * @param data Byte vector to write.
     * @return True on success, false on failure.
     */
    bool write_binary_file(std::string_view path, const std::vector<char>& data);

    /**
     * Create an empty file at `path` (if it doesn't already exist).
     *
     * @param path Path of the file to create.
     * @return True on success or if file already exists, false on failure.
     */
    bool create_empty_file(std::string_view path);

    /**
     * Read a chunk of a file starting at offset for up to `size` bytes.
     *
     * @param path   Path of the file.
     * @param offset Byte offset to start reading.
     * @param size   Maximum number of bytes to read.
     * @return Optional containing the chunk as string if successful, or std::nullopt on error or if EOF.
     */
    std::optional<std::string> read_file_chunk(std::string_view path, size_t offset, size_t size);


    /**
     * FileReader: helper class for sequential file reading.
     */
    class FileReader {
    public:
        /**
         * Construct a FileReader for the file at `path`.
         *
         * @param path Path to open for reading.
         */
        explicit FileReader(std::string_view path);

        ~FileReader();

        FileReader(const FileReader&) = delete;
        FileReader& operator=(const FileReader&) = delete;

        FileReader(FileReader&&) noexcept;
        FileReader& operator=(FileReader&&) noexcept;

        /**
         * Check if the file is open.
         *
         * @return True if open, false otherwise.
         */
        bool is_open() const;

        /**
         * Read a single line from the file.
         *
         * @return Optional containing the line (excluding newline) or std::nullopt on EOF / error.
         */
        std::optional<std::string> read_line();

        /**
         * Read the rest of the file as a string.
         *
         * @return Optional containing file contents or std::nullopt on error.
         */
        std::optional<std::string> read_all() const;

        /**
         * Read up to `count` bytes from the file.
         *
         * @param count Maximum bytes to read.
         * @return Optional containing a vector of bytes if successful, or std::nullopt on error.
         */
        std::optional<std::vector<char>> read_bytes(size_t count);

        /**
         * Check if end-of-file has been reached.
         *
         * @return True if at EOF, false otherwise.
         */
        bool eof() const;

        /**
         * Close the file.
         */
        void close();

    private:
        std::ifstream stream_;
    };


    /**
     * FileWriter: helper class for sequential file writing.
     */
    class FileWriter {
    public:
        /**
         * Construct a FileWriter for `path`, optionally in append mode.
         *
         * @param path   Path to open for writing.
         * @param append If true, append to existing file instead of truncating.
         */
        explicit FileWriter(std::string_view path, bool append = false);

        ~FileWriter();

        FileWriter(const FileWriter&) = delete;
        FileWriter& operator=(const FileWriter&) = delete;

        FileWriter(FileWriter&&) noexcept;
        FileWriter& operator=(FileWriter&&) noexcept;

        /**
         * Check if the writer is open.
         *
         * @return True if open, false otherwise.
         */
        bool is_open() const;

        /**
         * Write `content` to the file.
         *
         * @param content Text to write.
         * @return True on success, false on failure.
         */
        bool write(std::string_view content);

        /**
         * Write `line` followed by newline to the file.
         *
         * @param line Text line to write.
         * @return True on success, false on failure.
         */
        bool write_line(std::string_view line);

        /**
         * Flush any buffered output to the file.
         *
         * @return True on success, false on error.
         */
        bool flush();

        /**
         * Close the file.
         */
        void close();

    private:
        std::ofstream stream_;
    };

}

#endif //FILE_UTILS_H
