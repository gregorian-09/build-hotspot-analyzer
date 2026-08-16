    enum class IncludeInsertionKind {
        ExistingIncludeBlock,
        HeaderGuard,
        LeadingPreamble,
        StartOfFile
    };

    struct IncludeInsertionEdit {
        TextEdit edit;
        std::size_t inserted_line_one_based = 1;
        IncludeInsertionKind kind = IncludeInsertionKind::StartOfFile;
    };

    class GeneratedTextBuilder {
    public:
        void add_line(std::string line) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines_.push_back(std::move(line));
        }

        void add_blank_line() {
            if (lines_.empty() || lines_.back().empty()) {
                return;
            }
            lines_.emplace_back();
        }

        void add_block(std::string_view block) {
            std::size_t offset = 0;
            while (offset <= block.size()) {
                const auto next = block.find('\n', offset);
                const auto chunk = next == std::string_view::npos
                    ? block.substr(offset)
                    : block.substr(offset, next - offset);
                if (chunk.empty()) {
                    add_blank_line();
                } else {
                    add_line(std::string(chunk));
                }
                if (next == std::string_view::npos) {
                    break;
                }
                offset = next + 1;
            }
        }

        [[nodiscard]] bool empty() const {
            return lines_.empty();
        }

        [[nodiscard]] std::string str() const {
            std::ostringstream out;
            for (std::size_t i = 0; i < lines_.size(); ++i) {
                out << lines_[i];
                if (i + 1 < lines_.size() || !lines_.empty()) {
                    out << '\n';
                }
            }
            return out.str();
        }

    private:
        std::vector<std::string> lines_;
    };

    inline void append_include_block(
        GeneratedTextBuilder& builder,
        const std::vector<std::string>& include_lines
    ) {
        if (include_lines.empty()) {
            return;
        }
        for (const auto& include_line : include_lines) {
            builder.add_line(include_line);
        }
        builder.add_blank_line();
    }

    [[nodiscard]] inline std::string format_separated_block(std::string block) {
        block = trim_whitespace_copy(std::move(block));
        if (block.empty()) {
            return {};
        }
        return "\n" + block + "\n";
    }

    [[nodiscard]] inline std::string_view trim_whitespace_view(const std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return std::string_view{};
        }
        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    [[nodiscard]] inline std::string_view consume_leading_block_comments(
        std::string_view text,
        bool& in_block_comment
    ) {
        std::string_view current = text;
        while (true) {
            if (in_block_comment) {
                const auto end = current.find("*/");
                if (end == std::string_view::npos) {
                    return std::string_view{};
                }
                current.remove_prefix(end + 2);
                in_block_comment = false;
            }

            current = trim_whitespace_view(current);
            if (!current.starts_with("/*")) {
                return current;
            }

            current.remove_prefix(2);
            const auto end = current.find("*/");
            if (end == std::string_view::npos) {
                in_block_comment = true;
                return std::string_view{};
            }
            current.remove_prefix(end + 2);
        }
    }

    [[nodiscard]] inline std::size_t find_first_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) {
            return 0;
        }
        return directives.front().line;
    }

    [[nodiscard]] inline std::size_t find_last_include_line(const fs::path& file) {
        const auto directives = find_include_directives(file);
        if (directives.empty()) {
            return 0;
        }
        return directives.back().line;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_pragma_once_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        std::string line;
        std::size_t line_num = 0;
        const std::regex pragma_regex(R"(^\s*#\s*pragma\s+once\b)");
        while (std::getline(in, line)) {
            if (std::regex_search(line, pragma_regex)) {
                return line_num;
            }
            ++line_num;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_include_insertion_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        const std::regex include_regex(R"(^#\s*include\s*([<"])([^">]+)[">])", std::regex::ECMAScript);
        const std::regex pragma_regex(R"(^#\s*pragma\s+once\b)", std::regex::ECMAScript);
        const std::regex guard_ifndef_regex(R"(^#\s*ifndef\b)", std::regex::ECMAScript);
        const std::regex guard_if_defined_regex(R"(^#\s*if\s*!?\s*defined\s*\()", std::regex::ECMAScript);
        const std::regex define_regex(R"(^#\s*define\b)", std::regex::ECMAScript);

        std::string line;
        std::size_t line_num = 0;
        bool in_block_comment = false;
        bool in_include_block = false;
        bool expecting_guard_define = false;
        std::optional<std::size_t> pragma_once_line;
        std::optional<std::size_t> last_include_line;

        while (std::getline(in, line)) {
            std::string_view trimmed = consume_leading_block_comments(line, in_block_comment);
            if (trimmed.empty() || trimmed.starts_with("//")) {
                ++line_num;
                continue;
            }

            if (std::regex_search(trimmed.begin(), trimmed.end(), pragma_regex) && !in_include_block &&
                !last_include_line.has_value()) {
                pragma_once_line = line_num;
                ++line_num;
                continue;
            }

            if (std::regex_search(trimmed.begin(), trimmed.end(), guard_ifndef_regex) ||
                std::regex_search(trimmed.begin(), trimmed.end(), guard_if_defined_regex)) {
                if (!in_include_block && !last_include_line.has_value()) {
                    expecting_guard_define = true;
                    ++line_num;
                    continue;
                }
                break;
            }

            if (expecting_guard_define && std::regex_search(trimmed.begin(), trimmed.end(), define_regex)) {
                expecting_guard_define = false;
                ++line_num;
                continue;
            }

            if (std::regex_search(trimmed.begin(), trimmed.end(), include_regex)) {
                in_include_block = true;
                last_include_line = line_num;
                ++line_num;
                continue;
            }

            if (in_include_block) {
                break;
            }

            if (trimmed.starts_with("#")) {
                break;
            }
            break;
        }

        if (last_include_line.has_value()) {
            return last_include_line;
        }
        return pragma_once_line;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_header_guard_define_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        const std::regex ifndef_regex(R"(^\s*#\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
        const std::regex define_regex(R"(^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b)");

        std::string line;
        std::size_t line_num = 0;
        std::string guard_macro;
        bool found_ifndef = false;
        bool in_block_comment = false;

        while (std::getline(in, line)) {
            const std::string_view trimmed = consume_leading_block_comments(line, in_block_comment);
            std::match_results<std::string_view::const_iterator> match;

            if (!found_ifndef) {
                if (trimmed.empty() || trimmed.starts_with("//")) {
                    ++line_num;
                    continue;
                }
                if (std::regex_match(trimmed.begin(), trimmed.end(), match, ifndef_regex)) {
                    guard_macro = match[1].str();
                    found_ifndef = true;
                } else if (!trimmed.empty() && !trimmed.starts_with("#pragma once")) {
                    break;
                }
                ++line_num;
                continue;
            }

            if (trimmed.empty() || trimmed.starts_with("//")) {
                ++line_num;
                continue;
            }
            if (std::regex_match(trimmed.begin(), trimmed.end(), match, define_regex)) {
                if (match[1].str() == guard_macro) {
                    return line_num;
                }
                break;
            }
            ++line_num;
        }

        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::size_t> find_preferred_include_insertion_line(const fs::path& file) {
        if (auto include_line = find_include_insertion_line(file)) {
            return include_line;
        }
        return find_header_guard_define_line(file);
    }

    [[nodiscard]] inline std::optional<std::size_t> find_leading_preamble_line(const fs::path& file) {
        std::ifstream in(file);
        if (!in) {
            return std::nullopt;
        }

        std::string line;
        std::size_t line_num = 0;
        bool in_block_comment = false;
        std::optional<std::size_t> last_preamble_line;

        while (std::getline(in, line)) {
            std::string_view trimmed = consume_leading_block_comments(line, in_block_comment);
            if (trimmed.empty() || trimmed.starts_with("//")) {
                last_preamble_line = line_num;
                ++line_num;
                continue;
            }
            break;
        }

        return last_preamble_line;
    }

    [[nodiscard]] inline IncludeInsertionEdit make_preferred_include_insertion_edit(
        const fs::path& file,
        const std::string& content
    ) {
        if (auto line = find_preferred_include_insertion_line(file)) {
            auto edit = make_insert_after_line_edit(file, *line, content);
            if (find_include_directives(file).empty()) {
                edit.new_text = "\n" + content + "\n";
                return {std::move(edit), *line + 3, IncludeInsertionKind::HeaderGuard};
            }
            return {std::move(edit), *line + 2, IncludeInsertionKind::ExistingIncludeBlock};
        }
        if (auto line = find_leading_preamble_line(file)) {
            auto edit = make_insert_after_line_edit(file, *line, content);
            edit.new_text = "\n" + content + "\n";
            return {std::move(edit), *line + 3, IncludeInsertionKind::LeadingPreamble};
        }
        return {make_insert_at_start_edit(file, content), 1, IncludeInsertionKind::StartOfFile};
    }

    [[nodiscard]] inline double saturating_count_factor(
        const std::size_t count,
        const std::size_t saturation_count
    ) {
        if (count == 0) {
            return 0.0;
        }
        if (saturation_count <= 1) {
            return 1.0;
        }

        const double numerator = std::log1p(static_cast<double>(count));
        const double denominator = std::log1p(static_cast<double>(saturation_count));
        if (denominator <= 0.0) {
            return 1.0;
        }
        return std::clamp(numerator / denominator, 0.0, 1.0);
    }

    [[nodiscard]] inline Duration scaled_duration(
        const Duration duration,
        const double factor
    ) {
        if (duration <= Duration::zero() || factor <= 0.0) {
            return Duration::zero();
        }

        const auto scaled = static_cast<Duration::rep>(
            static_cast<double>(duration.count()) * factor
        );
        return Duration(std::max<Duration::rep>(0, scaled));
    }

    [[nodiscard]] inline std::size_t end_of_file_insert_line(const std::string& content) {
        return static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n')) + 1;
    }

    [[nodiscard]] inline bool has_proven_file_scope_at_eof(const std::string& content) {
        enum class LexState {
            Code,
            LineComment,
            BlockComment,
            StringLiteral,
            CharLiteral,
            RawString
        };

        const auto is_escaped = [&](const std::size_t index) {
            if (index == 0 || index > content.size()) {
                return false;
            }
            std::size_t backslash_count = 0;
            for (std::size_t cursor = index; cursor > 0; --cursor) {
                if (content[cursor - 1] != '\\') {
                    break;
                }
                ++backslash_count;
            }
            return (backslash_count % 2) == 1;
        };

        LexState state = LexState::Code;
        std::string raw_terminator;
        int brace_depth = 0;

        for (std::size_t i = 0; i < content.size(); ++i) {
            const char ch = content[i];

            switch (state) {
                case LexState::LineComment:
                    if (ch == '\n') {
                        state = LexState::Code;
                    }
                    continue;
                case LexState::BlockComment:
                    if (ch == '*' && i + 1 < content.size() && content[i + 1] == '/') {
                        state = LexState::Code;
                        ++i;
                    }
                    continue;
                case LexState::StringLiteral:
                    if (ch == '"' && !is_escaped(i)) {
                        state = LexState::Code;
                    }
                    continue;
                case LexState::CharLiteral:
                    if (ch == '\'' && !is_escaped(i)) {
                        state = LexState::Code;
                    }
                    continue;
                case LexState::RawString:
                    if (!raw_terminator.empty() &&
                        i + raw_terminator.size() <= content.size() &&
                        content.compare(i, raw_terminator.size(), raw_terminator) == 0) {
                        state = LexState::Code;
                        i += raw_terminator.size() - 1;
                    }
                    continue;
                case LexState::Code:
                    break;
            }

            if (ch == '/' && i + 1 < content.size()) {
                if (content[i + 1] == '/') {
                    state = LexState::LineComment;
                    ++i;
                    continue;
                }
                if (content[i + 1] == '*') {
                    state = LexState::BlockComment;
                    ++i;
                    continue;
                }
            }

            if (ch == 'R' && i + 1 < content.size() && content[i + 1] == '"') {
                const auto delimiter_end = content.find('(', i + 2);
                if (delimiter_end != std::string::npos) {
                    raw_terminator = ")" + content.substr(i + 2, delimiter_end - (i + 2)) + "\"";
                    state = LexState::RawString;
                    i = delimiter_end;
                    continue;
                }
            }

            if (ch == '"') {
                state = LexState::StringLiteral;
                continue;
            }
            if (ch == '\'') {
                state = LexState::CharLiteral;
                continue;
            }
            if (ch == '{') {
                ++brace_depth;
                continue;
            }
            if (ch == '}') {
                if (brace_depth == 0) {
                    return false;
                }
                --brace_depth;
            }
        }

        return state == LexState::Code && brace_depth == 0;
    }
