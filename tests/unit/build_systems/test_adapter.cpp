#include "bha/build_systems/adapter.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

using namespace bha::build_systems;
namespace fs = std::filesystem;

namespace {
    constexpr char kPathListSeparator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif

    struct TempDir {
        fs::path root;

        TempDir() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root = fs::temp_directory_path() / ("bha-buildsys-" + std::to_string(stamp));
            fs::create_directories(root);
        }

        ~TempDir() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    void write_file(const fs::path& path, const std::string& content) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    void make_executable(const fs::path& path) {
        fs::permissions(
            path,
            fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::group_exec | fs::perms::group_read |
                fs::perms::others_exec | fs::perms::others_read,
            fs::perm_options::replace
        );
    }

    struct ScopedEnvVar {
        std::string name;
        std::optional<std::string> original_value;

        ScopedEnvVar(std::string env_name, const std::string& value) : name(std::move(env_name)) {
            if (const char* existing = std::getenv(name.c_str())) {
                original_value = existing;
            }
#ifdef _WIN32
            _putenv_s(name.c_str(), value.c_str());
#else
            setenv(name.c_str(), value.c_str(), 1);
#endif
        }

        ~ScopedEnvVar() {
#ifdef _WIN32
            _putenv_s(name.c_str(), original_value.has_value() ? original_value->c_str() : "");
#else
            if (original_value.has_value()) {
                setenv(name.c_str(), original_value->c_str(), 1);
            } else {
                unsetenv(name.c_str());
            }
#endif
        }
    };

    std::string read_file_text(const fs::path& path) {
        std::ifstream in(path);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void write_fake_cmake(const fs::path& bin_dir, const fs::path& log_path) {
        const fs::path script = bin_dir / "cmake";
        write_file(
            script,
            "#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > \"" + log_path.string() + "\"\n"
            "exit 0\n"
        );
        make_executable(script);
    }

    void write_fake_compiler(const fs::path& path) {
        write_file(path, "#!/bin/sh\nexit 0\n");
        make_executable(path);
    }
}

TEST(BuildOptionsTest, DefaultValues) {
    const BuildOptions options;
    EXPECT_EQ(options.build_type, "Release");
    EXPECT_EQ(options.parallel_jobs, 0);
    EXPECT_TRUE(options.enable_tracing);
    EXPECT_FALSE(options.enable_memory_profiling);
    EXPECT_FALSE(options.clean_first);
    EXPECT_FALSE(options.verbose);
    EXPECT_TRUE(options.compiler.empty());
    EXPECT_TRUE(options.c_compiler.empty());
    EXPECT_TRUE(options.cxx_compiler.empty());
}

TEST(BuildOptionsTest, MemoryProfilingFlag) {
    BuildOptions options;
    options.enable_memory_profiling = true;
    EXPECT_TRUE(options.enable_memory_profiling);
    EXPECT_TRUE(options.enable_tracing);
}

TEST(BuildResultTest, DefaultValues) {
    const BuildResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.trace_files.empty());
    EXPECT_TRUE(result.memory_files.empty());
    EXPECT_EQ(result.build_time, bha::Duration::zero());
    EXPECT_EQ(result.files_compiled, 0);
}

TEST(BuildResultTest, MemoryFiles) {
    BuildResult result;
    result.success = true;
    result.memory_files.emplace_back("/path/to/file.su");
    result.memory_files.emplace_back("/path/to/file.map");

    EXPECT_EQ(result.memory_files.size(), 2);
    EXPECT_EQ(result.memory_files[0].string(), "/path/to/file.su");
    EXPECT_EQ(result.memory_files[1].string(), "/path/to/file.map");
}

TEST(BuildSystemRegistryTest, Singleton) {
    auto& registry1 = BuildSystemRegistry::instance();
    auto& registry2 = BuildSystemRegistry::instance();
    EXPECT_EQ(&registry1, &registry2);
}

TEST(BuildSystemRegistryTest, AdaptersNotEmpty) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();
    EXPECT_FALSE(registry.adapters().empty());
    EXPECT_GE(registry.adapters().size(), 4);
}

TEST(BuildSystemRegistryTest, GetByName) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();

    auto* cmake = registry.get("CMake");
    EXPECT_NE(cmake, nullptr);
    EXPECT_EQ(cmake->name(), "CMake");

    auto* ninja = registry.get("Ninja");
    EXPECT_NE(ninja, nullptr);
    EXPECT_EQ(ninja->name(), "Ninja");

    auto* nonexistent = registry.get("Nonexistent");
    EXPECT_EQ(nonexistent, nullptr);
}

TEST(BuildSystemRegistryTest, DetectsUnrealProjectFromUprojectAndTargetRules) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();

    TempDir temp;
    write_file(temp.root / "Sample.uproject", "{\n}");
    write_file(
        temp.root / "Source" / "SampleEditor.Target.cs",
        "using UnrealBuildTool;\n"
        "public class SampleEditorTarget : TargetRules {\n"
        "  public SampleEditorTarget(ReadOnlyTargetRules Target) : base(Target) {}\n"
        "}\n"
    );

    auto* adapter = registry.detect(temp.root);
    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->name(), "Unreal");
}

TEST(BuildSystemRegistryTest, PrefersAvailableAdapterWhenHigherConfidenceToolIsMissing) {
    register_all_adapters();
    const auto& registry = BuildSystemRegistry::instance();

#ifdef _WIN32
    if (std::system("where bazel >nul 2>nul") == 0 || std::system("where bazelisk >nul 2>nul") == 0) {
        GTEST_SKIP() << "Bazel is installed; availability fallback is not exercised";
    }
#else
    if (std::system("command -v bazel >/dev/null 2>&1") == 0 ||
        std::system("command -v bazelisk >/dev/null 2>&1") == 0) {
        GTEST_SKIP() << "Bazel is installed; availability fallback is not exercised";
    }
#endif

    TempDir temp;
    write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(sample)\n");
    write_file(temp.root / "MODULE.bazel", "module(name = \"sample\")\n");

    auto* adapter = registry.detect(temp.root);
    ASSERT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->name(), "CMake");
}

TEST(CMakeAdapterTest, DerivesCompanionCompilerFromExplicitCxxOverride) {
    register_all_adapters();
    auto* adapter = BuildSystemRegistry::instance().get("CMake");
    ASSERT_NE(adapter, nullptr);

    TempDir temp;
    const fs::path bin_dir = temp.root / "bin";
    const fs::path log_path = temp.root / "cmake-args.log";
    write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(sample C CXX)\n");
    write_fake_cmake(bin_dir, log_path);
    write_fake_compiler(bin_dir / "clang");
    write_fake_compiler(bin_dir / "clang++");

    ScopedEnvVar path_override(
        "PATH",
        bin_dir.string() + kPathListSeparator + (std::getenv("PATH") ? std::getenv("PATH") : "")
    );

    BuildOptions options;
    options.build_dir = temp.root / "build";
    options.cxx_compiler = (bin_dir / "clang++").string();

    const auto result = adapter->configure(temp.root, options);
    ASSERT_TRUE(result.is_ok()) << result.error().message();

    const std::string logged = read_file_text(log_path);
    EXPECT_NE(logged.find("-DCMAKE_C_COMPILER=" + (bin_dir / "clang").string()), std::string::npos);
    EXPECT_NE(logged.find("-DCMAKE_CXX_COMPILER=" + (bin_dir / "clang++").string()), std::string::npos);
}

TEST(CMakeAdapterTest, ExplicitCompilerPairOverridesLegacySingleCompiler) {
    register_all_adapters();
    auto* adapter = BuildSystemRegistry::instance().get("CMake");
    ASSERT_NE(adapter, nullptr);

    TempDir temp;
    const fs::path bin_dir = temp.root / "bin";
    const fs::path log_path = temp.root / "cmake-args.log";
    write_file(temp.root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(sample C CXX)\n");
    write_fake_cmake(bin_dir, log_path);
    write_fake_compiler(bin_dir / "gcc");
    write_fake_compiler(bin_dir / "g++");
    write_fake_compiler(bin_dir / "clang");
    write_fake_compiler(bin_dir / "clang++");

    ScopedEnvVar path_override(
        "PATH",
        bin_dir.string() + kPathListSeparator + (std::getenv("PATH") ? std::getenv("PATH") : "")
    );

    BuildOptions options;
    options.build_dir = temp.root / "build";
    options.compiler = (bin_dir / "gcc").string();
    options.c_compiler = (bin_dir / "clang").string();
    options.cxx_compiler = (bin_dir / "clang++").string();

    const auto result = adapter->configure(temp.root, options);
    ASSERT_TRUE(result.is_ok()) << result.error().message();

    const std::string logged = read_file_text(log_path);
    EXPECT_NE(logged.find("-DCMAKE_C_COMPILER=" + (bin_dir / "clang").string()), std::string::npos);
    EXPECT_NE(logged.find("-DCMAKE_CXX_COMPILER=" + (bin_dir / "clang++").string()), std::string::npos);
    EXPECT_EQ(logged.find("-DCMAKE_C_COMPILER=" + (bin_dir / "gcc").string()), std::string::npos);
}
