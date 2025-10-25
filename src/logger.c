#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_GRAY    "\033[90m"
#define COLOR_BLUE    "\033[94m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_RED     "\033[91m"
#define COLOR_BOLD    "\033[1m"

// Global logger state
static struct {
    LogLevel min_level;
    bool verbose_enabled;
    bool use_colors;
} logger_state = {
    .min_level = LOG_INFO,
    .verbose_enabled = false,
    .use_colors = false  // Will be set by logger_init()
};

// Check if terminal supports colors
static bool terminal_supports_colors() {
    // Check if stderr is a terminal
    if (!isatty(fileno(stderr))) {
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

// Initialize logger
void logger_init(LogLevel level) {
    logger_state.min_level = level;
    logger_state.verbose_enabled = (logger_state.min_level == LOG_VERBOSE);
    logger_state.use_colors = terminal_supports_colors();
}

// Set minimum log level
void logger_set_level(LogLevel level) {
    logger_state.min_level = level;
}

// Get current log level
LogLevel logger_get_level() {
    return logger_state.min_level;
}

// Enable/disable verbose mode
void logger_set_verbose(bool enabled) {
    logger_state.verbose_enabled = enabled;
    if (enabled && logger_state.min_level > LOG_VERBOSE) {
        logger_state.min_level = LOG_VERBOSE;
    }
}

// Get color for log level
static const char* get_level_color(LogLevel level) {
    if (!logger_state.use_colors) return "";

    switch (level) {
        case LOG_VERBOSE: return COLOR_GRAY;
        case LOG_INFO:    return COLOR_BLUE;
        case LOG_WARNING: return COLOR_YELLOW;
        case LOG_ERROR:   return COLOR_RED;
        default:          return COLOR_RESET;
    }
}

// Get prefix for log level
static const char* get_level_prefix(LogLevel level) {
    switch (level) {
        case LOG_VERBOSE: return "[VERBOSE]";
        case LOG_INFO:    return "[INFO]   ";
        case LOG_WARNING: return "[WARNING]";
        case LOG_ERROR:   return "[ERROR]  ";
        default:          return "[UNKNOWN]";
    }
}

// Core logging function
static void log_impl(LogLevel level, int indent, SourceLocation* loc, const char* format, va_list args) {
    // Check if this log level should be displayed
    if (level < logger_state.min_level) {
        return;
    }

    if (level == LOG_VERBOSE && !logger_state.verbose_enabled) {
        return;
    }

    const char* color = get_level_color(level);
    const char* prefix = get_level_prefix(level);
    const char* reset = logger_state.use_colors ? COLOR_RESET : "";
    const char* bold = logger_state.use_colors ? COLOR_BOLD : "";

    // Print colored prefix
    fprintf(stderr, "%s%s%s ", color, prefix, reset);

    // Print source location if provided
    if (loc && loc->filename) {
        fprintf(stderr, "%s%s:%zu:%zu:%s ",
                bold, loc->filename, loc->line, loc->column, reset);
    }

    // Print indentation
    for (int i = 0; i < indent; i++) {
        fprintf(stderr, "  ");
    }

    // Print the actual message
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

// Main logging functions (without source location)
void log_verbose(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_VERBOSE, 0, NULL, format, args);
    va_end(args);
}

void log_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_INFO, 0, NULL, format, args);
    va_end(args);
}

void log_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_WARNING, 0, NULL, format, args);
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_ERROR, 0, NULL, format, args);
    va_end(args);
}

// Logging functions with source location
void log_verbose_at(SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_VERBOSE, 0, loc, format, args);
    va_end(args);
}

void log_info_at(SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_INFO, 0, loc, format, args);
    va_end(args);
}

void log_warning_at(SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_WARNING, 0, loc, format, args);
    va_end(args);
}

void log_error_at(SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_ERROR, 0, loc, format, args);
    va_end(args);
}

// Indented logging (for nested/hierarchical output)
void log_verbose_indent(int indent, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_VERBOSE, indent, NULL, format, args);
    va_end(args);
}

void log_info_indent(int indent, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_INFO, indent, NULL, format, args);
    va_end(args);
}

void log_warning_indent(int indent, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_WARNING, indent, NULL, format, args);
    va_end(args);
}

void log_error_indent(int indent, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_ERROR, indent, NULL, format, args);
    va_end(args);
}

// Indented logging with source location
void log_verbose_indent_at(int indent, SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_VERBOSE, indent, loc, format, args);
    va_end(args);
}

void log_info_indent_at(int indent, SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_INFO, indent, loc, format, args);
    va_end(args);
}

void log_warning_indent_at(int indent, SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_WARNING, indent, loc, format, args);
    va_end(args);
}

void log_error_indent_at(int indent, SourceLocation* loc, const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_impl(LOG_ERROR, indent, loc, format, args);
    va_end(args);
}

// Section headers (for major compilation phases)
void log_section(const char* format, ...) {
    if (logger_state.min_level > LOG_VERBOSE) {
        return;
    }

    const char* bold = logger_state.use_colors ? COLOR_BOLD : "";
    const char* reset = logger_state.use_colors ? COLOR_RESET : "";

    fprintf(stderr, "\n%s=== ", bold);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, " ===%s\n", reset);
}
