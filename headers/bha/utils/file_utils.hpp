#pragma once

#include "bha/result.hpp"
#include "bha/error.hpp"
#include "bha/utils/string_utils.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace bha::utils {

    namespace fs = std::filesystem;

    [[nodiscard]] inline Result<std::string, Error> read_file(const fs::path& path) {
        if (std::error_code ec; !fs::exists(path, ec)) {
            return Result<std::string, Error>::failure(
                Error::not_found("File not found", path.string()));
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return Result<std::string, Error>::failure(
                Error::io_error("Failed to open file", path.string()));
        }
        std::ostringstream oss;
        oss << file.rdbuf();
        if (file.bad()) {
            return Result<std::string, Error>::failure(
                Error::io_error("Failed to read file", path.string()));
        }
        return Result<std::string, Error>::success(oss.str());
    }

    [[nodiscard]] inline Result<std::vector<std::string>, Error> read_lines(const fs::path& path) {
        if (std::error_code ec; !fs::exists(path, ec)) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::not_found("File not found", path.string()));
        }
        std::ifstream file(path);
        if (!file) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::io_error("Failed to open file", path.string()));
        }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(std::move(line));
        }
        if (file.bad()) {
            return Result<std::vector<std::string>, Error>::failure(
                Error::io_error("Failed to read file", path.string()));
        }
        return Result<std::vector<std::string>, Error>::success(std::move(lines));
    }

    inline Result<void, Error> write_file(const fs::path& path, std::string_view content) {
        auto parent = path.parent_path();
        if (std::error_code ec; !parent.empty() && !fs::exists(parent, ec)) {
            fs::create_directories(parent, ec);
            if (ec) {
                return Result<void, Error>::failure(
                    Error::io_error("Failed to create directory", parent.string()));
            }
        }
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return Result<void, Error>::failure(
                Error::io_error("Failed to open file for writing", path.string()));
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            return Result<void, Error>::failure(
                Error::io_error("Failed to write file", path.string()));
        }
        return Result<void, Error>::success();
    }

    [[nodiscard]] inline Result<std::uintmax_t, Error> file_size(const fs::path& path) {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec) {
            return Result<std::uintmax_t, Error>::failure(
                Error::io_error("Failed to get file size", path.string()));
        }
        return Result<std::uintmax_t, Error>::success(size);
    }

    [[nodiscard]] inline Result<fs::file_time_type, Error> last_modified(const fs::path& path) {
        std::error_code ec;
        const auto time = fs::last_write_time(path, ec);
        if (ec) {
            return Result<fs::file_time_type, Error>::failure(
                Error::io_error("Failed to get modification time", path.string()));
        }
        return Result<fs::file_time_type, Error>::success(time);
    }

    [[nodiscard]] inline Result<std::vector<fs::path>, Error> list_files(
        const fs::path& dir,
        const std::string_view extension = "",
        const bool recursive = false
    ) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            return Result<std::vector<fs::path>, Error>::failure(
                Error::not_found("Directory not found", dir.string()));
        }
        if (!fs::is_directory(dir, ec)) {
            return Result<std::vector<fs::path>, Error>::failure(
                Error::invalid_argument("Not a directory", dir.string()));
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
            for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; ++it) {
                process_entry(*it);
            }
        } else {
            for (fs::directory_iterator it(dir, ec), end; it != end && !ec; ++it) {
                process_entry(*it);
            }
        }
        if (ec) {
            return Result<std::vector<fs::path>, Error>::failure(
                Error::io_error("Failed to list directory", dir.string()));
        }
        return Result<std::vector<fs::path>, Error>::success(std::move(result));
    }

    inline Result<fs::path, Error> create_temp_file(
        std::string_view prefix = "bha_",
        std::string_view extension = ".tmp"
    ) {
        std::error_code ec;
        auto temp_dir = fs::temp_directory_path(ec);
        if (ec) {
            return Result<fs::path, Error>::failure(
                Error::io_error("Failed to get temp directory"));
        }
#ifdef _WIN32
        char temp_dir_buf[MAX_PATH];
        const DWORD dir_len = GetTempPathA(MAX_PATH, temp_dir_buf);
        if (dir_len == 0 || dir_len > MAX_PATH) {
            return Result<fs::path, Error>::failure(Error::io_error("Failed to get temp directory path"));
        }
        char temp_file_buf[MAX_PATH];
        std::string prefix_str(prefix);
        if (!GetTempFileNameA(temp_dir_buf, prefix_str.c_str(), 0, temp_file_buf)) {
            return Result<fs::path, Error>::failure(Error::io_error("Failed to create temp file"));
        }
        fs::path temp_path = temp_file_buf;
        if (!extension.empty()) {
            fs::path renamed = temp_path;
            renamed.replace_extension(extension);
            if (std::error_code rename_ec; !fs::equivalent(temp_path, renamed, rename_ec) || rename_ec) {
                fs::rename(temp_path, renamed, rename_ec);
                if (rename_ec) {
                    return Result<fs::path, Error>::failure(
                        Error::io_error("Failed to rename temp file", rename_ec.message()));
                }
            }
            temp_path = renamed;
        }
        return Result<fs::path, Error>::success(std::move(temp_path));
#else
        const std::string suffix(extension);
        std::string tmpl = (temp_dir / (std::string(prefix) + "XXXXXX" + suffix)).string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        int fd = -1;
        if (suffix.empty()) {
            fd = mkstemp(buf.data());
        } else {
            fd = mkstemps(buf.data(), static_cast<int>(suffix.size()));
        }
        if (fd == -1) {
            return Result<fs::path, Error>::failure(Error::io_error("Failed to create temp file"));
        }
        close(fd);
        return Result<fs::path, Error>::success(fs::path(buf.data()));
#endif
    }

    inline bool is_header_file_path(const fs::path& path) {
        const std::string ext = bha::utils::to_lower(path.extension().string());
        return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" ||
               ext == ".h++" || ext == ".inc" || ext == ".inl" ||
               ext == ".ipp" || ext == ".tpp";
    }

    inline bool is_source_file_path(const fs::path& path) {
        const std::string ext = bha::utils::to_lower(path.extension().string());
        return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
               ext == ".c++" || ext == ".cp" || ext == ".m" || ext == ".mm" ||
               ext == ".cu";
    }

    inline bool is_json_file(const fs::path& path) {
        return bha::utils::to_lower(path.extension().string()) == ".json";
    }

}  // namespace bha::utils
