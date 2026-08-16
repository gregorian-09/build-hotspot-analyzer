#include "bha/build_systems/adapter.hpp"
#include "bha/build_systems/adapter_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <thread>
#include <tuple>

namespace bha::build_systems {
    using namespace detail;

    // Unreal Adapter
    // --------------------------------------------------------------------------

    class UnrealAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "Unreal"; }

        [[nodiscard]] std::string description() const override {
            return "Unreal Build Tool adapter (.uproject / ModuleRules / TargetRules)";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            if (find_unreal_uproject(project_path).has_value()) {
                return 0.98;
            }
            if (has_unreal_build_markers(project_path)) {
                return 0.7;
            }
            return 0.0;
        }

        Result<void, Error> configure(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)project_path;
            (void)options;
            return Result<void, Error>::success();
        }

        Result<BuildResult, Error> build(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            BuildResult result;
            const auto start = std::chrono::steady_clock::now();

            const auto uproject = find_unreal_uproject(project_path);
            if (!uproject.has_value()) {
                result.error_message = "No .uproject file found at project root";
                return Result<BuildResult, Error>::success(result);
            }

            const std::string target_name = select_unreal_target_name(project_path, *uproject);
            if (target_name.empty()) {
                result.error_message = "Could not determine Unreal target name from .Target.cs files";
                return Result<BuildResult, Error>::success(result);
            }

            const std::string platform = unreal_platform_name();
            const std::string configuration = unreal_configuration_from_build_type(options.build_type);

            if (options.clean_first) {
                if (auto clean_result = clean(project_path, options); clean_result.is_err()) {
                    result.error_message = clean_result.error().message();
                    return Result<BuildResult, Error>::success(result);
                }
            }

            std::ostringstream cmd;
            if (const auto build_script = resolve_unreal_build_script(); build_script.has_value()) {
#ifdef _WIN32
                cmd << "\"" << build_script->string() << "\"";
                cmd << " \"" << target_name << "\"";
                cmd << " " << platform;
                cmd << " " << configuration;
                cmd << " -Project=\"" << uproject->string() << "\"";
#else
                cmd << shell_escape_posix(build_script->string());
                cmd << " " << shell_escape_posix(target_name);
                cmd << " " << shell_escape_posix(platform);
                cmd << " " << shell_escape_posix(configuration);
                cmd << " -Project=" << shell_escape_posix(uproject->string());
#endif
            } else {
                cmd << "UnrealBuildTool";
                cmd << " " << target_name;
                cmd << " " << platform;
                cmd << " " << configuration;
                cmd << " -Project=\"" << uproject->string() << "\"";
            }

            cmd << " -NoHotReload";
            cmd << " -Progress";
            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            const auto [exit_code, output] = execute_command(
                cmd.str(),
                project_path,
                options.on_output_line,
                options.should_cancel
            );
            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                const std::string error_summary = extract_error_summary(output);
                if (error_summary.empty()) {
                    result.error_message = "Unreal build failed";
                } else {
                    result.error_message = error_summary;
                }
            }

            result.trace_files = find_trace_files(project_path / "Saved");
            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            const auto uproject = find_unreal_uproject(project_path);
            if (!uproject.has_value()) {
                return Result<void, Error>::success();
            }

            const std::string target_name = select_unreal_target_name(project_path, *uproject);
            if (target_name.empty()) {
                return Result<void, Error>::success();
            }

            const std::string platform = unreal_platform_name();
            const std::string configuration = unreal_configuration_from_build_type(options.build_type);

            if (const auto build_script = resolve_unreal_build_script(); build_script.has_value()) {
                std::ostringstream cmd;
#ifdef _WIN32
                cmd << "\"" << build_script->string() << "\"";
                cmd << " \"" << target_name << "\"";
                cmd << " " << platform;
                cmd << " " << configuration;
                cmd << " -Project=\"" << uproject->string() << "\"";
#else
                cmd << shell_escape_posix(build_script->string());
                cmd << " " << shell_escape_posix(target_name);
                cmd << " " << shell_escape_posix(platform);
                cmd << " " << shell_escape_posix(configuration);
                cmd << " -Project=" << shell_escape_posix(uproject->string());
#endif
                cmd << " -clean";

                if (const auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                    return Result<void, Error>::failure(
                        Error(ErrorCode::InternalError, "Unreal clean failed: " + output)
                    );
                }
            }

            return Result<void, Error>::success();
        }

        Result<fs::path, Error> get_compile_commands(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;
            const fs::path compile_commands = project_path / "compile_commands.json";
            if (fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }
            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound, "compile_commands.json not found for Unreal project")
            );
        }
    };

    // --------------------------------------------------------------------------

    void register_unreal_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<UnrealAdapter>()
        );
    }

}  // namespace bha::build_systems
