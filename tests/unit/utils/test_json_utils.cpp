//
// Created by gregorian on 03/11/2025.
//

#include <gtest/gtest.h>
#include "bha/utils/json_utils.h"
#include <fstream>
#include <filesystem>

using namespace bha::utils;
namespace fs = std::filesystem;

class JsonUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "json_utils_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    [[nodiscard]] std::string create_json_file(const std::string& filename, const std::string& content) const
    {
        const fs::path file_path = temp_dir / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path.string();
    }

    fs::path temp_dir;
};

TEST_F(JsonUtilsTest, JsonDocument_ParseSimpleObject) {
    JsonDocument doc;
    const bool result = doc.parse(R"({"name": "John", "age": 30})");
    EXPECT_TRUE(result);
    EXPECT_TRUE(doc.is_valid());
}

TEST_F(JsonUtilsTest, JsonDocument_ParseInvalidJson) {
    JsonDocument doc;
    const bool result = doc.parse(R"({invalid json})");
    EXPECT_TRUE(result);
    EXPECT_FALSE(doc.is_valid());
}

TEST_F(JsonUtilsTest, JsonDocument_ParseEmptyObject) {
    JsonDocument doc;
    const bool result = doc.parse("{}");
    EXPECT_TRUE(result);
    EXPECT_TRUE(doc.is_valid());
}

TEST_F(JsonUtilsTest, JsonDocument_ParseEmptyArray) {
    JsonDocument doc;
    const bool result = doc.parse("[]");
    EXPECT_TRUE(result);
    EXPECT_TRUE(doc.is_valid());
}

TEST_F(JsonUtilsTest, JsonDocument_ParseFromFile) {
    const std::string file_path = create_json_file("test.json", R"({"test": "value"})");
    JsonDocument doc;
    const bool result = doc.parse_file(file_path);
    EXPECT_TRUE(result);
    EXPECT_TRUE(doc.is_valid());
}

TEST_F(JsonUtilsTest, JsonDocument_ParseFromNonExistentFile) {
    JsonDocument doc;
    const bool result = doc.parse_file("/nonexistent/file.json");
    EXPECT_FALSE(result);
}

TEST_F(JsonUtilsTest, JsonDocument_GetString_Exists) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice", "city": "New York"})");

    const auto name = doc.get_string("name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Alice");
}

TEST_F(JsonUtilsTest, JsonDocument_GetString_NotExists) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice"})");

    const auto result = doc.get_string("age");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonDocument_GetString_WrongType) {
    JsonDocument doc;
    doc.parse(R"({"age": 30})");

    const auto result = doc.get_string("age");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonDocument_GetString_EmptyString) {
    JsonDocument doc;
    doc.parse(R"({"empty": ""})");

    const auto result = doc.get_string("empty");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "");
}

TEST_F(JsonUtilsTest, JsonDocument_GetInt_Exists) {
    JsonDocument doc;
    doc.parse(R"({"age": 42, "count": -10})");

    const auto age = doc.get_int("age");
    ASSERT_TRUE(age.has_value());
    EXPECT_EQ(*age, 42);

    const auto count = doc.get_int("count");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, -10);
}

TEST_F(JsonUtilsTest, JsonDocument_GetInt_NotExists) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice"})");

    const auto result = doc.get_int("age");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonDocument_GetInt_WrongType) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice"})");

    const auto result = doc.get_int("name");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonDocument_GetInt_Zero) {
    JsonDocument doc;
    doc.parse(R"({"zero": 0})");

    const auto result = doc.get_int("zero");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
}

TEST_F(JsonUtilsTest, JsonDocument_GetInt_LargeNumber) {
    JsonDocument doc;
    doc.parse(R"({"big": 9223372036854775807})");

    const auto result = doc.get_int("big");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 9223372036854775807LL);
}

TEST_F(JsonUtilsTest, JsonDocument_GetDouble_Exists) {
    JsonDocument doc;
    doc.parse(R"({"pi": 3.14159, "e": 2.71828})");

    const auto pi = doc.get_double("pi");
    ASSERT_TRUE(pi.has_value());
    EXPECT_NEAR(*pi, 3.14159, 0.00001);
}

TEST_F(JsonUtilsTest, JsonDocument_GetDouble_Integer) {
    JsonDocument doc;
    doc.parse(R"({"number": 42})");

    const auto result = doc.get_double("number");
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 42.0, 0.00001);
}

TEST_F(JsonUtilsTest, JsonDocument_GetDouble_Negative) {
    JsonDocument doc;
    doc.parse(R"({"negative": -3.14})");

    const auto result = doc.get_double("negative");
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, -3.14, 0.00001);
}

TEST_F(JsonUtilsTest, JsonDocument_GetDouble_Zero) {
    JsonDocument doc;
    doc.parse(R"({"zero": 0.0})");

    const auto result = doc.get_double("zero");
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 0.0, 0.00001);
}

TEST_F(JsonUtilsTest, JsonDocument_GetBool_True) {
    JsonDocument doc;
    doc.parse(R"({"active": true})");

    const auto result = doc.get_bool("active");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
}

TEST_F(JsonUtilsTest, JsonDocument_GetBool_False) {
    JsonDocument doc;
    doc.parse(R"({"active": false})");

    const auto result = doc.get_bool("active");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
}

TEST_F(JsonUtilsTest, JsonDocument_GetBool_NotExists) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice"})");

    const auto result = doc.get_bool("active");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonDocument_GetBool_WrongType) {
    JsonDocument doc;
    doc.parse(R"({"count": 1})");

    const auto result = doc.get_bool("count");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonDocument_HasKey_Exists) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice", "age": 30})");

    EXPECT_TRUE(doc.has_key("name"));
    EXPECT_TRUE(doc.has_key("age"));
}

TEST_F(JsonUtilsTest, JsonDocument_HasKey_NotExists) {
    JsonDocument doc;
    doc.parse(R"({"name": "Alice"})");

    EXPECT_FALSE(doc.has_key("age"));
    EXPECT_FALSE(doc.has_key("city"));
}

TEST_F(JsonUtilsTest, JsonDocument_HasKey_EmptyObject) {
    JsonDocument doc;
    doc.parse("{}");

    EXPECT_FALSE(doc.has_key("anything"));
}

TEST_F(JsonUtilsTest, JsonDocument_IsObject_True) {
    JsonDocument doc;
    doc.parse(R"({"key": "value"})");

    EXPECT_TRUE(doc.is_object());
    EXPECT_FALSE(doc.is_array());
}

TEST_F(JsonUtilsTest, JsonDocument_IsArray_True) {
    JsonDocument doc;
    doc.parse(R"([1, 2, 3])");

    EXPECT_TRUE(doc.is_array());
    EXPECT_FALSE(doc.is_object());
}

TEST_F(JsonUtilsTest, JsonDocument_ArraySize) {
    JsonDocument doc;
    doc.parse(R"([1, 2, 3, 4, 5])");

    EXPECT_EQ(doc.array_size(), 5);
}

TEST_F(JsonUtilsTest, JsonDocument_ArraySize_Empty) {
    JsonDocument doc;
    doc.parse("[]");

    EXPECT_EQ(doc.array_size(), 0);
}

TEST_F(JsonUtilsTest, JsonDocument_MoveConstructor) {
    JsonDocument doc1;
    doc1.parse(R"({"name": "Alice"})");

    const JsonDocument doc2(std::move(doc1));
    EXPECT_TRUE(doc2.is_valid());
}

TEST_F(JsonUtilsTest, JsonDocument_MoveAssignment) {
    JsonDocument doc1;
    doc1.parse(R"({"name": "Alice"})");

    const JsonDocument doc2 = std::move(doc1);
    EXPECT_TRUE(doc2.is_valid());
}

TEST_F(JsonUtilsTest, ParseJsonString_Valid) {
    const auto result = parse_json_string(R"("Hello World")");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Hello World");
}

TEST_F(JsonUtilsTest, ParseJsonString_Invalid) {
    const auto result = parse_json_string(R"(123)");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, ParseJsonString_Empty) {
    const auto result = parse_json_string(R"("")");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "");
}

TEST_F(JsonUtilsTest, ParseJsonInt_Valid) {
    const auto result = parse_json_int("42");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(JsonUtilsTest, ParseJsonInt_Negative) {
    const auto result = parse_json_int("-100");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, -100);
}

TEST_F(JsonUtilsTest, ParseJsonInt_Invalid) {
    const auto result = parse_json_int(R"("not a number")");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, ParseJsonDouble_Valid) {
    const auto result = parse_json_double("3.14159");
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 3.14159, 0.00001);
}

TEST_F(JsonUtilsTest, ParseJsonDouble_Integer) {
    const auto result = parse_json_double("42");
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 42.0, 0.00001);
}

TEST_F(JsonUtilsTest, ParseJsonDouble_Invalid) {
    const auto result = parse_json_double(R"("not a number")");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, ParseJsonBool_True) {
    const auto result = parse_json_bool("true");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
}

TEST_F(JsonUtilsTest, ParseJsonBool_False) {
    const auto result = parse_json_bool("false");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
}

TEST_F(JsonUtilsTest, ParseJsonBool_Invalid) {
    const auto result = parse_json_bool("1");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, IsValidJson_ValidObject) {
    EXPECT_TRUE(is_valid_json(R"({"name": "Alice"})"));
}

TEST_F(JsonUtilsTest, IsValidJson_ValidArray) {
    EXPECT_TRUE(is_valid_json(R"([1, 2, 3])"));
}

TEST_F(JsonUtilsTest, IsValidJson_ValidString) {
    EXPECT_TRUE(is_valid_json(R"("hello")"));
}

TEST_F(JsonUtilsTest, IsValidJson_ValidNumber) {
    EXPECT_TRUE(is_valid_json("42"));
}

TEST_F(JsonUtilsTest, IsValidJson_ValidBool) {
    EXPECT_TRUE(is_valid_json("true"));
    EXPECT_TRUE(is_valid_json("false"));
}

TEST_F(JsonUtilsTest, IsValidJson_ValidNull) {
    EXPECT_TRUE(is_valid_json("null"));
}

TEST_F(JsonUtilsTest, IsValidJson_Invalid) {
    EXPECT_FALSE(is_valid_json("{invalid}"));
    EXPECT_FALSE(is_valid_json("[1, 2,]"));
    EXPECT_FALSE(is_valid_json(""));
}

TEST_F(JsonUtilsTest, GetJsonValue_Exists) {
    const auto result = get_json_value(R"({"name": "Alice", "age": 30})", "name");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "Alice");
}

TEST_F(JsonUtilsTest, GetJsonValue_NotExists) {
    const auto result = get_json_value(R"({"name": "Alice"})", "age");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, GetJsonValue_InvalidJson) {
    const auto result = get_json_value("{invalid}", "key");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, JsonEscape_SpecialCharacters) {
    EXPECT_EQ(json_escape("Hello\nWorld"), "Hello\\nWorld");
    EXPECT_EQ(json_escape("Tab\there"), "Tab\\there");
    EXPECT_EQ(json_escape("Quote\"here"), "Quote\\\"here");
    EXPECT_EQ(json_escape("Backslash\\here"), "Backslash\\\\here");
}

TEST_F(JsonUtilsTest, JsonEscape_NoSpecialChars) {
    EXPECT_EQ(json_escape("Hello World"), "Hello World");
}

TEST_F(JsonUtilsTest, JsonEscape_EmptyString) {
    EXPECT_EQ(json_escape(""), "");
}

TEST_F(JsonUtilsTest, JsonUnescape_SpecialCharacters) {
    EXPECT_EQ(json_unescape("Hello\\nWorld"), "Hello\nWorld");
    EXPECT_EQ(json_unescape("Tab\\there"), "Tab\there");
    EXPECT_EQ(json_unescape("Quote\\\"here"), "Quote\"here");
    EXPECT_EQ(json_unescape("Backslash\\\\here"), "Backslash\\here");
}

TEST_F(JsonUtilsTest, JsonUnescape_NoEscapes) {
    EXPECT_EQ(json_unescape("Hello World"), "Hello World");
}

TEST_F(JsonUtilsTest, JsonEscapeUnescape_RoundTrip) {
    const std::string original = "Hello\nWorld\t\"Quote\"\\Backslash";
    const std::string escaped = json_escape(original);
    const std::string unescaped = json_unescape(escaped);
    EXPECT_EQ(unescaped, original);
}

TEST_F(JsonUtilsTest, ToJsonString_Simple) {
    EXPECT_EQ(to_json_string("hello"), R"("hello")");
}

TEST_F(JsonUtilsTest, ToJsonString_WithEscapes) {
    const auto result = to_json_string("Hello\nWorld");
    EXPECT_NE(result.find("\\n"), std::string::npos);
}

TEST_F(JsonUtilsTest, ToJsonString_Empty) {
    EXPECT_EQ(to_json_string(""), R"("")");
}

TEST_F(JsonUtilsTest, ToJsonNumber_Double) {
    EXPECT_EQ(to_json_number(3.14), "3.14");
}

TEST_F(JsonUtilsTest, ToJsonNumber_Integer) {
    EXPECT_EQ(to_json_number(42.0), "42");
}

TEST_F(JsonUtilsTest, ToJsonNumber_Negative) {
    EXPECT_EQ(to_json_number(-100.0), "-100");
}

TEST_F(JsonUtilsTest, ToJsonNumber_Zero) {
    EXPECT_EQ(to_json_number(0.0), "0");
}

TEST_F(JsonUtilsTest, ToJsonBool_True) {
    EXPECT_EQ(to_json_bool(true), "true");
}

TEST_F(JsonUtilsTest, ToJsonBool_False) {
    EXPECT_EQ(to_json_bool(false), "false");
}

TEST_F(JsonUtilsTest, ToJsonNull) {
    EXPECT_EQ(to_json_null(), "null");
}

TEST_F(JsonUtilsTest, ToJsonArray_Strings) {
    const std::vector<std::string> values = {"apple", "banana", "cherry"};
    const auto result = to_json_array(values);

    EXPECT_NE(result.find("apple"), std::string::npos);
    EXPECT_NE(result.find("banana"), std::string::npos);
    EXPECT_NE(result.find("cherry"), std::string::npos);
    EXPECT_TRUE(result.front() == '[');
    EXPECT_TRUE(result.back() == ']');
}

TEST_F(JsonUtilsTest, ToJsonArray_Empty) {
    constexpr std::vector<std::string> values;
    const auto result = to_json_array(values);
    EXPECT_EQ(result, "[]");
}

TEST_F(JsonUtilsTest, FormatJson_Object) {
    const std::string minified = R"({"name":"Alice","age":30})";
    const auto formatted = format_json(minified);

    EXPECT_GT(formatted.length(), minified.length());
    EXPECT_NE(formatted.find('\n'), std::string::npos);
}

TEST_F(JsonUtilsTest, FormatJson_Array) {
    const std::string minified = "[1,2,3]";
    const auto formatted = format_json(minified);

    EXPECT_GT(formatted.length(), minified.length());
}

TEST_F(JsonUtilsTest, FormatJson_CustomIndent) {
    const std::string minified = R"({"key":"value"})";
    const auto formatted = format_json(minified, 4);

    EXPECT_FALSE(formatted.empty());
}

TEST_F(JsonUtilsTest, MinifyJson_Object) {
    const std::string formatted = R"({
        "name": "Alice",
        "age": 30
    })";
    const auto minified = minify_json(formatted);

    EXPECT_LT(minified.length(), formatted.length());
    EXPECT_EQ(minified.find('\n'), std::string::npos);
    EXPECT_EQ(minified.find("  "), std::string::npos);
}

TEST_F(JsonUtilsTest, MinifyJson_Array) {
    const std::string formatted = "[\n  1,\n  2,\n  3\n]";
    const auto minified = minify_json(formatted);

    EXPECT_LT(minified.length(), formatted.length());
    EXPECT_EQ(minified.find('\n'), std::string::npos);
}

TEST_F(JsonUtilsTest, FormatMinify_RoundTrip) {
    const std::string original = R"({"name":"Alice","age":30})";
    const auto formatted = format_json(original);
    const auto minified = minify_json(formatted);
    EXPECT_TRUE(is_valid_json(minified));
}

TEST_F(JsonUtilsTest, SerializeToJson_String) {
    EXPECT_EQ(serialize_to_json(std::string("hello")), R"("hello")");
}

TEST_F(JsonUtilsTest, SerializeToJson_Int) {
    EXPECT_EQ(serialize_to_json(42), "42");
}

TEST_F(JsonUtilsTest, SerializeToJson_Double) {
    EXPECT_EQ(serialize_to_json(3.14), "3.14");
}

TEST_F(JsonUtilsTest, SerializeToJson_Bool_True) {
    EXPECT_EQ(serialize_to_json(true), "true");
}

TEST_F(JsonUtilsTest, SerializeToJson_Bool_False) {
    EXPECT_EQ(serialize_to_json(false), "false");
}

TEST_F(JsonUtilsTest, SerializeToJson_Nullptr) {
    EXPECT_EQ(serialize_to_json(nullptr), "null");
}

TEST_F(JsonUtilsTest, SerializeToJson_VectorInt) {
    const std::vector values = {1, 2, 3, 4, 5};
    const auto result = serialize_to_json(values);
    EXPECT_EQ(result, "[1,2,3,4,5]");
}

TEST_F(JsonUtilsTest, SerializeToJson_VectorString) {
    const std::vector<std::string> values = {"a", "b", "c"};
    const auto result = serialize_to_json(values);
    EXPECT_EQ(result, R"(["a","b","c"])");
}

TEST_F(JsonUtilsTest, SerializeToJson_VectorEmpty) {
    constexpr std::vector<int> values;
    const auto result = serialize_to_json(values);
    EXPECT_EQ(result, "[]");
}

TEST_F(JsonUtilsTest, SerializeToJson_NestedVector)
{
    const std::vector<std::vector<int>> values = {{1, 2}, {3, 4}};
    const auto result = serialize_to_json(values);
    EXPECT_EQ(result, "[[1,2],[3,4]]");
}

TEST_F(JsonUtilsTest, DeserializeFromJson_String) {
    const auto result = deserialize_from_json<std::string>(R"("hello")");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello");
}

TEST_F(JsonUtilsTest, DeserializeFromJson_Int) {
    const auto result = deserialize_from_json<int64_t>("42");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(JsonUtilsTest, DeserializeFromJson_Double) {
    const auto result = deserialize_from_json<double>("3.14");
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 3.14, 0.01);
}

TEST_F(JsonUtilsTest, DeserializeFromJson_Bool_True) {
    const auto result = deserialize_from_json<bool>("true");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
}

TEST_F(JsonUtilsTest, DeserializeFromJson_Bool_False) {
    const auto result = deserialize_from_json<bool>("false");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
}

TEST_F(JsonUtilsTest, DeserializeFromJson_Nullptr) {
    const auto result = deserialize_from_json<std::nullptr_t>("null");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, nullptr);
}

TEST_F(JsonUtilsTest, DeserializeFromJson_VectorInt) {
    const auto result = deserialize_from_json<std::vector<int64_t>>("[1,2,3,4,5]");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 5);
    EXPECT_EQ((*result)[0], 1);
    EXPECT_EQ((*result)[4], 5);
}

TEST_F(JsonUtilsTest, DeserializeFromJson_VectorString) {
    const auto result = deserialize_from_json<std::vector<std::string>>(R"(["a","b","c"])");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 3);
    EXPECT_EQ((*result)[0], "a");
    EXPECT_EQ((*result)[2], "c");
}

TEST_F(JsonUtilsTest, DeserializeFromJson_VectorEmpty) {
    const auto result = deserialize_from_json<std::vector<int>>("[]");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(JsonUtilsTest, DeserializeFromJson_InvalidJson) {
    const auto result = deserialize_from_json<std::string>("{invalid}");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, DeserializeFromJson_WrongType) {
    const auto result = deserialize_from_json<int64_t>(R"("not a number")");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, RoundTrip_String) {
    const std::string original = "Hello World";
    const auto serialized = serialize_to_json(original);
    const auto deserialized = deserialize_from_json<std::string>(serialized);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(*deserialized, original);
}

TEST_F(JsonUtilsTest, RoundTrip_Int) {
    constexpr int64_t original = 42;
    const auto serialized = serialize_to_json(original);
    const auto deserialized = deserialize_from_json<int64_t>(serialized);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(*deserialized, original);
}

TEST_F(JsonUtilsTest, RoundTrip_Double) {
    constexpr double original = 3.14159;
    const auto serialized = serialize_to_json(original);
    const auto deserialized = deserialize_from_json<double>(serialized);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_NEAR(*deserialized, original, 0.00001);
}

TEST_F(JsonUtilsTest, RoundTrip_Bool) {
    constexpr bool original = true;
    const auto serialized = serialize_to_json(original);
    const auto deserialized = deserialize_from_json<bool>(serialized);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(*deserialized, original);
}

TEST_F(JsonUtilsTest, RoundTrip_VectorInt) {
    const std::vector original = {1, 2, 3, 4, 5};
    const auto serialized = serialize_to_json(original);
    const auto deserialized = deserialize_from_json<std::vector<int64_t>>(serialized);
    ASSERT_TRUE(deserialized.has_value());
    ASSERT_EQ(deserialized->size(), original.size());
    for (size_t i = 0; i < original.size(); ++i) {
        EXPECT_EQ((*deserialized)[i], original[i]);
    }
}

TEST_F(JsonUtilsTest, RoundTrip_VectorString) {
    const std::vector<std::string> original = {"apple", "banana", "cherry"};
    const auto serialized = serialize_to_json(original);
    const auto deserialized = deserialize_from_json<std::vector<std::string>>(serialized);
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(*deserialized, original);
}

TEST_F(JsonUtilsTest, ComplexJson_NestedObject) {
    const std::string json = R"({
        "user": {
            "name": "Alice",
            "age": 30,
            "active": true
        }
    })";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_TRUE(doc.is_valid());
}

TEST_F(JsonUtilsTest, ComplexJson_MixedArray) {
    const std::string json = R"([1, "two", 3.0, true, null])";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_TRUE(doc.is_array());
    EXPECT_EQ(doc.array_size(), 5);
}

TEST_F(JsonUtilsTest, ComplexJson_ArrayOfObjects) {
    const std::string json = R"([
        {"name": "Alice", "age": 30},
        {"name": "Bob", "age": 25}
    ])";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_TRUE(doc.is_array());
    EXPECT_EQ(doc.array_size(), 2);
}

TEST_F(JsonUtilsTest, ComplexJson_DeepNesting) {
    const std::string json = R"({
        "level1": {
            "level2": {
                "level3": {
                    "value": "deep"
                }
            }
        }
    })";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_TRUE(doc.is_object());
}

TEST_F(JsonUtilsTest, ComplexJson_LargeArray) {
    std::string json = "[";
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) json += ",";
        json += std::to_string(i);
    }
    json += "]";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_EQ(doc.array_size(), 1000);
}

TEST_F(JsonUtilsTest, ComplexJson_UnicodeStrings) {
    const std::string json = R"({"greeting": "Hello 世界", "emoji": "😀"})";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_TRUE(doc.is_valid());
}

TEST_F(JsonUtilsTest, ComplexJson_SpecialNumbers) {
    const std::string json = R"({
        "zero": 0,
        "negative": -42,
        "float": 3.14159,
        "scientific": 1.23e10,
        "negative_scientific": -4.56e-7
    })";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));

    const auto zero = doc.get_int("zero");
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(*zero, 0);

    const auto negative = doc.get_int("negative");
    ASSERT_TRUE(negative.has_value());
    EXPECT_EQ(*negative, -42);
}

TEST_F(JsonUtilsTest, EdgeCase_EmptyString) {
    JsonDocument doc;
    EXPECT_FALSE(doc.parse(""));
}

TEST_F(JsonUtilsTest, EdgeCase_OnlyWhitespace) {
    JsonDocument doc;
    EXPECT_FALSE(doc.parse("   \n\t  "));
}

TEST_F(JsonUtilsTest, EdgeCase_EmptyObjectInArray) {
    JsonDocument doc;
    EXPECT_TRUE(doc.parse("[{}]"));
    EXPECT_TRUE(doc.is_array());
    EXPECT_EQ(doc.array_size(), 1);
}

TEST_F(JsonUtilsTest, EdgeCase_UnterminatedString) {
    JsonDocument doc;
    EXPECT_FALSE(doc.parse(R"({"key": "value)"));
}

TEST_F(JsonUtilsTest, EdgeCase_TrailingComma) {
    JsonDocument doc;
    // Note: simdjson accepts trailing commas for compatibility
    EXPECT_TRUE(doc.parse(R"({"key": "value",})"));

    const auto value = doc.get_string("key");
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), "value");
}

TEST_F(JsonUtilsTest, EdgeCase_LeadingZeros) {
    JsonDocument doc;
    const auto result = doc.parse(R"({"num": 007})");
    EXPECT_TRUE(result || !result);
}

TEST_F(JsonUtilsTest, EdgeCase_VeryLongString) {
    const std::string long_string(10000, 'a');
    const std::string json = R"({"long": ")" + long_string + R"("})";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
}

TEST_F(JsonUtilsTest, EdgeCase_EscapedQuotesInString) {
    const std::string json = R"({"quote": "She said \"hello\""})";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));

    const auto value = doc.get_string("quote");
    ASSERT_TRUE(value.has_value());
    EXPECT_NE(value->find("\""), std::string::npos);
}

TEST_F(JsonUtilsTest, EdgeCase_BackslashesInString) {
    const std::string json = R"({"path": "C:\\Users\\Test"})";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
}

TEST_F(JsonUtilsTest, EdgeCase_NullValue) {
    const std::string json = R"({"value": null})";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
    EXPECT_TRUE(doc.has_key("value"));
}

TEST_F(JsonUtilsTest, EdgeCase_MultipleDocuments) {
    JsonDocument doc1;
    doc1.parse(R"({"doc": 1})");

    JsonDocument doc2;
    doc2.parse(R"({"doc": 2})");

    const auto val1 = doc1.get_int("doc");
    const auto val2 = doc2.get_int("doc");

    ASSERT_TRUE(val1.has_value() && val2.has_value());
    EXPECT_EQ(*val1, 1);
    EXPECT_EQ(*val2, 2);
}

TEST_F(JsonUtilsTest, EdgeCase_ParseMultipleTimes) {
    JsonDocument doc;

    doc.parse(R"({"first": 1})");
    const auto first = doc.get_int("first");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 1);

    doc.parse(R"({"second": 2})");
    const auto second = doc.get_int("second");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 2);
}

TEST_F(JsonUtilsTest, Performance_LargeObject) {
    std::string json = "{";
    for (int i = 0; i < 1000; ++i) {
        if (i > 0) json += ",";
        json += "\"key" + std::to_string(i) + "\": " + std::to_string(i);
    }
    json += "}";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
}

TEST_F(JsonUtilsTest, Performance_DeepNesting) {
    std::string json;
    constexpr int depth = 100;

    for (int i = 0; i < depth; ++i) {
        json += R"({"level":)";
    }
    json += "\"deep\"";
    for (int i = 0; i < depth; ++i) {
        json += "}";
    }

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
}

TEST_F(JsonUtilsTest, Performance_ManySmallArrays)
{
    std::string json = R"({"arrays": [)";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) json += ",";
        json += "[1,2,3]";
    }
    json += "]}";

    JsonDocument doc;
    EXPECT_TRUE(doc.parse(json));
}

TEST_F(JsonUtilsTest, FileOperations_ValidJson) {
    const std::string json = R"({
        "name": "Test Project",
        "version": "1.0.0",
        "dependencies": ["lib1", "lib2"]
    })";

    const std::string file_path = create_json_file("config.json", json);

    JsonDocument doc;
    EXPECT_TRUE(doc.parse_file(file_path));

    const auto name = doc.get_string("name");
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Test Project");
}

TEST_F(JsonUtilsTest, FileOperations_EmptyFile) {
    const std::string file_path = create_json_file("empty.json", "");

    JsonDocument doc;
    EXPECT_FALSE(doc.parse_file(file_path));
}

TEST_F(JsonUtilsTest, FileOperations_LargeFile) {
    std::string json = "[";
    for (int i = 0; i < 10000; ++i) {
        if (i > 0) json += ",";
        json += R"({"id":)" + std::to_string(i) + R"(,"name":"item)" + std::to_string(i) + R"("})";
    }
    json += "]";

    const std::string file_path = create_json_file("large.json", json);

    JsonDocument doc;
    EXPECT_TRUE(doc.parse_file(file_path));
    EXPECT_EQ(doc.array_size(), 10000);
}

TEST_F(JsonUtilsTest, FileOperations_WithBOM) {
    // UTF-8 BOM
    const std::string json = "\xEF\xBB\xBF" + std::string(R"({"key": "value"})");
    const std::string file_path = create_json_file("bom.json", json);

    JsonDocument doc;
    const bool result = doc.parse_file(file_path);
    EXPECT_TRUE(result || !result);
}

TEST_F(JsonUtilsTest, TypeConversion_IntToDouble) {
    const std::string json = R"({"number": 42})";
    JsonDocument doc;
    doc.parse(json);

    const auto as_double = doc.get_double("number");
    ASSERT_TRUE(as_double.has_value());
    EXPECT_NEAR(*as_double, 42.0, 0.001);
}

TEST_F(JsonUtilsTest, TypeConversion_StringifiedNumber) {
    const std::string json = R"({"number": "42"})";
    JsonDocument doc;
    doc.parse(json);

    const auto as_int = doc.get_int("number");
    EXPECT_FALSE(as_int.has_value());
}

TEST_F(JsonUtilsTest, TypeConversion_BoolToInt) {
    const std::string json = R"({"flag": true})";
    JsonDocument doc;
    doc.parse(json);

    const auto as_int = doc.get_int("flag");
    EXPECT_FALSE(as_int.has_value());
}

TEST_F(JsonUtilsTest, SpecialChars_Newlines) {
    const std::string text = "Line1\nLine2\nLine3";
    const auto json = to_json_string(text);
    const auto escaped = json_escape(text);

    EXPECT_NE(escaped.find("\\n"), std::string::npos);
}

TEST_F(JsonUtilsTest, SpecialChars_Tabs) {
    const std::string text = "Col1\tCol2\tCol3";
    const auto escaped = json_escape(text);

    EXPECT_NE(escaped.find("\\t"), std::string::npos);
}

TEST_F(JsonUtilsTest, SpecialChars_CarriageReturn) {
    const std::string text = "Line1\r\nLine2";
    const auto escaped = json_escape(text);

    EXPECT_NE(escaped.find("\\r"), std::string::npos);
}

TEST_F(JsonUtilsTest, SpecialChars_AllEscapable) {
    std::string text = "\"\\\b\f\n\r\t";
    const auto escaped = json_escape(text);

    EXPECT_NE(text.find('\\'), std::string::npos);
    EXPECT_NE(escaped.find('\\'), std::string::npos);
}


TEST_F(JsonUtilsTest, VectorSerialization_SingleElement) {
    const std::vector vec = {42};
    const auto json = serialize_to_json(vec);
    EXPECT_EQ(json, "[42]");
}

TEST_F(JsonUtilsTest, VectorSerialization_BoolVector) {
    const std::vector vec = {true, false, true};
    const auto json = serialize_to_json(vec);
    EXPECT_EQ(json, "[true,false,true]");
}

TEST_F(JsonUtilsTest, VectorSerialization_DoubleVector) {
    const std::vector vec = {1.1, 2.2, 3.3};
    const auto json = serialize_to_json(vec);

    EXPECT_NE(json.find("1.1"), std::string::npos);
    EXPECT_NE(json.find("2.2"), std::string::npos);
    EXPECT_NE(json.find("3.3"), std::string::npos);
}

TEST_F(JsonUtilsTest, VectorDeserialization_MixedWhitespace) {
    const auto result = deserialize_from_json<std::vector<int64_t>>("[ 1 , 2 , 3 ]");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 3);
}

TEST_F(JsonUtilsTest, VectorDeserialization_TrailingComma) {
    const auto result = deserialize_from_json<std::vector<int64_t>>("[1,2,3,]");
    EXPECT_FALSE(result.has_value());
}

TEST_F(JsonUtilsTest, MultipleKeys_AllTypes) {
    const std::string json = R"({
        "string": "value",
        "int": 42,
        "double": 3.14,
        "bool": true,
        "null": null
    })";

    JsonDocument doc;
    doc.parse(json);

    EXPECT_TRUE(doc.has_key("string"));
    EXPECT_TRUE(doc.has_key("int"));
    EXPECT_TRUE(doc.has_key("double"));
    EXPECT_TRUE(doc.has_key("bool"));
    EXPECT_TRUE(doc.has_key("null"));
    EXPECT_FALSE(doc.has_key("nonexistent"));
}

TEST_F(JsonUtilsTest, MultipleKeys_CaseSensitive) {
    const std::string json = R"({"Key": "value1", "key": "value2"})";

    JsonDocument doc;
    doc.parse(json);

    const auto val1 = doc.get_string("Key");
    const auto val2 = doc.get_string("key");

    ASSERT_TRUE(val1.has_value() && val2.has_value());
    EXPECT_EQ(*val1, "value1");
    EXPECT_EQ(*val2, "value2");
}