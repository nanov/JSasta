#include "format_string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

FormatString* format_string_parse(const char* format) {
    if (!format) return NULL;

    FormatString* fs = (FormatString*)malloc(sizeof(FormatString));
    fs->placeholder_count = 0;
    fs->part_count = 0;
    fs->parts = NULL;

    // First pass: count placeholders
    const char* p = format;
    while (*p) {
        if (*p == '{') {
            if (*(p + 1) == '}') {
                fs->placeholder_count++;
                p += 2;
            } else if (*(p + 1) == '{') {
                // Escaped brace: {{
                p += 2;
            } else {
                // Invalid: { not followed by } or {
                free(fs);
                return NULL;
            }
        } else if (*p == '}') {
            if (*(p + 1) == '}') {
                // Escaped brace: }}
                p += 2;
            } else {
                // Invalid: } without preceding {
                free(fs);
                return NULL;
            }
        } else {
            p++;
        }
    }

    // Allocate parts array (placeholder_count + 1)
    fs->part_count = fs->placeholder_count + 1;
    fs->parts = (char**)malloc(sizeof(char*) * fs->part_count);

    // Second pass: extract literal parts
    int part_idx = 0;
    p = format;

    char buffer[4096];  // Temp buffer for building parts
    int buf_idx = 0;

    while (*p) {
        if (*p == '{' && *(p + 1) == '}') {
            // Found placeholder - save current part
            buffer[buf_idx] = '\0';
            fs->parts[part_idx++] = strdup(buffer);
            buf_idx = 0;
            p += 2;
        } else if (*p == '{' && *(p + 1) == '{') {
            // Escaped { - add single {
            buffer[buf_idx++] = '{';
            p += 2;
        } else if (*p == '}' && *(p + 1) == '}') {
            // Escaped } - add single }
            buffer[buf_idx++] = '}';
            p += 2;
        } else {
            buffer[buf_idx++] = *p++;
        }
    }

    // Save final part
    buffer[buf_idx] = '\0';
    fs->parts[part_idx++] = strdup(buffer);

    return fs;
}

void format_string_free(FormatString* fs) {
    if (!fs) return;

    for (int i = 0; i < fs->part_count; i++) {
        free(fs->parts[i]);
    }
    free(fs->parts);
    free(fs);
}

bool format_string_validate_args(FormatString* fs, int arg_count) {
    if (!fs) return false;
    return fs->placeholder_count == arg_count;
}
