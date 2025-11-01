#ifndef JSASTA_FORMAT_STRING_H
#define JSASTA_FORMAT_STRING_H

#include <stdbool.h>

// Represents a parsed format string
// Example: "Hello {} and {}" becomes:
//   parts = ["Hello ", " and ", ""]
//   part_count = 3
//   placeholder_count = 2
typedef struct {
    char** parts;           // Literal string parts between {}
    int part_count;         // Number of parts (always placeholder_count + 1)
    int placeholder_count;  // Number of {} placeholders
} FormatString;

// Parse a format string containing {} placeholders
// Returns NULL if format string is invalid (e.g., unmatched braces)
FormatString* format_string_parse(const char* format);

// Free a parsed format string
void format_string_free(FormatString* fs);

// Validate format string matches argument count
// Returns true if valid, false if mismatch
bool format_string_validate_args(FormatString* fs, int arg_count);

#endif // JSASTA_FORMAT_STRING_H
