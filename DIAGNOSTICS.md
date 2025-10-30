# JSasta Diagnostics System

## Overview

The diagnostics system provides a flexible way to collect, manage, and report errors, warnings, and informational messages throughout the compilation process. It supports multiple output modes and destinations including console, files, and LSP streams.

## Features

- **Two modes**: COLLECT (batch reporting) or DIRECT (immediate output)
- **Flexible output**: Console, files, or any FILE* stream
- **Multiple severity levels**: Error, Warning, Info, Hint
- **Optional error codes** for categorization (e.g., "E001", "W042")
- **Console output** using existing logger functions
- **JSON export** for Language Server Protocol (LSP) integration
- **Summary statistics** with error/warning counts
- **Memory efficient**: DIRECT mode doesn't store diagnostics

## Diagnostic Modes

### COLLECT Mode (Default)
Collects all diagnostics in memory and reports them later. Useful for:
- Batch compilation showing all errors at once
- JSON export for LSP
- Testing error reporting

### DIRECT Mode
Emits diagnostics immediately as they occur. Useful for:
- Real-time feedback during long compilations
- Streaming to log files
- Memory-constrained environments
- Interactive development tools

## Usage

### 1. Create a Diagnostic Context

**Default (COLLECT mode to stderr):**
```c
DiagnosticContext* diag = diagnostic_context_create();
```

**DIRECT mode for immediate output:**
```c
DiagnosticContext* diag = diagnostic_context_create_with_mode(DIAG_MODE_DIRECT, stderr);
```

**Output to a file:**
```c
FILE* log = fopen("compile.log", "w");
DiagnosticContext* diag = diagnostic_context_create_with_mode(DIAG_MODE_DIRECT, log);
// ... compile ...
diagnostic_context_free(diag);
fclose(log);
```

**Change mode dynamically:**
```c
DiagnosticContext* diag = diagnostic_context_create();
diagnostic_set_mode(diag, DIAG_MODE_DIRECT);  // Switch to direct mode
diagnostic_set_stream(diag, my_stream);       // Change output stream
```

### 2. Add Diagnostics

```c
// Add an error
diagnostic_error(diag, loc, "E001", "Undefined variable: %s", var_name);

// Add a warning
diagnostic_warning(diag, loc, "W001", "Unused variable: %s", var_name);

// Add info
diagnostic_info(diag, loc, NULL, "Did you mean: %s?", suggestion);

// Add any severity with optional code
diagnostic_add(diag, DIAG_ERROR, loc, "E002", 
               "Type mismatch: expected %s, got %s", expected, actual);
```

### 3. Report Diagnostics

**Note:** In DIRECT mode, diagnostics are emitted immediately when added. Reporting functions are only needed for COLLECT mode.

```c
// COLLECT mode: Report all collected diagnostics to console (uses logger)
diagnostic_report_console(diag);

// Export to JSON file (for LSP) - only works in COLLECT mode
diagnostic_report_json(diag, "diagnostics.json");

// Print summary (works in both modes - shows counts)
diagnostic_print_summary(diag);
```

### 4. Check for Errors

```c
if (diagnostic_has_errors(diag)) {
    // Abort compilation
    diagnostic_context_free(diag);
    return EXIT_FAILURE;
}

// Get specific counts
int errors = diagnostic_count(diag, DIAG_ERROR);
int warnings = diagnostic_count(diag, DIAG_WARNING);
```

### 5. Clean Up

```c
diagnostic_context_free(diag);
```

## Integration Strategy

### Phase-Based Collection (like `collect_consts_and_structs`)

Instead of immediately calling `log_error_at()` and aborting, collect diagnostics:

**Before:**
```c
if (!entry) {
    log_error_at(&node->loc, "Undefined variable: %s", name);
    return NULL;  // Abort immediately
}
```

**After:**
```c
if (!entry) {
    diagnostic_error(diag, node->loc, "E001", 
                    "Undefined variable: %s", name);
    // Continue processing to find more errors
}
```

### Example: Type Inference with Diagnostics

```c
void type_inference_with_diagnostics(ASTNode* ast, SymbolTable* symbols, 
                                    TypeContext* type_ctx, 
                                    DiagnosticContext* diag) {
    // Pass 0: Collect consts and structs
    collect_consts_and_structs(ast, symbols, type_ctx, diag);
    
    // Check for errors before continuing
    if (diagnostic_has_errors(diag)) {
        return;
    }
    
    // Pass 1: Collect function signatures
    collect_function_signatures(ast, symbols, type_ctx, diag);
    
    if (diagnostic_has_errors(diag)) {
        return;
    }
    
    // ... more passes
}
```

## Diagnostic Codes (Suggested)

### Errors (E###)
- **E001**: Undefined variable
- **E002**: Type mismatch
- **E003**: Duplicate declaration
- **E004**: Invalid array size
- **E005**: Circular dependency
- **E006**: Missing return statement
- **E007**: Invalid operation
- **E008**: Function signature mismatch

### Warnings (W###)
- **W001**: Unused variable
- **W002**: Unused function
- **W003**: Implicit type conversion
- **W004**: Unreachable code
- **W005**: Deprecated feature

### Info (I###)
- **I001**: Type inference succeeded
- **I002**: Optimization applied
- **I003**: Suggestion/hint

## JSON Output Format

The JSON output is compatible with LSP diagnostics:

```json
{
  "diagnostics": [
    {
      "severity": "error",
      "location": {
        "file": "test.jsa",
        "line": 10,
        "column": 5
      },
      "code": "E001",
      "message": "Undefined variable: foo"
    }
  ],
  "summary": {
    "errors": 1,
    "warnings": 0,
    "info": 0
  }
}
```

## Mode Comparison Example

### COLLECT Mode Output
```
[INFO]    Compiling test.jsa...
(parsing happens, errors collected silently)
[ERROR]   test.jsa:5:13: Unexpected token in expression: ;
[ERROR]   test.jsa:5:13: Expected expression after =
[ERROR]   test.jsa:8:20: Type mismatch: expected i32, got string

=== Diagnostic Summary ===
  Errors: 3
```

### DIRECT Mode Output
```
[INFO]    Compiling test.jsa...
[error:E202] test.jsa:5:13: Unexpected token in expression: ;
[error:E214] test.jsa:5:13: Expected expression after =
[error:E301] test.jsa:8:20: Type mismatch: expected i32, got string

=== Diagnostic Summary ===
  Errors: 3
```

## Benefits

1. **Better UX**: Show all errors at once, not just the first one (COLLECT mode)
2. **Real-time feedback**: See errors as they occur (DIRECT mode)
3. **Flexible output**: Console, files, or custom streams
4. **LSP Integration**: Easy to integrate with VS Code and other editors
5. **Batch Mode**: Continue compilation even with non-fatal errors
6. **Memory efficient**: DIRECT mode doesn't store diagnostics
7. **Testing**: Easier to test error reporting
8. **Consistency**: Centralized diagnostic handling

## Migration Path

1. **Phase 1**: Add diagnostic context to main compilation functions
2. **Phase 2**: Convert `log_error_at()` calls to `diagnostic_error()`
3. **Phase 3**: Add warning and info diagnostics
4. **Phase 4**: Add LSP integration
5. **Phase 5**: Add error codes for categorization
