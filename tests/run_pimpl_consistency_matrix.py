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


def extract_json_object(raw: str) -> dict:
    start = raw.find("{")
    if start < 0:
        raise ValueError("output does not contain a JSON object")
    return json.loads(raw[start:])


def prepare_temp_fixture(fixture_root: Path, temp_root: Path) -> Path:
    project_root = temp_root / "project"
    if temp_root.exists():
        shutil.rmtree(temp_root)
    shutil.copytree(
        fixture_root,
        project_root,
        ignore=shutil.ignore_patterns("build", "traces", ".bha_traces", "output"),
    )
    return project_root


def validate_fixture(
    *,
    repo_root: Path,
    bha_bin: Path,
    refactor_bin: Path,
    fixture_name: str,
    expected_mode: str,
    header_name: str,
    source_name: str,
    class_name: str,
    expect_refactor_success: bool,
    timeout: int,
    compiler: str,
) -> int:
    fixture_root = repo_root / "tests" / "subprojects" / fixture_name
    if not fixture_root.exists():
        print(f"error: fixture missing: {fixture_root}")
        return 2

    temp_root = Path("/tmp") / f"bha-matrix-{fixture_name}"
    project_root = prepare_temp_fixture(fixture_root, temp_root)
    build_dir = project_root / "build"
    trace_dir = project_root / "traces"

    print(f"[{fixture_name}] build + trace")
    build = run_cmd([
        str(bha_bin),
        "build",
        "--build-system", "cmake",
        "--compiler", compiler,
        "--build-dir", str(build_dir),
        "--output", str(trace_dir),
        "--memory",
        "--clean",
    ], cwd=project_root, timeout=timeout, capture=True)
    if build.returncode != 0:
        print("error: bha build failed")
        print((build.stdout or "")[-4000:])
        print((build.stderr or "")[-4000:])
        return 1

    print(f"[{fixture_name}] suggester mode")
    suggest = run_cmd([
        str(bha_bin),
        "suggest",
        str(trace_dir),
        "--format", "json",
        "--include-unsafe",
        "--disable-consolidation",
        "--type", "pimpl",
        "--limit", "50",
    ], cwd=project_root, timeout=timeout, capture=True)
    if suggest.returncode != 0:
        print("error: bha suggest failed")
        print((suggest.stdout or "")[-4000:])
        print((suggest.stderr or "")[-4000:])
        return 1

    suggestions = extract_json_array(suggest.stdout)
    pimpl_suggestions = [item for item in suggestions if item.get("type") == "PIMPL Pattern"]
    matching = [item for item in pimpl_suggestions if item.get("application_mode") == expected_mode]
    if not matching:
        print(f"error: expected a PIMPL suggestion with application_mode={expected_mode}")
        print(json.dumps(pimpl_suggestions, indent=2))
        return 1

    print(f"[{fixture_name}] bha-refactor outcome")
    refactor = run_cmd([
        str(refactor_bin),
        "pimpl",
        "--compile-commands", str(build_dir / "compile_commands.json"),
        "--source", str(project_root / "src" / source_name),
        "--header", str(project_root / "include" / header_name),
        "--class", class_name,
        "--output-format", "json",
    ], cwd=project_root, timeout=timeout, capture=True)
    if refactor.returncode != 0:
        print("error: bha-refactor invocation failed")
        print((refactor.stdout or "")[-4000:])
        print((refactor.stderr or "")[-4000:])
        return 1

    result = extract_json_object(refactor.stdout)
    replacements = result.get("replacements", [])
    refactor_success = bool(result.get("success"))

    if refactor_success != expect_refactor_success:
        print("error: refactor success did not match expected matrix outcome")
        print(json.dumps(result, indent=2))
        return 1

    if expect_refactor_success and not replacements:
        print("error: successful refactor returned no replacements")
        print(json.dumps(result, indent=2))
        return 1

    if not expect_refactor_success and replacements:
        print("error: rejected refactor returned replacements")
        print(json.dumps(result, indent=2))
        return 1

    print(
        f"ok: {fixture_name} => application_mode={expected_mode}, "
        f"refactor_success={'true' if refactor_success else 'false'}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate that PIMPL suggestion classification matches bha-refactor acceptance"
    )
    parser.add_argument("--timeout", type=int, default=1800, help="timeout in seconds per external command")
    parser.add_argument("--compiler", default="clang", choices=["clang", "gcc"], help="compiler for bha build")
    parser.add_argument(
        "--refactor-bin",
        default="/tmp/bha-refactor-build/bha-refactor",
        help="path to the bha-refactor binary",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    bha_bin = repo_root / "build" / "bha"
    refactor_bin = Path(args.refactor_bin)

    if not bha_bin.exists():
        print(f"error: bha binary not found: {bha_bin}")
        return 2
    if not refactor_bin.exists():
        print(f"error: bha-refactor binary not found: {refactor_bin}")
        return 2

    fixtures = [
        {
            "fixture_name": "suggester_pimpl",
            "expected_mode": "direct-edits",
            "header_name": "pimpl_widget.hpp",
            "source_name": "pimpl_widget.cpp",
            "class_name": "Widget",
            "expect_refactor_success": True,
        },
        {
            "fixture_name": "suggester_pimpl_external",
            "expected_mode": "direct-edits",
            "header_name": "pimpl_widget_external.hpp",
            "source_name": "pimpl_widget_external.cpp",
            "class_name": "WidgetExternal",
            "expect_refactor_success": True,
        },
        {
            "fixture_name": "suggester_pimpl_external_copyable",
            "expected_mode": "direct-edits",
            "header_name": "pimpl_widget_external_copyable.hpp",
            "source_name": "pimpl_widget_external_copyable.cpp",
            "class_name": "WidgetExternalCopyable",
            "expect_refactor_success": True,
        },
        {
            "fixture_name": "suggester_pimpl_external_explicit_copy",
            "expected_mode": "advisory",
            "header_name": "pimpl_widget_external_explicit_copy.hpp",
            "source_name": "pimpl_widget_external_explicit_copy.cpp",
            "class_name": "WidgetExternalExplicitCopy",
            "expect_refactor_success": False,
        },
        {
            "fixture_name": "suggester_pimpl_external_inheritance",
            "expected_mode": "advisory",
            "header_name": "pimpl_widget_external_inheritance.hpp",
            "source_name": "pimpl_widget_external_inheritance.cpp",
            "class_name": "WidgetExternalInheritance",
            "expect_refactor_success": False,
        },
        {
            "fixture_name": "suggester_pimpl_external_template",
            "expected_mode": "advisory",
            "header_name": "widgettemplate.hpp",
            "source_name": "widgettemplate.cpp",
            "class_name": "Widgettemplate",
            "expect_refactor_success": False,
        },
        {
            "fixture_name": "suggester_pimpl_external_inline_private",
            "expected_mode": "advisory",
            "header_name": "pimpl_widget_external_inline_method.hpp",
            "source_name": "pimpl_widget_external_inline_method.cpp",
            "class_name": "WidgetExternalInlinePrivate",
            "expect_refactor_success": False,
        },
        {
            "fixture_name": "suggester_pimpl_external_macro_decl",
            "expected_mode": "advisory",
            "header_name": "pimpl_widget_external_macro_decl.hpp",
            "source_name": "pimpl_widget_external_macro_decl.cpp",
            "class_name": "WidgetExternalMacroDecl",
            "expect_refactor_success": False,
        },
    ]

    for fixture in fixtures:
        exit_code = validate_fixture(
            repo_root=repo_root,
            bha_bin=bha_bin,
            refactor_bin=refactor_bin,
            fixture_name=fixture["fixture_name"],
            expected_mode=fixture["expected_mode"],
            header_name=fixture["header_name"],
            source_name=fixture["source_name"],
            class_name=fixture["class_name"],
            expect_refactor_success=fixture["expect_refactor_success"],
            timeout=args.timeout,
            compiler=args.compiler,
        )
        if exit_code != 0:
            return exit_code

    return 0


if __name__ == "__main__":
    sys.exit(main())
