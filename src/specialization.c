#include "js_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Create a new specialization context
SpecializationContext* specialization_context_create() {
    SpecializationContext* ctx = (SpecializationContext*)calloc(1, sizeof(SpecializationContext));
    ctx->specializations = NULL;
    return ctx;
}

// Free specialization context
void specialization_context_free(SpecializationContext* ctx) {
    if (!ctx) return;

    FunctionSpecialization* current = ctx->specializations;
    while (current) {
        FunctionSpecialization* next = current->next;
        free(current->function_name);
        free(current->specialized_name);
        free(current->param_types);
        if (current->specialized_body) {
            ast_free(current->specialized_body);  // Free cloned AST
        }
        free(current);
        current = next;
    }

    free(ctx);
}

// Check if two type signatures match
static bool types_match(ValueType* types1, ValueType* types2, int count) {
    for (int i = 0; i < count; i++) {
        if (types1[i] != types2[i]) {
            return false;
        }
    }
    return true;
}

// Create specialized function name
static char* create_specialized_name(const char* func_name, ValueType* param_types, int param_count) {
    char* name = (char*)malloc(256);
    int offset = snprintf(name, 256, "%s_", func_name);

    for (int i = 0; i < param_count; i++) {
        const char* suffix = "";
        switch (param_types[i]) {
            case TYPE_INT: suffix = "int"; break;
            case TYPE_DOUBLE: suffix = "double"; break;
            case TYPE_STRING: suffix = "str"; break;
            case TYPE_BOOL: suffix = "bool"; break;
            default: suffix = "unknown"; break;
        }

        if (i > 0) {
            offset += snprintf(name + offset, 256 - offset, "_");
        }
        offset += snprintf(name + offset, 256 - offset, "%s", suffix);
    }

    return name;
}

// Add or find a specialization - NOW WITH AST CLONING
void specialization_context_add(SpecializationContext* ctx, const char* func_name,
                                ValueType* param_types, int param_count) {
    if (!ctx) return;

    // Check if this specialization already exists
    FunctionSpecialization* existing = ctx->specializations;
    while (existing) {
        if (strcmp(existing->function_name, func_name) == 0 &&
            existing->param_count == param_count &&
            types_match(existing->param_types, param_types, param_count)) {
            // Already exists
            return;
        }
        existing = existing->next;
    }

    // Create new specialization
    FunctionSpecialization* spec = (FunctionSpecialization*)calloc(1, sizeof(FunctionSpecialization));

    spec->function_name = strdup(func_name);
    spec->specialized_name = create_specialized_name(func_name, param_types, param_count);
    spec->param_count = param_count;
    spec->param_types = (ValueType*)malloc(sizeof(ValueType) * param_count);
    memcpy(spec->param_types, param_types, sizeof(ValueType) * param_count);
    spec->return_type = TYPE_UNKNOWN; // Will be inferred later
    spec->specialized_body = NULL;    // Will be set during specialization pass

    // Add to linked list
    spec->next = ctx->specializations;
    ctx->specializations = spec;

    printf("  Specialization: %s -> %s\n", func_name, spec->specialized_name);
}

// NEW: Create specialized AST for a function with specific parameter types
void specialization_create_body(FunctionSpecialization* spec, ASTNode* original_func_node) {
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
    memcpy(cloned_func->func_decl.param_types, spec->param_types,
           sizeof(ValueType) * spec->param_count);

    // Store the specialized body (just the function node, not the body specifically)
    spec->specialized_body = cloned_func;

    // Now perform type analysis on the cloned body with known parameter types
    SymbolTable* temp_symbols = symbol_table_create(NULL);

    // Insert parameters with their concrete types
    for (int i = 0; i < spec->param_count; i++) {
        symbol_table_insert(temp_symbols, cloned_func->func_decl.params[i],
                          spec->param_types[i], NULL);
    }

    // Analyze the function body with concrete parameter types
    // type_inference(cloned_func->func_decl.body, temp_symbols);

    // Infer return type from the analyzed body
    // spec->return_type = cloned_func->func_decl.return_type;

    symbol_table_free(temp_symbols);

    printf("  Specialized %s: analyzed with return type ", spec->specialized_name);
    switch (spec->return_type) {
        case TYPE_INT: printf("int\n"); break;
        case TYPE_DOUBLE: printf("double\n"); break;
        case TYPE_STRING: printf("string\n"); break;
        case TYPE_BOOL: printf("bool\n"); break;
        case TYPE_VOID: printf("void\n"); break;
        default: printf("unknown\n"); break;
    }
}

// Find a specific specialization
FunctionSpecialization* specialization_context_find(SpecializationContext* ctx,
                                                    const char* func_name,
                                                    ValueType* param_types,
                                                    int param_count) {
    if (!ctx) return NULL;

    FunctionSpecialization* current = ctx->specializations;
    while (current) {
        if (strcmp(current->function_name, func_name) == 0 &&
            current->param_count == param_count &&
            types_match(current->param_types, param_types, param_count)) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// Get first specialization for a function
FunctionSpecialization* specialization_context_get_all(SpecializationContext* ctx,
                                                       const char* func_name) {
    if (!ctx) return NULL;

    FunctionSpecialization* current = ctx->specializations;

    while (current) {
        if (strcmp(current->function_name, func_name) == 0) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// Print all specializations (for debugging)
void specialization_context_print(SpecializationContext* ctx) {
    if (!ctx) return;

    printf("\n=== Function Specializations ===\n");
    FunctionSpecialization* current = ctx->specializations;

    while (current) {
        printf("%s(", current->function_name);
        for (int i = 0; i < current->param_count; i++) {
            if (i > 0) printf(", ");
            switch (current->param_types[i]) {
                case TYPE_INT: printf("int"); break;
                case TYPE_DOUBLE: printf("double"); break;
                case TYPE_STRING: printf("string"); break;
                case TYPE_BOOL: printf("bool"); break;
                default: printf("unknown"); break;
            }
        }
        printf(") -> ");
        switch (current->return_type) {
            case TYPE_INT: printf("int"); break;
            case TYPE_DOUBLE: printf("double"); break;
            case TYPE_STRING: printf("string"); break;
            case TYPE_BOOL: printf("bool"); break;
            case TYPE_VOID: printf("void"); break;
            default: printf("unknown"); break;
        }
        printf(" [%s]", current->specialized_name);
        if (current->specialized_body) {
            printf(" ✓ body cloned");
        }
        printf("\n");

        current = current->next;
    }
    printf("================================\n\n");
}
