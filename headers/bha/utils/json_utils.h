//
// Created by gregorian on 14/10/2025.
//

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <simdjson.h>

namespace bha::utils
{
    /**
     * @class JsonDocument
     * Represents a JSON document for parsing and querying.
     *
     * This class provides methods to parse JSON strings and files, query values by key,
     * and check the validity and structure of the JSON document.
     */
    class JsonDocument {
    public:
        /**
         * Default constructor.
         *
         * Initializes an empty JsonDocument.
         */
        JsonDocument();

        /**
         * Destructor.
         *
         * Cleans up resources used by the JsonDocument.
         */
        ~JsonDocument();

        /**
         * Deleted copy constructor.
         *
         * Prevents copying of JsonDocument instances.
         */
        JsonDocument(const JsonDocument&) = delete;

        /**
         * Deleted copy assignment operator.
         *
         * Prevents assignment of JsonDocument instances.
         */
        JsonDocument& operator=(const JsonDocument&) = delete;

        /**
         * Move constructor.
         *
         * Transfers ownership of resources from another JsonDocument.
         */
        JsonDocument(JsonDocument&&) noexcept;

        /**
         * Move assignment operator.
         *
         * Transfers ownership of resources from another JsonDocument.
         *
         * @return Reference to this JsonDocument.
         */
        JsonDocument& operator=(JsonDocument&&) noexcept;

        /**
         * Parses a JSON string.
         *
         * Attempts to parse the provided JSON string.
         *
         * @param json The JSON string to parse.
         * @return True if parsing is successful, false otherwise.
         */
        bool parse(std::string_view json);

        /**
         * Parses a JSON file.
         *
         * Attempts to parse the JSON content from the specified file.
         *
         * @param path The path to the JSON file.
         * @return True if parsing is successful, false otherwise.
         */
        bool parse_file(std::string_view path);

        /**
         * Checks if the document is valid.
         *
         * @return True if the document is valid, false otherwise.
         */
        [[nodiscard]] bool is_valid() const;

        /**
         * Retrieves a string value by key.
         *
         * @param key The key to look up.
         * @return An optional containing the string value if found, otherwise std::nullopt.
         */
        std::optional<std::string> get_string(std::string_view key);

        /**
         * Retrieves an integer value by key.
         *
         * @param key The key to look up.
         * @return An optional containing the integer value if found, otherwise std::nullopt.
         */
        std::optional<int64_t> get_int(std::string_view key);

        /**
         * Retrieves a double value by key.
         *
         * @param key The key to look up.
         * @return An optional containing the double value if found, otherwise std::nullopt.
         */
        std::optional<double> get_double(std::string_view key);

        /**
         * Retrieves a boolean value by key.
         *
         * @param key The key to look up.
         * @return An optional containing the boolean value if found, otherwise std::nullopt.
         */
        std::optional<bool> get_bool(std::string_view key);

        /**
         * Checks if the document contains a specific key.
         *
         * @param key The key to check for.
         * @return True if the key exists, false otherwise.
         */
        bool has_key(std::string_view key);

        /**
         * Checks if the document is an array.
         *
         * @return True if the document is an array, false otherwise.
         */
        bool is_array();

        /**
         * Checks if the document is an object.
         *
         * @return True if the document is an object, false otherwise.
         */
        bool is_object();

        /**
         * Retrieves the size of the array.
         *
         * @return The number of elements in the array.
         */
        size_t array_size();

        /**
         * Retrieves the underlying simdjson document.
         *
         * @return A reference to the simdjson::ondemand::document.
         */
        simdjson::ondemand::document& get_document();

        /**
         * Retrieves the underlying simdjson document.
         *
         * @return A const reference to the simdjson::ondemand::document.
         */
        [[nodiscard]] const simdjson::ondemand::document& get_document() const;

    private:
        simdjson::ondemand::parser parser_; ///< The JSON parser.
        simdjson::padded_string json_data_; ///< The padded JSON string.
        std::optional<simdjson::ondemand::document> doc_; ///< The parsed JSON document.
    };

    /**
     * Parses a JSON string into a string value.
     *
     * @param json The JSON string to parse.
     * @return An optional containing the string value if parsing is successful, otherwise std::nullopt.
     */
    std::optional<std::string> parse_json_string(std::string_view json);

    /**
     * Parses a JSON string into an integer value.
     *
     * @param json The JSON string to parse.
     * @return An optional containing the integer value if parsing is successful, otherwise std::nullopt.
     */
    std::optional<int64_t> parse_json_int(std::string_view json);

    /**
     * Parses a JSON string into a double value.
     *
     * @param json The JSON string to parse.
     * @return An optional containing the double value if parsing is successful, otherwise std::nullopt.
     */
    std::optional<double> parse_json_double(std::string_view json);

    /**
     * Parses a JSON string into a boolean value.
     *
     * @param json The JSON string to parse.
     * @return An optional containing the boolean value if parsing is successful, otherwise std::nullopt.
     */
    std::optional<bool> parse_json_bool(std::string_view json);

    /**
     * Checks if a JSON string is valid.
     *
     * @param json The JSON string to check.
     * @return True if the JSON string is valid, false otherwise.
     */
    bool is_valid_json(std::string_view json);

    /**
     * Retrieves a value from a JSON string by key.
     *
     * @param json The JSON string to search.
     * @param key The key to look up.
     * @return An optional containing the value if found, otherwise std::nullopt.
     */
    std::optional<std::string> get_json_value(std::string_view json, std::string_view key);

    /**
     * Escapes a string for inclusion in a JSON document.
     *
     * @param str The string to escape.
     * @return The escaped string.
     */
    std::string json_escape(std::string_view str);

    /**
     * Unescapes a JSON-encoded string.
     *
     * @param str The JSON-encoded string to unescape.
     * @return The unescaped string.
     */
    std::string json_unescape(std::string_view str);

    /**
     * Converts a string to its JSON representation.
     *
     * @param str The string to convert.
     * @return The JSON representation of the string.
     */
    std::string to_json_string(std::string_view str);

    /**
     * Converts a double value to its JSON representation.
     *
     * @param value The double value to convert.
     * @return The JSON representation of the double value.
     */
    std::string to_json_number(double value);

    /**
     * Converts an integer value to its JSON representation.
     *
     * @param value The integer value to convert.
     * @return The JSON representation of the integer value.
     */
    std::string to_json_number(int64_t value);

    /**
     * Converts a boolean value to its JSON representation.
     *
     * @param value The boolean value to convert.
     * @return The JSON representation of the boolean value.
     */
    std::string to_json_bool(bool value);

    /**
     * Converts a null value to its JSON representation.
     *
     * @return The JSON representation of a null value.
     */
    std::string to_json_null();

    /**
     * Converts a vector of strings to a JSON array representation.
     *
     * @param values The vector of strings to convert.
     * @return The JSON array representation of the strings.
     */
    std::string to_json_array(const std::vector<std::string>& values);

    /**
     * Formats a JSON string with indentation.
     *
     * @param json The JSON string to format.
     * @param indent The number of spaces to use for indentation.
     * @return The formatted JSON string.
     */
    std::string format_json(std::string_view json, int indent = 2);

    /**
     * Minifies a JSON string.
     *
     * @param json The JSON string to minify.
     * @return The minified JSON string.
     */
    std::string minify_json(std::string_view json);

    /**
     * Serializes a value to a JSON string.
     *
     * @param value The value to serialize.
     * @return The JSON string representation of the value.
     */
    template<typename T>
    std::string serialize_to_json(const T& value);

    /**
     * Deserializes a JSON string into a value.
     *
     * @param json The JSON string to deserialize.
     * @return An optional containing the deserialized value if successful, otherwise std::nullopt.
     */
    template<typename T>
    std::optional<T> deserialize_from_json(std::string_view json);
}

#endif //JSON_UTILS_H
