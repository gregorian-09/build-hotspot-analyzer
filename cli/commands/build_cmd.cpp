#include "bha/cli/commands/command.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <iostream>
#include <filesystem>

namespace bha::cli
{
    namespace fs = std::filesystem;

    class BuildCommand final : public Command {
    public:
        [[nodiscard]] std::string_view name() const noexcept override {
            return "build";
        }

        [[nodiscard]] std::string_view description() const noexcept override {
            return "Build project with time tracing and optional memory profiling";
        }

        [[nodiscard]] std::string usage() const override {
            return "Usage: bha build [OPTIONS]\n"
                   "\n"
                   "Examples:\n"
                   "  bha build\n"
                   "  bha build --memory --analyze\n"
                   "  bha build --build-system cmake --config Debug\n"
                   "  bha build --clean --output traces/";
        }

        [[nodiscard]] std::vector<ArgDef> arguments() const override {
            return {
                {"build-system", 's', "Force specific build system (cmake, ninja, make, msbuild)", false, true, "", "SYSTEM"},
                {"config", 'c', "Build configuration (Debug, Release, etc.)", false, true, "Release", "CONFIG"},
                {"jobs", 'j', "Number of parallel jobs (0=auto)", false, true, "0", "N"},
                {"memory", 'm', "Enable memory profiling", false, false, "", ""},
                {"analyze", 'a', "Run analysis after build", false, false, "", ""},
                {"clean", 0, "Clean before build", false, false, "", ""},
                {"output", 'o', "Directory for trace files", false, true, "", "DIR"},
                {"compiler", 0, "Compiler to use", false, true, "", "COMPILER"},
            };
        }

        [[nodiscard]] std::string validate(const ParsedArgs&) const override {
            return "";
        }

        [[nodiscard]] int execute(const ParsedArgs& args) override {
            if (args.get_flag("help")) {
                print_help();
                return 0;
            }

            if (args.get_flag("verbose")) {
                set_verbosity(Verbosity::Verbose);
            } else if (args.get_flag("quiet")) {
                set_verbosity(Verbosity::Quiet);
            }

            fs::path project_path = fs::current_path();

            auto& registry = build_systems::BuildSystemRegistry::instance();
            build_systems::IBuildSystemAdapter* adapter = nullptr;

            if (std::string system_name = args.get_or("build-system", ""); !system_name.empty()) {
                adapter = registry.get(system_name);
                if (!adapter) {
                    print_error("Unknown build system: " + system_name);
                    print_error("Available: cmake, ninja, make, msbuild");
                    return 1;
                }
            } else {
                adapter = registry.detect(project_path);
                if (!adapter) {
                    print_error("Could not detect build system in current directory");
                    print_error("Use --build-system to specify manually");
                    return 1;
                }
                print_verbose("Detected build system: " + adapter->name());
            }

            build_systems::BuildOptions options;
            options.build_type = args.get_or("config", "Release");
            options.parallel_jobs = args.get_int("jobs").value_or(0);
            options.enable_tracing = true;
            options.enable_memory_profiling = args.get_flag("memory");
            options.clean_first = args.get_flag("clean");
            options.verbose = args.get_flag("verbose");

            if (std::string compiler = args.get_or("compiler", ""); !compiler.empty()) {
                options.compiler = compiler;
            }

            if (std::string output = args.get_or("output", ""); !output.empty()) {
                options.build_dir = fs::path(output);
            }

            print_verbose("Configuring project...");
            if (auto configure_result = adapter->configure(project_path, options); !configure_result.is_ok()) {
                print_error("Configuration failed: " + configure_result.error().message());
                return 1;
            }

            print_verbose("Building project...");
            auto build_result = adapter->build(project_path, options);
            if (!build_result.is_ok()) {
                print_error("Build failed: " + build_result.error().message());
                return 1;
            }

            auto& result = build_result.value();

            if (!result.success) {
                print_error("Build failed");
                if (!result.error_message.empty()) {
                    std::cerr << result.error_message << "\n";
                }
                return 1;
            }

            std::cout << "Build completed in " +
                         std::to_string(std::chrono::duration_cast<std::chrono::seconds>(result.build_time).count()) +
                         "s\n";
            print_verbose("Files compiled: " + std::to_string(result.files_compiled));
            print_verbose("Trace files: " + std::to_string(result.trace_files.size()));

            if (options.enable_memory_profiling) {
                print_verbose("Memory files: " + std::to_string(result.memory_files.size()));
            }

            if (args.get_flag("analyze") && !result.trace_files.empty()) {
                std::cout << "\nRunning analysis...\n";

                BuildTrace build_trace;
                build_trace.timestamp = std::chrono::system_clock::now();

                for (const auto& file : result.trace_files) {
                    if (auto parse_result = parsers::parse_trace_file(file); parse_result.is_ok()) {
                        build_trace.total_time += parse_result.value().metrics.total_time;
                        build_trace.units.push_back(std::move(parse_result.value()));
                    } else {
                        print_warning("Failed to parse: " + file.string());
                    }
                }

                if (build_trace.units.empty()) {
                    print_warning("No valid trace files parsed");
                    return 0;
                }

                AnalysisOptions analysis_opts;
                analysis_opts.max_threads = 0;
                analysis_opts.min_duration_threshold = Duration::zero();
                analysis_opts.analyze_templates = true;
                analysis_opts.analyze_includes = true;

                auto analysis_result = analyzers::run_full_analysis(build_trace, analysis_opts);
                if (!analysis_result.is_ok()) {
                    print_error("Analysis failed: " + analysis_result.error().message());
                    return 1;
                }

                const auto& analysis = analysis_result.value();

                std::cout << "\nTop 10 slowest files:\n";
                std::size_t count = 0;
                for (const auto& file_result : analysis.performance.slowest_files) {
                    if (count++ >= 10) break;
                    auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(file_result.compile_time).count();
                    std::cout << "  " << file_result.file.filename().string() << ": " << time_ms << "ms\n";
                }

                auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(build_trace.total_time).count();
                std::cout << "\nTotal compilation time: " + std::to_string(total_ms) + "ms\n";
            }

            return 0;
        }
    };

    namespace {
        struct BuildCommandRegistrar {
            BuildCommandRegistrar() {
                CommandRegistry::instance().register_command(
                    std::make_unique<BuildCommand>()
                );
            }
        } build_registrar;
    }

} // namespace bha::cli
