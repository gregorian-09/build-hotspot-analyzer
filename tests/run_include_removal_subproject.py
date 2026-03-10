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


def clean_dir(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def extract_line(path: Path, line_index: int) -> str:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return ""
    if 0 <= line_index < len(lines):
        return lines[line_index]
    if 1 <= line_index <= len(lines):
        return lines[line_index - 1]
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and validate include-removal suggestions for the test subproject")
    parser.add_argument("--timeout", type=int, default=1200, help="timeout in seconds per external command")
    parser.add_argument("--compiler", default="clang", choices=["clang", "gcc"], help="compiler for bha build")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    bha_bin = repo_root / "build" / "bha"
    project_root = repo_root / "tests" / "subprojects" / "suggester_include_removal"
    build_dir = project_root / "build"
    trace_dir = project_root / "traces"
    output_dir = project_root / "output"
    output_json = output_dir / "suggestions.json"

    if not bha_bin.exists():
        print(f"error: bha binary not found: {bha_bin}")
        return 2
    if not project_root.exists():
        print(f"error: project missing: {project_root}")
        return 2

    clean_dir(build_dir)
    clean_dir(trace_dir)
    clean_dir(output_dir)

    build_cmd = [
        str(bha_bin),
        "build",
        "--build-system",
        "cmake",
        "--compiler",
        args.compiler,
        "--build-dir",
        str(build_dir),
        "--output",
        str(trace_dir),
        "--memory",
        "--clean",
    ]

    print("[1/2] Building subproject and collecting traces...")
    build = run_cmd(build_cmd, cwd=project_root, timeout=args.timeout, capture=True)
    if build.returncode != 0:
        print("error: bha build failed")
        if build.stdout:
            print(build.stdout[-4000:])
        if build.stderr:
            print(build.stderr[-4000:])
        return 1

    suggest_cmd = [
        str(bha_bin),
        "suggest",
        str(trace_dir),
        "--format",
        "json",
        "--include-unsafe",
        "--disable-consolidation",
        "--type",
        "include-removal",
        "--limit",
        "500",
    ]
    print("[2/2] Running suggestion engine...")
    suggest = run_cmd(suggest_cmd, cwd=project_root, timeout=args.timeout, capture=True)
    if suggest.returncode != 0:
        print("error: bha suggest failed")
        if suggest.stdout:
            print(suggest.stdout[-4000:])
        if suggest.stderr:
            print(suggest.stderr[-4000:])
        return 1

    raw = suggest.stdout
    json_start = raw.find("[")
    if json_start < 0:
        print("error: suggestion output does not contain json")
        if raw:
            print(raw[-4000:])
        return 1

    try:
        suggestions = json.loads(raw[json_start:])
    except json.JSONDecodeError as exc:
        print(f"error: invalid suggestion json: {exc}")
        return 1

    output_json.write_text(json.dumps(suggestions, indent=2), encoding="utf-8")

    include_removal = [s for s in suggestions if s.get("type") == "Include Removal"]
    evidence_backed = [s for s in include_removal if s.get("is_safe") and s.get("edits")]
    print(f"total suggestions: {len(suggestions)}")
    print(f"include-removal suggestions: {len(include_removal)}")
    print(f"evidence-backed include removals: {len(evidence_backed)}")

    if not include_removal:
        print("warning: no Include Removal suggestions were generated for this fixture")
        return 0

    if not evidence_backed:
        print("error: expected at least one evidence-backed include removal from clang-tidy")
        return 1

    guarded_markers = ("windows.h", "unistd.h")
    violations: list[str] = []

    for suggestion in include_removal:
        for edit in suggestion.get("edits", []):
            file_path = Path(edit.get("file", ""))
            if not file_path.is_absolute():
                file_path = (project_root / file_path).resolve()
            line_text = extract_line(file_path, int(edit.get("start_line", -1)))
            if any(marker in line_text for marker in guarded_markers):
                violations.append(f"{file_path}:{edit.get('start_line')}: {line_text.strip()}")

    if violations:
        print("error: cross-platform include guard lines were targeted for removal")
        for violation in violations:
            print(f"  - {violation}")
        return 1

    print(f"ok: suggestion output written to {output_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
