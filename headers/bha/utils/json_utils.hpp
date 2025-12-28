//
// Created by gregorian-rayne on 12/28/25.
//

#ifndef BUILDTIMEHOTSPOTANALYZER_JSON_UTILS_HPP
#define BUILDTIMEHOTSPOTANALYZER_JSON_UTILS_HPP

/**
 * @file json_utils.hpp
 * @brief JSON serialization utilities.
 *
 * Provides helpers for parsing and serializing JSON data using
 * nlohmann/json. All operations use Result<T, Error> for error handling.
 */

#include "bha/result.hpp"
#include "bha/error.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>

namespace bha::json_utils {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    /**
     * Parses a JSON string.
     *
     * @param content The JSON string to parse.
     * @return The parsed JSON object or an error.
     */
    inline Result<json, Error> parse(std::string_view content) {
        try {
            return Result<json, Error>::success(json::parse(content));
        } catch (const json::parse_error& e) {
            return Result<json, Error>::failure(
                Error::parse_error("JSON parse error", e.what())
            );
        }
    }

    /**
     * Reads and parses a JSON file.
     *
     * @param path Path to the JSON file.
     * @return The parsed JSON object or an error.
     */
    inline Result<json, Error> read_file(const fs::path& path) {
        if (std::error_code ec; !fs::exists(path, ec)) {
            return Result<json, Error>::failure(
                Error::not_found("JSON file not found", path.string())
            );
        }

        std::ifstream file(path);
        if (!file) {
            return Result<json, Error>::failure(
                Error::io_error("Failed to open JSON file", path.string())
            );
        }

        try {
            json data;
            file >> data;
            return Result<json, Error>::success(std::move(data));
        } catch (const json::parse_error& e) {
            return Result<json, Error>::failure(
                Error::parse_error("JSON parse error", path.string() + ": " + e.what())
            );
        }
    }

    /**
     * Writes a JSON object to a file.
     *
     * @param path Path to write to.
     * @param data The JSON data to write.
     * @param indent Indentation level (-1 for compact output).
     * @return Success or an error.
     */
    inline Result<void, Error> write_file(
        const fs::path& path,
        const json& data,
        int indent = 2
    ) {
        auto parent = path.parent_path();
        if (std::error_code ec; !parent.empty() && !fs::exists(parent, ec)) {
            fs::create_directories(parent, ec);
            if (ec) {
                return Result<void, Error>::failure(
                    Error::io_error("Failed to create directory", parent.string())
                );
            }
        }

        std::ofstream file(path);
        if (!file) {
            return Result<void, Error>::failure(
                Error::io_error("Failed to open file for writing", path.string())
            );
        }

        try {
            if (indent >= 0) {
                file << data.dump(indent);
            } else {
                file << data.dump();
            }
        } catch (const json::type_error& e) {
            return Result<void, Error>::failure(
                Error::internal_error("JSON serialization error", e.what())
            );
        }

        if (!file) {
            return Result<void, Error>::failure(
                Error::io_error("Failed to write JSON file", path.string())
            );
        }

        return Result<void, Error>::success();
    }

    /**
     * Serializes a JSON object to a string.
     *
     * @param data The JSON data.
     * @param indent Indentation level (-1 for compact output).
     * @return The JSON string.
     */
    inline std::string to_string(const json& data, const int indent = -1) {
        return data.dump(indent);
    }

    /**
     * Gets a value from a JSON object with a default.
     *
     * @tparam T The type to retrieve.
     * @param obj The JSON object.
     * @param key The key to look up.
     * @param default_value The default value if key is missing.
     * @return The value or the default.
     */
    template<typename T>
    T get_or(const json& obj, const std::string& key, const T& default_value) {
        if (obj.contains(key)) {
            try {
                return obj.at(key).get<T>();
            } catch (...) {
                return default_value;
            }
        }
        return default_value;
    }

    /**
     * Gets a value from a JSON object.
     *
     * @tparam T The type to retrieve.
     * @param obj The JSON object.
     * @param key The key to look up.
     * @return The value or an error.
     */
    template<typename T>
    Result<T, Error> get(const json& obj, const std::string& key) {
        if (!obj.contains(key)) {
            return Result<T, Error>::failure(
                Error::not_found("JSON key not found", key)
            );
        }

        try {
            return Result<T, Error>::success(obj.at(key).get<T>());
        } catch (const json::type_error& e) {
            return Result<T, Error>::failure(
                Error::parse_error("JSON type mismatch", key + ": " + e.what())
            );
        }
    }

    /**
     * Merges two JSON objects.
     *
     * Values from the second object override values in the first.
     *
     * @param base The base object.
     * @param overlay The object to merge on top.
     * @return The merged object.
     */
    inline json merge(const json& base, const json& overlay) {
        json result = base;

        for (auto& [key, value] : overlay.items()) {
            if (result.contains(key) && result[key].is_object() && value.is_object()) {
                result[key] = merge(result[key], value);
            } else {
                result[key] = value;
            }
        }

        return result;
    }

    /**
     * Checks if a JSON value is a valid object.
     */
    inline bool is_object(const json& value) {
        return value.is_object();
    }

    /**
     * Checks if a JSON value is a valid array.
     */
    inline bool is_array(const json& value) {
        return value.is_array();
    }

    /**
     * Checks if a JSON value is a string.
     */
    inline bool is_string(const json& value) {
        return value.is_string();
    }

    /**
     * Checks if a JSON value is a number.
     */
    inline bool is_number(const json& value) {
        return value.is_number();
    }

}  // namespace bha::json_utils

#endif //BUILDTIMEHOTSPOTANALYZER_JSON_UTILS_HPP