#include "bha/refactor/pimpl_tooling.hpp"

#include <string>
#include <utility>

#ifndef BHA_HAVE_CLANG_TOOLING
#define BHA_HAVE_CLANG_TOOLING 0
#endif

namespace bha::refactor {
    namespace {
        void add_diagnostic(
            Result& result,
            const DiagnosticSeverity severity,
            std::string message
        ) {
            result.diagnostics.push_back({
                .severity = severity,
                .message = std::move(message),
                .file = {},
                .line = 0
            });
        }
    }

    bool clang_tooling_available() noexcept {
#if BHA_HAVE_CLANG_TOOLING
        return true;
#else
        return false;
#endif
    }

    Result run_pimpl_refactor_with_clang_tooling(const PimplRequest& request) {
        Result result;
        result.refactor_type = "pimpl";
        result.engine = "clang-libtooling-disabled";
        result.summary.class_name = request.class_name;
        result.allow_fallback = false;

#if BHA_HAVE_CLANG_TOOLING
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "The AST-only PIMPL backend does not emit structural replacements yet; refusing to apply a partial refactor"
        );
#else
        add_diagnostic(
            result,
            DiagnosticSeverity::Error,
            "Clang LibTooling is required for PIMPL refactoring; no text-edit fallback is available"
        );
#endif
        return result;
    }

}  // namespace bha::refactor
