#include "jsasta_compiler.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Create a new specialization context
SpecializationContext* specialization_context_create() {
    SpecializationContext* ctx = (SpecializationContext*)calloc(1, sizeof(SpecializationContext));
    ctx->specializations = NULL;
    ctx->functions_processed = 0;
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
        
        // Free TypeInfo for object parameters
        if (current->param_type_info) {
            for (int i = 0; i < current->param_count; i++) {
                if (current->param_type_info[i]) {
                    type_info_free(current->param_type_info[i]);
                }
            }
            free(current->param_type_info);
        }
        
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
FunctionSpecialization* specialization_context_add(SpecializationContext* ctx, const char* func_name,
                                ValueType* param_types, int param_count) {
    if (!ctx) return NULL;

    // Check if this specialization already exists
    FunctionSpecialization* existing = ctx->specializations;
    while (existing) {
        if (strcmp(existing->function_name, func_name) == 0 &&
            existing->param_count == param_count &&
            types_match(existing->param_types, param_types, param_count)) {
            // Already exists
            return NULL;
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
    
    // Initialize TypeInfo array for object parameters (will be populated at call sites)
    spec->param_type_info = (TypeInfo**)calloc(param_count, sizeof(TypeInfo*));
    
    spec->return_type = TYPE_UNKNOWN; // Will be inferred later
    spec->specialized_body = NULL;    // Will be set during specialization pass

    // Add to linked list
    spec->next = ctx->specializations;
    ctx->specializations = spec;
    ctx->functions_processed++;

    log_verbose_indent(2, "Specialization: %s -> %s", func_name, spec->specialized_name);
    return spec;
}

// NEW: Create specialized AST for a function with specific parameter types
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

// Helper to get type name as string
static const char* type_to_string(ValueType type) {
    switch (type) {
        case TYPE_INT: return "int";
        case TYPE_DOUBLE: return "double";
        case TYPE_STRING: return "string";
        case TYPE_BOOL: return "bool";
        case TYPE_VOID: return "void";
        default: return "unknown";
    }
}

// Print all specializations (for debugging)
void specialization_context_print(SpecializationContext* ctx) {
    if (!ctx) return;

    log_verbose("Function Specializations:");
    FunctionSpecialization* current = ctx->specializations;

    while (current) {
        // Build parameter type string
        char param_str[256] = "";
        int offset = 0;
        for (int i = 0; i < current->param_count; i++) {
            if (i > 0) offset += snprintf(param_str + offset, 256 - offset, ", ");
            offset += snprintf(param_str + offset, 256 - offset, "%s",
                             type_to_string(current->param_types[i]));
        }

        const char* body_status = current->specialized_body ? " ✓" : "";
        log_verbose_indent(1, "%s(%s) -> %s [%s]%s",
                          current->function_name,
                          param_str,
                          type_to_string(current->return_type),
                          current->specialized_name,
                          body_status);

        current = current->next;
    }
}
