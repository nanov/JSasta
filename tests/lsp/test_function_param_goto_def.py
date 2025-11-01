#!/usr/bin/env python3
import subprocess
import json
import time
import sys

def send_message(proc, method, params=None, id=None):
    msg = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        msg["params"] = params
    if id is not None:
        msg["id"] = id
    
    content = json.dumps(msg)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    proc.stdin.write(header + content)
    proc.stdin.flush()

def main():
    print("Testing Function Parameter Go-to-Definition")
    print("=" * 50)
    
    proc = subprocess.Popen(
        ["./build/jsastad"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=0
    )
    
    try:
        # Initialize
        send_message(proc, "initialize", {"capabilities": {}, "rootUri": "file:///tmp"}, id=1)
        print("→ initialize")
        time.sleep(0.2)
        
        # Initialized
        send_message(proc, "initialized", {})
        print("→ initialized")
        time.sleep(0.2)

        # Test code with function parameters - call the function to trigger specialization
        test_code = """function greet(name: string, age: i32) {
  print(name);
  print(age);
}

greet("Alice", 30);
"""

        # Open document
        send_message(
            proc,
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": "file:///tmp/test_params.jsa",
                    "languageId": "jsasta",
                    "version": 1,
                    "text": test_code,
                }
            },
        )
        print("✓ Document opened")
        time.sleep(2)  # Wait for type inference

        # Request go-to-definition on "name" in "print(name)" (line 1, column 8)
        send_message(
            proc,
            "textDocument/definition",
            {
                "textDocument": {"uri": "file:///tmp/test_params.jsa"},
                "position": {"line": 1, "character": 8},
            },
            id=2,
        )
        print("✓ Requested definition for 'name' usage")
        time.sleep(0.5)

        # Request go-to-definition on "age" in "print(age)" (line 2, column 8)
        send_message(
            proc,
            "textDocument/definition",
            {
                "textDocument": {"uri": "file:///tmp/test_params.jsa"},
                "position": {"line": 2, "character": 8},
            },
            id=3,
        )
        print("✓ Requested definition for 'age' usage")
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
        proc.wait(timeout=2)

if __name__ == "__main__":
    main()
