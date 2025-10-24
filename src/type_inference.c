#include "js_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// Forward declarations
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols);
static void infer_literal_types(ASTNode* node, SymbolTable* symbols);
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx);
static void create_specializations(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx);
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx);
static ValueType infer_function_return_type_with_params(ASTNode* body, SymbolTable* scope);

// Helper: Infer type from binary operation
static ValueType infer_binary_result_type(const char* op, ValueType left, ValueType right) {
    if (strcmp(op, "+") == 0) {
        if (left == TYPE_STRING || right == TYPE_STRING) return TYPE_STRING;
        if (left == TYPE_DOUBLE || right == TYPE_DOUBLE) return TYPE_DOUBLE;
        if (left == TYPE_INT && right == TYPE_INT) return TYPE_INT;
    }

    if (strcmp(op, "-") == 0 || strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
        if (left == TYPE_DOUBLE || right == TYPE_DOUBLE) return TYPE_DOUBLE;
        if (left == TYPE_INT && right == TYPE_INT) return TYPE_INT;
    }

    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        return TYPE_BOOL;
    }

    return TYPE_UNKNOWN;
}

// Helper: Infer function return type by walking body with typed parameters
static ValueType infer_function_return_type_with_params(ASTNode* node, SymbolTable* scope);

// Helper: Simple type inference for expressions (used during return type inference)
static ValueType infer_expr_type_simple(ASTNode* node, SymbolTable* scope) {
    if (!node) return TYPE_UNKNOWN;

    switch (node->type) {
        case AST_NUMBER:
            return node->value_type;
        case AST_STRING:
            return TYPE_STRING;
        case AST_BOOLEAN:
            return TYPE_BOOL;
        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(scope, node->identifier.name);
            return entry ? entry->type : TYPE_UNKNOWN;
        }
        case AST_BINARY_OP: {
            ValueType left = infer_expr_type_simple(node->binary_op.left, scope);
            ValueType right = infer_expr_type_simple(node->binary_op.right, scope);
            return infer_binary_result_type(node->binary_op.op, left, right);
        }
        case AST_UNARY_OP: {
            ValueType operand_type = infer_expr_type_simple(node->unary_op.operand, scope);
            if (strcmp(node->unary_op.op, "!") == 0) {
                return TYPE_BOOL;
            }
            return operand_type;
        }
        case AST_ASSIGNMENT: {
            // Return the type of the value being assigned
            return infer_expr_type_simple(node->assignment.value, scope);
        }
        case AST_CALL:
            // For recursive calls, assume int for now
            // Will be refined in later passes
            return TYPE_INT;
        default:
            return TYPE_UNKNOWN;
    }
}

// Helper: Infer function return type by walking body with typed parameters
static ValueType infer_function_return_type_with_params(ASTNode* node, SymbolTable* scope) {
    if (!node) return TYPE_VOID;

    switch (node->type) {
        case AST_RETURN:
            if (node->return_stmt.value) {
                ValueType ret_type = infer_expr_type_simple(node->return_stmt.value, scope);
                return ret_type;
            }
            return TYPE_VOID;

        case AST_VAR_DECL:
            // Process variable declaration and add to scope for later lookups
            if (node->var_decl.init) {
                ValueType var_type = infer_expr_type_simple(node->var_decl.init, scope);
                symbol_table_insert(scope, node->var_decl.name, var_type, NULL);
            }
            return TYPE_VOID;

        case AST_BLOCK:
        case AST_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                ValueType ret_type = infer_function_return_type_with_params(
                    node->program.statements[i], scope);
                if (ret_type != TYPE_VOID && ret_type != TYPE_UNKNOWN) {
                    return ret_type;
                }
            }
            return TYPE_VOID;

        case AST_IF: {
            ValueType then_type = infer_function_return_type_with_params(
                node->if_stmt.then_branch, scope);
            if (then_type != TYPE_VOID && then_type != TYPE_UNKNOWN) {
                return then_type;
            }
            if (node->if_stmt.else_branch) {
                ValueType else_type = infer_function_return_type_with_params(
                    node->if_stmt.else_branch, scope);
                if (else_type != TYPE_VOID && else_type != TYPE_UNKNOWN) {
                    return else_type;
                }
            }
            return TYPE_VOID;
        }

        case AST_FOR:
        case AST_WHILE:
            return infer_function_return_type_with_params(node->for_stmt.body, scope);

        default:
            return TYPE_VOID;
    }
}

// Pass 1: Collect function signatures
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                collect_function_signatures(node->program.statements[i], symbols);
            }
            break;

        case AST_FUNCTION_DECL:
            // Register function (type will be determined by specializations)
            symbol_table_insert(symbols, node->func_decl.name, TYPE_UNKNOWN, NULL);
            break;

        default:
            break;
    }
}

// Pass 2: Infer literal and obvious types
static void infer_literal_types(ASTNode* node, SymbolTable* symbols) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                infer_literal_types(node->program.statements[i], symbols);
            }
            break;

        case AST_NUMBER:
            // Already set by parser
            break;

        case AST_STRING:
            node->value_type = TYPE_STRING;
            break;

        case AST_BOOLEAN:
            node->value_type = TYPE_BOOL;
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                infer_literal_types(node->var_decl.init, symbols);
                node->value_type = node->var_decl.init->value_type;
                symbol_table_insert(symbols, node->var_decl.name, node->value_type, NULL);
            }
            break;

        case AST_BINARY_OP:
            infer_literal_types(node->binary_op.left, symbols);
            infer_literal_types(node->binary_op.right, symbols);
            node->value_type = infer_binary_result_type(
                node->binary_op.op,
                node->binary_op.left->value_type,
                node->binary_op.right->value_type);
            break;

        case AST_UNARY_OP:
            infer_literal_types(node->unary_op.operand, symbols);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->value_type = TYPE_BOOL;
            } else {
                node->value_type = node->unary_op.operand->value_type;
            }
            break;

        case AST_CALL:
            for (int i = 0; i < node->call.arg_count; i++) {
                infer_literal_types(node->call.args[i], symbols);
            }
            break;

        case AST_ASSIGNMENT:
            infer_literal_types(node->assignment.value, symbols);
            node->value_type = node->assignment.value->value_type;
            break;

        case AST_IF:
            infer_literal_types(node->if_stmt.condition, symbols);
            infer_literal_types(node->if_stmt.then_branch, symbols);
            if (node->if_stmt.else_branch) {
                infer_literal_types(node->if_stmt.else_branch, symbols);
            }
            break;

        case AST_FOR:
            if (node->for_stmt.init) infer_literal_types(node->for_stmt.init, symbols);
            if (node->for_stmt.condition) infer_literal_types(node->for_stmt.condition, symbols);
            if (node->for_stmt.update) infer_literal_types(node->for_stmt.update, symbols);
            infer_literal_types(node->for_stmt.body, symbols);
            break;

        case AST_WHILE:
            infer_literal_types(node->while_stmt.condition, symbols);
            infer_literal_types(node->while_stmt.body, symbols);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                infer_literal_types(node->return_stmt.value, symbols);
                node->value_type = node->return_stmt.value->value_type;
            }
            break;

        case AST_EXPR_STMT:
            infer_literal_types(node->expr_stmt.expression, symbols);
            break;

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
            if (entry) {
                node->value_type = entry->type;
            }
            break;
        }

        default:
            break;
    }
}

// Pass 3: Analyze call sites to find needed specializations
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                analyze_call_sites(node->program.statements[i], symbols, ctx);
            }
            break;

        case AST_CALL: {
            // First analyze arguments
            for (int i = 0; i < node->call.arg_count; i++) {
                analyze_call_sites(node->call.args[i], symbols, ctx);
            }

            // Check if calling a user function (not a built-in)
            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;

                // Check if it's a user-defined function
                SymbolEntry* entry = symbol_table_lookup(symbols, func_name);
                if (entry) {
                    // Collect argument types
                    ValueType* arg_types = malloc(sizeof(ValueType) * node->call.arg_count);
                    bool all_known = true;

                    for (int i = 0; i < node->call.arg_count; i++) {
                        arg_types[i] = node->call.args[i]->value_type;
                        if (arg_types[i] == TYPE_UNKNOWN) {
                            all_known = false;
                        }
                    }

                    // Only add if all types are known
                    if (all_known && node->call.arg_count > 0) {
                        specialization_context_add(ctx, func_name, arg_types, node->call.arg_count);
                    }

                    free(arg_types);
                }
            }
            break;
        }

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                analyze_call_sites(node->var_decl.init, symbols, ctx);
            }
            break;

        case AST_ASSIGNMENT:
            analyze_call_sites(node->assignment.value, symbols, ctx);
            break;

        case AST_BINARY_OP:
            analyze_call_sites(node->binary_op.left, symbols, ctx);
            analyze_call_sites(node->binary_op.right, symbols, ctx);
            break;

        case AST_UNARY_OP:
            analyze_call_sites(node->unary_op.operand, symbols, ctx);
            break;

        case AST_IF:
            analyze_call_sites(node->if_stmt.condition, symbols, ctx);
            analyze_call_sites(node->if_stmt.then_branch, symbols, ctx);
            if (node->if_stmt.else_branch) {
                analyze_call_sites(node->if_stmt.else_branch, symbols, ctx);
            }
            break;

        case AST_FOR:
            if (node->for_stmt.init) analyze_call_sites(node->for_stmt.init, symbols, ctx);
            if (node->for_stmt.condition) analyze_call_sites(node->for_stmt.condition, symbols, ctx);
            if (node->for_stmt.update) analyze_call_sites(node->for_stmt.update, symbols, ctx);
            analyze_call_sites(node->for_stmt.body, symbols, ctx);
            break;

        case AST_WHILE:
            analyze_call_sites(node->while_stmt.condition, symbols, ctx);
            analyze_call_sites(node->while_stmt.body, symbols, ctx);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                analyze_call_sites(node->return_stmt.value, symbols, ctx);
            }
            break;

        case AST_EXPR_STMT:
            analyze_call_sites(node->expr_stmt.expression, symbols, ctx);
            break;

        case AST_FUNCTION_DECL:
            if (node->func_decl.body) {
                analyze_call_sites(node->func_decl.body, symbols, ctx);
            }
            break;
        default:
            break;
    }
}

// Pass 4: Create specialized function versions
static void create_specializations(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                create_specializations(node->program.statements[i], symbols, ctx);
            }
            break;

        case AST_FUNCTION_DECL: {
            // Iterate through ALL specializations and process matching ones
            FunctionSpecialization* spec = ctx->specializations;
            bool found_any = false;

            while (spec) {
                // Check if this specialization is for our function
                if (strcmp(spec->function_name, node->func_decl.name) == 0) {
                    found_any = true;

                    // Set parameter types for this specialization
                    for (int i = 0; i < node->func_decl.param_count; i++) {
                        node->func_decl.param_types[i] = spec->param_types[i];
                    }

                    // Create function scope with specialized parameter types
                    SymbolTable* func_scope = symbol_table_create(symbols);
                    for (int i = 0; i < node->func_decl.param_count; i++) {
                        symbol_table_insert(func_scope, node->func_decl.params[i],
                                          spec->param_types[i], NULL);
                    }

                    // Infer return type with specialized parameters
                    ValueType return_type = infer_function_return_type_with_params(
                        node->func_decl.body, func_scope);

                    // Store return type in specialization (NOW MODIFIES ORIGINAL!)
                    spec->return_type = return_type;
                    s = spec;

                    // Debug: Print what we inferred
                    printf("    %s: inferred return type = ", spec->specialized_name);
                    switch (return_type) {
                        case TYPE_INT: printf("int\n"); break;
                        case TYPE_DOUBLE: printf("double\n"); break;
                        case TYPE_STRING: printf("string\n"); break;
                        case TYPE_BOOL: printf("bool\n"); break;
                        case TYPE_VOID: printf("void\n"); break;
                        default: printf("unknown\n"); break;
                    }

                    symbol_table_free(func_scope);
                }

                spec = spec->next;
            }

            if (!found_any) {
                // No specializations - use default int parameters
                for (int i = 0; i < node->func_decl.param_count; i++) {
                    node->func_decl.param_types[i] = TYPE_INT;
                }

                // Create scope with parameter types
                SymbolTable* func_scope = symbol_table_create(symbols);
                for (int i = 0; i < node->func_decl.param_count; i++) {
                    symbol_table_insert(func_scope, node->func_decl.params[i],
                                      node->func_decl.param_types[i], NULL);
                }

                // Infer return type
                node->func_decl.return_type = infer_function_return_type_with_params(
                    node->func_decl.body, func_scope);

                symbol_table_free(func_scope);
            }
            break;
        }

        default:
            break;
    }
}

static void clone_specialization_bodies(ASTNode* node, SymbolTable* symbols,
                                       SpecializationContext* ctx) {
    if (!node || !ctx) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                clone_specialization_bodies(node->program.statements[i], symbols, ctx);
            }
            break;

        case AST_FUNCTION_DECL: {
            // Check if this function has specializations
            FunctionSpecialization* spec = ctx->specializations;

            while (spec) {
                // Find all specializations for this function
                if (strcmp(spec->function_name, node->func_decl.name) == 0) {
                    printf("    Cloning and analyzing: %s\n", spec->specialized_name);

                    // Clone the AST and perform type analysis with concrete types
                    specialization_create_body(spec, node);
                }

                spec = spec->next;
            }
            break;
        }

        default:
            break;
    }
}


// Pass 5: Final type inference with all specializations known
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                infer_with_specializations(node->program.statements[i], symbols, ctx);
            }
            break;

        case AST_NUMBER:
        case AST_STRING:
        case AST_BOOLEAN:
            // Already set
            break;

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
            if (entry) {
                node->value_type = entry->type;
            }
            break;
        }

        case AST_BINARY_OP:
            infer_with_specializations(node->binary_op.left, symbols, ctx);
            infer_with_specializations(node->binary_op.right, symbols, ctx);
            node->value_type = infer_binary_result_type(
                node->binary_op.op,
                node->binary_op.left->value_type,
                node->binary_op.right->value_type);
            break;

        case AST_UNARY_OP:
            infer_with_specializations(node->unary_op.operand, symbols, ctx);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->value_type = TYPE_BOOL;
            } else {
                node->value_type = node->unary_op.operand->value_type;
            }
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                infer_with_specializations(node->var_decl.init, symbols, ctx);
                node->value_type = node->var_decl.init->value_type;
            }
            symbol_table_insert(symbols, node->var_decl.name, node->value_type, NULL);
            break;

        case AST_ASSIGNMENT:
            infer_with_specializations(node->assignment.value, symbols, ctx);
            node->value_type = node->assignment.value->value_type;
            break;

        case AST_CALL: {
            // Infer argument types
            for (int i = 0; i < node->call.arg_count; i++) {
                infer_with_specializations(node->call.args[i], symbols, ctx);
            }

            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;

                // Get argument types
                ValueType* arg_types = malloc(sizeof(ValueType) * node->call.arg_count);
                for (int i = 0; i < node->call.arg_count; i++) {
                    arg_types[i] = node->call.args[i]->value_type;
                }

                // Find matching specialization
                FunctionSpecialization* spec = specialization_context_find(
                    ctx, func_name, arg_types, node->call.arg_count);

                if (spec) {
                    node->value_type = spec->return_type;
                } else {
                    // No specialization, use default
                    node->value_type = TYPE_INT;
                }

                free(arg_types);
            } else if (node->call.callee->type == AST_MEMBER_ACCESS) {
                // TODO: Handle native functions return policy
                node->value_type = TYPE_VOID;
            } else {
                ASTNode* first_arg = node->call.args[0];
                first_arg->type = first_arg->type;
                ;
            }

            break;
        }

        case AST_IF:
            infer_with_specializations(node->if_stmt.condition, symbols, ctx);
            infer_with_specializations(node->if_stmt.then_branch, symbols, ctx);
            if (node->if_stmt.else_branch) {
                infer_with_specializations(node->if_stmt.else_branch, symbols, ctx);
            }
            break;

        case AST_FOR:
            if (node->for_stmt.init) infer_with_specializations(node->for_stmt.init, symbols, ctx);
            if (node->for_stmt.condition) infer_with_specializations(node->for_stmt.condition, symbols, ctx);
            if (node->for_stmt.update) infer_with_specializations(node->for_stmt.update, symbols, ctx);
            infer_with_specializations(node->for_stmt.body, symbols, ctx);
            break;

        case AST_WHILE:
            infer_with_specializations(node->while_stmt.condition, symbols, ctx);
            infer_with_specializations(node->while_stmt.body, symbols, ctx);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                infer_with_specializations(node->return_stmt.value, symbols, ctx);
                node->value_type = node->return_stmt.value->value_type;
            }
            break;

        case AST_EXPR_STMT:
            infer_with_specializations(node->expr_stmt.expression, symbols, ctx);
            break;

        default:
            break;
    }
}

// Main entry point: Multi-pass type inference with specialization
void type_inference(ASTNode* ast, SymbolTable* symbols) {
    if (!ast || !symbols) return;

    printf("Starting multi-pass type inference...\n");

    // Create specialization context
    SpecializationContext* ctx = specialization_context_create();

    // Pass 1: Collect function signatures
    printf("  Pass 1: Collecting function signatures\n");
    collect_function_signatures(ast, symbols);

    // Pass 2: Infer literal types
    printf("  Pass 2: Inferring literal types\n");
    infer_literal_types(ast, symbols);

    // Pass 3: Analyze call sites to find needed specializations
    printf("  Pass 3: Analyzing call sites\n");
    analyze_call_sites(ast, symbols, ctx);

    // Pass 4: Create specialized function versions
    printf("  Pass 4: Creating specializations\n");
    create_specializations(ast, symbols, ctx);

    // NEW PASS 4.5: Clone and analyze specialization bodies
    printf("  Pass 4.5: Cloning and analyzing specialization bodies\n");
    clone_specialization_bodies(ast, symbols, ctx);
    FunctionSpecialization* f = ctx->specializations;

    // Pass 5: Final type inference with all specializations known
    printf("  Pass 5: Final type resolution\n");
    infer_with_specializations(ast, symbols, ctx);

    // Store specialization context for codegen
    ast->specialization_ctx = ctx;

    printf("Type inference complete!\n");
}
