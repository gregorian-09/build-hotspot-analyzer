#ifndef BHA_FORWARD_DECL_SEMANTIC_INDEX_HPP
#define BHA_FORWARD_DECL_SEMANTIC_INDEX_HPP

#include "bha/project_index.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace bha::suggestions {

    struct ForwardDeclSemanticUse {
        fs::path source_file;
        bool requires_complete_type = false;
        bool in_dependent_context = false;
        bool through_alias = false;
        bool through_template = false;
        bool macro_expanded = false;
    };

    struct ForwardDeclSemanticRecord {
        fs::path declaration_file;
        std::string qualified_name;
        std::string keyword;
        bool complete_definition = false;
        bool macro_generated = false;
        bool template_declaration = false;
        bool unsupported_scope = false;
        std::vector<ForwardDeclSemanticUse> uses;
    };

    struct ForwardDeclSemanticInclude {
        fs::path including_file;
        fs::path included_file;
        std::size_t offset = 0;
        std::size_t length = 0;
        std::size_t line = 0;
        std::size_t col_start = 0;
        std::size_t col_end = 0;
    };

    struct ForwardDeclSemanticResult {
        std::vector<ForwardDeclSemanticRecord> records;
        std::vector<ForwardDeclSemanticInclude> includes;
        std::string diagnostic;
        bool available = false;
    };

    [[nodiscard]] ForwardDeclSemanticResult analyze_forward_declarations(
        ProjectIndex& project_index,
        const fs::path& header,
        const std::vector<CompilationUnit>& commands
    );

    [[nodiscard]] bool validate_forward_decl_replacements(
        ProjectIndex& project_index,
        const std::vector<CompilationUnit>& commands,
        const std::vector<ForwardDeclSemanticInclude>& includes,
        std::string_view replacement_text,
        std::string& diagnostic
    );

}  // namespace bha::suggestions

#endif  // BHA_FORWARD_DECL_SEMANTIC_INDEX_HPP
