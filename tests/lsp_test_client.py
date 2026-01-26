#!/usr/bin/env python3

import json
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, Optional

class LSPClient:
    def __init__(self, server_path: Path):
        self.server_path = server_path
        self.process: Optional[subprocess.Popen] = None
        self.request_id = 0

    def start(self):
        self.process = subprocess.Popen(
            [str(self.server_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False
        )

    def stop(self):
        if self.process:
            self.send_notification("exit")
            self.process.terminate()
            self.process.wait(timeout=5)

    def send_message(self, message: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        if not self.process or not self.process.stdin:
            raise RuntimeError("Server not started")

        content = json.dumps(message).encode('utf-8')
        header = f"Content-Length: {len(content)}\r\n\r\n".encode('utf-8')

        self.process.stdin.write(header + content)
        self.process.stdin.flush()

        if message.get("method") in ["shutdown", "exit"]:
            return None

        return self.read_response()

    def read_response(self) -> Optional[Dict[str, Any]]:
        if not self.process or not self.process.stdout:
            return None

        headers = {}
        while True:
            line = self.process.stdout.readline().decode('utf-8')
            if line == '\r\n':
                break
            if ':' in line:
                key, value = line.split(':', 1)
                headers[key.strip()] = value.strip()

        content_length = int(headers.get('Content-Length', 0))
        if content_length == 0:
            return None

        content = self.process.stdout.read(content_length).decode('utf-8')
        return json.loads(content)

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

    def initialize(self, root_uri: str) -> Optional[Dict[str, Any]]:
        return self.send_request("initialize", {
            "rootUri": root_uri,
            "capabilities": {}
        })

    def shutdown(self):
        return self.send_request("shutdown")

    def execute_command(self, command: str, arguments: Optional[list] = None) -> Optional[Dict[str, Any]]:
        return self.send_request("workspace/executeCommand", {
            "command": command,
            "arguments": arguments or []
        })

def test_initialize(client: LSPClient, project_root: str):
    print("Testing initialize...")
    response = client.initialize(f"file://{project_root}")

    if response and "result" in response:
        print("✓ Initialize successful")
        return True
    else:
        print("✗ Initialize failed")
        print(f"Response: {response}")
        return False

def test_analyze(client: LSPClient, project_root: str, build_dir: Optional[str] = None):
    print("\nTesting bha.analyze command...")
    args = {
        "projectRoot": project_root,
        "rebuild": False
    }
    if build_dir:
        args["buildDir"] = build_dir

    response = client.execute_command("bha.analyze", [args])

    if response and "result" in response:
        result = response["result"]
        if "analysisId" in result and "suggestions" in result:
            print(f"✓ Analysis successful")
            print(f"  Analysis ID: {result['analysisId']}")
            print(f"  Suggestions: {len(result['suggestions'])}")
            print(f"  Files analyzed: {result.get('filesAnalyzed', 0)}")
            return result
        else:
            print("✗ Analysis response missing required fields")
            return None
    else:
        print("✗ Analysis failed")
        print(f"Response: {response}")
        return None

def test_get_suggestion_details(client: LSPClient, suggestion_id: str):
    print(f"\nTesting bha.getSuggestionDetails for {suggestion_id}...")
    response = client.execute_command("bha.getSuggestionDetails", [suggestion_id])

    if response and "result" in response:
        result = response["result"]
        print(f"✓ Got suggestion details")
        print(f"  Title: {result.get('title', 'N/A')}")
        print(f"  Type: {result.get('type', 'N/A')}")
        return result
    else:
        print("✗ Failed to get suggestion details")
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

def main():
    if len(sys.argv) < 2:
        print("Usage: lsp_test_client.py <project_root> [build_dir] [server_path]")
        sys.exit(1)

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
        server_path = Path(__file__).parent.parent / "cmake-build-debug" / "bha-lsp"

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

    client = LSPClient(server_path)

    try:
        client.start()
        print("✓ Server started")

        if not test_initialize(client, str(project_root)):
            sys.exit(1)

        client.send_notification("initialized")
        print("✓ Sent initialized notification")

        analysis_result = test_analyze(client, str(project_root), str(build_dir) if build_dir else None)

        if analysis_result and analysis_result.get("suggestions"):
            first_suggestion_id = analysis_result["suggestions"][0]["id"]
            test_get_suggestion_details(client, first_suggestion_id)

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
