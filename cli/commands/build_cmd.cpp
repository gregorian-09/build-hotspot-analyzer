#include "bha/cli/commands/command.hpp"
#include "bha/cli/formatter.hpp"
#include "bha/build_systems/adapter.hpp"
#include "bha/build_sessions/cmake_instrumentation.hpp"
#include "bha/build_sessions/cmake_file_api.hpp"
#include "bha/build_sessions/session_file.hpp"
#include "bha/parsers/parser.hpp"
#include "bha/parsers/sccache_stats_parser.hpp"
#include "bha/parsers/p1689_module_parser.hpp"
#include "bha/parsers/process_resource_parser.hpp"
#include "bha/parsers/memory_parser.hpp"
#include "bha/analyzers/analyzer.hpp"

#include <iostream>
#include <filesystem>
#include <unordered_map>

namespace bha::cli
{
    namespace fs = std::filesystem;

    namespace {
        BuildSystemType build_system_type(const std::string_view name) {
            if (name == "CMake") return BuildSystemType::CMake;
            if (name == "Ninja") return BuildSystemType::Ninja;
            if (name == "Make") return BuildSystemType::Make;
            if (name == "MSBuild") return BuildSystemType::MSBuild;
            if (name == "Bazel") return BuildSystemType::Bazel;
            if (name == "Buck2") return BuildSystemType::Buck2;
            if (name == "Meson") return BuildSystemType::Meson;
            if (name == "SCons") return BuildSystemType::SCons;
            if (name == "XCode") return BuildSystemType::XCode;
            return BuildSystemType::Unknown;
        }

        BuildSession make_adapter_session(
            const build_systems::IBuildSystemAdapter& adapter,
            const fs::path& project_path,
            const build_systems::BuildOptions& options,
            const Duration duration
        ) {
            BuildSession session;
            session.id = (project_path / "bha-adapter-build").generic_string();
            session.build_directory = options.build_dir.empty()
                ? project_path / "build"
                : options.build_dir;
            session.build_system = build_system_type(adapter.name());
            session.configuration = options.build_type;
            session.instrumentation_hook = "bha-adapter-wall-clock";

            BuildCommandEvent event;
            event.id = session.id;
            event.role = BuildStepRole::Build;
            event.command = adapter.name();
            event.working_directory = project_path;
            event.configuration = options.build_type;
            event.start_time = std::chrono::system_clock::now() -
                std::chrono::duration_cast<Timestamp::duration>(duration);
            event.duration = duration;
            event.result = 0;
            event.timing_provenance.evidence = EvidenceKind::Observed;
            event.timing_provenance.producer = "bha-adapter";
            event.timing_provenance.capture_mode = "adapter-wall-clock";
            event.timing_provenance.scope = "build";
            event.timing_provenance.timing_domain = TimingDomain::WallClock;
            event.timing_provenance.timing_aggregation = TimingAggregation::Exclusive;
            session.commands.push_back(std::move(event));

            MetricCapability capability;
            capability.metric = "build.adapter.wall_time";
            capability.provenance.evidence = EvidenceKind::Observed;
            capability.provenance.producer = "bha-adapter";
            capability.provenance.capture_mode = "steady-clock-around-build-command";
            capability.provenance.scope = "build";
            capability.provenance.timing_domain = TimingDomain::WallClock;
            capability.provenance.timing_aggregation = TimingAggregation::Exclusive;
            session.metric_capabilities.push_back(std::move(capability));
            return session;
        }

        bool materialize_cmake_trace_references(
            BuildSession& session,
            const fs::path& session_directory
        ) {
            if (session_directory.empty()) {
                return false;
            }

            const fs::path trace_directory = session_directory / "compile-trace";
            std::error_code ec;
            fs::create_directories(trace_directory, ec);
            if (ec) {
                return false;
            }

            std::vector<std::pair<BuildCommandEvent*, fs::path>> materialized;
            materialized.reserve(session.commands.size());
            for (auto& command : session.commands) {
                if (!command.trace_file.has_value()) {
                    continue;
                }

                const fs::path source = *command.trace_file;
                const fs::path destination = trace_directory / source.filename();
                if (source != destination) {
                    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        return false;
                    }
                }
                const auto relative_path = fs::relative(destination, session_directory, ec);
                if (ec) {
                    return false;
                }
                materialized.emplace_back(&command, relative_path);
            }
            for (const auto& [command, relative_path] : materialized) {
                command->trace_file = relative_path;
            }
            return true;
        }
    }

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
                {"build-dir", 'b', "Directory for build artifacts", false, true, "", "DIR"},
                {"output", 'o', "Directory for trace files", false, true, "", "DIR"},
                {"compiler", 0, "Compiler to use", false, true, "", "COMPILER"},
                {"cmake-args", 0, "Additional CMake arguments (semicolon-separated)", false, true, "", "ARGS"},
                {"configure-args", 0, "Additional configure/make arguments (semicolon-separated)", false, true, "", "ARGS"},
                {"cache-stats", 0, "Structured sccache JSON statistics file for --analyze", false, true, "", "FILE"},
                {"module-deps", 0, "Clang P1689 module dependency JSON file for --analyze", false, true, "", "FILE"},
                {"resource-stats", 0, "Clang -fproc-stat-report CSV file for --analyze", false, true, "", "FILE"},
                {"cmake-index", 0, "CMake Instrumentation API v1 index file for --analyze", false, true, "", "FILE"},
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

            if (std::string build_dir = args.get_or("build-dir", ""); !build_dir.empty()) {
                options.build_dir = fs::path(build_dir);
            }

            if (std::string output = args.get_or("output", ""); !output.empty()) {
                options.trace_output_dir = fs::path(output);
            }

            if (std::string cmake_args = args.get_or("cmake-args", ""); !cmake_args.empty()) {
                std::istringstream ss(cmake_args);
                std::string arg;
                while (std::getline(ss, arg, ';')) {
                    if (!arg.empty()) {
                        options.extra_args.push_back(arg);
                    }
                }
            }

            if (std::string configure_args = args.get_or("configure-args", ""); !configure_args.empty()) {
                std::istringstream ss(configure_args);
                std::string arg;
                while (std::getline(ss, arg, ';')) {
                    if (!arg.empty()) {
                        options.extra_args.push_back(arg);
                    }
                }
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
                    std::cerr << result.error_message;
                    if (!result.error_message.ends_with('\n')) {
                        std::cerr << "\n";
                    }
                }
                if (args.get_flag("verbose") && !result.output.empty()) {
                    std::cerr << result.output;
                    if (!result.output.ends_with('\n')) {
                        std::cerr << "\n";
                    }
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

            if (args.get_flag("verbose") && !result.output.empty()) {
                std::cout << result.output;
                if (!result.output.ends_with('\n')) {
                    std::cout << "\n";
                }
            }

            BuildSession persisted_session = make_adapter_session(
                *adapter,
                project_path,
                options,
                result.build_time
            );
            if (result.cmake_instrumentation_index.has_value()) {
                build_sessions::CMakeInstrumentationParser parser;
                if (const auto parsed = parser.parse_index_file(*result.cmake_instrumentation_index);
                    parsed.is_ok()) {
                    persisted_session = parsed.value();
                } else {
                    print_warning(
                        "Failed to persist CMake instrumentation session: " + parsed.error().message()
                    );
                }
            }
            fs::path session_directory = options.trace_output_dir;
            if (session_directory.empty() && !result.trace_files.empty()) {
                session_directory = result.trace_files.front().parent_path();
            }
            if (session_directory.empty()) {
                session_directory = options.build_dir.empty()
                    ? project_path / "build" / "traces"
                    : options.build_dir / "traces";
            }
            const fs::path session_path =
                session_directory / std::string(build_sessions::kBuildSessionFileName);
            if (result.cmake_instrumentation_index.has_value() &&
                !materialize_cmake_trace_references(persisted_session, session_directory)) {
                print_warning(
                    "Failed to materialize CMake compile traces beside the build session; "
                    "the session will retain producer paths"
                );
            }
            build_sessions::BuildSessionFileParser session_parser;
            if (const auto write_result = session_parser.write_file(persisted_session, session_path);
                write_result.is_err()) {
                print_warning("Failed to persist build session: " + write_result.error().message());
            } else {
                print_verbose("Persisted build session: " + session_path.string());
            }

            if (args.get_flag("analyze") &&
                (!result.trace_files.empty() || result.build_time > Duration::zero() ||
                 args.get("cmake-index").has_value())) {
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

                const auto requested_cmake_index = args.get("cmake-index");
                const auto cmake_index_path = requested_cmake_index.has_value()
                    ? std::optional<fs::path>(fs::path(*requested_cmake_index))
                    : result.cmake_instrumentation_index;
                if (cmake_index_path.has_value()) {
                    build_sessions::CMakeInstrumentationParser parser;
                    if (const auto attach_result = parser.attach_to_trace(build_trace, *cmake_index_path);
                        attach_result.is_err()) {
                        print_error(
                            "Failed to attach CMake instrumentation index: " +
                            attach_result.error().message()
                        );
                        return 1;
                    }
                    print_verbose(
                        "Attached CMake instrumentation index: " + cmake_index_path->string()
                    );
                }

                if (result.cmake_file_api_index.has_value()) {
                    build_sessions::CMakeFileApiParser parser;
                    const auto graph = parser.parse_reply_index(
                        *result.cmake_file_api_index,
                        options.build_type
                    );
                    if (graph.is_err()) {
                        print_warning(
                            "Failed to attach CMake File API target graph: " + graph.error().message()
                        );
                    } else {
                        build_trace.target_graph = graph.value();
                        print_verbose(
                            "Attached CMake File API target graph: " +
                            result.cmake_file_api_index->string()
                        );
                    }
                }

                if (!build_trace.build_session.has_value()) {
                    build_trace.build_session = make_adapter_session(
                        *adapter,
                        project_path,
                        options,
                        result.build_time
                    );
                }

                if (build_trace.units.empty() && !build_trace.build_session.has_value()) {
                    print_warning("No valid trace files parsed");
                    return 0;
                }

                if (const auto cache_stats_path = args.get("cache-stats")) {
                    parsers::SccacheStatsParser parser;
                    if (const auto cache_result = parser.attach_to_trace(build_trace, *cache_stats_path);
                        cache_result.is_err()) {
                        print_error("Failed to parse cache statistics: " + cache_result.error().message());
                        return 1;
                    }
                    print_verbose("Attached cache statistics: " + *cache_stats_path);
                }

                if (const auto module_deps_path = args.get("module-deps")) {
                    parsers::P1689ModuleParser parser;
                    if (const auto module_result = parser.attach_to_trace(build_trace, *module_deps_path);
                        module_result.is_err()) {
                        print_error("Failed to parse module dependencies: " + module_result.error().message());
                        return 1;
                    }
                    print_verbose("Attached module dependencies: " + *module_deps_path);
                }

                if (const auto resource_stats_path = args.get("resource-stats")) {
                    parsers::ProcessResourceParser resource_parser;
                    if (const auto resource_result = resource_parser.attach_to_trace(
                            build_trace,
                            *resource_stats_path
                        ); resource_result.is_err()) {
                        print_error(
                            "Failed to parse process resource statistics: " +
                            resource_result.error().message()
                        );
                        return 1;
                    }
                    print_verbose("Attached process resource statistics: " + *resource_stats_path);
                }

                if (!result.memory_files.empty()) {
                    std::unordered_map<std::string, MemoryMetrics> memory_map;
                    for (const auto& file : result.memory_files) {
                        if (file.extension() != ".su") continue;
                        if (auto mem_result = parsers::parse_stack_usage_file(file); mem_result.is_ok()) {
                            std::string filename = file.filename().string();
                            if (filename.size() > 3) {
                                std::string key = filename.substr(0, filename.size() - 3);
                                memory_map[key] = mem_result.value();
                            }
                        }
                    }

                    for (auto& unit : build_trace.units) {
                        std::string source_name = unit.source_file.filename().string();
                        if (auto it = memory_map.find(source_name); it != memory_map.end()) {
                            unit.metrics.memory = it->second;
                        }
                    }
                    print_verbose("Matched " + std::to_string(memory_map.size()) + " files with stack usage data");
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
