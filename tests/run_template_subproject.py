#!/usr/bin/env python3

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List


def run_cmd(cmd: List[str], cwd: Path, timeout: int, capture: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        text=True,
        capture_output=capture,
        timeout=timeout,
        check=False,
    )


def parse_json_array(stdout: str) -> list:
    start = stdout.find("[")
    if start < 0:
        return []
    try:
        return json.loads(stdout[start:])
    except json.JSONDecodeError:
        return []


def line_starts(text: str) -> List[int]:
    starts = [0]
    for idx, ch in enumerate(text):
        if ch == "\n":
            starts.append(idx + 1)
    return starts


def to_offset(starts: List[int], text_len: int, line: int, col: int) -> int:
    if line < 0:
        return 0
    if line >= len(starts):
        return text_len
    base = starts[line]
    return min(base + max(col, 0), text_len)


def apply_text_edits(edits: list[dict], project_root: Path) -> List[Path]:
    grouped: Dict[Path, list[dict]] = {}
    for edit in edits:
        file_path = Path(edit.get("file", ""))
        if not file_path:
            continue
        if not file_path.is_absolute():
            file_path = (project_root / file_path).resolve()
        grouped.setdefault(file_path, []).append(edit)

    changed: List[Path] = []
    for file_path, file_edits in grouped.items():
        text = file_path.read_text(encoding="utf-8") if file_path.exists() else ""
        starts = line_starts(text)
        indexed: list[tuple[int, int, str]] = []
        for edit in file_edits:
            s_line = int(edit.get("start_line", 0))
            s_col = int(edit.get("start_col", 0))
            e_line = int(edit.get("end_line", s_line))
            e_col = int(edit.get("end_col", s_col))
            new_text = str(edit.get("new_text", ""))
            s_off = to_offset(starts, len(text), s_line, s_col)
            e_off = to_offset(starts, len(text), e_line, e_col)
            indexed.append((s_off, e_off, new_text))

        indexed.sort(key=lambda item: (item[0], item[1]), reverse=True)
        updated = text
        for s_off, e_off, new_text in indexed:
            updated = updated[:s_off] + new_text + updated[e_off:]

        if updated != text:
            file_path.parent.mkdir(parents=True, exist_ok=True)
            file_path.write_text(updated, encoding="utf-8")
            changed.append(file_path)

    return changed


def prepare_fixture(fixture: Path, project_root: Path) -> None:
    project_root.mkdir(parents=True, exist_ok=True)
    shutil.copy2(fixture / "CMakeLists.txt", project_root / "CMakeLists.txt")

    include_src = fixture / "include"
    if include_src.exists():
        include_dst = project_root / "include"
        include_dst.mkdir(parents=True, exist_ok=True)
        for entry in include_src.iterdir():
            if entry.is_file():
                shutil.copy2(entry, include_dst / entry.name)

    src_src = fixture / "src"
    if src_src.exists():
        src_dst = project_root / "src"
        src_dst.mkdir(parents=True, exist_ok=True)
        for entry in src_src.iterdir():
            if entry.is_file():
                shutil.copy2(entry, src_dst / entry.name)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and validate explicit-template edits on dedicated subproject")
    parser.add_argument("--timeout", type=int, default=7200, help="timeout per external command in seconds")
    parser.add_argument(
        "--fixture",
        default="suggester_explicit_template",
        help="fixture name under tests/subprojects",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    fixture = repo_root / "tests" / "subprojects" / args.fixture
    work_root = repo_root / "tests" / "temp" / f"suggester_template_run_{args.fixture}"
    project_root = work_root / "project"
    build_dir = project_root / "build"
    trace_dir = project_root / "traces"

    bha_bin = repo_root / "build" / "bha"
    if not bha_bin.exists():
        print(f"error: bha binary missing: {bha_bin}")
        return 2
    if not fixture.exists():
        print(f"error: fixture missing: {fixture}")
        return 2

    if work_root.exists():
        shutil.rmtree(work_root)
    prepare_fixture(fixture, project_root)

    print("[1/5] Building fixture and collecting traces...")
    build_cmd = [
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
    ]
    build = run_cmd(build_cmd, project_root, args.timeout)
    if build.returncode != 0:
        print("error: bha build failed")
        print((build.stdout or "")[-4000:])
        print((build.stderr or "")[-4000:])
        return 1

    print("[2/5] Generating explicit-template suggestions...")
    suggest_cmd = [
        str(bha_bin),
        "suggest",
        str(trace_dir),
        "--format",
        "json",
        "--type",
        "explicit-template",
        "--disable-consolidation",
        "--limit",
        "100",
        "--template-min-count",
        "2",
        "--template-min-time",
        "1",
        "--include-unsafe",
    ]
    suggest = run_cmd(suggest_cmd, project_root, args.timeout)
    if suggest.returncode != 0:
        print("error: bha suggest failed")
        print((suggest.stdout or "")[-4000:])
        print((suggest.stderr or "")[-4000:])
        return 1

    suggestions = parse_json_array(suggest.stdout)
    print(f"  explicit-template suggestions before apply: {len(suggestions)}")
    if not suggestions:
        print("error: no explicit-template suggestions produced")
        return 1

    print("[3/5] Applying explicit-template edits iteratively...")
    iterations = 0
    max_iterations = 20
    while iterations < max_iterations:
        selected = next((item for item in suggestions if (item.get("edits") or [])), None)
        if selected is None:
            break

        iterations += 1
        selected_id = str(selected.get("id", ""))
        edits = selected.get("edits") or []
        changed_files = apply_text_edits(edits, project_root)
        if not changed_files:
            print(f"error: no files changed while applying suggestion {selected_id}")
            return 1
        print(f"  iteration {iterations}: applied {selected_id}, files changed: {len(changed_files)}")

        print("[4/5] Rebuilding project after edits...")
        rebuild = run_cmd(["cmake", "--build", str(build_dir), "-j4"], project_root, args.timeout)
        if rebuild.returncode != 0:
            print("error: rebuild failed after applying explicit-template edits")
            print((rebuild.stdout or "")[-4000:])
            print((rebuild.stderr or "")[-4000:])
            return 1

        print("[5/5] Re-running explicit-template suggestions...")
        suggest_after = run_cmd(suggest_cmd, project_root, args.timeout)
        if suggest_after.returncode != 0:
            print("error: post-apply suggest failed")
            print((suggest_after.stdout or "")[-4000:])
            print((suggest_after.stderr or "")[-4000:])
            return 1

        suggestions_after = parse_json_array(suggest_after.stdout)
        ids_after = {str(item.get("id", "")) for item in suggestions_after}
        print(f"  iteration {iterations}: explicit-template suggestions now: {len(suggestions_after)}")
        if selected_id and selected_id in ids_after:
            print(f"error: suggestion {selected_id} still present after applying its edits")
            return 1
        suggestions = suggestions_after

    if iterations >= max_iterations:
        print("error: reached iteration limit while applying explicit-template suggestions")
        return 1
    if suggestions:
        print(f"error: remaining explicit-template suggestions after iterative apply: {len(suggestions)}")
        return 1

    print(f"ok: explicit-template iterative edit flow validated in {project_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
