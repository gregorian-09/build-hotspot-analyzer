#!/usr/bin/env python3

"""Emit a bounded, revision-pinned repository/runner matrix for Actions."""

import argparse
import json


PROJECTS = {
    "leveldb": {
        "repository": "google/leveldb",
        "revision": "7ee830d02b623e8ffe0b95d59a74db1e58da04c5",
        "test_cmake_arg": "-DLEVELDB_BUILD_TESTS=ON",
    },
    "yaml-cpp": {
        "repository": "jbeder/yaml-cpp",
        "revision": "1e0876c671268661deb2628040e3959e1e9d6e69",
        "test_cmake_arg": "-DYAML_CPP_BUILD_TESTS=ON",
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
    return {
        "include": [
            {
                "project": name,
                "os": runner,
                "c_compiler": compilers[0],
                "cxx_compiler": compilers[1],
                **PROJECTS[name],
            }
            for name in names
            for runner, compilers in RUNNERS.items()
        ]
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", choices=("all", *PROJECTS))
    args = parser.parse_args()
    print("matrix=" + json.dumps(build_matrix(args.project), separators=(",", ":")))


if __name__ == "__main__":
    main()
