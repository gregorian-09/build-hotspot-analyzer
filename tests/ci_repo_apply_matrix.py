#!/usr/bin/env python3

"""Pinned native project/OS matrix, including explicit unsupported cells."""

import argparse
import json


# Revisions are immutable upstream commits. Native CMake paths are based on each
# project's upstream build files; Make/Meson gaps are reported, never skipped.
PROJECTS = {
    "benchmark": {
        "repository": "google/benchmark",
        "revision": "82a89508915e7607e10675ee5a4dacd8fc72c649",
        "language": "C++",
        "cmake_args": ["-DBENCHMARK_ENABLE_TESTING=ON", "-DBENCHMARK_ENABLE_GTEST_TESTS=OFF"],
    },
    "mimalloc": {
        "repository": "microsoft/mimalloc",
        "revision": "c683b7c60c91cb2809977e37d3f4d084d7497180",
        "language": "C",
        "cmake_args": ["-DMI_BUILD_TESTS=ON"],
    },
    "rocksdb": {
        "repository": "facebook/rocksdb",
        "revision": "5d6f321b7024693ea12ae32461f13b69c3bee9b4",
        "language": "C++",
        "cmake_args": [
            "-DWITH_TESTS=OFF", "-DWITH_TOOLS=OFF", "-DWITH_BENCHMARK_TOOLS=OFF",
            "-DFAIL_ON_WARNINGS=OFF",
        ],
    },
    "zstd": {
        "repository": "facebook/zstd",
        "revision": "01b7154f1172432f8abe9b3bb9909e14a1176b7d",
        "language": "C",
        "source_subdir": "build/cmake",
        "cmake_args": [
            "-DZSTD_BUILD_PROGRAMS=OFF", "-DZSTD_BUILD_CONTRIB=OFF", "-DZSTD_BUILD_TESTS=ON",
        ],
    },
    "abseil": {
        "repository": "abseil/abseil-cpp",
        "revision": "2d5a8e38e7443d0aef7c5fe4a3590fbaa44e8978",
        "language": "C++",
        "cmake_args": [
            "-DCMAKE_CXX_STANDARD=17", "-DABSL_BUILD_TESTING=OFF",
            "-DABSL_USE_GOOGLETEST_HEAD=OFF", "-DABSL_BUILD_TEST_HELPERS=OFF",
        ],
    },
    "catch2": {
        "repository": "catchorg/Catch2",
        "revision": "02f8b5593a15cdec8558256e6f40a0c7fb5862d6",
        "language": "C++",
        "cmake_args": ["-DCATCH_BUILD_TESTING=OFF", "-DCATCH_INSTALL_DOCS=OFF"],
    },
    "leveldb": {
        "repository": "google/leveldb",
        "revision": "7ee830d02b623e8ffe0b95d59a74db1e58da04c5",
        "language": "C++",
        "cmake_args": [
            "-DLEVELDB_BUILD_TESTS=ON", "-DLEVELDB_BUILD_BENCHMARKS=OFF",
            "-DHAVE_SNAPPY=OFF", "-DCMAKE_CXX_STANDARD=17",
        ],
        "require_tests": True,
    },
    "yaml-cpp": {
        "repository": "jbeder/yaml-cpp",
        "revision": "1e0876c671268661deb2628040e3959e1e9d6e69",
        "language": "C++",
        "cmake_args": ["-DYAML_BUILD_SHARED_LIBS=OFF", "-DYAML_CPP_BUILD_TESTS=ON"],
        "require_tests": True,
    },
    "libjpeg-turbo": {
        "repository": "libjpeg-turbo/libjpeg-turbo",
        "revision": "b33c60b439d58bc3564d0d32c1bb397fcfb77dc6",
        "language": "C",
        "cmake_args": ["-DWITH_SIMD=OFF", "-DENABLE_SHARED=OFF", "-DWITH_TURBOJPEG=OFF"],
    },
    "glfw": {
        "repository": "glfw/glfw",
        "revision": "92dcf4ce74f2e2554a98fea09be7c705c17daa5a",
        "language": "C",
        "cmake_args": [
            "-DGLFW_BUILD_EXAMPLES=OFF", "-DGLFW_BUILD_TESTS=OFF",
            "-DGLFW_BUILD_DOCS=OFF", "-DGLFW_BUILD_WAYLAND=OFF",
        ],
    },
    "redis": {
        "repository": "redis/redis",
        "revision": "16e021b465fbbc73d5f070b4da5ce9f5ee4baea1",
        "language": "C",
        "build_system": "make",
        "unsupported": {
            "ubuntu-24.04": "Native Make build exists, but the shipped CLI does not register the experimental Make adapter",
            "macos-15-intel": "Native Make build exists, but the shipped CLI does not register the experimental Make adapter",
            "windows-2022": "Redis has no upstream native MSVC build",
        },
    },
    "curl": {
        "repository": "curl/curl",
        "revision": "7f364029b861d064caa128f4a8cb7b34696f7db3",
        "language": "C",
        "cmake_args": [
            "-DBUILD_TESTING=OFF", "-DCURL_ENABLE_SSL=OFF", "-DCURL_USE_LIBPSL=OFF",
            "-DCURL_USE_LIBSSH2=OFF", "-DUSE_NGHTTP2=OFF", "-DBUILD_CURL_EXE=OFF",
        ],
    },
    "zlib": {
        "repository": "madler/zlib",
        "revision": "da607da739fa6047df13e66a2af6b8bec7c2a498",
        "language": "C",
        "cmake_args": [],
    },
    "libpng": {
        "repository": "pnggroup/libpng",
        "revision": "4eeb825ab26605ed34cd74495d4cce920939fe9f",
        "language": "C",
        "cmake_args": ["-DPNG_SHARED=OFF"],
    },
    "weston": {
        "repository": "wayland/weston",
        "revision": "46298ecc7e7ca393567d431a562ba1ce3eeed04b",
        "language": "C",
        "build_system": "meson",
        "unsupported": {
            "ubuntu-24.04": "Meson project is not supported by the CMake-only benchmark harness",
            "windows-2022": "Weston has no documented native MSVC build",
            "macos-15-intel": "Weston has no documented native macOS build",
        },
    },
}

RUNNERS = {
    "ubuntu-24.04": ("clang-18", "clang++-18"),
    "windows-2022": ("cl", "cl"),
    "macos-15-intel": ("clang", "clang++"),
}


def build_matrix(selection: str) -> dict:
    if selection not in PROJECTS and selection != "all":
        raise ValueError(f"Unknown project: {selection}")
    names = PROJECTS if selection == "all" else (selection,)
    included, unsupported = [], []
    for name in names:
        project = PROJECTS[name]
        for runner, compilers in RUNNERS.items():
            reason = project.get("unsupported", {}).get(runner)
            if reason:
                unsupported.append({"project": name, "os": runner, "reason": reason})
            else:
                included.append({
                    "project": name,
                    "os": runner,
                    "c_compiler": compilers[0],
                    "cxx_compiler": compilers[1],
                    "repository": project["repository"],
                    "revision": project["revision"],
                    "require_tests": project.get("require_tests", False),
                })
    return {"include": included, "unsupported": unsupported}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", choices=("all", *PROJECTS))
    args = parser.parse_args()
    plan = build_matrix(args.project)
    print("matrix=" + json.dumps({"include": plan["include"]}, separators=(",", ":")))
    print("unsupported=" + json.dumps(plan["unsupported"], separators=(",", ":")))


if __name__ == "__main__":
    main()
