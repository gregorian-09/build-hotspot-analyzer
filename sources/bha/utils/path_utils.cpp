//
// Created by gregorian on 14/10/2025.
//

#include "bha/utils/path_utils.h"
#include "bha/utils/string_utils.h"
#include <algorithm>

namespace bha::utils {

std::string normalize_path(const std::string_view path) {
    try {
        return fs::path(path).lexically_normal().string();
    } catch (const std::exception&) {
        return std::string(path);
    }
}

std::string get_absolute_path(const std::string_view path) {
    try {
        return fs::absolute(fs::path(path)).string();
    } catch (const std::exception&) {
        return std::string(path);
    }
}

std::string get_relative_path(const std::string_view path, const std::string_view base) {
    try {
        const fs::path p(path);
        const fs::path b(base);
        return fs::relative(p, b).string();
    } catch (const std::exception&) {
        return std::string(path);
    }
}

std::string get_filename(const std::string_view path) {
    return fs::path(path).filename().string();
}

std::string get_stem(const std::string_view path) {
    return fs::path(path).stem().string();
}

std::string get_extension(const std::string_view path) {
    return fs::path(path).extension().string();
}

std::string get_parent_path(const std::string_view path) {
    return fs::path(path).parent_path().string();
}

std::string join_paths(const std::string_view path1, const std::string_view path2) {
    const fs::path p1(path1);
    const fs::path p2(path2);
    return (p1 / p2).string();
}

bool is_absolute(const std::string_view path) {
    return fs::path(path).is_absolute();
}

bool path_exists(const std::string_view path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

bool is_file(const std::string_view path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec);
}

bool is_directory(const std::string_view path) {
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec);
}

bool has_extension(const std::string_view path, const std::string_view ext) {
    const std::string path_ext = get_extension(path);
    std::string target_ext(ext);

    if (!target_ext.empty() && target_ext[0] != '.') {
        target_ext = "." + target_ext;
    }

    return equals_ignore_case(path_ext, target_ext);
}

std::string replace_extension(const std::string_view path, const std::string_view new_ext) {
    fs::path p(path);
    std::string ext(new_ext);

    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }

    return p.replace_extension(ext).string();
}

std::string to_native_separators(const std::string_view path) {
    return fs::path(path).make_preferred().string();
}

std::string to_posix_separators(const std::string_view path) {
    std::string result(path);
    std::ranges::replace(result, '\\', '/');
    return result;
}

bool is_subdirectory_of(const std::string_view path, const std::string_view parent) {
    try {
        const fs::path p = fs::absolute(fs::path(path));
        const fs::path par = fs::absolute(fs::path(parent));

        const auto rel = fs::relative(p, par);
        return !rel.empty() && rel.native()[0] != '.';
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<std::string> find_file_in_parents(const std::string_view start_dir, const std::string_view filename) {
    try {
        fs::path current = fs::absolute(fs::path(start_dir));

        while (true) {
            if (fs::path candidate = current / filename; fs::exists(candidate)) {
                return candidate.string();
            }

            fs::path parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
    } catch (const std::exception&) {
    }

    return std::nullopt;
}

std::vector<std::string> list_files(const std::string_view directory, const bool recursive) {
    std::vector<std::string> files;

    try
    {
        std::error_code ec;
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(fs::path(directory), ec)) {
                if (entry.is_regular_file(ec)) {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(fs::path(directory), ec)) {
                if (entry.is_regular_file(ec)) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception&) {
    }

    return files;
}

std::vector<std::string> list_files_with_extension(const std::string_view directory,
                                                     const std::string_view extension,
                                                     const bool recursive) {
    std::vector<std::string> files;

    std::string target_ext(extension);
    if (!target_ext.empty() && target_ext[0] != '.') {
        target_ext = "." + target_ext;
    }

    try
    {
        std::error_code ec;
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(fs::path(directory), ec)) {
                if (entry.is_regular_file(ec)) {
                    if (std::string ext = entry.path().extension().string(); equals_ignore_case(ext, target_ext)) {
                        files.push_back(entry.path().string());
                    }
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(fs::path(directory), ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string ext = entry.path().extension().string();
                    if (equals_ignore_case(ext, target_ext)) {
                        files.push_back(entry.path().string());
                    }
                }
            }
        }
    } catch (const std::exception&) {
    }

    return files;
}

std::string make_preferred(const std::string_view path) {
    return fs::path(path).make_preferred().string();
}

bool create_directories(const std::string_view path) {
    std::error_code ec;
    return fs::create_directories(fs::path(path), ec);
}

std::optional<uintmax_t> file_size(const std::string_view path) {
    std::error_code ec;
    auto size = fs::file_size(fs::path(path), ec);

    if (ec) {
        return std::nullopt;
    }

    return size;
}

std::string get_current_directory() {
    std::error_code ec;
    const auto path = fs::current_path(ec);

    if (ec) {
        return "";
    }

    return path.string();
}

bool is_same_file(const std::string_view path1, const std::string_view path2) {
    std::error_code ec;
    return fs::equivalent(fs::path(path1), fs::path(path2), ec);
}

} // namespace bha::utils