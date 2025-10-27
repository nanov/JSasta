#include "jsasta_compiler.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// Static counter for generating unique type names
static int type_name_counter = 0;

// Helper function to generate unique type name for objects
static char* generate_type_name(void) {
    char* name = (char*)malloc(32);
    snprintf(name, 32, "Object_%d", type_name_counter++);
    return name;
}
// Forward declarations
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx);
static void infer_literal_types(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx);
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, TypeContext* ctx);
static void create_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx);
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx);
static TypeInfo* infer_function_return_type_with_params(ASTNode* body, SymbolTable* scope);
static void iterative_specialization_discovery(ASTNode* ast, SymbolTable* symbols, TypeContext* ctx);


// Helper: Infer type from binary operation
static TypeInfo* infer_binary_result_type(const char* op, TypeInfo* left, TypeInfo* right) {
    log_verbose("      infer_binary_result_type: %s op=%s %s",
                left ? left->type_name : "NULL", op, right ? right->type_name : "NULL");
    if (strcmp(op, "+") == 0) {
        if (left == Type_String || right == Type_String) return Type_String;
        if (left == Type_Double || right == Type_Double) return Type_Double;
        if (left == Type_Int && right == Type_Int) return Type_Int;
    }

    if (strcmp(op, "-") == 0 || strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
        if (left == Type_Double || right == Type_Double) return Type_Double;
        if (left == Type_Int && right == Type_Int) return Type_Int;
    }

    if (strcmp(op, "%") == 0) {
        if (left == Type_Int && right == Type_Int) return Type_Int;
    }

    if (strcmp(op, ">>") == 0 || strcmp(op, "<<") == 0) {
        if (left == Type_Int && right == Type_Int) return Type_Int;
    }

    if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0) {
	    if (left == Type_Int && right == Type_Int) {
            log_verbose("      Returning Type_Int for bitwise op");
            return Type_Int;
        }
    }

    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        return Type_Bool;
    }

    return Type_Unknown;
}

// Helper: Infer function return type by walking body with typed parameters
static TypeInfo* infer_function_return_type_with_params(ASTNode* node, SymbolTable* scope);

// Helper: Simple type inference for expressions (used during return type inference)
static TypeInfo* infer_expr_type_simple(ASTNode* node, SymbolTable* scope) {
    if (!node) return Type_Unknown;

    // If type_info is already set (by infer_literal_types), use it
    if (node->type_info && !type_info_is_unknown(node->type_info)) {
        log_verbose("      Using cached type_info: %s", node->type_info->type_name);
        return node->type_info;
    }
    if (node->type_info) {
        log_verbose("      type_info is unknown, inferring...");
    }

    switch (node->type) {
        case AST_NUMBER:
            return node->type_info ? node->type_info : Type_Unknown;
        case AST_STRING:
            return Type_String;
        case AST_BOOLEAN:
            return Type_Bool;
        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(scope, node->identifier.name);
            return entry ? entry->type_info : Type_Unknown;
        }
        case AST_BINARY_OP: {
            TypeInfo*  left = infer_expr_type_simple(node->binary_op.left, scope);
            TypeInfo*  right = infer_expr_type_simple(node->binary_op.right, scope);
            return infer_binary_result_type(node->binary_op.op, left, right);
        }
        case AST_UNARY_OP: {
            TypeInfo* operand_type = infer_expr_type_simple(node->unary_op.operand, scope);
            if (strcmp(node->unary_op.op, "!") == 0) {
                return Type_Bool;
            }
            return operand_type;
        }
        case AST_ASSIGNMENT: {
            // Return the type of the value being assigned
            return infer_expr_type_simple(node->assignment.value, scope);
        }
        case AST_TERNARY: {
            TypeInfo*  true_type = infer_expr_type_simple(node->ternary.true_expr, scope);
            TypeInfo*  false_type = infer_expr_type_simple(node->ternary.false_expr, scope);
            // If both branches have the same type, use that type
            if (true_type == false_type) return true_type;
            // If one is double and the other is int, promote to double
            if ((true_type == Type_Double && false_type == Type_Int) ||
                (true_type == Type_Int && false_type == Type_Double)) {
                return Type_Double;
            }
            // Otherwise, return unknown
            return Type_Unknown;
        }
        case AST_ARRAY_LITERAL: {
            // Determine array type from first element
            if (node->array_literal.count > 0) {
                TypeInfo* elem_type = infer_expr_type_simple(node->array_literal.elements[0], scope);
                if (elem_type == Type_Int) return Type_Array_Int;
                if (elem_type == Type_Double) return Type_Array_Double;
                if (elem_type == Type_Bool) return Type_Array_Bool;
                if (elem_type == Type_String) return Type_Array_String;
            }
            return Type_Array_Int; // Default to int array
        }
        case AST_INDEX_ACCESS: {
            TypeInfo* obj_type = infer_expr_type_simple(node->index_access.object, scope);
            // String indexing returns string (single char)
            if (obj_type == Type_String) return Type_String;
            if (type_info_is_array(obj_type)) return obj_type->data.array.element_type;
            return Type_Unknown;
        }
        case AST_OBJECT_LITERAL:
            return node->type_info;
        case AST_MEMBER_ACCESS: {
            // Try to infer the property type using TypeInfo
            ASTNode* obj = node->member_access.object;
            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(scope, obj->identifier.name);
                if (entry && entry->type_info) {
                    // Use TypeInfo to find the property type
                    int prop_index = type_info_find_property(entry->type_info, node->member_access.property);
                    if (prop_index >= 0) {
                        return entry->type_info->data.object.property_types[prop_index];
                    }
                }
            }
            return Type_Unknown;
        }
        case AST_CALL:
            // For now return unknown - will be resolved in later passes
            // Runtime functions will be checked if user function not found
            return Type_Unknown;
        default:
            return Type_Unknown;
    }
}

// Helper: Infer function return type by walking body with typed parameters
static TypeInfo* infer_function_return_type_with_params(ASTNode* node, SymbolTable* scope) {
    if (!node) return Type_Void;

    switch (node->type) {
        case AST_RETURN:
            if (node->return_stmt.value) {
                TypeInfo* ret_type = infer_expr_type_simple(node->return_stmt.value, scope);
                log_verbose("    Return statement type: %s", ret_type ? ret_type->type_name : "NULL");
                return ret_type;
            }
            return Type_Void;

        case AST_VAR_DECL:
            // Process variable declaration and add to scope for later lookups
            if (node->var_decl.init) {
                infer_expr_type_simple(node->var_decl.init, scope);
                symbol_table_insert(scope, node->var_decl.name, node->var_decl.init->type_info, NULL, node->var_decl.is_const);
            }
            return Type_Void;

        case AST_BLOCK:
        case AST_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                TypeInfo* ret_type = infer_function_return_type_with_params(
                    node->program.statements[i], scope);
                if (ret_type != Type_Void && !type_info_is_unknown(ret_type)) {
                    return ret_type;
                }
            }
            return Type_Void;

        case AST_IF: {
            TypeInfo*  then_type = infer_function_return_type_with_params(
                node->if_stmt.then_branch, scope);
            if (then_type != Type_Void && !type_info_is_unknown(then_type)) {
                return then_type;
            }
            if (node->if_stmt.else_branch) {
                TypeInfo*  else_type = infer_function_return_type_with_params(
                    node->if_stmt.else_branch, scope);
                if (else_type != Type_Void && !type_info_is_unknown(else_type)) {
                    return else_type;
                }
            }
            return Type_Void;
        }

        case AST_FOR:
        case AST_WHILE:
            return infer_function_return_type_with_params(node->for_stmt.body, scope);

        default:
            return Type_Void;
    }
}

// Pass 1: Collect function signatures
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                collect_function_signatures(node->program.statements[i], symbols, type_ctx);
            }
            break;

        case AST_FUNCTION_DECL: {
            // All functions (user and external) now use the same structure
            const char* func_name = node->func_decl.name;
            TypeInfo** param_type_hints = node->func_decl.param_type_hints;
            int param_count = node->func_decl.param_count;
            TypeInfo* return_type_hint = node->func_decl.return_type_hint;
            ASTNode* body = node->func_decl.body;  // NULL for external functions
            bool is_variadic = node->func_decl.is_variadic;

            // Register function in symbol table
            symbol_table_insert_func_declaration(symbols, func_name, node);

            // Create function type in TypeContext
            if (type_ctx) {
                TypeInfo* func_type = type_context_create_function_type(
                    type_ctx,
                    func_name,
                    param_type_hints,
                    param_count,
                    return_type_hint,
                    body,
                    is_variadic
                );

                // Store the function declaration node in the TypeInfo
                func_type->data.function.func_decl_node = node;

                log_verbose("Created %sfunction type: %s", body ? "" : "external ", func_type->type_name);

                // If fully typed (external functions have no body and are always fully typed)
                if (func_type->data.function.is_fully_typed) {
                    FunctionSpecialization* spec = type_context_add_specialization(
                        type_ctx, func_type,
                        param_type_hints,
                        param_count
                    );

                    if (spec) {
                        // Use original name instead of specialized name
                        free(spec->specialized_name);
                        spec->specialized_name = strdup(func_name);

                        // Set return type
                        spec->return_type_info = return_type_hint;

                        // For user functions with bodies, clone only the body and run type inference
                        if (body) {
                            // Clone only the body (not the entire function)
                            ASTNode* cloned_body = ast_clone(body);

                            // Run type inference on the body with known parameter types
                            SymbolTable* temp_symbols = symbol_table_create(symbols);
                            for (int i = 0; i < param_count; i++) {
                                symbol_table_insert(temp_symbols,
                                                  node->func_decl.params[i],
                                                  param_type_hints[i], NULL, false);
                            }
                            infer_literal_types(cloned_body, temp_symbols, NULL);
                            symbol_table_free(temp_symbols);

                            spec->specialized_body = cloned_body;
                        } else {
                            // External functions have no body
                            spec->specialized_body = NULL;
                        }

                        log_verbose("Created single specialization for %sfunction: %s",
                                  body ? "fully typed " : "external ", func_name);
                    }
                }

                // Update the symbol entry to include the TypeInfo
                SymbolEntry* entry = symbol_table_lookup(symbols, func_name);
                if (entry) {
                    entry->type_info = func_type;
                }
            }
            break;
        }

        default:
            break;
    }
}

// Pass 2: Infer literal and obvious types
static void infer_literal_types(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                infer_literal_types(node->program.statements[i], symbols, type_ctx);
            }
            break;

        case AST_NUMBER:
            // Already set by parser
            break;

        case AST_STRING:
            // REMOVED: get_node_value_type(node) = TYPE_STRING;
            break;

        case AST_BOOLEAN:
            // REMOVED: get_node_value_type(node) = TYPE_BOOL;
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                infer_literal_types(node->var_decl.init, symbols, type_ctx);

                // If type hint is provided, validate it matches the initialization value
                if (node->var_decl.type_hint) {
                    TypeInfo* declared_type = node->var_decl.type_hint;
                    TypeInfo* inferred_type = node->var_decl.init->type_info;

                    // Check for type mismatch
                    if (type_info_is_unknown(inferred_type) && inferred_type != declared_type) {
                        // Allow int -> double promotion
                        if (!(declared_type == Type_Double && inferred_type == Type_Int)) {
                            log_error_at(&node->loc,
                                "Type mismatch: variable '%s' declared as %s but initialized with %s",
                                node->var_decl.name,
                                declared_type->type_name,
                                inferred_type->type_name);
                        }
                    }

                    // For objects, validate structure matches if both are objects
                    if (type_info_is_object(declared_type) && type_info_is_object(inferred_type) &&
                        node->var_decl.init->type == AST_OBJECT_LITERAL &&
                        node->var_decl.init->type_info) {

                        TypeInfo* declared_info = node->var_decl.type_hint;
                        TypeInfo* actual_info = node->var_decl.init->type_info;

                        // Validate property count matches
                        if (declared_info->data.object.property_count != actual_info->data.object.property_count) {
                            log_error_at(&node->loc,
                                "Object property count mismatch: expected %d properties but got %d",
                                declared_info->data.object.property_count, actual_info->data.object.property_count);
                        }

                        // Validate each property
                        for (int i = 0; i < declared_info->data.object.property_count && i < actual_info->data.object.property_count; i++) {
                            // Check property name
                            if (strcmp(declared_info->data.object.property_names[i], actual_info->data.object.property_names[i]) != 0) {
                                log_error_at(&node->loc,
                                    "Property name mismatch: expected '%s' but got '%s'",
                                    declared_info->data.object.property_names[i], actual_info->data.object.property_names[i]);
                            }

                            // Check property type
                            // TODO: utiliti fucntion to check if same
                            TypeInfo* declared_prop_type = declared_info->data.object.property_types[i];
                            TypeInfo* actual_prop_type = actual_info->data.object.property_types[i];
                            if (declared_prop_type != actual_prop_type) {
                                log_error_at(&node->loc,
                                    "Property '%s' type mismatch: expected %s but got %s",
                                    declared_info->data.object.property_names[i],
                                    declared_prop_type->type_name,
                                    actual_prop_type->type_name);
                            }
                        }
                    }

                    // Use the declared type
                    node->type_info = declared_type;
                } else {
                    // No type hint - infer from initialization
                    node->type_info = node->var_decl.init->type_info;
                }

                // Special case: if assigning a function identifier, copy the function's node reference
                if (node->var_decl.init->type == AST_IDENTIFIER && type_info_is_function_ctx(node->type_info)) {
                    SymbolEntry* func_entry = symbol_table_lookup(symbols, node->var_decl.init->identifier.name);
                    if (func_entry && func_entry->node) {
                        // Insert with function's node so analyze_call_sites can trace back to function decl
                        symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->type_info, node->var_decl.is_const, func_entry->node);
                    } else {
                        symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->type_info, node->var_decl.is_const, node);
                    }
                } else {
                    // Use the new function that stores the AST node (needed for object member access type inference)
                    symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->type_info, node->var_decl.is_const, node);
                }

                // Store TypeInfo in symbol table
                SymbolEntry* entry = symbol_table_lookup(symbols, node->var_decl.name);
                if (entry) {
                    if (node->var_decl.type_hint && type_info_is_object(node->var_decl.type_hint)) {
                        // Use the declared type info
                        entry->type_info = type_info_clone(node->var_decl.type_hint);
                        log_verbose("Variable '%s' assigned declared object type with %d properties",
                                   node->var_decl.name, entry->type_info->data.object.property_count);
                    } else if (node->var_decl.init->type == AST_OBJECT_LITERAL && node->var_decl.init->type_info) {
                        // Use inferred type info from literal
                        entry->type_info = type_info_clone(node->var_decl.init->type_info);
                        log_verbose("Variable '%s' assigned inferred type '%s'",
                                   node->var_decl.name, entry->type_info->type_name);
                    }
                }
            } else if (node->var_decl.type_hint) {
                // Variable declared with type but no initialization
                // REMOVED: get_node_value_type(node) = node->var_decl.type_hint->base_type;
                symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->type_info, node->var_decl.is_const, node);

                // Store TypeInfo for objects
                if (type_info_is_object(node->var_decl.type_hint)) {
                    SymbolEntry* entry = symbol_table_lookup(symbols, node->var_decl.name);
                    if (entry) {
                        entry->type_info = type_info_clone(node->var_decl.type_hint);
                    }
                }
            }
            break;

        case AST_BINARY_OP:
            infer_literal_types(node->binary_op.left, symbols, type_ctx);
            infer_literal_types(node->binary_op.right, symbols, type_ctx);
            // Binary op type inferred from operands
            node->type_info = infer_binary_result_type(node->binary_op.op,
                                                       node->binary_op.left->type_info,
                                                       node->binary_op.right->type_info);
            break;

        case AST_UNARY_OP:
            infer_literal_types(node->unary_op.operand, symbols, type_ctx);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->type_info = Type_Bool;
            } else {
                node->type_info = node->unary_op.operand->type_info;
            }
            break;

        case AST_CALL:
            for (int i = 0; i < node->call.arg_count; i++) {
                infer_literal_types(node->call.args[i], symbols, type_ctx);
            }
            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;
                const SymbolEntry* entry = symbol_table_lookup(symbols, func_name);
                // no user function
                if (!entry) {
                	node->type_info = runtime_get_function_type(func_name);
                }
            }
            break;
        case AST_ASSIGNMENT:
            infer_literal_types(node->assignment.value, symbols, type_ctx);
            node->type_info = node->assignment.value->type_info;
            break;

        case AST_MEMBER_ASSIGNMENT: {
            // Infer types for object and value
            infer_literal_types(node->member_assignment.object, symbols, type_ctx);
            infer_literal_types(node->member_assignment.value, symbols, type_ctx);

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
                            TypeInfo* prop_type = obj_lit->object_literal.values[i]->type_info;
                            TypeInfo* assigned_type = node->member_assignment.value->type_info;

                            if (prop_type != assigned_type) {
                                log_error_at(&node->loc,
                                    "Type mismatch: cannot assign %s to property '%s' of type %s",
                                    assigned_type ? assigned_type->type_name : "unknown",
                                    node->member_assignment.property,
                                    prop_type ? prop_type->type_name : "unknown");
                            }
                            break;
                        }
                    }
                }
            }

            // REMOVED: get_node_value_type(node) = get_node_value_type(node->member_assignment.value);
            break;
        }

        case AST_TERNARY:
            infer_literal_types(node->ternary.condition, symbols, type_ctx);
            infer_literal_types(node->ternary.true_expr, symbols, type_ctx);
            infer_literal_types(node->ternary.false_expr, symbols, type_ctx);
            // Determine result type based on both branches
            if (node->ternary.true_expr->type_info == node->ternary.false_expr->type_info) {
                node->type_info = node->ternary.true_expr->type_info;
            } else if ((node->ternary.true_expr->type_info == Type_Double &&
                        node->ternary.false_expr->type_info == Type_Int) ||
                       (node->ternary.true_expr->type_info == Type_Int &&
                        node->ternary.false_expr->type_info == Type_Double)) {
                node->type_info = Type_Double;
            } else {
                node->type_info = Type_Unknown;
            }
            break;

        case AST_IF:
            infer_literal_types(node->if_stmt.condition, symbols, type_ctx);
            infer_literal_types(node->if_stmt.then_branch, symbols, type_ctx);
            if (node->if_stmt.else_branch) {
                infer_literal_types(node->if_stmt.else_branch, symbols, type_ctx);
            }
            break;

        case AST_FOR:
            if (node->for_stmt.init) infer_literal_types(node->for_stmt.init, symbols, type_ctx);
            if (node->for_stmt.condition) infer_literal_types(node->for_stmt.condition, symbols, type_ctx);
            if (node->for_stmt.update) infer_literal_types(node->for_stmt.update, symbols, type_ctx);
            infer_literal_types(node->for_stmt.body, symbols, type_ctx);
            break;

        case AST_WHILE:
            infer_literal_types(node->while_stmt.condition, symbols, type_ctx);
            infer_literal_types(node->while_stmt.body, symbols, type_ctx);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                infer_literal_types(node->return_stmt.value, symbols, type_ctx);
                // REMOVED: get_node_value_type(node) = get_node_value_type(node->return_stmt.value);
            }
            break;

        case AST_PREFIX_OP:
        case AST_POSTFIX_OP: {
            // ++i or i++ should have the same type as the variable
            const char* var_name = (node->type == AST_PREFIX_OP) ?
                                   node->prefix_op.name : node->postfix_op.name;
            SymbolEntry* entry = symbol_table_lookup(symbols, var_name);
            if (entry) {
                // REMOVED: get_node_value_type(node) = entry->type;
            } else {
                // Variable not yet defined - will be caught later
                // REMOVED: get_node_value_type(node) = TYPE_INT; // Default to int for now
            }
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_literal_types(node->compound_assignment.value, symbols, type_ctx);
            // Result type should match the variable's type
            {
                SymbolEntry* entry = symbol_table_lookup(symbols, node->compound_assignment.name);
                if (entry) {
                    // REMOVED: get_node_value_type(node) = entry->type;
                } else {
                    // Variable not defined - will be caught later
                    // REMOVED: get_node_value_type(node) = get_node_value_type(node->compound_assignment.value);
                }
            }
            break;

        case AST_EXPR_STMT:
            infer_literal_types(node->expr_stmt.expression, symbols, type_ctx);
            break;

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
            if (entry) {
                node->type_info = entry->type_info;
            } else if (!type_info_is_unknown(node->type_info)) {
                // Only report error on first encounter (when type is not yet UNKNOWN)
                log_error_at(&node->loc, "Undefined variable: %s", node->identifier.name);
                node->type_info = Type_Unknown;
            }
            break;
        }

        case AST_ARRAY_LITERAL:
            // Infer types of all elements
            for (int i = 0; i < node->array_literal.count; i++) {
                infer_literal_types(node->array_literal.elements[i], symbols, type_ctx);
            }
            // Determine array type from first element
            if (node->array_literal.count > 0) {
                // Array type determined from first element
                // Type inference handled by type_info
            } else {
                // REMOVED: get_node_value_type(node) = TYPE_ARRAY_INT; // Empty array defaults to int
            }
            break;

        case AST_INDEX_ACCESS:
            infer_literal_types(node->index_access.object, symbols, type_ctx);
            infer_literal_types(node->index_access.index, symbols, type_ctx);
            // Determine result type based on object type
            if (node->index_access.object->type_info == Type_String) {
                node->type_info = Type_String;
            } else if (node->index_access.object->type_info == Type_Array_Int) {
                node->type_info = Type_Int;
            } else if (node->index_access.object->type_info == Type_Array_Double) {
                node->type_info = Type_Double;
            } else if (node->index_access.object->type_info == Type_Array_String) {
                node->type_info = Type_String;
            } else if (node->index_access.object->type_info == Type_Array_Bool) {
                node->type_info = Type_Bool;
            }
            break;

        case AST_INDEX_ASSIGNMENT:
            infer_literal_types(node->index_assignment.object, symbols, type_ctx);
            infer_literal_types(node->index_assignment.index, symbols, type_ctx);
            infer_literal_types(node->index_assignment.value, symbols, type_ctx);
            // Assignment returns the assigned value's type
            // REMOVED: get_node_value_type(node) = node->index_assignment.get_node_value_type(value);
            break;

        case AST_OBJECT_LITERAL: {
            // Infer types of all property values first
            for (int i = 0; i < node->object_literal.count; i++) {
                infer_literal_types(node->object_literal.values[i], symbols, type_ctx);
            }

            // Create TypeInfo with structural sharing (TypeContext owns the allocation)
            if (type_ctx) {
                node->type_info = type_context_create_object_type_from_literal(type_ctx, node);
            } else {
                // Fallback: create without context (shouldn't happen in normal flow)
                TypeInfo* type_info = type_info_create_from_object_literal(node);
                type_info->type_name = generate_type_name();
                node->type_info = type_info;
            }

            if (node->type_info) {
                log_verbose("Object literal assigned type '%s' with %d properties",
                           node->type_info->type_name, node->type_info->data.object.property_count);
            }
            break;
        }

        case AST_MEMBER_ACCESS: {
            infer_literal_types(node->member_access.object, symbols, type_ctx);

            // Try to infer the type from the object literal
            ASTNode* obj = node->member_access.object;
            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry && entry->type_info) {
                    // Use TypeInfo to find the property type
                    int prop_index = type_info_find_property(entry->type_info, node->member_access.property);
                    if (prop_index >= 0) {
                        node->type_info = entry->type_info->data.object.property_types[prop_index];
                        break;
                    }
                }
            }

            // Couldn't determine type
            node->type_info = Type_Unknown;
            break;
        }

        default:
            break;
    }
}

static void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node, TypeInfo** arg_types, SymbolTable* symbols, TypeContext* ctx) {
    if (!spec || !original_func_node || original_func_node->type != AST_FUNCTION_DECL) {
        return;
    }

    // Clone only the body (not the entire function)
    ASTNode* cloned_body = ast_clone(original_func_node->func_decl.body);

    SymbolTable* temp_symbols = symbol_table_create(symbols);
    // Insert parameters with their concrete types AND TypeInfo for objects
    for (int i = 0; i < spec->param_count; i++) {
        symbol_table_insert(temp_symbols, original_func_node->func_decl.params[i], spec->param_type_info[i], NULL, false);

        // TypeInfo is already set by symbol_table_insert if param_type_info is available
        if (type_info_is_object(arg_types[i]) && spec->param_type_info[i]) {
            SymbolEntry* entry = symbol_table_lookup(temp_symbols, original_func_node->func_decl.params[i]);
            if (entry && !entry->type_info) {
                entry->type_info = type_info_clone(spec->param_type_info[i]);
                log_verbose("  Parameter '%s' in temp_symbols assigned type '%s'",
                           original_func_node->func_decl.params[i], entry->type_info->type_name);
            }
        }
    }
    infer_literal_types(cloned_body, temp_symbols, NULL);  // NULL type_ctx - objects inside functions won't create new types
    iterative_specialization_discovery(cloned_body, temp_symbols, ctx);

    // Infer return type from function body
    TypeInfo* inferred_return = infer_function_return_type_with_params(cloned_body, temp_symbols);
    log_verbose("  Inferred return type for %s: %s", spec->specialized_name,
                inferred_return ? inferred_return->type_name : "NULL");

    // If return type hint is provided, use it and validate
    if (original_func_node->func_decl.return_type_hint &&
        !type_info_is_unknown(original_func_node->func_decl.return_type_hint)) {
        spec->return_type_info = original_func_node->func_decl.return_type_hint;

        // Validate inferred return type matches the hint
        if (!type_info_is_unknown(inferred_return) && inferred_return != spec->return_type_info) {
            // Allow int -> double promotion
            if (!(spec->return_type_info == Type_Double && inferred_return == Type_Int)) {
                log_error("Function '%s' declared to return %s but returns %s",
                    original_func_node->func_decl.name,
                    spec->return_type_info ? spec->return_type_info->type_name : "unknown",
                    inferred_return ? inferred_return->type_name : "unknown");
            }
        }
    } else {
        // No hint - use inferred type
        spec->return_type_info = inferred_return;
    }

    free(temp_symbols);

    spec->specialized_body = cloned_body;

    const char* return_type_str = spec->return_type_info ? spec->return_type_info->type_name : "unknown";
    log_verbose_indent(2, "Analyzed %s with return type %s", spec->specialized_name, return_type_str);
}

// Pass 3: Analyze call sites to find needed specializations
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, TypeContext* ctx) {
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

                    if (func_decl->type == AST_FUNCTION_DECL) {
                        // Skip fully typed functions (including external) - they already have a specialization
                        if (entry->type_info && entry->type_info->data.function.is_fully_typed) {
                            break;
                        }

                        // Use the function's actual name for specialization
                        actual_func_name = func_decl->func_decl.name;
                    }

                    // Collect argument types
                    TypeInfo** arg_types = malloc(sizeof(TypeInfo*) * node->call.arg_count);
                    bool all_known = true;

                    for (int i = 0; i < node->call.arg_count; i++) {
                        // Prefer function parameter type hints over inferred argument types
                        if (func_decl->type == AST_FUNCTION_DECL &&
                            i < func_decl->func_decl.param_count &&
                            func_decl->func_decl.param_type_hints &&
                            func_decl->func_decl.param_type_hints[i]) {
                            arg_types[i] = func_decl->func_decl.param_type_hints[i];

                            // Validate argument matches declared type
                            TypeInfo* arg_value_type = node->call.args[i]->type_info;
                            if (!type_info_is_unknown(arg_value_type) && arg_value_type != arg_types[i]) {
                                // Allow int -> double promotion
                                if (!(arg_types[i] == Type_Double && arg_value_type == Type_Int)) {
                                    log_error_at(&node->loc,
                                        "Type mismatch in call to '%s': parameter %d expects %s but got %s",
                                        actual_func_name, i + 1,
                                        arg_types[i] ? arg_types[i]->type_name : "unknown",
                                        arg_value_type ? arg_value_type->type_name : "unknown");
                                }
                            }
                        } else {
                            arg_types[i] = node->call.args[i]->type_info;
                        }

                        if (type_info_is_unknown(arg_types[i])) {
                            all_known = false;
                        }
                    }

                    // Only add if all types are known
                    if (all_known && node->call.arg_count > 0) {
                        FunctionSpecialization* spec = specialization_context_add_by_type_info(ctx, actual_func_name, arg_types, node->call.arg_count);
                        if (spec) {
                        	// Populate TypeInfo for object arguments BEFORE creating body
                        	// (needed for return type inference)
                        	for (int i = 0; i < node->call.arg_count; i++) {
                        	    if (type_info_is_object(arg_types[i]) && !spec->param_type_info[i]) {
                        	        ASTNode* arg_node = node->call.args[i];
                        	        if (arg_node->type == AST_IDENTIFIER) {
                        	            SymbolEntry* entry = symbol_table_lookup(symbols, arg_node->identifier.name);
                        	            if (entry && entry->type_info) {
                        	                spec->param_type_info[i] = type_info_clone(entry->type_info);
                        	                log_verbose("Call site: Argument %d ('%s') assigned type '%s' for function '%s'",
                        	                           i, arg_node->identifier.name, entry->type_info->type_name, actual_func_name);
                        	            }
                        	        } else if (arg_node->type == AST_OBJECT_LITERAL && arg_node->type_info) {
                        	            spec->param_type_info[i] = type_info_clone(arg_node->type_info);
                        	            log_verbose("Call site: Argument %d (object literal) assigned type '%s' for function '%s'",
                        	                       i, arg_node->type_info->type_name, actual_func_name);
                        	        }
                        	    }
                        	}

                        	// Now create the body with TypeInfo available
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
static void create_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                create_specializations(node->program.statements[i], symbols, ctx);
            }
            break;

        case AST_FUNCTION_DECL: {
            // Check if this function has any specializations
            TypeInfo* func_type = type_context_find_function_type(ctx, node->func_decl.name);
            bool found_any = false;

            if (func_type && func_type->data.function.specializations) {
                found_any = true;
            }

            if (!found_any) {
                // No specializations - create scope with parameter types
                SymbolTable* func_scope = symbol_table_create(symbols);
                for (int i = 0; i < node->func_decl.param_count; i++) {
                    TypeInfo* param_type_info = (node->func_decl.param_type_hints && node->func_decl.param_type_hints[i])
                        ? node->func_decl.param_type_hints[i] : NULL;
                    symbol_table_insert(func_scope, node->func_decl.params[i],
                                      param_type_info, NULL, false);
                }

                // Infer return type
                TypeInfo* inferred_return = infer_function_return_type_with_params(
                    node->func_decl.body, func_scope);
                // Store inferred return type (or use hint if provided)
                if (!node->func_decl.return_type_hint || type_info_is_unknown(node->func_decl.return_type_hint)) {
                    node->func_decl.return_type_hint = inferred_return;
                }

                symbol_table_free(func_scope);
            }
            break;
        }

        default:
            break;
    }
}

// Pass 5: Final type inference with all specializations known
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx) {
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
                node->type_info = entry->type_info;
            }
            // Don't report error here - it's already reported in infer_literal_types
            break;
        }

        case AST_BINARY_OP:
            infer_with_specializations(node->binary_op.left, symbols, ctx);
            infer_with_specializations(node->binary_op.right, symbols, ctx);
            // Binary op type inferred from operands
            node->type_info = infer_binary_result_type(node->binary_op.op,
                                                       node->binary_op.left->type_info,
                                                       node->binary_op.right->type_info);
            break;

        case AST_UNARY_OP:
            infer_with_specializations(node->unary_op.operand, symbols, ctx);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->type_info = Type_Bool;
            } else {
                node->type_info = node->unary_op.operand->type_info;
            }
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                infer_with_specializations(node->var_decl.init, symbols, ctx);
                // REMOVED: get_node_value_type(node) = get_node_value_type(node->var_decl.init);
                node->type_info = node->var_decl.init->type_info;

                // Special case: if assigning a function, copy the node reference
                if (node->var_decl.init->type == AST_IDENTIFIER && type_info_is_function_ctx(node->type_info)) {
                    SymbolEntry* func_entry = symbol_table_lookup(symbols, node->var_decl.init->identifier.name);
                    if (func_entry && func_entry->node) {
                        // Insert variable with function's node so analyze_call_sites can trace back
                        symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->type_info, node->var_decl.is_const, func_entry->node);
                        break;
                    }
                }
            }
            symbol_table_insert(symbols, node->var_decl.name, node->type_info, NULL, node->var_decl.is_const);
            break;

        case AST_ASSIGNMENT:
            infer_with_specializations(node->assignment.value, symbols, ctx);
            node->type_info = node->assignment.value->type_info;
            break;

        case AST_TERNARY:
            infer_with_specializations(node->ternary.condition, symbols, ctx);
            infer_with_specializations(node->ternary.true_expr, symbols, ctx);
            infer_with_specializations(node->ternary.false_expr, symbols, ctx);
            // Determine result type based on both branches
            if (node->ternary.true_expr->type_info == node->ternary.false_expr->type_info) {
                node->type_info = node->ternary.true_expr->type_info;
            } else if ((node->ternary.true_expr->type_info == Type_Double &&
                        node->ternary.false_expr->type_info == Type_Int) ||
                       (node->ternary.true_expr->type_info == Type_Int &&
                        node->ternary.false_expr->type_info == Type_Double)) {
                node->type_info = Type_Double;
            } else {
                node->type_info = Type_Unknown;
            }
            break;

        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                infer_with_specializations(node->array_literal.elements[i], symbols, ctx);
            }
            // Determine array type from first element
            if (node->array_literal.count > 0) {
                TypeInfo* elem_type = node->array_literal.elements[0]->type_info;
                if (elem_type == Type_Int) {
                    node->type_info = Type_Array_Int;
                } else if (elem_type == Type_Double) {
                    node->type_info = Type_Array_Double;
                } else if (elem_type == Type_Bool) {
                    node->type_info = Type_Array_Bool;
                } else if (elem_type == Type_String) {
                    node->type_info = Type_Array_String;
                } else {
                    node->type_info = Type_Array_Int; // Default
                }
            } else {
                node->type_info = Type_Array_Int; // Empty array defaults to int
            }
            break;

        case AST_INDEX_ACCESS:
            infer_with_specializations(node->index_access.object, symbols, ctx);
            infer_with_specializations(node->index_access.index, symbols, ctx);
            // Determine result type based on object type
            if (node->index_access.object->type_info == Type_String) {
                node->type_info = Type_String;
            } else if (node->index_access.object->type_info == Type_Array_Int) {
                node->type_info = Type_Int;
            } else if (node->index_access.object->type_info == Type_Array_Double) {
                node->type_info = Type_Double;
            } else if (node->index_access.object->type_info == Type_Array_String) {
                node->type_info = Type_String;
            } else if (node->index_access.object->type_info == Type_Array_Bool) {
                node->type_info = Type_Bool;
            }
            break;

        case AST_INDEX_ASSIGNMENT:
            infer_with_specializations(node->index_assignment.object, symbols, ctx);
            infer_with_specializations(node->index_assignment.index, symbols, ctx);
            infer_with_specializations(node->index_assignment.value, symbols, ctx);
            // Assignment returns the assigned value's type
            // REMOVED: get_node_value_type(node) = node->index_assignment.get_node_value_type(value);
            break;

        case AST_CALL: {
            // Infer argument types
            for (int i = 0; i < node->call.arg_count; i++) {
                infer_with_specializations(node->call.args[i], symbols, ctx);
            }

            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;

                // Special handling for Array() constructor
                if (strcmp(func_name, "Array") == 0 && node->call.arg_count == 1) {
                    // Array(size) creates an int array by default
                    node->type_info = Type_Array_Int;
                    break;
                }

                // Get argument types
                TypeInfo** arg_types = malloc(sizeof(TypeInfo*) * node->call.arg_count);
                for (int i = 0; i < node->call.arg_count; i++) {
                    arg_types[i] = node->call.args[i]->type_info;
                }

                // Try to find user-defined function specialization
                FunctionSpecialization* spec = specialization_context_find_by_type_info(
                    ctx, func_name, arg_types, node->call.arg_count);

                if (spec) {
                    // Found user function specialization (includes fully typed functions)
                    node->type_info = spec->return_type_info;
                } else {
                    // Not a user function, check if it's a runtime builtin
                    TypeInfo* runtime_type = runtime_get_function_type(func_name);
                    if (!type_info_is_unknown(runtime_type)) {
                        node->type_info = runtime_type;
                    } else {
                        // Unknown function - default to void, will error in codegen if not found
                        node->type_info = Type_Void;
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
                    TypeInfo* runtime_type = runtime_get_function_type(full_name);
                    if (!type_info_is_unknown(runtime_type)) {
                        // REMOVED: get_node_value_type(node) = runtime_type;
                        break;
                    }
                }

                // Default for member access
                // REMOVED: get_node_value_type(node) = TYPE_VOID;
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
                // REMOVED: get_node_value_type(node) = get_node_value_type(node->return_stmt.value);
            }
            break;

        case AST_PREFIX_OP:
        case AST_POSTFIX_OP: {
            // ++i or i++ should have the same type as the variable
            const char* var_name = (node->type == AST_PREFIX_OP) ?
                                   node->prefix_op.name : node->postfix_op.name;
            SymbolEntry* entry = symbol_table_lookup(symbols, var_name);
            if (entry) {
                // REMOVED: get_node_value_type(node) = entry->type;
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
                    // REMOVED: get_node_value_type(node) = entry->type;
                } else {
                    // Variable not defined
                    // REMOVED: get_node_value_type(node) = get_node_value_type(node->compound_assignment.value);
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
            // Type info should already be set by infer_literal_types
            // Nothing extra needed here
            break;

        case AST_MEMBER_ACCESS: {
            infer_with_specializations(node->member_access.object, symbols, ctx);

            // Try to infer the type from TypeInfo
            ASTNode* obj = node->member_access.object;
            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry && entry->type_info) {
                    // Use TypeInfo to find the property type
                    int prop_index = type_info_find_property(entry->type_info, node->member_access.property);
                    if (prop_index >= 0) {
                        node->type_info = entry->type_info->data.object.property_types[prop_index];
                        break;
                    }
                }
            }

            // Couldn't determine type
            if (!node->type_info) {
                node->type_info = Type_Unknown;
            }
            break;
        }

        default:
            break;
    }
}

// Helper: Count total number of specializations across all function types
static void iterative_specialization_discovery(ASTNode*ast, SymbolTable* symbols, TypeContext* ctx) {
	int iteration = 0;
  int max_iterations = 100; // Safety limit to prevent infinite loops

  while (iteration < max_iterations) {
    size_t spec_count_before = ctx->specialization_count;

    log_verbose_indent(2, "Iteration %d: %zu specializations before", iteration, spec_count_before);

    // Pass 3: Analyze call sites to find needed specializations
    analyze_call_sites(ast, symbols, ctx);
    log_verbose_indent(2, "After analyze_call_sites: %zu specializations", ctx->specialization_count);

    // Pass 4: Create specialized function versions
    create_specializations(ast, symbols, ctx);
    log_verbose_indent(2, "After create_specializations: %zu specializations", ctx->specialization_count);

    // Pass 5: Propagate types with known specializations
    infer_with_specializations(ast, symbols, ctx);
    log_verbose_indent(2, "After infer_with_specializations: %zu specializations", ctx->specialization_count);

    size_t spec_count_after = ctx->specialization_count;

    // If no new specializations were discovered, we're done
    if (spec_count_after == spec_count_before) {
        log_verbose_indent(2, "Convergence reached after %d iteration(s)", iteration + 1);
        return;
    }

    log_verbose_indent(2, "Added %zu new specializations in iteration %d",
                      spec_count_after - spec_count_before, iteration);
    iteration++;
  }

  log_warning("Maximum iterations reached (%d), some types may be unresolved. Total specializations: %zu",
              max_iterations, ctx->specialization_count);
}

// Main entry point: Multi-pass type inference with specialization
void type_inference_with_context(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx) {
    if (!ast || !symbols || !type_ctx) return;

    log_verbose("Starting multi-pass type inference");

    // Pass 1: Collect function signatures
    log_verbose_indent(1, "Pass 1: Collecting function signatures");
    collect_function_signatures(ast, symbols, type_ctx);

    // Pass 2: Infer literal types
    log_verbose_indent(1, "Pass 2: Inferring literal types");
    infer_literal_types(ast, symbols, type_ctx);

    // Pass 3-5: Iteratively analyze and specialize until no new specializations found
    // This is needed because variable types depend on function return types,
    // which depend on specializations, which depend on call site argument types
    log_verbose_indent(1, "Pass 3-5: Iterative specialization discovery");
    iterative_specialization_discovery(ast, symbols, type_ctx);

    // Store type context for codegen (contains both types and specializations)
    ast->type_ctx = type_ctx;

    log_verbose("Type inference complete");
}
