#pragma once

#include <string>
#include <string_view>
#include <filesystem>
#include <cctype>

namespace bha::lsp::uri
{
    namespace fs = std::filesystem;

    inline bool is_unreserved(const unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    }

    inline std::string percent_encode(const std::string_view input) {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (is_unreserved(c) || c == '/' || c == ':') {
                out.push_back(static_cast<char>(c));
            } else {
                static const char* hex = "0123456789ABCDEF";
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    }

    inline int hex_value(const unsigned char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        return -1;
    }

    inline std::string percent_decode(const std::string_view input) {
        std::string out;
        out.reserve(input.size());
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '%' && i + 2 < input.size()) {
                const int hi = hex_value(static_cast<unsigned char>(input[i + 1]));
                const int lo = hex_value(static_cast<unsigned char>(input[i + 2]));
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            out.push_back(input[i]);
        }
        return out;
    }

    inline std::string path_to_uri(const fs::path& path) {
        fs::path abs = fs::absolute(path).lexically_normal();
        std::string path_str = abs.string();
#ifdef _WIN32
        for (char& c : path_str) {
            if (c == '\\') c = '/';
        }
        if (path_str.size() >= 2 && std::isalpha(static_cast<unsigned char>(path_str[0])) && path_str[1] == ':') {
            return "file:///" + percent_encode(path_str);
        }
        if (path_str.rfind("//", 0) == 0) {
            return "file:" + percent_encode(path_str);
        }
        return "file://" + percent_encode(path_str);
#else
        return "file://" + percent_encode(path_str);
#endif
    }

    inline fs::path uri_to_path(std::string_view uri) {
        constexpr std::string_view prefix = "file://";
        if (uri.rfind(prefix, 0) != 0) {
            return fs::path(std::string(uri));
        }

        std::string_view rest = uri.substr(prefix.size());
#ifdef _WIN32
        if (rest.rfind('/', 0) == 0 && rest.size() >= 3 &&
            std::isalpha(static_cast<unsigned char>(rest[1])) && rest[2] == ':') {
            rest = rest.substr(1);
        }
        std::string decoded = percent_decode(rest);
        for (char& c : decoded) {
            if (c == '/') c = '\\';
        }
        return fs::path(decoded);
#else
        std::string decoded = percent_decode(rest);
        return fs::path(decoded);
#endif
    }
}
