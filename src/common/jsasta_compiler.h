#ifndef JASTA_COMPILER_H
#define JASTA_COMPILER_H

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/DebugInfo.h>
#include <stdbool.h>
#include "logger.h"

// Token types for lexer
// Use X-Macro pattern to define tokens with automatic string generation
#define TOKEN_TYPES(X) \
    X(TOKEN_EOF, "EOF") \
    X(TOKEN_VAR, "var") \
    X(TOKEN_LET, "let") \
    X(TOKEN_CONST, "const") \
    X(TOKEN_FUNCTION, "function") \
    X(TOKEN_EXTERNAL, "external") \
    X(TOKEN_IMPORT, "import") \
    X(TOKEN_EXPORT, "export") \
    X(TOKEN_FROM, "from") \
    X(TOKEN_STRUCT, "struct") \
    X(TOKEN_REF, "ref") \
    X(TOKEN_RETURN, "return") \
    X(TOKEN_BREAK, "break") \
    X(TOKEN_CONTINUE, "continue") \
    X(TOKEN_IF, "if") \
    X(TOKEN_ELSE, "else") \
    X(TOKEN_FOR, "for") \
    X(TOKEN_WHILE, "while") \
    X(TOKEN_NEW, "new") \
    X(TOKEN_DELETE, "delete") \
    X(TOKEN_TRUE, "true") \
    X(TOKEN_FALSE, "false") \
    X(TOKEN_I8, "i8") \
    X(TOKEN_I16, "i16") \
    X(TOKEN_I32, "i32") \
    X(TOKEN_I64, "i64") \
    X(TOKEN_U8, "u8") \
    X(TOKEN_U16, "u16") \
    X(TOKEN_U32, "u32") \
    X(TOKEN_U64, "u64") \
    X(TOKEN_INT, "int") \
    X(TOKEN_IDENTIFIER, "identifier") \
    X(TOKEN_NUMBER, "number") \
    X(TOKEN_STRING, "string") \
    X(TOKEN_PLUS, "+") \
    X(TOKEN_MINUS, "-") \
    X(TOKEN_PERCENT, "%") \
    X(TOKEN_PLUSPLUS, "++") \
    X(TOKEN_MINUSMINUS, "--") \
    X(TOKEN_RIGHT_SHIFT, ">>") \
    X(TOKEN_LEFT_SHIFT, "<<") \
    X(TOKEN_BIT_AND, "&") \
    X(TOKEN_BIT_OR, "|") \
    X(TOKEN_BIT_XOR, "^") \
    X(TOKEN_STAR, "*") \
    X(TOKEN_SLASH, "/") \
    X(TOKEN_ASSIGN, "=") \
    X(TOKEN_PLUS_ASSIGN, "+=") \
    X(TOKEN_MINUS_ASSIGN, "-=") \
    X(TOKEN_STAR_ASSIGN, "*=") \
    X(TOKEN_SLASH_ASSIGN, "/=") \
    X(TOKEN_EQ, "==") \
    X(TOKEN_NE, "!=") \
    X(TOKEN_LT, "<") \
    X(TOKEN_GT, ">") \
    X(TOKEN_LE, "<=") \
    X(TOKEN_GE, ">=") \
    X(TOKEN_LPAREN, "(") \
    X(TOKEN_RPAREN, ")") \
    X(TOKEN_LBRACE, "{") \
    X(TOKEN_RBRACE, "}") \
    X(TOKEN_LBRACKET, "[") \
    X(TOKEN_RBRACKET, "]") \
    X(TOKEN_SEMICOLON, ";") \
    X(TOKEN_COMMA, ",") \
    X(TOKEN_DOT, ".") \
    X(TOKEN_AT, "@") \
    X(TOKEN_ELLIPSIS, "...") \
    X(TOKEN_AND, "&&") \
    X(TOKEN_OR, "||") \
    X(TOKEN_NOT, "!") \
    X(TOKEN_QUESTION, "?") \
    X(TOKEN_COLON, ":")

// Generate enum
#define TOKEN_ENUM(name, str) name,
typedef enum {
    TOKEN_TYPES(TOKEN_ENUM)
} TokenType;
#undef TOKEN_ENUM

// Function to get token string representation
const char* token_type_to_string(TokenType type);

typedef struct {
    TokenType type;
    char* value;
    size_t line;
    size_t column;
} Token;


// AST Node types
typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_FUNCTION_DECL,  // Used for both user and external functions
    AST_STRUCT_DECL,    // Struct type definition
    AST_IMPORT_DECL,    // Import statement
    AST_EXPORT_DECL,    // Export statement
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_IF,
    AST_FOR,
    AST_WHILE,
    AST_EXPR_STMT,
    AST_BLOCK,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_CALL,
    AST_METHOD_CALL,    // Method call: obj.method(...) or Type.method(...)
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_STRING,
    AST_BOOLEAN,
    AST_ASSIGNMENT,
    AST_COMPOUND_ASSIGNMENT,
    AST_MEMBER_ACCESS,
    AST_MEMBER_ASSIGNMENT,  // obj.prop = value
    AST_TERNARY,
    AST_INDEX_ACCESS,
    AST_ARRAY_LITERAL,
    AST_INDEX_ASSIGNMENT,
    AST_PREFIX_OP,      // ++i, --i
    AST_POSTFIX_OP,     // i++, i--
    AST_OBJECT_LITERAL, // Object literal { key: value, ... }
    AST_NEW_EXPR,       // new T[size] - heap allocation
    AST_DELETE_EXPR,    // delete expr - heap deallocation
} ASTNodeType;

// Forward declare TypeInfo for recursive types
typedef struct TypeInfo TypeInfo;
typedef struct FunctionSpecialization FunctionSpecialization;
typedef struct ASTNode ASTNode;
typedef struct SymbolTable SymbolTable;
typedef struct SymbolEntry SymbolEntry;
typedef struct DiagnosticContext DiagnosticContext;

// Builtin function callbacks
// Validation callback: called during type inference to validate call arguments
// Returns true if validation passed, false otherwise
typedef bool (*BuiltinValidateCallback)(ASTNode* call_node, DiagnosticContext* diag);

// Codegen callback: called during code generation to emit custom LLVM IR
// Returns the LLVM value representing the result of the call
// context is a void* to CodeGen (to keep common/compiler layers separate)
typedef LLVMValueRef (*BuiltinCodegenCallback)(void* context, ASTNode* call_node);


// Type kind for categorizing types
typedef enum {
    TYPE_KIND_PRIMITIVE,    // int, double, string, bool, void
    TYPE_KIND_OBJECT,       // User-defined object types
    TYPE_KIND_ARRAY,        // Array types
    TYPE_KIND_FUNCTION,     // Function types
    TYPE_KIND_REF,          // Reference types (pointers with mutable flag)
    TYPE_KIND_ALIAS,        // Type alias (e.g., usize -> u64)
    TYPE_KIND_UNKNOWN       // Unknown/unresolved types
} TypeKind;

// Type metadata - stores structure information
struct TypeInfo {
    TypeKind kind;              // Type category
    int type_id;                // Unique type ID within TypeContext
    char* type_name;            // Type name (e.g., "Person", "Object_0", "int[]")
    bool is_global;

    // Type-specific data (use union to save memory and improve organization)
    union {
        // For TYPE_KIND_PRIMITIVE: integer metadata
        struct {
            int bit_width;      // 8, 16, 32, 64 (for integer types)
            bool is_signed;     // true for i8-i64, false for u8-u64 (future)
        } integer;

        // For TYPE_KIND_OBJECT: property metadata
        struct {
            char** property_names;      // Property names
            TypeInfo** property_types;  // Property types (allows nested objects)
            int property_count;         // Number of properties
            ASTNode* struct_decl_node;  // Reference to struct declaration (for default values)
        } object;

        // For TYPE_KIND_ARRAY: element type
        struct {
            TypeInfo* element_type;     // Type of array elements
        } array;

        // For TYPE_KIND_FUNCTION: function signature and specializations
        struct {
            TypeInfo** param_types;              // Generic parameter types (may be NULL for untyped)
            TypeInfo* return_type;               // Return type
            int param_count;                     // Number of parameters
            bool is_variadic;                    // True if function accepts variable arguments (...)
            bool is_fully_typed;                 // True if all params and return type are known (cached)
            FunctionSpecialization* specializations;  // Linked list of specializations
            ASTNode* original_body;              // Original AST body (for cloning during specialization)
            ASTNode* func_decl_node;             // Function declaration node (for function variables)
        } function;

        // For TYPE_KIND_REF: reference/pointer type
        struct {
            TypeInfo* target_type;   // Type being referenced
            bool is_mutable;         // True if reference is mutable (default)
        } ref;

        // For TYPE_KIND_ALIAS: points to the actual type
        struct {
            TypeInfo* target_type;  // The type this alias resolves to
        } alias;
    } data;
};

// TypeEntry for linked list of registered types
typedef struct TypeEntry {
    TypeInfo* type;
    LLVMTypeRef llvm_type;  // Pre-generated LLVM type (for objects)
    struct TypeEntry* next;
} TypeEntry;

// TypeAlias for type aliasing (e.g., type usize = u64)
typedef struct TypeAlias {
    char* alias_name;          // The alias name (e.g., "usize")
    TypeInfo* target_type;     // The actual type it resolves to (e.g., Type_U64)
    struct TypeAlias* next;    // Linked list
} TypeAlias;

// Forward declare TraitRegistry
typedef struct TraitRegistry TraitRegistry;

// TypeContext manages all type information
typedef struct TypeContext {
    TypeEntry* type_table;           // Linked list of all registered types
    int type_count;                  // Number of registered types
    int next_anonymous_id;           // For generating unique anonymous type names
    int specialization_count;        // Total number of specializations across all functions

    char* module_prefix;             // Module prefix for name mangling (e.g., "math__")

    // Trait system for operator overloading and methods
    TraitRegistry* trait_registry;

    // Type aliases (e.g., usize, nint, uint)
    TypeAlias* type_aliases;  // Linked list of type aliases

    // Note: Function specializations are now stored in TypeInfo.data.function.specializations
} TypeContext;

// Function specialization for polymorphism
// Note: Specializations are now stored in TypeInfo.data.function.specializations
// Note: Parameter names are retrieved from the parent TypeInfo's func_decl_node during codegen
struct FunctionSpecialization {
    char* specialized_name;        // Specialized name (e.g., "add_int_int")
    TypeInfo** param_type_info;    // Concrete parameter types for this specialization
    int param_count;
    TypeInfo* return_type_info;    // Return type as TypeInfo
    ASTNode* specialized_body;     // Cloned and type-analyzed function body (just the body, not the whole function)
    struct FunctionSpecialization* next;  // Linked list
};

inline static bool function_specialization_is_external(FunctionSpecialization* s) {
	return !s->specialized_body;
}


struct ASTNode {
    ASTNodeType type;
    TypeInfo* type_info;           // Unified type representation
    TypeContext* type_ctx;         // For AST_PROGRAM, stores types and specializations
    SymbolTable* symbol_table;     // For AST_PROGRAM and AST_BLOCK, stores the scope's symbol table

    // Source location information
    SourceLocation loc;

    union {
        struct {
            ASTNode** statements;
            int count;
        } program;

        struct {
            char* name;
            ASTNode* init;
            bool is_const;
            TypeInfo* type_hint;  // Optional type annotation (NULL if not specified, supports objects)
            int array_size;       // For array declarations (e.g., i32[10]), 0 if not an array
            ASTNode* array_size_expr;  // Expression for array size (e.g., identifier or const expr)
            SymbolEntry* symbol_entry;  // Pointer to the symbol table entry for this variable (set during type inference)
        } var_decl;

        struct {
            char* name;
            char** params;
            SourceLocation* param_locs;   // Source locations for each parameter
            int param_count;
            ASTNode* body;                // NULL for external functions
            TypeInfo** param_type_hints;  // Optional for user functions, required for external
            TypeInfo* return_type_hint;   // Optional for user functions, required for external
            bool is_variadic;             // Variable arguments support (... in parameter list)
            
            // Builtin function callbacks (NULL for regular user functions)
            BuiltinValidateCallback validate_callback;  // Custom validation logic
            BuiltinCodegenCallback codegen_callback;    // Custom codegen logic
        } func_decl;

        struct {
            char* name;                   // Struct name
            char** property_names;        // Property names
            SourceLocation* property_locs;  // Source locations for each property
            TypeInfo** property_types;    // Property types
            ASTNode** default_values;     // Default literal values (NULL if no default)
            int* property_array_sizes;    // Array size for each property (0 if not array)
            ASTNode** property_array_size_exprs;  // Expression for array size (for const evaluation)
            int property_count;
            ASTNode** methods;            // Method function declarations (AST_FUNC_DECL nodes)
            int method_count;
        } struct_decl;

        struct {
            char* module_path;       // Path to module file: "./math/lib.jsa"
            char* namespace_name;    // Namespace identifier: "math"
            void* imported_module;   // Pointer to loaded Module (set during module loading)
            char* module_prefix;     // Prefix for name mangling: "math__"
        } import_decl;

        struct {
            ASTNode* declaration;    // The declaration being exported (function, const, struct)
        } export_decl;

        struct {
            ASTNode* value;
        } return_stmt;

        struct {
            ASTNode* condition;
            ASTNode* then_branch;
            ASTNode* else_branch;
        } if_stmt;

        struct {
            ASTNode* init;
            ASTNode* condition;
            ASTNode* update;
            ASTNode* body;
        } for_stmt;

        struct {
            ASTNode* condition;
            ASTNode* body;
        } while_stmt;

        struct {
            ASTNode* expression;
        } expr_stmt;

        struct {
            ASTNode** statements;
            int count;
        } block;

        struct {
            char* op;
            ASTNode* left;
            ASTNode* right;
        } binary_op;

        struct {
            char* op;
            ASTNode* operand;
        } unary_op;

        struct {
            ASTNode* callee;
            ASTNode** args;
            int arg_count;
        } call;

        struct {
            ASTNode* object;        // The object or type (identifier) being called on
            char* method_name;      // The method name
            ASTNode** args;         // Method arguments
            int arg_count;
            bool is_static;         // true if Type.method(), false if obj.method()
        } method_call;

        struct {
            char* name;
        } identifier;

        struct {
            double value;
        } number;

        struct {
            char* value;
        } string;

        struct {
            bool value;
        } boolean;

        struct {
            char* name;
            ASTNode* value;
            SymbolEntry* symbol_entry;  // Pointer to the symbol table entry for the variable being assigned
        } assignment;

        struct {
            char* name;           // For simple identifier (can be NULL)
            ASTNode* target;      // For member/index access (can be NULL)
            char* op;             // "+=", "-=", "*=", "/="
            ASTNode* value;
        } compound_assignment;

        struct {
            ASTNode* object;
            char* property;
            SourceLocation property_loc;  // Location of the property name (for go-to-definition)
            SymbolEntry* symbol_entry;  // Symbol entry if object is identifier (resolved during type inference)
            int property_index;         // Index of property in struct (-1 if not applicable, resolved during type inference)
        } member_access;

        struct {
            ASTNode* object;
            char* property;
            ASTNode* value;
        } member_assignment;

        struct {
            ASTNode* condition;
            ASTNode* true_expr;
            ASTNode* false_expr;
        } ternary;

        struct {
            ASTNode* object;
            ASTNode* index;
            struct TraitImpl* trait_impl;  // Index trait implementation (resolved during type inference)
            SymbolEntry* symbol_entry;     // Symbol entry if object is identifier (resolved during type inference)
        } index_access;

        struct {
            ASTNode** elements;
            int count;
        } array_literal;

        struct {
            ASTNode* object;
            ASTNode* index;
            ASTNode* value;
            struct TraitImpl* trait_impl;  // RefIndex trait implementation (resolved during type inference)
            SymbolEntry* symbol_entry;     // Symbol entry if object is identifier (resolved during type inference)
        } index_assignment;

        struct {
            char* op;           // "++" or "--"
            char* name;         // variable name (can be NULL)
            ASTNode* target;    // For member/index access (can be NULL)
        } prefix_op;

        struct {
            char* op;           // "++" or "--"
            char* name;         // variable name (can be NULL)
            ASTNode* target;    // For member/index access (can be NULL)
        } postfix_op;

        struct {
            char** keys;      // Property names
            ASTNode** values; // Property values
            int count;        // Number of properties
        } object_literal;

        struct {
            TypeInfo* element_type;  // Type of array elements (e.g., int)
            ASTNode* size_expr;      // Size expression (e.g., 12 for new int[12])
        } new_expr;

        struct {
            ASTNode* operand;        // Expression to delete (must be ref type)
        } delete_expr;
    };
};

// Lexer
typedef struct {
    const char* source;
    size_t position;
    size_t line;
    size_t column;
    char current;
} Lexer;

Lexer* lexer_create(const char* source);
void lexer_free(Lexer* lexer);
Token* lexer_next_token(Lexer* lexer);
void token_free(Token* token);

// Forward declare DiagnosticContext (defined in diagnostics.h)
struct DiagnosticContext;

// Parser
typedef struct {
    Lexer* lexer;
    Token* current_token;
    const char* filename;
    TypeContext* type_ctx;  // For structural type sharing of objects
    struct DiagnosticContext* diagnostics;  // For collecting parse errors
} Parser;

Parser* parser_create(const char* source, const char* filename, TypeContext* type_ctx, struct DiagnosticContext* diagnostics);
void parser_free(Parser* parser);
ASTNode* parser_parse(Parser* parser);


ASTNode* ast_create(ASTNodeType type);
ASTNode* ast_create_with_loc(ASTNodeType type, SourceLocation loc);
void ast_free(ASTNode* node);
ASTNode* ast_clone(ASTNode* node);

// Symbol table for type inference
typedef struct SymbolEntry {
    char* name;
    bool is_const;
    LLVMValueRef value;
    ASTNode* node;           // For regular symbols: the declaration node
                             // For namespace symbols: the AST_IMPORT_DECL node
                             // For function parameters: the function declaration node
    LLVMTypeRef llvm_type;   // For objects, stores the struct type
    TypeInfo* type_info;     // For objects and complex types, stores metadata
    int array_size;          // For arrays, stores the size (0 if not an array)
    int param_index;         // For function parameters: the parameter index (-1 for non-parameters)
    struct SymbolEntry* next;
} SymbolEntry;

typedef struct SymbolTable {
    SymbolEntry* head;
    struct SymbolTable* parent;
} SymbolTable;

SymbolTable* symbol_table_create(SymbolTable* parent);
void symbol_table_free(SymbolTable* table);
void symbol_table_insert(SymbolTable* table, const char* name, TypeInfo* type_info, LLVMValueRef value, bool is_const);
void symbol_table_insert_var_declaration(SymbolTable* table, const char* name, TypeInfo* type_info, bool is_const, ASTNode* var_decl_node);
void symbol_table_insert_func_declaration(SymbolTable* table, const char* name, ASTNode* node);
void symbol_table_insert_namespace(SymbolTable* table, const char* name, ASTNode* import_node);
SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name);
SymbolEntry* symbol_table_lookup_all_scopes(SymbolTable* table, const char* name);

// TypeInfo management
TypeInfo* type_info_create(TypeKind kind, char* name);
TypeInfo* type_info_create_primitive(char* name);
TypeInfo* type_info_create_integer(char* name, int bit_width, bool is_signed);
TypeInfo* type_info_create_array(TypeInfo* element_type);
TypeInfo* type_info_create_unknown();
TypeInfo* type_info_create_int();
TypeInfo* type_info_create_double();
TypeInfo* type_info_create_bool();
TypeInfo* type_info_create_string();
TypeInfo* type_info_create_void();
TypeInfo* type_info_create_from_object_literal(ASTNode* obj_literal);
TypeInfo* type_info_create_alias(char* alias_name, TypeInfo* target_type);
void type_info_free_shallow(TypeInfo* type_info);  // Shallow free (doesn't free referenced types)
void type_info_free(TypeInfo* type_info);           // Deep free (frees nested types)
TypeInfo* type_info_clone(TypeInfo* type_info);
TypeInfo* type_info_resolve_alias(TypeInfo* type_info);  // Recursively resolve aliases
int type_info_find_property(TypeInfo* type_info, const char* property_name);

// Global type variables
TypeInfo* Type_Unknown;
TypeInfo* Type_Bool;
TypeInfo* Type_Void;

// Signed integer types
TypeInfo* Type_I8;
TypeInfo* Type_I16;
TypeInfo* Type_I32;
TypeInfo* Type_I64;

// Unsigned integer types
TypeInfo* Type_U8;
TypeInfo* Type_U16;
TypeInfo* Type_U32;
TypeInfo* Type_U64;

// Legacy integer type (alias for i32)
TypeInfo* Type_Int;

// Platform-specific type aliases
TypeInfo* Type_Usize;  // Alias for platform size_t (u32 or u64 depending on platform)
TypeInfo* Type_Nint;   // Alias for platform int (i32 or i64 depending on platform)
TypeInfo* Type_Uint;   // Alias for platform unsigned int (u32 or u64 depending on platform)

// Other primitive types
TypeInfo* Type_Double;
TypeInfo* Type_Object;
TypeInfo* Type_String;

// Array types
TypeInfo* Type_Array_Int;    // Legacy, will use Type_Array_I32
TypeInfo* Type_Array_I8;
TypeInfo* Type_Array_I16;
TypeInfo* Type_Array_I32;
TypeInfo* Type_Array_I64;
TypeInfo* Type_Array_U8;
TypeInfo* Type_Array_U16;
TypeInfo* Type_Array_U32;
TypeInfo* Type_Array_U64;
TypeInfo* Type_Array_Bool;
TypeInfo* Type_Array_Double;
TypeInfo* Type_Array_String;

static inline bool type_info_is_unknown(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info && type_info->kind == TYPE_KIND_UNKNOWN;
}

// Check if type is any integer type (signed or unsigned)
static inline bool type_info_is_integer(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    if (!type_info || type_info->kind != TYPE_KIND_PRIMITIVE) return false;
    return type_info == Type_I8 || type_info == Type_I16 ||
           type_info == Type_I32 || type_info == Type_I64 ||
           type_info == Type_U8 || type_info == Type_U16 ||
           type_info == Type_U32 || type_info == Type_U64;
}

// Check if type is signed integer
static inline bool type_info_is_signed_int(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    if (!type_info || type_info->kind != TYPE_KIND_PRIMITIVE) return false;
    return type_info == Type_I8 || type_info == Type_I16 ||
           type_info == Type_I32 || type_info == Type_I64;
}

// Check if type is unsigned integer
static inline bool type_info_is_unsigned_int(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    if (!type_info || type_info->kind != TYPE_KIND_PRIMITIVE) return false;
    return type_info == Type_U8 || type_info == Type_U16 ||
           type_info == Type_U32 || type_info == Type_U64;
}

// Get integer bit width (returns 0 for non-integer types)
static inline int type_info_get_int_width(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    if (!type_info || type_info->kind != TYPE_KIND_PRIMITIVE) return 0;
    if (!type_info_is_integer(type_info)) return 0;
    return type_info->data.integer.bit_width;
}

// Legacy: check if type is i32 or the old "int"
static inline bool type_info_is_int(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_I32 || type_info == Type_Int;
}

static inline bool type_info_is_double_ctx(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_Double;
}

static inline bool type_info_is_double(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_Double;
}

static inline bool type_info_is_string_ctx(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_String;
}

static inline bool type_info_is_string(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_String;
}

static inline bool type_info_is_bool_ctx(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_Bool;
}

static inline bool type_info_is_bool(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_Bool;
}

static inline bool type_info_is_void(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_Void;
}

static inline bool type_info_is_object(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info && type_info->kind == TYPE_KIND_OBJECT;
}

static inline bool type_info_is_array(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info && type_info->kind == TYPE_KIND_ARRAY;
}

static inline bool type_info_is_ref(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info && type_info->kind == TYPE_KIND_REF;
}

// Get the underlying type from a ref type (like type_info_resolve_alias)
static inline TypeInfo* type_info_get_ref_target(TypeInfo* type_info) {
    if (type_info && type_info->kind == TYPE_KIND_REF) {
        return type_info->data.ref.target_type;
    }
    return type_info;
}

static inline bool type_info_is_function(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info && type_info->kind == TYPE_KIND_FUNCTION;
}

static inline bool type_info_is_function_ctx(TypeInfo* type_info) {
    type_info = type_info_resolve_alias(type_info);
    return type_info && type_info->kind == TYPE_KIND_FUNCTION;
}

// Helper to check if void with TypeContext (for compatibility)
static inline bool type_info_is_void_ctx(TypeInfo* type_info, TypeContext* ctx) {
    (void)ctx;  // unused for now
    type_info = type_info_resolve_alias(type_info);
    return type_info == Type_Void;
}

// Check if array has specific element type
static inline bool type_info_is_array_of(TypeInfo* array_type, TypeInfo* element_type) {
    array_type = type_info_resolve_alias(array_type);
    element_type = type_info_resolve_alias(element_type);
    return array_type && array_type->kind == TYPE_KIND_ARRAY &&
           array_type->data.array.element_type == element_type;
}

// TypeContext API - Unified type management
void type_system_init_global_types();  // Initialize global Type_* variables once at startup
TypeContext* type_context_create();
void type_context_free(TypeContext* ctx);
TypeInfo* type_context_register_type(TypeContext* ctx, TypeInfo* type);
TypeInfo* type_context_find_type(TypeContext* ctx, const char* type_name);
TypeInfo* type_context_create_object_type_from_literal(TypeContext* ctx, ASTNode* obj_literal);
TypeInfo* type_context_find_or_create_object_type(TypeContext* ctx, TypeInfo* obj_type);

// Primitive type accessors (return actual types with aliases resolved)
TypeInfo* type_context_get_int(TypeContext* ctx);
TypeInfo* type_context_get_double(TypeContext* ctx);
TypeInfo* type_context_get_string(TypeContext* ctx);
TypeInfo* type_context_get_bool(TypeContext* ctx);
TypeInfo* type_context_get_void(TypeContext* ctx);

// Reference type management
TypeInfo* type_context_get_or_create_ref_type(TypeContext* ctx, TypeInfo* target_type, bool is_mutable);

// Function type management
TypeInfo* type_context_create_function_type(TypeContext* ctx, const char* func_name,
                                            TypeInfo** param_types, int param_count,
                                            TypeInfo* return_type, ASTNode* original_body,
                                            bool is_variadic);
TypeInfo* type_context_find_function_type(TypeContext* ctx, const char* func_name);
TypeInfo* type_context_create_struct_type(TypeContext* ctx, const char* struct_name,
                                          char** property_names, TypeInfo** property_types,
                                          int property_count, ASTNode* struct_decl_node);
TypeInfo* type_context_find_struct_type(TypeContext* ctx, const char* struct_name);
FunctionSpecialization* type_context_add_specialization(TypeContext* ctx, TypeInfo* func_type,
                                                        TypeInfo** param_type_info, int param_count);
FunctionSpecialization* type_context_find_specialization(TypeContext* ctx, TypeInfo* func_type,
                                                         TypeInfo** param_type_info, int param_count);

// Type alias management
void type_context_register_alias(TypeContext* ctx, const char* alias_name, TypeInfo* target_type);
TypeInfo* type_context_resolve_alias(TypeContext* ctx, const char* alias_name);

// Type analysis
void type_analyze(ASTNode* node, SymbolTable* symbols);

// Type inference (separate pass before type checking)
void type_inference(ASTNode* ast, SymbolTable* symbols);
void type_inference_with_context(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx);
void type_inference_with_diagnostics(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx, struct DiagnosticContext* diag);

// Specialization API (now part of TypeContext)
// Note: specialization_context_create/free removed - use type_context_create/free
void specialization_context_print(TypeContext* ctx);

// TypeInfo-based specialization functions
FunctionSpecialization* specialization_context_add_by_type_info(TypeContext* ctx, const char* func_name,
                                TypeInfo** param_type_info, int param_count);
FunctionSpecialization* specialization_context_find_by_type_info(TypeContext* ctx,
                                                                  const char* func_name,
                                                                  TypeInfo** param_type_info,
                                                                  int param_count);

// void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node);

// Forward declaration
typedef struct CodeGen CodeGen;

// Code generator
typedef struct RuntimeFunction {
    char* name;
    TypeInfo* return_type;  // Return type of the function
    LLVMValueRef (*handler)(CodeGen*, ASTNode*);
    struct RuntimeFunction* next;
} RuntimeFunction;

typedef struct CodeGen {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMContextRef context;
    SymbolTable* symbols;
    LLVMValueRef current_function;
    RuntimeFunction* runtime_functions;
    TypeContext* type_ctx;                      // Type context for TypeInfo and specializations
    TraitRegistry* trait_registry;              // Trait registry (shared with type_ctx)

    // Loop control - for break/continue
    LLVMBasicBlockRef loop_exit_block;          // Block to jump to on 'break'
    LLVMBasicBlockRef loop_continue_block;      // Block to jump to on 'continue'

    // Stack allocation management
    LLVMBasicBlockRef entry_block;              // Entry block of current function for allocas

    // Debug information
    bool enable_debug_symbols;                  // Whether to generate debug symbols (DWARF)
    bool enable_debug;                          // Whether to enable debug mode (for debug.assert, etc.)
    const char* source_filename;                // Source file name for debug info
    LLVMDIBuilderRef di_builder;                // Debug info builder
    LLVMMetadataRef di_compile_unit;            // Compile unit for debug info
    LLVMMetadataRef di_file;                    // File metadata
    LLVMMetadataRef current_di_scope;           // Current debug scope (function or file)
} CodeGen;

CodeGen* codegen_create(const char* module_name);
void codegen_free(CodeGen* gen);
void codegen_generate(CodeGen* gen, ASTNode* ast, bool is_entry_module);
void codegen_emit_llvm_ir(CodeGen* gen, const char* filename);
LLVMValueRef codegen_node(CodeGen* gen, ASTNode* node);
LLVMTypeRef get_llvm_type(CodeGen* gen, TypeInfo* type_info);

// Runtime function registration
TypeInfo* codegen_get_runtime_function_type(CodeGen* gen, const char* name);
LLVMValueRef codegen_call_runtime_function(CodeGen* gen, const char* name, ASTNode* call_node);

// Runtime library
void runtime_init(CodeGen* gen);

// Runtime function type lookup (for type inference)
TypeInfo* runtime_get_function_type(const char* name);

// Utility functions
char* read_file(const char* filename);
int compile_file(const char* input_file, const char* output_file, bool enable_debug_symbols, bool enable_debug);


FunctionSpecialization* s;
ASTNode* c_n;


#endif // JS_COMPILER_H
