#!/usr/bin/env python3

"""Behavioral checks for the isolated repository apply benchmark."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("run_repo_apply_benchmark.py")
sys.path.insert(0, str(SCRIPT.parent))
from run_repo_apply_benchmark import ExperimentError, select_suggestion  # noqa: E402


class SuggestionSelectionTest(unittest.TestCase):
    def test_numeric_ids_break_equal_rank_ties(self):
        suggestions = [
            {"id": identifier, "priority": "high", "confidence": 0.9,
             "autoApplicable": True, "applicationMode": "direct-edits"}
            for identifier in ("ana-10", "ana-2")
        ]
        self.assertEqual(select_suggestion({"suggestions": suggestions}, None)["id"], "ana-2")

    def test_requested_advisory_cannot_be_benchmarked_as_applied(self):
        with self.assertRaises(ExperimentError):
            select_suggestion({"suggestions": [{"id": "ana-1", "applicationMode": "advisory"}]}, "ana-1")


FAKE_BHA = r'''#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

args = sys.argv[1:]
action = args[1]
mode = os.environ.get("BHA_FAKE_MODE", "success")

def option(name):
    return args[args.index(name) + 1]

if action == "record":
    root = Path(option("--project-root"))
    build = Path(option("--build-dir"))
    traces = Path(option("--trace-dir"))
    if "--clean" in args and build.exists():
        shutil.rmtree(build)
    subprocess.run([
        "cmake", "-S", str(root), "-B", str(build),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_C_COMPILER=" + option("--c-compiler"),
        "-DCMAKE_CXX_COMPILER=" + option("--cxx-compiler"),
    ], check=True, capture_output=True, text=True)
    subprocess.run(["cmake", "--build", str(build)], check=True, capture_output=True, text=True)
    traces.mkdir(parents=True, exist_ok=True)
    trace = traces / "fixture.json"
    trace.write_text("{}\n", encoding="utf-8")
    print(json.dumps({"buildTimeMs": 80 if "post" in traces.parts else 100,
                      "traceFiles": [str(trace)]}))
elif action == "analyze":
    suggestions = [] if mode == "none" else [{
        "id": "ana-1", "type": "include", "title": "Fixture edit", "confidence": 0.9,
        "autoApplicable": True, "applicationMode": "direct-edits",
    }]
    print(json.dumps({"analysisId": "fixture-analysis", "suggestions": suggestions}))
elif action == "apply":
    assert option("--suggestion-id") == "ana-1"
    assert "--all" not in args
    if mode == "fail":
        print(json.dumps({"success": False, "changedFiles": [],
                          "buildValidation": {"ran": True, "success": False}}))
        sys.exit(1)
    source = Path(option("--project-root")) / "main.cpp"
    source.write_text(source.read_text(encoding="utf-8") + "// applied\n", encoding="utf-8")
    print(json.dumps({"success": True, "changedFiles": [str(source)],
                      "backupId": "fixture-backup",
                      "buildValidation": {"ran": True, "success": True}}))
else:
    raise SystemExit("unexpected action " + action)
'''


class RepoApplyBenchmarkTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bha-benchmark-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.repo = self.root / "repos" / "fixture"
        self.repo.mkdir(parents=True)
        (self.repo / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\n"
            "project(fixture LANGUAGES CXX)\n"
            "add_executable(fixture main.cpp)\n"
            "enable_testing()\n"
            "add_test(NAME fixture COMMAND fixture)\n",
            encoding="utf-8",
        )
        self.original = "int main() { return 0; }\n"
        (self.repo / "main.cpp").write_text(self.original, encoding="utf-8")
        subprocess.run(["git", "init", "--quiet", str(self.repo)], check=True)
        subprocess.run(["git", "-C", str(self.repo), "add", "CMakeLists.txt", "main.cpp"], check=True)
        subprocess.run([
            "git", "-C", str(self.repo), "-c", "user.name=BHA Test",
            "-c", "user.email=bha@example.invalid", "commit", "--quiet", "-m", "fixture",
        ], check=True)
        fake_script = self.root / "fake-bha.py" if os.name == "nt" else self.root / "fake-bha"
        fake_script.write_text(FAKE_BHA, encoding="utf-8")
        if os.name == "nt":
            self.fake_bha = self.root / "fake-bha.cmd"
            self.fake_bha.write_text(
                f'@echo off\n"{sys.executable}" "{fake_script}" %*\n',
                encoding="utf-8",
            )
        else:
            self.fake_bha = fake_script
            self.fake_bha.chmod(0o755)

    def run_experiment(self, mode="success", *extra, bha_path=None):
        output = self.root / "results"
        command = [
            sys.executable, str(SCRIPT), "--project", "fixture",
            "--repos-root", str(self.repo.parent), "--output-root", str(output),
            "--bha-path", str(bha_path or self.fake_bha), "--runs", "1", "--jobs", "1",
            "--timeout-seconds", "120", "--analysis-timeout-seconds", "120", *extra,
        ]
        if os.name == "nt":
            command.extend(("--c-compiler", "cl", "--cxx-compiler", "cl"))
        process = subprocess.run(
            command, capture_output=True, text=True, check=False,
            timeout=180, env={**os.environ, "BHA_FAKE_MODE": mode},
        )
        runs = list(output.iterdir())
        self.assertEqual(len(runs), 1, process.stdout + process.stderr)
        summary = json.loads((runs[0] / "summary.json").read_text(encoding="utf-8"))
        return process, summary["records"][0], runs[0]

    def test_apply_uses_suggestion_id_and_measures_fresh_builds(self):
        process, result, run_dir = self.run_experiment("success", "--require-tests")
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(result["status"], "validated")
        self.assertEqual(result["traceCaptureBuildMs"], {"baseline": 100, "post": 80})
        self.assertEqual(len(result["baselineBuildMs"]), 1)
        self.assertEqual(len(result["postBuildMs"]), 1)
        self.assertGreater(result["baselineBuildMs"][0], 0)
        self.assertGreater(result["postBuildMs"][0], 0)
        self.assertNotEqual(result["baselineTraceDirs"], result["postTraceDirs"])
        self.assertEqual(result["selectedSuggestion"]["id"], "ana-1")
        self.assertEqual(result["projectTests"], "passed")
        self.assertEqual((self.repo / "main.cpp").read_text(encoding="utf-8"), self.original)
        self.assertIn("// applied", (run_dir / "fixture" / "worktree" / "main.cpp").read_text(encoding="utf-8"))
        self.assertNotIn("applyDirectEdits", (run_dir / "fixture" / "logs" / "apply.log").read_text(encoding="utf-8"))
        self.assertIn("--clean-first", (run_dir / "fixture" / "logs" / "baseline-clean-01.log").read_text(encoding="utf-8"))
        self.assertIn("validated", (run_dir / "summary.md").read_text(encoding="utf-8"))

    def test_no_applicable_suggestion_has_no_fabricated_savings(self):
        process, result, _ = self.run_experiment("none")
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(result["status"], "no_applicable_suggestion")
        self.assertIsNone(result["timing"])
        self.assertEqual(result["baselineBuildMs"], [])
        self.assertEqual(result["postBuildMs"], [])

    def test_require_applied_refuses_no_applicable_suggestion(self):
        process, result, _ = self.run_experiment("none", "--require-applied")
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(result["status"], "no_applicable_suggestion")

    def test_failed_apply_does_not_report_savings(self):
        process, result, _ = self.run_experiment("fail")
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(result["status"], "apply_failed")
        self.assertIsNone(result["timing"])
        self.assertEqual(result["postBuildMs"], [])
        self.assertEqual((self.repo / "main.cpp").read_text(encoding="utf-8"), self.original)

    def test_require_tests_refuses_build_only_result(self):
        (self.repo / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\n"
            "project(fixture LANGUAGES CXX)\n"
            "add_executable(fixture main.cpp)\n",
            encoding="utf-8",
        )
        subprocess.run(["git", "-C", str(self.repo), "add", "CMakeLists.txt"], check=True)
        subprocess.run([
            "git", "-C", str(self.repo), "-c", "user.name=BHA Test",
            "-c", "user.email=bha@example.invalid", "commit", "--quiet", "-m", "without tests",
        ], check=True)
        process, result, _ = self.run_experiment("success", "--require-tests")
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(result["status"], "tests_failed")
        self.assertEqual(result["projectTests"], "unavailable")

    @unittest.skipUnless(os.environ.get("BHA_TEST_BINARY"), "Set BHA_TEST_BINARY for a real CLI smoke test")
    def test_real_bha_record_and_analysis(self):
        process, result, run_dir = self.run_experiment(bha_path=Path(os.environ["BHA_TEST_BINARY"]))
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(result["status"], "no_applicable_suggestion")
        self.assertGreater(result["traceCaptureBuildMs"]["baseline"], 0)
        self.assertEqual(result["baselineBuildMs"], [])
        self.assertIsNone(result["timing"])
        self.assertTrue((run_dir / "fixture" / "build" / "compile_commands.json").is_file())


if __name__ == "__main__":
    unittest.main()
