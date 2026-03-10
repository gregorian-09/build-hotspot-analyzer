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
        if not file_path.exists():
            continue
        text = file_path.read_text(encoding="utf-8")
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
            file_path.write_text(updated, encoding="utf-8")
            changed.append(file_path)

    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and validate move-to-cpp edits on dedicated subproject")
    parser.add_argument("--timeout", type=int, default=1800, help="timeout per external command in seconds")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    fixture = repo_root / "tests" / "subprojects" / "suggester_move_to_cpp"
    work_root = repo_root / "tests" / "temp" / "suggester_move_to_cpp_run"
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
    shutil.copytree(fixture, project_root)

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

    print("[2/5] Generating move-to-cpp suggestions...")
    suggest_cmd = [
        str(bha_bin),
        "suggest",
        str(trace_dir),
        "--format",
        "json",
        "--type",
        "move-to-cpp",
        "--disable-consolidation",
        "--limit",
        "100",
        "--include-unsafe",
    ]
    suggest = run_cmd(suggest_cmd, project_root, args.timeout)
    if suggest.returncode != 0:
        print("error: bha suggest failed")
        print((suggest.stdout or "")[-4000:])
        print((suggest.stderr or "")[-4000:])
        return 1

    suggestions = parse_json_array(suggest.stdout)
    print(f"  move-to-cpp suggestions before apply: {len(suggestions)}")
    if not suggestions:
        print("error: no move-to-cpp suggestions produced")
        return 1

    selected = next((item for item in suggestions if (item.get("edits") or [])), None)
    if selected is None:
        print("error: no move-to-cpp suggestion has text edits")
        return 1

    selected_id = str(selected.get("id", ""))
    edits = selected.get("edits") or []

    print("[3/5] Applying move-to-cpp edits from suggestion payload...")
    changed_files = apply_text_edits(edits, project_root)
    if not changed_files:
        print("error: no files changed after applying text edits")
        return 1
    print(f"  files changed: {len(changed_files)}")

    print("[4/5] Rebuilding project after edits...")
    rebuild = run_cmd(["cmake", "--build", str(build_dir), "-j4"], project_root, args.timeout)
    if rebuild.returncode != 0:
        print("error: rebuild failed after applying move-to-cpp edits")
        print((rebuild.stdout or "")[-4000:])
        print((rebuild.stderr or "")[-4000:])
        return 1

    print("[5/5] Re-running move-to-cpp suggestions...")
    suggest_after = run_cmd(suggest_cmd, project_root, args.timeout)
    if suggest_after.returncode != 0:
        print("error: post-apply suggest failed")
        print((suggest_after.stdout or "")[-4000:])
        print((suggest_after.stderr or "")[-4000:])
        return 1

    suggestions_after = parse_json_array(suggest_after.stdout)
    print(f"  move-to-cpp suggestions after apply: {len(suggestions_after)}")
    ids_after = {str(item.get("id", "")) for item in suggestions_after}
    if selected_id and selected_id in ids_after:
        print("error: selected move-to-cpp suggestion still present after applying edits")
        return 1

    print(f"ok: move-to-cpp edit flow validated in {project_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
