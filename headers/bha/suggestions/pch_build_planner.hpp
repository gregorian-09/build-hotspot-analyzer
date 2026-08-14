#ifndef BHA_PCH_BUILD_PLANNER_HPP
#define BHA_PCH_BUILD_PLANNER_HPP

#include "bha/types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace bha::suggestions {

    struct CMakePCHTargetPlan {
        fs::path file;
        std::string content;
        std::string target_name;
        std::size_t target_start_line = 0;
        fs::path pch_path;
    };

    struct PCHBuildPlan {
        std::vector<TextEdit> edits;
        std::vector<FileTarget> files;
    };

    [[nodiscard]] PCHBuildPlan plan_cmake_pch_edits(const CMakePCHTargetPlan& plan);

    [[nodiscard]] PCHBuildPlan plan_msbuild_pch_edits(
        const fs::path& project_root,
        const fs::path& pch_path,
        const std::function<bool()>& should_cancel = {}
    );

}  // namespace bha::suggestions

#endif  // BHA_PCH_BUILD_PLANNER_HPP
