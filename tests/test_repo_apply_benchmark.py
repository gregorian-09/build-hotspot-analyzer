#!/usr/bin/env python3

"""Behavioral checks for the isolated repository apply benchmark."""

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


SCRIPT = Path(__file__).with_name("run_repo_apply_benchmark.py")
WINDOWS_CAPTURE = Path(__file__).resolve().parent.parent / "cmake" / "bha-capture.bat"
sys.path.insert(0, str(SCRIPT.parent))
from run_repo_apply_benchmark import ExperimentError, managed_pdb_server, select_suggestion  # noqa: E402


def cleanup_with_sharing_retry(cleanup, *, windows, timeout_seconds=10):
    deadline = time.monotonic() + timeout_seconds
    while True:
        try:
            cleanup()
            return
        except PermissionError as error:
            if not windows or getattr(error, "winerror", None) != 32 or time.monotonic() >= deadline:
                raise
            time.sleep(0.2)


class SuggestionSelectionTest(unittest.TestCase):
    def test_windows_cleanup_retries_transient_sharing_violation(self):
        attempts = []

        def cleanup():
            attempts.append(1)
            if len(attempts) == 1:
                error = PermissionError("transient sharing violation")
                error.winerror = 32
                raise error

        cleanup_with_sharing_retry(cleanup, windows=True)
        self.assertEqual(len(attempts), 2)

    def test_cleanup_does_not_retry_other_permission_errors(self):
        attempts = []

        def cleanup():
            attempts.append(1)
            error = PermissionError("access denied")
            error.winerror = 5
            raise error

        with self.assertRaises(PermissionError):
            cleanup_with_sharing_retry(cleanup, windows=True)
        self.assertEqual(len(attempts), 1)

    def test_persistent_sharing_violation_still_fails(self):
        error = PermissionError("persistent sharing violation")
        error.winerror = 32

        def cleanup():
            raise error

        with self.assertRaises(PermissionError):
            cleanup_with_sharing_retry(cleanup, windows=True, timeout_seconds=0)

    @unittest.skipUnless(os.name == "nt", "Requires the Visual Studio developer environment")
    def test_msvc_pdb_server_is_scoped_to_the_benchmark(self):
        previous = os.environ.get("_MSPDBSRV_ENDPOINT_")
        with managed_pdb_server("cl") as server:
            self.assertIsNotNone(server)
            self.assertIsNone(server.poll())
            self.assertNotEqual(os.environ["_MSPDBSRV_ENDPOINT_"], previous)
        self.assertIsNotNone(server.poll())
        self.assertEqual(os.environ.get("_MSPDBSRV_ENDPOINT_"), previous)

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


@unittest.skipUnless(os.name == "nt", "Requires Windows batch execution")
class WindowsCaptureLauncherTest(unittest.TestCase):
    def test_parallel_compiles_keep_stderr_and_traces_isolated(self):
        with tempfile.TemporaryDirectory(prefix="bha capture ") as directory:
            root = Path(directory)
            trace_dir = root / "traces"
            temp_dir = root / "temporary"
            trace_dir.mkdir()
            temp_dir.mkdir()
            compiler = root / "fake-compiler.ps1"
            compiler.write_text(
                "Start-Sleep -Milliseconds 100\n"
                "[Console]::Error.WriteLine('Total: 0.010s')\n",
                encoding="utf-8",
            )
            environment = {
                **os.environ,
                "BHA_TRACE_DIR": str(trace_dir),
                "TEMP": str(temp_dir),
                "TMP": str(temp_dir),
            }

            def compile_source(index):
                source = root / str(index) / "unit.cpp"
                source.parent.mkdir()
                source.write_text("int value = 1;\n", encoding="utf-8")
                return subprocess.run(
                    [str(WINDOWS_CAPTURE), "powershell.exe", "-NoProfile",
                     "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                     str(compiler), str(source)],
                    cwd=root, env=environment, capture_output=True, text=True, check=False,
                )

            with ThreadPoolExecutor(max_workers=8) as executor:
                results = list(executor.map(compile_source, range(8)))
            for result in results:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            traces = list(trace_dir.glob("*.bha.txt"))
            self.assertEqual(len(traces), 8)
            for trace in traces:
                self.assertIn("Total: 0.010s", trace.read_text(encoding="utf-8"))
            self.assertEqual(list(temp_dir.iterdir()), [])


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
    def cleanup_fixture(self):
        try:
            cleanup_with_sharing_retry(self.temp.cleanup, windows=os.name == "nt")
        except PermissionError as error:
            handle = os.environ.get("BHA_HANDLE_EXE")
            if os.name == "nt" and handle and error.filename:
                try:
                    diagnostic = subprocess.run(
                        [handle, "-accepteula", "-nobanner", self.root.name],
                        capture_output=True, text=True, check=False, timeout=30,
                    )
                    print("Open handles for locked benchmark fixture:\n"
                          + diagnostic.stdout + diagnostic.stderr, file=sys.stderr)
                except (OSError, subprocess.TimeoutExpired) as diagnostic_error:
                    print(f"Handle diagnostic failed: {diagnostic_error}", file=sys.stderr)
            raise

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bha-benchmark-test-")
        self.addCleanup(self.cleanup_fixture)
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

    def run_experiment(self, mode="success", *extra, bha_path=None, extra_env=None):
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
            timeout=180, env={**os.environ, "BHA_FAKE_MODE": mode, **(extra_env or {})},
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

    def commit_without_tests(self):
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

    def test_require_tests_refuses_build_only_result(self):
        self.commit_without_tests()
        process, result, _ = self.run_experiment("success", "--require-tests")
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(result["status"], "tests_failed")
        self.assertEqual(result["projectTests"], "unavailable")

    def test_require_applied_keeps_build_only_status_distinct(self):
        self.commit_without_tests()
        process, result, _ = self.run_experiment("success", "--require-applied")
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(result["status"], "build_only")
        self.assertEqual(result["projectTests"], "unavailable")

    @unittest.skipIf(os.name == "nt", "The fake npm executable uses a Unix shebang")
    def test_vscode_mode_consumes_host_result_and_preserves_original_clone(self):
        fake_bin = self.root / "bin"
        fake_bin.mkdir()
        fake_npm = fake_bin / "npm"
        fake_npm.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys\n"
            "assert sys.argv[1:] == ['run', 'test:host']\n"
            "assert json.loads(os.environ['BHA_HOST_REAL_BUILD_PROFILE'])['buildSystem'] == 'CMake'\n"
            "source = pathlib.Path(os.environ['BHA_HOST_REAL_PROJECT_ROOT']) / 'main.cpp'\n"
            "source.write_text(source.read_text() + '// applied in host\\n')\n"
            "result = {'apply': {'success': True, 'changedFiles': [str(source)], "
            "'buildValidation': {'ran': True, 'success': True}}, 'suggestionCount': 1}\n"
            "pathlib.Path(os.environ['BHA_HOST_REAL_RESULT_PATH']).write_text(json.dumps(result))\n",
            encoding="utf-8",
        )
        fake_npm.chmod(0o755)
        process, record, _ = self.run_experiment(
            "success", "--apply-mode", "vscode", "--lsp-path", str(self.fake_bha),
            "--require-applied", "--require-tests",
            extra_env={"PATH": str(fake_bin) + os.pathsep + os.environ["PATH"]},
        )
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(record["status"], "validated")
        self.assertEqual(record["vscodeHost"]["suggestionCount"], 1)
        self.assertEqual((self.repo / "main.cpp").read_text(encoding="utf-8"), self.original)

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
