#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// ============================================================================
// PART 1: Static String Utilities (Header-only helpers)
// ============================================================================

// Safe string comparison (handles NULL)
static inline bool str_equals(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

// Safe string comparison with length limit
static inline bool str_equals_n(const char* a, const char* b, size_t n) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strncmp(a, b, n) == 0;
}

// Check if string starts with prefix
static inline bool str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix) return false;
    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

// Check if string ends with suffix
static inline bool str_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Safe strdup (returns NULL if input is NULL)
static inline char* str_dup(const char* str) {
    return str ? strdup(str) : NULL;
}

// Duplicate string with length
static inline char* str_dup_n(const char* str, size_t len) {
    if (!str) return NULL;
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, str, len);
    result[len] = '\0';
    return result;
}

// Concatenate two strings into a new allocation
static inline char* str_concat(const char* a, const char* b) {
    if (!a && !b) return NULL;
    if (!a) return strdup(b);
    if (!b) return strdup(a);

    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char* result = (char*)malloc(len_a + len_b + 1);
    if (!result) return NULL;

    memcpy(result, a, len_a);
    memcpy(result + len_a, b, len_b + 1);
    return result;
}

// Concatenate three strings into a new allocation
static inline char* str_concat3(const char* a, const char* b, const char* c) {
    if (!a && !b && !c) return NULL;

    size_t len_a = a ? strlen(a) : 0;
    size_t len_b = b ? strlen(b) : 0;
    size_t len_c = c ? strlen(c) : 0;

    char* result = (char*)malloc(len_a + len_b + len_c + 1);
    if (!result) return NULL;

    char* ptr = result;
    if (a) { memcpy(ptr, a, len_a); ptr += len_a; }
    if (b) { memcpy(ptr, b, len_b); ptr += len_b; }
    if (c) { memcpy(ptr, c, len_c); ptr += len_c; }
    *ptr = '\0';

    return result;
}

// Format string into new allocation (like asprintf)
static inline char* str_format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static inline char* str_format(const char* fmt, ...) {
    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);

    // Calculate required size
    int size = vsnprintf(NULL, 0, fmt, args1);
    va_end(args1);

    if (size < 0) {
        va_end(args2);
        return NULL;
    }

    char* result = (char*)malloc(size + 1);
    if (!result) {
        va_end(args2);
        return NULL;
    }

    vsnprintf(result, size + 1, fmt, args2);
    va_end(args2);

    return result;
}

// Find character in string (returns index or -1)
static inline int str_index_of(const char* str, char ch) {
    if (!str) return -1;
    const char* pos = strchr(str, ch);
    return pos ? (int)(pos - str) : -1;
}

// Find substring in string (returns index or -1)
static inline int str_index_of_str(const char* haystack, const char* needle) {
    if (!haystack || !needle) return -1;
    const char* pos = strstr(haystack, needle);
    return pos ? (int)(pos - haystack) : -1;
}

// Trim whitespace from start and end (allocates new string)
static inline char* str_trim(const char* str) {
    if (!str) return NULL;

    // Skip leading whitespace
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }

    if (*str == '\0') return strdup("");

    // Find end
    const char* end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }

    size_t len = end - str + 1;
    return str_dup_n(str, len);
}

// ============================================================================
// PART 2: Dynamic Mutable String (JSA String Builder)
// ============================================================================

typedef struct JsaStringBuilder {
    char* data;        // String data (null-terminated)
    size_t length;     // Current string length (excluding null terminator)
    size_t capacity;   // Allocated capacity (including space for null terminator)
} JsaStringBuilder;

// Create a new string builder
JsaStringBuilder* jsa_string_builder_create(void);

// Create a string builder with initial capacity
JsaStringBuilder* jsa_string_builder_create_with_capacity(size_t capacity);

// Create a string builder from existing string
JsaStringBuilder* jsa_string_builder_from_string(const char* str);

// Free a string builder
void jsa_string_builder_free(JsaStringBuilder* sb);

// Clear the string builder (keeps capacity)
void jsa_string_builder_clear(JsaStringBuilder* sb);

// Ensure capacity is at least min_capacity
bool jsa_string_builder_reserve(JsaStringBuilder* sb, size_t min_capacity);

// Append a string
bool jsa_string_builder_append(JsaStringBuilder* sb, const char* str);

// Append a character
bool jsa_string_builder_append_char(JsaStringBuilder* sb, char ch);

// Append a substring
bool jsa_string_builder_append_n(JsaStringBuilder* sb, const char* str, size_t len);

// Append formatted string
bool jsa_string_builder_append_format(JsaStringBuilder* sb, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Insert string at position
bool jsa_string_builder_insert(JsaStringBuilder* sb, size_t pos, const char* str);

// Insert substring at position
bool jsa_string_builder_insert_n(JsaStringBuilder* sb, size_t pos, const char* str, size_t len);

// Delete range [start, start+len)
bool jsa_string_builder_delete(JsaStringBuilder* sb, size_t start, size_t len);

// Replace range [start, start+len) with new string
bool jsa_string_builder_replace(JsaStringBuilder* sb, size_t start, size_t len, const char* str);

// Get C string (null-terminated, valid until next modification)
const char* jsa_string_builder_cstr(const JsaStringBuilder* sb);

// Take ownership of string (caller must free, invalidates builder)
char* jsa_string_builder_take(JsaStringBuilder* sb);

// Get length
static inline size_t jsa_string_builder_length(const JsaStringBuilder* sb) {
    return sb ? sb->length : 0;
}

// Get capacity
static inline size_t jsa_string_builder_capacity(const JsaStringBuilder* sb) {
    return sb ? sb->capacity : 0;
}

// Check if empty
static inline bool jsa_string_builder_is_empty(const JsaStringBuilder* sb) {
    return !sb || sb->length == 0;
}

// ============================================================================
// PART 3: LSP Text Document Utilities (for incremental updates)
// ============================================================================

// LSP position (0-based line and character)
typedef struct {
    size_t line;
    size_t character;
} TextPosition;

// LSP range
typedef struct {
    TextPosition start;
    TextPosition end;
} TextRange;

// Apply LSP text edit to a string builder
// This handles line/column indexing for incremental updates
bool jsa_string_builder_apply_edit(JsaStringBuilder* sb, const TextRange* range, const char* new_text);

// Convert position (line, character) to byte offset in string
// Returns -1 if position is out of bounds
int jsa_string_builder_position_to_offset(const JsaStringBuilder* sb, size_t line, size_t character);

// Convert byte offset to position (line, character)
// Returns false if offset is out of bounds
bool jsa_string_builder_offset_to_position(const JsaStringBuilder* sb, size_t offset, size_t* out_line, size_t* out_character);
