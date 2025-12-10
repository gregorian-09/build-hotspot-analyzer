//
// Created by gregorian on 14/10/2025.
//

#include "bha/utils/file_utils.h"
#include "bha/utils/path_utils.h"
#include <filesystem>
#include <sstream>
#include <iterator>

namespace bha::utils {

namespace fs = std::filesystem;

std::optional<std::string> read_file(const std::string_view path) {
    std::ifstream file(std::string(path), std::ios::in | std::ios::binary);

    if (!file.is_open()) {
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::optional<std::vector<std::string>> read_lines(const std::string_view path) {
    std::ifstream file{std::string(path)};

    if (!file.is_open()) {
        return std::nullopt;
    }

    std::vector<std::string> lines;
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    return lines;
}

bool write_file(const std::string_view path, const std::string_view content) {
    const std::filesystem::path p(path);

    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream file(p, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}


bool write_lines(const std::string_view path, const std::vector<std::string>& lines) {
    std::ofstream file{std::string(path)};

    if (!file.is_open()) {
        return false;
    }

    for (const auto& line : lines) {
        file << line << '\n';
    }

    return file.good();
}

bool append_to_file(const std::string_view path, const std::string_view content) {
    std::ofstream file(std::string(path), std::ios::out | std::ios::app);

    if (!file.is_open()) {
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

bool copy_file(const std::string_view source, const std::string_view destination, const bool overwrite) {
    std::error_code ec;
    const auto options = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;

    return fs::copy_file(fs::path(source), fs::path(destination), options, ec);
}

bool move_file(const std::string_view source, const std::string_view destination) {
    std::error_code ec;
    fs::rename(fs::path(source), fs::path(destination), ec);
    return !ec;
}

bool delete_file(const std::string_view path) {
    std::error_code ec;
    return fs::remove(fs::path(path), ec);
}

bool file_exists(const std::string_view path) {
    return path_exists(path) && is_file(path);
}

std::optional<uintmax_t> get_file_size(const std::string_view path) {
    return file_size(path);
}

std::optional<std::string> get_file_extension(std::string_view path) {
    std::string ext = get_extension(path);
    return ext.empty() ? std::nullopt : std::optional(ext);
}

bool is_readable(const std::string_view path) {
    const std::ifstream file{std::string(path)};
    return file.good();
}

bool is_writable(std::string_view path) {
    if (!file_exists(path)) {
        std::ofstream file{std::string(path)};
        bool writable = file.good();
        file.close();
        if (writable) {
            delete_file(path);
        }
        return writable;
    }

    std::ofstream file(std::string(path), std::ios::app);
    return file.good();
}

std::optional<std::vector<char>> read_binary_file(const std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary);

    if (!file.is_open()) {
        return std::nullopt;
    }

    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        return std::nullopt;
    }

    return buffer;
}

bool write_binary_file(const std::string_view path, const std::vector<char>& data) {
    std::ofstream file(std::string(path), std::ios::binary);

    if (!file.is_open()) {
        return false;
    }

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return file.good();
}

bool create_empty_file(const std::string_view path) {
    const std::ofstream file{std::string(path)};
    return file.good();
}

std::optional<std::string> read_file_chunk(const std::string_view path, const size_t offset, const size_t size) {
    std::ifstream file(std::string(path), std::ios::binary);

    if (!file.is_open()) {
        return std::nullopt;
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

    if (!file.good()) {
        return std::nullopt;
    }

    std::string buffer(size, '\0');
    file.read(buffer.data(), static_cast<std::streamsize>(size));

    if (file.gcount() > 0) {
        buffer.resize(static_cast<size_t>(file.gcount()));
        return buffer;
    }

    return std::nullopt;
}

FileReader::FileReader(std::string_view path)
    : stream_(std::string(path)) {
}

FileReader::~FileReader() {
    close();
}

FileReader::FileReader(FileReader&& other) noexcept
    : stream_(std::move(other.stream_)) {
}

FileReader& FileReader::operator=(FileReader&& other) noexcept {
    if (this != &other) {
        close();
        stream_ = std::move(other.stream_);
    }
    return *this;
}

bool FileReader::is_open() const {
    return stream_.is_open();
}

std::optional<std::string> FileReader::read_line() {
    if (!is_open()) {
        return std::nullopt;
    }

    if (std::string line; std::getline(stream_, line)) {
        return line;
    }

    return std::nullopt;
}

std::optional<std::string> FileReader::read_all() const
{
    if (!is_open()) {
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << stream_.rdbuf();
    return ss.str();
}

std::optional<std::vector<char>> FileReader::read_bytes(const size_t count) {
    if (!is_open()) {
        return std::nullopt;
    }

    std::vector<char> buffer(count);
    stream_.read(buffer.data(), static_cast<std::streamsize>(count));

    if (stream_.gcount() > 0) {
        buffer.resize(stream_.gcount());
        return buffer;
    }

    return std::nullopt;
}

bool FileReader::eof() const {
    return stream_.eof();
}

void FileReader::close() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

FileWriter::FileWriter(const std::string_view path, const bool append) {
    auto mode = std::ios::out;
    if (append) {
        mode |= std::ios::app;
    }
    stream_.open(std::string(path), mode);
}

FileWriter::~FileWriter() {
    close();
}

FileWriter::FileWriter(FileWriter&& other) noexcept
    : stream_(std::move(other.stream_)) {
}

FileWriter& FileWriter::operator=(FileWriter&& other) noexcept {
    if (this != &other) {
        close();
        stream_ = std::move(other.stream_);
    }
    return *this;
}

bool FileWriter::is_open() const {
    return stream_.is_open();
}

bool FileWriter::write(const std::string_view content) {
    if (!is_open()) {
        return false;
    }

    stream_.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream_.good();
}

bool FileWriter::write_line(const std::string_view line) {
    if (!is_open()) {
        return false;
    }

    stream_ << line << '\n';
    return stream_.good();
}

bool FileWriter::flush() {
    if (!is_open()) {
        return false;
    }

    stream_.flush();
    return stream_.good();
}

void FileWriter::close() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

} // namespace bha::utils