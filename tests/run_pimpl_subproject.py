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


def apply_empty_body_variant(project_root: Path) -> None:
    header_path = project_root / "include" / "pimpl_widget.hpp"
    source_path = project_root / "src" / "pimpl_widget.cpp"

    header_text = header_path.read_text(encoding="utf-8")
    header_text = header_text.replace("    Widget();\n", "    Widget() noexcept;\n")
    header_text = header_text.replace("    ~Widget();\n", "    ~Widget() noexcept;\n")
    header_path.write_text(header_text, encoding="utf-8")

    source_text = source_path.read_text(encoding="utf-8")
    source_text = source_text.replace("Widget::Widget() = default;\n", "Widget::Widget() noexcept {}\n")
    source_text = source_text.replace("Widget::~Widget() = default;\n", "Widget::~Widget() noexcept {}\n")
    source_path.write_text(source_text, encoding="utf-8")


def apply_copyable_variant(project_root: Path) -> None:
    header_path = project_root / "include" / "pimpl_widget.hpp"
    header_text = header_path.read_text(encoding="utf-8")
    header_text = header_text.replace("    Widget(const Widget&) = delete;\n", "")
    header_text = header_text.replace("    Widget& operator=(const Widget&) = delete;\n", "")
    header_path.write_text(header_text, encoding="utf-8")


def apply_copyable_noexcept_variant(project_root: Path) -> None:
    header_path = project_root / "include" / "pimpl_widget.hpp"
    header_text = header_path.read_text(encoding="utf-8")
    header_text = header_text.replace("    Widget(const Widget&) = delete;\n", "    Widget(const Widget&) noexcept;\n")
    header_text = header_text.replace("    Widget& operator=(const Widget&) = delete;\n", "    Widget& operator=(const Widget&) noexcept;\n")
    header_path.write_text(header_text, encoding="utf-8")


def apply_copy_defaulted_variant(project_root: Path) -> None:
    header_path = project_root / "include" / "pimpl_widget.hpp"
    header_text = header_path.read_text(encoding="utf-8")
    header_text = header_text.replace("    Widget(const Widget&) = delete;\n", "    Widget(const Widget&) = default;\n")
    header_text = header_text.replace("    Widget& operator=(const Widget&) = delete;\n", "    Widget& operator=(const Widget&) = default;\n")
    header_path.write_text(header_text, encoding="utf-8")


def apply_shadowed_local_variant(project_root: Path) -> None:
    source_path = project_root / "src" / "pimpl_widget.cpp"
    source_text = source_path.read_text(encoding="utf-8")
    source_text = source_text.replace(
        "std::string Widget::label() const {\n    return label_ + std::to_string(expander_.value);\n}\n",
        "std::string Widget::label() const {\n    std::string label_ = \"local\";\n    return label_;\n}\n",
    )
    source_path.write_text(source_text, encoding="utf-8")


def apply_lambda_variant(project_root: Path) -> None:
    source_path = project_root / "src" / "pimpl_widget.cpp"
    source_text = source_path.read_text(encoding="utf-8")
    source_text = source_text.replace(
        "std::string Widget::label() const {\n    return label_ + std::to_string(expander_.value);\n}\n",
        "std::string Widget::label() const {\n    auto compute = [this]() { return label_ + std::to_string(expander_.value); };\n    return compute();\n}\n",
    )
    source_path.write_text(source_text, encoding="utf-8")


def apply_macro_member_variant(project_root: Path) -> None:
    source_path = project_root / "src" / "pimpl_widget.cpp"
    source_text = source_path.read_text(encoding="utf-8")
    source_text = source_text.replace(
        "std::string Widget::label() const {\n    return label_ + std::to_string(expander_.value);\n}\n",
        "std::string Widget::label() const {\n#define PICK_VALUE(X) (X)\n    return PICK_VALUE(label_);\n}\n",
    )
    source_path.write_text(source_text, encoding="utf-8")


def apply_this_member_variant(project_root: Path) -> None:
    source_path = project_root / "src" / "pimpl_widget.cpp"
    source_text = source_path.read_text(encoding="utf-8")
    source_text = source_text.replace("values_.push_back(value);", "this->values_.push_back(value);")
    source_text = source_text.replace("counters_[\"total\"] += value;", "this->counters_[\"total\"] += value;")
    source_text = source_text.replace("fast_lookup_[\"total\"] = counters_[\"total\"];", "this->fast_lookup_[\"total\"] = this->counters_[\"total\"];")
    source_path.write_text(source_text, encoding="utf-8")


def apply_inline_private_simple_variant(project_root: Path) -> None:
    header_path = project_root / "include" / "pimpl_widget.hpp"
    source_path = project_root / "src" / "pimpl_widget.cpp"

    header_text = header_path.read_text(encoding="utf-8")
    header_text = header_text.replace(
        "private:\n",
        "private:\n    int bias() const { return counters_.count(\"total\") ? counters_.at(\"total\") : expander_.value; }\n",
    )
    header_path.write_text(header_text, encoding="utf-8")

    source_text = source_path.read_text(encoding="utf-8")
    source_text = source_text.replace(
        "    return sum + heavy::Fib<19>::value + expander_.value;\n",
        "    return sum + heavy::Fib<19>::value + bias();\n",
    )
    source_path.write_text(source_text, encoding="utf-8")


def validate_variant(
    repo_root: Path,
    bha_bin: Path,
    fixture_root: Path,
    temp_root: Path,
    timeout: int,
    compiler: str,
    label: str,
    expect_safe: bool = True,
    mutate_fixture=None,
) -> bool:
    project_root = temp_root / label
    build_dir = project_root / "build"
    trace_dir = project_root / "traces"

    if project_root.exists():
        shutil.rmtree(project_root)
    shutil.copytree(fixture_root, project_root)
    if mutate_fixture is not None:
        mutate_fixture(project_root)

    build_cmd = [
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
    ]
    print(f"[{label}] [1/4] Building fixture and collecting traces...")
    build = run_cmd(build_cmd, cwd=project_root, timeout=timeout, capture=True)
    if build.returncode != 0:
        print("error: bha build failed")
        print((build.stdout or "")[-4000:])
        print((build.stderr or "")[-4000:])
        return False

    suggest_cmd = [
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
        "50",
    ]
    print(f"[{label}] [2/4] Checking CLI suggestion output...")
    suggest = run_cmd(suggest_cmd, cwd=project_root, timeout=timeout, capture=True)
    if suggest.returncode != 0:
        print("error: bha suggest failed")
        print((suggest.stdout or "")[-4000:])
        print((suggest.stderr or "")[-4000:])
        return False

    suggestions = extract_json_array(suggest.stdout)
    pimpl_suggestions = [s for s in suggestions if s.get("type") == "PIMPL Pattern"]
    safe_pimpl = [s for s in pimpl_suggestions if s.get("is_safe") and s.get("edits")]

    print(f"  total suggestions: {len(suggestions)}")
    print(f"  pimpl suggestions: {len(pimpl_suggestions)}")
    print(f"  safe pimpl suggestions: {len(safe_pimpl)}")
    if expect_safe and not safe_pimpl:
        print("error: expected a strict, safe PIMPL suggestion with concrete edits")
        return False
    if not expect_safe and safe_pimpl:
        print("error: expected no strict/safe PIMPL suggestion for this variant")
        return False
    if not expect_safe:
        advisory_pimpl = [
            s for s in pimpl_suggestions
            if s.get("application_mode") == "advisory"
        ]
        if advisory_pimpl:
            print("error: expected non-safe PIMPL suggestions to use external-refactor, found advisory mode")
            print(json.dumps(advisory_pimpl[0], indent=2))
            return False

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
        return False

    print(f"[{label}] [3/4] Applying the safe PIMPL suggestion through the LSP...")
    client = LSPClient(server_path=server_path, cwd=project_root, stderr_path=temp_root / f"{label}.lsp.stderr.log")
    client.start()
    try:
        if not test_initialize(client, str(project_root)):
            return False
        client.send_notification("initialized")

        analysis = test_analyze(client, str(project_root), build_dir=str(build_dir), timeout_seconds=timeout)
        if not analysis:
            return False

        pimpl_ids = [
            suggestion["id"]
            for suggestion in analysis.get("suggestions", [])
            if "PIMPL" in suggestion.get("title", "") and suggestion.get("autoApplicable")
        ]
        if not expect_safe:
            if pimpl_ids:
                print("error: LSP unexpectedly marked PIMPL as auto-applicable")
                return False
            advisory = [
                suggestion for suggestion in analysis.get("suggestions", [])
                if "PIMPL" in suggestion.get("title", "") and suggestion.get("applicationMode") == "advisory"
            ]
            if advisory:
                print("error: expected non-safe PIMPL suggestions to avoid advisory mode in LSP")
                print(json.dumps(advisory[0], indent=2))
                return False
            print(f"ok: expected non-safe external-refactor PIMPL classification for '{label}'")
            return True
        if not pimpl_ids:
            print("error: LSP analysis did not surface a safe PIMPL suggestion")
            return False

        response = execute_command_with_timeout(
            client,
            "bha.applySuggestion",
            [{
                "suggestionId": pimpl_ids[0],
                "skipConsent": True,
                "skipRebuild": False,
            }],
            timeout_seconds=timeout,
            timeout_label="Apply suggestion",
        )
        if not response or "result" not in response or not response["result"].get("success"):
            print("error: LSP failed to apply the strict PIMPL suggestion")
            print(json.dumps(response, indent=2))
            return False
    finally:
        try:
            client.shutdown()
        finally:
            client.stop()

    print(f"[{label}] [4/4] Rebuilding the rewritten temp project...")
    rebuild = run_cmd(["cmake", "--build", str(build_dir), "-j4"], cwd=project_root, timeout=timeout, capture=True)
    if rebuild.returncode != 0:
        print("error: rebuild failed after applying the PIMPL refactor")
        print((rebuild.stdout or "")[-4000:])
        print((rebuild.stderr or "")[-4000:])
        return False

    header_text = (project_root / "include" / "pimpl_widget.hpp").read_text(encoding="utf-8")
    source_text = (project_root / "src" / "pimpl_widget.cpp").read_text(encoding="utf-8")

    required_markers = [
        "struct Impl;",
        "std::unique_ptr<Impl> pimpl_;",
        "struct Widget::Impl {",
        "pimpl_->values_.push_back(value);",
    ]
    if label in {"copyable", "copy-defaulted"}:
        required_markers.append("Widget::Widget(const Widget& other)")
        required_markers.append("Widget& Widget::operator=(const Widget& other)")
    if label == "inline-private-simple":
        required_markers.append("int Widget::bias() const")
        required_markers.append("pimpl_->counters_")
    if label == "copyable-noexcept":
        required_markers.append("Widget::Widget(const Widget& other) noexcept")
        required_markers.append("Widget& Widget::operator=(const Widget& other) noexcept")
    if label == "empty-body-noexcept":
        required_markers.append("Widget::Widget() noexcept : pimpl_(std::make_unique<Impl>()) {}")
    else:
        required_markers.append("Widget::Widget() : pimpl_(std::make_unique<Impl>()) {}")
    missing = [
        marker for marker in required_markers
        if marker not in header_text and marker not in source_text
    ]
    if missing:
        print("error: rewritten files are missing expected PIMPL markers")
        for marker in missing:
            print(f"  - {marker}")
        return False

    print(f"ok: strict PIMPL refactor validated for '{label}' in {project_root}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the strict PIMPL auto-refactor on the test subproject")
    parser.add_argument("--timeout", type=int, default=1800, help="timeout in seconds per external command")
    parser.add_argument("--compiler", default="clang", choices=["clang", "gcc"], help="compiler for bha build")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    bha_bin = repo_root / "build" / "bha"
    fixture_root = repo_root / "tests" / "subprojects" / "suggester_pimpl"
    temp_root = Path("/tmp") / "bha-pimpl-apply"

    if not bha_bin.exists():
        print(f"error: bha binary not found: {bha_bin}")
        return 2
    if not fixture_root.exists():
        print(f"error: fixture missing: {fixture_root}")
        return 2

    if temp_root.exists():
        shutil.rmtree(temp_root)

    scenarios = [
        ("defaulted", None, True),
        ("copyable", apply_copyable_variant, True),
        ("copyable-noexcept", apply_copyable_noexcept_variant, True),
        ("this-member", apply_this_member_variant, True),
        ("inline-private-simple", apply_inline_private_simple_variant, True),
        ("copy-defaulted", apply_copy_defaulted_variant, True),
        ("shadowed-local", apply_shadowed_local_variant, False),
        ("lambda-body", apply_lambda_variant, False),
        ("macro-member", apply_macro_member_variant, True),
        ("empty-body-noexcept", apply_empty_body_variant, True),
    ]
    for label, mutate_fixture, expect_safe in scenarios:
        if not validate_variant(
            repo_root=repo_root,
            bha_bin=bha_bin,
            fixture_root=fixture_root,
            temp_root=temp_root,
            timeout=args.timeout,
            compiler=args.compiler,
            label=label,
            expect_safe=expect_safe,
            mutate_fixture=mutate_fixture,
        ):
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
