#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def run_cmd(cmd: list[str], cwd: Path, timeout: int, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        text=True,
        capture_output=capture,
        timeout=timeout,
        check=False,
    )


def extract_json_array(raw: str) -> list[dict]:
    start = raw.find("[")
    if start < 0:
        raise ValueError("output does not contain a JSON array")
    return json.loads(raw[start:])


def run_targeted_unit_tests(repo_root: Path, timeout: int) -> int:
    print("[phase8] running targeted unit tests...")
    result = run_cmd(
        [
            "ctest",
            "--test-dir",
            str(repo_root / "build" / "tests"),
            "--output-on-failure",
            "-R",
            "(SuggestionTest|PimplEligibilityTest|PIMPLSuggesterTest)",
        ],
        cwd=repo_root,
        timeout=timeout,
        capture=True,
    )
    if result.returncode != 0:
        print("error: targeted unit tests failed")
        print((result.stdout or "")[-4000:])
        print((result.stderr or "")[-4000:])
        return 1
    print("ok: targeted unit tests passed")
    return 0


def run_consistency_matrix(repo_root: Path, compiler: str, timeout: int, refactor_bin: Path) -> int:
    print("[phase8] running consistency matrix...")
    result = run_cmd(
        [
            "python3",
            "-u",
            str(repo_root / "tests" / "run_pimpl_consistency_matrix.py"),
            "--timeout",
            str(timeout),
            "--compiler",
            compiler,
            "--refactor-bin",
            str(refactor_bin),
        ],
        cwd=repo_root,
        timeout=timeout * 2,
    )
    if result.returncode != 0:
        print("error: consistency matrix failed")
        return 1
    print("ok: consistency matrix passed")
    return 0


def run_external_apply_runner(repo_root: Path, compiler: str, timeout: int, refactor_bin: Path) -> int:
    print("[phase8] running external apply runner...")
    result = run_cmd(
        [
            "python3",
            "-u",
            str(repo_root / "tests" / "run_pimpl_external_subproject.py"),
            "--timeout",
            str(timeout),
            "--compiler",
            compiler,
            "--refactor-bin",
            str(refactor_bin),
        ],
        cwd=repo_root,
        timeout=timeout * 2,
    )
    if result.returncode != 0:
        print("error: external apply runner failed")
        return 1
    print("ok: external apply runner passed")
    return 0


def run_cli_advisory_blocker_check(repo_root: Path, compiler: str, timeout: int) -> int:
    print("[phase8] checking CLI advisory blocker details...")
    fixture = repo_root / "tests" / "subprojects" / "suggester_pimpl_external_explicit_copy"
    if not fixture.exists():
        print(f"error: missing fixture: {fixture}")
        return 2

    temp_root = Path("/tmp") / "bha-phase8-cli-advisory"
    project_root = temp_root / "project"
    if temp_root.exists():
        shutil.rmtree(temp_root)
    shutil.copytree(fixture, project_root)
    header = project_root / "include" / "pimpl_widget_external_explicit_copy.hpp"
    if not header.exists():
        print(f"error: missing header for advisory probe: {header}")
        return 2
    header_text = header.read_text()
    marker = "private:\n"
    if marker not in header_text:
        print("error: could not locate private section for advisory probe")
        return 2
    header_text = header_text.replace(
        marker,
        "private:\n#ifdef BHA_PIMPL_ADVISORY_PROBE\n#endif\n",
        1,
    )
    header.write_text(header_text)

    build_dir = project_root / "build"
    trace_dir = project_root / "traces"
    bha_bin = repo_root / "build" / "bha"

    build = run_cmd(
        [
            str(bha_bin),
            "build",
            "--build-system",
            "cmake",
            "--compiler",
            compiler,
            "--build-dir",
            str(build_dir),
            "--output",
            str(trace_dir),
            "--memory",
            "--clean",
        ],
        cwd=project_root,
        timeout=timeout,
        capture=True,
    )
    if build.returncode != 0:
        print("error: fixture build failed")
        print((build.stdout or "")[-4000:])
        print((build.stderr or "")[-4000:])
        return 1

    suggest = run_cmd(
        [
            str(bha_bin),
            "suggest",
            str(trace_dir),
            "--format",
            "json",
            "--include-unsafe",
            "--disable-consolidation",
            "--type",
            "pimpl",
            "--limit",
            "10",
        ],
        cwd=project_root,
        timeout=timeout,
        capture=True,
    )
    if suggest.returncode != 0:
        print("error: suggest command failed")
        print((suggest.stdout or "")[-4000:])
        print((suggest.stderr or "")[-4000:])
        return 1

    suggestions = extract_json_array(suggest.stdout)
    advisory_pimpl = [
        s for s in suggestions
        if s.get("type") == "PIMPL Pattern" and s.get("application_mode") == "advisory"
    ]
    if not advisory_pimpl:
        print("error: expected advisory PIMPL suggestion in CLI output")
        print(json.dumps(suggestions, indent=2))
        return 1

    blocked_reason = advisory_pimpl[0].get("auto_apply_blocked_reason", "")
    app_summary = advisory_pimpl[0].get("application_summary", "")
    app_guidance = advisory_pimpl[0].get("application_guidance", "")
    if not blocked_reason or not app_summary or not app_guidance:
        print("error: advisory metadata missing in CLI output")
        print(json.dumps(advisory_pimpl[0], indent=2))
        return 1

    print("ok: CLI advisory metadata present")
    return 0


def run_lsp_advisory_blocker_check(repo_root: Path, compiler: str, timeout: int, refactor_bin: Path) -> int:
    print("[phase8] checking LSP advisory blocker details...")
    fixture = repo_root / "tests" / "subprojects" / "suggester_pimpl_external_explicit_copy"
    if not fixture.exists():
        print(f"error: missing fixture: {fixture}")
        return 2

    temp_root = Path("/tmp") / "bha-phase8-lsp-advisory"
    project_root = temp_root / "project"
    if temp_root.exists():
        shutil.rmtree(temp_root)
    shutil.copytree(fixture, project_root)
    header = project_root / "include" / "pimpl_widget_external_explicit_copy.hpp"
    if not header.exists():
        print(f"error: missing header for advisory probe: {header}")
        return 2
    header_text = header.read_text()
    marker = "private:\n"
    if marker not in header_text:
        print("error: could not locate private section for advisory probe")
        return 2
    header_text = header_text.replace(
        marker,
        "private:\n#ifdef BHA_PIMPL_ADVISORY_PROBE\n#endif\n",
        1,
    )
    header.write_text(header_text)

    build_dir = project_root / "build"
    trace_dir = project_root / "traces"
    bha_bin = repo_root / "build" / "bha"

    build = run_cmd(
        [
            str(bha_bin),
            "build",
            "--build-system",
            "cmake",
            "--compiler",
            compiler,
            "--build-dir",
            str(build_dir),
            "--output",
            str(trace_dir),
            "--memory",
            "--clean",
        ],
        cwd=project_root,
        timeout=timeout,
        capture=True,
    )
    if build.returncode != 0:
        print("error: fixture build failed")
        print((build.stdout or "")[-4000:])
        print((build.stderr or "")[-4000:])
        return 1

    sys.path.insert(0, str((repo_root / "lsp" / "tests").resolve()))
    from lsp_test_client import LSPClient, default_server_path, test_analyze, test_initialize

    client = LSPClient(
        server_path=default_server_path(),
        cwd=project_root,
        stderr_path=temp_root / "phase8-lsp.stderr.log",
        env={"BHA_REFACTOR": str(refactor_bin)},
    )
    client.start()
    try:
        settings = {
            "optimization": {
                "showPreviewBeforeApply": False,
                "includeUnsafeSuggestions": True,
                "allowMissingCompileCommands": True,
            }
        }
        if not test_initialize(client, str(project_root), settings=settings):
            return 1
        client.send_notification("initialized")
        analysis = test_analyze(client, str(project_root), build_dir=str(build_dir), timeout_seconds=timeout)
        if not analysis:
            return 1

        advisory_pimpl = [
            s for s in analysis.get("suggestions", [])
            if "PIMPL" in s.get("title", "") and s.get("applicationMode") == "advisory"
        ]
        if not advisory_pimpl:
            print("error: expected advisory PIMPL suggestion in LSP output")
            print(json.dumps(analysis, indent=2))
            return 1

        first = advisory_pimpl[0]
        if not first.get("applicationSummary") or not first.get("applicationGuidance") or not first.get("autoApplyBlockedReason"):
            print("error: advisory metadata missing in LSP output")
            print(json.dumps(first, indent=2))
            return 1
    finally:
        try:
            client.shutdown()
        finally:
            client.stop()

    print("ok: LSP advisory metadata present")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Phase 8 PIMPL completion gate checks")
    parser.add_argument("--timeout", type=int, default=1800, help="timeout in seconds per major command")
    parser.add_argument("--compiler", default="clang", choices=["clang", "gcc"], help="compiler for fixture builds")
    parser.add_argument(
        "--refactor-bin",
        default="/tmp/bha-refactor-build/bha-refactor",
        help="path to bha-refactor binary",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    refactor_bin = Path(args.refactor_bin)
    if not refactor_bin.exists():
        print(f"error: bha-refactor binary not found: {refactor_bin}")
        return 2

    checks = [
        lambda: run_targeted_unit_tests(repo_root, args.timeout),
        lambda: run_consistency_matrix(repo_root, args.compiler, args.timeout, refactor_bin),
        lambda: run_external_apply_runner(repo_root, args.compiler, args.timeout, refactor_bin),
        lambda: run_cli_advisory_blocker_check(repo_root, args.compiler, args.timeout),
        lambda: run_lsp_advisory_blocker_check(repo_root, args.compiler, args.timeout, refactor_bin),
    ]

    for check in checks:
        exit_code = check()
        if exit_code != 0:
            return exit_code

    print("[phase8] all completion gate checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
