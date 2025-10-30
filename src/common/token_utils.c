#include "jsasta_compiler.h"

// Generate string lookup table
#define TOKEN_STRING(name, str) str,
static const char* token_strings[] = {
    TOKEN_TYPES(TOKEN_STRING)
};
#undef TOKEN_STRING

const char* token_type_to_string(TokenType type) {
    if (type >= 0 && type < sizeof(token_strings) / sizeof(token_strings[0])) {
        return token_strings[type];
    }
    return "UNKNOWN";
}
