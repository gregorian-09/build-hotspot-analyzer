#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path


def run_case(repo_root: Path, fixture: str, timeout: int) -> tuple[bool, str]:
    cmd = [
        sys.executable,
        str(repo_root / "tests" / "run_template_subproject.py"),
        "--timeout",
        str(timeout),
        "--fixture",
        fixture,
    ]
    proc = subprocess.run(
        cmd,
        cwd=str(repo_root),
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode == 0, output


def main() -> int:
    parser = argparse.ArgumentParser(description="Run explicit-template edge-case fixtures")
    parser.add_argument("--timeout", type=int, default=7200, help="timeout per fixture in seconds")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    fixtures = [
        "suggester_explicit_template",
        "suggester_explicit_template_struct",
        "suggester_explicit_template_existing",
    ]

    failures = []
    for fixture in fixtures:
        print(f"=== fixture: {fixture} ===")
        ok, output = run_case(repo_root, fixture, args.timeout)
        print(output.rstrip())
        if not ok:
            failures.append(fixture)

    if failures:
        print("\nFAILED fixtures:", ", ".join(failures))
        return 1

    print("\nok: all explicit-template edge-case fixtures passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
