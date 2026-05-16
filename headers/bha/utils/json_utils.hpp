#pragma once

#include "bha/error.hpp"
#include "bha/result.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace bha::utils {

    namespace fs = std::filesystem;
    using json = nlohmann::json;

    inline Result<json, Error> parse_json(std::string_view content) {
        try {
            return Result<json, Error>::success(json::parse(content));
        } catch (const json::parse_error& e) {
            return Result<json, Error>::failure(
                Error::parse_error("JSON parse error", e.what()));
        }
    }

    template<typename T>
    T json_get_or(const json& obj, const std::string& key, const T& default_value) {
        if (obj.contains(key)) {
            try {
                return obj.at(key).get<T>();
            } catch (...) {
                return default_value;
            }
        }
        return default_value;
    }

    template<typename T>
    Result<T, Error> json_get(const json& obj, const std::string& key) {
        if (!obj.contains(key)) {
            return Result<T, Error>::failure(Error::not_found("JSON key not found", key));
        }
        try {
            return Result<T, Error>::success(obj.at(key).get<T>());
        } catch (const json::type_error& e) {
            return Result<T, Error>::failure(
                Error::parse_error("JSON type mismatch", key + ": " + e.what()));
        }
    }

    inline json json_merge(const json& base, const json& overlay) {
        json result = base;
        for (auto& [key, value] : overlay.items()) {
            if (result.contains(key) && result[key].is_object() && value.is_object()) {
                result[key] = json_merge(result[key], value);
            } else {
                result[key] = value;
            }
        }
        return result;
    }

}  // namespace bha::utils
