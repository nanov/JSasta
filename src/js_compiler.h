#ifndef JS_COMPILER_H
#define JS_COMPILER_H

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdbool.h>

// Token types for lexer
typedef enum {
    TOKEN_EOF,
    TOKEN_VAR,
    TOKEN_LET,
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
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_ASSIGN,
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
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
} TokenType;

typedef struct {
    TokenType type;
    char* value;
    int line;
    int column;
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
    AST_MEMBER_ACCESS,
} ASTNodeType;

// Type system for specialization
typedef enum {
    TYPE_UNKNOWN,
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_VOID,
} ValueType;

// Forward declarations for specialization
typedef struct SpecializationContext SpecializationContext;
typedef struct FunctionSpecialization FunctionSpecialization;
typedef struct ASTNode ASTNode;

// Function specialization for polymorphism
struct FunctionSpecialization {
    char* function_name;           // Original function name
    char* specialized_name;        // Specialized name (e.g., "add_int_int")
    ValueType* param_types;        // Parameter types for this specialization
    int param_count;
    ValueType return_type;         // Return type for this specialization
    ASTNode* specialized_body;     // Cloned and type-analyzed AST for this specialization
    struct FunctionSpecialization* next;  // Linked list
};

// Specialization context - tracks all function specializations
struct SpecializationContext {
    FunctionSpecialization* specializations;
};


struct ASTNode {
    ASTNodeType type;
    ValueType value_type;
    SpecializationContext* specialization_ctx;  // For AST_PROGRAM, stores specializations
    union {
        struct {
            ASTNode** statements;
            int count;
        } program;

        struct {
            char* name;
            ASTNode* init;
            bool is_const;
        } var_decl;

        struct {
            char* name;
            char** params;
            int param_count;
            ASTNode* body;
            ValueType* param_types;
            ValueType return_type;
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
            ASTNode* object;
            char* property;
        } member_access;
    };
};

// Lexer
typedef struct {
    const char* source;
    int position;
    int line;
    int column;
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
} Parser;

Parser* parser_create(const char* source);
void parser_free(Parser* parser);
ASTNode* parser_parse(Parser* parser);


ASTNode* ast_create(ASTNodeType type);
void ast_free(ASTNode* node);
ASTNode* ast_clone(ASTNode* node);

// Symbol table for type inference
typedef struct SymbolEntry {
    char* name;
    ValueType type;
    LLVMValueRef value;
    struct SymbolEntry* next;
} SymbolEntry;

typedef struct SymbolTable {
    SymbolEntry* head;
    struct SymbolTable* parent;
} SymbolTable;

SymbolTable* symbol_table_create(SymbolTable* parent);
void symbol_table_free(SymbolTable* table);
void symbol_table_insert(SymbolTable* table, const char* name, ValueType type, LLVMValueRef value);
SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name);

// Type analysis
void type_analyze(ASTNode* node, SymbolTable* symbols);

// Type inference (separate pass before type checking)
void type_inference(ASTNode* ast, SymbolTable* symbols);

// Specialization context API
SpecializationContext* specialization_context_create();
void specialization_context_free(SpecializationContext* ctx);
void specialization_context_add(SpecializationContext* ctx, const char* func_name,
                                ValueType* param_types, int param_count);
FunctionSpecialization* specialization_context_find(SpecializationContext* ctx,
                                                    const char* func_name,
                                                    ValueType* param_types,
                                                    int param_count);
FunctionSpecialization* specialization_context_get_all(SpecializationContext* ctx,
                                                       const char* func_name);
void specialization_context_print(SpecializationContext* ctx);

void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node);

// Forward declaration
typedef struct CodeGen CodeGen;

// Code generator
typedef struct RuntimeFunction {
    char* name;
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
void codegen_register_runtime_function(CodeGen* gen, const char* name,
                                       LLVMValueRef (*handler)(CodeGen*, ASTNode*));
LLVMValueRef codegen_call_runtime_function(CodeGen* gen, const char* name, ASTNode* call_node);

// Runtime library
void runtime_init(CodeGen* gen);

// Utility functions
char* read_file(const char* filename);
void compile_file(const char* input_file, const char* output_file);


FunctionSpecialization* s;


#endif // JS_COMPILER_H
