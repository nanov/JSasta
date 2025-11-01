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

// Display for double (future)
void display_f64(double value, Formatter* f) {
    fprintf(f->stream, "%g", value);
}
