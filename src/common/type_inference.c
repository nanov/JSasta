#include "common/string_utils.h"
#include "jsasta_compiler.h"
#include "traits.h"
#include "operator_utils.h"
#include "logger.h"
#include "diagnostics.h"
#include "module_loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// Helper: Check if a symbol entry is a namespace (has an import node)
static inline bool symbol_is_namespace(SymbolEntry* entry) {
    return entry && entry->node && entry->node->type == AST_IMPORT_DECL;
}

// Helper: Get the imported module from a namespace symbol entry
static inline Module* symbol_get_imported_module(SymbolEntry* entry) {
    return symbol_is_namespace(entry) ? (Module*)entry->node->import_decl.imported_module : NULL;
}

// Helper: Check if an identifier is an enum type name
static inline bool identifier_is_enum_type(const char* name, TypeContext* type_ctx) {
    return name && type_ctx && type_context_find_enum_type(type_ctx, name) != NULL;
}

// Helper: Get enum type by name
static inline TypeInfo* get_enum_type(const char* name, TypeContext* type_ctx) {
    return name && type_ctx ? type_context_find_enum_type(type_ctx, name) : NULL;
}

// Helper macro for type errors (similar to PARSE_ERROR)
#define TYPE_ERROR(diag, loc, err, ...) do { \
    if (diag) { \
        JSASTA_ERROR(diag, loc, err, ##__VA_ARGS__); \
    } else { \
        log_error_at(&loc, JSASTA_GET_TEMPLATE(err), ##__VA_ARGS__); \
    } \
} while(0)

// Helper: Resolve namespaced type (e.g., "termios.termios_t" or "a.b.c.Type")
// Walks the namespace chain from left to right: a.b.c.Type means
// - Look up 'a' in current symbols (must be namespace)
// - Look up 'b' in module 'a' (must be namespace)
// - Look up 'c' in module 'b' (must be namespace)
// - Look up 'Type' in module 'c' (the actual type)
// Returns the resolved TypeInfo* or NULL if not found
static TypeInfo* resolve_namespaced_type(const char* type_path, SymbolTable* symbols, TypeContext* type_ctx) {
    if (!type_path || !symbols) return NULL;

    log_verbose("Resolving type path: %s", type_path);

    // Check if this is a namespaced type (contains a dot)
    if (!strchr(type_path, '.')) {
        // Not namespaced, just look it up directly in the TypeContext
        // Try primitive types first (handles string, i32, etc.), then struct, then enum
        TypeInfo* result = type_context_find_type(type_ctx, type_path);
        if (!result) {
            result = type_context_find_struct_type(type_ctx, type_path);
        }
        if (!result) {
            result = type_context_find_enum_type(type_ctx, type_path);
        }
        log_verbose("  Direct lookup '%s': %s", type_path, result ? "found" : "not found");
        return result;
    }

    // Split the path into parts: "a.b.c.Type" -> ["a", "b", "c", "Type"]
    char path_copy[512];
    strncpy(path_copy, type_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* parts[32]; // Max 32 levels of nesting
    int part_count = 0;

    char* token = strtok(path_copy, ".");
    while (token && part_count < 32) {
        parts[part_count++] = token;
        token = strtok(NULL, ".");
    }

    if (part_count < 2) {
        log_error("Invalid type path: %s", type_path);
        return NULL;
    }

    // The last part is the type name, everything before is namespace chain
    const char* type_name = parts[part_count - 1];

    // For now, only support single-level namespaces: namespace.Type
    // Deeply nested like a.b.c.Type would require modules to re-export other modules
    if (part_count > 2) {
        log_error("Deeply nested namespace types not yet supported: '%s'", type_path);
        log_error("Only single-level namespaces like 'namespace.Type' are supported");
        return NULL;
    }

    // Single level: namespace.Type
    const char* namespace_name = parts[0];
    log_verbose("  Looking up namespace '%s'", namespace_name);

    // Look up the namespace in the current symbol table
    SymbolEntry* entry = symbol_table_lookup(symbols, namespace_name);
    if (!entry || !symbol_is_namespace(entry)) {
        log_error("Unknown namespace '%s' in type path '%s'", namespace_name, type_path);
        return NULL;
    }

    // Get the module for this namespace
    Module* current_module = symbol_get_imported_module(entry);
    if (!current_module) {
        log_error("Failed to get module for namespace '%s'", namespace_name);
        return NULL;
    }

    log_verbose("  Found module: %s", current_module->relative_path);

    // Now look up the actual type in the final module's TypeContext
    if (!current_module || !current_module->type_ctx) {
        log_error("No module or type context for type lookup");
        return NULL;
    }

    TypeInfo* resolved = type_context_find_struct_type(current_module->type_ctx, type_name);
    if (!resolved) {
        log_error("Type '%s' not found in final namespace", type_name);
        return NULL;
    }

    log_verbose("  Resolved to type: %s", resolved->type_name);
    return resolved;
}

// Static counter for generating unique type names
static int type_name_counter = 0;

// Helper function to generate unique type name for objects
static char* generate_type_name(void) {
    char* name = (char*)malloc(32);
    snprintf(name, 32, "Object_%d", type_name_counter++);
    return name;
}

// Result type for const expression evaluation (Rust-style)
typedef enum {
    EVAL_SUCCESS,        // Successfully evaluated
    EVAL_WAITING,        // Dependencies not ready yet (e.g., undefined identifier that might be defined later)
    EVAL_CYCLE,          // Circular dependency detected
    EVAL_ERROR           // Real error (type mismatch, negative value, etc.)
} EvalStatus;

typedef struct {
    EvalStatus status;
    int value;           // Only valid if status == EVAL_SUCCESS
    char* error_msg;     // Only set if status == EVAL_ERROR or EVAL_CYCLE
    SourceLocation loc;  // Location of error
} EvalResult;

// Evaluation stack for cycle detection (like Rust's query stack)
static ASTNode* eval_stack[100];
static int eval_stack_depth = 0;

// Helper functions to create EvalResult
static EvalResult eval_success(int value) {
    return (EvalResult){.status = EVAL_SUCCESS, .value = value, .error_msg = NULL};
}

static EvalResult eval_waiting(SourceLocation loc, const char* msg) {
    char* error = strdup(msg);
    return (EvalResult){.status = EVAL_WAITING, .value = 0, .error_msg = error, .loc = loc};
}

static EvalResult eval_cycle(SourceLocation loc, const char* msg) {
    char* error = strdup(msg);
    return (EvalResult){.status = EVAL_CYCLE, .value = 0, .error_msg = error, .loc = loc};
}

static EvalResult eval_error(SourceLocation loc, const char* msg) {
    char* error = strdup(msg);
    return (EvalResult){.status = EVAL_ERROR, .value = 0, .error_msg = error, .loc = loc};
}

// Forward declarations
static void collect_consts_and_structs(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag);
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag);
static void infer_literal_types(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag);
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag);
static void create_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag);
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag);
static TypeInfo* infer_function_return_type_with_params(ASTNode* body, SymbolTable* scope, DiagnosticContext* diag);
static void iterative_specialization_discovery(ASTNode* ast, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag);

// Helper: Evaluate a constant expression to an integer value (Rust-style with EvalResult)
// Uses the eval_stack for cycle detection
static EvalResult eval_const_expr_internal(ASTNode* expr, SymbolTable* symbols) {
    if (!expr) {
        SourceLocation dummy_loc = {.filename = "", .line = 0, .column = 0};
        return eval_error(dummy_loc, "NULL expression in const evaluation");
    }

    // Check for circular dependency using eval_stack
    for (int i = 0; i < eval_stack_depth; i++) {
        if (eval_stack[i] == expr) {
            return eval_cycle(expr->loc, "Circular dependency detected in const expression");
        }
    }

    // Prevent stack overflow
    if (eval_stack_depth >= 100) {
        return eval_error(expr->loc, "Const expression recursion too deep");
    }

    // Push to eval stack
    eval_stack[eval_stack_depth++] = expr;

    EvalResult result;

    switch (expr->type) {
        case AST_NUMBER: {
            // Check if it's an integer
            double value = expr->number.value;
            if (value != (int)value) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Array size must be an integer, got %.2f", value);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }
            int int_val = (int)value;
            if (int_val <= 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Array size must be positive, got %d", int_val);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }
            result = eval_success(int_val);
            goto cleanup;
        }

        case AST_IDENTIFIER: {
            // Look up const variable
            SymbolEntry* entry = symbol_table_lookup(symbols, expr->identifier.name);
            if (!entry) {
                // This might be defined later - return WAITING
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined identifier '%s' in array size expression", expr->identifier.name);
                result = eval_waiting(expr->loc, msg);
                goto cleanup;
            }
            if (!entry->is_const) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "Variable '%s' is not declared as 'const' and cannot be used in array size expression\n"
                    "  Hint: Change 'var %s' to 'const %s' if it's a compile-time constant",
                    expr->identifier.name, expr->identifier.name, expr->identifier.name);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }
            if (!entry->node || entry->node->type != AST_VAR_DECL) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Const '%s' is not a variable declaration", expr->identifier.name);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }
            if (!entry->node->var_decl.init) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Const '%s' has no initializer and cannot be evaluated", expr->identifier.name);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }
            // Recursively evaluate the const's initializer
            result = eval_const_expr_internal(entry->node->var_decl.init, symbols);
            goto cleanup;
        }

        case AST_BINARY_OP: {
            EvalResult left_result = eval_const_expr_internal(expr->binary_op.left, symbols);
            if (left_result.status != EVAL_SUCCESS) {
                result = left_result;
                goto cleanup;
            }

            EvalResult right_result = eval_const_expr_internal(expr->binary_op.right, symbols);
            if (right_result.status != EVAL_SUCCESS) {
                result = right_result;
                goto cleanup;
            }

            int left = left_result.value;
            int right = right_result.value;
            const char* op = expr->binary_op.op;
            int computed;

            if (strcmp(op, "+") == 0) {
                computed = left + right;
            } else if (strcmp(op, "-") == 0) {
                computed = left - right;
            } else if (strcmp(op, "*") == 0) {
                computed = left * right;
            } else if (strcmp(op, "/") == 0) {
                if (right == 0) {
                    result = eval_error(expr->loc, "Division by zero in array size expression");
                    goto cleanup;
                }
                computed = left / right;
            } else if (strcmp(op, "%") == 0) {
                if (right == 0) {
                    result = eval_error(expr->loc, "Modulo by zero in array size expression");
                    goto cleanup;
                }
                computed = left % right;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "Operator '%s' is not supported in array size expressions (supported: + - * / %%)", op);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }

            if (computed <= 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Array size expression evaluates to %d, but must be positive", computed);
                result = eval_error(expr->loc, msg);
                goto cleanup;
            }
            result = eval_success(computed);
            goto cleanup;
        }

        case AST_STRING:
            result = eval_error(expr->loc, "String literals cannot be used in array size expressions");
            goto cleanup;

        case AST_BOOLEAN:
            result = eval_error(expr->loc, "Boolean values cannot be used in array size expressions");
            goto cleanup;

        case AST_CALL:
            result = eval_error(expr->loc, "Function calls cannot be used in array size expressions (must be compile-time constants)");
            goto cleanup;

        default:
            result = eval_error(expr->loc, "This expression cannot be used in array size (must be a const integer expression)");
            goto cleanup;
    }

cleanup:
    // Pop from eval stack
    eval_stack_depth--;
    return result;
}

// New wrapper that returns EvalResult (for Rust-style error handling)
static EvalResult eval_const_expr_result(ASTNode* expr, SymbolTable* symbols) {
    eval_stack_depth = 0;  // Reset stack
    return eval_const_expr_internal(expr, symbols);
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
        if (left == Type_Str || right == Type_Str) {
            return Type_Str;
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
static TypeInfo* infer_function_return_type_with_params(ASTNode* node, SymbolTable* scope, DiagnosticContext* diag);

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
            return Type_Str;
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
                if (elem_type == Type_Str) return Type_Array_Str;
            }
            return Type_Array_Int; // Default to int array
        }
        case AST_INDEX_ACCESS: {
            TypeInfo* obj_type = infer_expr_type_simple(node->index_access.object, scope);

            // Unwrap ref types to get the actual target type
            TypeInfo* target_type = type_info_get_ref_target(obj_type);

            // String and array indexing now handled by Index trait
            if (type_info_is_array(target_type)) return target_type->data.array.element_type;
            return Type_Unknown;
        }
        case AST_OBJECT_LITERAL:
        case AST_STRUCT_LITERAL:
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
static TypeInfo* infer_function_return_type_with_params(ASTNode* node, SymbolTable* scope, DiagnosticContext* diag) {
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
                    node->program.statements[i], scope, diag);
                if (ret_type != Type_Void && !type_info_is_unknown(ret_type)) {
                    return ret_type;
                }
            }
            return Type_Void;

        case AST_IF: {
            TypeInfo*  then_type = infer_function_return_type_with_params(
                node->if_stmt.then_branch, scope, diag);
            if (then_type != Type_Void && !type_info_is_unknown(then_type)) {
                return then_type;
            }
            if (node->if_stmt.else_branch) {
                TypeInfo*  else_type = infer_function_return_type_with_params(
                    node->if_stmt.else_branch, scope, diag);
                if (else_type != Type_Void && !type_info_is_unknown(else_type)) {
                    return else_type;
                }
            }
            return Type_Void;
        }

        case AST_FOR:
        case AST_WHILE:
            return infer_function_return_type_with_params(node->for_stmt.body, scope, diag);

        default:
            return Type_Void;
    }
}

// Pass 0: Collect struct declarations (before functions, so functions can use struct types)
static void collect_struct_declarations(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                collect_struct_declarations(node->program.statements[i], symbols, type_ctx, diag);
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
                    infer_literal_types(default_values[i], symbols, NULL, diag);

                    // Check if default value type matches property type
                    TypeInfo* default_type = default_values[i]->type_info;
                    TypeInfo* prop_type = property_types[i];

                    if (default_type != prop_type) {
                        // Allow int -> double promotion
                        if (!(prop_type == Type_Double && default_type == Type_Int)) {
                            TYPE_ERROR(diag, node->loc, TE_DUPLICATE_VARIABLE,
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
                char* mangled_name = str_format("%s.%s", struct_name, method->func_decl.name);
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

// Collect enum declarations and register them in TypeContext
static void collect_enum_declarations(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                collect_enum_declarations(node->program.statements[i], symbols, type_ctx, diag);
            }
            break;

        case AST_ENUM_DECL: {
            const char* enum_name = node->enum_decl.name;
            char** variant_names = node->enum_decl.variant_names;
            char*** variant_field_names = node->enum_decl.variant_field_names;
            TypeInfo*** variant_field_types = node->enum_decl.variant_field_types;
            int* variant_field_counts = node->enum_decl.variant_field_counts;
            int variant_count = node->enum_decl.variant_count;

            // Resolve any unresolved field types
            for (int i = 0; i < variant_count; i++) {
                if (variant_field_types[i]) {
                    for (int j = 0; j < variant_field_counts[i]; j++) {
                        TypeInfo* field_type = variant_field_types[i][j];
                        // Check if this is an unresolved type that needs resolution
                        if (field_type && field_type->kind == TYPE_KIND_UNKNOWN && field_type->type_name) {
                            TypeInfo* resolved = resolve_namespaced_type(field_type->type_name, symbols, type_ctx);
                            if (resolved) {
                                variant_field_types[i][j] = resolved;
                            } else {
                                TYPE_ERROR(diag, node->loc, TE_TYPE_MISMATCH,
                                    "Cannot resolve type '%s' in enum '%s' variant '%s'",
                                    field_type->type_name, enum_name, variant_names[i]);
                                variant_field_types[i][j] = Type_Unknown;
                            }
                        }
                    }
                }
            }

            // Register enum type in TypeContext
            if (type_ctx) {
                TypeInfo* existing = type_context_find_enum_type(type_ctx, enum_name);
                if (!existing) {
                    TypeInfo* enum_type = type_context_create_enum_type(
                        type_ctx,
                        enum_name,
                        variant_names,
                        variant_field_names,
                        variant_field_types,
                        variant_field_counts,
                        variant_count,
                        node  // Pass the enum declaration node
                    );

                    if (enum_type) {
                        log_verbose("Registered enum type during type inference: %s with %d variants",
                                   enum_name, variant_count);
                    }
                } else {
                    log_verbose("Enum type already registered: %s", enum_name);
                }
            }

            break;
        }

        default:
            break;
    }
}

// Pass 1: Collect function signatures
static void collect_function_signatures(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                collect_function_signatures(node->program.statements[i], symbols, type_ctx, diag);
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

            // Resolve namespaced type hints in parameters
            for (int i = 0; i < param_count; i++) {
                if (param_type_hints[i] &&
                    param_type_hints[i]->kind == TYPE_KIND_UNKNOWN &&
                    param_type_hints[i]->type_name) {
                    TypeInfo* resolved = resolve_namespaced_type(param_type_hints[i]->type_name, symbols, type_ctx);
                    if (resolved) {
                        param_type_hints[i] = resolved;
                    } else {
                        TYPE_ERROR(diag, node->loc, TE_TYPE_MISMATCH,
                            "Cannot resolve parameter type '%s' in function '%s'",
                            param_type_hints[i]->type_name, func_name);
                        param_type_hints[i] = Type_Unknown;
                    }
                }
            }

            // Resolve namespaced return type hint
            // Skip Type_Unknown (which has type_name "unknown" but shouldn't be resolved)
            if (return_type_hint &&
                return_type_hint->kind == TYPE_KIND_UNKNOWN &&
                return_type_hint != Type_Unknown &&
                return_type_hint->type_name) {
                TypeInfo* resolved = resolve_namespaced_type(return_type_hint->type_name, symbols, type_ctx);
                if (resolved) {
                    node->func_decl.return_type_hint = resolved;
                    return_type_hint = resolved;
                } else {
                    TYPE_ERROR(diag, node->loc, TE_TYPE_MISMATCH,
                        "Cannot resolve return type '%s' in function '%s'",
                        return_type_hint->type_name, func_name);
                    node->func_decl.return_type_hint = Type_Unknown;
                    return_type_hint = Type_Unknown;
                }
            }

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

                // Store the type info on the node for LSP and other uses
                node->type_info = func_type;

                log_verbose("Created %sfunction type: %s", body ? "" : "external ", func_type->type_name);

                // If fully typed (external functions have no body and are always fully typed)
                if (func_type->data.function.is_fully_typed) {
                    FunctionSpecialization* spec = type_context_add_specialization(
                        type_ctx, func_type,
                        param_type_hints,
                        param_count
                    );

                    if (spec) {
                        // Use module-prefixed name for user functions, original name for external functions
                        free(spec->specialized_name);
                        if (body && type_ctx->module_prefix) {
                            // User functions get module prefix
                            char mangled_name[512];
                            snprintf(mangled_name, sizeof(mangled_name), "%s__%s", type_ctx->module_prefix, func_name);
                            spec->specialized_name = strdup(mangled_name);
                        } else {
                            // External functions keep their original name
                            spec->specialized_name = strdup(func_name);
                        }

                        // Set return type
                        spec->return_type_info = return_type_hint;

                        // For user functions with bodies, clone the body and set up symbol table
                        if (body) {
                            ASTNode* cloned_body = ast_clone(body);

                            // Create symbol table with parameters
                            SymbolTable* temp_symbols = symbol_table_create(symbols);
                            for (int i = 0; i < param_count; i++) {
                                // Pass the function node as the declaration node for parameters
                                // This allows LSP to find the parameter definitions
                                symbol_table_insert_var_declaration(temp_symbols,
                                                  node->func_decl.params[i],
                                                  param_type_hints[i], false, node);
                                // Set param_index for LSP go-to-definition
                                SymbolEntry* param_entry = symbol_table_lookup(temp_symbols, node->func_decl.params[i]);
                                if (param_entry) {
                                    param_entry->param_index = i;
                                }
                            }

                            // Store the symbol table in the cloned body
                            cloned_body->symbol_table = temp_symbols;

                            // Run infer_literal_types to set up the structure
                            infer_literal_types(cloned_body, temp_symbols, type_ctx, diag);

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

        case AST_EXPORT_DECL: {
            // Unwrap export declaration and process the inner declaration
            collect_function_signatures(node->export_decl.declaration, symbols, type_ctx, diag);
            break;
        }

        case AST_STRUCT_DECL: {
            // Process methods as global functions
            for (int i = 0; i < node->struct_decl.method_count; i++) {
                ASTNode* method = node->struct_decl.methods[i];
                // Process each method as a regular function
                collect_function_signatures(method, symbols, type_ctx, diag);
            }
            break;
        }

        default:
            break;
    }
}

// Helper: Check if an expression contains a logical OR (||) operator
static bool expression_contains_or(ASTNode* node) {
    if (!node) return false;

    switch (node->type) {
        case AST_BINARY_OP:
            if (strcmp(node->binary_op.op, "||") == 0) {
                return true;
            }
            return expression_contains_or(node->binary_op.left) ||
                   expression_contains_or(node->binary_op.right);

        case AST_UNARY_OP:
            return expression_contains_or(node->unary_op.operand);

        case AST_PATTERN_MATCH:
            return expression_contains_or(node->pattern_match.expr);

        case AST_CALL:
            for (int i = 0; i < node->call.arg_count; i++) {
                if (expression_contains_or(node->call.args[i])) {
                    return true;
                }
            }
            return false;

        case AST_TERNARY:
            return expression_contains_or(node->ternary.condition) ||
                   expression_contains_or(node->ternary.true_expr) ||
                   expression_contains_or(node->ternary.false_expr);

        default:
            return false;
    }
}

// Helper: Collect all pattern match nodes from an expression tree
// Only collects patterns that are in && chains (left side of || is excluded if || present)
static void collect_pattern_bindings(ASTNode* node, ASTNode*** patterns, int* pattern_count, int* pattern_capacity) {
    if (!node) return;

    switch (node->type) {
        case AST_PATTERN_MATCH:
            // Add this pattern to the list
            if (*pattern_count >= *pattern_capacity) {
                *pattern_capacity *= 2;
                *patterns = realloc(*patterns, sizeof(ASTNode*) * (*pattern_capacity));
            }
            (*patterns)[*pattern_count] = node;
            (*pattern_count)++;
            break;

        case AST_BINARY_OP:
            if (strcmp(node->binary_op.op, "&&") == 0) {
                // For &&, collect from both sides
                collect_pattern_bindings(node->binary_op.left, patterns, pattern_count, pattern_capacity);
                collect_pattern_bindings(node->binary_op.right, patterns, pattern_count, pattern_capacity);
            } else if (strcmp(node->binary_op.op, "||") == 0) {
                // For ||, don't collect any patterns - bindings won't be available
                // This is handled by the has_or check
            }
            break;

        default:
            // Other node types don't contain patterns
            break;
    }
}

// Helper: Infer types only for pattern match nodes in an expression
// This avoids trying to infer types for expressions that use pattern bindings
static void infer_pattern_types_only(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PATTERN_MATCH:
            // Infer this pattern match
            infer_literal_types(node, symbols, type_ctx, diag);
            break;

        case AST_BINARY_OP:
            // Descend into binary ops to find pattern matches
            infer_pattern_types_only(node->binary_op.left, symbols, type_ctx, diag);
            infer_pattern_types_only(node->binary_op.right, symbols, type_ctx, diag);
            break;

        default:
            // Don't infer other node types in the first pass
            break;
    }
}

// Pass 2: Infer literal and obvious types
static void infer_literal_types(ASTNode* node, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            // AST_PROGRAM uses the passed-in symbols (top-level scope)
            for (int i = 0; i < node->program.count; i++) {
                infer_literal_types(node->program.statements[i], symbols, type_ctx, diag);
            }
            break;

        case AST_BLOCK: {
            // AST_BLOCK creates a new scope with the current scope as parent
            // Only create if it doesn't already exist (to avoid duplicates during iteration)
            SymbolTable* block_symbols = node->symbol_table;
            if (!block_symbols) {
                block_symbols = symbol_table_create(symbols);
                node->symbol_table = block_symbols;
            }
            for (int i = 0; i < node->block.count; i++) {
                infer_literal_types(node->block.statements[i], block_symbols, type_ctx, diag);
            }
            break;
        }

        case AST_NUMBER:
        case AST_STRING:
        case AST_BOOLEAN:
            // Type info already set by parser and properly cloned
            break;

        case AST_VAR_DECL:
            // Resolve namespaced type hints (e.g., "termios.termios_t")
            if (node->var_decl.type_hint &&
                node->var_decl.type_hint->kind == TYPE_KIND_UNKNOWN &&
                node->var_decl.type_hint->type_name) {
                log_verbose("VAR_DECL: Resolving type hint '%s' for variable '%s'",
                           node->var_decl.type_hint->type_name, node->var_decl.name);
                TypeInfo* resolved = resolve_namespaced_type(node->var_decl.type_hint->type_name, symbols, type_ctx);
                if (resolved) {
                    log_verbose("VAR_DECL: Successfully resolved to type: %s", resolved->type_name);
                    node->var_decl.type_hint = resolved;
                } else {
                    log_error("VAR_DECL: Failed to resolve type '%s'", node->var_decl.type_hint->type_name);
                    TYPE_ERROR(diag, node->loc, TE_TYPE_MISMATCH,
                        "Cannot resolve type '%s'", node->var_decl.type_hint->type_name);
                    node->var_decl.type_hint = Type_Unknown;
                }
            }

            // Evaluate const expression for array size
            if (node->var_decl.array_size_expr) {
                EvalResult result = eval_const_expr_result(node->var_decl.array_size_expr, symbols);
                if (result.status == EVAL_SUCCESS) {
                    node->var_decl.array_size = result.value;
                } else {
                    // Report error with proper diagnostic
                    if (result.error_msg) {
                        TYPE_ERROR(diag, result.loc, TE_FIELD_NOT_FOUND, "%s", result.error_msg);
                        free(result.error_msg);
                    } else {
                        TYPE_ERROR(diag, node->loc, TE_FIELD_NOT_FOUND, "Invalid array size expression");
                    }
                    node->var_decl.array_size = 0;
                }
            }

            if (node->var_decl.init) {
                // Special case: if we have an array type hint and object literal (empty {}),
                // convert the object literal to an array literal
                bool is_array_init = (node->var_decl.type_hint &&
                                     type_info_is_array(node->var_decl.type_hint) &&
                                     node->var_decl.init->type == AST_OBJECT_LITERAL &&
                                     node->var_decl.init->object_literal.count == 0);

                if (is_array_init) {
                    // Convert empty object literal to empty array literal
                    node->var_decl.init->type = AST_ARRAY_LITERAL;
                    free(node->var_decl.init->object_literal.keys);
                    free(node->var_decl.init->object_literal.values);
                    node->var_decl.init->array_literal.elements = NULL;
                    node->var_decl.init->array_literal.count = 0;
                    // Set type info to match the declared array type
                    node->var_decl.init->type_info = node->var_decl.type_hint;
                    log_verbose("Converted empty object literal to array literal for '%s'",
                               node->var_decl.name);
                }

                // Special case: if we have a struct type hint and object literal,
                // skip normal type inference to avoid creating anonymous types
                bool is_struct_literal = (node->var_decl.type_hint &&
                                         type_info_is_object(node->var_decl.type_hint) &&
                                         node->var_decl.type_hint->data.object.struct_decl_node &&
                                         node->var_decl.init->type == AST_OBJECT_LITERAL);

                if (!is_struct_literal && !is_array_init) {
                    infer_literal_types(node->var_decl.init, symbols, type_ctx, diag);
                }

                // If type hint is provided, validate it matches the initialization value
                if (node->var_decl.type_hint) {
                    TypeInfo* declared_type = node->var_decl.type_hint;
                    TypeInfo* inferred_type = node->var_decl.init->type_info;

                    // Check for type mismatch
                    if (type_info_is_unknown(inferred_type) && inferred_type != declared_type) {
                        // Allow int -> double promotion
                        if (!(declared_type == Type_Double && inferred_type == Type_Int)) {
                            TYPE_ERROR(diag, node->loc, TE_TYPE_MISMATCH,
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
                                    infer_literal_types(value, symbols, type_ctx, diag);
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
                                            TYPE_ERROR(diag, node->loc, TE_WRONG_ARGUMENT_COUNT,
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
                                TYPE_ERROR(diag, node->loc, TE_OPERATOR_NOT_SUPPORTED,
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
                                        TYPE_ERROR(diag, node->loc, TE_RETURN_TYPE_MISMATCH,
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
                    log_verbose("[VAR_DECL] %s: inferred type from init: %s",
                               node->var_decl.name,
                               node->type_info ? node->type_info->type_name : "NULL");
                }

                // If initializing with an array literal, set the array size from the literal
                // But only if array_size wasn't already explicitly set from type hint
                if (node->var_decl.init->type == AST_ARRAY_LITERAL && type_info_is_array(node->type_info)) {
                    if (node->var_decl.array_size == 0) {
                        node->var_decl.array_size = node->var_decl.init->array_literal.count;
                        log_verbose("[VAR_DECL] %s: array size set to %d from literal",
                                   node->var_decl.name, node->var_decl.array_size);
                    } else {
                        log_verbose("[VAR_DECL] %s: array size already set to %d from type hint",
                                   node->var_decl.name, node->var_decl.array_size);
                    }
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
                        // Don't clone - just reference the TypeInfo from TypeContext
                        entry->type_info = node->var_decl.type_hint;
                        if (type_info_is_ref(node->var_decl.type_hint)) {
                            log_verbose("Variable '%s' assigned declared ref type '%s'",
                                       node->var_decl.name, entry->type_info->type_name);
                        } else {
                            log_verbose("Variable '%s' assigned declared object type with %d properties",
                                       node->var_decl.name, entry->type_info->data.object.property_count);
                        }
                    } else if (node->var_decl.init->type == AST_OBJECT_LITERAL && node->var_decl.init->type_info) {
                        // Use inferred type info from literal
                        // Don't clone - just reference the TypeInfo from the literal
                        entry->type_info = node->var_decl.init->type_info;
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
                        // Don't clone - just reference the TypeInfo from TypeContext
                        entry->type_info = node->var_decl.type_hint;
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
            infer_literal_types(node->binary_op.left, symbols, type_ctx, diag);
            infer_literal_types(node->binary_op.right, symbols, type_ctx, diag);
            // Binary op type inferred from operands
            node->type_info = infer_binary_result_type(&node->loc, node->binary_op.op,
                                                       node->binary_op.left->type_info,
                                                       node->binary_op.right->type_info);
            break;

        case AST_UNARY_OP:
            infer_literal_types(node->unary_op.operand, symbols, type_ctx, diag);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->type_info = Type_Bool;
            } else if (strcmp(node->unary_op.op, "ref") == 0) {
                // ref operator creates a reference type
                TypeInfo* operand_type = node->unary_op.operand->type_info;
                node->type_info = type_context_get_or_create_ref_type(type_ctx, operand_type, true);
            } else {
                node->type_info = node->unary_op.operand->type_info;
            }
            break;

        case AST_CALL:
            for (int i = 0; i < node->call.arg_count; i++) {
                infer_literal_types(node->call.args[i], symbols, type_ctx, diag);
            }
            if (node->call.callee->type == AST_IDENTIFIER) {
                const char* func_name = node->call.callee->identifier.name;
                const SymbolEntry* entry = symbol_table_lookup(symbols, func_name);
                // no user function
                if (entry) {
                    // For fully typed functions (including external), set return type from specialization
                    if (entry->type_info && entry->type_info->data.function.is_fully_typed) {
                        if (entry->type_info->data.function.specializations) {
                            node->type_info = entry->type_info->data.function.specializations->return_type_info;
                        }
                    }
                } else {
              		JSASTA_ERROR(diag, node->loc, TE_FUNCTION_NOT_FOUND, "Function not declared");
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
                    infer_literal_types(node->method_call.object, symbols, type_ctx, diag);
                }
            } else {
                // Infer type for the object - it's an expression (could be namespace.Type)
                infer_literal_types(node->method_call.object, symbols, type_ctx, diag);

                // After inference, check if the object resolved to a type (e.g., test.assert)
                // If so, this is a static method call
                if (node->method_call.object->type_info && type_info_is_object(node->method_call.object->type_info)) {
                    node->method_call.is_static = true;
                    log_verbose("[METHOD_CALL infer_literal] Treating as static call on type: %s",
                               node->method_call.object->type_info->type_name);
                } else {
                    node->method_call.is_static = false;
                }

                log_verbose("[METHOD_CALL infer_literal] object type after infer: %s",
                           node->method_call.object->type_info ?
                           node->method_call.object->type_info->type_name : "NULL");
            }

            // Infer types for arguments
            for (int i = 0; i < node->method_call.arg_count; i++) {
                infer_literal_types(node->method_call.args[i], symbols, type_ctx, diag);
            }

            // Look up the method to get its return type
            if (node->method_call.object->type_info && type_info_is_object(node->method_call.object->type_info)) {
                const char* type_name = node->method_call.object->type_info->type_name;
                char mangled_name[256];
                snprintf(mangled_name, sizeof(mangled_name), "%s.%s", type_name, node->method_call.method_name);

                TypeInfo* method_type = type_context_find_function_type(type_ctx, mangled_name);
                if (method_type && method_type->data.function.specializations) {
                    // Get the return type from the specialization
                    FunctionSpecialization* spec = method_type->data.function.specializations;
                    node->type_info = spec->return_type_info;
                    log_verbose("[METHOD_CALL infer_literal] %s -> return type: %s",
                               mangled_name,
                               node->type_info ? node->type_info->type_name : "NULL");
                }
            }
            break;

        case AST_ASSIGNMENT:
            infer_literal_types(node->assignment.value, symbols, type_ctx, diag);
            node->type_info = node->assignment.value->type_info;
            // Store pointer to the symbol entry for fast access in codegen
            node->assignment.symbol_entry = symbol_table_lookup(symbols, node->assignment.name);
            break;

        case AST_MEMBER_ASSIGNMENT: {
            // Infer types for object
            infer_literal_types(node->member_assignment.object, symbols, type_ctx, diag);

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
                infer_literal_types(node->member_assignment.value, symbols, type_ctx, diag);
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
                                    TYPE_ERROR(diag, node->loc, TE_MISSING_RETURN,
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
            infer_literal_types(node->ternary.condition, symbols, type_ctx, diag);
            infer_literal_types(node->ternary.true_expr, symbols, type_ctx, diag);
            infer_literal_types(node->ternary.false_expr, symbols, type_ctx, diag);
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

        case AST_IF: {
            // First pass: Only infer pattern matches to get their types and binding types
            // Don't infer the whole condition yet because it might reference pattern bindings
            infer_pattern_types_only(node->if_stmt.condition, symbols, type_ctx, diag);

            // Collect all pattern matches from the condition expression
            int pattern_capacity = 4;
            int pattern_count = 0;
            ASTNode** patterns = malloc(sizeof(ASTNode*) * pattern_capacity);
            collect_pattern_bindings(node->if_stmt.condition, &patterns, &pattern_count, &pattern_capacity);

            // Check if the condition contains any || operators
            bool has_or = expression_contains_or(node->if_stmt.condition);

            // Only add pattern bindings to scope if:
            // 1. There are pattern matches with bindings
            // 2. There's no || in the condition (bindings would be ambiguous)
            if (pattern_count > 0 && !has_or) {
                // Create a new scope for the condition and then-branch to hold pattern bindings
                SymbolTable* binding_scope = symbol_table_create(symbols);

                // Add all pattern bindings from all collected patterns
                for (int p = 0; p < pattern_count; p++) {
                    ASTNode* pattern = patterns[p];
                    for (int i = 0; i < pattern->pattern_match.binding_count; i++) {
                        if (!pattern->pattern_match.binding_is_wildcard[i]) {
                            char* binding_name = pattern->pattern_match.binding_names[i];
                            TypeInfo* binding_type = pattern->pattern_match.binding_types[i];
                            bool is_const = !pattern->pattern_match.binding_is_mutable[i];

                            symbol_table_insert(binding_scope, binding_name, binding_type, NULL, is_const);
                        }
                    }
                }

                // Second pass: Now infer the full condition with bindings in scope
                // This allows expressions like (msg is Foo(let x) && x > 10) to work
                infer_literal_types(node->if_stmt.condition, binding_scope, type_ctx, diag);

                // Attach the scope to the then-branch node so codegen can use it
                node->if_stmt.then_branch->symbol_table = binding_scope;

                // Infer types in then-branch with the binding scope
                infer_literal_types(node->if_stmt.then_branch, binding_scope, type_ctx, diag);
            } else {
                // Normal if without pattern matching bindings available
                // Still need to infer the condition
                infer_literal_types(node->if_stmt.condition, symbols, type_ctx, diag);
                infer_literal_types(node->if_stmt.then_branch, symbols, type_ctx, diag);
            }

            free(patterns);

            if (node->if_stmt.else_branch) {
                infer_literal_types(node->if_stmt.else_branch, symbols, type_ctx, diag);
            }
            break;
        }

        case AST_FOR: {
            // For loops create their own scope for variables declared in init
            // Only create if it doesn't already exist (to avoid duplicates during iteration)
            SymbolTable* for_scope = node->symbol_table;
            if (!for_scope) {
                for_scope = symbol_table_create(symbols);
                node->symbol_table = for_scope;
            }

            if (node->for_stmt.init) infer_literal_types(node->for_stmt.init, for_scope, type_ctx, diag);
            if (node->for_stmt.condition) infer_literal_types(node->for_stmt.condition, for_scope, type_ctx, diag);
            if (node->for_stmt.update) infer_literal_types(node->for_stmt.update, for_scope, type_ctx, diag);
            infer_literal_types(node->for_stmt.body, for_scope, type_ctx, diag);
            break;
        }

        case AST_WHILE:
            infer_literal_types(node->while_stmt.condition, symbols, type_ctx, diag);
            infer_literal_types(node->while_stmt.body, symbols, type_ctx, diag);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                infer_literal_types(node->return_stmt.value, symbols, type_ctx, diag);
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
                infer_literal_types(target, symbols, type_ctx, diag);
            }
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_literal_types(node->compound_assignment.value, symbols, type_ctx, diag);

            // Infer type of the target (if it's a member/index access)
            if (node->compound_assignment.target) {
                infer_literal_types(node->compound_assignment.target, symbols, type_ctx, diag);
            }
            break;

        case AST_EXPR_STMT:
            infer_literal_types(node->expr_stmt.expression, symbols, type_ctx, diag);
            break;

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
            if (entry) {
                node->type_info = entry->type_info;
            } else if (identifier_is_enum_type(node->identifier.name, type_ctx)) {
                // This is an enum type name - it will be converted to enum variant in later pass
                // Set a placeholder type for now
                node->type_info = get_enum_type(node->identifier.name, type_ctx);
            } else if (!type_info_is_unknown(node->type_info)) {
                // Only report error on first encounter (when type is not yet UNKNOWN)
                TYPE_ERROR(diag, node->loc, TE_UNDEFINED_VARIABLE, node->identifier.name);
                node->type_info = Type_Unknown;
            }
            break;
        }

        case AST_ARRAY_LITERAL:
            // Infer types of all elements
            for (int i = 0; i < node->array_literal.count; i++) {
                infer_literal_types(node->array_literal.elements[i], symbols, type_ctx, diag);
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
                } else if (elem_type == Type_Str) {
                    node->type_info = Type_Array_Str;
                } else {
                    node->type_info = Type_Array_Int; // Default
                }
            } else {
                node->type_info = Type_Array_Int; // Empty array defaults to int
            }
            break;

        case AST_INDEX_ACCESS: {
            infer_literal_types(node->index_access.object, symbols, type_ctx, diag);
            infer_literal_types(node->index_access.index, symbols, type_ctx, diag);

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

            // For builtin indexable types (arrays), auto-implement Index and RefIndex traits
            trait_ensure_index_impl(index_target_type);
            trait_ensure_ref_index_impl(index_target_type);

            // Look up Index<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_Index, index_target_type,
                                                    type_param_bindings, 1);

            // If direct Index<IndexType> not found, try to find Index<T> where From<IndexType> exists for T
            if (!trait_impl && Trait_From && type_info_is_integer(index_type)) {
                // Common index types to try (in order of preference)
                TypeInfo* candidate_types[] = { Type_Usize, Type_I64, Type_I32 };
                for (int i = 0; i < 3 && !trait_impl; i++) {
                    TypeInfo* candidate = candidate_types[i];
                    if (candidate == index_type) continue; // Skip if same type

                    // Check if Index<candidate> exists on target type
                    TypeInfo* candidate_binding[] = { candidate };
                    TraitImpl* candidate_index = trait_find_impl(Trait_Index, index_target_type,
                                                                 candidate_binding, 1);
                    if (!candidate_index) continue;

                    // Check if From<index_type> exists for candidate
                    trait_ensure_from_impl(candidate, index_type);
                    TypeInfo* from_binding[] = { index_type };
                    TraitImpl* from_impl = trait_find_impl(Trait_From, candidate, from_binding, 1);

                    if (from_impl) {
                        // Found a conversion! Update index node's type info so codegen knows to convert
                        node->index_access.index->type_info = candidate;
                        trait_impl = candidate_index;
                        break;
                    }
                }
            }

            if (!trait_impl) {
                TYPE_ERROR(diag, node->loc, TE_ARRAY_INDEX_NON_INTEGER, "Type '%s' does not implement Index<%s>",
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
            infer_literal_types(node->index_assignment.object, symbols, type_ctx, diag);
            infer_literal_types(node->index_assignment.index, symbols, type_ctx, diag);
            infer_literal_types(node->index_assignment.value, symbols, type_ctx, diag);

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
            trait_ensure_ref_index_impl(index_target_type);

            // Look up RefIndex<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_RefIndex, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                TYPE_ERROR(diag, node->loc, TE_PROPERTY_NOT_FOUND, "Type '%s' does not implement RefIndex<%s> (required for index assignment)",
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
                infer_literal_types(node->object_literal.values[i], symbols, type_ctx, diag);
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

        case AST_STRUCT_LITERAL: {
            // Look up the struct type by name
            TypeInfo* struct_type = type_context_find_struct_type(type_ctx, node->struct_literal.struct_name);
            if (!struct_type) {
                log_error("Unknown struct type: %s", node->struct_literal.struct_name);
                return;
            }

            // Infer types of all field values
            for (int i = 0; i < node->struct_literal.field_count; i++) {
                infer_literal_types(node->struct_literal.field_values[i], symbols, type_ctx, diag);
            }

            // Set the struct type
            node->type_info = struct_type;

            log_verbose("Struct literal '%s' assigned type with %d fields",
                       node->struct_literal.struct_name, node->struct_literal.field_count);
            break;
        }

        case AST_ENUM_VARIANT: {
            // Resolve enum type if not already set (e.g., for variant construction created in parser)
            if (!node->enum_variant.enum_type) {
                node->enum_variant.enum_type = get_enum_type(node->enum_variant.enum_name, type_ctx);
            }

            // Resolve variant index and set type
            if (node->enum_variant.enum_type) {
                TypeInfo* enum_type = node->enum_variant.enum_type;

                // Find variant index
                for (int i = 0; i < enum_type->data.enum_type.variant_count; i++) {
                    if (strcmp(enum_type->data.enum_type.variant_names[i], node->enum_variant.variant_name) == 0) {
                        node->enum_variant.variant_index = i;
                        break;
                    }
                }

                if (node->enum_variant.variant_index == -1) {
                    TYPE_ERROR(diag, node->loc, TE_TYPE_ANNOTATION_REQUIRED,
                        "Enum '%s' does not have variant '%s'",
                        node->enum_variant.enum_name, node->enum_variant.variant_name);
                } else {
                    // If this is a variant construction with fields, type-check them
                    if (node->enum_variant.field_count > 0) {
                        int variant_idx = node->enum_variant.variant_index;
                        int expected_field_count = enum_type->data.enum_type.variant_field_counts[variant_idx];

                        // Check field count matches
                        if (node->enum_variant.field_count != expected_field_count) {
                            TYPE_ERROR(diag, node->loc, TE_INCOMPATIBLE_TYPES,
                                "Variant '%s' expects %d fields, got %d",
                                node->enum_variant.variant_name, expected_field_count, node->enum_variant.field_count);
                        } else {
                            // Type-check each field
                            char** expected_field_names = enum_type->data.enum_type.variant_field_names[variant_idx];
                            TypeInfo** expected_field_types = enum_type->data.enum_type.variant_field_types[variant_idx];

                            for (int i = 0; i < node->enum_variant.field_count; i++) {
                                // Check field name matches
                                bool found = false;
                                int field_pos = -1;
                                for (int j = 0; j < expected_field_count; j++) {
                                    if (strcmp(node->enum_variant.field_names[i], expected_field_names[j]) == 0) {
                                        found = true;
                                        field_pos = j;
                                        break;
                                    }
                                }

                                if (!found) {
                                    TYPE_ERROR(diag, node->loc, TE_INVALID_CAST,
                                        "Variant '%s' has no field '%s'",
                                        node->enum_variant.variant_name, node->enum_variant.field_names[i]);
                                    continue;
                                }

                                // Type-check the field value
                                infer_literal_types(node->enum_variant.field_values[i], symbols, type_ctx, diag);

                                TypeInfo* value_type = node->enum_variant.field_values[i]->type_info;
                                TypeInfo* expected_type = expected_field_types[field_pos];

                                // Simple pointer comparison for type checking
                                if (value_type != expected_type && value_type != Type_Unknown) {
                                    TYPE_ERROR(diag, node->loc, TE_TRAIT_NOT_IMPLEMENTED,
                                        "Type mismatch for field '%s': expected %s, got %s",
                                        node->enum_variant.field_names[i],
                                        expected_type->type_name ? expected_type->type_name : "unknown",
                                        value_type->type_name ? value_type->type_name : "unknown");
                                }
                            }
                        }
                    }
                }

                // Set the node's type to the enum type
                node->type_info = enum_type;
            } else {
                TYPE_ERROR(diag, node->loc, TE_CANNOT_INFER_TYPE,
                    "Unknown enum type '%s'", node->enum_variant.enum_name);
                node->type_info = Type_Unknown;
            }
            break;
        }

        case AST_PATTERN_MATCH: {
            // Type check pattern matching: expr is EnumType.Variant(bindings)
            // First, infer the type of the expression being matched
            infer_literal_types(node->pattern_match.expr, symbols, type_ctx, diag);

            // Look up the enum type
            TypeInfo* enum_type = get_enum_type(node->pattern_match.enum_name, type_ctx);
            if (!enum_type) {
                TYPE_ERROR(diag, node->loc, TE_INCOMPATIBLE_TYPES,
                    "Unknown enum type '%s' in pattern match", node->pattern_match.enum_name);
                node->type_info = Type_Bool; // Pattern match result is always bool
                break;
            }

            node->pattern_match.enum_type = enum_type;

            // Verify the expression type matches the enum type
            TypeInfo* expr_type = node->pattern_match.expr->type_info;
            if (expr_type && expr_type != Type_Unknown && expr_type != enum_type) {
                TYPE_ERROR(diag, node->loc, TE_INVALID_CAST,
                    "Type mismatch in pattern match: expected '%s', got '%s'",
                    enum_type->type_name, expr_type->type_name);
            }

            // Find the variant index
            int variant_index = -1;
            for (int i = 0; i < enum_type->data.enum_type.variant_count; i++) {
                if (strcmp(enum_type->data.enum_type.variant_names[i], node->pattern_match.variant_name) == 0) {
                    variant_index = i;
                    break;
                }
            }

            if (variant_index == -1) {
                TYPE_ERROR(diag, node->loc, TE_TRAIT_NOT_IMPLEMENTED,
                    "Enum '%s' does not have variant '%s'",
                    node->pattern_match.enum_name, node->pattern_match.variant_name);
                node->type_info = Type_Bool;
                break;
            }

            node->pattern_match.variant_index = variant_index;

            // Get variant field information
            int field_count = enum_type->data.enum_type.variant_field_counts[variant_index];
            TypeInfo** field_types = enum_type->data.enum_type.variant_field_types[variant_index];

            // Determine if this is struct binding or destructuring
            int non_wildcard_count = 0;
            for (int i = 0; i < node->pattern_match.binding_count; i++) {
                if (!node->pattern_match.binding_is_wildcard[i]) {
                    non_wildcard_count++;
                }
            }

            // Struct binding: one binding for multiple fields (binds whole struct)
            // Destructuring: binding count matches field count (extracts individual fields)
            // Special case: if variant has only 1 field, always destructure (not struct bind)
            if (non_wildcard_count == 1 && node->pattern_match.binding_count == 1 && field_count > 1) {
                // Single binding for multiple fields - bind all fields as struct
                node->pattern_match.is_struct_binding = true;

                // Look up the variant struct type created during enum registration
                // Type name is "EnumName.VariantName"
                size_t struct_name_len = strlen(node->pattern_match.enum_name) +
                                        strlen(node->pattern_match.variant_name) + 2;
                char* struct_name = malloc(struct_name_len);
                snprintf(struct_name, struct_name_len, "%s.%s",
                        node->pattern_match.enum_name, node->pattern_match.variant_name);

                TypeInfo* struct_type = type_context_find_struct_type(type_ctx, struct_name);
                free(struct_name);

                if (!struct_type) {
                    // Fallback: variant has no fields or struct type wasn't created
                    TYPE_ERROR(diag, node->loc, TE_AMBIGUOUS_TYPE,
                        "Cannot find struct type for variant '%s.%s'",
                        node->pattern_match.enum_name, node->pattern_match.variant_name);
                    node->pattern_match.binding_types[0] = Type_Unknown;
                } else {
                    node->pattern_match.binding_types[0] = struct_type;
                }
            } else {
                // Multiple bindings or wildcards - destructure
                node->pattern_match.is_struct_binding = false;

                if (node->pattern_match.binding_count != field_count) {
                    TYPE_ERROR(diag, node->loc, TE_METHOD_NOT_FOUND,
                        "Pattern binding count mismatch: variant '%s' has %d fields, but pattern has %d bindings",
                        node->pattern_match.variant_name, field_count, node->pattern_match.binding_count);
                }

                // Assign types to each binding
                for (int i = 0; i < node->pattern_match.binding_count && i < field_count; i++) {
                    if (!node->pattern_match.binding_is_wildcard[i]) {
                        node->pattern_match.binding_types[i] = field_types[i];
                    }
                }
            }

            // Pattern match result type is always bool
            node->type_info = Type_Bool;
            break;
        }

        case AST_MEMBER_ACCESS: {
            infer_literal_types(node->member_access.object, symbols, type_ctx, diag);

            // Try to infer the type from the object
            ASTNode* obj = node->member_access.object;
            TypeInfo* obj_type_info = NULL;

            if (obj->type == AST_IDENTIFIER) {
                const char* obj_name = obj->identifier.name;
                const char* member_name = node->member_access.property;

                // Look up the identifier in the symbol table
                SymbolEntry* entry = symbol_table_lookup(symbols, obj_name);

                // Check if this is an enum type (e.g., "Color" in "Color.red")
                if (identifier_is_enum_type(obj_name, type_ctx)) {
                    // This is an enum variant access! Convert AST_MEMBER_ACCESS to AST_ENUM_VARIANT
                    TypeInfo* enum_type = get_enum_type(obj_name, type_ctx);

                    // Save the old data before transforming the node
                    ASTNode* old_object = node->member_access.object;
                    char* old_property = node->member_access.property;

                    // Transform the node type and set enum_variant fields
                    node->type = AST_ENUM_VARIANT;
                    node->enum_variant.enum_name = strdup(obj_name);
                    node->enum_variant.variant_name = strdup(member_name);
                    node->enum_variant.enum_type = enum_type;
                    node->enum_variant.variant_index = -1; // Will be resolved below
                    // Initialize construction fields (not used for simple references)
                    node->enum_variant.field_names = NULL;
                    node->enum_variant.field_values = NULL;
                    node->enum_variant.field_count = 0;

                    // Free the old member_access data
                    ast_free(old_object);
                    free(old_property);

                    // Now resolve the variant index and set type (same as AST_ENUM_VARIANT case below)
                    for (int i = 0; i < enum_type->data.enum_type.variant_count; i++) {
                        if (strcmp(enum_type->data.enum_type.variant_names[i], node->enum_variant.variant_name) == 0) {
                            node->enum_variant.variant_index = i;
                            break;
                        }
                    }

                    if (node->enum_variant.variant_index == -1) {
                        TYPE_ERROR(diag, node->loc, TE_TYPE_ANNOTATION_REQUIRED,
                            "Enum '%s' does not have variant '%s'",
                            node->enum_variant.enum_name, node->enum_variant.variant_name);
                    }

                    node->type_info = enum_type;
                    break;
                }

                // Check if this is an imported namespace (e.g., "math" in "math.add")
                if (symbol_is_namespace(entry)) {
                    // This is a namespace! Resolve the member from the imported module
                    Module* imported_module = symbol_get_imported_module(entry);

                    // Look up the export in the imported module
                    ExportedSymbol* exported = module_find_export(imported_module, member_name);

                    if (exported && exported->declaration) {
                        // Found the export! Get its type
                        ASTNode* decl = exported->declaration;

                        if (decl->type == AST_FUNCTION_DECL) {
                            // For functions, look up the function type from TypeContext
                            // Use the mangled name for lookup
                            char* mangled_name = module_mangle_symbol(imported_module->module_prefix, member_name);
                            TypeInfo* func_type = type_context_find_function_type(type_ctx, mangled_name);
                            free(mangled_name);

                            if (func_type) {
                                node->type_info = func_type;
                            } else {
                                node->type_info = Type_Unknown;
                            }
                        } else if (decl->type == AST_VAR_DECL) {
                            // For constants, get the type from the declaration
                            node->type_info = decl->type_info ? decl->type_info : Type_Unknown;
                        } else if (decl->type == AST_STRUCT_DECL) {
                            // For struct types (e.g., test.assert), look up the type in the module's TypeContext
                            TypeInfo* struct_type = type_context_find_struct_type(imported_module->type_ctx, member_name);
                            if (struct_type) {
                                node->type_info = struct_type;
                            } else {
                                node->type_info = Type_Unknown;
                            }
                        } else {
                            node->type_info = Type_Unknown;
                        }

                        // Store symbol entry for codegen (namespace entry)
                        node->member_access.symbol_entry = entry;
                        break;
                    } else {
                        // Member not found in namespace
                        TYPE_ERROR(diag, node->loc, CE_CODEGEN_FAILED,
                                  "Module '%s' has no exported member '%s'",
                                  obj_name, member_name);
                        node->type_info = Type_Unknown;
                        break;
                    }
                }

                // Not a namespace, regular identifier
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
                trait_ensure_length_impl(target_type_info);

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

        case AST_NEW_EXPR: {
            // Infer type of size expression
            infer_literal_types(node->new_expr.size_expr, symbols, type_ctx, diag);

            // Resolve element type if it's unknown (could be a struct type or primitive)
            if (node->new_expr.element_type->kind == TYPE_KIND_UNKNOWN) {
                const char* type_name = node->new_expr.element_type->type_name;
                TypeInfo* resolved = NULL;

                // Try primitives first
                if (strcmp(type_name, "bool") == 0) {
                    resolved = Type_Bool;
                } else if (strcmp(type_name, "string") == 0) {
                    resolved = Type_Str;
                } else if (strcmp(type_name, "double") == 0) {
                    resolved = Type_Double;
                } else {
                    // Try to find struct type in context
                    resolved = type_context_find_struct_type(type_ctx, type_name);
                }

                if (resolved) {
                    // Free the temporary unknown type and use the resolved one
                    free((char*)node->new_expr.element_type->type_name);
                    free(node->new_expr.element_type);
                    node->new_expr.element_type = resolved;
                } else {
                    TYPE_ERROR(diag, node->loc, TE_MISSING_RETURN, "Unknown type '%s' in new expression", type_name);
                    node->type_info = Type_Unknown;
                    break;
                }
            }

            // new T[size] returns ref T[] (ref to array of T)
            // Create array type T[]
            TypeInfo* array_type = type_info_create_array(node->new_expr.element_type);
            // Wrap in ref type
            node->type_info = type_context_get_or_create_ref_type(type_ctx, array_type, true);
            break;
        }

        case AST_DELETE_EXPR: {
            // Infer type of operand
            infer_literal_types(node->delete_expr.operand, symbols, type_ctx, diag);

            // Validate that operand is a ref type
            TypeInfo* operand_type = node->delete_expr.operand->type_info;
            if (!type_info_is_ref(operand_type)) {
                TYPE_ERROR(diag, node->loc, TE_CANNOT_DELETE_NON_REF);
            }

            // delete returns void
            node->type_info = Type_Void;
            break;
        }

        default:
            break;
    }
}

static void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node, TypeInfo** arg_types, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag) {
    if (!spec || !original_func_node || original_func_node->type != AST_FUNCTION_DECL) {
        return;
    }

    // Use existing cloned body if available (from Pass 1), otherwise clone now
    ASTNode* cloned_body = spec->specialized_body;
    SymbolTable* temp_symbols = NULL;

    if (cloned_body) {
        // Body already cloned in Pass 1, use its symbol table
        if (!cloned_body->symbol_table) {
            // This shouldn't happen - body should have been set up with symbol table in Pass 1
            if (diag) {
                JSASTA_ERROR(diag, original_func_node->loc, JE_INTERNAL_ERROR,
                    "Cloned body exists but has no symbol table for %s", spec->specialized_name);
            }
            return;
        }
        temp_symbols = cloned_body->symbol_table;
    } else {
        // Clone the body now (for functions not specialized in Pass 1)
        cloned_body = ast_clone(original_func_node->func_decl.body);
        temp_symbols = symbol_table_create(symbols);
    }

    // Insert parameters with their concrete types AND TypeInfo for objects
    for (int i = 0; i < spec->param_count; i++) {
        symbol_table_insert(temp_symbols, original_func_node->func_decl.params[i], spec->param_type_info[i], NULL, false);

        // Set param_index and node for LSP go-to-definition
        SymbolEntry* param_entry = symbol_table_lookup(temp_symbols, original_func_node->func_decl.params[i]);
        if (param_entry) {
            param_entry->param_index = i;
            param_entry->node = original_func_node;  // Point to the function declaration
        }

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
    // infer_literal_types is called inside iterative_specialization_discovery, no need to call it here
    iterative_specialization_discovery(cloned_body, temp_symbols, ctx, diag);

    // Infer return type from function body
    TypeInfo* inferred_return = infer_function_return_type_with_params(cloned_body, temp_symbols, diag);
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
static void analyze_call_sites(ASTNode* node, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM: {
            // Use the program's own symbol table if it was created
            SymbolTable* prog_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->program.count; i++) {
                analyze_call_sites(node->program.statements[i], prog_symbols, ctx, diag);
            }
            break;
        }

        case AST_BLOCK: {
            // Use the block's own symbol table if it was created
            SymbolTable* block_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->block.count; i++) {
                analyze_call_sites(node->block.statements[i], block_symbols, ctx, diag);
            }
            break;
        }

        case AST_CALL: {
            // First analyze arguments
            for (int i = 0; i < node->call.arg_count; i++) {
                analyze_call_sites(node->call.args[i], symbols, ctx, diag);
                infer_with_specializations(node->call.args[i], symbols, ctx, diag);
            }

            // Handle namespace member access calls (e.g., math.add(5, 3))
            if (node->call.callee->type == AST_MEMBER_ACCESS) {
                ASTNode* obj = node->call.callee->member_access.object;
                const char* member_name = node->call.callee->member_access.property;

                // Check if object is a namespace (imported module)
                if (obj->type == AST_IDENTIFIER) {
                    SymbolEntry* obj_entry = node->call.callee->member_access.symbol_entry;

                    if (symbol_is_namespace(obj_entry)) {
                        Module* imported_module = symbol_get_imported_module(obj_entry);

                        // Find the exported function
                        ExportedSymbol* exported = module_find_export(imported_module, member_name);
                        if (exported && exported->declaration && exported->declaration->type == AST_FUNCTION_DECL) {
                            ASTNode* func_decl = exported->declaration;

                            // Use mangled name for specialization
                            char* mangled_name = module_mangle_symbol(imported_module->module_prefix, member_name);

                            // Collect argument types
                            TypeInfo** arg_types = malloc(sizeof(TypeInfo*) * node->call.arg_count);
                            bool all_known = true;

                            for (int i = 0; i < node->call.arg_count; i++) {
                                arg_types[i] = node->call.args[i]->type_info;
                                if (type_info_is_unknown(arg_types[i])) {
                                    all_known = false;
                                }
                            }

                            // Create specialization if all types are known
                            if (all_known) {
                                FunctionSpecialization* spec = specialization_context_add_by_type_info(ctx, mangled_name, arg_types, node->call.arg_count);
                                if (spec) {
                                    specialization_create_body(spec, func_decl, arg_types, symbols, ctx, diag);

                                    // Set the call node's return type
                                    if (spec->return_type_info) {
                                        node->type_info = spec->return_type_info;
                                    }
                                }
                            }

                            free(arg_types);
                            free(mangled_name);
                            break;
                        }
                    }
                }
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
                            // But we still need to set the call node's return type from the existing specialization
                            if (entry->type_info->data.function.specializations) {
                                node->type_info = entry->type_info->data.function.specializations->return_type_info;
                            }
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
                                    TYPE_ERROR(diag, node->loc, TE_TYPE_MISMATCH,
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
                    if (all_known) {
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
                        	specialization_create_body(spec, func_decl, arg_types, symbols, ctx, diag);

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
            analyze_call_sites(node->method_call.object, symbols, ctx, diag);
            infer_with_specializations(node->method_call.object, symbols, ctx, diag);

            for (int i = 0; i < node->method_call.arg_count; i++) {
                analyze_call_sites(node->method_call.args[i], symbols, ctx, diag);
                infer_with_specializations(node->method_call.args[i], symbols, ctx, diag);
            }

            // Check if this is a namespace member access (e.g., math.add(5, 3))
            if (node->method_call.object->type == AST_IDENTIFIER) {
                const char* obj_name = node->method_call.object->identifier.name;
                SymbolEntry* obj_entry = symbol_table_lookup(symbols, obj_name);

                if (symbol_is_namespace(obj_entry)) {
                    // This is a namespace call!
                    Module* imported_module = symbol_get_imported_module(obj_entry);
                    const char* member_name = node->method_call.method_name;

                    // Find the exported function
                    ExportedSymbol* exported = module_find_export(imported_module, member_name);
                    if (exported && exported->declaration && exported->declaration->type == AST_FUNCTION_DECL) {
                        ASTNode* func_decl = exported->declaration;

                        // Call validation callback if present
                        // Use the caller's TypeContext for validation (types/traits are in caller's context)
                        if (func_decl->func_decl.validate_callback) {
                            if (!func_decl->func_decl.validate_callback(node, ctx, diag)) {
                                // Validation failed, error already reported
                                break;
                            }
                        }

                        // Use mangled name for specialization
                        char* mangled_name = module_mangle_symbol(imported_module->module_prefix, member_name);

                        // Collect argument types
                        TypeInfo** arg_types = malloc(sizeof(TypeInfo*) * node->method_call.arg_count);
                        bool all_known = true;

                        for (int i = 0; i < node->method_call.arg_count; i++) {
                            arg_types[i] = node->method_call.args[i]->type_info;
                            if (type_info_is_unknown(arg_types[i])) {
                                all_known = false;
                            }
                        }

                        // Create specialization if all types are known
                        if (all_known) {
                            // Use the imported module's TypeContext, not the caller's!
                            TypeContext* module_type_ctx = imported_module->ast->type_ctx;

                            // Look up the function in the module's TypeContext (without mangling)
                            TypeInfo* module_func_type = type_context_find_function_type(module_type_ctx, member_name);

                            if (!module_func_type) {
                                log_warning("Function '%s' not found in module '%s' TypeContext", member_name, imported_module->relative_path);
                                break;
                            }

                            // For variadic or external (builtin) functions, still set return type but skip body specialization
                            if (func_decl->func_decl.is_variadic || func_decl->func_decl.body == NULL) {
                                // External/builtin functions don't need specialization
                                // Just set the return type from the function declaration
                                if (func_decl->func_decl.return_type_hint) {
                                    node->type_info = func_decl->func_decl.return_type_hint;
                                }
                                free(arg_types);
                                free(mangled_name);
                                break;
                            }

                            // Add specialization to the module's TypeContext
                            FunctionSpecialization* spec = type_context_add_specialization(module_type_ctx, module_func_type, arg_types, node->method_call.arg_count);

                            if (spec) {
                                log_verbose("  Analyzing body of %s using module's own context", mangled_name);
                                // Use the imported module's symbol table
                                SymbolTable* module_symbols = imported_module->module_scope;
                                specialization_create_body(spec, func_decl, arg_types, module_symbols, module_type_ctx, diag);
                                log_verbose("  Completed body analysis of %s", mangled_name);

                                // Set the call node's return type
                                if (spec->return_type_info) {
                                    node->type_info = spec->return_type_info;
                                }
                            }
                        }

                        free(arg_types);
                        free(mangled_name);
                        break;
                    }
                }
            }

            // Check if this is a static method on an imported type (e.g., test.assert.equals())
            if (node->method_call.is_static && node->method_call.object->type == AST_MEMBER_ACCESS) {
                ASTNode* member_obj = node->method_call.object->member_access.object;
                if (member_obj->type == AST_IDENTIFIER) {
                    SymbolEntry* entry = symbol_table_lookup(symbols, member_obj->identifier.name);
                    if (symbol_is_namespace(entry)) {
                        // This is namespace.Type.method() - handle validation
                        Module* imported_module = symbol_get_imported_module(entry);
                        const char* type_member = node->method_call.object->member_access.property;

                        // Look for the struct declaration
                        ExportedSymbol* type_export = module_find_export(imported_module, type_member);
                        if (type_export && type_export->declaration &&
                            type_export->declaration->type == AST_STRUCT_DECL) {
                            // Find the method in the struct
                            ASTNode* struct_decl = type_export->declaration;
                            const char* method_name = node->method_call.method_name;

                            // Build mangled method name
                            char mangled_method[256];
                            snprintf(mangled_method, sizeof(mangled_method), "%s.%s", type_member, method_name);

                            for (int i = 0; i < struct_decl->struct_decl.method_count; i++) {
                                ASTNode* method = struct_decl->struct_decl.methods[i];
                                if (strcmp(method->func_decl.name, mangled_method) == 0) {
                                    // Found the method - call validation callback if present
                                    if (method->func_decl.validate_callback) {
                                        if (!method->func_decl.validate_callback(node, ctx, diag)) {
                                            // Validation failed, error already reported
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
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
                    TYPE_ERROR(diag, node->loc, TE_CANNOT_CALL_METHOD_ON_NON_OBJECT);
                    break;
                }
            }

            // Methods are fully typed and already validated during infer_literal_types
            // The method call node already has its type_info set if the method was found
            // No need to re-validate here since methods may have module-prefixed names
            break;
        }

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                analyze_call_sites(node->var_decl.init, symbols, ctx, diag);
            }
            break;

        case AST_ASSIGNMENT:
            analyze_call_sites(node->assignment.value, symbols, ctx, diag);
            break;

        case AST_MEMBER_ASSIGNMENT:
            analyze_call_sites(node->member_assignment.object, symbols, ctx, diag);
            analyze_call_sites(node->member_assignment.value, symbols, ctx, diag);
            break;

        case AST_COMPOUND_ASSIGNMENT:
            analyze_call_sites(node->compound_assignment.value, symbols, ctx, diag);
            if (node->compound_assignment.target) {
                analyze_call_sites(node->compound_assignment.target, symbols, ctx, diag);
            }
            break;

        case AST_TERNARY:
            analyze_call_sites(node->ternary.condition, symbols, ctx, diag);
            analyze_call_sites(node->ternary.true_expr, symbols, ctx, diag);
            analyze_call_sites(node->ternary.false_expr, symbols, ctx, diag);
            break;

        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                analyze_call_sites(node->array_literal.elements[i], symbols, ctx, diag);
            }
            break;

        case AST_INDEX_ACCESS:
            analyze_call_sites(node->index_access.object, symbols, ctx, diag);
            analyze_call_sites(node->index_access.index, symbols, ctx, diag);
            break;

        case AST_INDEX_ASSIGNMENT:
            analyze_call_sites(node->index_assignment.object, symbols, ctx, diag);
            analyze_call_sites(node->index_assignment.index, symbols, ctx, diag);
            analyze_call_sites(node->index_assignment.value, symbols, ctx, diag);
            break;

        case AST_BINARY_OP:
            analyze_call_sites(node->binary_op.left, symbols, ctx, diag);
            analyze_call_sites(node->binary_op.right, symbols, ctx, diag);
            break;

        case AST_UNARY_OP:
            analyze_call_sites(node->unary_op.operand, symbols, ctx, diag);
            break;

        case AST_IF:
            analyze_call_sites(node->if_stmt.condition, symbols, ctx, diag);
            analyze_call_sites(node->if_stmt.then_branch, symbols, ctx, diag);
            if (node->if_stmt.else_branch) {
                analyze_call_sites(node->if_stmt.else_branch, symbols, ctx, diag);
            }
            break;

        case AST_FOR: {
            // Use the for loop's own symbol table if it was created
            SymbolTable* for_symbols = node->symbol_table ? node->symbol_table : symbols;
            if (node->for_stmt.init) analyze_call_sites(node->for_stmt.init, for_symbols, ctx, diag);
            if (node->for_stmt.condition) analyze_call_sites(node->for_stmt.condition, for_symbols, ctx, diag);
            if (node->for_stmt.update) analyze_call_sites(node->for_stmt.update, for_symbols, ctx, diag);
            analyze_call_sites(node->for_stmt.body, for_symbols, ctx, diag);
            break;
        }

        case AST_WHILE:
            analyze_call_sites(node->while_stmt.condition, symbols, ctx, diag);
            analyze_call_sites(node->while_stmt.body, symbols, ctx, diag);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                analyze_call_sites(node->return_stmt.value, symbols, ctx, diag);
            }
            break;

        case AST_BREAK:
        case AST_CONTINUE:
            // Nothing to analyze for break/continue
            break;

        case AST_EXPR_STMT:
            analyze_call_sites(node->expr_stmt.expression, symbols, ctx, diag);
            break;

        case AST_OBJECT_LITERAL:
            for (int i = 0; i < node->object_literal.count; i++) {
                analyze_call_sites(node->object_literal.values[i], symbols, ctx, diag);
            }
            break;

        case AST_STRUCT_LITERAL:
            for (int i = 0; i < node->struct_literal.field_count; i++) {
                analyze_call_sites(node->struct_literal.field_values[i], symbols, ctx, diag);
            }
            break;

        case AST_MEMBER_ACCESS:
            analyze_call_sites(node->member_access.object, symbols, ctx, diag);
            break;

        case AST_ENUM_VARIANT:
            // Analyze field values if present
            for (int i = 0; i < node->enum_variant.field_count; i++) {
                analyze_call_sites(node->enum_variant.field_values[i], symbols, ctx, diag);
            }
            break;

        case AST_PATTERN_MATCH:
            // Analyze the expression being matched
            analyze_call_sites(node->pattern_match.expr, symbols, ctx, diag);
            break;

        case AST_NEW_EXPR:
            analyze_call_sites(node->new_expr.size_expr, symbols, ctx, diag);
            break;

        case AST_DELETE_EXPR:
            analyze_call_sites(node->delete_expr.operand, symbols, ctx, diag);
            break;

        default:
            break;
    }
}

// Pass 4: Create specialized function versions
static void create_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM: {
            // Use the program's own symbol table if it was created
            SymbolTable* prog_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->program.count; i++) {
                create_specializations(node->program.statements[i], prog_symbols, ctx, diag);
            }
            break;
        }

        case AST_BLOCK: {
            // Use the block's own symbol table if it was created
            SymbolTable* block_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->block.count; i++) {
                create_specializations(node->block.statements[i], block_symbols, ctx, diag);
            }
            break;
        }

        case AST_FUNCTION_DECL:
            // Function declarations are handled through call sites
            break;

        default:
            break;
    }
}

// Pass 5: Final type inference with all specializations known
static void infer_with_specializations(ASTNode* node, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM: {
            // Use the program's own symbol table if it was created
            SymbolTable* prog_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->program.count; i++) {
                infer_with_specializations(node->program.statements[i], prog_symbols, ctx, diag);
            }
            break;
        }

        case AST_BLOCK: {
            // Use the block's own symbol table if it was created
            SymbolTable* block_symbols = node->symbol_table ? node->symbol_table : symbols;
            for (int i = 0; i < node->block.count; i++) {
                infer_with_specializations(node->block.statements[i], block_symbols, ctx, diag);
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
            infer_with_specializations(node->binary_op.left, symbols, ctx, diag);
            infer_with_specializations(node->binary_op.right, symbols, ctx, diag);
            // Binary op type inferred from operands
            node->type_info = infer_binary_result_type(&node->loc,
            																				   node->binary_op.op,
                                                       node->binary_op.left->type_info,
                                                       node->binary_op.right->type_info);
            break;

        case AST_UNARY_OP:
            infer_with_specializations(node->unary_op.operand, symbols, ctx, diag);
            if (strcmp(node->unary_op.op, "!") == 0) {
                node->type_info = Type_Bool;
            } else if (strcmp(node->unary_op.op, "ref") == 0) {
                // ref operator creates a reference type
                TypeInfo* operand_type = node->unary_op.operand->type_info;
                node->type_info = type_context_get_or_create_ref_type(ctx, operand_type, true);
            } else {
                node->type_info = node->unary_op.operand->type_info;
            }
            break;

        case AST_VAR_DECL:
            if (node->var_decl.init) {
                infer_with_specializations(node->var_decl.init, symbols, ctx, diag);
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
            infer_with_specializations(node->assignment.value, symbols, ctx, diag);
            node->type_info = node->assignment.value->type_info;
            // Store pointer to the symbol entry for fast access in codegen
            if (!node->assignment.symbol_entry) {
                node->assignment.symbol_entry = symbol_table_lookup(symbols, node->assignment.name);
            }
            break;

        case AST_TERNARY:
            infer_with_specializations(node->ternary.condition, symbols, ctx, diag);
            infer_with_specializations(node->ternary.true_expr, symbols, ctx, diag);
            infer_with_specializations(node->ternary.false_expr, symbols, ctx, diag);
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
                infer_with_specializations(node->array_literal.elements[i], symbols, ctx, diag);
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
                } else if (elem_type == Type_Str) {
                    node->type_info = Type_Array_Str;
                } else {
                    node->type_info = Type_Array_Int; // Default
                }
            } else {
                node->type_info = Type_Array_Int; // Empty array defaults to int
            }
            break;

        case AST_INDEX_ACCESS: {
            infer_with_specializations(node->index_access.object, symbols, ctx, diag);
            infer_with_specializations(node->index_access.index, symbols, ctx, diag);

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

            // For builtin indexable types (arrays), auto-implement Index and RefIndex traits
            trait_ensure_index_impl(index_target_type);
            trait_ensure_ref_index_impl(index_target_type);

            // Look up Index<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_Index, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                TYPE_ERROR(diag, node->loc, TE_ARRAY_INDEX_NON_INTEGER, "Type '%s' does not implement Index<%s>",
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
            infer_with_specializations(node->index_assignment.object, symbols, ctx, diag);
            infer_with_specializations(node->index_assignment.index, symbols, ctx, diag);
            infer_with_specializations(node->index_assignment.value, symbols, ctx, diag);

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
            trait_ensure_ref_index_impl(index_target_type);

            // Look up RefIndex<IndexType> trait implementation on the target type
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_RefIndex, index_target_type,
                                                    type_param_bindings, 1);

            if (!trait_impl) {
                TYPE_ERROR(diag, node->loc, TE_PROPERTY_NOT_FOUND, "Type '%s' does not implement RefIndex<%s> (required for index assignment)",
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
                infer_with_specializations(node->call.args[i], symbols, ctx, diag);
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
                    log_verbose("DEBUG: Found specialization for '%s'", func_name);
                    node->type_info = spec->return_type_info;
                } else {
                    // Unknown function - report error
                    if (diag) {
                        JSASTA_ERROR(diag, node->loc,
                            TE_FUNCTION_NOT_FOUND, func_name);
                    }
                    node->type_info = Type_Void;
                }

                free(arg_types);
            } else if (node->call.callee->type == AST_MEMBER_ACCESS) {
            		// TODO: We can infer alot more here, and do our job eaier in the next phases
              //
                // Default for member access
                // REMOVED: get_node_value_type(node) = TYPE_VOID;
            }
            break;
        }

        case AST_METHOD_CALL: {
            // Infer types for object and arguments
            infer_with_specializations(node->method_call.object, symbols, ctx, diag);
            for (int i = 0; i < node->method_call.arg_count; i++) {
                infer_with_specializations(node->method_call.args[i], symbols, ctx, diag);
            }

            // Check if this is a namespace member access (e.g., math.add(5, 3))
            if (node->method_call.object->type == AST_IDENTIFIER) {
                const char* obj_name = node->method_call.object->identifier.name;
                SymbolEntry* obj_entry = symbol_table_lookup(symbols, obj_name);

                if (symbol_is_namespace(obj_entry)) {
                    // This is a namespace call!
                    Module* imported_module = symbol_get_imported_module(obj_entry);
                    const char* member_name = node->method_call.method_name;

                    // Use mangled name to look up the specialization
                    char* mangled_name = module_mangle_symbol(imported_module->module_prefix, member_name);

                    // Find the specialization and set the return type
                    TypeInfo* func_type = type_context_find_function_type(ctx, mangled_name);
                    if (func_type && func_type->data.function.specializations) {
                        // Find the matching specialization by argument types
                        TypeInfo** arg_types = malloc(sizeof(TypeInfo*) * node->method_call.arg_count);
                        for (int i = 0; i < node->method_call.arg_count; i++) {
                            arg_types[i] = node->method_call.args[i]->type_info;
                        }

                        FunctionSpecialization* spec = specialization_context_find_by_type_info(
                            ctx, mangled_name, arg_types, node->method_call.arg_count);

                        if (spec && spec->return_type_info) {
                            node->type_info = spec->return_type_info;
                        } else {
                            node->type_info = Type_Unknown;
                        }

                        free(arg_types);
                    } else {
                        // No specialization - check if this is an external/builtin function with return_type_hint
                        ExportedSymbol* exported = module_find_export(imported_module, member_name);
                        if (exported && exported->declaration && exported->declaration->type == AST_FUNCTION_DECL) {
                            ASTNode* func_decl = exported->declaration;
                            if (func_decl->func_decl.return_type_hint) {
                                node->type_info = func_decl->func_decl.return_type_hint;
                            } else {
                                node->type_info = Type_Unknown;
                            }
                        } else {
                            node->type_info = Type_Unknown;
                        }
                    }

                    free(mangled_name);
                    break;
                }
            }

            // Check if this is a static method call on an imported type (e.g., test.assert.equals())
            // In this case, the object is a member access like "test.assert"
            TypeContext* lookup_ctx = ctx; // Default to caller's context
            if (node->method_call.is_static && node->method_call.object->type == AST_MEMBER_ACCESS) {
                ASTNode* member_obj = node->method_call.object->member_access.object;
                if (member_obj->type == AST_IDENTIFIER) {
                    SymbolEntry* entry = symbol_table_lookup(symbols, member_obj->identifier.name);
                    if (symbol_is_namespace(entry)) {
                        // This is namespace.Type.method() - use the module's TypeContext
                        Module* imported_module = symbol_get_imported_module(entry);
                        lookup_ctx = imported_module->type_ctx;
                        log_verbose("[METHOD_CALL] Using imported module's TypeContext for lookup: %s",
                                   imported_module->relative_path);
                    }
                }
            }

            // Build the mangled function name: StructName.method_name
            char mangled_name[256];
            TypeInfo* target_type = NULL;  // For instance methods, stores unwrapped object type

            if (node->method_call.is_static) {
                // Static method: Type.method
                // Get type name from type_info (works for both identifier and namespace.Type cases)
                const char* type_name = node->method_call.object->type_info ?
                                        node->method_call.object->type_info->type_name : "unknown";
                snprintf(mangled_name, sizeof(mangled_name), "%s.%s", type_name, node->method_call.method_name);
            } else {
                // Instance method: need to determine the type from the object
                TypeInfo* obj_type = node->method_call.object->type_info;

                // Unwrap ref types to get the actual object type
                target_type = type_info_get_ref_target(obj_type);

                if (target_type && type_info_is_object(target_type)) {
                    snprintf(mangled_name, sizeof(mangled_name), "%s.%s", target_type->type_name, node->method_call.method_name);
                } else {
                    TYPE_ERROR(diag, node->loc, TE_CANNOT_CALL_METHOD_ON_NON_OBJECT);
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
                // Use target_type which was already computed above (unwraps ref types)
                if (target_type && type_info_is_object(target_type)) {
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

            log_verbose("[METHOD_CALL] Looking up: %s with %d args", mangled_name, total_args);
            for (int i = 0; i < total_args; i++) {
                log_verbose("  arg[%d]: %s", i, arg_types[i] ? arg_types[i]->type_name : "NULL");
            }

            FunctionSpecialization* spec = specialization_context_find_by_type_info(
                lookup_ctx, mangled_name, arg_types, total_args);

            if (spec) {
                node->type_info = spec->return_type_info;
                // Store the resolved specialization for codegen to avoid redundant lookups
                node->method_call.resolved_spec = spec;
                log_verbose("[METHOD_CALL] %s -> return type: %s, resolved to: %s",
                           mangled_name,
                           spec->return_type_info ? spec->return_type_info->type_name : "NULL",
                           spec->specialized_name);
            } else {
                // Specialization not found - check if this is a builtin/external function
                TypeInfo* method_func_type = type_context_find_function_type(lookup_ctx, mangled_name);
                if (method_func_type && method_func_type->data.function.original_body == NULL) {
                    // Builtin function - use return type hint directly
                    if (method_func_type->data.function.return_type) {
                        node->type_info = method_func_type->data.function.return_type;
                        node->method_call.resolved_spec = NULL; // No specialization for builtins
                        log_verbose("[METHOD_CALL] %s -> builtin, return type: %s",
                                   mangled_name,
                                   node->type_info ? node->type_info->type_name : "NULL");
                    } else {
                        node->type_info = Type_Unknown;
                        node->method_call.resolved_spec = NULL;
                    }
                } else {
                    log_verbose("[METHOD_CALL] %s -> NOT FOUND", mangled_name);
                    TYPE_ERROR(diag, node->loc, TE_CANNOT_CALL_METHOD_ON_NON_OBJECT, "Method '%s' not found or type mismatch", mangled_name);
                    node->type_info = Type_Unknown;
                    node->method_call.resolved_spec = NULL;
                }
            }

            free(arg_types);
            break;
        }

        case AST_IF:
            infer_with_specializations(node->if_stmt.condition, symbols, ctx, diag);
            infer_with_specializations(node->if_stmt.then_branch, symbols, ctx, diag);
            if (node->if_stmt.else_branch) {
                infer_with_specializations(node->if_stmt.else_branch, symbols, ctx, diag);
            }
            break;

        case AST_FOR: {
            // Use the for loop's own symbol table if it was created
            SymbolTable* for_symbols = node->symbol_table ? node->symbol_table : symbols;
            if (node->for_stmt.init) infer_with_specializations(node->for_stmt.init, for_symbols, ctx, diag);
            if (node->for_stmt.condition) infer_with_specializations(node->for_stmt.condition, for_symbols, ctx, diag);
            if (node->for_stmt.update) infer_with_specializations(node->for_stmt.update, for_symbols, ctx, diag);
            infer_with_specializations(node->for_stmt.body, for_symbols, ctx, diag);
            break;
        }

        case AST_WHILE:
            infer_with_specializations(node->while_stmt.condition, symbols, ctx, diag);
            infer_with_specializations(node->while_stmt.body, symbols, ctx, diag);
            break;

        case AST_RETURN:
            if (node->return_stmt.value) {
                infer_with_specializations(node->return_stmt.value, symbols, ctx, diag);
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
                infer_with_specializations(target, symbols, ctx, diag);
            }
            break;
        }

        case AST_COMPOUND_ASSIGNMENT:
            // Infer type of the value expression
            infer_with_specializations(node->compound_assignment.value, symbols, ctx, diag);

            // Infer type of the target (if it's a member/index access)
            if (node->compound_assignment.target) {
                infer_with_specializations(node->compound_assignment.target, symbols, ctx, diag);
            }
            break;

        case AST_EXPR_STMT:
            infer_with_specializations(node->expr_stmt.expression, symbols, ctx, diag);
            break;

        case AST_OBJECT_LITERAL:
            for (int i = 0; i < node->object_literal.count; i++) {
                infer_with_specializations(node->object_literal.values[i], symbols, ctx, diag);
            }
            // Type info should already be set by infer_literal_types
            // Nothing extra needed here
            break;

        case AST_STRUCT_LITERAL:
            for (int i = 0; i < node->struct_literal.field_count; i++) {
                infer_with_specializations(node->struct_literal.field_values[i], symbols, ctx, diag);
            }
            break;

        case AST_ENUM_VARIANT: {
            // Type was already set in infer_literal_types, just ensure it's still there
            if (!node->type_info || node->type_info == Type_Unknown) {
                // Try to resolve again if it wasn't set
                if (node->enum_variant.enum_type) {
                    node->type_info = node->enum_variant.enum_type;
                }
            }
            break;
        }

        case AST_PATTERN_MATCH: {
            // Infer the expression being matched
            infer_with_specializations(node->pattern_match.expr, symbols, ctx, diag);
            // Type is always bool, already set in infer_literal_types
            break;
        }

        case AST_MEMBER_ACCESS: {
            infer_with_specializations(node->member_access.object, symbols, ctx, diag);

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
                trait_ensure_length_impl(target_type_info);

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

        case AST_NEW_EXPR: {
            // Infer type of size expression
            infer_with_specializations(node->new_expr.size_expr, symbols, ctx, diag);

            // Resolve element type if it's unknown (could be a struct type or primitive)
            if (node->new_expr.element_type->kind == TYPE_KIND_UNKNOWN) {
                const char* type_name = node->new_expr.element_type->type_name;
                TypeInfo* resolved = NULL;

                // Try primitives first
                if (strcmp(type_name, "bool") == 0) {
                    resolved = Type_Bool;
                } else if (strcmp(type_name, "string") == 0) {
                    resolved = Type_Str;
                } else if (strcmp(type_name, "double") == 0) {
                    resolved = Type_Double;
                } else {
                    // Try to find struct type in context
                    resolved = type_context_find_struct_type(ctx, type_name);
                }

                if (resolved) {
                    // Free the temporary unknown type and use the resolved one
                    free((char*)node->new_expr.element_type->type_name);
                    free(node->new_expr.element_type);
                    node->new_expr.element_type = resolved;
                } else {
                    TYPE_ERROR(diag, node->loc, TE_MISSING_RETURN, "Unknown type '%s' in new expression", type_name);
                    node->type_info = Type_Unknown;
                    break;
                }
            }

            // new T[size] returns ref T[] (ref to array of T)
            // Create array type T[]
            TypeInfo* array_type = type_info_create_array(node->new_expr.element_type);
            // Wrap in ref type
            node->type_info = type_context_get_or_create_ref_type(ctx, array_type, true);
            break;
        }

        case AST_DELETE_EXPR: {
            // Infer type of operand
            infer_with_specializations(node->delete_expr.operand, symbols, ctx, diag);

            // Validate that operand is a ref type
            TypeInfo* operand_type = node->delete_expr.operand->type_info;
            if (!type_info_is_ref(operand_type)) {
                TYPE_ERROR(diag, node->loc, TE_CANNOT_DELETE_NON_REF);
            }

            // delete returns void
            node->type_info = Type_Void;
            break;
        }

        default:
            break;
    }
}

// Helper: Count total number of specializations across all function types
static void iterative_specialization_discovery(ASTNode* ast, SymbolTable* symbols, TypeContext* ctx, DiagnosticContext* diag) {
	int iteration = 0;
  int max_iterations = 100; // Safety limit to prevent infinite loops

  while (iteration < max_iterations) {
    size_t spec_count_before = ctx->specialization_count;

    log_verbose_indent(2, "Iteration %d: %zu specializations before", iteration, spec_count_before);

    // Re-infer literal types to pick up any new type information (e.g., external function return types)
    infer_literal_types(ast, symbols, ctx, diag);

    // Pass 3: Analyze call sites to find needed specializations
    analyze_call_sites(ast, symbols, ctx, diag);
    log_verbose_indent(2, "After analyze_call_sites: %zu specializations", ctx->specialization_count);

    // Pass 4: Create specialized function versions
    create_specializations(ast, symbols, ctx, diag);
    log_verbose_indent(2, "After create_specializations: %zu specializations", ctx->specialization_count);

    // Pass 5: Propagate types with known specializations
    infer_with_specializations(ast, symbols, ctx, diag);

    // Pass 5.5: Process all specialization bodies (including fully-typed functions from Pass 1)
    // This ensures method calls inside method bodies get their resolved_spec set
    TypeEntry* type_entry = ctx->type_table;
    while (type_entry) {
        if (type_entry->type->kind == TYPE_KIND_FUNCTION) {
            FunctionSpecialization* spec = type_entry->type->data.function.specializations;
            while (spec) {
                if (spec->specialized_body && spec->specialized_body->symbol_table) {
                    infer_with_specializations(spec->specialized_body, spec->specialized_body->symbol_table, ctx, diag);
                }
                spec = spec->next;
            }
        }
        type_entry = type_entry->next;
    }

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

// Pass 0: Iteratively collect consts and structs (Rust-style with recursive evaluation)
// This handles dependencies between consts and struct field array sizes
static void collect_consts_and_structs(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!ast) return;
    if (ast->type != AST_PROGRAM && ast->type != AST_BLOCK) return;

    int max_iterations = 100;
    int iteration = 0;
    bool progress_made = true;

    // Track which declarations we've successfully processed
    bool* processed = (bool*)calloc(ast->program.count, sizeof(bool));

    while (progress_made && iteration < max_iterations) {
        progress_made = false;
        iteration++;

        for (int i = 0; i < ast->program.count; i++) {
            if (processed[i]) continue;  // Already processed

            ASTNode* stmt = ast->program.statements[i];

            // Try to process const declarations
            if (stmt->type == AST_VAR_DECL && stmt->var_decl.is_const) {
                // Try to evaluate array size expression if present
                if (stmt->var_decl.array_size_expr) {
                    EvalResult result = eval_const_expr_result(stmt->var_decl.array_size_expr, symbols);

                    if (result.status == EVAL_SUCCESS) {
                        stmt->var_decl.array_size = result.value;
                        // Continue to register the const
                    } else if (result.status == EVAL_WAITING) {
                        // Dependencies not ready, try again later
                        if (result.error_msg) free(result.error_msg);
                        continue;
                    } else {
                        // Real error - log it now
                        if (result.error_msg) {
                            TYPE_ERROR(diag, result.loc, TE_VARIANT_NOT_FOUND, "%s", result.error_msg);
                            free(result.error_msg);
                        }
                        processed[i] = true;  // Mark as done (with error)
                        continue;
                    }
                }

                // Register the const in symbol table (even if no array size)
                if (stmt->var_decl.init) {
                    infer_literal_types(stmt->var_decl.init, symbols, type_ctx, diag);
                    // Use type hint if provided, otherwise use inferred type from init
                    TypeInfo* var_type = stmt->var_decl.type_hint ? stmt->var_decl.type_hint : stmt->var_decl.init->type_info;
                    symbol_table_insert_var_declaration(symbols, stmt->var_decl.name,
                                                       var_type,
                                                       stmt->var_decl.is_const, stmt);
                } else if (stmt->var_decl.type_hint) {
                    symbol_table_insert_var_declaration(symbols, stmt->var_decl.name,
                                                       stmt->var_decl.type_hint,
                                                       stmt->var_decl.is_const, stmt);
                }

                log_verbose_indent(2, "Processed const: %s", stmt->var_decl.name);
                processed[i] = true;
                progress_made = true;
            }
            // Try to process struct declarations
            else if (stmt->type == AST_STRUCT_DECL) {
                // Try to evaluate all field array sizes
                bool all_fields_resolved = true;

                for (int j = 0; j < stmt->struct_decl.property_count; j++) {
                    if (stmt->struct_decl.property_array_size_exprs[j]) {
                        EvalResult result = eval_const_expr_result(stmt->struct_decl.property_array_size_exprs[j], symbols);

                        if (result.status == EVAL_SUCCESS) {
                            stmt->struct_decl.property_array_sizes[j] = result.value;
                        } else if (result.status == EVAL_WAITING) {
                            // Dependencies not ready
                            if (result.error_msg) free(result.error_msg);
                            all_fields_resolved = false;
                            break;  // Can't process this struct yet
                        } else {
                            // Real error
                            if (result.error_msg) {
                                TYPE_ERROR(diag, result.loc, TE_VARIANT_NOT_FOUND, "%s", result.error_msg);
                                free(result.error_msg);
                            }
                            // Mark field as error but continue
                            stmt->struct_decl.property_array_sizes[j] = 0;
                        }
                    }
                }

                if (all_fields_resolved) {
                    // All fields resolved, register the struct
                    collect_struct_declarations(stmt, symbols, type_ctx, diag);
                    log_verbose_indent(2, "Processed struct: %s", stmt->struct_decl.name);
                    processed[i] = true;
                    progress_made = true;
                }
                // If not all resolved, we'll try again next iteration
            }
            // Try to process enum declarations
            else if (stmt->type == AST_ENUM_DECL) {
                // Enums don't have array sizes to evaluate, just register them
                collect_enum_declarations(stmt, symbols, type_ctx, diag);
                log_verbose_indent(2, "Processed enum: %s", stmt->enum_decl.name);
                processed[i] = true;
                progress_made = true;
            }
        }

        if (progress_made) {
            log_verbose_indent(2, "Iteration %d: made progress", iteration);
        }
    }

    // Check for unprocessed declarations (these are errors)
    for (int i = 0; i < ast->program.count; i++) {
        if (!processed[i]) {
            ASTNode* stmt = ast->program.statements[i];
            if (stmt->type == AST_VAR_DECL && stmt->var_decl.is_const) {
                TYPE_ERROR(diag, stmt->loc, TE_ENUM_NOT_FOUND, "Could not resolve const declaration '%s' (circular dependency or undefined reference)",
                           stmt->var_decl.name);
            }
        }
    }

    free(processed);
    log_verbose_indent(2, "Completed after %d iteration(s)", iteration);
}

// Main entry point: Multi-pass type inference with specialization and diagnostics
void type_inference_with_diagnostics(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx, DiagnosticContext* diag) {
    if (!ast || !symbols || !type_ctx) return;

    log_verbose("Starting multi-pass type inference");

    // Pass 0: Iteratively collect consts and structs (handles dependencies)
    log_verbose_indent(1, "Pass 0: Collecting consts and struct declarations");
    collect_consts_and_structs(ast, symbols, type_ctx, diag);

    // Note: We continue even if pass 0 has errors to collect more diagnostics
    // Pass 1 can still find errors in function signatures independent of const/struct errors

    // Pass 1: Collect function signatures
    log_verbose_indent(1, "Pass 1: Collecting function signatures");
    collect_function_signatures(ast, symbols, type_ctx, diag);

    // Note: We continue even if pass 1 has errors to collect more diagnostics
    // Pass 2-4 can still find errors (undefined variables, type mismatches, etc.)

    // Pass 2-4: Iteratively analyze and specialize until no new specializations found
    // This handles: literal types, call site analysis, specialization creation, and final inference
    // Variable types depend on function return types, which depend on specializations,
    // which depend on call site argument types - so we iterate until convergence
    log_verbose_indent(1, "Pass 2-4: Iterative specialization discovery");
    iterative_specialization_discovery(ast, symbols, type_ctx, diag);

    // Check for errors after ALL passes - only stop before codegen
    if (diag && diagnostic_has_errors(diag)) {
        log_verbose("Type inference found errors, stopping before codegen");
        return;
    }

    // Store type context for codegen (contains both types and specializations)
    ast->type_ctx = type_ctx;

    // Store symbol table in AST for use in codegen
    ast->symbol_table = symbols;

    log_verbose("Type inference complete");
}

// Backward compatibility: version without diagnostics
void type_inference_with_context(ASTNode* ast, SymbolTable* symbols, TypeContext* type_ctx) {
    type_inference_with_diagnostics(ast, symbols, type_ctx, NULL);
}
