#!/usr/bin/env python3
import subprocess
import json
import time
import os


def send_message(proc, method, params=None, id=None):
    msg = {"jsonrpc": "2.0", "method": method}
    if id is not None:
        msg["id"] = id
    if params is not None:
        msg["params"] = params
    content = json.dumps(msg)
    message = f"Content-Length: {len(content)}\r\n\r\n{content}"
    proc.stdin.write(message.encode())
    proc.stdin.flush()


os.remove("/tmp/jsasta_lsp.log") if os.path.exists("/tmp/jsasta_lsp.log") else None

proc = subprocess.Popen(
    ["build/jsastad"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)

try:
    time.sleep(0.2)
    send_message(
        proc, "initialize", {"capabilities": {}, "rootUri": "file:///tmp"}, id=1
    )
    time.sleep(0.3)
    send_message(proc, "initialized", {})
    time.sleep(0.2)

    # Just a struct
    test_code = "struct Person { name: string; }\n"

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
    print("Sent simple code")
    time.sleep(2)

    send_message(proc, "shutdown", id=4)
    send_message(proc, "exit", {})

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

print("Checking log...")
if os.path.exists("/tmp/jsasta_lsp.log"):
    with open("/tmp/jsasta_lsp.log") as f:
        log = f.read()
    if "Code index built" in log:
        print("✓ Simple code worked")
    else:
        print("✗ Simple code failed")
        print(log[-500:])
