#!/usr/bin/env python3

"""Verify that the LSP server preserves byte-exact stdio framing."""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def frame(message: dict) -> bytes:
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def responses(wire: bytes) -> list[dict]:
    messages = []
    offset = 0
    while offset < len(wire):
        header_end = wire.find(b"\r\n\r\n", offset)
        if header_end < 0:
            raise ValueError(f"Malformed LSP header at byte {offset}: {wire[offset:offset + 80]!r}")
        header = wire[offset:header_end]
        if not header.startswith(b"Content-Length: ") or b"\r\r\n" in header:
            raise ValueError(f"Malformed LSP header: {header!r}")
        length = int(header.removeprefix(b"Content-Length: "))
        body_start = header_end + 4
        body_end = body_start + length
        if body_end > len(wire):
            raise ValueError("LSP body is shorter than Content-Length")
        messages.append(json.loads(wire[body_start:body_end]))
        offset = body_end
    return messages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lsp-server", type=Path, required=True)
    args = parser.parse_args()

    wire = b"".join([
        frame({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}}),
        frame({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
        frame({"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": {}}),
        frame({"jsonrpc": "2.0", "method": "exit", "params": {}}),
    ])
    result = subprocess.run([str(args.lsp_server)], input=wire, capture_output=True, timeout=20, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"LSP server exited with {result.returncode}: {result.stderr.decode(errors='replace')}")
    parsed = responses(result.stdout)
    if not any(message.get("id") == 1 and "capabilities" in message.get("result", {}) for message in parsed):
        raise ValueError("Missing initialize response")
    if not any(message.get("id") == 2 and "result" in message for message in parsed):
        raise ValueError("Missing shutdown response")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
