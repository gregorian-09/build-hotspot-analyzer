#!/usr/bin/env python3

"""Measure BHA suggestion application in isolated Clang/CMake project worktrees."""

import argparse
import json
import os
import platform
import shlex
import statistics
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "lsp" / "tests"))
from lsp_test_client import PROJECT_CMAKE_FLAGS, PROJECT_CMAKE_SUBDIR  # noqa: E402


class ExperimentError(RuntimeError):
    pass


def run_process(
    command: list[str], cwd: Path, timeout: int, log: Path, *, capture: bool = True,
) -> subprocess.CompletedProcess[str]:
    log.parent.mkdir(parents=True, exist_ok=True)
    if not capture:
        with log.open("w", encoding="utf-8") as output:
            output.write(f"$ {shlex.join(command)}\n\n")
            output.flush()
            try:
                result = subprocess.run(
                    command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT,
                    text=True, timeout=timeout, check=False,
                )
            except subprocess.TimeoutExpired as error:
                output.write(f"\nTimed out after {timeout}s\n")
                raise ExperimentError(f"Timed out after {timeout}s; see {log}") from error
            output.write(f"\nexit: {result.returncode}\n")
            return result
    try:
        result = subprocess.run(
            command, cwd=cwd, capture_output=True, text=True,
            timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired as error:
        log.write_text(
            f"$ {shlex.join(command)}\n\nTimed out after {timeout}s\n"
            f"stdout:\n{error.stdout or b''}\nstderr:\n{error.stderr or b''}\n",
            encoding="utf-8",
        )
        raise ExperimentError(f"Timed out after {timeout}s; see {log}") from error
    log.write_text(
        f"$ {shlex.join(command)}\nexit: {result.returncode}\n\n"
        f"stdout:\n{result.stdout}\n\nstderr:\n{result.stderr}\n",
        encoding="utf-8",
    )
    return result


def json_result(result: subprocess.CompletedProcess[str], log: Path) -> dict[str, Any]:
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ExperimentError(f"Expected JSON output; see {log}") from error
    if not isinstance(payload, dict):
        raise ExperimentError(f"Expected a JSON object; see {log}")
    return payload


def require_success(result: subprocess.CompletedProcess[str], log: Path) -> None:
    if result.returncode != 0:
        raise ExperimentError(f"Command exited {result.returncode}; see {log}")


def select_suggestion(analysis: dict[str, Any], requested_id: str | None) -> dict[str, Any] | None:
    suggestions = analysis.get("suggestions")
    if not isinstance(suggestions, list):
        raise ExperimentError("Analysis did not return a suggestions list")
    if requested_id and not any(isinstance(item, dict) and item.get("id") == requested_id for item in suggestions):
        raise ExperimentError(f"Suggestion {requested_id} was absent from the fresh analysis")
    eligible = [
        item for item in suggestions
        if isinstance(item, dict)
        and (not requested_id or item.get("id") == requested_id)
        and item.get("autoApplicable") is True
        and item.get("applicationMode") == "direct-edits"
        and isinstance(item.get("id"), str)
    ]
    if not eligible:
        if requested_id:
            raise ExperimentError(f"Requested suggestion {requested_id} is not direct and auto-applicable")
        return None

    def rank(item: dict[str, Any]) -> tuple[int, float, float, int, str]:
        priority = {"high": 0, "medium": 1, "low": 2}.get(item.get("priority"), 3)
        impact = item.get("estimatedImpact")
        savings = impact.get("timeSavedMs", 0) if isinstance(impact, dict) else 0
        savings = savings if isinstance(savings, (int, float)) and not isinstance(savings, bool) else 0
        confidence = item.get("confidence", 0)
        confidence = confidence if isinstance(confidence, (int, float)) and not isinstance(confidence, bool) else 0
        identifier = item["id"]
        numeric = identifier.removeprefix("ana-")
        return (priority, -savings, -confidence, int(numeric) if numeric.isdigit() else sys.maxsize, identifier)

    return min(eligible, key=rank)


def summarize_timings(baseline: list[int], post: list[int]) -> dict[str, float]:
    before = statistics.median(baseline)
    after = statistics.median(post)
    return {
        "baselineMedianMs": before,
        "postMedianMs": after,
        "savedMs": before - after,
        "savedPercent": round((before - after) / before * 100, 3),
    }


def project_args(args: argparse.Namespace, source: Path, build: Path, traces: Path) -> list[str]:
    return [
        "--project-root", str(source), "--build-dir", str(build),
        "--trace-dir", str(traces), "--build-system", "cmake",
        "--build-type", args.build_type, "--c-compiler", args.c_compiler,
        "--cxx-compiler", args.cxx_compiler, "--jobs", str(args.jobs),
    ]


def cmake_args(name: str) -> list[str]:
    return shlex.split(PROJECT_CMAKE_FLAGS.get(name, ""))


def first_version_line(command: list[str]) -> str | None:
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.splitlines()[0] if result.returncode == 0 and result.stdout else None


def run_case(name: str, args: argparse.Namespace, run_dir: Path) -> dict[str, Any]:
    case_dir = run_dir / name
    logs = case_dir / "logs"
    repo = (args.repos_root / name).resolve()
    result: dict[str, Any] = {
        "project": name, "repo": str(repo), "status": "pending",
        "revision": None, "source": None, "selectedSuggestion": None,
        "suggestionsFound": None, "baselineBuildMs": [], "postBuildMs": [],
        "baselineTraceDirs": [], "postTraceDirs": [],
        "traceCaptureBuildMs": {},
        "measurement": "Untraced CMake --build --clean-first wall time; configuration excluded",
        "timing": None, "buildValidation": None, "projectTests": "not-run",
        "reason": None, "logs": str(logs),
    }
    phase = "setup"
    try:
        if not repo.is_dir():
            raise ExperimentError(f"Repository missing: {repo}")
        if case_dir.resolve().is_relative_to(repo):
            raise ExperimentError("Output must be outside the source repository")

        revision_log = logs / "revision.log"
        revision = run_process(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            REPO_ROOT, args.timeout_seconds, revision_log,
        )
        require_success(revision, revision_log)
        result["revision"] = revision.stdout.strip()

        worktree = case_dir / "worktree"
        worktree_log = logs / "worktree.log"
        created = run_process(
            ["git", "-C", str(repo), "worktree", "add", "--detach", str(worktree), result["revision"]],
            REPO_ROOT, args.timeout_seconds, worktree_log, capture=False,
        )
        require_success(created, worktree_log)
        source = worktree / PROJECT_CMAKE_SUBDIR.get(name, "")
        if not (source / "CMakeLists.txt").is_file():
            raise ExperimentError(f"No CMakeLists.txt in worktree source: {source}")
        result["source"] = str(source)
        result["cmakeArgs"] = cmake_args(name) + args.cmake_arg

        build = case_dir / "build"
        plain_build = case_dir / "plain-build"
        extra = ["--extra-args", ";".join(result["cmakeArgs"])] if result["cmakeArgs"] else []

        def record(stage: str) -> Path:
            traces = case_dir / stage / "traces"
            log = logs / f"{stage}-trace-record.log"
            command = [
                str(args.bha_path), "project", "record", "--json",
                *project_args(args, source, build, traces), "--clean", *extra,
            ]
            process = run_process(command, source, args.timeout_seconds, log)
            require_success(process, log)
            payload = json_result(process, log)
            duration = payload.get("buildTimeMs")
            trace_files = payload.get("traceFiles")
            if not isinstance(duration, (int, float)) or isinstance(duration, bool) or duration <= 0:
                raise ExperimentError(f"Record returned no positive build wall time; see {log}")
            if not isinstance(trace_files, list) or not any(Path(item).is_file() for item in trace_files if isinstance(item, str)):
                raise ExperimentError(f"Record produced no compiler traces; see {log}")
            if not (build / "compile_commands.json").is_file():
                raise ExperimentError(f"Record produced no compilation database; see {log}")
            result["traceCaptureBuildMs"][stage] = int(duration)
            return traces

        def configure_plain(stage: str) -> None:
            log = logs / f"{stage}-configure.log"
            command = [
                "cmake", "-S", str(source), "-B", str(plain_build),
                f"-DCMAKE_BUILD_TYPE={args.build_type}",
                f"-DCMAKE_C_COMPILER={args.c_compiler}",
                f"-DCMAKE_CXX_COMPILER={args.cxx_compiler}",
                *result["cmakeArgs"],
            ]
            configured = run_process(command, source, args.timeout_seconds, log, capture=False)
            require_success(configured, log)

        def measure_plain(stage: str, number: int) -> int:
            log = logs / f"{stage}-clean-{number:02d}.log"
            command = [
                "cmake", "--build", str(plain_build), "--clean-first", "--parallel", str(args.jobs),
            ]
            started = time.monotonic_ns()
            measured = run_process(command, source, args.timeout_seconds, log, capture=False)
            duration_ms = (time.monotonic_ns() - started) // 1_000_000
            require_success(measured, log)
            if duration_ms <= 0:
                raise ExperimentError(f"Clean build had no measurable elapsed time; see {log}")
            return duration_ms

        phase = "baseline"
        last_traces = record("baseline")
        result["baselineTraceDirs"].append(str(last_traces))

        phase = "analysis"
        analyze_log = logs / "analyze.log"
        analyzed = run_process(
            [str(args.bha_path), "project", "analyze", "--json",
             *project_args(args, source, build, last_traces), *extra,
             "--min-confidence", str(args.min_confidence)],
            source, args.analysis_timeout_seconds, analyze_log,
        )
        require_success(analyzed, analyze_log)
        analysis = json_result(analyzed, analyze_log)
        candidate = select_suggestion(analysis, args.suggestion_id)
        result["suggestionsFound"] = len(analysis["suggestions"])
        if candidate is None:
            result["status"] = "no_applicable_suggestion"
            result["reason"] = "No direct, auto-applicable suggestion in the fresh analysis"
            return result
        result["selectedSuggestion"] = {
            key: candidate.get(key) for key in ("id", "type", "title", "confidence")
        }

        phase = "baseline"
        configure_plain("baseline")
        for number in range(1, args.runs + 1):
            result["baselineBuildMs"].append(measure_plain("baseline", number))

        phase = "apply"
        apply_log = logs / "apply.log"
        applied = run_process(
            [str(args.bha_path), "project", "apply", "--json",
             *project_args(args, source, build, last_traces), *extra,
             "--min-confidence", str(args.min_confidence),
             "--suggestion-id", candidate["id"]],
            source, args.timeout_seconds, apply_log,
        )
        application = json_result(applied, apply_log)
        result["application"] = application
        result["buildValidation"] = application.get("buildValidation")
        if (applied.returncode != 0 or application.get("success") is not True
                or not application.get("changedFiles")):
            raise ExperimentError(f"Suggestion did not apply with changed files; see {apply_log}")
        validation = application.get("buildValidation")
        if not isinstance(validation, dict) or validation.get("ran") is not True or validation.get("success") is not True:
            raise ExperimentError(f"Post-apply build validation did not pass; see {apply_log}")

        phase = "post_build"
        configure_plain("post")
        for number in range(1, args.runs + 1):
            result["postBuildMs"].append(measure_plain("post", number))
        result["timing"] = summarize_timings(result["baselineBuildMs"], result["postBuildMs"])

        phase = "post_trace"
        post_traces = record("post")
        result["postTraceDirs"].append(str(post_traces))

        phase = "tests"
        tests_list_log = logs / "ctest-list.log"
        listed = run_process(
            ["ctest", "--test-dir", str(plain_build), "--show-only=json-v1"],
            source, args.timeout_seconds, tests_list_log,
        )
        require_success(listed, tests_list_log)
        tests = json_result(listed, tests_list_log).get("tests")
        if not isinstance(tests, list):
            raise ExperimentError(f"CTest returned no test list; see {tests_list_log}")
        result["testCount"] = len(tests)
        if not tests:
            result["projectTests"] = "unavailable"
            if args.require_tests:
                raise ExperimentError("The project configured no CTest tests")
            result["status"] = "build_only"
            return result

        tests_log = logs / "ctest.log"
        tested = run_process(
            ["ctest", "--test-dir", str(plain_build), "--output-on-failure", "--parallel", str(args.jobs)],
            source, args.timeout_seconds, tests_log, capture=False,
        )
        require_success(tested, tests_log)
        result["projectTests"] = "passed"
        result["status"] = "validated"
        return result
    except (ExperimentError, OSError, ValueError) as error:
        result["status"] = f"{phase}_failed"
        result["reason"] = str(error)
        return result
    finally:
        case_dir.mkdir(parents=True, exist_ok=True)
        (case_dir / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", action="append", required=True, help="Name under --repos-root; repeatable")
    parser.add_argument("--repos-root", type=Path, default=REPO_ROOT / "tests" / "cli" / "repos")
    parser.add_argument("--output-root", type=Path, default=REPO_ROOT / "tests" / "cli" / "benchmarks")
    parser.add_argument("--bha-path", type=Path, default=REPO_ROOT / "build-linux-clang" / "bha")
    parser.add_argument("--c-compiler", default="clang")
    parser.add_argument("--cxx-compiler", default="clang++")
    parser.add_argument("--build-type", default="Release")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Extra CMake option; repeatable")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--runs", type=int, default=3, help="Untraced clean builds per state")
    parser.add_argument("--timeout-seconds", type=int, default=3600)
    parser.add_argument("--analysis-timeout-seconds", type=int, default=600)
    parser.add_argument("--min-confidence", type=float, default=0.5)
    parser.add_argument("--suggestion-id", help="Apply this analyzed ID if direct and eligible")
    parser.add_argument("--require-tests", action="store_true", help="Fail if the project has no CTest tests")
    args = parser.parse_args()
    if args.jobs < 1 or args.runs < 1 or args.timeout_seconds < 1 or args.analysis_timeout_seconds < 1:
        parser.error("jobs, runs, and timeouts must be positive")
    if not 0 <= args.min_confidence <= 1:
        parser.error("min-confidence must be between 0 and 1")
    args.repos_root = args.repos_root.resolve()
    args.output_root = args.output_root.resolve()
    args.bha_path = args.bha_path.resolve()
    if not args.bha_path.is_file():
        parser.error(f"BHA CLI does not exist: {args.bha_path}")
    for name in args.project:
        if name in {"", ".", ".."} or Path(name).name != name:
            parser.error(f"Invalid project name: {name}")
    if len(args.project) != len(set(args.project)):
        parser.error("Project names must be unique within a run")
    return args


def main() -> int:
    args = parse_args()
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    run_dir = args.output_root / timestamp
    run_dir.mkdir(parents=True)
    records = []
    for name in args.project:
        record = run_case(name, args, run_dir)
        records.append(record)
        print(f"{name}: {record['status']} (result: {run_dir / name / 'result.json'})", flush=True)
    summary = {
        "schema": "bha.repo-apply-benchmark.v2",
        "startedAtUtc": timestamp,
        "runDirectory": str(run_dir),
        "host": platform.platform(),
        "cpuCount": os.cpu_count(),
        "tools": {
            "bha": first_version_line([str(args.bha_path), "version"]),
            "cmake": first_version_line(["cmake", "--version"]),
            "cxx": first_version_line([args.cxx_compiler, "--version"]),
        },
        "compiler": {"c": args.c_compiler, "cxx": args.cxx_compiler},
        "runsPerState": args.runs,
        "statusCounts": dict(Counter(record["status"] for record in records)),
        "records": records,
    }
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# BHA Repository Apply Benchmark", "",
        "Untraced `cmake --build --clean-first` wall time; CMake configuration is excluded.",
        "Positive savings mean the post-edit median was faster. Results without passing project tests are labeled accordingly.",
        "",
        "| Project | Revision | Status | Baseline median | Post median | Savings | Tests |",
        "| --- | --- | --- | ---: | ---: | ---: | --- |",
    ]
    for record in records:
        timing = record["timing"] or {}
        before = f"{timing['baselineMedianMs']:.1f} ms" if timing else "n/a"
        after = f"{timing['postMedianMs']:.1f} ms" if timing else "n/a"
        savings = f"{timing['savedPercent']:+.3f}%" if timing else "n/a"
        lines.append(
            f"| {record['project']} | {(record['revision'] or 'n/a')[:12]} | {record['status']} | "
            f"{before} | {after} | {savings} | {record['projectTests']} |"
        )
    lines.append("")
    (run_dir / "summary.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"Summary: {run_dir / 'summary.json'}")
    return 1 if any(record["status"].endswith("_failed") for record in records) else 0


if __name__ == "__main__":
    raise SystemExit(main())
