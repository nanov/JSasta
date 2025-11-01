#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "lsp_json.h"

// Forward declaration
static void json_value_to_builder(JSONValue* value, JSONBuilder* builder);

// JSON Builder implementation
struct JSONBuilder {
    char* buffer;
    size_t size;
    size_t capacity;
    int depth;          // Nesting depth
    bool needs_comma;   // Track if we need comma before next element
};

JSONBuilder* json_builder_create(void) {
    JSONBuilder* builder = malloc(sizeof(JSONBuilder));
    builder->capacity = 1024;
    builder->buffer = malloc(builder->capacity);
    builder->size = 0;
    builder->depth = 0;
    builder->needs_comma = false;
    builder->buffer[0] = '\0';
    return builder;
}

void json_builder_free(JSONBuilder* builder) {
    if (builder) {
        free(builder->buffer);
        free(builder);
    }
}

char* json_builder_to_string(JSONBuilder* builder) {
    return strdup(builder->buffer);
}

static void json_ensure_capacity(JSONBuilder* builder, size_t additional) {
    size_t required = builder->size + additional + 1;
    if (required > builder->capacity) {
        while (builder->capacity < required) {
            builder->capacity *= 2;
        }
        builder->buffer = realloc(builder->buffer, builder->capacity);
    }
}

static void json_append(JSONBuilder* builder, const char* str) {
    size_t len = strlen(str);
    json_ensure_capacity(builder, len);
    strcpy(builder->buffer + builder->size, str);
    builder->size += len;
}

static void json_append_char(JSONBuilder* builder, char c) {
    json_ensure_capacity(builder, 1);
    builder->buffer[builder->size++] = c;
    builder->buffer[builder->size] = '\0';
}

static void json_add_comma_if_needed(JSONBuilder* builder) {
    if (builder->needs_comma) {
        json_append_char(builder, ',');
    }
    builder->needs_comma = true;
}

void json_start_object(JSONBuilder* builder) {
    json_add_comma_if_needed(builder);
    json_append_char(builder, '{');
    builder->depth++;
    builder->needs_comma = false;
}

void json_end_object(JSONBuilder* builder) {
    json_append_char(builder, '}');
    builder->depth--;
    builder->needs_comma = true;
}

void json_start_array(JSONBuilder* builder) {
    json_add_comma_if_needed(builder);
    json_append_char(builder, '[');
    builder->depth++;
    builder->needs_comma = false;
}

void json_end_array(JSONBuilder* builder) {
    json_append_char(builder, ']');
    builder->depth--;
    builder->needs_comma = true;
}

void json_add_key(JSONBuilder* builder, const char* key) {
    json_add_comma_if_needed(builder);
    json_append_char(builder, '"');
    json_append(builder, key);
    json_append(builder, "\":");
    builder->needs_comma = false;
}

char* json_escape_string(const char* str) {
    if (!str) return strdup("null");

    size_t len = strlen(str);
    char* escaped = malloc(len * 2 + 3); // Worst case: all chars need escaping + quotes + null
    char* p = escaped;

    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        switch (c) {
            case '"':  *p++ = '\\'; *p++ = '"'; break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\b': *p++ = '\\'; *p++ = 'b'; break;
            case '\f': *p++ = '\\'; *p++ = 'f'; break;
            case '\n': *p++ = '\\'; *p++ = 'n'; break;
            case '\r': *p++ = '\\'; *p++ = 'r'; break;
            case '\t': *p++ = '\\'; *p++ = 't'; break;
            default:
                if ((unsigned char)c < 32) {
                    p += sprintf(p, "\\u%04x", (unsigned char)c);
                } else {
                    *p++ = c;
                }
                break;
        }
    }
    *p = '\0';
    return escaped;
}

void json_add_string(JSONBuilder* builder, const char* value) {
    json_add_comma_if_needed(builder);
    if (value == NULL) {
        json_append(builder, "null");
    } else {
        char* escaped = json_escape_string(value);
        json_append_char(builder, '"');
        json_append(builder, escaped);
        json_append_char(builder, '"');
        free(escaped);
    }
}

void json_add_number(JSONBuilder* builder, int64_t value) {
    json_add_comma_if_needed(builder);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)value);
    json_append(builder, buf);
}

void json_add_bool(JSONBuilder* builder, bool value) {
    json_add_comma_if_needed(builder);
    json_append(builder, value ? "true" : "false");
}

void json_add_null(JSONBuilder* builder) {
    json_add_comma_if_needed(builder);
    json_append(builder, "null");
}

void json_add_string_field(JSONBuilder* builder, const char* key, const char* value) {
    json_add_key(builder, key);
    json_add_string(builder, value);
}

void json_add_number_field(JSONBuilder* builder, const char* key, int64_t value) {
    json_add_key(builder, key);
    json_add_number(builder, value);
}

void json_add_bool_field(JSONBuilder* builder, const char* key, bool value) {
    json_add_key(builder, key);
    json_add_bool(builder, value);
}

void json_add_raw(JSONBuilder* builder, const char* json) {
    json_add_comma_if_needed(builder);
    json_append(builder, json);
}

void json_add_raw_field(JSONBuilder* builder, const char* key, const char* json) {
    json_add_key(builder, key);
    json_append(builder, json);
}

// === Simple JSON Parser ===


static void skip_whitespace(JSONParser* p) {
    while (p->pos < p->length && isspace(p->input[p->pos])) {
        p->pos++;
    }
}

static bool match_char(JSONParser* p, char c) {
    skip_whitespace(p);
    if (p->pos < p->length && p->input[p->pos] == c) {
        p->pos++;
        return true;
    }
    return false;
}

static bool consume_next_char(JSONParser* p, char* c) {
    skip_whitespace(p);
    if (p->pos < p->length) {
    		*c = p->input[p->pos++];
        return true;
    }
    return false;
}


static bool match_string(JSONParser* p, const char* str) {
    skip_whitespace(p);
    size_t len = strlen(str);
    if (p->pos + len <= p->length && memcmp(p->input + p->pos, str, len) == 0) {
        p->pos += len;
        return true;
    }
    return false;
}

static JSONValue* parse_value(JSONParser* p);

static JSONValue* parse_null(JSONParser* p) {
    if (!match_string(p, "null")) return NULL;
    JSONValue* v = malloc(sizeof(JSONValue));
    v->type = JSON_NULL;
    return v;
}


static JSONValue* parse_bool(JSONParser* p) {
    if (match_string(p, "true")) {
        JSONValue* v = malloc(sizeof(JSONValue));
        v->type = JSON_BOOL;
        v->bool_value = true;
        return v;
    }
    if (match_string(p, "false")) {
        JSONValue* v = malloc(sizeof(JSONValue));
        v->type = JSON_BOOL;
        v->bool_value = false;
        return v;
    }
    return NULL;
}




static JSONValue* parse_number(JSONParser* p) {
    skip_whitespace(p);
    size_t start = p->pos;

    if (p->pos < p->length && (p->input[p->pos] == '-' || p->input[p->pos] == '+')) {
        p->pos++;
    }

    if (p->pos >= p->length || !isdigit(p->input[p->pos])) {
        p->pos = start;
        return NULL;
    }

    while (p->pos < p->length && isdigit(p->input[p->pos])) {
        p->pos++;
    }

    // For simplicity, we only parse integers
    char* num_str = strndup(p->input + start, p->pos - start);
    JSONValue* v = malloc(sizeof(JSONValue));
    v->type = JSON_NUMBER;
    v->number_value = atoll(num_str);
    free(num_str);
    return v;
}

static int parse_string_string_and_terminate(JSONParser* p) {
  if (!match_char(p, '"')) return 1;

  const char* search_start = p->input + p->pos;
  size_t remaining_len = p->length - p->pos;
  void* closing_quote_ptr = memchr(search_start, '"', remaining_len);

  if (closing_quote_ptr == NULL) return 1; // Error: Unterminated string.

  char* closing_quote = (char*)closing_quote_ptr;
  *closing_quote = '\0'; // Replace '"' with '\0'.
   p->pos += (closing_quote - search_start) + 1;
  return 0;
}

static JSONValue* parse_string(JSONParser* p) {
    skip_whitespace(p);
    if (!match_char(p, '"')) return NULL;

    // Build unescaped string
    size_t capacity = 256;
    char* str = malloc(capacity);
    size_t len = 0;

    while (p->pos < p->length && p->input[p->pos] != '"') {
        if (len + 2 >= capacity) {
            capacity *= 2;
            str = realloc(str, capacity);
        }

        if (p->input[p->pos] == '\\' && p->pos + 1 < p->length) {
            p->pos++; // Skip backslash
            char escaped = p->input[p->pos++];
            switch (escaped) {
                case 'n': str[len++] = '\n'; break;
                case 't': str[len++] = '\t'; break;
                case 'r': str[len++] = '\r'; break;
                case '"': str[len++] = '"'; break;
                case '\\': str[len++] = '\\'; break;
                case '/': str[len++] = '/'; break;
                case 'b': str[len++] = '\b'; break;
                case 'f': str[len++] = '\f'; break;
                default:
                    // Unknown escape, keep as-is
                    str[len++] = '\\';
                    str[len++] = escaped;
                    break;
            }
        } else {
            str[len++] = p->input[p->pos++];
        }
    }

    if (p->pos >= p->length) {
        free(str);
        return NULL;
    }

    str[len] = '\0';
    p->pos++; // Skip closing quote

    JSONValue* v = malloc(sizeof(JSONValue));
    v->type = JSON_STRING;
    v->string_value = str;
    return v;
}

static JSONValue* parse_array(JSONParser* p) {
    if (!match_char(p, '[')) return NULL;

    JSONValue* v = malloc(sizeof(JSONValue));
    v->type = JSON_ARRAY;
    v->array_value.count = 0;
    v->array_value.elements = malloc(sizeof(JSONValue*) * 16);
    int capacity = 16;

    skip_whitespace(p);
    if (match_char(p, ']')) {
        return v;
    }

    do {
        JSONValue* elem = parse_value(p);
        if (!elem) {
            json_value_free(v);
            return NULL;
        }

        if (v->array_value.count >= capacity) {
            capacity *= 2;
            v->array_value.elements = realloc(v->array_value.elements, sizeof(JSONValue*) * capacity);
        }

        v->array_value.elements[v->array_value.count++] = elem;
    } while (match_char(p, ','));

    if (!match_char(p, ']')) {
        json_value_free(v);
        return NULL;
    }

    return v;
}

static JSONValue* parse_object(JSONParser* p) {
    if (!match_char(p, '{')) return NULL;

    JSONValue* v = malloc(sizeof(JSONValue));
    v->type = JSON_OBJECT;
    v->object_value.count = 0;
    v->object_value.keys = malloc(sizeof(char*) * 16);
    v->object_value.values = malloc(sizeof(JSONValue*) * 16);
    int capacity = 16;

    skip_whitespace(p);
    if (match_char(p, '}')) {
        return v;
    }

    do {
        JSONValue* key_val = parse_string(p);
        if (!key_val || key_val->type != JSON_STRING) {
            json_value_free(v);
            if (key_val) json_value_free(key_val);
            return NULL;
        }

        if (!match_char(p, ':')) {
            json_value_free(v);
            json_value_free(key_val);
            return NULL;
        }

        JSONValue* val = parse_value(p);
        if (!val) {
            json_value_free(v);
            json_value_free(key_val);
            return NULL;
        }

        if (v->object_value.count >= capacity) {
            capacity *= 2;
            v->object_value.keys = realloc(v->object_value.keys, sizeof(char*) * capacity);
            v->object_value.values = realloc(v->object_value.values, sizeof(JSONValue*) * capacity);
        }

        v->object_value.keys[v->object_value.count] = key_val->string_value;
        v->object_value.values[v->object_value.count] = val;
        v->object_value.count++;

        key_val->string_value = NULL; // Transfer ownership
        json_value_free(key_val);

    } while (match_char(p, ','));

    if (!match_char(p, '}')) {
        json_value_free(v);
        return NULL;
    }

    return v;
}

static int skip_string_content(JSONParser* p) {
    while (p->pos < p->length) {
        char c = p->input[p->pos];
        if (c == '"') {
            p->pos++; // Consume closing quote
            return 0;
        }
        if (c == '\\' && p->pos + 1 < p->length) {
            p->pos += 2; // Skip backslash and the escaped char
        } else {
            p->pos++;
        }
    }
    return 1; // Unterminated string
}

static int json_skip_value_fast(JSONParser* p, char scope_end) {
    skip_whitespace(p);
    if (p->pos >= p->length) return 1; // End of input

    char start_char = p->input[p->pos];

    switch (start_char) {
        // Case 1: A string. We must find its real end.
        case '"':
            p->pos++; // Consume opening quote
            return skip_string_content(p);

        // Case 2: A nested structure. We must find its real end.
        case '{':
        case '[': {
            char open_char = start_char;
            char close_char = (open_char == '{') ? '}' : ']';
            int depth = 1;

            p->pos++; // Consume the opening brace/bracket

            while (depth > 0 && p->pos < p->length) {
                char current_char = p->input[p->pos];

                if (current_char == '"') {
                    p->pos++; // Consume opening quote of the inner string
                    if (skip_string_content(p) != 0) return 1; // Unterminated string
                    continue;
                }

                if (current_char == open_char) {
                    depth++;
                } else if (current_char == close_char) {
                    depth--;
                }
                p->pos++;
            }
            return (depth == 0) ? 0 : 1; // Fail if unterminated
        }

        // Case 3: Any other value (number, true, false, null, or even garbage).
        // Perform a blind, high-speed scan for a terminator.
        default: {
          while (p->pos < p->length)  while (p->pos < p->length) {
              unsigned char c = p->input[p->pos];
              if (isspace(c) || c == ',' || c == scope_end)
                  break;
              p->pos++;
          }
          return 0; // This scan always "succeeds".s "succeeds"
        }
    }
}

int json_parse_fast_object(JSONParser* p, JSONParserObjectCallback* callback, void* user_data) {
    if (!match_char(p, '{')) return 1;
    skip_whitespace(p);
    if (match_char(p, '}')) return 0;


    while(p->pos < p->length) {
	    const char* key = &p->input[p->pos + 1];
      if (parse_string_string_and_terminate(p)  != 0)
          return 1;

      skip_whitespace(p);
      if (!match_char(p, ':')) return 1;
      skip_whitespace(p);

      int res = callback(p, key, user_data);
      if (res == -1)
      	res = json_skip_value_fast(p, '}');

      if (res != 0)
      	return res;

      skip_whitespace(p);
      char next;
      if (!consume_next_char(p, &next)) return 1;
      if (next == ',') continue;
      if (next == '}') return 0;
      return 1;
    }
    return 0; // unreachable
}

int json_parse_fast_array(JSONParser* p, JSONParserArrayCallback* callback, void* opaue) {
    if (!match_char(p, '[')) return 1;
    skip_whitespace(p);
    if (match_char(p, ']')) return 0;

    size_t idx = 0;

    while(p->pos < p->length) {
  		int res = callback(p, idx++, opaue);
      if (res == -1)
      	res = json_skip_value_fast(p, ']');

      if (res != 0)
      	return res;

      skip_whitespace(p);
      char next;
      if (!consume_next_char(p, &next)) return 1;
      if (next == ',') continue;
      if (next == ']') return 0;
      return 1;
    }
    return 0; // unreachable
}

int json_get_fast_string(JSONParser* p, char** target) {
	skip_whitespace(p);
  if (!match_char(p, '"')) return 0;

  size_t capacity = 128;
  char* str = malloc(capacity);
  size_t len = 0;

  while (p->pos < p->length && p->input[p->pos] != '"') {
      if (len + 6 >= capacity) { // unicode
          capacity *= 2;
          str = realloc(str, capacity);
      }

      if (p->input[p->pos] == '\\' && p->pos + 1 < p->length) {
          p->pos++; // Skip backslash
          unsigned char escaped = p->input[p->pos++];
          switch (escaped) {
              case 'n': str[len++] = '\n'; break;
              case 't': str[len++] = '\t'; break;
              case 'r': str[len++] = '\r'; break;
              case '"': str[len++] = '"';  break;
              case '\\': str[len++] = '\\'; break;
              case '/': str[len++] = '/';  break;
              case 'b': str[len++] = '\b'; break;
              case 'f': str[len++] = '\f'; break;
              case 'u': { // unicode
                  if (p->pos + 4 > p->length) {
                      free(str);
                      return 0; // Malformed \u escape at end of input
                  }
                  str[len++] = '\\';
                  str[len++] = 'u';
                  memcpy(&str[len], &p->input[p->pos], 4);
                  p->pos += 4;
                  len += 4;
                  break;
              }
              default: // Unknown escape, keep as-is
                  str[len++] = '\\';
                  str[len++] = escaped;
                  break;
          }
      } else {
          str[len++] = p->input[p->pos++];
      }
  }

  // Use match_char to ensure the string is properly terminated with a quote
  if (!match_char(p, '"')) {
      free(str);
      return 1; // Unterminated string
  }

  str[len] = '\0';

  *target = str; // Return shrunk ptr, or original on realloc failure
  return 0;
}

int json_get_fast_integer(JSONParser* p, int64_t* target) {
  skip_whitespace(p);

  int64_t multiplier = 1;

  if (p->input[p->pos] == '-') {
    multiplier = -1;
    p->pos++;
  } else if (p->input[p->pos] == '+') {
	  p->pos++;
  }

  int64_t value = 0;

  while (p->pos < p->length && isdigit((unsigned char)p->input[p->pos])) {
    value = value * 10 + (p->input[p->pos] - '0');
    p->pos++;
  }

  *target = value * multiplier;
  return 0;
}

int json_get_fast_bool(JSONParser* p, bool* target) {
	skip_whitespace(p);

    // 1. Try to match "true".
    if (p->pos + 4 <= p->length && strncmp(&p->input[p->pos], "true", 4) == 0) {
        p->pos += 4;
        *target = true;
        return 0;
    }

    // 2. Try to match "false".
    if (p->pos + 5 <= p->length && strncmp(&p->input[p->pos], "false", 5) == 0) {
        p->pos += 5;
        *target = false;
        return 0;
    }

    return 1;
}

JSONParser json_parser_create(const char* json, size_t size) {
  JSONParser p = {
      .input = json,
      .pos = 0,
      .length = size,
  };
  return p;
}

static JSONValue* parse_value(JSONParser* p) {
    skip_whitespace(p);

    if (p->pos >= p->length) return NULL;

    // Try each type
    JSONValue* v = parse_null(p);
    if (v) return v;

    v = parse_bool(p);
    if (v) return v;

    v = parse_number(p);
    if (v) return v;

    v = parse_string(p);
    if (v) return v;

    v = parse_array(p);
    if (v) return v;

    v = parse_object(p);
    if (v) return v;

    return NULL;
}

JSONValue* json_parse(const char* json) {
    if (!json) return NULL;

    JSONParser p = {
        .input = json,
        .pos = 0,
        .length = strlen(json)
    };

    return parse_value(&p);
}

void json_value_free(JSONValue* value) {
    if (!value) return;

    switch (value->type) {
        case JSON_STRING:
            free(value->string_value);
            break;
        case JSON_ARRAY:
            for (int i = 0; i < value->array_value.count; i++) {
                json_value_free(value->array_value.elements[i]);
            }
            free(value->array_value.elements);
            break;
        case JSON_OBJECT:
            for (int i = 0; i < value->object_value.count; i++) {
                free(value->object_value.keys[i]);
                json_value_free(value->object_value.values[i]);
            }
            free(value->object_value.keys);
            free(value->object_value.values);
            break;
        default:
            break;
    }

    free(value);
}

JSONValue* json_object_get(JSONValue* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;

    for (int i = 0; i < obj->object_value.count; i++) {
        if (strcmp(obj->object_value.keys[i], key) == 0) {
            return obj->object_value.values[i];
        }
    }

    return NULL;
}

const char* json_get_string(JSONValue* value) {
    if (!value || value->type != JSON_STRING) return NULL;
    return value->string_value;
}

int64_t json_get_number(JSONValue* value) {
    if (!value || value->type != JSON_NUMBER) return 0;
    return value->number_value;
}

bool json_get_bool(JSONValue* value) {
    if (!value || value->type != JSON_BOOL) return false;
    return value->bool_value;
}

bool json_is_null(JSONValue* value) {
    return !value || value->type == JSON_NULL;
}

// Serialize JSONValue back to string
char* json_value_to_string(JSONValue* value) {
    if (!value) return strdup("null");

    JSONBuilder* builder = json_builder_create();
    json_value_to_builder(value, builder);
    char* result = json_builder_to_string(builder);
    json_builder_free(builder);
    return result;
}

// Helper to serialize JSONValue to builder
static void json_value_to_builder(JSONValue* value, JSONBuilder* builder) {
    if (!value || value->type == JSON_NULL) {
        json_add_null(builder);
        return;
    }

    switch (value->type) {
        case JSON_BOOL:
            json_add_bool(builder, value->bool_value);
            break;

        case JSON_NUMBER:
            json_add_number(builder, value->number_value);
            break;

        case JSON_STRING:
            json_add_string(builder, value->string_value);
            break;

        case JSON_ARRAY:
            json_start_array(builder);
            for (int i = 0; i < value->array_value.count; i++) {
                json_value_to_builder(value->array_value.elements[i], builder);
            }
            json_end_array(builder);
            break;

        case JSON_OBJECT:
            json_start_object(builder);
            for (int i = 0; i < value->object_value.count; i++) {
                json_add_key(builder, value->object_value.keys[i]);
                json_value_to_builder(value->object_value.values[i], builder);
            }
            json_end_object(builder);
            break;

        default:
            json_add_null(builder);
            break;
    }
}
