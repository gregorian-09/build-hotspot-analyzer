//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_PATH_UTILS_HPP
#define BUILDTIMEHOTSPOTANALYZER_PATH_UTILS_HPP

/**
 * @file path_utils.hpp
 * @brief Path manipulation utilities.
 *
 * Provides utilities for normalizing, comparing, and manipulating
 * file system paths.
 */

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bha::path_utils {

    namespace fs = std::filesystem;

    /**
     * Normalizes a path by resolving . and .. components.
     *
     * Unlike fs::canonical(), this works on paths that don't exist
     * and doesn't resolve symlinks.
     *
     * @param path The path to normalize.
     * @return The normalized path.
     */
    inline fs::path normalize(const fs::path& path) {
        fs::path result;

        for (const auto& component : path) {
            if (component == ".") {
                continue;
            }
            if (component == "..") {
                if (!result.empty() && result.filename() != "..") {
                    result = result.parent_path();
                } else {
                    result /= component;
                }
            } else {
                result /= component;
            }
        }

        return result.empty() ? "." : result;
    }

    /**
     * Makes a path relative to a base directory.
     *
     * @param path The path to make relative.
     * @param base The base directory.
     * @return The relative path, or the original if not possible.
     */
    inline fs::path make_relative(const fs::path& path, const fs::path& base) {
        std::error_code ec;
        auto result = fs::relative(path, base, ec);

        if (ec || result.empty()) {
            return path;
        }

        return result;
    }

    /**
     * Gets the common ancestor of multiple paths.
     *
     * @param paths The paths to find the common ancestor of.
     * @return The common ancestor path.
     */
    inline fs::path common_ancestor(const std::vector<fs::path>& paths) {
        if (paths.empty()) {
            return {};
        }

        if (paths.size() == 1) {
            return paths[0].parent_path();
        }

        fs::path result = normalize(paths[0]).parent_path();

        for (std::size_t i = 1; i < paths.size(); ++i) {
            auto normalized = normalize(paths[i]).parent_path();

            while (!result.empty()) {
                if (auto rel = make_relative(normalized, result); !rel.empty() && rel.native()[0] != '.') {
                    break;
                }
                if (result == result.root_path()) {
                    break;
                }
                result = result.parent_path();
            }
        }

        return result;
    }

    /**
     * Checks if a path is under a base directory.
     *
     * @param path The path to check.
     * @param base The base directory.
     * @return True if path is under base.
     */
    inline bool is_under(const fs::path& path, const fs::path& base) {
        const auto path_normalized = normalize(path);
        const auto base_normalized = normalize(base);

        const auto rel = make_relative(path_normalized, base_normalized);
        if (rel.empty()) {
            return false;
        }

        const auto rel_str = rel.string();
        return !rel_str.empty() && rel_str[0] != '.' && rel_str.find("..") == std::string::npos;
    }

    /**
     * Replaces the extension of a path.
     *
     * @param path The original path.
     * @param new_extension The new extension (with or without leading dot).
     * @return The path with the new extension.
     */
    inline fs::path replace_extension(const fs::path& path, std::string_view new_extension) {
        fs::path result = path;
        if (!new_extension.empty() && new_extension[0] != '.') {
            result.replace_extension(std::string(".") + std::string(new_extension));
        } else {
            result.replace_extension(new_extension);
        }
        return result;
    }

    /**
     * Gets the stem (filename without extension) of a path.
     *
     * @param path The path.
     * @return The stem.
     */
    inline std::string stem(const fs::path& path) {
        return path.stem().string();
    }

    /**
     * Joins path components.
     *
     * @param parts The path components.
     * @return The joined path.
     */
    inline fs::path join(const std::vector<std::string>& parts) {
        fs::path result;
        for (const auto& part : parts) {
            result /= part;
        }
        return result;
    }

    /**
     * Splits a path into its components.
     *
     * @param path The path to split.
     * @return Vector of path components.
     */
    inline std::vector<std::string> split(const fs::path& path) {
        std::vector<std::string> components;
        for (const auto& component : path) {
            if (auto str = component.string(); !str.empty()) {
                components.push_back(str);
            }
        }
        return components;
    }

    /**
     * Converts a path to use forward slashes (for cross-platform consistency).
     *
     * @param path The path to convert.
     * @return The path with forward slashes.
     */
    inline std::string to_forward_slashes(const fs::path& path) {
        std::string result = path.string();
        for (char& c : result) {
            if (c == '\\') {
                c = '/';
            }
        }
        return result;
    }

    /**
     * Gets the depth of a path (number of directory components).
     *
     * @param path The path to measure.
     * @return The depth.
     */
    inline std::size_t depth(const fs::path& path) {
        std::size_t count = 0;
        for (const auto& component : path) {
            if (auto str = component.string(); !str.empty() && str != "/" && str != "\\") {
                ++count;
            }
        }
        return count;
    }

    /**
     * Checks if two paths refer to the same file.
     *
     * @param path1 First path.
     * @param path2 Second path.
     * @return True if paths are equivalent.
     */
    inline bool equivalent(const fs::path& path1, const fs::path& path2) {
        std::error_code ec;
        return fs::equivalent(path1, path2, ec);
    }

}  // namespace bha::path_utils

#endif //BUILDTIMEHOTSPOTANALYZER_PATH_UTILS_HPP