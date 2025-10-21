//
// Created by gregorian on 21/10/2025.
//

#include "bha/build_systems/build_adapter.h"
#include "bha/build_systems/cmake_adapter.h"
#include "bha/build_systems/ninja_adapter.h"
#include "bha/build_systems/make_adapter.h"
#include "bha/build_systems/msbuild_adapter.h"

namespace bha::build_systems {

    core::Result<std::unique_ptr<BuildAdapter>> BuildAdapterFactory::create_adapter(
        const std::string& build_dir
    ) {
        auto type_result = detect_build_system_type(build_dir);
        if (!type_result.is_success()) {
            return core::Result<std::unique_ptr<BuildAdapter>>::failure(
                type_result.error()
            );
        }

        switch (type_result.value()) {
            case BuildSystemType::CMAKE:
                return core::Result<std::unique_ptr<BuildAdapter>>::success(
                    std::make_unique<CMakeAdapter>(build_dir)
                );

            case BuildSystemType::NINJA:
                return core::Result<std::unique_ptr<BuildAdapter>>::success(
                    std::make_unique<NinjaAdapter>(build_dir)
                );

            case BuildSystemType::MAKE:
                return core::Result<std::unique_ptr<BuildAdapter>>::success(
                    std::make_unique<MakeAdapter>(build_dir)
                );

            case BuildSystemType::MSBUILD:
                return core::Result<std::unique_ptr<BuildAdapter>>::success(
                    std::make_unique<MSBuildAdapter>(build_dir)
                );

            default:
                return core::Result<std::unique_ptr<BuildAdapter>>::failure(
                    core::Error(
                        core::ErrorCode::UNSUPPORTED_FORMAT,
                        "Unknown build system type"
                    )
                );
        }
    }

    core::Result<BuildSystemType> BuildAdapterFactory::detect_build_system_type(
        const std::string& build_dir
    ) {
        namespace fs = std::filesystem;
        const fs::path dir(build_dir);

        if (fs::exists(dir / "compile_commands.json") &&
            fs::exists(dir / "CMakeCache.txt")) {
            return core::Result<BuildSystemType>::success(BuildSystemType::CMAKE);
        }

        if (fs::exists(dir / "build.ninja") ||
            fs::exists(dir / ".ninja_log")) {
            return core::Result<BuildSystemType>::success(BuildSystemType::NINJA);
        }

        if (fs::exists(dir / "Makefile") ||
            fs::exists(dir / "makefile")) {
            return core::Result<BuildSystemType>::success(BuildSystemType::MAKE);
        }

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".sln") {
                return core::Result<BuildSystemType>::success(BuildSystemType::MSBUILD);
            }
        }

        return core::Result<BuildSystemType>::failure(
            core::Error(
                core::ErrorCode::FILE_NOT_FOUND,
                "Could not detect build system in directory: " + build_dir
            )
        );
    }

}