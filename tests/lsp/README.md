# LSP Integration Tests

This directory contains integration tests for the JSasta Language Server Protocol (LSP) implementation.

## Running Tests

### Integration Test

```bash
# From project root
python3 tests/lsp/test_lsp_integration.py
```

This test verifies:
- ✓ Server initialization
- ✓ Document opening
- ✓ Incremental text changes (via JsaStringBuilder)
- ✓ Code index building
- ✓ Type inference worker thread
- ✓ Diagnostics publishing
- ✓ Graceful shutdown

## Test Output

The test will show:
- Messages sent to the server
- Key log entries from the server
- Verification checklist of features

Server logs are written to `/tmp/jsasta_lsp.log` for debugging.

## Requirements

- Python 3.6+
- Built LSP server at `build/jsastad`

Build the server first:
```bash
make
```
