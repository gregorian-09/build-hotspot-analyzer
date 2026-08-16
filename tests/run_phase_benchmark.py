#!/usr/bin/env python3
"""Run a repeatable CTest timing benchmark and emit machine-readable results."""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--filter", default="BuildSystem|ProjectIndex|Pimpl|PIMPL|PImpl")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    if args.repeat < 1:
        parser.error("--repeat must be at least 1")

    command = [
        "ctest",
        "--test-dir",
        str(args.build_dir),
        "--output-on-failure",
        "-R",
        args.filter,
    ]
    samples: list[float] = []
    for _ in range(args.repeat):
        started = time.perf_counter()
        completed = subprocess.run(command, check=False)
        elapsed = time.perf_counter() - started
        if completed.returncode != 0:
            return completed.returncode
        samples.append(round(elapsed * 1000.0, 3))

    result = {
        "command": command,
        "repeat": args.repeat,
        "elapsedMs": samples,
        "minMs": min(samples),
        "medianMs": statistics.median(samples),
        "maxMs": max(samples),
    }
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
