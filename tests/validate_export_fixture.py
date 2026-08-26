#!/usr/bin/env python3
"""Validate real BHA export output across all supported report formats."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_structural_schema(document: Any, schema: dict[str, Any]) -> None:
    require(isinstance(document, dict), "analysis document must be an object")
    for key in schema.get("required", []):
        require(key in document, f"missing required document field: {key}")

    properties = schema.get("properties", {})
    for key, definition in properties.items():
        if key not in document:
            continue
        expected_type = definition.get("type")
        if expected_type is None:
            continue
        expected_types = expected_type if isinstance(expected_type, list) else [expected_type]
        value = document[key]
        valid = any(
            type_name == "null" and value is None
            or type_name == "object" and isinstance(value, dict)
            or type_name == "array" and isinstance(value, list)
            or type_name == "string" and isinstance(value, str)
            or type_name == "number" and isinstance(value, (int, float)) and not isinstance(value, bool)
            or type_name == "integer" and isinstance(value, int) and not isinstance(value, bool)
            for type_name in expected_types
        )
        require(valid, f"invalid type for document field: {key}")


def validate_json(schema_path: Path, document_path: Path) -> None:
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    document = json.loads(document_path.read_text(encoding="utf-8"))
    validate_structural_schema(document, schema)

    try:
        import jsonschema  # type: ignore
    except ImportError:
        print("jsonschema is unavailable; structural schema validation completed")
        return

    jsonschema.Draft202012Validator(schema).validate(document)
    print("JSON Schema 2020-12 validation passed")


def run_export(binary: Path, trace: Path, output: Path, report_format: str) -> None:
    command = [
        str(binary),
        "export",
        str(trace),
        "--format",
        report_format,
        "-o",
        str(output),
    ]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        raise AssertionError(
            f"{report_format} export failed with {completed.returncode}:\n"
            f"stdout: {completed.stdout}\nstderr: {completed.stderr}"
        )


def validate_csv_bundle(bundle: Path) -> None:
    expected_tables = {"metadata.csv", "summary.csv", "files.csv", "metric_capabilities.csv"}
    actual_tables = {path.name for path in bundle.glob("*.csv")}
    require(expected_tables.issubset(actual_tables), "CSV bundle is missing required tables")
    for table_path in sorted(bundle.glob("*.csv")):
        with table_path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.reader(handle))
        require(rows, f"CSV table is empty: {table_path.name}")
        width = len(rows[0])
        require(width > 0, f"CSV table has no columns: {table_path.name}")
        require(all(len(row) == width for row in rows), f"CSV table is not rectangular: {table_path.name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="bha-export-fixture-") as temporary:
        root = Path(temporary)
        json_path = root / "report.json"
        html_path = root / "report.html"
        markdown_path = root / "report.md"
        csv_bundle = root / "csv"

        run_export(args.binary, args.trace, json_path, "json")
        run_export(args.binary, args.trace, html_path, "html")
        run_export(args.binary, args.trace, markdown_path, "md")
        run_export(args.binary, args.trace, csv_bundle, "csv")

        validate_json(args.schema, json_path)
        document = json.loads(json_path.read_text(encoding="utf-8"))
        require(document["document_type"] == "bha-analysis", "wrong JSON document type")
        require("performance" in document and "dependencies" in document, "JSON lost analysis domains")
        require("build_session" in document and "process_resources" in document, "JSON lost new domains")
        require("Build Context" in html_path.read_text(encoding="utf-8"), "HTML lost Build Context view")
        require("Evidence and Limitations" in markdown_path.read_text(encoding="utf-8"), "Markdown lost evidence section")
        validate_csv_bundle(csv_bundle)
        print("cross-format export fixture passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        print(f"export fixture failed: {error}", file=sys.stderr)
        raise SystemExit(1)
