#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stddef.h>

// Log levels
typedef enum {
    LOG_VERBOSE,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

// Source location information (optional)
typedef struct {
    const char* filename;
    size_t line;
    size_t column;
} SourceLocation;

// Initialize logger with log level
void logger_init(LogLevel level);

// Set minimum log level (messages below this level won't be displayed)
void logger_set_level(LogLevel level);

// Get current log level
LogLevel logger_get_level();

// Enable/disable verbose mode
void logger_set_verbose(bool enabled);

// Main logging functions (without source location)
void log_verbose(const char* format, ...);
void log_info(const char* format, ...);
void log_warning(const char* format, ...);
void log_error(const char* format, ...);

// Logging functions with source location
void log_verbose_at(SourceLocation* loc, const char* format, ...);
void log_info_at(SourceLocation* loc, const char* format, ...);
void log_warning_at(SourceLocation* loc, const char* format, ...);
void log_error_at(SourceLocation* loc, const char* format, ...);

// Indented logging (for nested/hierarchical output)
void log_verbose_indent(int indent, const char* format, ...);
void log_info_indent(int indent, const char* format, ...);
void log_warning_indent(int indent, const char* format, ...);
void log_error_indent(int indent, const char* format, ...);

// Indented logging with source location
void log_verbose_indent_at(int indent, SourceLocation* loc, const char* format, ...);
void log_info_indent_at(int indent, SourceLocation* loc, const char* format, ...);
void log_warning_indent_at(int indent, SourceLocation* loc, const char* format, ...);
void log_error_indent_at(int indent, SourceLocation* loc, const char* format, ...);

// Section headers (for major compilation phases)
void log_section(const char* format, ...);

// Error counting
int logger_get_error_count();
void logger_reset_error_count();
bool logger_has_errors();

// Helper macro to create source location
#define SRC_LOC(file, ln, col) &(SourceLocation){.filename = file, .line = ln, .column = col}

#endif // LOGGER_H
