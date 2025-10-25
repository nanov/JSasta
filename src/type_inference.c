#include "jsasta_compiler.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// Helper function to convert ValueType to string for error messages
static const char* value_type_to_string(ValueType type) {
    switch (type) {
        case TYPE_INT: return "int";
        case TYPE_DOUBLE: return "double";
        case TYPE_STRING: return "string";
        case TYPE_BOOL: return "bool";
        case TYPE_VOID: return "void";
        case TYPE_FUNCTION: return "function";
        case TYPE_ARRAY_INT: return "int[]";
        case TYPE_ARRAY_DOUBLE: return "double[]";
        case TYPE_ARRAY_STRING: return "string[]";
        case TYPE_OBJECT: return "object";
        case TYPE_UNKNOWN: return "unknown";
        default: return "unknown";
    }
}

// Forward declarations
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols);
static void infer_literal_types(ASTNode* node, SymbolTable* symbols);
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx);
static void create_specializations(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx);
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, SpecializationContext* ctx);
static ValueType infer_function_return_type_with_params(ASTNode* body, SymbolTable* scope);
static void iterative_specialization_discovery(ASTNode* ast, SymbolTable* symbols, SpecializationContext* ctx);


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

    if (strcmp(op, ">>") == 0 || strcmp(op, "<<") == 0) {
        if (left == TYPE_INT && right == TYPE_INT) return TYPE_INT;
    }

    if (strcmp(op, "&") == 0) {
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
        case AST_TERNARY: {
            ValueType true_type = infer_expr_type_simple(node->ternary.true_expr, scope);
            ValueType false_type = infer_expr_type_simple(node->ternary.false_expr, scope);
            // If both branches have the same type, use that type
            if (true_type == false_type) return true_type;
            // If one is double and the other is int, promote to double
            if ((true_type == TYPE_DOUBLE && false_type == TYPE_INT) ||
                (true_type == TYPE_INT && false_type == TYPE_DOUBLE)) {
                return TYPE_DOUBLE;
            }
            // Otherwise, return unknown
            return TYPE_UNKNOWN;
        }
        case AST_ARRAY_LITERAL: {
            // Determine array type from first element
            if (node->array_literal.count > 0) {
                ValueType elem_type = infer_expr_type_simple(node->array_literal.elements[0], scope);
                if (elem_type == TYPE_INT) return TYPE_ARRAY_INT;
                if (elem_type == TYPE_DOUBLE) return TYPE_ARRAY_DOUBLE;
                if (elem_type == TYPE_STRING) return TYPE_ARRAY_STRING;
            }
            return TYPE_ARRAY_INT; // Default to int array
        }
        case AST_INDEX_ACCESS: {
            ValueType obj_type = infer_expr_type_simple(node->index_access.object, scope);
            // String indexing returns string (single char)
            if (obj_type == TYPE_STRING) return TYPE_STRING;
            // Array indexing returns element type
            if (obj_type == TYPE_ARRAY_INT) return TYPE_INT;
            if (obj_type == TYPE_ARRAY_DOUBLE) return TYPE_DOUBLE;
            if (obj_type == TYPE_ARRAY_STRING) return TYPE_STRING;
            return TYPE_UNKNOWN;
        }
        case AST_OBJECT_LITERAL:
            return TYPE_OBJECT;
        case AST_MEMBER_ACCESS:
            // For now, we can't infer the type of member access without more context
            // This will be resolved during codegen based on the actual object structure
            return TYPE_UNKNOWN;
        case AST_CALL:
            // For now return unknown - will be resolved in later passes
            // Runtime functions will be checked if user function not found
            return TYPE_UNKNOWN;
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
                symbol_table_insert(scope, node->var_decl.name, var_type, NULL, node->var_decl.is_const);
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
            symbol_table_insert_func_declaration(symbols, node->func_decl.name, node);
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
                // Use the new function that stores the AST node (needed for object member access type inference)
                symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->value_type, node->var_decl.is_const, node);
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
            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;
                const SymbolEntry* entry = symbol_table_lookup(symbols, func_name);
                // no user function
                if (!entry) {
                	node->value_type = runtime_get_function_type(func_name);
                }
            }
            break;
        case AST_ASSIGNMENT:
            infer_literal_types(node->assignment.value, symbols);
            node->value_type = node->assignment.value->value_type;
            break;

        case AST_MEMBER_ASSIGNMENT: {
            // Infer types for object and value
            infer_literal_types(node->member_assignment.object, symbols);
            infer_literal_types(node->member_assignment.value, symbols);
            
            // Type check: verify the assigned value matches the property's original type
            ASTNode* obj = node->member_assignment.object;
            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry && entry->node && entry->node->type == AST_VAR_DECL &&
                    entry->node->var_decl.init && entry->node->var_decl.init->type == AST_OBJECT_LITERAL) {
                    
                    ASTNode* obj_lit = entry->node->var_decl.init;
                    // Find the property and check its type
                    for (int i = 0; i < obj_lit->object_literal.count; i++) {
                        if (strcmp(obj_lit->object_literal.keys[i], node->member_assignment.property) == 0) {
                            ValueType prop_type = obj_lit->object_literal.values[i]->value_type;
                            ValueType assigned_type = node->member_assignment.value->value_type;
                            
                            if (prop_type != assigned_type) {
                                log_error_at(&node->loc, 
                                    "Type mismatch: cannot assign %s to property '%s' of type %s",
                                    value_type_to_string(assigned_type),
                                    node->member_assignment.property,
                                    value_type_to_string(prop_type));
                            }
                            break;
                        }
                    }
                }
            }
            
            node->value_type = node->member_assignment.value->value_type;
            break;
        }

        case AST_TERNARY:
            infer_literal_types(node->ternary.condition, symbols);
            infer_literal_types(node->ternary.true_expr, symbols);
            infer_literal_types(node->ternary.false_expr, symbols);
            // Determine result type based on both branches
            if (node->ternary.true_expr->value_type == node->ternary.false_expr->value_type) {
                node->value_type = node->ternary.true_expr->value_type;
            } else if ((node->ternary.true_expr->value_type == TYPE_DOUBLE &&
                        node->ternary.false_expr->value_type == TYPE_INT) ||
                       (node->ternary.true_expr->value_type == TYPE_INT &&
                        node->ternary.false_expr->value_type == TYPE_DOUBLE)) {
                node->value_type = TYPE_DOUBLE;
            } else {
                node->value_type = TYPE_UNKNOWN;
            }
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

        case AST_PREFIX_OP:
        case AST_POSTFIX_OP: {
            // ++i or i++ should have the same type as the variable
            const char* var_name = (node->type == AST_PREFIX_OP) ?
                                   node->prefix_op.name : node->postfix_op.name;
            SymbolEntry* entry = symbol_table_lookup(symbols, var_name);
            if (entry) {
                node->value_type = entry->type;
            } else {
                // Variable not yet defined - will be caught later
                node->value_type = TYPE_INT; // Default to int for now
            }
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_literal_types(node->compound_assignment.value, symbols);
            // Result type should match the variable's type
            {
                SymbolEntry* entry = symbol_table_lookup(symbols, node->compound_assignment.name);
                if (entry) {
                    node->value_type = entry->type;
                } else {
                    // Variable not defined - will be caught later
                    node->value_type = node->compound_assignment.value->value_type;
                }
            }
            break;

        case AST_EXPR_STMT:
            infer_literal_types(node->expr_stmt.expression, symbols);
            break;

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
            if (entry) {
                node->value_type = entry->type;
            } else if (node->value_type != TYPE_UNKNOWN) {
                // Only report error on first encounter (when type is not yet UNKNOWN)
                log_error_at(&node->loc, "Undefined variable: %s", node->identifier.name);
                node->value_type = TYPE_UNKNOWN;
            }
            break;
        }

        case AST_ARRAY_LITERAL:
            // Infer types of all elements
            for (int i = 0; i < node->array_literal.count; i++) {
                infer_literal_types(node->array_literal.elements[i], symbols);
            }
            // Determine array type from first element
            if (node->array_literal.count > 0) {
                ValueType elem_type = node->array_literal.elements[0]->value_type;
                if (elem_type == TYPE_INT) node->value_type = TYPE_ARRAY_INT;
                else if (elem_type == TYPE_DOUBLE) node->value_type = TYPE_ARRAY_DOUBLE;
                else if (elem_type == TYPE_STRING) node->value_type = TYPE_ARRAY_STRING;
                else node->value_type = TYPE_ARRAY_INT;
            } else {
                node->value_type = TYPE_ARRAY_INT; // Empty array defaults to int
            }
            break;

        case AST_INDEX_ACCESS:
            infer_literal_types(node->index_access.object, symbols);
            infer_literal_types(node->index_access.index, symbols);
            // Determine result type based on object type
            if (node->index_access.object->value_type == TYPE_STRING) {
                node->value_type = TYPE_STRING;
            } else if (node->index_access.object->value_type == TYPE_ARRAY_INT) {
                node->value_type = TYPE_INT;
            } else if (node->index_access.object->value_type == TYPE_ARRAY_DOUBLE) {
                node->value_type = TYPE_DOUBLE;
            } else if (node->index_access.object->value_type == TYPE_ARRAY_STRING) {
                node->value_type = TYPE_STRING;
            }
            break;

        case AST_INDEX_ASSIGNMENT:
            infer_literal_types(node->index_assignment.object, symbols);
            infer_literal_types(node->index_assignment.index, symbols);
            infer_literal_types(node->index_assignment.value, symbols);
            // Assignment returns the assigned value's type
            node->value_type = node->index_assignment.value->value_type;
            break;

        case AST_OBJECT_LITERAL:
            // Infer types of all property values
            for (int i = 0; i < node->object_literal.count; i++) {
                infer_literal_types(node->object_literal.values[i], symbols);
            }
            node->value_type = TYPE_OBJECT;
            break;

        case AST_MEMBER_ACCESS: {
            infer_literal_types(node->member_access.object, symbols);

            // Try to infer the type from the object literal
            ASTNode* obj = node->member_access.object;
            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry && entry->node && entry->node->type == AST_VAR_DECL &&
                    entry->node->var_decl.init && entry->node->var_decl.init->type == AST_OBJECT_LITERAL) {

                    ASTNode* obj_lit = entry->node->var_decl.init;
                    // Find the property and get its type
                    for (int i = 0; i < obj_lit->object_literal.count; i++) {
                        if (strcmp(obj_lit->object_literal.keys[i], node->member_access.property) == 0) {
                            node->value_type = obj_lit->object_literal.values[i]->value_type;
                            return;
                        }
                    }
                }
            }

            // Couldn't determine type
            node->value_type = TYPE_UNKNOWN;
            break;
        }

        default:
            break;
    }
}

static void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node, ValueType* arg_types, SymbolTable* symbols, SpecializationContext* ctx) {
    if (!spec || !original_func_node || original_func_node->type != AST_FUNCTION_DECL) {
        return;
    }

    // Clone the entire function node
    ASTNode* cloned_func = ast_clone(original_func_node);

    // Update function name to specialized name
    free(cloned_func->func_decl.name);
    cloned_func->func_decl.name = strdup(spec->specialized_name);

    // Set parameter types in the cloned function
    if (!cloned_func->func_decl.param_types) {
        cloned_func->func_decl.param_types = (ValueType*)malloc(sizeof(ValueType) * spec->param_count);
    }
    memcpy(cloned_func->func_decl.param_types, spec->param_types, sizeof(ValueType) * spec->param_count);

    SymbolTable* temp_symbols = symbol_table_create(symbols);
    ASTNode* ast = cloned_func->func_decl.body;
    // Insert parameters with their concrete types
    for (int i = 0; i < cloned_func->func_decl.param_count; i++) {
        symbol_table_insert(temp_symbols, cloned_func->func_decl.params[i], arg_types[i], NULL, false);
    }
    infer_literal_types(ast, temp_symbols);
    iterative_specialization_discovery(ast, temp_symbols, ctx);
    spec->return_type = infer_function_return_type_with_params(
        ast, temp_symbols);
    free(temp_symbols);

    spec->specialized_body = cloned_func;

    const char* return_type_str = "unknown";
    switch (spec->return_type) {
        case TYPE_INT: return_type_str = "int"; break;
        case TYPE_DOUBLE: return_type_str = "double"; break;
        case TYPE_STRING: return_type_str = "string"; break;
        case TYPE_BOOL: return_type_str = "bool"; break;
        case TYPE_VOID: return_type_str = "void"; break;
        default: return_type_str = "unknown"; break;
    }
    log_verbose_indent(2, "Analyzed %s with return type %s", spec->specialized_name, return_type_str);
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
                infer_with_specializations(node->call.args[i], symbols, ctx);
            }

            // Check if calling a user function (not a built-in)
            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;

                // Check if it's a user-defined function or function variable
                SymbolEntry* entry = symbol_table_lookup(symbols, func_name);
                if (entry && entry->node) {
                    // If it's a function variable, get the actual function name
                    const char* actual_func_name = func_name;
                    ASTNode* func_decl = entry->node;

                    if (entry->type == TYPE_FUNCTION && func_decl->type == AST_FUNCTION_DECL) {
                        // Use the function's actual name for specialization
                        actual_func_name = func_decl->func_decl.name;
                    }

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
                        FunctionSpecialization* spec = specialization_context_add(ctx, actual_func_name, arg_types, node->call.arg_count);
                        if (spec) {
                        	specialization_create_body(spec, func_decl, arg_types, symbols, ctx);
                        }
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

        case AST_MEMBER_ASSIGNMENT:
            analyze_call_sites(node->member_assignment.object, symbols, ctx);
            analyze_call_sites(node->member_assignment.value, symbols, ctx);
            break;

        case AST_COMPOUND_ASSIGNMENT:
            analyze_call_sites(node->compound_assignment.value, symbols, ctx);
            break;

        case AST_TERNARY:
            analyze_call_sites(node->ternary.condition, symbols, ctx);
            analyze_call_sites(node->ternary.true_expr, symbols, ctx);
            analyze_call_sites(node->ternary.false_expr, symbols, ctx);
            break;

        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                analyze_call_sites(node->array_literal.elements[i], symbols, ctx);
            }
            break;

        case AST_INDEX_ACCESS:
            analyze_call_sites(node->index_access.object, symbols, ctx);
            analyze_call_sites(node->index_access.index, symbols, ctx);
            break;

        case AST_INDEX_ASSIGNMENT:
            analyze_call_sites(node->index_assignment.object, symbols, ctx);
            analyze_call_sites(node->index_assignment.index, symbols, ctx);
            analyze_call_sites(node->index_assignment.value, symbols, ctx);
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

        case AST_OBJECT_LITERAL:
            for (int i = 0; i < node->object_literal.count; i++) {
                analyze_call_sites(node->object_literal.values[i], symbols, ctx);
            }
            break;

        case AST_MEMBER_ACCESS:
            analyze_call_sites(node->member_access.object, symbols, ctx);
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
                    break;
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
                                      node->func_decl.param_types[i], NULL, false);
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
            // Don't report error here - it's already reported in infer_literal_types
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
            symbol_table_insert(symbols, node->var_decl.name, node->value_type, NULL, node->var_decl.is_const);
            break;

        case AST_ASSIGNMENT:
            infer_with_specializations(node->assignment.value, symbols, ctx);
            node->value_type = node->assignment.value->value_type;
            break;

        case AST_TERNARY:
            infer_with_specializations(node->ternary.condition, symbols, ctx);
            infer_with_specializations(node->ternary.true_expr, symbols, ctx);
            infer_with_specializations(node->ternary.false_expr, symbols, ctx);
            // Determine result type based on both branches
            if (node->ternary.true_expr->value_type == node->ternary.false_expr->value_type) {
                node->value_type = node->ternary.true_expr->value_type;
            } else if ((node->ternary.true_expr->value_type == TYPE_DOUBLE &&
                        node->ternary.false_expr->value_type == TYPE_INT) ||
                       (node->ternary.true_expr->value_type == TYPE_INT &&
                        node->ternary.false_expr->value_type == TYPE_DOUBLE)) {
                node->value_type = TYPE_DOUBLE;
            } else {
                node->value_type = TYPE_UNKNOWN;
            }
            break;

        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                infer_with_specializations(node->array_literal.elements[i], symbols, ctx);
            }
            // Determine array type from first element
            if (node->array_literal.count > 0) {
                ValueType elem_type = node->array_literal.elements[0]->value_type;
                if (elem_type == TYPE_INT) node->value_type = TYPE_ARRAY_INT;
                else if (elem_type == TYPE_DOUBLE) node->value_type = TYPE_ARRAY_DOUBLE;
                else if (elem_type == TYPE_STRING) node->value_type = TYPE_ARRAY_STRING;
                else node->value_type = TYPE_ARRAY_INT;
            } else {
                node->value_type = TYPE_ARRAY_INT;
            }
            break;

        case AST_INDEX_ACCESS:
            infer_with_specializations(node->index_access.object, symbols, ctx);
            infer_with_specializations(node->index_access.index, symbols, ctx);
            // Determine result type based on object type
            if (node->index_access.object->value_type == TYPE_STRING) {
                node->value_type = TYPE_STRING;
            } else if (node->index_access.object->value_type == TYPE_ARRAY_INT) {
                node->value_type = TYPE_INT;
            } else if (node->index_access.object->value_type == TYPE_ARRAY_DOUBLE) {
                node->value_type = TYPE_DOUBLE;
            } else if (node->index_access.object->value_type == TYPE_ARRAY_STRING) {
                node->value_type = TYPE_STRING;
            }
            break;

        case AST_INDEX_ASSIGNMENT:
            infer_with_specializations(node->index_assignment.object, symbols, ctx);
            infer_with_specializations(node->index_assignment.index, symbols, ctx);
            infer_with_specializations(node->index_assignment.value, symbols, ctx);
            // Assignment returns the assigned value's type
            node->value_type = node->index_assignment.value->value_type;
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

                // First, try to find user-defined function specialization
                FunctionSpecialization* spec = specialization_context_find(
                    ctx, func_name, arg_types, node->call.arg_count);

                if (spec) {
                    // Found user function
                    node->value_type = spec->return_type;
                } else {
                    // Not a user function, check if it's a runtime builtin
                    ValueType runtime_type = runtime_get_function_type(func_name);
                    if (runtime_type != TYPE_UNKNOWN) {
                        node->value_type = runtime_type;
                    } else {
                        // Unknown function - default to void, will error in codegen if not found
                        node->value_type = TYPE_VOID;
                    }
                }

                free(arg_types);
            } else if (node->call.callee->type == AST_MEMBER_ACCESS) {
                // Handle member access (e.g., console.log)
                ASTNode* obj = node->call.callee->member_access.object;
                char* prop = node->call.callee->member_access.property;

                if (obj->type == AST_IDENTIFIER) {
                    char full_name[256];
                    snprintf(full_name, sizeof(full_name), "%s.%s", obj->identifier.name, prop);
                    ValueType runtime_type = runtime_get_function_type(full_name);
                    if (runtime_type != TYPE_UNKNOWN) {
                        node->value_type = runtime_type;
                        break;
                    }
                }

                // Default for member access
                node->value_type = TYPE_VOID;
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

        case AST_PREFIX_OP:
        case AST_POSTFIX_OP: {
            // ++i or i++ should have the same type as the variable
            const char* var_name = (node->type == AST_PREFIX_OP) ?
                                   node->prefix_op.name : node->postfix_op.name;
            SymbolEntry* entry = symbol_table_lookup(symbols, var_name);
            if (entry) {
                node->value_type = entry->type;
            }
            // Type already set in infer_literal_types if variable is undefined
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_with_specializations(node->compound_assignment.value, symbols, ctx);
            // Result type should match the variable's type
            {
                SymbolEntry* entry = symbol_table_lookup(symbols, node->compound_assignment.name);
                if (entry) {
                    node->value_type = entry->type;
                } else {
                    // Variable not defined
                    node->value_type = node->compound_assignment.value->value_type;
                }
            }
            break;

        case AST_EXPR_STMT:
            infer_with_specializations(node->expr_stmt.expression, symbols, ctx);
            break;

        case AST_OBJECT_LITERAL:
            for (int i = 0; i < node->object_literal.count; i++) {
                infer_with_specializations(node->object_literal.values[i], symbols, ctx);
            }
            node->value_type = TYPE_OBJECT;
            break;

        case AST_MEMBER_ACCESS: {
            infer_with_specializations(node->member_access.object, symbols, ctx);

            // Try to infer the type from the object literal
            ASTNode* obj = node->member_access.object;
            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry && entry->node && entry->node->type == AST_VAR_DECL &&
                    entry->node->var_decl.init && entry->node->var_decl.init->type == AST_OBJECT_LITERAL) {

                    ASTNode* obj_lit = entry->node->var_decl.init;
                    // Find the property and get its type
                    for (int i = 0; i < obj_lit->object_literal.count; i++) {
                        if (strcmp(obj_lit->object_literal.keys[i], node->member_access.property) == 0) {
                            node->value_type = obj_lit->object_literal.values[i]->value_type;
                            break;
                        }
                    }
                }
            }

            if (node->value_type == TYPE_UNKNOWN) {
                // Couldn't determine type, leave as unknown
            }
            break;
        }

        default:
            break;
    }
}

static void iterative_specialization_discovery(ASTNode*ast, SymbolTable* symbols, SpecializationContext* ctx) {
	int iteration = 0;
  int max_iterations = 100; // Safety limit to prevent infinite loops

  while (iteration < max_iterations) {
    size_t current_spec_count = ctx->functions_processed;

    log_verbose_indent(2, "Iteration %d: %zu specializations", iteration, current_spec_count);
    // Pass 3: Analyze call sites to find needed specializations
    analyze_call_sites(ast, symbols, ctx);
    // Pass 4: Create specialized function versions
    create_specializations(ast, symbols, ctx);
    // Pass 5: Propagate types with known specializations
    infer_with_specializations(ast, symbols, ctx);


    // If no new specializations were discovered, we're done
    if (ctx->functions_processed == current_spec_count) {
        log_verbose_indent(2, "Convergence reached after %d iteration(s)", iteration + 1);
        return;;
    }

    current_spec_count = ctx->functions_processed;
    iteration++;
  }

  log_warning("Maximum iterations reached, some types may be unresolved");
}

// Main entry point: Multi-pass type inference with specialization
void type_inference(ASTNode* ast, SymbolTable* symbols) {
    if (!ast || !symbols) return;

    log_verbose("Starting multi-pass type inference");

    // Create specialization context
    SpecializationContext* ctx = specialization_context_create();

    // Pass 1: Collect function signatures
    log_verbose_indent(1, "Pass 1: Collecting function signatures");
    collect_function_signatures(ast, symbols);

    // Pass 2: Infer literal types
    log_verbose_indent(1, "Pass 2: Inferring literal types");
    infer_literal_types(ast, symbols);

    // Pass 3-5: Iteratively analyze and specialize until no new specializations found
    // This is needed because variable types depend on function return types,
    // which depend on specializations, which depend on call site argument types
    log_verbose_indent(1, "Pass 3-5: Iterative specialization discovery");
    iterative_specialization_discovery(ast, symbols, ctx);

    // Store specialization context for codegen
    ast->specialization_ctx = ctx;

    log_verbose("Type inference complete");
}
