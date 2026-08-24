#include "bha/build_systems/adapter.hpp"
#include "bha/build_systems/adapter_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tuple>

namespace bha::build_systems {
    using namespace detail;

    // XCode Adapter
    // --------------------------------------------------------------------------

    class XCodeAdapter final : public IBuildSystemAdapter {
    public:
        [[nodiscard]] std::string name() const override { return "XCode"; }

        [[nodiscard]] std::string description() const override {
            return "Apple Xcode build system adapter";
        }

        [[nodiscard]] double detect(const fs::path& project_path) const override {
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".xcodeproj") {
                    return 0.95;
                }
                if (entry.path().extension() == ".xcworkspace") {
                    return 0.95;
                }
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
            auto start = std::chrono::steady_clock::now();

            fs::path project_file;
            fs::path workspace_file;
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".xcworkspace") {
                    workspace_file = entry.path();
                    break;
                }
                if (entry.path().extension() == ".xcodeproj") {
                    project_file = entry.path();
                }
            }

            if (workspace_file.empty() && project_file.empty()) {
                result.error_message = "No Xcode project or workspace found";
                return Result<BuildResult, Error>::success(result);
            }

            if (options.clean_first) {
                clean(project_path, options);
            }

            std::ostringstream cmd;
            cmd << "xcodebuild";

            if (!workspace_file.empty()) {
                cmd << " -workspace \"" << workspace_file.string() << "\"";
                cmd << " -scheme \"" << workspace_file.stem().string() << "\"";
            } else {
                cmd << " -project \"" << project_file.string() << "\"";
            }

            cmd << " -configuration " << options.build_type;
            cmd << " -jobs " << (options.parallel_jobs > 0 ? options.parallel_jobs : get_cpu_count());

            if (options.enable_tracing) {
                cmd << " -enableBuildTimingTracing YES";
            }

            if (!options.verbose) {
                cmd << " -quiet";
            }

            for (const auto& arg : options.extra_args) {
                cmd << " " << arg;
            }

            auto [exit_code, output] = execute_command(
                cmd.str(),
                project_path,
                options.on_output_line,
                options.should_cancel
            );

            result.output = output;
            result.success = (exit_code == 0);

            if (!result.success) {
                const std::string error_summary = extract_error_summary(output);
                result.error_message = error_summary.empty() ? "Build failed" : error_summary;
            }

            result.trace_files = find_trace_files(project_path / "build");

            result.build_time = std::chrono::duration_cast<Duration>(std::chrono::steady_clock::now() - start);
            return Result<BuildResult, Error>::success(result);
        }

        Result<void, Error> clean(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            fs::path project_file;
            fs::path workspace_file;
            for (const auto& entry : fs::directory_iterator(project_path)) {
                if (entry.path().extension() == ".xcworkspace") {
                    workspace_file = entry.path();
                    break;
                }
                if (entry.path().extension() == ".xcodeproj") {
                    project_file = entry.path();
                }
            }

            std::ostringstream cmd;
            cmd << "xcodebuild clean";

            if (!workspace_file.empty()) {
                cmd << " -workspace \"" << workspace_file.string() << "\"";
                cmd << " -scheme \"" << workspace_file.stem().string() << "\"";
            } else if (!project_file.empty()) {
                cmd << " -project \"" << project_file.string() << "\"";
            }

            cmd << " -configuration " << options.build_type;

            if (auto [exit_code, output] = execute_command(cmd.str(), project_path); exit_code != 0) {
                return Result<void, Error>::failure(
                    Error(ErrorCode::InternalError, "Clean failed: " + output)
                );
            }

            return Result<void, Error>::success();
        }

        Result<fs::path, Error> get_compile_commands(
            const fs::path& project_path,
            const BuildOptions& options
        ) override {
            (void)options;

            if (const fs::path compile_commands = project_path / "compile_commands.json"; fs::exists(compile_commands)) {
                return Result<fs::path, Error>::success(compile_commands);
            }

            return Result<fs::path, Error>::failure(
                Error(ErrorCode::NotFound,
                      "compile_commands.json not found. Use xcpretty or XcodeGen to generate it.")
            );
        }
    };

    // --------------------------------------------------------------------------
    // Registration functions
    // --------------------------------------------------------------------------


    void register_xcode_adapter() {
        BuildSystemRegistry::instance().register_adapter(
            std::make_unique<XCodeAdapter>()
        );
    }

}  // namespace bha::build_systems
