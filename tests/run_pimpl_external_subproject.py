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


def validate_fixture(
    *,
    repo_root: Path,
    bha_bin: Path,
    refactor_bin: Path,
    fixture_name: str,
    expected_application_mode: str,
    header_name: str,
    header_markers: list[str],
    source_markers: list[str],
    executable_name: str | None,
    timeout: int,
    compiler: str,
) -> int:
    fixture_root = repo_root / "tests" / "subprojects" / fixture_name
    if not fixture_root.exists():
        print(f"error: fixture missing: {fixture_root}")
        return 2

    temp_root = Path("/tmp") / f"bha-{fixture_name}"
    project_root = temp_root / "project"
    build_dir = project_root / "build"
    trace_dir = project_root / "traces"

    if temp_root.exists():
        shutil.rmtree(temp_root)
    shutil.copytree(fixture_root, project_root)

    print("[1/4] Building fixture and collecting traces...")
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

    print("[2/4] Checking CLI suggestion output...")
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
    pimpl_suggestions = [s for s in suggestions if s.get("type") == "PIMPL Pattern"]
    print(f"  total suggestions: {len(suggestions)}")
    print(f"  pimpl suggestions: {len(pimpl_suggestions)}")
    if expected_application_mode != "tooling-rejection":
        matching_pimpl = [
            s for s in pimpl_suggestions
            if s.get("application_mode") == expected_application_mode and not s.get("edits")
        ]
        print(f"  matching pimpl suggestions ({expected_application_mode}): {len(matching_pimpl)}")
        if not matching_pimpl:
            print(f"error: expected a {expected_application_mode} PIMPL suggestion without direct edits")
            return 1

    if expected_application_mode == "tooling-rejection":
        print("[3/4] Validating the Clang tooling hard-blocker path...")
        source_name = executable_name
        if not source_name:
            print("error: tooling-rejection fixture is missing a source file marker in executable_name")
            return 1
        refactor = run_cmd([
            str(refactor_bin),
            "pimpl",
            "--compile-commands", str(build_dir / "compile_commands.json"),
            "--source", str(project_root / "src" / source_name),
            "--header", str(project_root / "include" / header_name),
            "--class", header_markers[0],
            "--output-format", "json",
        ], cwd=project_root, timeout=timeout, capture=True)
        if refactor.returncode != 0:
            print("error: bha-refactor invocation failed for tooling-rejection validation")
            print((refactor.stdout or "")[-4000:])
            print((refactor.stderr or "")[-4000:])
            return 1
        try:
            refactor_result = json.loads(refactor.stdout)
        except json.JSONDecodeError as exc:
            print(f"error: failed to parse bha-refactor JSON: {exc}")
            print((refactor.stdout or "")[-4000:])
            return 1

        diagnostics = refactor_result.get("diagnostics", [])
        blocker = source_markers[0] if source_markers else ""
        if (
            refactor_result.get("success") or
            refactor_result.get("engine") != "clang-libtooling" or
            refactor_result.get("replacements") or
            not any(blocker in diagnostic.get("message", "") for diagnostic in diagnostics)
        ):
            print("error: expected a terminal clang-libtooling blocker result")
            print(json.dumps(refactor_result, indent=2))
            return 1
        print(f"ok: tooling blocker validated for {fixture_name}")
        return 0

    sys.path.insert(0, str((repo_root / "lsp" / "tests").resolve()))
    from lsp_test_client import (
        LSPClient,
        default_server_path,
        execute_command_with_timeout,
        test_analyze,
        test_initialize,
    )

    server_path = default_server_path()
    if not server_path.exists():
        print(f"error: bha-lsp binary not found: {server_path}")
        return 1

    if expected_application_mode == "external-refactor":
        print("[3/4] Applying the external PIMPL suggestion through the LSP...")
    else:
        print("[3/4] Validating the advisory PIMPL suggestion through the LSP...")
    client = LSPClient(
        server_path=server_path,
        cwd=project_root,
        stderr_path=temp_root / "external.lsp.stderr.log",
        env={"BHA_REFACTOR": str(refactor_bin)},
    )
    client.start()
    try:
        settings = None
        if expected_application_mode == "advisory":
            settings = {"optimization": {"includeUnsafeSuggestions": True}}
        if not test_initialize(client, str(project_root), settings=settings):
            return 1
        client.send_notification("initialized")

        analysis = test_analyze(client, str(project_root), build_dir=str(build_dir), timeout_seconds=timeout)
        if not analysis:
            return 1

        matching_ids = [
            suggestion["id"]
            for suggestion in analysis.get("suggestions", [])
            if "PIMPL" in suggestion.get("title", "") and suggestion.get("applicationMode") == expected_application_mode
        ]
        if not matching_ids:
            print(f"error: LSP analysis did not surface a {expected_application_mode} PIMPL suggestion")
            print(json.dumps(analysis, indent=2))
            return 1

        if expected_application_mode == "external-refactor":
            response = execute_command_with_timeout(
                client,
                "bha.applySuggestion",
                [{
                    "suggestionId": matching_ids[0],
                    "skipConsent": True,
                    "skipRebuild": False,
                }],
                timeout_seconds=timeout,
                timeout_label="Apply suggestion",
            )
            if not response or "result" not in response or not response["result"].get("success"):
                print("error: LSP failed to apply the external PIMPL suggestion")
                print(json.dumps(response, indent=2))
                return 1
    finally:
        try:
            client.shutdown()
        finally:
            client.stop()

    if expected_application_mode != "external-refactor":
        print(f"ok: advisory-only PIMPL suggestion validated for {fixture_name}")
        return 0

    print("[4/4] Rebuilding the rewritten temp project...")
    rebuild = run_cmd(["cmake", "--build", str(build_dir), "-j4"], cwd=project_root, timeout=timeout, capture=True)
    if rebuild.returncode != 0:
        print("error: rebuild failed after applying the external PIMPL refactor")
        print((rebuild.stdout or "")[-4000:])
        print((rebuild.stderr or "")[-4000:])
        return 1

    header_text = (project_root / "include" / header_name).read_text(encoding="utf-8")
    source_candidates = sorted((project_root / "src").glob("*.cpp"))
    source_text = "\n".join(candidate.read_text(encoding="utf-8") for candidate in source_candidates)
    required_markers = [
        "struct Impl;",
        "std::unique_ptr<Impl> pimpl_;",
        *header_markers,
        *source_markers,
    ]
    missing = [marker for marker in required_markers if marker not in header_text and marker not in source_text]
    if missing:
        print("error: rewritten files are missing expected external-refactor markers")
        for marker in missing:
            print(f"  - {marker}")
        return 1

    print(f"ok: external PIMPL refactor validated for {fixture_name} in {project_root}")
    if executable_name:
        executable_path = build_dir / executable_name
        if not executable_path.exists():
            print(f"error: expected executable missing after rebuild: {executable_path}")
            return 1
        run_result = run_cmd([str(executable_path)], cwd=project_root, timeout=timeout, capture=True)
        if run_result.returncode != 0:
            print("error: post-apply executable validation failed")
            print((run_result.stdout or "")[-4000:])
            print((run_result.stderr or "")[-4000:])
            return 1
        print(f"ok: post-apply executable succeeded for {fixture_name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the external PIMPL refactor path")
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
            "fixture_name": "suggester_pimpl_external",
            "expected_application_mode": "external-refactor",
            "header_name": "pimpl_widget_external.hpp",
            "header_markers": [
                "void update_total(int value);",
            ],
            "source_markers": [
                "struct WidgetExternal::Impl {",
                "pimpl_->values_.push_back(value);",
                "pimpl_->counters_[\"total\"] += value;",
                "pimpl_->fast_lookup_[\"total\"] = pimpl_->counters_[\"total\"];",
            ],
            "executable_name": None,
        },
        {
            "fixture_name": "suggester_pimpl_external_multiline",
            "expected_application_mode": "external-refactor",
            "header_name": "pimpl_widget_external_multiline.hpp",
            "header_markers": [
                "void update_total(",
                "        int value,",
                "        int bias = 0);",
                "friend struct WidgetExternalMultilineProbe;",
                "struct CounterTag {",
                "int value = 0;",
                "};",
                "using CounterMap = std::map<std::string, int>;",
            ],
            "source_markers": [
                "struct WidgetExternalMultiline::Impl {",
                "pimpl_->values_.push_back(value);",
                "pimpl_->counters_[\"total\"] += value + bias;",
                "pimpl_->fast_lookup_[\"total\"] = pimpl_->counters_[\"total\"];",
            ],
            "executable_name": None,
        },
        {
            "fixture_name": "suggester_pimpl_external_copyable",
            "expected_application_mode": "external-refactor",
            "header_name": "pimpl_widget_external_copyable.hpp",
            "header_markers": [
                "WidgetExternalCopyable(const WidgetExternalCopyable&);",
                "WidgetExternalCopyable& operator=(const WidgetExternalCopyable&);",
                "int bias() const;",
            ],
            "source_markers": [
                "struct WidgetExternalCopyable::Impl {",
                "WidgetExternalCopyable::WidgetExternalCopyable(const WidgetExternalCopyable& other)",
                "WidgetExternalCopyable& WidgetExternalCopyable::operator=(const WidgetExternalCopyable& other)",
                "pimpl_->values_.push_back(value);",
                "pimpl_->counters_[\"total\"] += value;",
                "pimpl_->fast_lookup_[\"total\"] = pimpl_->counters_[\"total\"];",
                "return pimpl_->label_ + std::to_string(pimpl_->expander_.value);",
            ],
            "executable_name": "copyable_probe",
        },
        {
            "fixture_name": "suggester_pimpl_external_explicit_copy",
            "expected_application_mode": "advisory",
            "header_name": "pimpl_widget_external_explicit_copy.hpp",
            "header_markers": [],
            "source_markers": [],
            "executable_name": None,
        },
        {
            "fixture_name": "suggester_pimpl_external_inheritance",
            "expected_application_mode": "advisory",
            "header_name": "pimpl_widget_external_inheritance.hpp",
            "header_markers": [],
            "source_markers": [],
            "executable_name": None,
        },
        {
            "fixture_name": "suggester_pimpl_external_template",
            "expected_application_mode": "advisory",
            "header_name": "widgettemplate.hpp",
            "header_markers": [],
            "source_markers": [],
            "executable_name": None,
        },
        {
            "fixture_name": "suggester_pimpl_external_inline_private",
            "expected_application_mode": "advisory",
            "header_name": "pimpl_widget_external_inline_method.hpp",
            "header_markers": [],
            "source_markers": [],
            "executable_name": None,
        },
        {
            "fixture_name": "suggester_pimpl_external_macro_decl",
            "expected_application_mode": "advisory",
            "header_name": "pimpl_widget_external_macro_decl.hpp",
            "header_markers": [],
            "source_markers": [],
            "executable_name": None,
        },
    ]

    for fixture in fixtures:
        exit_code = validate_fixture(
            repo_root=repo_root,
            bha_bin=bha_bin,
            refactor_bin=refactor_bin,
            fixture_name=fixture["fixture_name"],
            expected_application_mode=fixture["expected_application_mode"],
            header_name=fixture["header_name"],
            header_markers=fixture["header_markers"],
            source_markers=fixture["source_markers"],
            executable_name=fixture["executable_name"],
            timeout=args.timeout,
            compiler=args.compiler,
        )
        if exit_code != 0:
            return exit_code

    return 0


if __name__ == "__main__":
    sys.exit(main())
