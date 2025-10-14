//
// Created by gregorian on 14/10/2025.
//

#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <filesystem>
#include <string_view>
#include <vector>
#include <optional>

namespace bha::utils
{
    namespace fs = std::filesystem;

    /**
     * Normalize the components of `path`, resolving `.` and `..` elements.
     *
     * @param path The input path string view.
     * @return A normalized path string, with redundant elements eliminated.
     *
     * The result may or may not be absolute depending on the input. Behavior for invalid paths is implementation-dependent.
     */
    std::string normalize_path(std::string_view path);

    /**
     * Resolve `path` to an absolute path.
     *
     * @param path The input path.
     * @return The absolute path string corresponding to `path`.
     *
     * If `path` is relative, it is resolved against the current working directory.
     * If `path` is already absolute, it is returned (possibly normalized).
     */
    std::string get_absolute_path(std::string_view path);

    /**
     * Compute the relative path from `base` to `path`.
     *
     * @param path The target path.
     * @param base The base directory path.
     * @return A string representing the relative path from `base` to `path`.
     *
     * If no relative path exists (e.g., different root), the behavior is implementation-defined (may return absolute).
     */
    std::string get_relative_path(std::string_view path, std::string_view base);

    /**
     * Extract the filename (last component) from `path`.
     *
     * @param path The input path.
     * @return The filename portion (with extension) of `path`.
     *
     * If `path` ends with a directory separator, returns an empty string or the last element depending on implementation.
     */
    std::string get_filename(std::string_view path);

    /**
     * Extract the stem (filename without extension) from `path`.
     *
     * @param path The input path.
     * @return The stem (filename without extension).
     *
     * If there is no extension, returns the same as get_filename(path).
     */
    std::string get_stem(std::string_view path);

    /**
     * Retrieve the extension (suffix after the last dot) of `path`.
     *
     * @param path The input path.
     * @return The extension including the dot (e.g. “.txt”), or an empty string if none.
     */
    std::string get_extension(std::string_view path);

    /**
     * Get the parent directory of `path`.
     *
     * @param path The input path.
     * @return The parent path string; may be empty if none (e.g. root).
     */
    std::string get_parent_path(std::string_view path);

    /**
     * Join two path components into a single path.
     *
     * @param path1 The first path component.
     * @param path2 The second path component.
     * @return The joined path (with correct separators).
     */
    std::string join_paths(std::string_view path1, std::string_view path2);

    /**
     * Join multiple path components into one path.
     *
     * @tparam Paths Variadic additional path component types.
     * @param first The first path component.
     * @param rest  Other path components.
     * @return The joined path containing all components.
     */
    template<typename... Paths>
    std::string join_paths(std::string_view first, Paths&&... rest) {
        fs::path result(first);
        (result /= ... /= fs::path(rest));
        return result.string();
    }

    /**
     * Check if `path` is absolute.
     *
     * @param path The input path.
     * @return true if `path` is absolute, otherwise false.
     */
    bool is_absolute(std::string_view path);

    /**
     * Check whether a file or directory exists at `path`.
     *
     * @param path The input path.
     * @return true if the path exists, otherwise false.
     */
    bool path_exists(std::string_view path);

    /**
     * Check if `path` refers to a regular file.
     *
     * @param path The input path.
     * @return true if path is an existing file, otherwise false.
     */
    bool is_file(std::string_view path);

    /**
     * Check if `path` refers to a directory.
     *
     * @param path The input path.
     * @return true if path is an existing directory, otherwise false.
     */
    bool is_directory(std::string_view path);

    /**
     * Check whether `path` has the extension `ext`.
     *
     * @param path The input path.
     * @param ext  The extension to check (including or excluding dot, depending on your convention).
     * @return true if `path` ends with that extension, otherwise false.
     */
    bool has_extension(std::string_view path, std::string_view ext);

    /**
     * Replace the extension of `path` with `new_ext`.
     *
     * @param path    The original path.
     * @param new_ext The new extension to use.
     * @return The path string with its extension replaced.
     *
     * If `path` has no extension, `new_ext` is appended.
     */
    std::string replace_extension(std::string_view path, std::string_view new_ext);

    /**
     * Convert path separators in `path` to the native separators (e.g. “\” on Windows).
     *
     * @param path The input path.
     * @return Path string using native separators.
     */
    std::string to_native_separators(std::string_view path);

    /**
     * Convert path separators in `path` to POSIX style (forward slash “/”).
     *
     * @param path The input path.
     * @return Path string using POSIX separators.
     */
    std::string to_posix_separators(std::string_view path);

    /**
     * Check whether `path` is a subdirectory (descendant) of `parent`.
     *
     * @param path   The path to test.
     * @param parent The parent directory path.
     * @return true if `path` is within `parent`, otherwise false.
     */
    bool is_subdirectory_of(std::string_view path, std::string_view parent);

    /**
     * Search for a file named `filename` by traversing upward from `start_dir`.
     *
     * @param start_dir The directory to start searching from.
     * @param filename  The file name to locate.
     * @return An optional string containing the found file path, or std::nullopt if not found.
     */
    std::optional<std::string> find_file_in_parents(std::string_view start_dir, std::string_view filename);

    /**
     * List files in the directory `directory`.
     *
     * @param directory The directory path.
     * @param recursive Whether to list files recursively.
     * @return A vector of file paths.
     *
     * If `recursive` is true, subdirectories are traversed.
     */
    std::vector<std::string> list_files(std::string_view directory, bool recursive = false);

    /**
     * List files in `directory` with a specific `extension`.
     *
     * @param directory The directory to scan.
     * @param extension The file extension to match.
     * @param recursive Whether to scan subdirectories recursively.
     * @return A vector of matching file paths.
     */
    std::vector<std::string> list_files_with_extension(std::string_view directory,
                                                       std::string_view extension,
                                                       bool recursive = false);

    /**
     * Make the path “preferred” (normalize separators, remove redundant parts).
     *
     * @param path The input path.
     * @return A “preferred” version of the path.
     */
    std::string make_preferred(std::string_view path);

    /**
     * Create directories for `path` (if they do not exist).
     *
     * @param path The directory path to create.
     * @return true if successful or already exists, false on error.
     */
    bool create_directories(std::string_view path);

    /**
     * Get the file size at `path`, if it exists.
     *
     * @param path The file path.
     * @return An optional containing the file size, or std::nullopt if path does not exist or is not a file.
     */
    std::optional<uintmax_t> file_size(std::string_view path);

    /**
     * Get the current working directory.
     *
     * @return The string path of the current directory.
     */
    std::string get_current_directory();

    /**
     * Check if two paths refer to the same file (considering symbolic links, canonicalization).
     *
     * @param path1 First path.
     * @param path2 Second path.
     * @return true if they refer to the same file, otherwise false.
     */
    bool is_same_file(std::string_view path1, std::string_view path2);
}

#endif //PATH_UTILS_H
