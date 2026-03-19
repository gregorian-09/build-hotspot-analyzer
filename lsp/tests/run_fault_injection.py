#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, Optional

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from lsp_test_client import LSPClient, default_server_path, execute_command_with_timeout  # noqa: E402


def run_cmd(cmd: list[str], cwd: Path, timeout: int = 600) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def ensure(ok: bool, message: str):
    if not ok:
        raise RuntimeError(message)


def setup_client(
    server_path: Path,
    project_root: Path,
    settings: Dict[str, Any],
    stderr_name: str,
) -> LSPClient:
    stderr_path = project_root / stderr_name
    client = LSPClient(server_path, cwd=project_root, stderr_path=stderr_path)
    client.start()
    init = client.initialize(f"file://{project_root}", settings)
    ensure(init is not None and "result" in init, "LSP initialize failed")
    client.send_notification("initialized")
    return client


def optimization_settings(
    project_root: Path,
    build_command: str,
    build_timeout: int = 300,
) -> Dict[str, Any]:
    return {
        "optimization": {
            "autoApplyAll": False,
            "showPreviewBeforeApply": False,
            "rebuildAfterApply": True,
            "rollbackOnBuildFailure": True,
            "buildCommand": build_command,
            "buildTimeout": build_timeout,
            "keepBackups": True,
            "allowMissingCompileCommands": True,
            "backupDirectory": str((project_root / ".lsp-optimization-backup").resolve()),
        }
    }


def make_line_replace_edit(file_path: Path, old_line_text: str, new_line_text: str) -> Dict[str, Any]:
    lines = file_path.read_text(encoding="utf-8").splitlines()
    for idx, line in enumerate(lines):
        if line.strip() == old_line_text.strip():
            return {
                "file": str(file_path),
                "start_line": idx,
                "start_col": 0,
                "end_line": idx,
                "end_col": len(line),
                "new_text": new_line_text,
            }
    raise RuntimeError(f"Target line not found in {file_path}: {old_line_text}")


def scenario_semantic_forward_decl_rollback(server_path: Path, bha_bin: Path, workspace_root: Path):
    print("[scenario 1] stale forward-decl semantic breakage triggers rollback")
    fixture = REPO_ROOT / "tests" / "subprojects" / "suggester_forward_decl"
    project_root = workspace_root / "scenario_forward_decl"
    shutil.copytree(fixture, project_root)

    build_dir = project_root / "build"
    trace_dir = project_root / "traces"

    build = run_cmd(
        [
            str(bha_bin),
            "build",
            "--build-system",
            "cmake",
            "--compiler",
            "clang",
            "--build-dir",
            str(build_dir),
            "--output",
            str(trace_dir),
            "--memory",
            "--clean",
        ],
        project_root,
        timeout=1200,
    )
    ensure(build.returncode == 0, f"Initial fixture build failed:\n{build.stdout}\n{build.stderr}")

    settings = optimization_settings(
        project_root,
        build_command=f"cmake --build \"{build_dir}\" -j4",
        build_timeout=300,
    )

    client = setup_client(server_path, project_root, settings, "lsp-scenario1.err")
    try:
        analysis = execute_command_with_timeout(
            client,
            "bha.analyze",
            [{
                "projectRoot": str(project_root),
                "buildDir": str(build_dir),
                "rebuild": False,
            }],
            timeout_seconds=600,
            timeout_label="scenario1 analyze",
        )
        ensure(analysis is not None and "result" in analysis, "Analyze command failed")
        suggestions = analysis["result"].get("suggestions", [])
        ensure(bool(suggestions), "No suggestions were produced for scenario 1")

        forward = None
        for suggestion in suggestions:
            if "forward" in str(suggestion.get("type", "")).lower():
                forward = suggestion
                break
        if forward is None:
            forward = suggestions[0]
        suggestion_id = forward.get("id", "")
        ensure(bool(suggestion_id), "Forward-decl suggestion id missing")

        alpha_hpp = project_root / "include" / "alpha.hpp"
        stale_content = alpha_hpp.read_text(encoding="utf-8")
        stale_content += "\nstatic_assert(sizeof(fwd_decl::Beta) > 0, \"force complete type\");\n"
        alpha_hpp.write_text(stale_content, encoding="utf-8")

        apply = execute_command_with_timeout(
            client,
            "bha.applySuggestion",
            [{"suggestionId": suggestion_id, "skipConsent": True}],
            timeout_seconds=300,
            timeout_label="scenario1 applySuggestion",
        )
        ensure(apply is not None and "result" in apply, "applySuggestion failed in scenario 1")
        result = apply["result"]
        ensure(not result.get("success", False), "Scenario 1 expected apply failure due semantic gate")
        after = alpha_hpp.read_text(encoding="utf-8")
        ensure(after == stale_content, "Scenario 1 rollback did not restore pre-apply file content")
    finally:
        try:
            client.shutdown()
        except Exception:
            pass
        client.stop()

    print("  ✓ scenario 1 passed")


def create_link_fail_fixture(project_root: Path):
    (project_root / "include").mkdir(parents=True, exist_ok=True)
    (project_root / "src").mkdir(parents=True, exist_ok=True)

    (project_root / "CMakeLists.txt").write_text(
        """cmake_minimum_required(VERSION 3.20)
project(lsp_fault_injection LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(core STATIC src/foo.cpp)
target_include_directories(core PUBLIC include)

add_executable(app src/main.cpp)
target_link_libraries(app PRIVATE core)
""",
        encoding="utf-8",
    )
    (project_root / "include" / "foo.hpp").write_text(
        """#pragma once
int foo();
""",
        encoding="utf-8",
    )
    (project_root / "src" / "foo.cpp").write_text(
        """#include "foo.hpp"

int foo() { return 7; }
""",
        encoding="utf-8",
    )
    (project_root / "src" / "main.cpp").write_text(
        """#include "foo.hpp"
#include <iostream>

int main() {
    std::cout << foo() << "\\n";
    return 0;
}
""",
        encoding="utf-8",
    )


def configure_and_build_fixture(project_root: Path, build_dir: Path):
    configure = run_cmd(
        [
            "cmake",
            "-S",
            str(project_root),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ],
        project_root,
        timeout=300,
    )
    ensure(configure.returncode == 0, f"CMake configure failed:\n{configure.stdout}\n{configure.stderr}")
    build = run_cmd(["cmake", "--build", str(build_dir), "-j4"], project_root, timeout=300)
    ensure(build.returncode == 0, f"Fixture build failed:\n{build.stdout}\n{build.stderr}")


def scenario_build_failure_rollback(server_path: Path, workspace_root: Path):
    print("[scenario 2] syntax-valid edit causes build failure and rollback")
    project_root = workspace_root / "scenario_build_failure"
    create_link_fail_fixture(project_root)
    build_dir = project_root / "build"
    configure_and_build_fixture(project_root, build_dir)

    foo_cpp = project_root / "src" / "foo.cpp"
    original = foo_cpp.read_text(encoding="utf-8")
    edit = make_line_replace_edit(foo_cpp, "int foo() { return 7; }", "int foo_impl() { return 7; }")

    settings = optimization_settings(
        project_root,
        build_command=f"cmake --build \"{build_dir}\" -j4",
        build_timeout=300,
    )
    client = setup_client(server_path, project_root, settings, "lsp-scenario2.err")
    try:
        apply = execute_command_with_timeout(
            client,
            "bha.applyEdits",
            [{"edits": [edit]}],
            timeout_seconds=300,
            timeout_label="scenario2 applyEdits",
        )
        ensure(apply is not None and "result" in apply, "applyEdits failed in scenario 2")
        result = apply["result"]
        ensure(not result.get("success", False), "Scenario 2 expected apply failure from link/build failure")

        rollback = result.get("rollback", {})
        ensure(rollback.get("attempted", False), "Scenario 2 expected rollback attempt")
        ensure(rollback.get("success", False), "Scenario 2 expected rollback success")
        ensure(foo_cpp.read_text(encoding="utf-8") == original, "Scenario 2 rollback did not restore foo.cpp")
    finally:
        try:
            client.shutdown()
        except Exception:
            pass
        client.stop()

    print("  ✓ scenario 2 passed")


def scenario_kill_mid_apply_recovery(server_path: Path, workspace_root: Path):
    print("[scenario 3] kill mid-apply and recover clean tree after restart")
    project_root = workspace_root / "scenario_kill_mid_apply"
    create_link_fail_fixture(project_root)
    build_dir = project_root / "build"
    configure_and_build_fixture(project_root, build_dir)

    foo_cpp = project_root / "src" / "foo.cpp"
    original = foo_cpp.read_text(encoding="utf-8")
    mutated_line = "int foo_impl() { return 7; }"
    edit = make_line_replace_edit(foo_cpp, "int foo() { return 7; }", mutated_line)

    settings = optimization_settings(
        project_root,
        build_command=f"sleep 30 && cmake --build \"{build_dir}\" -j4",
        build_timeout=120,
    )

    backup_dir = project_root / ".lsp-optimization-backup"
    client = setup_client(server_path, project_root, settings, "lsp-scenario3.err")
    try:
        queued = execute_command_with_timeout(
            client,
            "bha.applyEdits",
            [{"edits": [edit], "async": True}],
            timeout_seconds=20,
            timeout_label="scenario3 queue async applyEdits",
        )
        ensure(queued is not None and "result" in queued, "Scenario 3 async apply queue failed")
        ensure(queued["result"].get("accepted", False), "Scenario 3 async apply was not accepted")

        changed_observed = False
        for _ in range(60):
            text = foo_cpp.read_text(encoding="utf-8")
            backups = [p for p in backup_dir.glob("*") if p.is_dir()]
            if mutated_line in text and backups:
                changed_observed = True
                break
            time.sleep(0.1)
        ensure(changed_observed, "Scenario 3 could not observe mid-apply mutated state before kill")

        ensure(client.process is not None, "Scenario 3 client process missing")
        client.process.kill()
        client.process.wait(timeout=5)
    finally:
        client.stop()

    # Restart and recover using the most recent durable backup.
    client2 = setup_client(server_path, project_root, settings, "lsp-scenario3-restart.err")
    try:
        mutated_after_restart = mutated_line in foo_cpp.read_text(encoding="utf-8")
        ensure(mutated_after_restart, "Scenario 3 expected dirty file after crash before recovery")

        backups = sorted([p for p in backup_dir.glob("*") if p.is_dir()], key=lambda p: p.stat().st_mtime)
        ensure(bool(backups), "Scenario 3 expected durable backups after crash")
        backup_id = backups[-1].name

        revert = execute_command_with_timeout(
            client2,
            "bha.revertChanges",
            [{"backupId": backup_id}],
            timeout_seconds=60,
            timeout_label="scenario3 revertChanges",
        )
        ensure(revert is not None and "result" in revert, "Scenario 3 revertChanges failed")
        ensure(revert["result"].get("success", False), "Scenario 3 revert did not succeed")
        ensure(foo_cpp.read_text(encoding="utf-8") == original, "Scenario 3 tree not clean after restart recovery")
    finally:
        try:
            client2.shutdown()
        except Exception:
            pass
        client2.stop()

    print("  ✓ scenario 3 passed")


def scenario_apply_all_skip_rebuild(server_path: Path, workspace_root: Path):
    print("[scenario 4] applyAllSuggestions honors skipRebuild")
    project_root = workspace_root / "scenario_apply_all_skip_rebuild"
    create_link_fail_fixture(project_root)
    build_dir = project_root / "build"
    configure_and_build_fixture(project_root, build_dir)

    settings = optimization_settings(
        project_root,
        build_command=f"cmake --build \"{build_dir}\" -j4",
        build_timeout=300,
    )
    client = setup_client(server_path, project_root, settings, "lsp-scenario4.err")
    try:
        apply_all = execute_command_with_timeout(
            client,
            "bha.applyAllSuggestions",
            [{
                "skipConsent": True,
                "safeOnly": False,
                "skipRebuild": True,
            }],
            timeout_seconds=300,
            timeout_label="scenario4 applyAllSuggestions",
        )
        ensure(apply_all is not None and "result" in apply_all, "Scenario 4 applyAllSuggestions failed")
        result = apply_all["result"]
        ensure(result.get("success", False), "Scenario 4 expected applyAllSuggestions success")

        build_validation = result.get("buildValidation", {})
        ensure(
            build_validation.get("requested") is False and build_validation.get("ran") is False,
            f"Scenario 4 expected skipRebuild to disable validation, got: {build_validation}",
        )
    finally:
        try:
            client.shutdown()
        except Exception:
            pass
        client.stop()

    print("  ✓ scenario 4 passed")


def scenario_async_job_retention_bound(server_path: Path, workspace_root: Path):
    print("[scenario 5] async finished jobs are pruned to bounded retention")
    project_root = workspace_root / "scenario_async_job_retention"
    create_link_fail_fixture(project_root)
    build_dir = project_root / "build"
    configure_and_build_fixture(project_root, build_dir)

    settings = optimization_settings(
        project_root,
        build_command=f"cmake --build \"{build_dir}\" -j4",
        build_timeout=300,
    )
    client = setup_client(server_path, project_root, settings, "lsp-scenario5.err")
    try:
        total_jobs = 150
        job_ids: list[str] = []
        for _ in range(total_jobs):
            queued = execute_command_with_timeout(
                client,
                "bha.applyAllSuggestions",
                [{
                    "skipConsent": True,
                    "safeOnly": False,
                    "skipRebuild": True,
                    "async": True,
                }],
                timeout_seconds=30,
                timeout_label="scenario5 queue applyAllSuggestions",
            )
            ensure(queued is not None and "result" in queued, "Scenario 5 failed to queue async applyAllSuggestions")
            result = queued["result"]
            ensure(result.get("accepted", False), "Scenario 5 async applyAllSuggestions not accepted")
            job_id = result.get("jobId", "")
            ensure(bool(job_id), "Scenario 5 queued job missing jobId")
            job_ids.append(job_id)

        def wait_for_finished(job_id: str, timeout_seconds: int = 120):
            deadline = time.time() + timeout_seconds
            while time.time() < deadline:
                status = execute_command_with_timeout(
                    client,
                    "bha.getJobStatus",
                    [{"jobId": job_id}],
                    timeout_seconds=15,
                    timeout_label="scenario5 getJobStatus",
                )
                ensure(status is not None and "result" in status, "Scenario 5 getJobStatus failed")
                payload = status["result"]
                if payload.get("found") and payload.get("finished"):
                    return
                time.sleep(0.05)
            raise RuntimeError(f"Scenario 5 timeout waiting for job completion: {job_id}")

        wait_for_finished(job_ids[-1])
        wait_for_finished(job_ids[len(job_ids) // 2])

        for _ in range(10):
            execute_command_with_timeout(
                client,
                "bha.getJobStatus",
                [{"jobId": job_ids[-1]}],
                timeout_seconds=15,
                timeout_label="scenario5 trigger cleanup",
            )

        oldest = execute_command_with_timeout(
            client,
            "bha.getJobStatus",
            [{"jobId": job_ids[0]}],
            timeout_seconds=15,
            timeout_label="scenario5 oldest status",
        )
        newest = execute_command_with_timeout(
            client,
            "bha.getJobStatus",
            [{"jobId": job_ids[-1]}],
            timeout_seconds=15,
            timeout_label="scenario5 newest status",
        )

        ensure(oldest is not None and "result" in oldest, "Scenario 5 missing oldest status response")
        ensure(newest is not None and "result" in newest, "Scenario 5 missing newest status response")
        ensure(newest["result"].get("found", False), "Scenario 5 expected newest job to remain queryable")
        ensure(not oldest["result"].get("found", False), "Scenario 5 expected oldest finished job to be pruned")
    finally:
        try:
            client.shutdown()
        except Exception:
            pass
        client.stop()

    print("  ✓ scenario 5 passed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run LSP fault-injection rollback tests")
    parser.add_argument("--server-path", type=Path, default=default_server_path(), help="Path to bha-lsp executable")
    parser.add_argument("--bha-path", type=Path, default=(REPO_ROOT / "build" / "bha"), help="Path to bha executable")
    parser.add_argument("--keep-workdir", action="store_true", help="Keep temporary workspace")
    args = parser.parse_args()

    server_path = args.server_path.resolve()
    bha_bin = args.bha_path.resolve()
    ensure(server_path.exists(), f"Server executable missing: {server_path}")
    ensure(bha_bin.exists(), f"BHA executable missing: {bha_bin}")

    workspace_base = REPO_ROOT / "tests" / "temp"
    workspace_base.mkdir(parents=True, exist_ok=True)
    temp_dir = Path(tempfile.mkdtemp(prefix="lsp-fault-injection-", dir=str(workspace_base)))
    print(f"workspace: {temp_dir}")

    try:
        scenario_semantic_forward_decl_rollback(server_path, bha_bin, temp_dir)
        scenario_build_failure_rollback(server_path, temp_dir)
        scenario_kill_mid_apply_recovery(server_path, temp_dir)
        scenario_apply_all_skip_rebuild(server_path, temp_dir)
        scenario_async_job_retention_bound(server_path, temp_dir)
    finally:
        if args.keep_workdir:
            print(f"kept workspace: {temp_dir}")
        else:
            shutil.rmtree(temp_dir, ignore_errors=True)

    print("ok: all fault-injection scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
