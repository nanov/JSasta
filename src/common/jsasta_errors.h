#ifndef JSASTA_ERRORS_H
#define JSASTA_ERRORS_H

#include <string.h>

/**
 * JSasta Compiler Error and Warning Catalog
 * 
 * Single-header error system using X-macros.
 * 
 * Error Code Prefixes:
 * - JEXXX: General/System/Compiler errors
 * - PEXXX: Parser errors
 * - TEXXX: Type system errors
 * - CEXXX: Code generation errors
 * - VEXXX: Validation errors (builtins, modules)
 * - JWXXX: General warnings
 * - PWXXX: Parser warnings
 * - TWXXX: Type warnings
 * - CWXXX: Codegen warnings
 * - VWXXX: Validation warnings
 * 
 * Usage:
 *   JSASTA_ERROR(diag, loc, PE_EXPECTED_PROPERTY_NAME);
 *   JSASTA_ERROR(diag, loc, TE_UNDEFINED_VARIABLE, "myVar");
 */

// ============================================================================
// ERROR CATALOG - X-Macro Definition
// ============================================================================
// Format: ERROR_DEF(LONG_NAME, "SHORT_CODE", "Template message")

#define JSASTA_ERROR_CATALOG \
    /* General/System Errors (JE001-JE099) */ \
    ERROR_DEF(JE_INTERNAL_ERROR,                  "JE001", "Internal compiler error: %s") \
    ERROR_DEF(JE_OUT_OF_MEMORY,                   "JE002", "Out of memory") \
    \
    /* Parser Errors (PE100-PE399) */ \
    ERROR_DEF(PE_EXPECTED_TOKEN,                  "PE100", "Expected %s, got %s") \
    ERROR_DEF(PE_UNKNOWN_TYPE,                    "PE101", "Unknown type '%s'") \
    ERROR_DEF(PE_UNKNOWN_TYPE_PATH,               "PE102", "Unknown type '%s'") \
    ERROR_DEF(PE_EXPECTED_PROPERTY_NAME,          "PE201", "Expected property name in object literal") \
    ERROR_DEF(PE_UNEXPECTED_TOKEN_EXPR,           "PE202", "Unexpected token in expression: %s") \
    ERROR_DEF(PE_POSTFIX_ON_NON_LVALUE,           "PE203", "Postfix operator can only be applied to identifiers or member access") \
    ERROR_DEF(PE_EXPECTED_IDENTIFIER_AFTER_DOT,   "PE204", "Expected identifier after '.'") \
    ERROR_DEF(PE_EXPECTED_PROPERTY_NAME_AFTER_DOT, "PE205", "Expected property name after '.'") \
    ERROR_DEF(PE_EXPECTED_CLOSING_BRACKET,        "PE206", "Expected ']' after index expression") \
    ERROR_DEF(PE_EXPECTED_EXPR_AFTER_PREFIX,      "PE207", "Expected identifier or expression after %s") \
    ERROR_DEF(PE_INVALID_ASSIGNMENT_TARGET,       "PE208", "Invalid assignment target") \
    ERROR_DEF(PE_COMPOUND_ASSIGN_REQUIRES_LVALUE, "PE209", "Compound assignment requires identifier or member access on left side") \
    ERROR_DEF(PE_EXPECTED_TYPE,                   "PE210", "Expected type after 'new'") \
    ERROR_DEF(PE_EXPECTED_IDENTIFIER_AFTER_VAR,   "PE211", "Expected identifier after var/let/const") \
    ERROR_DEF(PE_EXPECTED_ARRAY_SIZE,             "PE212", "Expected array size expression after '['") \
    ERROR_DEF(PE_EXPECTED_CLOSING_BRACKET_AFTER_SIZE, "PE213", "Expected ']' after array size") \
    ERROR_DEF(PE_EXPECTED_EXPR_AFTER_EQUALS,      "PE214", "Expected expression after =") \
    ERROR_DEF(PE_EXTERNAL_FUNC_NEEDS_TYPE_HINTS,  "PE215", "External function parameters must have type annotations") \
    ERROR_DEF(PE_UNKNOWN_TYPE_IN_PARAM,           "PE216", "Unknown type '%s' in external function parameter") \
    ERROR_DEF(PE_EXPECTED_PARAM_NAME_OR_TYPE,     "PE217", "Expected parameter name or type in external function declaration") \
    ERROR_DEF(PE_EXTERNAL_FUNC_NEEDS_RETURN_TYPE, "PE218", "External function must have return type annotation") \
    ERROR_DEF(PE_EXPECTED_STRUCT_NAME,            "PE219", "Expected struct name after 'struct' keyword") \
    ERROR_DEF(PE_EXPECTED_METHOD_PARAM_NAME,      "PE220", "Expected parameter name") \
    ERROR_DEF(PE_METHOD_PARAM_NEEDS_TYPE,         "PE221", "Method parameter '%s' must have a type annotation") \
    ERROR_DEF(PE_METHOD_NEEDS_RETURN_TYPE,        "PE222", "Method '%s' must have a return type annotation") \
    ERROR_DEF(PE_EXPECTED_PROPERTY_OR_METHOD_NAME, "PE223", "Expected property or method name in struct declaration") \
    ERROR_DEF(PE_STRUCT_PROPERTY_NEEDS_TYPE,      "PE224", "Struct property '%s' must have a type annotation") \
    ERROR_DEF(PE_EXPECTED_ARRAY_SIZE_IN_STRUCT,   "PE225", "Expected array size expression after '['") \
    ERROR_DEF(PE_EXPECTED_CLOSING_BRACKET_IN_STRUCT, "PE226", "Expected ']' after array size") \
    ERROR_DEF(PE_STRUCT_ARRAY_NEEDS_SIZE_OR_REF,  "PE227", "Array fields in structs must have explicit size (e.g., arr: i32[12]) or be a reference (e.g., arr: ref i32[]).") \
    ERROR_DEF(PE_DEFAULT_VALUE_MUST_BE_LITERAL,   "PE228", "Default values must be literals (number, string, true, or false)") \
    ERROR_DEF(PE_STUCK_ON_TOKEN,                  "PE229", "Stuck on token %s, value '%s'") \
    ERROR_DEF(PE_EXPECTED_NAMESPACE_IDENTIFIER,   "PE230", "Expected namespace identifier after 'import'") \
    ERROR_DEF(PE_EXPECTED_FROM_KEYWORD,           "PE231", "Expected 'from' after namespace identifier") \
    ERROR_DEF(PE_EXPECTED_MODULE_PATH,            "PE232", "Expected string literal or @builtin for module path") \
    ERROR_DEF(PE_EXPECTED_SEMICOLON_AFTER_IMPORT, "PE233", "Expected ';' after import declaration") \
    ERROR_DEF(PE_EXPECTED_ENUM_NAME,              "PE240", "Expected enum name after 'enum' keyword") \
    ERROR_DEF(PE_EXPECTED_VARIANT_NAME,           "PE241", "Expected variant name in enum declaration") \
    ERROR_DEF(PE_EXPECTED_FIELD_NAME_IN_VARIANT,  "PE242", "Expected field name or type in enum variant") \
    ERROR_DEF(PE_FIELD_NEEDS_TYPE_IN_VARIANT,     "PE243", "Expected type annotation for field '%s'") \
    ERROR_DEF(PE_EXPECTED_SEMICOLON_OR_PAREN_AFTER_VARIANT, "PE245", "Expected ';' or '(' after variant name") \
    ERROR_DEF(PE_EXPECTED_ENUM_TYPE_IN_PATTERN,   "PE250", "Expected enum type before variant construction") \
    ERROR_DEF(PE_EXPECTED_DOT_AFTER_ENUM_TYPE,    "PE251", "Expected '.' after enum type name") \
    ERROR_DEF(PE_EXPECTED_VARIANT_NAME_IN_PATTERN, "PE252", "Expected variant name after '.'") \
    ERROR_DEF(PE_EXPECTED_IDENTIFIER_IN_PATTERN,  "PE253", "Expected identifier after 'let'/'var'/'const' in pattern") \
    ERROR_DEF(PE_EXPECTED_BINDING_KEYWORD,        "PE254", "Expected 'let', 'var', 'const', or '_' in pattern binding") \
    ERROR_DEF(PE_EXPECTED_COMMA_OR_PAREN_IN_PATTERN, "PE255", "Expected ',' or ')' in pattern bindings") \
    \
    /* Type System Errors (TE100-TE399) */ \
    ERROR_DEF(TE_TYPE_MISMATCH,                   "TE101", "Type mismatch: expected %s, got %s") \
    ERROR_DEF(TE_UNDEFINED_VARIABLE,              "TE301", "Undefined variable: %s") \
    ERROR_DEF(TE_CANNOT_CALL_METHOD_ON_NON_OBJECT, "TE302", "Cannot call method on non-object type") \
    ERROR_DEF(TE_ARRAY_INDEX_NON_INTEGER,         "TE304", "Array index must be an integer type, got %s") \
    ERROR_DEF(TE_PROPERTY_NOT_FOUND,              "TE305", "Property '%s' not found on type '%s'") \
    ERROR_DEF(TE_DUPLICATE_VARIABLE,              "TE306", "Variable '%s' is already defined in this scope") \
    ERROR_DEF(TE_FUNCTION_NOT_FOUND,              "TE307", "Function '%s' not found") \
    ERROR_DEF(TE_WRONG_ARGUMENT_COUNT,            "TE308", "Function '%s' expects %d argument%s, but got %d") \
    ERROR_DEF(TE_OPERATOR_NOT_SUPPORTED,          "TE309", "Operator '%s' not supported for type '%s'") \
    ERROR_DEF(TE_RETURN_TYPE_MISMATCH,            "TE310", "Return type mismatch: expected %s, got %s") \
    ERROR_DEF(TE_MISSING_RETURN,                  "TE311", "Function '%s' must return a value of type %s") \
    ERROR_DEF(TE_CANNOT_DELETE_NON_REF,           "TE312", "Cannot delete non-reference type") \
    ERROR_DEF(TE_FIELD_NOT_FOUND,                 "TE313", "Field '%s' not found in %s") \
    ERROR_DEF(TE_VARIANT_NOT_FOUND,               "TE314", "Variant '%s' not found in enum '%s'") \
    ERROR_DEF(TE_ENUM_NOT_FOUND,                  "TE315", "Enum '%s' not found") \
    ERROR_DEF(TE_TYPE_ANNOTATION_REQUIRED,        "TE320", "Type annotation required for %s") \
    ERROR_DEF(TE_CANNOT_INFER_TYPE,               "TE321", "Cannot infer type for %s") \
    ERROR_DEF(TE_INCOMPATIBLE_TYPES,              "TE322", "Incompatible types: %s and %s") \
    ERROR_DEF(TE_INVALID_CAST,                    "TE323", "Cannot cast from %s to %s") \
    ERROR_DEF(TE_TRAIT_NOT_IMPLEMENTED,           "TE324", "Type '%s' does not implement trait '%s'") \
    ERROR_DEF(TE_METHOD_NOT_FOUND,                "TE325", "Method '%s' not found on type '%s'") \
    ERROR_DEF(TE_AMBIGUOUS_TYPE,                  "TE326", "Ambiguous type: could be %s or %s") \
    \
    /* Code Generation Errors (CE400-CE499) */ \
    ERROR_DEF(CE_CODEGEN_FAILED,                  "CE400", "Code generation failed: %s") \
    \
    /* Validation Errors (VE300-VE399) */ \
    ERROR_DEF(VE_FORMAT_REQUIRES_ARG,             "VE301", "%s requires at least one argument (format string)") \
    ERROR_DEF(VE_FORMAT_STRING_MUST_BE_LITERAL,   "VE302", "First argument to %s must be a string literal") \
    ERROR_DEF(VE_INVALID_FORMAT_STRING,           "VE303", "Invalid format string: unmatched braces") \
    ERROR_DEF(VE_FORMAT_PLACEHOLDER_MISMATCH,     "VE304", "%s: format string has %d placeholder%s but only %d argument%s provided") \
    ERROR_DEF(VE_ASSERT_EQUALS_REQUIRES_TWO_ARGS, "VE305", "assert_equals requires exactly 2 arguments") \
    ERROR_DEF(VE_ASSERT_EQUALS_TYPE_MISMATCH,     "VE306", "assert_equals arguments must have the same type") \
    ERROR_DEF(VE_ASSERT_EQUALS_NEEDS_EQ_TRAIT,    "VE307", "Type '%s' does not implement the Eq trait, which is required for assert_equals") \
    ERROR_DEF(VE_ASSERT_THAT_REQUIRES_ARG,        "VE308", "assert_that requires at least 1 argument (condition)") \
    ERROR_DEF(VE_ASSERT_THAT_CONDITION_MUST_BE_BOOL, "VE309", "First argument to assert_that must be bool, got %s") \
    ERROR_DEF(VE_ASSERT_THAT_MESSAGE_MUST_BE_LITERAL, "VE310", "Second argument to assert_that (message) must be a string literal") \
    ERROR_DEF(VE_ASSERT_THAT_INVALID_FORMAT,      "VE311", "Invalid format string: unmatched braces") \
    ERROR_DEF(VE_ASSERT_THAT_PLACEHOLDER_MISMATCH, "VE312", "assert_that: format string has %d placeholder%s but only %d argument%s provided") \
    ERROR_DEF(VE_ASSERT_NOT_EQUALS_REQUIRES_TWO_ARGS, "VE314", "assert_not_equals requires exactly 2 arguments") \
    ERROR_DEF(VE_ASSERT_NOT_EQUALS_TYPE_MISMATCH, "VE315", "assert_not_equals arguments must have the same type") \
    ERROR_DEF(VE_ASSERT_NOT_EQUALS_NEEDS_EQ_TRAIT, "VE316", "Type '%s' does not implement the Eq trait, which is required for assert_not_equals")

#define JSASTA_WARNING_CATALOG \
    /* Type Warnings (TW000-TW099) */ \
    ERROR_DEF(TW042_UNREACHABLE_CODE,                "TW042", "Unreachable code detected") \
    \
    /* Validation Warnings (VW300-VW399) */ \
    ERROR_DEF(VW_FORMAT_EXTRA_ARGS,               "VW304", "%s: format string has %d placeholder%s but %d argument%s provided (extra arguments will be ignored)") \
    ERROR_DEF(VW_ASSERT_THAT_EXTRA_ARGS,          "VW313", "assert_that: format string has %d placeholder%s but %d argument%s provided (extra arguments will be ignored)")

// ============================================================================
// GENERATED ENUMS AND CONSTANTS
// ============================================================================

// Generate enum for all error codes
typedef enum {
#define ERROR_DEF(name, code, msg) name,
    JSASTA_ERROR_CATALOG
    JSASTA_WARNING_CATALOG
#undef ERROR_DEF
    JSASTA_ERROR_COUNT
} JSastaErrorCode;

// ============================================================================
// LOOKUP TABLE (Static, inline in header)
// ============================================================================

typedef struct {
    const char* long_name;
    const char* code;
    const char* template;
} JSastaErrorInfo;

static const JSastaErrorInfo jsasta_error_table[] = {
#define ERROR_DEF(name, code, msg) { #name, code, msg },
    JSASTA_ERROR_CATALOG
    JSASTA_WARNING_CATALOG
#undef ERROR_DEF
};

// ============================================================================
// LOOKUP MACROS - Direct array access using enum as index (O(1))
// ============================================================================

#define JSASTA_GET_CODE(err)     (jsasta_error_table[err].code)
#define JSASTA_GET_TEMPLATE(err) (jsasta_error_table[err].template)
#define JSASTA_GET_INFO(err)     (&jsasta_error_table[err])

// Lookup by string code (requires linear search) - O(n)
static inline const JSastaErrorInfo* jsasta_get_by_code(const char* code) {
    for (int i = 0; i < JSASTA_ERROR_COUNT; i++) {
        if (strcmp(jsasta_error_table[i].code, code) == 0) {
            return &jsasta_error_table[i];
        }
    }
    return NULL;
}

#endif // JSASTA_ERRORS_H
