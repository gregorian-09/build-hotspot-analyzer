//
// Created by gregorian on 14/10/2025.
//

#include "bha/utils/json_utils.h"
#include "bha/utils/file_utils.h"
#include "bha/utils/string_utils.h"
#include <sstream>
#include <iomanip>

namespace bha::utils {

JsonDocument::JsonDocument() = default;

JsonDocument::~JsonDocument() = default;

JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;

JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

bool JsonDocument::parse(const std::string_view json) {
    try {
        json_data_ = simdjson::padded_string(json);
        doc_ = parser_.iterate(json_data_);
        return true;
    } catch (const simdjson::simdjson_error&) {
        doc_ = std::nullopt;
        return false;
    }
}

bool JsonDocument::parse_file(const std::string_view path) {
    const auto content = read_file(path);
    if (!content) {
        return false;
    }
    return parse(*content);
}

bool JsonDocument::is_valid() const {
    if (!doc_.has_value()) {
        return false;
    }

    try {
        simdjson::ondemand::parser temp_parser;
        auto temp_doc = temp_parser.iterate(json_data_);
        if (temp_doc.error()) {
            return false;
        }

        auto type_result = temp_doc.type();
        if (type_result.error()) {
            return false;
        }

        if (const auto type = type_result.value(); type == simdjson::ondemand::json_type::object) {
            auto obj_result = temp_doc.get_object();
            if (obj_result.error()) {
                return false;
            }
            for (auto field : obj_result.value()) {
                if (auto key_result = field.unescaped_key(); key_result.error()) {
                    return false;
                }
            }
        } else if (type == simdjson::ondemand::json_type::array) {
            auto arr_result = temp_doc.get_array();
            if (arr_result.error()) {
                return false;
            }
            for (auto elem : arr_result.value()) {
                if (auto elem_type = elem.type(); elem_type.error()) {
                    return false;
                }
            }
        }

        return true;
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

std::optional<std::string> JsonDocument::get_string(const std::string_view key) {
    if (!doc_) return std::nullopt;

    try {
        auto value = doc_->find_field(key)->get_string();
        return std::string(value.value());
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

std::optional<int64_t> JsonDocument::get_int(const std::string_view key) {
    if (!doc_) return std::nullopt;

    try {
        return doc_->find_field(key)->get_int64().value();
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

std::optional<double> JsonDocument::get_double(const std::string_view key) {
    if (!doc_) return std::nullopt;

    try {
        return doc_->find_field(key)->get_double().value();
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

std::optional<bool> JsonDocument::get_bool(const std::string_view key) {
    if (!doc_) return std::nullopt;

    try {
        return doc_->find_field(key)->get_bool().value();
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

bool JsonDocument::has_key(const std::string_view key) {
    if (!doc_) return false;

    try {
        const auto result = doc_->find_field_unordered(key);
        return !result.error();
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

bool JsonDocument::is_array() {
    if (!doc_) return false;

    try {
        return doc_->type() == simdjson::ondemand::json_type::array;
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

bool JsonDocument::is_object() {
    if (!doc_) return false;

    try {
        return doc_->type() == simdjson::ondemand::json_type::object;
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

size_t JsonDocument::array_size() {
    if (!doc_ || !is_array()) return 0;

    try {
        auto result = doc_->count_elements();
        if (result.error()) {
            return 0;
        }
        return result.value();
    } catch (const simdjson::simdjson_error&) {
        return 0;
    }
}

simdjson::ondemand::document& JsonDocument::get_document() {
    return *doc_;
}

const simdjson::ondemand::document& JsonDocument::get_document() const {
    return *doc_;
}

std::optional<std::string> parse_json_string(const std::string_view json) {
    JsonDocument doc;
    if (!doc.parse(json)) {
        return std::nullopt;
    }

    try {
        auto value = doc.get_document().get_string();
        return std::string(value.value());
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

std::optional<int64_t> parse_json_int(const std::string_view json) {
    JsonDocument doc;
    if (!doc.parse(json)) {
        return std::nullopt;
    }

    try {
        return doc.get_document().get_int64().value();
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

std::optional<double> parse_json_double(const std::string_view json) {
    JsonDocument doc;
    if (!doc.parse(json)) {
        return std::nullopt;
    }

    try {
        return doc.get_document().get_double().value();
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

std::optional<bool> parse_json_bool(const std::string_view json) {
    JsonDocument doc;
    if (!doc.parse(json)) {
        return std::nullopt;
    }

    try {
        return doc.get_document().get_bool().value();
    } catch (const simdjson::simdjson_error&) {
        return std::nullopt;
    }
}

bool is_valid_json(const std::string_view json) {
    JsonDocument doc;
    if (!doc.parse(json)) {
        return false;
    }

    try {
        auto& raw_doc = doc.get_document();

        if (const auto type = raw_doc.type().value(); type == simdjson::ondemand::json_type::array) {
            auto array = raw_doc.get_array().value();
            if (const auto count = array.count_elements(); count.error()) {
                return false;
            }
        } else if (type == simdjson::ondemand::json_type::object) {
            auto obj = raw_doc.get_object().value();
            if (const auto count = obj.count_fields(); count.error()) {
                return false;
            }
        }

        return true;
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

std::optional<std::string> get_json_value(const std::string_view json, const std::string_view key) {
    JsonDocument doc;
    if (!doc.parse(json)) {
        return std::nullopt;
    }
    return doc.get_string(key);
}

std::string json_escape(const std::string_view str) {
    std::ostringstream ss;

    for (const char c : str) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (c < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(c);
                } else {
                    ss << c;
                }
        }
    }

    return ss.str();
}

std::string json_unescape(const std::string_view str) {
    std::ostringstream ss;

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            ++i;
            switch (str[i]) {
                case '"':  ss << '"'; break;
                case '\\': ss << '\\'; break;
                case '/':  ss << '/'; break;
                case 'b':  ss << '\b'; break;
                case 'f':  ss << '\f'; break;
                case 'n':  ss << '\n'; break;
                case 'r':  ss << '\r'; break;
                case 't':  ss << '\t'; break;
                case 'u':
                    if (i + 4 < str.size()) {
                        std::string hex(str.substr(i + 1, 4));
                        const int code = std::stoi(hex, nullptr, 16);
                        ss << static_cast<char>(code);
                        i += 4;
                    }
                    break;
                default:
                    ss << str[i];
            }
        } else {
            ss << str[i];
        }
    }

    return ss.str();
}

std::string to_json_string(const std::string_view str) {
    return "\"" + json_escape(str) + "\"";
}

std::string to_json_number(const double value) {
    std::ostringstream ss;
    ss << std::setprecision(15) << value;
    return ss.str();
}

std::string to_json_number(const int64_t value) {
    return std::to_string(value);
}

std::string to_json_bool(const bool value) {
    return value ? "true" : "false";
}

std::string to_json_null() {
    return "null";
}

std::string to_json_array(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "[]";
    }

    std::ostringstream ss;
    ss << '[';

    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) ss << ',';
        ss << to_json_string(values[i]);
    }

    ss << ']';
    return ss.str();
}

std::string format_json(const std::string_view json, const int indent) {
    std::ostringstream result;
    int current_indent = 0;
    bool in_string = false;
    bool escape_next = false;

    for (const char c : json) {
        if (escape_next) {
            result << c;
            escape_next = false;
            continue;
        }

        if (c == '\\' && in_string) {
            result << c;
            escape_next = true;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            result << c;
            continue;
        }

        if (in_string) {
            result << c;
            continue;
        }

        switch (c) {
            case '{':
            case '[':
                result << c << '\n';
                current_indent += indent;
                result << std::string(current_indent, ' ');
                break;

            case '}':
            case ']':
                result << '\n';
                current_indent -= indent;
                result << std::string(current_indent, ' ') << c;
                break;

            case ',':
                result << c << '\n' << std::string(current_indent, ' ');
                break;

            case ':':
                result << c << ' ';
                break;

            case ' ':
            case '\t':
            case '\n':
            case '\r':
                break;

            default:
                result << c;
        }
    }

    return result.str();
}

std::string minify_json(const std::string_view json) {
    std::ostringstream result;
    bool in_string = false;
    bool escape_next = false;

    for (const char c : json) {
        if (escape_next) {
            result << c;
            escape_next = false;
            continue;
        }

        if (c == '\\' && in_string) {
            result << c;
            escape_next = true;
            continue;
        }

        if (c == '"') {
            in_string = !in_string;
            result << c;
            continue;
        }

        if (in_string) {
            result << c;
            continue;
        }

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            result << c;
        }
    }

    return result.str();
}

} // namespace bha::utils