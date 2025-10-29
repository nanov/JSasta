#include "jsasta_compiler.h"
#include "traits.h"
#include "operator_utils.h"
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

// Helper: Evaluate a constant expression to an integer value
// Returns -1 on error
// The 'evaluating' set tracks nodes being evaluated to detect circular dependencies
static int eval_const_expr_internal(ASTNode* expr, SymbolTable* symbols, ASTNode** evaluating, int eval_depth) {
    if (!expr) {
        log_error("NULL expression in const evaluation");
        return -1;
    }
    
    // Prevent infinite recursion
    if (eval_depth > 20) {
        log_error_at(&expr->loc, "Const expression recursion too deep (possible circular dependency)");
        return -1;
    }
    
    // Check for circular dependency
    for (int i = 0; i < eval_depth; i++) {
        if (evaluating[i] == expr) {
            log_error_at(&expr->loc, "Circular dependency detected in const expression");
            return -1;
        }
    }
    
    evaluating[eval_depth] = expr;
    
    switch (expr->type) {
        case AST_NUMBER: {
            // Check if it's an integer
            double value = expr->number.value;
            if (value != (int)value) {
                log_error_at(&expr->loc, "Array size must be an integer, got %.2f", value);
                return -1;
            }
            int int_val = (int)value;
            if (int_val <= 0) {
                log_error_at(&expr->loc, "Array size must be positive, got %d", int_val);
                return -1;
            }
            return int_val;
        }
            
        case AST_IDENTIFIER: {
            // Look up const variable
            SymbolEntry* entry = symbol_table_lookup(symbols, expr->identifier.name);
            if (!entry) {
                log_error_at(&expr->loc, "Undefined identifier '%s' in array size expression", expr->identifier.name);
                return -1;
            }
            if (!entry->is_const) {
                log_error_at(&expr->loc, "Variable '%s' is not declared as 'const' and cannot be used in array size expression", 
                           expr->identifier.name);
                log_error_at(&expr->loc, "  Hint: Change 'var %s' to 'const %s' if it's a compile-time constant", 
                           expr->identifier.name, expr->identifier.name);
                return -1;
            }
            if (!entry->node || entry->node->type != AST_VAR_DECL) {
                log_error_at(&expr->loc, "Const '%s' is not a variable declaration", expr->identifier.name);
                return -1;
            }
            if (!entry->node->var_decl.init) {
                log_error_at(&expr->loc, "Const '%s' has no initializer and cannot be evaluated", expr->identifier.name);
                return -1;
            }
            // Recursively evaluate the const's initializer
            // (Type checking will happen when we evaluate the initializer)
            return eval_const_expr_internal(entry->node->var_decl.init, symbols, evaluating, eval_depth + 1);
        }
        
        case AST_BINARY_OP: {
            int left = eval_const_expr_internal(expr->binary_op.left, symbols, evaluating, eval_depth + 1);
            int right = eval_const_expr_internal(expr->binary_op.right, symbols, evaluating, eval_depth + 1);
            if (left < 0 || right < 0) return -1;
            
            const char* op = expr->binary_op.op;
            int result;
            if (strcmp(op, "+") == 0) {
                result = left + right;
            } else if (strcmp(op, "-") == 0) {
                result = left - right;
            } else if (strcmp(op, "*") == 0) {
                result = left * right;
            } else if (strcmp(op, "/") == 0) {
                if (right == 0) {
                    log_error_at(&expr->loc, "Division by zero in array size expression");
                    return -1;
                }
                result = left / right;
            } else if (strcmp(op, "%") == 0) {
                if (right == 0) {
                    log_error_at(&expr->loc, "Modulo by zero in array size expression");
                    return -1;
                }
                result = left % right;
            } else {
                log_error_at(&expr->loc, "Operator '%s' is not supported in array size expressions", op);
                log_error_at(&expr->loc, "  Supported operators: + - * / %%");
                return -1;
            }
            
            if (result <= 0) {
                log_error_at(&expr->loc, "Array size expression evaluates to %d, but must be positive", result);
                return -1;
            }
            return result;
        }
        
        case AST_STRING:
            log_error_at(&expr->loc, "String literals cannot be used in array size expressions");
            return -1;
            
        case AST_BOOLEAN:
            log_error_at(&expr->loc, "Boolean values cannot be used in array size expressions");
            return -1;
            
        case AST_CALL:
            log_error_at(&expr->loc, "Function calls cannot be used in array size expressions");
            log_error_at(&expr->loc, "  Array sizes must be compile-time constants");
            return -1;
        
        default:
            log_error_at(&expr->loc, "This expression cannot be used in array size (must be a const integer expression)");
            return -1;
    }
}

// Wrapper function for const expression evaluation
static int eval_const_expr(ASTNode* expr, SymbolTable* symbols) {
    ASTNode* evaluating[20];  // Stack to track evaluation path
    return eval_const_expr_internal(expr, symbols, evaluating, 0);
}

// Helper: Infer type from binary operation using trait system
static TypeInfo* infer_binary_result_type(SourceLocation* node, const char* op, TypeInfo* left, TypeInfo* right) {
    log_verbose_at(node, "      infer_binary_result_type: %s op=%s %s",
                left ? left->type_name : "NULL", op, right ? right->type_name : "NULL");

    // Special handling for logical operators (not implemented as traits yet)
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        return Type_Bool;
    }

    // Special handling for string concatenation (will be implemented as trait later)
    if (strcmp(op, "+") == 0) {
        if (left == Type_String || right == Type_String) {
            return Type_String;
        }
    }

    // Use trait system to determine output type
    Trait* trait = operator_to_trait(op);
    if (trait && left && right) {
        TypeInfo* output = trait_get_binary_output(trait, left, right);
        if (output) {
            log_verbose("      Trait %s returned output type: %s", trait->name, output->type_name);
            return output;
        }
    }

    // Fallback to unknown if no trait implementation found
    log_verbose("      No trait implementation found for %s %s %s",
                left ? left->type_name : "NULL", op, right ? right->type_name : "NULL");
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
            return infer_binary_result_type(&node->loc, node->binary_op.op, left, right);
        }
        case AST_UNARY_OP: {
            TypeInfo* operand_type = infer_expr_type_simple(node->unary_op.operand, scope);
            if (strcmp(node->unary_op.op, "!") == 0) {
                return Type_Bool;
            } else if (strcmp(node->unary_op.op, "ref") == 0) {
                // ref operator creates a reference type
                TypeInfo* ref_type = type_info_create(TYPE_KIND_REF, NULL);
                ref_type->data.ref.target_type = operand_type;
                ref_type->data.ref.is_mutable = true;

                char type_name[256];
                snprintf(type_name, sizeof(type_name), "ref<%s>",
                        operand_type && operand_type->type_name ? operand_type->type_name : "?");
                ref_type->type_name = strdup(type_name);

                return ref_type;
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
            
            // Unwrap ref types to get the actual target type
            TypeInfo* target_type = type_info_get_ref_target(obj_type);
            
            // String indexing returns u8 (byte value)
            if (target_type == Type_String) return Type_U8;
            if (type_info_is_array(target_type)) return target_type->data.array.element_type;
            return Type_Unknown;
        }
        case AST_OBJECT_LITERAL:
            return node->type_info;
        case AST_MEMBER_ACCESS: {
            // Try to infer the property type using TypeInfo
            ASTNode* obj = node->member_access.object;
            TypeInfo* obj_type_info = NULL;

            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(scope, obj->identifier.name);
                if (entry) {
                    obj_type_info = entry->type_info;
                }
            } else if (obj->type == AST_MEMBER_ACCESS || obj->type == AST_INDEX_ACCESS) {
                // Nested member/index access - recursively get the type
                obj_type_info = infer_expr_type_simple(obj, scope);
            }

            // Unwrap ref types to get the actual object type
            if (obj_type_info && type_info_is_ref(obj_type_info)) {
                obj_type_info = type_info_get_ref_target(obj_type_info);
            }

            if (obj_type_info && type_info_is_object(obj_type_info)) {
                // Use TypeInfo to find the property type
                int prop_index = type_info_find_property(obj_type_info, node->member_access.property);
                if (prop_index >= 0) {
                    return obj_type_info->data.object.property_types[prop_index];
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

        case AST_BREAK:
        case AST_CONTINUE:
            // Break and continue don't have a type
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

// Pass 0: Collect struct declarations (before functions, so functions can use struct types)
static void collect_struct_declarations(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                collect_struct_declarations(node->program.statements[i], symbols, type_ctx);
            }
            break;

        case AST_STRUCT_DECL: {
            const char* struct_name = node->struct_decl.name;
            char** property_names = node->struct_decl.property_names;
            TypeInfo** property_types = node->struct_decl.property_types;
            ASTNode** default_values = node->struct_decl.default_values;
            int property_count = node->struct_decl.property_count;

            // Validate and infer types for default values
            for (int i = 0; i < property_count; i++) {
                if (default_values[i]) {
                    // Infer the literal's type
                    infer_literal_types(default_values[i], symbols, NULL);

                    // Check if default value type matches property type
                    TypeInfo* default_type = default_values[i]->type_info;
                    TypeInfo* prop_type = property_types[i];

                    if (default_type != prop_type) {
                        // Allow int -> double promotion
                        if (!(prop_type == Type_Double && default_type == Type_Int)) {
                            log_error_at(&node->loc,
                                "Type mismatch in struct '%s': property '%s' has type %s but default value has type %s",
                                struct_name, property_names[i],
                                prop_type ? prop_type->type_name : "unknown",
                                default_type ? default_type->type_name : "unknown");
                        }
                    }
                }
            }

            // Register struct type in TypeContext (if not already registered during parsing)
            if (type_ctx) {
                // Check if already registered during parsing
                TypeInfo* existing = type_context_find_struct_type(type_ctx, struct_name);
                if (!existing) {
                    TypeInfo* struct_type = type_context_create_struct_type(
                        type_ctx,
                        struct_name,
                        property_names,
                        property_types,
                        property_count,
                        node  // Pass the struct declaration node for default values
                    );

                    if (struct_type) {
                        log_verbose("Registered struct type during type inference: %s with %d properties",
                                   struct_name, property_count);
                    }
                } else {
                    log_verbose("Struct type already registered: %s", struct_name);
                }
            }

            // Process methods: create global functions with mangled names
            for (int i = 0; i < node->struct_decl.method_count; i++) {
                ASTNode* method = node->struct_decl.methods[i];

                // Create mangled name: struct_name.method_name
                char* mangled_name = (char*)malloc(strlen(struct_name) + strlen(method->func_decl.name) + 2);
                sprintf(mangled_name, "%s.%s", struct_name, method->func_decl.name);

                // Update the method's name to the mangled name
                free(method->func_decl.name);
                method->func_decl.name = mangled_name;

                log_verbose("Registered method as global function: %s", mangled_name);
            }

            break;
        }

        default:
            break;
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
                            infer_literal_types(cloned_body, temp_symbols, type_ctx);
                            // Note: Don't free temp_symbols - it's the parent of cloned_body's symbol_table

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

        case AST_STRUCT_DECL: {
            // Process methods as global functions
            for (int i = 0; i < node->struct_decl.method_count; i++) {
                ASTNode* method = node->struct_decl.methods[i];
                // Process each method as a regular function
                collect_function_signatures(method, symbols, type_ctx);
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
            // AST_PROGRAM uses the passed-in symbols (top-level scope)
            for (int i = 0; i < node->program.count; i++) {
                infer_literal_types(node->program.statements[i], symbols, type_ctx);
            }
            break;

        case AST_BLOCK: {
            // AST_BLOCK creates a new scope with the current scope as parent
            SymbolTable* block_symbols = symbol_table_create(symbols);
            node->symbol_table = block_symbols;
            for (int i = 0; i < node->block.count; i++) {
                infer_literal_types(node->block.statements[i], block_symbols, type_ctx);
            }
            break;
        }

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
            // Evaluate const expression for array size
            if (node->var_decl.array_size_expr) {
                int size = eval_const_expr(node->var_decl.array_size_expr, symbols);
                if (size < 0) {
                    log_error_at(&node->loc, "Invalid array size expression");
                    node->var_decl.array_size = 0;
                } else {
                    node->var_decl.array_size = size;
                }
            }
            
            if (node->var_decl.init) {
                // Special case: if we have a struct type hint and object literal,
                // skip normal type inference to avoid creating anonymous types
                bool is_struct_literal = (node->var_decl.type_hint &&
                                         type_info_is_object(node->var_decl.type_hint) &&
                                         node->var_decl.type_hint->data.object.struct_decl_node &&
                                         node->var_decl.init->type == AST_OBJECT_LITERAL);

                if (!is_struct_literal) {
                    infer_literal_types(node->var_decl.init, symbols, type_ctx);
                }

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

                    // For objects (especially structs), validate and fill in default values
                    if (type_info_is_object(declared_type) &&
                        node->var_decl.init->type == AST_OBJECT_LITERAL) {

                        TypeInfo* declared_info = node->var_decl.type_hint;
                        ASTNode* obj_literal = node->var_decl.init;

                        // If this is a struct, we need to infer types for the property values
                        // with contextual typing from the expected struct field types
                        if (is_struct_literal) {
                            for (int i = 0; i < obj_literal->object_literal.count; i++) {
                                // Find the expected type for this property
                                const char* prop_key = obj_literal->object_literal.keys[i];
                                TypeInfo* expected_prop_type = NULL;

                                for (int j = 0; j < declared_info->data.object.property_count; j++) {
                                    if (strcmp(declared_info->data.object.property_names[j], prop_key) == 0) {
                                        expected_prop_type = declared_info->data.object.property_types[j];
                                        break;
                                    }
                                }

                                // Apply contextual typing to literals
                                ASTNode* value = obj_literal->object_literal.values[i];
                                if (value->type == AST_NUMBER && expected_prop_type && type_info_is_integer(expected_prop_type)) {
                                    // Set the literal to the expected type directly
                                    value->type_info = expected_prop_type;
                                } else {
                                    infer_literal_types(value, symbols, type_ctx);
                                }
                            }
                        }

                        // Check if this is a struct type with default values
                        ASTNode* struct_decl = declared_info->data.object.struct_decl_node;

                        // Build a map of provided properties
                        bool* provided = (bool*)calloc(declared_info->data.object.property_count, sizeof(bool));

                        // Validate provided properties and mark them
                        for (int i = 0; i < obj_literal->object_literal.count; i++) {
                            const char* provided_key = obj_literal->object_literal.keys[i];
                            bool found = false;

                            // Find this property in the struct definition
                            for (int j = 0; j < declared_info->data.object.property_count; j++) {
                                if (strcmp(declared_info->data.object.property_names[j], provided_key) == 0) {
                                    found = true;
                                    provided[j] = true;

                                    // Validate type
                                    TypeInfo* expected_type = declared_info->data.object.property_types[j];
                                    TypeInfo* actual_type = obj_literal->object_literal.values[i]->type_info;
                                    if (expected_type != actual_type) {
                                        // Allow safe type conversions:
                                        // 1. int -> double promotion
                                        // 2. any integer type -> any other integer type (will be handled by LLVM cast)
                                        bool allow_conversion = false;

                                        if (expected_type == Type_Double && actual_type == Type_Int) {
                                            allow_conversion = true;
                                        } else if (type_info_is_integer(expected_type) && type_info_is_integer(actual_type)) {
                                            // Allow any integer to integer conversion (i32 -> u8, i32 -> u64, etc.)
                                            allow_conversion = true;
                                        }

                                        if (!allow_conversion) {
                                            log_error_at(&node->loc,
                                                "Property '%s' type mismatch: expected %s but got %s",
                                                provided_key,
                                                expected_type->type_name,
                                                actual_type->type_name);
                                        }
                                    }
                                    break;
                                }
                            }

                            if (!found) {
                                log_error_at(&node->loc,
                                    "Unknown property '%s' in struct '%s'",
                                    provided_key, declared_info->type_name);
                            }
                        }

                        // Rebuild the object literal with properties in the correct struct order
                        if (struct_decl && struct_decl->type == AST_STRUCT_DECL) {
                            char** new_keys = (char**)malloc(sizeof(char*) * declared_info->data.object.property_count);
                            ASTNode** new_values = (ASTNode**)malloc(sizeof(ASTNode*) * declared_info->data.object.property_count);

                            for (int i = 0; i < declared_info->data.object.property_count; i++) {
                                new_keys[i] = strdup(declared_info->data.object.property_names[i]);

                                if (provided[i]) {
                                    // Find this property in the original object literal
                                    for (int j = 0; j < obj_literal->object_literal.count; j++) {
                                        if (strcmp(obj_literal->object_literal.keys[j], declared_info->data.object.property_names[i]) == 0) {
                                            new_values[i] = obj_literal->object_literal.values[j];
                                            break;
                                        }
                                    }
                                } else {
                                    // Property is missing - use default value
                                    if (struct_decl->struct_decl.default_values[i]) {
                                        new_values[i] = ast_clone(struct_decl->struct_decl.default_values[i]);
                                        log_verbose("Filled in default value for property '%s' in struct '%s'",
                                                   declared_info->data.object.property_names[i],
                                                   declared_info->type_name);
                                    } else {
                                        // No default value - this is an error
                                        log_error_at(&node->loc,
                                            "Missing required property '%s' in struct '%s' (no default value)",
                                            declared_info->data.object.property_names[i],
                                            declared_info->type_name);
                                        new_values[i] = NULL;
                                    }
                                }
                            }

                            // Free old arrays (but not the values we're keeping)
                            for (int i = 0; i < obj_literal->object_literal.count; i++) {
                                free(obj_literal->object_literal.keys[i]);
                            }
                            free(obj_literal->object_literal.keys);
                            free(obj_literal->object_literal.values);

                            // Replace with new ordered arrays
                            obj_literal->object_literal.keys = new_keys;
                            obj_literal->object_literal.values = new_values;
                            obj_literal->object_literal.count = declared_info->data.object.property_count;
                        }

                        free(provided);

                        // For structs, use the struct type directly instead of creating anonymous type
                        if (struct_decl && struct_decl->type == AST_STRUCT_DECL) {
                            obj_literal->type_info = declared_info;
                            log_verbose("Assigned struct type '%s' to object literal (no anonymous type created)",
                                       declared_info->type_name);
                        } else {
                            // For non-struct object types, re-infer the type
                            if (type_ctx) {
                                obj_literal->type_info = type_context_create_object_type_from_literal(type_ctx, obj_literal);
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
                    // Store pointer to symbol entry in the AST node for fast access
                    node->var_decl.symbol_entry = entry;

                    if (node->var_decl.type_hint && (type_info_is_object(node->var_decl.type_hint) || type_info_is_ref(node->var_decl.type_hint))) {
                        // Use the declared type info (for both objects and refs)
                        entry->type_info = type_info_clone(node->var_decl.type_hint);
                        if (type_info_is_ref(node->var_decl.type_hint)) {
                            log_verbose("Variable '%s' assigned declared ref type '%s'",
                                       node->var_decl.name, entry->type_info->type_name);
                        } else {
                            log_verbose("Variable '%s' assigned declared object type with %d properties",
                                       node->var_decl.name, entry->type_info->data.object.property_count);
                        }
                    } else if (node->var_decl.init->type == AST_OBJECT_LITERAL && node->var_decl.init->type_info) {
                        // Use inferred type info from literal
                        entry->type_info = type_info_clone(node->var_decl.init->type_info);
                        log_verbose("Variable '%s' assigned inferred type '%s'",
                                   node->var_decl.name, entry->type_info->type_name);
                    }
                }
            } else if (node->var_decl.type_hint) {
                // Variable declared with type but no initialization
                node->type_info = node->var_decl.type_hint;

                symbol_table_insert_var_declaration(symbols, node->var_decl.name, node->type_info, node->var_decl.is_const, node);

                // Store TypeInfo for objects
                if (type_info_is_object(node->var_decl.type_hint)) {
                    SymbolEntry* entry = symbol_table_lookup(symbols, node->var_decl.name);
                    if (entry) {
                        // Store pointer to symbol entry in the AST node for fast access
                        node->var_decl.symbol_entry = entry;
                        entry->type_info = type_info_clone(node->var_decl.type_hint);
                    }
                } else {
                    // Still store the symbol entry pointer even for non-objects
                    SymbolEntry* entry = symbol_table_lookup(symbols, node->var_decl.name);
                    if (entry) {
                        node->var_decl.symbol_entry = entry;
                    }
                }
            }
            break;

        case AST_BINARY_OP:
            infer_literal_types(node->binary_op.left, symbols, type_ctx);
            infer_literal_types(node->binary_op.right, symbols, type_ctx);
            // Binary op type inferred from operands
            node->type_info = infer_binary_result_type(&node->loc, node->binary_op.op,
                                                       node->binary_op.left->type_info,
                                                       node->binary_op.right->type_info);
            break;

        case AST_UNARY_OP:
            infer_literal_types(node->unary_op.operand, symbols, type_ctx);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->type_info = Type_Bool;
            } else if (strcmp(node->unary_op.op, "ref") == 0) {
                // ref operator creates a reference type
                TypeInfo* operand_type = node->unary_op.operand->type_info;
                TypeInfo* ref_type = type_info_create(TYPE_KIND_REF, NULL);
                ref_type->data.ref.target_type = operand_type;
                ref_type->data.ref.is_mutable = true;  // Default to mutable

                // Generate type name like "ref<termios>"
                char type_name[256];
                snprintf(type_name, sizeof(type_name), "ref<%s>",
                        operand_type && operand_type->type_name ? operand_type->type_name : "?");
                ref_type->type_name = strdup(type_name);

                node->type_info = ref_type;
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

        case AST_METHOD_CALL:
            // Determine if this is a static or instance method first
            // Static: object is an identifier that refers to a type name
            // Instance: object is a variable/expression
            if (node->method_call.object->type == AST_IDENTIFIER) {
                const char* name = node->method_call.object->identifier.name;
                // Check if it's a type name (struct)
                TypeInfo* type = type_context_find_struct_type(type_ctx, name);
                if (type) {
                    node->method_call.is_static = true;
                    // Store the type in the object node for easy access in codegen
                    node->method_call.object->type_info = type;
                } else {
                    node->method_call.is_static = false;
                    // Infer type for the object - it's a variable
                    infer_literal_types(node->method_call.object, symbols, type_ctx);
                }
            } else {
                node->method_call.is_static = false;
                // Infer type for the object - it's an expression
                infer_literal_types(node->method_call.object, symbols, type_ctx);
            }

            // Infer types for arguments
            for (int i = 0; i < node->method_call.arg_count; i++) {
                infer_literal_types(node->method_call.args[i], symbols, type_ctx);
            }
            break;

        case AST_ASSIGNMENT:
            infer_literal_types(node->assignment.value, symbols, type_ctx);
            node->type_info = node->assignment.value->type_info;
            // Store pointer to the symbol entry for fast access in codegen
            node->assignment.symbol_entry = symbol_table_lookup(symbols, node->assignment.name);
            break;

        case AST_MEMBER_ASSIGNMENT: {
            // Infer types for object
            infer_literal_types(node->member_assignment.object, symbols, type_ctx);

            // Apply contextual typing to the value if it's a literal
            ASTNode* obj = node->member_assignment.object;
            TypeInfo* expected_prop_type = NULL;

            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);

                // Check if it's a struct with type hint
                if (entry && entry->node && entry->node->type == AST_VAR_DECL) {
                    TypeInfo* var_type = entry->node->var_decl.type_hint;
                    if (var_type && type_info_is_object(var_type)) {
                        // Find the property type in the struct definition
                        int prop_idx = type_info_find_property(var_type, node->member_assignment.property);
                        if (prop_idx >= 0) {
                            expected_prop_type = var_type->data.object.property_types[prop_idx];
                        }
                    }
                }
            }

            // Apply contextual typing to number literals
            if (node->member_assignment.value->type == AST_NUMBER && expected_prop_type && type_info_is_integer(expected_prop_type)) {
                node->member_assignment.value->type_info = expected_prop_type;
            } else {
                infer_literal_types(node->member_assignment.value, symbols, type_ctx);
            }

            // Type check: verify the assigned value matches the property type
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
                                // Allow safe integer conversions
                                bool allow_conversion = false;
                                if (type_info_is_integer(prop_type) && type_info_is_integer(assigned_type)) {
                                    allow_conversion = true;
                                }

                                if (!allow_conversion) {
                                    log_error_at(&node->loc,
                                        "Type mismatch: cannot assign %s to property '%s' of type %s",
                                        assigned_type ? assigned_type->type_name : "unknown",
                                        node->member_assignment.property,
                                        prop_type ? prop_type->type_name : "unknown");
                                }
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

        case AST_FOR: {
            // For loops create their own scope for variables declared in init
            SymbolTable* for_scope = symbol_table_create(symbols);
            node->symbol_table = for_scope;

            if (node->for_stmt.init) infer_literal_types(node->for_stmt.init, for_scope, type_ctx);
            if (node->for_stmt.condition) infer_literal_types(node->for_stmt.condition, for_scope, type_ctx);
            if (node->for_stmt.update) infer_literal_types(node->for_stmt.update, for_scope, type_ctx);
            infer_literal_types(node->for_stmt.body, for_scope, type_ctx);
            break;
        }

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

        case AST_BREAK:
        case AST_CONTINUE:
            // Nothing to infer for break/continue
            break;

        case AST_PREFIX_OP:
        case AST_POSTFIX_OP: {
            // Infer type of the target (if it's a member/index access)
            ASTNode* target = (node->type == AST_PREFIX_OP) ?
                              node->prefix_op.target : node->postfix_op.target;
            if (target) {
                infer_literal_types(target, symbols, type_ctx);
            }
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_literal_types(node->compound_assignment.value, symbols, type_ctx);

            // Infer type of the target (if it's a member/index access)
            if (node->compound_assignment.target) {
                infer_literal_types(node->compound_assignment.target, symbols, type_ctx);
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

        case AST_INDEX_ACCESS: {
            infer_literal_types(node->index_access.object, symbols, type_ctx);
            infer_literal_types(node->index_access.index, symbols, type_ctx);

            TypeInfo* object_type = node->index_access.object->type_info;
            TypeInfo* index_type = node->index_access.index->type_info;

            // If object is an identifier, store its symbol entry for codegen
            if (node->index_access.object->type == AST_IDENTIFIER) {
                node->index_access.symbol_entry = symbol_table_lookup(symbols,
                    node->index_access.object->identifier.name);
            } else {
                node->index_access.symbol_entry = NULL;
            }

            // If object is a ref type, look through to the target type for indexing
            TypeInfo* index_target_type = type_info_get_ref_target(object_type);

            // For builtin indexable types (arrays), auto-implement Index trait
            trait_ensure_index_impl(index_target_type, type_ctx);

            // Look up Index<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_Index, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                log_error_at(&node->loc, "Type '%s' does not implement Index<%s>",
                            index_target_type->type_name ? index_target_type->type_name : "?",
                            index_type->type_name ? index_type->type_name : "?");
                node->type_info = Type_Unknown;
                node->index_access.trait_impl = NULL;
                break;
            }

            // Store the trait implementation for codegen
            node->index_access.trait_impl = trait_impl;

            // Get the output type from the trait (use target type, not ref wrapper)
            TypeInfo* output_type = trait_get_assoc_type(Trait_Index, index_target_type,
                                                         type_param_bindings, 1, "Output");
            node->type_info = output_type ? output_type : Type_Unknown;
            break;
        }

        case AST_INDEX_ASSIGNMENT: {
            infer_literal_types(node->index_assignment.object, symbols, type_ctx);
            infer_literal_types(node->index_assignment.index, symbols, type_ctx);
            infer_literal_types(node->index_assignment.value, symbols, type_ctx);

            TypeInfo* object_type = node->index_assignment.object->type_info;
            TypeInfo* index_type = node->index_assignment.index->type_info;

            // If object is an identifier, store its symbol entry for codegen
            if (node->index_assignment.object->type == AST_IDENTIFIER) {
                node->index_assignment.symbol_entry = symbol_table_lookup(symbols,
                    node->index_assignment.object->identifier.name);
            } else {
                node->index_assignment.symbol_entry = NULL;
            }

            // If object is a ref type, look through to the target type for indexing
            TypeInfo* index_target_type = type_info_get_ref_target(object_type);

            // For builtin indexable types (arrays), auto-implement RefIndex trait
            trait_ensure_ref_index_impl(index_target_type, type_ctx);

            // Look up RefIndex<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_RefIndex, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                log_error_at(&node->loc, "Type '%s' does not implement RefIndex<%s> (required for index assignment)",
                            index_target_type->type_name ? index_target_type->type_name : "?",
                            index_type->type_name ? index_type->type_name : "?");
                node->index_assignment.trait_impl = NULL;
                break;
            }

            // Store the trait implementation for codegen
            node->index_assignment.trait_impl = trait_impl;

            // Assignment returns the assigned value's type
            node->type_info = node->index_assignment.value->type_info;
            break;
        }

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

            // Try to infer the type from the object
            ASTNode* obj = node->member_access.object;
            TypeInfo* obj_type_info = NULL;

            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry) {
                    obj_type_info = entry->type_info;
                    // Store symbol entry for codegen optimization
                    node->member_access.symbol_entry = entry;
                }
            } else {
                node->member_access.symbol_entry = NULL;
                if (obj->type == AST_MEMBER_ACCESS || obj->type == AST_INDEX_ACCESS) {
                    // Nested member/index access - the object node should have type_info set
                    obj_type_info = obj->type_info;
                }
            }

            // Unwrap ref types to get the actual object type
            TypeInfo* target_type_info = type_info_get_ref_target(obj_type_info);

            // Check for trait-based properties (like "length")
            if (strcmp(node->member_access.property, "length") == 0) {
                // Ensure Length trait is implemented
                trait_ensure_length_impl(target_type_info, type_ctx);

                // Look up Length trait implementation
                TraitImpl* trait_impl = trait_find_impl(Trait_Length, target_type_info, NULL, 0);
                if (trait_impl) {
                    // Get the output type from the trait
                    TypeInfo* output_type = trait_get_assoc_type(Trait_Length, target_type_info, NULL, 0, "Output");
                    node->type_info = output_type ? output_type : Type_Unknown;
                    break;
                }
            }

            if (target_type_info && type_info_is_object(target_type_info)) {
                // Use TypeInfo to find the property type
                int prop_index = type_info_find_property(target_type_info, node->member_access.property);
                if (prop_index >= 0) {
                    // Store property index for codegen optimization
                    node->member_access.property_index = prop_index;
                    node->type_info = target_type_info->data.object.property_types[prop_index];
                    break;
                } else {
                    node->member_access.property_index = -1;
                }
            } else {
                node->member_access.property_index = -1;
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
    infer_literal_types(cloned_body, temp_symbols, ctx);  // Pass type_ctx so we can look up struct types for static method calls
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

    // Note: Don't free temp_symbols - it's the parent of the body's symbol_table
    // and will be freed when the AST is freed

    spec->specialized_body = cloned_body;

    const char* return_type_str = spec->return_type_info ? spec->return_type_info->type_name : "unknown";
    log_verbose_indent(2, "Analyzed %s with return type %s", spec->specialized_name, return_type_str);
}

// Pass 3: Analyze call sites to find needed specializations
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, TypeContext* ctx) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM: {
            // Use the program's own symbol table if it was created
            SymbolTable* prog_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->program.count; i++) {
                analyze_call_sites(node->program.statements[i], prog_symbols, ctx);
            }
            break;
        }

        case AST_BLOCK: {
            // Use the block's own symbol table if it was created
            SymbolTable* block_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->block.count; i++) {
                analyze_call_sites(node->block.statements[i], block_symbols, ctx);
            }
            break;
        }

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
                        	
                        	// Set the call node's return type from the specialization
                        	if (spec->return_type_info) {
                        	    node->type_info = spec->return_type_info;
                        	}
                        }
                    }

                    free(arg_types);
                }
            }
            break;
        }

        case AST_METHOD_CALL: {
            // Analyze object and arguments
            analyze_call_sites(node->method_call.object, symbols, ctx);
            infer_with_specializations(node->method_call.object, symbols, ctx);

            for (int i = 0; i < node->method_call.arg_count; i++) {
                analyze_call_sites(node->method_call.args[i], symbols, ctx);
                infer_with_specializations(node->method_call.args[i], symbols, ctx);
            }

            // Build the mangled function name: StructName.method_name
            char mangled_name[256];
            if (node->method_call.is_static) {
                // Static method: Type.method
                const char* type_name = node->method_call.object->identifier.name;
                snprintf(mangled_name, sizeof(mangled_name), "%s.%s", type_name, node->method_call.method_name);
            } else {
                // Instance method: need to determine the type from the object
                TypeInfo* obj_type = node->method_call.object->type_info;
                if (obj_type && type_info_is_object(obj_type)) {
                    snprintf(mangled_name, sizeof(mangled_name), "%s.%s", obj_type->type_name, node->method_call.method_name);
                } else {
                    log_error_at(&node->loc, "Cannot call method on non-object type");
                    break;
                }
            }

            // Methods are fully typed, so they should already have a specialization
            // Just verify the method exists
            SymbolEntry* entry = symbol_table_lookup(symbols, mangled_name);
            if (!entry) {
                log_error_at(&node->loc, "Method '%s' not found", mangled_name);
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
            if (node->compound_assignment.target) {
                analyze_call_sites(node->compound_assignment.target, symbols, ctx);
            }
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

        case AST_FOR: {
            // Use the for loop's own symbol table if it was created
            SymbolTable* for_symbols = node->symbol_table ? node->symbol_table : symbols;
            if (node->for_stmt.init) analyze_call_sites(node->for_stmt.init, for_symbols, ctx);
            if (node->for_stmt.condition) analyze_call_sites(node->for_stmt.condition, for_symbols, ctx);
            if (node->for_stmt.update) analyze_call_sites(node->for_stmt.update, for_symbols, ctx);
            analyze_call_sites(node->for_stmt.body, for_symbols, ctx);
            break;
        }

        case AST_WHILE:
            analyze_call_sites(node->while_stmt.condition, symbols, ctx);
            analyze_call_sites(node->while_stmt.body, symbols, ctx);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                analyze_call_sites(node->return_stmt.value, symbols, ctx);
            }
            break;

        case AST_BREAK:
        case AST_CONTINUE:
            // Nothing to analyze for break/continue
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
        case AST_PROGRAM: {
            // Use the program's own symbol table if it was created
            SymbolTable* prog_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->program.count; i++) {
                create_specializations(node->program.statements[i], prog_symbols, ctx);
            }
            break;
        }

        case AST_BLOCK: {
            // Use the block's own symbol table if it was created
            SymbolTable* block_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->block.count; i++) {
                create_specializations(node->block.statements[i], block_symbols, ctx);
            }
            break;
        }

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
        case AST_PROGRAM: {
            // Use the program's own symbol table if it was created
            SymbolTable* prog_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->program.count; i++) {
                infer_with_specializations(node->program.statements[i], prog_symbols, ctx);
            }
            break;
        }

        case AST_BLOCK: {
            // Use the block's own symbol table if it was created
            SymbolTable* block_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->block.count; i++) {
                infer_with_specializations(node->block.statements[i], block_symbols, ctx);
            }
            break;
        }

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
            node->type_info = infer_binary_result_type(&node->loc,
            																				   node->binary_op.op,
                                                       node->binary_op.left->type_info,
                                                       node->binary_op.right->type_info);
            break;

        case AST_UNARY_OP:
            infer_with_specializations(node->unary_op.operand, symbols, ctx);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->type_info = Type_Bool;
            } else if (strcmp(node->unary_op.op, "ref") == 0) {
                // ref operator creates a reference type
                TypeInfo* operand_type = node->unary_op.operand->type_info;
                TypeInfo* ref_type = type_info_create(TYPE_KIND_REF, NULL);
                ref_type->data.ref.target_type = operand_type;
                ref_type->data.ref.is_mutable = true;  // Default to mutable

                // Generate type name like "ref<termios>"
                char type_name[256];
                snprintf(type_name, sizeof(type_name), "ref<%s>",
                        operand_type && operand_type->type_name ? operand_type->type_name : "?");
                ref_type->type_name = strdup(type_name);

                node->type_info = ref_type;
            } else {
                node->type_info = node->unary_op.operand->type_info;
            }
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                infer_with_specializations(node->var_decl.init, symbols, ctx);
                // REMOVED: get_node_value_type(node) = get_node_value_type(node->var_decl.init);
                // Only set type_info from init if there's no explicit type hint
                // Otherwise, keep the declared type that was set in infer_literal_types
                if (!node->var_decl.type_hint) {
                    node->type_info = node->var_decl.init->type_info;
                    
                    // Update the symbol table entry with the refined type
                    SymbolEntry* entry = symbol_table_lookup(symbols, node->var_decl.name);
                    if (entry) {
                        entry->type_info = node->type_info;
                    }
                }

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
            // Don't insert again - the symbol was already created in infer_literal_types
            // Just make sure the symbol_entry pointer is set
            if (!node->var_decl.symbol_entry) {
                node->var_decl.symbol_entry = symbol_table_lookup(symbols, node->var_decl.name);
            }
            break;

        case AST_ASSIGNMENT:
            infer_with_specializations(node->assignment.value, symbols, ctx);
            node->type_info = node->assignment.value->type_info;
            // Store pointer to the symbol entry for fast access in codegen
            if (!node->assignment.symbol_entry) {
                node->assignment.symbol_entry = symbol_table_lookup(symbols, node->assignment.name);
            }
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

        case AST_INDEX_ACCESS: {
            infer_with_specializations(node->index_access.object, symbols, ctx);
            infer_with_specializations(node->index_access.index, symbols, ctx);

            TypeInfo* object_type = node->index_access.object->type_info;
            TypeInfo* index_type = node->index_access.index->type_info;

            // If object is an identifier, store its symbol entry for codegen
            if (node->index_access.object->type == AST_IDENTIFIER) {
                node->index_access.symbol_entry = symbol_table_lookup(symbols,
                    node->index_access.object->identifier.name);
            } else {
                node->index_access.symbol_entry = NULL;
            }

            // If object is a ref type, look through to the target type for indexing
            TypeInfo* index_target_type = type_info_get_ref_target(object_type);

            // For builtin indexable types (arrays), auto-implement Index trait
            trait_ensure_index_impl(index_target_type, ctx);

            // Look up Index<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_Index, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                log_error_at(&node->loc, "Type '%s' does not implement Index<%s>",
                            index_target_type->type_name ? index_target_type->type_name : "?",
                            index_type->type_name ? index_type->type_name : "?");
                node->type_info = Type_Unknown;
                node->index_access.trait_impl = NULL;
                break;
            }

            // Store the trait implementation for codegen
            node->index_access.trait_impl = trait_impl;

            // Get the output type from the trait (use target type, not ref wrapper)
            TypeInfo* output_type = trait_get_assoc_type(Trait_Index, index_target_type,
                                                         type_param_bindings, 1, "Output");
            node->type_info = output_type ? output_type : Type_Unknown;
            break;
        }

        case AST_INDEX_ASSIGNMENT: {
            infer_with_specializations(node->index_assignment.object, symbols, ctx);
            infer_with_specializations(node->index_assignment.index, symbols, ctx);
            infer_with_specializations(node->index_assignment.value, symbols, ctx);

            TypeInfo* object_type = node->index_assignment.object->type_info;
            TypeInfo* index_type = node->index_assignment.index->type_info;

            // If object is an identifier, store its symbol entry for codegen
            if (node->index_assignment.object->type == AST_IDENTIFIER) {
                node->index_assignment.symbol_entry = symbol_table_lookup(symbols,
                    node->index_assignment.object->identifier.name);
            } else {
                node->index_assignment.symbol_entry = NULL;
            }

            // If object is a ref type, look through to the target type for indexing
            TypeInfo* index_target_type = type_info_get_ref_target(object_type);

            // For builtin indexable types (arrays), auto-implement RefIndex trait
            trait_ensure_ref_index_impl(index_target_type, ctx);

            // Look up RefIndex<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_RefIndex, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                log_error_at(&node->loc, "Type '%s' does not implement RefIndex<%s> (required for index assignment)",
                            index_target_type->type_name ? index_target_type->type_name : "?",
                            index_type->type_name ? index_type->type_name : "?");
                node->index_assignment.trait_impl = NULL;
                break;
            }

            // Store the trait implementation for codegen
            node->index_assignment.trait_impl = trait_impl;

            // Assignment returns the assigned value's type
            node->type_info = node->index_assignment.value->type_info;
            break;
        }

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

        case AST_METHOD_CALL: {
            // Infer types for object and arguments
            infer_with_specializations(node->method_call.object, symbols, ctx);
            for (int i = 0; i < node->method_call.arg_count; i++) {
                infer_with_specializations(node->method_call.args[i], symbols, ctx);
            }

            // Build the mangled function name: StructName.method_name
            char mangled_name[256];
            if (node->method_call.is_static) {
                // Static method: Type.method
                const char* type_name = node->method_call.object->identifier.name;
                snprintf(mangled_name, sizeof(mangled_name), "%s.%s", type_name, node->method_call.method_name);
            } else {
                // Instance method: need to determine the type from the object
                TypeInfo* obj_type = node->method_call.object->type_info;
                if (obj_type && type_info_is_object(obj_type)) {
                    snprintf(mangled_name, sizeof(mangled_name), "%s.%s", obj_type->type_name, node->method_call.method_name);
                } else {
                    log_error_at(&node->loc, "Cannot call method on non-object type");
                    node->type_info = Type_Unknown;
                    break;
                }
            }

            // Look up the method specialization
            // For instance methods, we need to include the object type as first argument
            int total_args = node->method_call.arg_count;
            if (!node->method_call.is_static) {
                total_args++; // Add implicit self parameter
            }

            TypeInfo** arg_types = malloc(sizeof(TypeInfo*) * total_args);

            if (!node->method_call.is_static) {
                // For instance methods, lookup by explicit args only
                // The method's first parameter (self) will be injected during codegen
                for (int i = 0; i < node->method_call.arg_count; i++) {
                    arg_types[i] = node->method_call.args[i]->type_info;
                }

                // For instance methods, get the first parameter type from the method signature
                TypeInfo* obj_type = node->method_call.object->type_info;
                if (obj_type && type_info_is_object(obj_type)) {
                    // Look up the method's function type to get its first parameter
                    TypeInfo* method_func_type = type_context_find_function_type(ctx, mangled_name);
                    if (method_func_type && method_func_type->data.function.specializations) {
                        // Get the first specialization (methods are fully typed so they have exactly one)
                        FunctionSpecialization* spec = method_func_type->data.function.specializations;
                        if (spec && spec->param_type_info && spec->param_count > 0) {
                            // Use the actual first parameter type from the method signature
                            TypeInfo* first_param = spec->param_type_info[0];

                            // Shift explicit args and prepend the first param type
                            for (int i = node->method_call.arg_count - 1; i >= 0; i--) {
                                arg_types[i + 1] = arg_types[i];
                            }
                            arg_types[0] = first_param;
                        }
                    }
                }
            } else {
                // Static methods don't have implicit self
                for (int i = 0; i < node->method_call.arg_count; i++) {
                    arg_types[i] = node->method_call.args[i]->type_info;
                }
            }

            FunctionSpecialization* spec = specialization_context_find_by_type_info(
                ctx, mangled_name, arg_types, total_args);

            if (spec) {
                node->type_info = spec->return_type_info;
            } else {
                log_error_at(&node->loc, "Method '%s' not found or type mismatch", mangled_name);
                node->type_info = Type_Unknown;
            }

            free(arg_types);
            break;
        }

        case AST_IF:
            infer_with_specializations(node->if_stmt.condition, symbols, ctx);
            infer_with_specializations(node->if_stmt.then_branch, symbols, ctx);
            if (node->if_stmt.else_branch) {
                infer_with_specializations(node->if_stmt.else_branch, symbols, ctx);
            }
            break;

        case AST_FOR: {
            // Use the for loop's own symbol table if it was created
            SymbolTable* for_symbols = node->symbol_table ? node->symbol_table : symbols;
            if (node->for_stmt.init) infer_with_specializations(node->for_stmt.init, for_symbols, ctx);
            if (node->for_stmt.condition) infer_with_specializations(node->for_stmt.condition, for_symbols, ctx);
            if (node->for_stmt.update) infer_with_specializations(node->for_stmt.update, for_symbols, ctx);
            infer_with_specializations(node->for_stmt.body, for_symbols, ctx);
            break;
        }

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

        case AST_BREAK:
        case AST_CONTINUE:
            // Nothing to infer for break/continue
            break;

        case AST_PREFIX_OP:
        case AST_POSTFIX_OP: {
            // Infer type of the target (if it's a member/index access)
            ASTNode* target = (node->type == AST_PREFIX_OP) ?
                              node->prefix_op.target : node->postfix_op.target;
            if (target) {
                infer_with_specializations(target, symbols, ctx);
            }
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_with_specializations(node->compound_assignment.value, symbols, ctx);

            // Infer type of the target (if it's a member/index access)
            if (node->compound_assignment.target) {
                infer_with_specializations(node->compound_assignment.target, symbols, ctx);
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
            TypeInfo* obj_type_info = NULL;

            if (obj->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(symbols, obj->identifier.name);
                if (entry) {
                    obj_type_info = entry->type_info;
                    // Store symbol entry for codegen optimization
                    node->member_access.symbol_entry = entry;
                }
            } else {
                node->member_access.symbol_entry = NULL;
                if (obj->type == AST_MEMBER_ACCESS || obj->type == AST_INDEX_ACCESS) {
                    // Nested member/index access - the object node should have type_info set
                    obj_type_info = obj->type_info;
                }
            }

            // Unwrap ref types to get the actual object type
            TypeInfo* target_type_info = type_info_get_ref_target(obj_type_info);

            // Check for trait-based properties (like "length")
            if (strcmp(node->member_access.property, "length") == 0) {
                // Ensure Length trait is implemented
                trait_ensure_length_impl(target_type_info, ctx);

                // Look up Length trait implementation
                TraitImpl* trait_impl = trait_find_impl(Trait_Length, target_type_info, NULL, 0);
                if (trait_impl) {
                    // Get the output type from the trait
                    TypeInfo* output_type = trait_get_assoc_type(Trait_Length, target_type_info, NULL, 0, "Output");
                    node->type_info = output_type ? output_type : Type_Unknown;
                    break;
                }
            }

            if (target_type_info && type_info_is_object(target_type_info)) {
                // Use TypeInfo to find the property type
                int prop_index = type_info_find_property(target_type_info, node->member_access.property);
                if (prop_index >= 0) {
                    // Store property index for codegen optimization
                    node->member_access.property_index = prop_index;
                    node->type_info = target_type_info->data.object.property_types[prop_index];
                    break;
                } else {
                    node->member_access.property_index = -1;
                }
            } else {
                node->member_access.property_index = -1;
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

    // Pass 0: Collect struct declarations first (so functions can use struct types)
    log_verbose_indent(1, "Pass 0: Collecting struct declarations");
    collect_struct_declarations(ast, symbols, type_ctx);

    // Pass 0.5: Collect global variables (so functions can reference them)
    log_verbose_indent(1, "Pass 0.5: Collecting global variables");
    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.statements[i];
            if (stmt->type == AST_VAR_DECL) {
                // Add variable to symbol table with unknown type initially
                // Type will be refined in Pass 2
                symbol_table_insert_var_declaration(symbols, stmt->var_decl.name,
                                                    Type_Unknown, stmt->var_decl.is_const, stmt);
                log_verbose_indent(2, "Registered global variable: %s", stmt->var_decl.name);
            }
        }
    }

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

    // Store symbol table in AST for use in codegen
    ast->symbol_table = symbols;

    log_verbose("Type inference complete");
}
