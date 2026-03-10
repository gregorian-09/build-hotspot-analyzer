#!/usr/bin/env python3

import json
import subprocess
import sys
import threading
from pathlib import Path
from typing import Any, Dict, Optional, List

class LSPClient:
    def __init__(self, server_path: Path, cwd: Optional[Path] = None, stderr_path: Optional[Path] = None):
        self.server_path = server_path
        self.cwd = cwd
        self.stderr_path = stderr_path
        self.process: Optional[subprocess.Popen] = None
        self.request_id = 0
        self.stderr_log: Optional[Any] = None

    def start(self):
        log_path = self.stderr_path or Path("/tmp/bha-lsp-subprojects.err")
        self.stderr_log = open(log_path, "ab")
        self.process = subprocess.Popen(
            [str(self.server_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr_log,
            text=False,
            cwd=str(self.cwd) if self.cwd else None
        )

    def stop(self):
        if self.process:
            try:
                self.send_notification("exit")
            except Exception:
                pass
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except Exception:
                pass
        if self.stderr_log:
            self.stderr_log.close()
            self.stderr_log = None

    def send_message(self, message: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        if not self.process or not self.process.stdin:
            raise RuntimeError("Server not started")

        content = json.dumps(message).encode("utf-8")
        header = f"Content-Length: {len(content)}\r\n\r\n".encode("utf-8")

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
                decoded = line.decode("utf-8")
                if decoded == "\r\n":
                    break
                if ":" in decoded:
                    key, value = decoded.split(":", 1)
                    headers[key.strip()] = value.strip()

            content_length = int(headers.get("Content-Length", 0))
            if content_length == 0:
                continue

            content = self.process.stdout.read(content_length).decode("utf-8")
            message = json.loads(content)
            if expected_id is None or message.get("id") == expected_id:
                return message

    def send_request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        self.request_id += 1
        message = {
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": method,
            "params": params or {},
        }
        return self.send_message(message)

    def send_notification(self, method: str, params: Optional[Dict[str, Any]] = None):
        message = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params or {},
        }
        self.send_message(message)

    def initialize(self, root_uri: str, settings: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
        payload = {"rootUri": root_uri, "capabilities": {}}
        if settings:
            payload["settings"] = settings
        return self.send_request("initialize", payload)

    def shutdown(self):
        return self.send_request("shutdown")

    def execute_command(self, command: str, arguments: Optional[list] = None) -> Optional[Dict[str, Any]]:
        return self.send_request("workspace/executeCommand", {
            "command": command,
            "arguments": arguments or []
        })


def build_project(bha_path: Path, project_root: Path, build_dir: Path) -> bool:
    cmd = [
        str(bha_path),
        "build",
        "--build-system", "cmake",
        "--compiler", "clang",
        "--build-dir", str(build_dir),
        "--clean",
        "--memory",
    ]
    env = dict(**{**{}})
    result = subprocess.run(cmd, cwd=str(project_root), capture_output=True, text=True)
    if result.returncode != 0:
        print("✗ Build failed")
        print(result.stdout)
        print(result.stderr)
        return False
    return True


def ensure_compile_commands(build_dir: Path):
    compile_commands = build_dir / "compile_commands.json"
    if compile_commands.exists():
        return
    compile_commands.write_text("[]", encoding="utf-8")


def test_analyze(client: LSPClient, project_root: str, build_dir: str, timeout_seconds: int = 600):
    print("Testing bha.analyze command...")
    args = {"projectRoot": project_root, "rebuild": False, "buildDir": build_dir}

    response_holder: Dict[str, Any] = {}

    def worker():
        response_holder["response"] = client.execute_command("bha.analyze", [args])

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    thread.join(timeout_seconds)

    if thread.is_alive():
        print(f"✗ Analysis timed out after {timeout_seconds} seconds; skipping project.")
        if client.process:
            try:
                client.process.terminate()
            except Exception:
                pass
        return None

    response = response_holder.get("response")

    if response and "result" in response:
        result = response["result"]
        if "analysisId" in result and "suggestions" in result:
            print("✓ Analysis successful")
            print(f"  Analysis ID: {result['analysisId']}")
            print(f"  Suggestions: {len(result['suggestions'])}")
            print(f"  Files analyzed: {result.get('filesAnalyzed', 0)}")
            return result
        print("✗ Analysis response missing required fields")
        return None

    print("✗ Analysis failed")
    print(f"Response: {response}")
    if client.process:
        rc = client.process.poll()
        if rc is not None:
            print(f"  LSP exited with code {rc}")
    return None


def test_apply_all(client: LSPClient):
    print("Applying all suggestions...")
    response = client.execute_command("bha.applyAllSuggestions", [{"skipConsent": True, "safeOnly": False}])
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

    print("✗ Apply all suggestions failed")
    print(f"Response: {response}")
    return None


def run_for_project(server_path: Path, bha_path: Path, project_root: Path):
    build_dir = project_root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    if not build_project(bha_path, project_root, build_dir):
        return

    ensure_compile_commands(build_dir)

    build_cmd = (
        f"cd \"{project_root}\" && \"{bha_path}\" build --build-system cmake "
        f"--compiler clang --build-dir \"{build_dir}\" --memory"
    )

    settings = {
        "optimization": {
            "autoApplyAll": False,
            "showPreviewBeforeApply": False,
            "rebuildAfterApply": True,
            "rollbackOnBuildFailure": True,
            "buildCommand": build_cmd,
            "buildTimeout": 7200,
            "keepBackups": True,
            "allowMissingCompileCommands": True,
            "includeUnsafeSuggestions": True,
            "minConfidence": 0.4,
            "backupDirectory": str((project_root / ".lsp-optimization-backup").resolve()),
        }
    }

    stderr_path = Path(f"/tmp/bha-lsp-{project_root.name}.err")
    try:
        stderr_path.unlink()
    except FileNotFoundError:
        pass

    print("\n" + "=" * 60)
    print(f"Project: {project_root.name}")
    print(f"Root: {project_root}")
    print(f"Build: {build_dir}")
    print("=" * 60)

    client = LSPClient(server_path, project_root, stderr_path)
    try:
        client.start()
        init = client.initialize(f"file://{project_root}", settings)
        if not (init and "result" in init):
            print("✗ Initialize failed")
            return
        print("✓ Initialize successful")

        client.send_notification("initialized")

        analysis_result = test_analyze(client, str(project_root), str(build_dir))
        if not analysis_result:
            return

        if not analysis_result.get("suggestions"):
            print("No suggestions generated.")
            return
        for suggestion in analysis_result["suggestions"]:
            title = suggestion.get("title", "<missing title>")
            stype = suggestion.get("type", "<missing type>")
            print(f"  Suggestion: {title} (type={stype})")

        apply_result = test_apply_all(client)
        if not apply_result:
            return

        client.shutdown()
    finally:
        client.stop()


def main():
    repo_root = Path(__file__).resolve().parents[2]
    subprojects_root = repo_root / "tests" / "subprojects"
    server_path = repo_root / "build" / "lsp" / "bha-lsp"
    if not server_path.exists():
        server_path = repo_root / "build" / "bha-lsp"
    bha_path = repo_root / "build" / "bha"

    if not server_path.exists() or not bha_path.exists():
        print("Error: build artifacts missing. Run cmake --build build -j first.")
        sys.exit(1)
    if not subprojects_root.exists():
        print(f"Error: subprojects root not found: {subprojects_root}")
        sys.exit(1)

    projects: List[Path] = [p for p in subprojects_root.iterdir() if p.is_dir()]
    if not projects:
        print("No subprojects found.")
        return

    for project_root in sorted(projects):
        run_for_project(server_path, bha_path, project_root)


if __name__ == "__main__":
    main()
