#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Formatter struct - simple wrapper around FILE* for now
// Later we can add precision, width, fill, align fields
typedef struct {
    FILE* stream;
    // Future: precision, width, fill, align, etc.
} Formatter;

// Helper to get stdout as FILE*
FILE* get_stdout() {
    return stdout;
}

// Helper to get stderr as FILE*
FILE* get_stderr() {
    return stderr;
}

// Helper to get stdin as FILE*
FILE* get_stdin() {
    return stdin;
}

// Helper to create formatter for stdout
Formatter formatter_stdout() {
    Formatter f;
    f.stream = stdout;
    return f;
}

// Helper to create formatter for stderr
Formatter formatter_stderr() {
    Formatter f;
    f.stream = stderr;
    return f;
}

// ===== Display trait implementations for builtin types =====

// Display for i32
void display_i32(int32_t value, Formatter* f) {
    fprintf(f->stream, "%d", value);
}

// Display for i64
void display_i64(int64_t value, Formatter* f) {
    fprintf(f->stream, "%lld", (long long)value);
}

// Display for i8
void display_i8(int8_t value, Formatter* f) {
    fprintf(f->stream, "%d", (int)value);
}

// Display for i16
void display_i16(int16_t value, Formatter* f) {
    fprintf(f->stream, "%d", (int)value);
}

// Display for u32
void display_u32(uint32_t value, Formatter* f) {
    fprintf(f->stream, "%u", value);
}

// Display for u64
void display_u64(uint64_t value, Formatter* f) {
    fprintf(f->stream, "%llu", (unsigned long long)value);
}

// Display for u8
void display_u8(uint8_t value, Formatter* f) {
    fprintf(f->stream, "%u", (unsigned)value);
}

// Display for u16
void display_u16(uint16_t value, Formatter* f) {
    fprintf(f->stream, "%u", (unsigned)value);
}

// Display for bool
void display_bool(bool value, Formatter* f) {
    fprintf(f->stream, "%s", value ? "true" : "false");
}

// Display for string (const char*)
void display_string(const char* value, Formatter* f) {
    fprintf(f->stream, "%s", value);
}

// Display for str type - takes a pointer to struct with { i8* data, i64 length }
// Note: str data is NOT null-terminated, so we must use the length
typedef struct {
    int8_t* data;
    int64_t length;
} StrWrapper;

void display_str(StrWrapper* str, Formatter* f) {
    // Write exactly 'length' bytes from data
    // str->data is not null-terminated, so we can't use %s
    if (str && str->data && str->length > 0) {
        fwrite(str->data, 1, (size_t)str->length, f->stream);
    }
}

// Display for double (future)
void display_f64(double value, Formatter* f) {
    fprintf(f->stream, "%g", value);
}
