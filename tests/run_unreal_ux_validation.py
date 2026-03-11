#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path
from typing import Any, Dict, List, Optional


def run_cmd(cmd: List[str], cwd: Path, timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def build_fixture(project_root: Path, mode: str) -> None:
    if mode == "iwyu":
        iwyu_line = "bEnforceIWYU = false;"
        pch_line = "PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;"
        unity_line = "bUseUnity = true;"
        target_unity = "bUseUnityBuild = true;"
    elif mode == "pch":
        iwyu_line = "bEnforceIWYU = true;"
        pch_line = "PCHUsage = PCHUsageMode.NoPCHs;"
        unity_line = "bUseUnity = true;"
        target_unity = "bUseUnityBuild = true;"
    elif mode == "unity":
        iwyu_line = "bEnforceIWYU = true;"
        pch_line = "PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;"
        unity_line = "bUseUnity = false;"
        target_unity = "bUseUnityBuild = false;"
    else:
        raise ValueError(f"unknown mode: {mode}")

    write_text(project_root / "Game.uproject", "{\n}\n")
    write_text(
        project_root / "Source" / "CoreModule" / "CoreModule.Build.cs",
        textwrap.dedent(
            f"""\
            using UnrealBuildTool;

            public class CoreModule : ModuleRules {{
                public CoreModule(ReadOnlyTargetRules Target) : base(Target) {{
                    {iwyu_line}
                    {pch_line}
                    {unity_line}
                }}
            }}
            """
        ),
    )
    write_text(
        project_root / "Source" / "GameEditor.Target.cs",
        textwrap.dedent(
            f"""\
            using UnrealBuildTool;

            public class GameEditorTarget : TargetRules {{
                public GameEditorTarget(TargetInfo Target) : base(Target) {{
                    {target_unity}
                    bUseAdaptiveUnityBuild = false;
                }}
            }}
            """
        ),
    )
    write_text(
        project_root / "Source" / "CoreModule" / "Public" / "CoreType.hpp",
        "#pragma once\n#include <string>\n#include <vector>\nstruct CoreType { std::vector<std::string> values; };\n",
    )
    write_text(
        project_root / "Source" / "CoreModule" / "Private" / "CoreType.cpp",
        '#include "../Public/CoreType.hpp"\nint core_anchor() { return static_cast<int>(CoreType{}.values.size()); }\n',
    )
    for idx in range(12):
        write_text(
            project_root / "Source" / "CoreModule" / "Private" / f"Unit{idx}.cpp",
            textwrap.dedent(
                f"""\
                #include "../Public/CoreType.hpp"
                #include <map>
                #include <set>
                #include <unordered_map>
                #include <vector>

                int work_{idx}() {{
                    std::map<int, std::set<int>> data;
                    data[{idx}] = {{1, 2, 3}};
                    std::unordered_map<int, int> lookup;
                    lookup[{idx}] = static_cast<int>(data[{idx}].size());
                    return lookup[{idx}];
                }}
                """
            ),
        )

    sources = "\n".join(
        [f"  Source/CoreModule/Private/Unit{idx}.cpp" for idx in range(12)]
    )
    write_text(
        project_root / "CMakeLists.txt",
        textwrap.dedent(
            f"""\
            cmake_minimum_required(VERSION 3.20)
            project(unreal_ux_{mode} LANGUAGES CXX)
            set(CMAKE_CXX_STANDARD 20)
            add_library(CoreModule STATIC
              Source/CoreModule/Private/CoreType.cpp
            {sources}
            )
            target_include_directories(CoreModule PUBLIC Source/CoreModule/Public)
            """
        ),
    )


def prepare_fixture_root(work_root: Path, mode: str) -> Path:
    project_root = work_root / mode
    if project_root.exists():
        shutil.rmtree(project_root)
    build_fixture(project_root, mode)
    return project_root


def append_repo_root_to_sys_path(repo_root: Path) -> None:
    repo_root_str = str(repo_root)
    if repo_root_str not in sys.path:
        sys.path.insert(0, repo_root_str)


def run_lsp_analysis(
    server_path: Path,
    project_root: Path,
    build_dir: Path,
    analysis_timeout: int,
) -> Optional[Dict[str, Any]]:
    from lsp.tests.lsp_test_client import LSPClient, execute_command_with_timeout

    client = LSPClient(
        server_path=server_path,
        cwd=project_root,
        env={"BHA_CLANG_TIDY": "/bin/true"},
    )
    client.start()
    try:
        init = client.initialize(f"file://{project_root}")
        if not init or "result" not in init:
            return None
        client.send_notification("initialized")
        response = execute_command_with_timeout(
            client,
            "bha.analyze",
            [
                {
                    "projectRoot": str(project_root),
                    "buildDir": str(build_dir),
                    "rebuild": False,
                }
            ],
            timeout_seconds=analysis_timeout,
            timeout_label=f"Analyze[{project_root.name}]",
        )
        if not response or "result" not in response:
            return None
        return response["result"]
    finally:
        try:
            client.shutdown()
        except Exception:
            pass
        client.stop()


def validate_unreal_checks(result: Dict[str, Any]) -> List[str]:
    issues: List[str] = []
    checks = result.get("unrealEnvironmentChecks")
    if not isinstance(checks, list) or not checks:
        return ["missing unrealEnvironmentChecks in bha.analyze response"]

    ids = {check.get("id") for check in checks if isinstance(check, dict)}
    required = {"project-detected", "build-tooling", "module-rules", "target-rules"}
    missing = sorted(required.difference(ids))
    if missing:
        issues.append("missing expected check ids: " + ", ".join(missing))

    for check in checks:
        if not isinstance(check, dict):
            continue
        if not check.get("message"):
            issues.append(f"check '{check.get('id', 'unknown')}' is missing message")
    return issues


def count_unreal_framed_suggestions(result: Dict[str, Any]) -> int:
    suggestions = result.get("suggestions")
    if not isinstance(suggestions, list):
        return 0
    count = 0
    for suggestion in suggestions:
        if not isinstance(suggestion, dict):
            continue
        title = str(suggestion.get("title", "")).lower()
        description = str(suggestion.get("description", "")).lower()
        if "unreal" in title or "module rules" in description or "target rules" in description or "ubt" in description:
            count += 1
    return count


def write_summary(work_root: Path, payload: Dict[str, Any]) -> None:
    write_text(work_root / "validation_summary.json", json.dumps(payload, indent=2) + "\n")

    lines = ["# Unreal UX Validation", ""]
    for run in payload.get("runs", []):
        status = "ok" if run.get("ok") else "failed"
        lines.append(f"- `{run.get('mode')}`: {status}")
        lines.append(f"  checks={run.get('checkCount', 0)} suggestions={run.get('suggestionCount', 0)} unrealFramed={run.get('unrealFramedSuggestions', 0)}")
        for issue in run.get("issues", []):
            lines.append(f"  issue: {issue}")
    lines.append("")
    lines.append(f"overall: {'ok' if payload.get('ok') else 'failed'}")
    write_text(work_root / "validation_report.md", "\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Unreal UX LSP response contract on synthetic fixtures")
    parser.add_argument("--timeout", type=int, default=1800, help="timeout per build command in seconds")
    parser.add_argument("--analysis-timeout", type=int, default=180, help="timeout per bha.analyze call in seconds")
    parser.add_argument(
        "--work-root",
        type=Path,
        default=Path("/tmp/bha-unreal-ux-validation"),
        help="temporary root for generated fixtures and reports",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    append_repo_root_to_sys_path(repo_root)

    bha_path = repo_root / "build" / "bha"
    server_path = repo_root / "build" / "lsp" / "bha-lsp"
    if not server_path.exists():
        server_path = repo_root / "build" / "bha-lsp"

    if not bha_path.exists():
        print(f"error: bha binary missing: {bha_path}")
        return 2
    if not server_path.exists():
        print(f"error: bha-lsp binary missing: {server_path}")
        return 2

    work_root = args.work_root.resolve()
    work_root.mkdir(parents=True, exist_ok=True)

    modes = ["iwyu", "pch", "unity"]
    results: List[Dict[str, Any]] = []

    for mode in modes:
        project_root = prepare_fixture_root(work_root, mode)
        build_dir = project_root / "build"
        traces_dir = project_root / "traces"

        print(f"[{mode}] building traces...")
        build = run_cmd(
            [
                str(bha_path),
                "build",
                "--build-system",
                "cmake",
                "--compiler",
                "clang",
                "--build-dir",
                str(build_dir),
                "--output",
                str(traces_dir),
                "--memory",
                "--clean",
            ],
            cwd=project_root,
            timeout=args.timeout,
        )
        if build.returncode != 0:
            results.append(
                {
                    "mode": mode,
                    "ok": False,
                    "issues": ["bha build failed"],
                    "stdoutTail": (build.stdout or "")[-2000:],
                    "stderrTail": (build.stderr or "")[-2000:],
                }
            )
            continue

        print(f"[{mode}] analyzing via LSP...")
        result = run_lsp_analysis(
            server_path=server_path,
            project_root=project_root,
            build_dir=build_dir,
            analysis_timeout=args.analysis_timeout,
        )
        if result is None:
            results.append(
                {
                    "mode": mode,
                    "ok": False,
                    "issues": ["bha.analyze failed or timed out"],
                }
            )
            continue

        issues = validate_unreal_checks(result)
        unreal_framed = count_unreal_framed_suggestions(result)
        results.append(
            {
                "mode": mode,
                "ok": len(issues) == 0,
                "issues": issues,
                "checkCount": len(result.get("unrealEnvironmentChecks") or []),
                "suggestionCount": len(result.get("suggestions") or []),
                "unrealFramedSuggestions": unreal_framed,
            }
        )
        print(
            f"[{mode}] checks={results[-1]['checkCount']} suggestions={results[-1]['suggestionCount']} "
            f"unreal-framed={unreal_framed} status={'ok' if results[-1]['ok'] else 'failed'}"
        )

    summary = {
        "ok": all(item.get("ok") for item in results),
        "runs": results,
    }
    write_summary(work_root, summary)
    print(f"summary: {work_root / 'validation_summary.json'}")
    print(f"report: {work_root / 'validation_report.md'}")

    if summary["ok"]:
        print("ok: Unreal UX contract validated")
        return 0

    print("error: Unreal UX contract validation failed")
    return 1


if __name__ == "__main__":
    sys.exit(main())
