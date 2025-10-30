#ifndef LSP_JSON_H
#define LSP_JSON_H

#include <stdbool.h>
#include <stdint.h>

// Simple JSON builder for LSP responses
// This is a minimal implementation to avoid external dependencies like json-c

typedef struct JSONBuilder JSONBuilder;

// Create a new JSON builder
JSONBuilder* json_builder_create(void);

// Free the builder
void json_builder_free(JSONBuilder* builder);

// Get the final JSON string (caller must free)
char* json_builder_to_string(JSONBuilder* builder);

// Start/end object
void json_start_object(JSONBuilder* builder);
void json_end_object(JSONBuilder* builder);

// Start/end array
void json_start_array(JSONBuilder* builder);
void json_end_array(JSONBuilder* builder);

// Add key (for objects)
void json_add_key(JSONBuilder* builder, const char* key);

// Add values
void json_add_string(JSONBuilder* builder, const char* value);
void json_add_number(JSONBuilder* builder, int64_t value);
void json_add_bool(JSONBuilder* builder, bool value);
void json_add_null(JSONBuilder* builder);

// Convenience: add key-value pairs
void json_add_string_field(JSONBuilder* builder, const char* key, const char* value);
void json_add_number_field(JSONBuilder* builder, const char* key, int64_t value);
void json_add_bool_field(JSONBuilder* builder, const char* key, bool value);

// Add raw JSON (for nested objects)
void json_add_raw(JSONBuilder* builder, const char* json);
void json_add_raw_field(JSONBuilder* builder, const char* key, const char* json);

// === Simple JSON Parser ===

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JSONType;

typedef struct JSONValue JSONValue;

struct JSONValue {
    JSONType type;
    union {
        bool bool_value;
        int64_t number_value;
        char* string_value;
        struct {
            JSONValue** elements;
            int count;
        } array_value;
        struct {
            char** keys;
            JSONValue** values;
            int count;
        } object_value;
    };
};

// Parse JSON string to JSONValue tree
JSONValue* json_parse(const char* json);

// Free JSONValue tree
void json_value_free(JSONValue* value);

// Get value from object by key
JSONValue* json_object_get(JSONValue* obj, const char* key);

// Get string from JSONValue
const char* json_get_string(JSONValue* value);

// Get number from JSONValue
int64_t json_get_number(JSONValue* value);

// Get bool from JSONValue
bool json_get_bool(JSONValue* value);

// Check if value exists and is not null
bool json_is_null(JSONValue* value);

// Escape string for JSON
char* json_escape_string(const char* str);

// Serialize JSONValue back to string
char* json_value_to_string(JSONValue* value);

#endif // LSP_JSON_H
