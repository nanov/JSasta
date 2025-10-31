#!/usr/bin/env python3

import subprocess
import json
import time
import os


def send_message(proc, method, params=None, id=None):
    """Send a JSON-RPC message to the LSP server."""
    msg = {"jsonrpc": "2.0", "method": method}
    if id is not None:
        msg["id"] = id
    if params is not None:
        msg["params"] = params

    content = json.dumps(msg)
    message = f"Content-Length: {len(content)}\r\n\r\n{content}"

    print(f"→ Sending: {method}")
    try:
        proc.stdin.write(message.encode())
        proc.stdin.flush()
    except BrokenPipeError:
        # Server already exited, that's OK
        pass


def main():
    print("Testing JSasta LSP Server")
    print("=" * 50)

    # Clear old log
    if os.path.exists("/tmp/jsasta_lsp.log"):
        os.remove("/tmp/jsasta_lsp.log")

    # Start LSP server (relative to project root)
    project_root = os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    lsp_path = os.path.join(project_root, "build", "jsastad")

    if not os.path.exists(lsp_path):
        print(f"Error: LSP server not found at {lsp_path}")
        print("Please build the project first: make")
        return

    proc = subprocess.Popen(
        [lsp_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=project_root,
    )

    try:
        time.sleep(0.2)

        # 1. Initialize
        send_message(
            proc, "initialize", {"capabilities": {}, "rootUri": "file:///tmp"}, id=1
        )
        time.sleep(0.3)

        # 2. Initialized notification
        send_message(proc, "initialized", {})
        time.sleep(0.2)

        # 3. Open document
        test_code = """external printf(string, ...): void;

function greet(name: string) {
  printf("Hello, %s\\n", name);
}

greet("World");
"""

        send_message(
            proc,
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": "file:///tmp/test.jsa",
                    "languageId": "jsasta",
                    "version": 1,
                    "text": test_code,
                }
            },
        )

        print("✓ Document opened, waiting for analysis...")
        time.sleep(2)

        # 4. Change document (incremental)
        send_message(
            proc,
            "textDocument/didChange",
            {
                "textDocument": {"uri": "file:///tmp/test.jsa", "version": 2},
                "contentChanges": [
                    {
                        "range": {
                            "start": {"line": 6, "character": 7},
                            "end": {"line": 6, "character": 12},
                        },
                        "text": "Universe",
                    }
                ],
            },
        )

        print("✓ Document updated (incremental), waiting for analysis...")
        time.sleep(2)

        # 5. Shutdown
        send_message(proc, "shutdown", id=2)
        time.sleep(0.2)

        # 6. Exit
        send_message(proc, "exit", {})
        time.sleep(0.2)

    finally:
        # Close stdin to avoid broken pipe during cleanup
        try:
            proc.stdin.close()
        except:
            pass
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except:
            pass

    print("\n" + "=" * 50)
    print("Test completed!")
    print("\nChecking server logs...")

    if os.path.exists("/tmp/jsasta_lsp.log"):
        with open("/tmp/jsasta_lsp.log", "r") as f:
            log = f.read()

        print("\n=== Key Log Entries ===")
        for line in log.split("\n"):
            if any(
                keyword in line
                for keyword in [
                    "Document opened",
                    "Parsing:",
                    "parse complete",
                    "Code index built",
                    "Type inference",
                    "Incremental update",
                    "diagnostics",
                ]
            ):
                print(line)

        # Check for success indicators
        checks = {
            "✓ Worker thread started": "worker thread starting" in log.lower(),
            "✓ Document parsing": "Parsing:" in log,
            "✓ Code index built": "Code index built" in log,
            "✓ Type inference queued": "queueing type inference" in log.lower()
            or "Type inference work queued" in log,
            "✓ Incremental update": "Incremental update" in log,
        }

        print("\n=== Verification ===")
        for check, passed in checks.items():
            print(check if passed else check.replace("✓", "✗"))

        # Check for actual errors (not "errors: 0" which is success)
        error_lines = [
            line
            for line in log.split("\n")
            if ("error" in line.lower() and "errors: 0" not in line.lower())
            or ("failed" in line.lower() and "parse failed" not in line.lower())
        ]
        if error_lines:
            print("\n⚠ Actual issues found:")
            for line in error_lines[:5]:
                print(f"  {line}")
    else:
        print("⚠ No log file created at /tmp/jsasta_lsp.log")


if __name__ == "__main__":
    main()
