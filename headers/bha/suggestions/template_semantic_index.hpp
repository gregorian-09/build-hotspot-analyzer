#ifndef BHA_TEMPLATE_SEMANTIC_INDEX_HPP
#define BHA_TEMPLATE_SEMANTIC_INDEX_HPP

#include "bha/project_index.hpp"

#include <string>
#include <vector>

namespace bha::suggestions {

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
        fs::path source_file;
        fs::path declaration_file;
        bool complete_definition = false;
        bool has_explicit_instantiation = false;
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

    private:
        ProjectIndex& project_index_;
        TemplateSemanticStatus status_ = TemplateSemanticStatus::Unavailable;
        std::string diagnostic_;
        std::vector<TemplateSemanticRecord> records_;
    };

}  // namespace bha::suggestions

#endif  // BHA_TEMPLATE_SEMANTIC_INDEX_HPP
