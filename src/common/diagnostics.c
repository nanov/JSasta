#include "diagnostics.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_GRAY    "\033[90m"
#define COLOR_BLUE    "\033[94m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_RED     "\033[91m"
#define COLOR_BOLD    "\033[1m"

// Check if a FILE* stream supports colors
static bool stream_supports_colors(FILE* stream) {
    if (!stream) return false;

    // Check if stream is a terminal
    int fd = fileno(stream);
    if (!isatty(fd)) {
        return false;
    }

    // Check TERM environment variable
    const char* term = getenv("TERM");
    if (!term) {
        return false;
    }

    // Check for common terminal types that support colors
    if (strstr(term, "color") != NULL ||
        strstr(term, "xterm") != NULL ||
        strstr(term, "screen") != NULL ||
        strstr(term, "tmux") != NULL ||
        strstr(term, "rxvt") != NULL ||
        strstr(term, "linux") != NULL ||
        strcmp(term, "cygwin") == 0) {
        return true;
    }

    // Check for NO_COLOR environment variable (standard)
    if (getenv("NO_COLOR") != NULL) {
        return false;
    }

    // Check for COLORTERM environment variable
    if (getenv("COLORTERM") != NULL) {
        return true;
    }

    return false;
}

DiagnosticContext* diagnostic_context_create(void) {
    return diagnostic_context_create_with_mode(DIAG_MODE_COLLECT, stderr);
}

DiagnosticContext* diagnostic_context_create_with_mode(DiagnosticMode mode, FILE* stream) {
    DiagnosticContext* ctx = (DiagnosticContext*)calloc(1, sizeof(DiagnosticContext));
    ctx->head = NULL;
    ctx->tail = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    ctx->info_count = 0;
    ctx->has_errors = false;
    ctx->mode = mode;
    ctx->output_stream = stream ? stream : stderr;
    ctx->use_colors = stream_supports_colors(ctx->output_stream);
    return ctx;
}

void diagnostic_set_mode(DiagnosticContext* ctx, DiagnosticMode mode) {
    if (ctx) {
        ctx->mode = mode;
    }
}

void diagnostic_set_stream(DiagnosticContext* ctx, FILE* stream) {
    if (ctx) {
        ctx->output_stream = stream ? stream : stderr;
        ctx->use_colors = stream_supports_colors(ctx->output_stream);
    }
}

static const char* severity_to_string(DiagnosticSeverity severity) {
    switch (severity) {
        case DIAG_ERROR: return "error";
        case DIAG_WARNING: return "warning";
        case DIAG_INFO: return "info";
        case DIAG_HINT: return "hint";
        default: return "unknown";
    }
}

// Get color for severity level
static const char* get_severity_color(DiagnosticSeverity severity) {
    switch (severity) {
        case DIAG_ERROR:   return COLOR_RED;
        case DIAG_WARNING: return COLOR_YELLOW;
        case DIAG_INFO:    return COLOR_BLUE;
        case DIAG_HINT:    return COLOR_GRAY;
        default:           return COLOR_RESET;
    }
}

// Map diagnostic severity to logger level
static LogLevel severity_to_log_level(DiagnosticSeverity severity) {
    switch (severity) {
        case DIAG_ERROR:   return LOG_ERROR;
        case DIAG_WARNING: return LOG_WARNING;
        case DIAG_INFO:    return LOG_INFO;
        case DIAG_HINT:    return LOG_VERBOSE;
        default:           return LOG_INFO;
    }
}

// Check if a diagnostic should be displayed based on logger settings
static bool should_display_diagnostic(DiagnosticSeverity severity) {
    LogLevel level = severity_to_log_level(severity);
    LogLevel min_level = logger_get_level();

    // Check minimum log level
    if (level < min_level) {
        return false;
    }

    // Hints use verbose level - check if verbose is enabled
    if (severity == DIAG_HINT) {
        // Verbose messages need explicit enabling
        // We approximate this by checking if min_level is LOG_VERBOSE
        return (min_level == LOG_VERBOSE);
    }

    return true;
}

// Emit a single diagnostic to a stream (for DIRECT mode)
static void diagnostic_emit_direct(DiagnosticContext* ctx, DiagnosticSeverity severity,
                                   SourceLocation loc, const char* code,
                                   const char* message) {
    if (!ctx || !ctx->output_stream) return;

    // Check if this diagnostic should be displayed based on logger settings
    if (!should_display_diagnostic(severity)) {
        return;
    }

    FILE* stream = ctx->output_stream;
    const char* severity_str = severity_to_string(severity);
    const char* color = ctx->use_colors ? get_severity_color(severity) : "";
    const char* reset = ctx->use_colors ? COLOR_RESET : "";
    const char* bold = ctx->use_colors ? COLOR_BOLD : "";

    // Format: [SEVERITY] filename:line:col: message
    // or:     [SEVERITY:CODE] filename:line:col: message
    // With colors: color[SEVERITY]reset bold[filename:line:col]reset: message

    if (code) {
        fprintf(stream, "%s[%s:%s]%s %s%s:%zu:%zu:%s %s\n",
               color, severity_str, code, reset,
               bold, loc.filename, loc.line, loc.column, reset,
               message);
    } else {
        fprintf(stream, "%s[%s]%s %s%s:%zu:%zu:%s %s\n",
               color, severity_str, reset,
               bold, loc.filename, loc.line, loc.column, reset,
               message);
    }
    fflush(stream);
}

void diagnostic_context_free(DiagnosticContext* ctx) {
    if (!ctx) return;

    Diagnostic* current = ctx->head;
    while (current) {
        Diagnostic* next = current->next;
        free(current->message);
        free(current->code);
        free(current);
        current = next;
    }
    free(ctx);
}

void diagnostic_add(DiagnosticContext* ctx, DiagnosticSeverity severity,
                   SourceLocation loc, const char* code, const char* format, ...) {
    if (!ctx) return;

    // Format the message
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Update counts first (needed for both modes)
    switch (severity) {
        case DIAG_ERROR:
            ctx->error_count++;
            ctx->has_errors = true;
            break;
        case DIAG_WARNING:
            ctx->warning_count++;
            break;
        case DIAG_INFO:
            ctx->info_count++;
            break;
        case DIAG_HINT:
            // Don't count hints
            break;
    }

    // In DIRECT mode, emit immediately and optionally skip collection
    if (ctx->mode == DIAG_MODE_DIRECT) {
        diagnostic_emit_direct(ctx, severity, loc, code, message);
        // Don't collect in direct mode to save memory
        return;
    }

    // COLLECT mode: store diagnostic for later reporting
    Diagnostic* diag = (Diagnostic*)calloc(1, sizeof(Diagnostic));
    diag->severity = severity;
    diag->location = loc;
    diag->message = strdup(message);
    diag->code = code ? strdup(code) : NULL;
    diag->next = NULL;

    // Add to linked list
    if (ctx->tail) {
        ctx->tail->next = diag;
        ctx->tail = diag;
    } else {
        ctx->head = diag;
        ctx->tail = diag;
    }
}

void diagnostic_error(DiagnosticContext* ctx, SourceLocation loc,
                     const char* code, const char* format, ...) {
    if (!ctx) return;

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    diagnostic_add(ctx, DIAG_ERROR, loc, code, "%s", message);
}

void diagnostic_warning(DiagnosticContext* ctx, SourceLocation loc,
                       const char* code, const char* format, ...) {
    if (!ctx) return;

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    diagnostic_add(ctx, DIAG_WARNING, loc, code, "%s", message);
}

void diagnostic_info(DiagnosticContext* ctx, SourceLocation loc,
                    const char* code, const char* format, ...) {
    if (!ctx) return;

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    diagnostic_add(ctx, DIAG_INFO, loc, code, "%s", message);
}

void diagnostic_report_console(DiagnosticContext* ctx) {
    if (!ctx) return;

    Diagnostic* current = ctx->head;
    while (current) {
        SourceLocation loc = current->location;

        // Build the prefix with optional error code
        char prefix[64];
        if (current->code) {
            snprintf(prefix, sizeof(prefix), "[%s:%s]",
                    severity_to_string(current->severity), current->code);
        } else {
            snprintf(prefix, sizeof(prefix), "[%s]",
                    severity_to_string(current->severity));
        }

        // Use logger functions based on severity
        switch (current->severity) {
            case DIAG_ERROR:
                log_error_at(&loc, "%s", current->message);
                break;
            case DIAG_WARNING:
                log_warning_at(&loc, "%s", current->message);
                break;
            case DIAG_INFO:
                log_info_at(&loc, "%s", current->message);
                break;
            case DIAG_HINT:
                // Use verbose for hints
                log_verbose("%s:%zu:%zu: %s %s",
                           loc.filename, loc.line, loc.column,
                           prefix, current->message);
                break;
        }

        current = current->next;
    }
}

void diagnostic_report_json(DiagnosticContext* ctx, const char* output_file) {
    if (!ctx) return;

    FILE* f = fopen(output_file, "w");
    if (!f) {
        fprintf(stderr, "Failed to open diagnostic output file: %s\n", output_file);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"diagnostics\": [\n");

    Diagnostic* current = ctx->head;
    bool first = true;
    while (current) {
        if (!first) {
            fprintf(f, ",\n");
        }
        first = false;

        fprintf(f, "    {\n");
        fprintf(f, "      \"severity\": \"%s\",\n", severity_to_string(current->severity));
        fprintf(f, "      \"location\": {\n");
        fprintf(f, "        \"file\": \"%s\",\n", current->location.filename);
        fprintf(f, "        \"line\": %zu,\n", current->location.line);
        fprintf(f, "        \"column\": %zu\n", current->location.column);
        fprintf(f, "      },\n");
        if (current->code) {
            fprintf(f, "      \"code\": \"%s\",\n", current->code);
        }
        fprintf(f, "      \"message\": \"%s\"\n", current->message);
        fprintf(f, "    }");

        current = current->next;
    }

    fprintf(f, "\n  ],\n");
    fprintf(f, "  \"summary\": {\n");
    fprintf(f, "    \"errors\": %d,\n", ctx->error_count);
    fprintf(f, "    \"warnings\": %d,\n", ctx->warning_count);
    fprintf(f, "    \"info\": %d\n", ctx->info_count);
    fprintf(f, "  }\n");
    fprintf(f, "}\n");

    fclose(f);
}

void diagnostic_clear(DiagnosticContext* ctx) {
    if (!ctx) return;

    Diagnostic* current = ctx->head;
    while (current) {
        Diagnostic* next = current->next;
        free(current->message);
        free(current->code);
        free(current);
        current = next;
    }

    ctx->head = NULL;
    ctx->tail = NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    ctx->info_count = 0;
    ctx->has_errors = false;
}

bool diagnostic_has_errors(DiagnosticContext* ctx) {
    return ctx ? ctx->has_errors : false;
}

int diagnostic_count(DiagnosticContext* ctx, DiagnosticSeverity severity) {
    if (!ctx) return 0;

    switch (severity) {
        case DIAG_ERROR: return ctx->error_count;
        case DIAG_WARNING: return ctx->warning_count;
        case DIAG_INFO: return ctx->info_count;
        case DIAG_HINT: {
            // Count hints by iterating
            int count = 0;
            Diagnostic* current = ctx->head;
            while (current) {
                if (current->severity == DIAG_HINT) count++;
                current = current->next;
            }
            return count;
        }
        default: return 0;
    }
}

void diagnostic_print_summary(DiagnosticContext* ctx) {
    if (!ctx) return;

    if (ctx->error_count == 0 && ctx->warning_count == 0 && ctx->info_count == 0) {
        log_info("No diagnostics");
        return;
    }

    fprintf(stderr, "\n");
    fprintf(stderr, "=== Diagnostic Summary ===\n");
    if (ctx->error_count > 0) {
        fprintf(stderr, "  Errors: %d\n", ctx->error_count);
    }
    if (ctx->warning_count > 0) {
        fprintf(stderr, "  Warnings: %d\n", ctx->warning_count);
    }
    if (ctx->info_count > 0) {
        fprintf(stderr, "  Info: %d\n", ctx->info_count);
    }
    fprintf(stderr, "\n");
}
