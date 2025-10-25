#ifndef JASTA_COMPILER_H
#define JASTA_COMPILER_H

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>
#include "logger.h"

// Token types for lexer
typedef enum {
    TOKEN_EOF,
    TOKEN_VAR,
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_FUNCTION,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_PERCENT,
    TOKEN_PLUSPLUS,
    TOKEN_MINUSMINUS,
    TOKEN_RIGHT_SHIFT,
    TOKEN_LEFT_SHIFT,
    TOKEN_BIT_AND,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_ASSIGN,
    TOKEN_PLUS_ASSIGN,   // +=
    TOKEN_MINUS_ASSIGN,  // -=
    TOKEN_STAR_ASSIGN,   // *=
    TOKEN_SLASH_ASSIGN,  // /=
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_QUESTION,
    TOKEN_COLON,
} TokenType;

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
    AST_FUNCTION_DECL,
    AST_RETURN,
    AST_IF,
    AST_FOR,
    AST_WHILE,
    AST_EXPR_STMT,
    AST_BLOCK,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_CALL,
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
} ASTNodeType;

// Type system for specialization
typedef enum {
    TYPE_UNKNOWN,
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_FUNCTION,  // Function pointer/reference
    TYPE_ARRAY_INT,
    TYPE_ARRAY_DOUBLE,
    TYPE_ARRAY_STRING,
    TYPE_OBJECT,    // Object with properties
} ValueType;

// Forward declare TypeInfo for recursive types
typedef struct TypeInfo TypeInfo;

// Type metadata for objects - stores structure information
struct TypeInfo {
    ValueType base_type;        // TYPE_OBJECT, TYPE_ARRAY_INT, etc.

    // For objects: property metadata
    char** property_names;      // Property names
    TypeInfo** property_types;  // Property type info (allows nested objects)
    int property_count;         // Number of properties

    // For future: user-defined type name
    char* type_name;            // e.g., "Person", "Rectangle", etc.
};

// Forward declarations for specialization
typedef struct SpecializationContext SpecializationContext;
typedef struct FunctionSpecialization FunctionSpecialization;
typedef struct ASTNode ASTNode;

// Function specialization for polymorphism
struct FunctionSpecialization {
    char* function_name;           // Original function name
    char* specialized_name;        // Specialized name (e.g., "add_int_int")
    ValueType* param_types;        // Parameter types for this specialization
    TypeInfo** param_type_info;    // TypeInfo for object parameters (NULL for non-objects)
    int param_count;
    ValueType return_type;         // Return type for this specialization
    ASTNode* specialized_body;     // Cloned and type-analyzed AST for this specialization
    struct FunctionSpecialization* next;  // Linked list
};

// Specialization context - tracks all function specializations
struct SpecializationContext {
    FunctionSpecialization* specializations;
    size_t functions_processed;
};


struct ASTNode {
    ASTNodeType type;
    ValueType value_type;
    SpecializationContext* specialization_ctx;  // For AST_PROGRAM, stores specializations

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
        } var_decl;

        struct {
            char* name;
            char** params;
            int param_count;
            ASTNode* body;
            ValueType* param_types;
            ValueType return_type;
            TypeInfo** param_type_hints;  // Optional type annotations for params (NULL if not specified, supports objects)
            TypeInfo* return_type_hint;   // Optional return type annotation (NULL if not specified, supports objects)
        } func_decl;

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
        } assignment;

        struct {
            char* name;
            char* op;      // "+=", "-=", "*=", "/="
            ASTNode* value;
        } compound_assignment;

        struct {
            ASTNode* object;
            char* property;
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
        } index_access;

        struct {
            ASTNode** elements;
            int count;
        } array_literal;

        struct {
            ASTNode* object;
            ASTNode* index;
            ASTNode* value;
        } index_assignment;

        struct {
            char* op;      // "++" or "--"
            char* name;    // variable name
        } prefix_op;

        struct {
            char* op;      // "++" or "--"
            char* name;    // variable name
        } postfix_op;

        struct {
            char** keys;      // Property names
            ASTNode** values; // Property values
            int count;        // Number of properties
            TypeInfo* type_info; // Type metadata (assigned during type inference)
        } object_literal;
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

// Parser
typedef struct {
    Lexer* lexer;
    Token* current_token;
    const char* filename;
} Parser;

Parser* parser_create(const char* source, const char* filename);
void parser_free(Parser* parser);
ASTNode* parser_parse(Parser* parser);


ASTNode* ast_create(ASTNodeType type);
ASTNode* ast_create_with_loc(ASTNodeType type, SourceLocation loc);
void ast_free(ASTNode* node);
ASTNode* ast_clone(ASTNode* node);

// Symbol table for type inference
typedef struct SymbolEntry {
    char* name;
    ValueType type;
    bool is_const;
    LLVMValueRef value;
    ASTNode* node;
    LLVMTypeRef llvm_type;  // For objects, stores the struct type
    TypeInfo* type_info;     // For objects and complex types, stores metadata
    struct SymbolEntry* next;
} SymbolEntry;

typedef struct SymbolTable {
    SymbolEntry* head;
    struct SymbolTable* parent;
} SymbolTable;

SymbolTable* symbol_table_create(SymbolTable* parent);
void symbol_table_free(SymbolTable* table);
void symbol_table_insert(SymbolTable* table, const char* name, ValueType type, LLVMValueRef value, bool is_const);
void symbol_table_insert_var_declaration(SymbolTable* table, const char* name, ValueType type, bool is_const, ASTNode* var_decl_node);
void symbol_table_insert_func_declaration(SymbolTable* table, const char* name, ASTNode* node);
SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name);

// TypeInfo management
TypeInfo* type_info_create(ValueType base_type);
TypeInfo* type_info_create_from_object_literal(ASTNode* obj_literal);
void type_info_free(TypeInfo* type_info);
TypeInfo* type_info_clone(TypeInfo* type_info);
int type_info_find_property(TypeInfo* type_info, const char* property_name);

// Type analysis
void type_analyze(ASTNode* node, SymbolTable* symbols);

// Type inference (separate pass before type checking)
void type_inference(ASTNode* ast, SymbolTable* symbols);

// Specialization context API
SpecializationContext* specialization_context_create();
void specialization_context_free(SpecializationContext* ctx);
FunctionSpecialization* specialization_context_add(SpecializationContext* ctx, const char* func_name,
                                ValueType* param_types, int param_count);
FunctionSpecialization* specialization_context_find(SpecializationContext* ctx,
                                                    const char* func_name,
                                                    ValueType* param_types,
                                                    int param_count);
FunctionSpecialization* specialization_context_get_all(SpecializationContext* ctx,
                                                       const char* func_name);
void specialization_context_print(SpecializationContext* ctx);

// void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node);

// Forward declaration
typedef struct CodeGen CodeGen;

// Code generator
typedef struct RuntimeFunction {
    char* name;
    ValueType return_type;  // Return type of the function
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
    SpecializationContext* specialization_ctx;  // For polymorphic functions
} CodeGen;

CodeGen* codegen_create(const char* module_name);
void codegen_free(CodeGen* gen);
void codegen_generate(CodeGen* gen, ASTNode* ast);
void codegen_emit_llvm_ir(CodeGen* gen, const char* filename);
LLVMValueRef codegen_node(CodeGen* gen, ASTNode* node);

// Runtime function registration
void codegen_register_runtime_function(CodeGen* gen, const char* name, ValueType return_type,
                                       LLVMValueRef (*handler)(CodeGen*, ASTNode*));
ValueType codegen_get_runtime_function_type(CodeGen* gen, const char* name);
LLVMValueRef codegen_call_runtime_function(CodeGen* gen, const char* name, ASTNode* call_node);

// Runtime library
void runtime_init(CodeGen* gen);

// Runtime function type lookup (for type inference)
ValueType runtime_get_function_type(const char* name);

// Utility functions
char* read_file(const char* filename);
void compile_file(const char* input_file, const char* output_file);


FunctionSpecialization* s;
ASTNode* c_n;


#endif // JS_COMPILER_H
