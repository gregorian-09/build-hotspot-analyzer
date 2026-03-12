#!/usr/bin/env python3

import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, Optional, List, Tuple


def default_server_path() -> Path:
    root = Path(__file__).resolve().parents[2]
    candidates = [
        (root / "build" / "lsp" / "bha-lsp").resolve(),
        (root / "build" / "bha-lsp").resolve(),
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]

class LSPClient:
    def __init__(
        self,
        server_path: Path,
        cwd: Optional[Path] = None,
        stderr_path: Optional[Path] = None,
        env: Optional[Dict[str, str]] = None,
    ):
        self.server_path = server_path
        self.cwd = cwd
        self.stderr_path = stderr_path
        self.env = env
        self.process: Optional[subprocess.Popen] = None
        self.request_id = 0
        self.stderr_log: Optional[Any] = None
        self.debug_requests = str(os.environ.get("BHA_LSP_TEST_CLIENT_DEBUG", "0")).lower() in {"1", "true", "yes"}

    def start(self):
        log_path = self.stderr_path or Path("/tmp/bha-lsp.err")
        self.stderr_log = open(log_path, "ab")
        self.process = subprocess.Popen(
            [str(self.server_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr_log,
            text=False,
            cwd=str(self.cwd) if self.cwd else None,
            env={**os.environ, **(self.env or {})}
        )

    def stop(self):
        if self.process:
            try:
                self.send_notification("exit")
            except Exception:
                pass
            self.process.terminate()
            self.process.wait(timeout=5)
        if self.stderr_log:
            self.stderr_log.close()
            self.stderr_log = None

    def send_message(self, message: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        if not self.process or not self.process.stdin:
            raise RuntimeError("Server not started")

        if self.debug_requests and message.get("method") == "workspace/executeCommand":
            params = message.get("params", {})
            if isinstance(params, dict) and params.get("command") == "bha.analyze":
                print(f"  LSP request: {json.dumps(message, indent=2)}")

        content = json.dumps(message).encode('utf-8')
        header = f"Content-Length: {len(content)}\r\n\r\n".encode('utf-8')

        self.process.stdin.write(header + content)
        self.process.stdin.flush()

        if message.get("method") == "exit":
            return None

        if "id" not in message:
            return None

        return self.read_response(expected_id=message.get("id"))

    def read_response(self, expected_id: Optional[int] = None) -> Optional[Dict[str, Any]]:
        if not self.process or not self.process.stdout:
            return None

        while True:
            headers: Dict[str, str] = {}
            while True:
                line = self.process.stdout.readline()
                if not line:
                    return None
                decoded = line.decode('utf-8')
                if decoded == '\r\n':
                    break
                if ':' in decoded:
                    key, value = decoded.split(':', 1)
                    headers[key.strip()] = value.strip()

            content_length = int(headers.get('Content-Length', 0))
            if content_length == 0:
                continue

            content = self.process.stdout.read(content_length).decode('utf-8')
            message = json.loads(content)
            if expected_id is None:
                return message
            if message.get("id") == expected_id:
                return message

    def send_request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        self.request_id += 1
        message = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
            "params": params or {}
        }
        return self.send_message(message)

    def send_notification(self, method: str, params: Optional[Dict[str, Any]] = None):
        message = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {}
        }
        self.send_message(message)

    def initialize(self, root_uri: str, settings: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        return self.send_request("initialize", {
            "rootUri": root_uri,
            "capabilities": {},
            **({"settings": settings} if settings else {})
        })

    def shutdown(self):
        return self.send_request("shutdown")

    def execute_command(self, command: str, arguments: Optional[list] = None) -> Optional[Dict[str, Any]]:
        return self.send_request("workspace/executeCommand", {
            "command": command,
            "arguments": arguments or []
        })

def test_initialize(client: LSPClient, project_root: str, settings: Optional[Dict[str, Any]] = None):
    print("Testing initialize...")
    response = client.initialize(f"file://{project_root}", settings)

    if response and "result" in response:
        print("✓ Initialize successful")
        if settings:
            client.send_notification("workspace/didChangeConfiguration", {"settings": settings})
        return True
    else:
        print("✗ Initialize failed")
        print(f"Response: {response}")
        return False

def execute_command_with_timeout(
    client: LSPClient,
    command: str,
    arguments: Optional[list] = None,
    timeout_seconds: int = 300,
    timeout_label: str = "Command",
):
    response_holder: Dict[str, Any] = {}

    def worker():
        response_holder["response"] = client.execute_command(command, arguments)

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    thread.join(timeout_seconds)

    if thread.is_alive():
        print(f"✗ {timeout_label} timed out after {timeout_seconds} seconds")
        if client.process:
            try:
                client.process.terminate()
            except Exception:
                pass
        return None

    return response_holder.get("response")

def test_analyze(client: LSPClient, project_root: str, build_dir: Optional[str] = None, timeout_seconds: int = 300):
    print("\nTesting bha.analyze command...")
    args = {
        "projectRoot": project_root,
        "rebuild": False
    }
    if build_dir:
        build_path = Path(build_dir)
        compile_commands = build_path / "compile_commands.json"
        trace_files = list(build_path.glob("*.json"))
        print(f"  Build dir: {build_path}")
        print(f"  compile_commands.json: {'yes' if compile_commands.exists() else 'no'}")
        print(f"  trace files in build dir: {len([p for p in trace_files if p.name != 'compile_commands.json'])}")
        args["buildDir"] = build_dir

    response = execute_command_with_timeout(
        client,
        "bha.analyze",
        [args],
        timeout_seconds=timeout_seconds,
        timeout_label="Analysis",
    )
    if response is None:
        print(f"✗ Analysis timed out after {timeout_seconds} seconds; skipping project.")
        return None

    if response and "result" in response:
        result = response["result"]
        if "analysisId" in result and "suggestions" in result:
            print(f"✓ Analysis successful")
            print(f"  Analysis ID: {result['analysisId']}")
            print(f"  Suggestions: {len(result['suggestions'])}")
            print(f"  Files analyzed: {result.get('filesAnalyzed', 0)}")
            unreal_checks = result.get("unrealEnvironmentChecks") or []
            if unreal_checks:
                print(f"  Unreal environment checks: {len(unreal_checks)}")
                for check in unreal_checks:
                    check_id = check.get("id", "unknown")
                    status = check.get("status", "unknown")
                    message = check.get("message", "")
                    print(f"    - [{status}] {check_id}: {message}")
            return result
        else:
            print("✗ Analysis response missing required fields")
            return None
    else:
        print("✗ Analysis failed")
        print(f"Response: {response}")
        if client.process:
            rc = client.process.poll()
            if rc is not None:
                print(f"  LSP exited with code {rc}")
        return None

def test_apply_all_suggestions(client: LSPClient, skip_consent: bool = True):
    print("\nApplying all suggestions...")
    args = {
        "skipConsent": skip_consent,
        "safeOnly": False
    }
    response = client.execute_command("bha.applyAllSuggestions", [args])

    if response and "result" in response:
        result = response["result"]
        print("✓ Apply all suggestions completed")
        print(f"  Success: {result.get('success')}")
        print(f"  Applied: {result.get('appliedCount')}")
        print(f"  Skipped: {result.get('skippedCount')}")
        print(f"  Failed: {result.get('failedCount')}")
        if "buildValidation" in result:
            validation = result.get("buildValidation") or {}
            print(
                "  Build validation: "
                f"requested={validation.get('requested')} "
                f"ran={validation.get('ran')} "
                f"success={validation.get('success')}"
            )
        if "rollback" in result:
            rollback = result.get("rollback") or {}
            print(
                "  Rollback: "
                f"attempted={rollback.get('attempted')} "
                f"success={rollback.get('success')} "
                f"reason={rollback.get('reason')}"
            )
        errors = result.get("errors") or []
        if errors:
            print("  Errors:")
            for err in errors:
                print(f"    - {err.get('message', err)}")
        return result
    else:
        print("✗ Apply all suggestions failed")
        print(f"Response: {response}")
        if client.process:
            rc = client.process.poll()
            if rc is not None:
                print(f"  LSP exited with code {rc}")
        return None

def test_revert_changes(client: LSPClient, backup_id: str):
    print("\nReverting changes...")
    response = client.execute_command("bha.revertChanges", [{"backupId": backup_id}])

    if response and "result" in response:
        result = response["result"]
        print(f"✓ Revert completed (success={result.get('success')})")
        return result
    else:
        print("✗ Revert failed")
        print(f"Response: {response}")
        return None

def test_get_suggestion_details(client: LSPClient, suggestion_id: str):
    print(f"\nTesting bha.getSuggestionDetails for {suggestion_id}...")
    response = client.execute_command("bha.getSuggestionDetails", [{"suggestionId": suggestion_id}])

    if response and "result" in response:
        result = response["result"]
        print(f"✓ Got suggestion details")
        print(f"  Title: {result.get('title', 'N/A')}")
        print(f"  Type: {result.get('type', 'N/A')}")
        files_to_create = result.get("filesToCreate") or []
        files_to_modify = result.get("filesToModify") or []
        if files_to_create:
            print(f"  Files to create: {len(files_to_create)}")
            for path in files_to_create:
                print(f"  Create: {path}")
        if files_to_modify:
            print(f"  Files to modify: {len(files_to_modify)}")
            for path in files_to_modify:
                print(f"  Modify: {path}")
            if any(not path for path in files_to_modify):
                print(f"  Modify raw: {files_to_modify}")
        return result
    else:
        print("✗ Failed to get suggestion details")
        print(f"Response: {response}")
        return None

def test_show_metrics(client: LSPClient):
    print("\nTesting bha.showMetrics command...")
    response = client.execute_command("bha.showMetrics", [])

    if response and "result" in response:
        result = response["result"]
        print(f"✓ Got metrics")
        print(f"  Suggestions: {len(result)}")
        return result
    else:
        print("✗ Failed to get metrics")
        return None

def test_shutdown(client: LSPClient):
    print("\nTesting shutdown...")
    response = client.shutdown()

    if response and "result" in response:
        print("✓ Shutdown successful")
        return True
    else:
        print("✗ Shutdown failed")
        return False

PROJECT_CMAKE_SUBDIR = {
    "zstd": "build/cmake",
}

PROJECT_CMAKE_FLAGS = {
    "benchmark": "-DBENCHMARK_ENABLE_TESTING=OFF -DBENCHMARK_ENABLE_GTEST_TESTS=OFF",
    "mimalloc": "-DMI_BUILD_TESTS=OFF",
    "rocksdb": "-DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DWITH_BENCHMARK_TOOLS=OFF -DFAIL_ON_WARNINGS=OFF",
    "zstd": "-DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_BUILD_TESTS=OFF",
    "abseil": "-DABSL_BUILD_TESTING=OFF -DABSL_USE_GOOGLETEST_HEAD=OFF -DABSL_BUILD_TEST_HELPERS=OFF",
    "catch2": "-DCATCH_BUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF",
    "leveldb": "-DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF -DHAVE_SNAPPY=OFF -DCMAKE_CXX_STANDARD=17",
    "yaml-cpp": "-DYAML_BUILD_SHARED_LIBS=OFF -DYAML_CPP_BUILD_TESTS=OFF",
    "libjpeg-turbo": "-DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF",
    "glfw": "-DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF",
    "opencv": "-DBUILD_LIST=core,imgproc,imgcodecs -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF",
}

PROJECT_MAKE_FLAGS = {
    "redis": "BUILD_TLS=no",
}

PROJECT_MAKE_EXTRA_ARGS = {
    "curl": "--disable-dependency-tracking --without-ssl --without-libssh2 --without-libpsl --disable-shared",
    "libpng": "--disable-dependency-tracking --disable-shared",
}

PROJECT_MESON_FLAGS = {
    "weston": "-Dbackend-drm=true -Dbackend-wayland=false -Dbackend-x11=false -Dbackend-headless=true "
              "-Drenderer-gl=false -Dxwayland=false -Dpipewire=false -Dremoting=false -Dshell-lua=false "
              "-Dbackend-rdp=false -Drenderer-vulkan=false -Dshell-desktop=false -Dshell-ivi=false "
              "-Dbackend-pipewire=false -Dshell-kiosk=true -Ddemo-clients=false -Dsimple-clients=[] -Dtests=false",
}

PROJECT_ANALYSIS_TIMEOUTS = {
    "abseil": 1000,
    "opencv": 1800,
    "rocksdb": 1000,
}

EXCLUDED_PROJECTS = {
    "rocksdb",
}

def normalize_extra_args(args: str) -> str:
    return args.replace(" ", ";")

def build_command(
    bha_path: Path,
    build_system: str,
    compiler: str,
    project: str,
    source_dir: Path,
    build_dir: Path,
    trace_dir: Path
) -> str:
    cmd = [
        f"\"{bha_path}\"",
        "build",
        "--build-system", build_system,
        "--compiler", compiler,
        "--build-dir", f"\"{build_dir}\"",
        "--output", f"\"{trace_dir}\"",
        "--memory",
        "--clean"
    ]

    extra_args = ""
    if build_system == "cmake":
        extra_args = PROJECT_CMAKE_FLAGS.get(project, "")
    elif build_system == "make":
        extra_args = PROJECT_MAKE_FLAGS.get(project, "")
        extra_extra = PROJECT_MAKE_EXTRA_ARGS.get(project, "")
        if extra_extra:
            extra_args = f"{extra_args} {extra_extra}".strip()
    elif build_system == "meson":
        extra_args = PROJECT_MESON_FLAGS.get(project, "")

    if extra_args:
        extra_args = normalize_extra_args(extra_args)
        if build_system == "cmake":
            cmd.extend(["--cmake-args", f"\"{extra_args}\""])
        else:
            cmd.extend(["--configure-args", f"\"{extra_args}\""])

    if project in {"curl", "libpng"}:
        cmd.append("--verbose")
        log_path = f"/tmp/bha-build-{project}.log"
        return f"cd \"{source_dir}\" && BHA_BUILD_LOG=\"{log_path}\" " + " ".join(cmd)

    return f"cd \"{source_dir}\" && " + " ".join(cmd)

def ensure_trace_link(build_dir: Path, trace_dir: Path):
    build_dir.mkdir(parents=True, exist_ok=True)
    for trace_file in trace_dir.resolve().glob("*.json"):
        target = build_dir / trace_file.name
        if target.exists():
            continue
        try:
            os.link(trace_file, target)
        except OSError:
            target.write_bytes(trace_file.read_bytes())

def ensure_compile_commands(build_dir: Path):
    compile_commands = build_dir / "compile_commands.json"
    if compile_commands.exists():
        return
    compile_commands.write_text("[]", encoding="utf-8")

def collect_projects(test_root: Path, compiler_filter: str, build_system_filter: str) -> List[Tuple[str, str, str, Path, Path, Path]]:
    projects: List[Tuple[str, str, str, Path, Path, Path]] = []
    for build_system_dir in test_root.iterdir():
        if not build_system_dir.is_dir():
            continue
        build_system = build_system_dir.name
        if build_system_filter != "all" and build_system != build_system_filter:
            continue

        for compiler_dir in build_system_dir.iterdir():
            if not compiler_dir.is_dir():
                continue
            compiler = compiler_dir.name
            if compiler_filter != "all" and compiler != compiler_filter:
                continue

            for project_dir in compiler_dir.iterdir():
                if not project_dir.is_dir():
                    continue
                project = project_dir.name
                if project in EXCLUDED_PROJECTS:
                    continue
                build_dir = project_dir / "build"
                trace_dir = project_dir / "traces"
                if not trace_dir.exists():
                    continue
                if compiler == "clang" and not any(trace_dir.glob("*.json")):
                    continue
                projects.append((project, build_system, compiler, build_dir, trace_dir, project_dir))

    return projects

def run_for_project(
    server_path: Path,
    bha_path: Path,
    project: str,
    build_system: str,
    compiler: str,
    build_dir: Path,
    trace_dir: Path,
    repo_dir: Path,
    keep_changes: bool
):
    source_dir = repo_dir / PROJECT_CMAKE_SUBDIR.get(project, "")
    project_root = source_dir.resolve()

    if not project_root.exists():
        print(f"✗ Project root not found: {project_root}")
        return

    ensure_trace_link(build_dir, trace_dir)
    ensure_compile_commands(build_dir)

    settings = {
        "optimization": {
            "autoApplyAll": False,
            "showPreviewBeforeApply": False,
            "rebuildAfterApply": True,
            "rollbackOnBuildFailure": True,
            "buildCommand": build_command(
                bha_path, build_system, compiler, project, project_root, build_dir, trace_dir
            ),
            "buildTimeout": 7200,
            "keepBackups": True,
            "allowMissingCompileCommands": True,
            "backupDirectory": str((project_root / ".lsp-optimization-backup").resolve()),
        }
    }

    print("\n" + "=" * 60)
    print(f"Project: {project} | System: {build_system} | Compiler: {compiler}")
    print(f"Root: {project_root}")
    print(f"Build: {build_dir}")
    print(f"Traces: {trace_dir}")
    print("=" * 60)

    stderr_path = Path(f"/tmp/bha-lsp-{project}.err")
    try:
        stderr_path.unlink()
    except FileNotFoundError:
        pass

    client = LSPClient(server_path, project_root, stderr_path)
    try:
        client.start()
        if not test_initialize(client, str(project_root), settings):
            return

        client.send_notification("initialized")

        timeout_seconds = PROJECT_ANALYSIS_TIMEOUTS.get(project, 300)
        analysis_result = test_analyze(client, str(project_root), str(build_dir), timeout_seconds)
        if not analysis_result:
            return

        if not analysis_result.get("suggestions"):
            print("No suggestions generated.")
            return
        for suggestion in analysis_result["suggestions"]:
            suggestion_id = suggestion.get("id")
            if not suggestion_id:
                print("✗ Suggestion missing id")
                print(f"  Keys: {list(suggestion.keys())}")
                print(f"  Suggestion: {json.dumps(suggestion, indent=2)}")
                continue
            test_get_suggestion_details(client, suggestion_id)

        apply_result = test_apply_all_suggestions(client, skip_consent=True)
        if not apply_result:
            return

        backup_id = apply_result.get("backupId", "")
        if backup_id and not keep_changes:
            test_revert_changes(client, backup_id)

        test_shutdown(client)
    finally:
        client.stop()

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  lsp_test_client.py <project_root> [build_dir] [server_path]")
        print("  lsp_test_client.py --tests-root <tests/cli> [--compiler clang|gcc|all] [--build-system cmake|make|meson|all] [--server-path PATH] [--bha-path PATH] [--keep-changes]")
        sys.exit(1)

    if sys.argv[1] == "--tests-root":
        test_root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else (Path(__file__).parent / "temp").resolve()
        compiler_filter = "clang"
        build_system_filter = "all"
        project_filter = None
        server_path = None
        bha_path = None
        keep_changes = False

        idx = 3
        while idx < len(sys.argv):
            arg = sys.argv[idx]
            if arg == "--compiler" and idx + 1 < len(sys.argv):
                compiler_filter = sys.argv[idx + 1]
                idx += 2
            elif arg == "--build-system" and idx + 1 < len(sys.argv):
                build_system_filter = sys.argv[idx + 1]
                idx += 2
            elif arg == "--server-path" and idx + 1 < len(sys.argv):
                server_path = Path(sys.argv[idx + 1]).resolve()
                idx += 2
            elif arg == "--bha-path" and idx + 1 < len(sys.argv):
                bha_path = Path(sys.argv[idx + 1]).resolve()
                idx += 2
            elif arg == "--keep-changes":
                keep_changes = True
                idx += 1
            elif arg == "--project" and idx + 1 < len(sys.argv):
                project_filter = sys.argv[idx + 1]
                idx += 2
            else:
                idx += 1

        if not server_path:
            server_path = default_server_path()
        if not bha_path:
            bha_path = (Path(__file__).resolve().parents[2] / "build" / "bha").resolve()

        if not server_path.exists():
            print(f"Error: Server executable not found at {server_path}")
            sys.exit(1)
        if not bha_path.exists():
            print(f"Error: BHA binary not found at {bha_path}")
            sys.exit(1)
        if not test_root.exists():
            print(f"Error: Test root not found at {test_root}")
            sys.exit(1)

        projects = collect_projects(test_root, compiler_filter, build_system_filter)
        if not projects:
            print("No projects found.")
            sys.exit(0)

        for project, build_system, compiler, build_dir, trace_dir, project_dir in projects:
            if project_filter and project != project_filter:
                continue
            repo_dir = test_root / "repos" / project
            if not repo_dir.exists():
                print(f"✗ Repo missing: {repo_dir}")
                continue
            run_for_project(
                server_path,
                bha_path,
                project,
                build_system,
                compiler,
                build_dir,
                trace_dir,
                repo_dir,
                keep_changes
            )

        return

    project_root = Path(sys.argv[1]).resolve()
    build_dir = None
    if len(sys.argv) > 2 and not sys.argv[2].endswith("bha-lsp"):
        build_dir = Path(sys.argv[2]).resolve()

    server_path = None
    if len(sys.argv) > 3:
        server_path = Path(sys.argv[3])
    elif len(sys.argv) > 2 and sys.argv[2].endswith("bha-lsp"):
        server_path = Path(sys.argv[2])

    if not server_path:
        server_path = default_server_path()

    if not server_path.exists():
        print(f"Error: Server executable not found at {server_path}")
        sys.exit(1)

    if not project_root.exists():
        print(f"Error: Project root not found at {project_root}")
        sys.exit(1)

    print(f"Server: {server_path}")
    print(f"Project: {project_root}")
    if build_dir:
        print(f"Build dir: {build_dir}")
    print("=" * 60)

    client = LSPClient(server_path, project_root)
    try:
        client.start()
        print("✓ Server started")

        if not test_initialize(client, str(project_root)):
            sys.exit(1)

        client.send_notification("initialized")
        print("✓ Sent initialized notification")

        analysis_result = test_analyze(client, str(project_root), str(build_dir) if build_dir else None)

        if analysis_result and analysis_result.get("suggestions"):
            test_apply_all_suggestions(client, skip_consent=True)

        test_show_metrics(client)
        test_shutdown(client)

        print("\n" + "=" * 60)
        print("All tests completed successfully!")
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        client.stop()

if __name__ == "__main__":
    main()
