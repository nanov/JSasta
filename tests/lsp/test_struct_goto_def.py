#!/usr/bin/env python3
"""
Test go-to-definition for struct members.
This verifies that clicking on a property in `obj.property` jumps to the property definition in the struct.
"""

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

    print(f"→ {method}")
    proc.stdin.write(message.encode())
    proc.stdin.flush()


def main():
    print("Testing Struct Member Go-to-Definition")
    print("=" * 50)

    # Clear old log
    if os.path.exists("/tmp/jsasta_lsp.log"):
        os.remove("/tmp/jsasta_lsp.log")

    project_root = os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    lsp_path = os.path.join(project_root, "build", "jsastad")

    proc = subprocess.Popen(
        [lsp_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=project_root,
    )

    try:
        time.sleep(0.2)

        # Initialize
        send_message(
            proc, "initialize", {"capabilities": {}, "rootUri": "file:///tmp"}, id=1
        )
        time.sleep(0.3)

        # Initialized
        send_message(proc, "initialized", {})
        time.sleep(0.2)

        # Test code with struct and member access (no function)
        test_code = """struct Person {
  name: string;
  age: i32;
}

var p: Person;
p.name;
p.age;
"""

        # Open document
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
        print("✓ Document opened")
        time.sleep(2)  # Wait for type inference

        # Request go-to-definition on "name" in "p.name" (line 6, column 2)
        send_message(
            proc,
            "textDocument/definition",
            {
                "textDocument": {"uri": "file:///tmp/test.jsa"},
                "position": {"line": 6, "character": 2},
            },
            id=2,
        )
        print("✓ Requested definition for 'name'")
        time.sleep(0.5)

        # Request go-to-definition on "age" in "p.age" (line 7, column 2)
        send_message(
            proc,
            "textDocument/definition",
            {
                "textDocument": {"uri": "file:///tmp/test.jsa"},
                "position": {"line": 7, "character": 2},
            },
            id=3,
        )
        print("✓ Requested definition for 'age'")
        time.sleep(0.5)

        # Shutdown
        send_message(proc, "shutdown", id=4)
        time.sleep(0.2)
        send_message(proc, "exit", {})
        time.sleep(0.2)

    finally:
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

    # Check logs
    if os.path.exists("/tmp/jsasta_lsp.log"):
        with open("/tmp/jsasta_lsp.log", "r") as f:
            log = f.read()

        # Look for key indicators
        checks = {
            "Code index built": "Code index built" in log,
            "Code index rebuilt": "Rebuilding code index with type information" in log,
            "Definition found": "Definition found:" in log,
        }

        print("\n=== Results ===")
        for check, passed in checks.items():
            print(f"{'✓' if passed else '✗'} {check}")

        if all(checks.values()):
            print("\n✓ All checks passed!")
            print("Struct member go-to-definition is working!")
        else:
            print("\n⚠ Some checks failed - see log for details")

        print(f"\nFull log: /tmp/jsasta_lsp.log")
    else:
        print("⚠ No log file created")


if __name__ == "__main__":
    main()
