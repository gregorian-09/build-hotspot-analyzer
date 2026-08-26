#ifndef BHA_TEMPLATE_SEMANTIC_INDEX_HPP
#define BHA_TEMPLATE_SEMANTIC_INDEX_HPP

#include "bha/project_index.hpp"

#include <string>
#include <string_view>
#include <vector>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

namespace bha::suggestions {

    struct TemplateSemanticUse {
        fs::path source_file;
        std::string kind;
        bool requires_complete_type = false;
        bool in_dependent_context = false;
    };

    enum class TemplateSemanticStatus {
        Unavailable,
        NoCompilationDatabase,
        Parsed,
        Failed
    };

    struct TemplateSemanticRecord {
        std::string template_name;
        std::string specialization;
        std::string specialization_kind;
        std::string declaration_kind;
        std::string canonical_extern_declaration;
        std::string canonical_explicit_definition;
        fs::path source_file;
        fs::path declaration_file;
        std::size_t declaration_line = 0;
        std::size_t declaration_column = 0;
        std::size_t declaration_end_line = 0;
        std::size_t declaration_end_column = 0;
        std::size_t declaration_end_offset = 0;
        std::vector<fs::path> use_files;
        std::vector<TemplateSemanticUse> uses;
        std::vector<fs::path> explicit_definition_files;
        bool complete_definition = false;
        bool has_explicit_instantiation = false;
        bool has_explicit_instantiation_declaration = false;
        bool has_external_linkage = false;
        bool has_single_explicit_definition = false;
        bool has_dependent_arguments = false;
        bool has_dependent_use_context = false;
        bool has_unsupported_scope = false;
        bool has_unsupported_function_form = false;
        bool has_unsupported_variable_form = false;
        bool has_declaration_identity_conflict = false;
    };

    /**
     * Builds reusable semantic facts from Clang ASTs using project compile commands.
     * This index is intentionally separate from trace ranking and edit generation.
     */
    class TemplateSemanticIndex {
    public:
        explicit TemplateSemanticIndex(ProjectIndex& project_index);

        void build();

        [[nodiscard]] TemplateSemanticStatus status() const noexcept;
        [[nodiscard]] const std::string& diagnostic() const noexcept;
        [[nodiscard]] const std::vector<TemplateSemanticRecord>& records() const noexcept;
        [[nodiscard]] const TemplateSemanticRecord* find_exact(
            std::string_view specialization
        ) const noexcept;

    private:
#if BHA_HAVE_CLANG_TOOLING
        ProjectIndex& project_index_;
#endif
        TemplateSemanticStatus status_ = TemplateSemanticStatus::Unavailable;
        std::string diagnostic_;
        std::vector<TemplateSemanticRecord> records_;
    };

}  // namespace bha::suggestions

#endif  // BHA_TEMPLATE_SEMANTIC_INDEX_HPP
