#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdbool.h>
#include <stdio.h>
#include "logger.h"

// Diagnostic severity levels
typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_INFO,
    DIAG_HINT
} DiagnosticSeverity;

// Diagnostic output mode
typedef enum {
    DIAG_MODE_COLLECT,  // Collect diagnostics for later reporting
    DIAG_MODE_DIRECT    // Report diagnostics immediately as they occur
} DiagnosticMode;

// Individual diagnostic message
typedef struct Diagnostic {
    DiagnosticSeverity severity;
    SourceLocation location;
    char* message;
    char* code;  // Optional error code (e.g., "E001", "W042")
    struct Diagnostic* next;
} Diagnostic;

// Diagnostic context for collecting diagnostics
typedef struct DiagnosticContext {
    Diagnostic* head;
    Diagnostic* tail;
    int error_count;
    int warning_count;
    int info_count;
    bool has_errors;
    
    // Output configuration
    DiagnosticMode mode;
    FILE* output_stream;  // Stream for direct output (stderr by default, can be file/lsp)
    bool use_colors;      // Whether to use ANSI colors in output
} DiagnosticContext;

// Create a new diagnostic context with default settings (COLLECT mode, stderr)
DiagnosticContext* diagnostic_context_create(void);

// Create a diagnostic context with custom mode and stream
DiagnosticContext* diagnostic_context_create_with_mode(DiagnosticMode mode, FILE* stream);

// Set diagnostic mode (COLLECT or DIRECT)
void diagnostic_set_mode(DiagnosticContext* ctx, DiagnosticMode mode);

// Set output stream for direct mode (default: stderr)
void diagnostic_set_stream(DiagnosticContext* ctx, FILE* stream);

// Free diagnostic context and all diagnostics
void diagnostic_context_free(DiagnosticContext* ctx);

// Add a diagnostic to the context
void diagnostic_add(DiagnosticContext* ctx, DiagnosticSeverity severity, 
                   SourceLocation loc, const char* code, const char* format, ...);

// Convenience functions for different severity levels
void diagnostic_error(DiagnosticContext* ctx, SourceLocation loc, 
                     const char* code, const char* format, ...);
void diagnostic_warning(DiagnosticContext* ctx, SourceLocation loc, 
                       const char* code, const char* format, ...);
void diagnostic_info(DiagnosticContext* ctx, SourceLocation loc, 
                    const char* code, const char* format, ...);

// Report all diagnostics to console (uses logger)
void diagnostic_report_console(DiagnosticContext* ctx);

// Report all diagnostics in JSON format (for LSP)
void diagnostic_report_json(DiagnosticContext* ctx, const char* output_file);

// Clear all diagnostics but keep the context
void diagnostic_clear(DiagnosticContext* ctx);

// Check if context has errors
bool diagnostic_has_errors(DiagnosticContext* ctx);

// Get diagnostic count by severity
int diagnostic_count(DiagnosticContext* ctx, DiagnosticSeverity severity);

// Print summary of diagnostics
void diagnostic_print_summary(DiagnosticContext* ctx);

// ============================================================================
// JSASTA ERROR CATALOG INTEGRATION
// ============================================================================

#include "jsasta_errors.h"

// Report error using enum constant from error catalog
#define JSASTA_ERROR(diag, loc, err, ...) \
    diagnostic_error(diag, loc, JSASTA_GET_CODE(err), JSASTA_GET_TEMPLATE(err), ##__VA_ARGS__)

// Report warning using enum constant from error catalog
#define JSASTA_WARNING(diag, loc, warn, ...) \
    diagnostic_warning(diag, loc, JSASTA_GET_CODE(warn), JSASTA_GET_TEMPLATE(warn), ##__VA_ARGS__)

// Parser-specific error (when you have parser context)
#define PARSER_ERROR(parser, err, ...) \
    JSASTA_ERROR((parser)->diagnostics, (parser)->current_token->location, err, ##__VA_ARGS__)

#endif // DIAGNOSTICS_H
