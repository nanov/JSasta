#include "string_utils.h"
#include <stdarg.h>

// Default initial capacity for string builders
#define SB_DEFAULT_CAPACITY 64
#define SB_GROWTH_FACTOR 2

// ============================================================================
// JsaStringBuilder Implementation
// ============================================================================

JsaStringBuilder* jsa_string_builder_create(void) {
    return jsa_string_builder_create_with_capacity(SB_DEFAULT_CAPACITY);
}

JsaStringBuilder* jsa_string_builder_create_with_capacity(size_t capacity) {
    if (capacity < 1) capacity = 1;
    
    JsaStringBuilder* sb = (JsaStringBuilder*)malloc(sizeof(JsaStringBuilder));
    if (!sb) return NULL;
    
    sb->data = (char*)malloc(capacity);
    if (!sb->data) {
        free(sb);
        return NULL;
    }
    
    sb->data[0] = '\0';
    sb->length = 0;
    sb->capacity = capacity;
    
    return sb;
}

JsaStringBuilder* jsa_string_builder_from_string(const char* str) {
    if (!str) return jsa_string_builder_create();
    
    size_t len = strlen(str);
    JsaStringBuilder* sb = jsa_string_builder_create_with_capacity(len + 1);
    if (!sb) return NULL;
    
    memcpy(sb->data, str, len + 1);
    sb->length = len;
    
    return sb;
}

void jsa_string_builder_free(JsaStringBuilder* sb) {
    if (!sb) return;
    free(sb->data);
    free(sb);
}

void jsa_string_builder_clear(JsaStringBuilder* sb) {
    if (!sb) return;
    sb->data[0] = '\0';
    sb->length = 0;
}

bool jsa_string_builder_reserve(JsaStringBuilder* sb, size_t min_capacity) {
    if (!sb) return false;
    if (sb->capacity >= min_capacity) return true;
    
    // Grow by at least 2x, but ensure we meet min_capacity
    size_t new_capacity = sb->capacity * SB_GROWTH_FACTOR;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    
    char* new_data = (char*)realloc(sb->data, new_capacity);
    if (!new_data) return false;
    
    sb->data = new_data;
    sb->capacity = new_capacity;
    
    return true;
}

bool jsa_string_builder_append(JsaStringBuilder* sb, const char* str) {
    if (!sb || !str) return false;
    return jsa_string_builder_append_n(sb, str, strlen(str));
}

bool jsa_string_builder_append_char(JsaStringBuilder* sb, char ch) {
    if (!sb) return false;
    
    // Need space for character + null terminator
    if (!jsa_string_builder_reserve(sb, sb->length + 2)) {
        return false;
    }
    
    sb->data[sb->length] = ch;
    sb->length++;
    sb->data[sb->length] = '\0';
    
    return true;
}

bool jsa_string_builder_append_n(JsaStringBuilder* sb, const char* str, size_t len) {
    if (!sb || !str || len == 0) return false;
    
    // Need space for len characters + null terminator
    if (!jsa_string_builder_reserve(sb, sb->length + len + 1)) {
        return false;
    }
    
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    
    return true;
}

bool jsa_string_builder_append_format(JsaStringBuilder* sb, const char* fmt, ...) {
    if (!sb || !fmt) return false;
    
    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);
    
    // Calculate required size
    int size = vsnprintf(NULL, 0, fmt, args1);
    va_end(args1);
    
    if (size < 0) {
        va_end(args2);
        return false;
    }
    
    // Reserve space
    if (!jsa_string_builder_reserve(sb, sb->length + size + 1)) {
        va_end(args2);
        return false;
    }
    
    // Format into buffer
    vsnprintf(sb->data + sb->length, size + 1, fmt, args2);
    va_end(args2);
    
    sb->length += size;
    
    return true;
}

bool jsa_string_builder_insert(JsaStringBuilder* sb, size_t pos, const char* str) {
    if (!str) return false;
    return jsa_string_builder_insert_n(sb, pos, str, strlen(str));
}

bool jsa_string_builder_insert_n(JsaStringBuilder* sb, size_t pos, const char* str, size_t len) {
    if (!sb || !str || len == 0) return false;
    
    // Clamp position to valid range
    if (pos > sb->length) {
        pos = sb->length;
    }
    
    // Reserve space
    if (!jsa_string_builder_reserve(sb, sb->length + len + 1)) {
        return false;
    }
    
    // Move existing data to make room
    if (pos < sb->length) {
        memmove(sb->data + pos + len, sb->data + pos, sb->length - pos);
    }
    
    // Copy new data
    memcpy(sb->data + pos, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    
    return true;
}

bool jsa_string_builder_delete(JsaStringBuilder* sb, size_t start, size_t len) {
    if (!sb || len == 0) return true;
    
    // Clamp to valid range
    if (start >= sb->length) return true;
    if (start + len > sb->length) {
        len = sb->length - start;
    }
    
    // Move data after deleted region
    memmove(sb->data + start, sb->data + start + len, sb->length - start - len);
    sb->length -= len;
    sb->data[sb->length] = '\0';
    
    return true;
}

bool jsa_string_builder_replace(JsaStringBuilder* sb, size_t start, size_t len, const char* str) {
    if (!sb || !str) return false;
    
    // Delete old content
    if (!jsa_string_builder_delete(sb, start, len)) {
        return false;
    }
    
    // Insert new content
    return jsa_string_builder_insert(sb, start, str);
}

const char* jsa_string_builder_cstr(const JsaStringBuilder* sb) {
    return sb ? sb->data : "";
}

char* jsa_string_builder_take(JsaStringBuilder* sb) {
    if (!sb) return NULL;
    
    char* result = sb->data;
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    
    return result;
}

// ============================================================================
// LSP Text Document Utilities
// ============================================================================

int jsa_string_builder_position_to_offset(const JsaStringBuilder* sb, size_t line, size_t character) {
    if (!sb || !sb->data) return -1;
    
    size_t current_line = 0;
    size_t offset = 0;
    
    // Scan to find the target line
    while (offset < sb->length && current_line < line) {
        if (sb->data[offset] == '\n') {
            current_line++;
        }
        offset++;
    }
    
    // If we didn't reach the target line, position is out of bounds
    if (current_line < line) return -1;
    
    // Now advance by character count within the line
    size_t current_char = 0;
    while (offset < sb->length && current_char < character && sb->data[offset] != '\n') {
        offset++;
        current_char++;
    }
    
    // Return the offset (even if we didn't reach exact character - we're at end of line)
    return (int)offset;
}

bool jsa_string_builder_offset_to_position(const JsaStringBuilder* sb, size_t offset, size_t* out_line, size_t* out_character) {
    if (!sb || !sb->data || offset > sb->length) return false;
    if (!out_line || !out_character) return false;
    
    size_t line = 0;
    size_t character = 0;
    
    for (size_t i = 0; i < offset; i++) {
        if (sb->data[i] == '\n') {
            line++;
            character = 0;
        } else {
            character++;
        }
    }
    
    *out_line = line;
    *out_character = character;
    
    return true;
}

bool jsa_string_builder_apply_edit(JsaStringBuilder* sb, const TextRange* range, const char* new_text) {
    if (!sb || !range || !new_text) return false;
    
    // Convert positions to offsets
    int start_offset = jsa_string_builder_position_to_offset(sb, range->start.line, range->start.character);
    int end_offset = jsa_string_builder_position_to_offset(sb, range->end.line, range->end.character);
    
    if (start_offset < 0 || end_offset < 0) return false;
    if (start_offset > end_offset) return false;
    
    // Calculate length to delete
    size_t delete_len = (size_t)(end_offset - start_offset);
    
    // Replace the range
    return jsa_string_builder_replace(sb, (size_t)start_offset, delete_len, new_text);
}
