#!/usr/bin/env python3

"""Compare the CLI, LSP suggestion, and LSP raw-edit apply workflows."""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
LSP_TEST_DIR = REPO_ROOT / "lsp" / "tests"
if str(LSP_TEST_DIR) not in sys.path:
    sys.path.insert(0, str(LSP_TEST_DIR))

from lsp_test_client import LSPClient, execute_command_with_timeout  # noqa: E402


def run_command(command: list[str], cwd: Path, timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def parse_json_output(process: subprocess.CompletedProcess[str], label: str) -> dict[str, Any]:
    if process.returncode != 0:
        raise RuntimeError(
            f"{label} failed with exit code {process.returncode}\n"
            f"stdout:\n{process.stdout[-4000:]}\n"
            f"stderr:\n{process.stderr[-4000:]}"
        )
    try:
        payload = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"{label} returned invalid JSON: {error}\n{process.stdout[-4000:]}"
        ) from error
    if not isinstance(payload, dict):
        raise RuntimeError(f"{label} returned a non-object JSON payload")
    return payload


def normalized_apply_result(result: dict[str, Any], project_root: Path) -> dict[str, Any]:
    root = str(project_root)

    def relative_paths(values: Any) -> list[str]:
        if not isinstance(values, list):
            return []
        paths = []
        for value in values:
            path = Path(value)
            try:
                paths.append(path.relative_to(project_root).as_posix())
            except ValueError:
                paths.append(path.as_posix())
        return sorted(paths)

    validation = result.get("buildValidation") or {}
    rollback = result.get("rollback") or {}
    errors = result.get("errors") or []
    normalized_errors = []
    for error in errors:
        if not isinstance(error, dict):
            normalized_errors.append(error)
            continue
        normalized = dict(error)
        message = normalized.get("message")
        if isinstance(message, str) and root in message:
            normalized["message"] = message.replace(root, "<workspace>")
        normalized_errors.append(normalized)

    return {
        "success": result.get("success"),
        "changedFiles": relative_paths(result.get("changedFiles")),
        "errors": normalized_errors,
        "hasBackup": bool(result.get("backupId")),
        "buildValidation": {
            "requested": validation.get("requested"),
            "ran": validation.get("ran"),
            "success": validation.get("success"),
            "errorCount": validation.get("errorCount"),
        },
        "rollback": {
            "attempted": rollback.get("attempted"),
            "success": rollback.get("success"),
            "reason": rollback.get("reason"),
            "restoredFiles": relative_paths(rollback.get("restoredFiles")),
        },
    }


def write_fixture(project_root: Path) -> None:
    (project_root / "include").mkdir(parents=True)
    (project_root / "src").mkdir()
    (project_root / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(bha_apply_parity LANGUAGES CXX)\n"
        "set(CMAKE_CXX_STANDARD 20)\n"
        "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "add_executable(bha_apply_parity src/main.cpp)\n"
        "target_include_directories(bha_apply_parity PRIVATE include)\n",
        encoding="utf-8",
    )
    (project_root / "include" / "used.hpp").write_text(
        "#pragma once\ninline int used_value() { return 7; }\n",
        encoding="utf-8",
    )
    (project_root / "include" / "unused.hpp").write_text(
        "#pragma once\ninline int unused_value() { return 9; }\n",
        encoding="utf-8",
    )
    (project_root / "src" / "main.cpp").write_text(
        '#include "used.hpp"\n#include "unused.hpp"\nint main() { return used_value(); }\n',
        encoding="utf-8",
    )


def source_snapshot(project_root: Path) -> dict[Path, bytes]:
    paths = [
        project_root / "CMakeLists.txt",
        project_root / "include" / "used.hpp",
        project_root / "include" / "unused.hpp",
        project_root / "src" / "main.cpp",
    ]
    return {path: path.read_bytes() for path in paths}


def restore_snapshot(snapshot: dict[Path, bytes]) -> None:
    for path, content in snapshot.items():
        path.write_bytes(content)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Path to the bha CLI")
    parser.add_argument("--lsp-server", type=Path, required=True, help="Path to bha-lsp")
    parser.add_argument("--timeout", type=int, default=300, help="Timeout per external command")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="bha apply parity ") as temporary_root:
        project_root = Path(temporary_root) / "project"
        project_root.mkdir()
        write_fixture(project_root)
        build_dir = project_root / "build"
        trace_dir = project_root / "traces"

        build = run_command(
            [
                str(args.binary),
                "build",
                "--build-system",
                "cmake",
                "--compiler",
                "clang",
                "--build-dir",
                str(build_dir),
                "--output",
                str(trace_dir),
                "--clean",
            ],
            project_root,
            args.timeout,
        )
        require(
            build.returncode == 0,
            f"Initial fixture build failed\nstdout:\n{build.stdout[-4000:]}\nstderr:\n{build.stderr[-4000:]}",
        )
        snapshot = source_snapshot(project_root)

        analysis_args = [
            "project",
            "analyze",
            "--project-root",
            str(project_root),
            "--build-dir",
            str(build_dir),
            "--trace-dir",
            str(trace_dir),
            "--type",
            "include-removal",
            "--include-unsafe",
            "--min-confidence",
            "0",
            "--json",
        ]
        cli_analysis = parse_json_output(
            run_command([str(args.binary), *analysis_args], project_root, args.timeout),
            "CLI analysis",
        )
        cli_suggestions = cli_analysis.get("suggestions") or []
        require(cli_suggestions, "CLI produced no evidence-backed include suggestion")
        suggestion_id = cli_suggestions[0].get("id")
        require(isinstance(suggestion_id, str) and suggestion_id, "CLI suggestion has no ID")

        cli_apply = parse_json_output(
            run_command(
                [
                    str(args.binary),
                    "project",
                    "apply",
                    "--project-root",
                    str(project_root),
                    "--build-dir",
                    str(build_dir),
                    "--trace-dir",
                    str(trace_dir),
                    "--suggestion-id",
                    suggestion_id,
                    "--type",
                    "include-removal",
                    "--include-unsafe",
                    "--min-confidence",
                    "0",
                    "--json",
                ],
                project_root,
                args.timeout,
            ),
            "CLI apply",
        )
        cli_bytes = source_snapshot(project_root)
        cli_normalized = normalized_apply_result(cli_apply, project_root)
        require(cli_normalized["success"] is True, f"CLI apply was not successful: {cli_apply}")
        require(cli_normalized["buildValidation"]["ran"] is True, "CLI did not run build validation")

        restore_snapshot(snapshot)
        shutil.rmtree(project_root / ".lsp-optimization-backup", ignore_errors=True)

        client = LSPClient(
            args.lsp_server,
            cwd=project_root,
            stderr_path=project_root / "lsp.stderr.log",
        )
        profile = {
            "projectRoot": project_root.as_uri(),
            "buildSystem": "cmake",
            "buildDir": build_dir.as_uri(),
            "traceOutputDir": trace_dir.as_uri(),
            "buildType": "Release",
            "compiler": "clang",
            "parallelJobs": 0,
        }
        settings = {
            "optimization": {
                "autoApplyAll": False,
                "showPreviewBeforeApply": False,
                "rebuildAfterApply": True,
                "rollbackOnBuildFailure": True,
                "buildCommand": f'cmake --build "{build_dir}"',
                "keepBackups": False,
                "allowMissingCompileCommands": False,
                "minConfidence": 0.0,
            }
        }
        try:
            client.start()
            initialized = client.initialize(project_root.as_uri(), settings)
            require(initialized is not None and "result" in initialized, "LSP initialize failed")
            client.send_notification("initialized")
            lsp_analysis_response = execute_command_with_timeout(
                client,
                "bha.analyze",
                [{
                    "projectRoot": project_root.as_uri(),
                    "buildDir": build_dir.as_uri(),
                    "traceDir": trace_dir.as_uri(),
                    "rebuild": False,
                    "enabledTypes": ["include-removal"],
                    "includeUnsafe": True,
                    "minConfidence": 0.0,
                }],
                args.timeout,
                "LSP analysis",
            )
            require(
                lsp_analysis_response is not None and "result" in lsp_analysis_response,
                f"LSP analysis failed: {lsp_analysis_response}",
            )
            lsp_analysis = lsp_analysis_response["result"]
            lsp_suggestions = lsp_analysis.get("suggestions") or []
            require(lsp_suggestions, "LSP produced no evidence-backed include suggestion")
            lsp_suggestion_id = lsp_suggestions[0].get("id")
            require(lsp_suggestion_id == suggestion_id, "CLI and LSP suggestion IDs diverged")

            lsp_apply_response = execute_command_with_timeout(
                client,
                "bha.applySuggestion",
                [{
                    "suggestionId": lsp_suggestion_id,
                    "skipConsent": True,
                    "skipRebuild": False,
                    "buildProfile": profile,
                }],
                args.timeout,
                "LSP apply",
            )
            require(
                lsp_apply_response is not None and "result" in lsp_apply_response,
                f"LSP apply failed: {lsp_apply_response}",
            )
            lsp_apply = lsp_apply_response["result"]

            lsp_bytes = source_snapshot(project_root)
            lsp_normalized = normalized_apply_result(lsp_apply, project_root)
            require(lsp_normalized["success"] is True, f"LSP apply was not successful: {lsp_apply}")
            require(lsp_normalized["buildValidation"]["ran"] is True, "LSP did not run build validation")

            restore_snapshot(snapshot)
            shutil.rmtree(project_root / ".lsp-optimization-backup", ignore_errors=True)
            raw_apply_response = execute_command_with_timeout(
                client,
                "bha.applyDirectEdits",
                [{
                    "projectRoot": project_root.as_uri(),
                    "skipRebuild": False,
                    "edits": [{
                        "file": (project_root / "src" / "main.cpp").as_uri(),
                        "startLine": 1,
                        "startCol": 0,
                        "endLine": 2,
                        "endCol": 0,
                        "newText": "",
                    }],
                }],
                args.timeout,
                "LSP raw edit apply",
            )
            require(
                raw_apply_response is not None and "result" in raw_apply_response,
                f"LSP raw edit apply failed: {raw_apply_response}",
            )
            raw_apply = raw_apply_response["result"]
            raw_bytes = source_snapshot(project_root)
            raw_normalized = normalized_apply_result(raw_apply, project_root)
            require(raw_normalized["success"] is True, f"LSP raw edit apply was not successful: {raw_apply}")
            require(raw_normalized["buildValidation"]["ran"] is True, "LSP raw edit did not run build validation")
            require(raw_bytes == lsp_bytes, "LSP raw edit and suggestion apply produced different source bytes")
            require(
                raw_normalized == lsp_normalized,
                f"Raw edit result mismatch:\nSuggestion: {lsp_normalized}\nRaw: {raw_normalized}",
            )
        finally:
            try:
                client.shutdown()
            except Exception:
                pass
            client.stop()

        require(cli_bytes == lsp_bytes, "CLI and LSP produced different source bytes")
        require(
            cli_normalized == lsp_normalized,
            f"Apply result mismatch:\nCLI: {cli_normalized}\nLSP: {lsp_normalized}",
        )

        print(json.dumps({"status": "ok", "result": cli_normalized}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
