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
        
        // Free TypeInfo array (note: TypeInfo objects themselves are owned by TypeContext)
        if (current->param_type_info) {
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

// Check if two TypeInfo signatures match
static bool type_infos_match(TypeInfo** types1, TypeInfo** types2, int count) {
    for (int i = 0; i < count; i++) {
        // Direct pointer comparison (types are interned in TypeContext)
        if (types1[i] != types2[i]) {
            return false;
        }
    }
    return true;
}

// Helper to get type name for specialization naming
static const char* get_type_suffix(TypeInfo* type_info) {
    if (!type_info) return "unknown";
    
    if (type_info == Type_Int) return "int";
    if (type_info == Type_Double) return "double";
    if (type_info == Type_String) return "str";
    if (type_info == Type_Bool) return "bool";
    if (type_info == Type_Void) return "void";
    
    // For arrays
    if (type_info_is_array(type_info)) {
        if (type_info->data.array.element_type == Type_Int) return "arrint";
        if (type_info->data.array.element_type == Type_Double) return "arrdouble";
        if (type_info->data.array.element_type == Type_String) return "arrstr";
        if (type_info->data.array.element_type == Type_Bool) return "arrbool";
        return "arr";
    }
    
    // For objects, use type name if available
    if (type_info_is_object(type_info)) {
        return type_info->type_name ? type_info->type_name : "obj";
    }
    
    return "unknown";
}

// Create specialized function name from TypeInfo array
static char* create_specialized_name_from_type_info(const char* func_name, TypeInfo** param_type_info, int param_count) {
    char* name = (char*)malloc(256);
    int offset = snprintf(name, 256, "%s_", func_name);

    for (int i = 0; i < param_count; i++) {
        const char* suffix = get_type_suffix(param_type_info[i]);

        if (i > 0) {
            offset += snprintf(name + offset, 256 - offset, "_");
        }
        offset += snprintf(name + offset, 256 - offset, "%s", suffix);
    }

    return name;
}

// Add or find a specialization using TypeInfo
FunctionSpecialization* specialization_context_add_by_type_info(SpecializationContext* ctx, const char* func_name,
                                TypeInfo** param_type_info, int param_count) {
    if (!ctx) return NULL;

    // Check if this specialization already exists
    FunctionSpecialization* existing = ctx->specializations;
    while (existing) {
        if (strcmp(existing->function_name, func_name) == 0 &&
            existing->param_count == param_count &&
            type_infos_match(existing->param_type_info, param_type_info, param_count)) {
            // Already exists
            return NULL;
        }
        existing = existing->next;
    }

    // Create new specialization
    FunctionSpecialization* spec = (FunctionSpecialization*)calloc(1, sizeof(FunctionSpecialization));

    spec->function_name = strdup(func_name);
    spec->specialized_name = create_specialized_name_from_type_info(func_name, param_type_info, param_count);
    spec->param_count = param_count;
    
    // Initialize TypeInfo array (store references - TypeContext manages lifetime)
    spec->param_type_info = (TypeInfo**)calloc(param_count, sizeof(TypeInfo*));
    for (int i = 0; i < param_count; i++) {
        spec->param_type_info[i] = param_type_info[i];
    }
    
    spec->return_type_info = NULL;    // Will be inferred later
    spec->specialized_body = NULL;    // Will be set during specialization pass

    // Add to linked list
    spec->next = ctx->specializations;
    ctx->specializations = spec;
    ctx->functions_processed++;

    log_verbose_indent(2, "Specialization: %s -> %s", func_name, spec->specialized_name);
    return spec;
}

// Find specialization by TypeInfo array
FunctionSpecialization* specialization_context_find_by_type_info(SpecializationContext* ctx,
                                                                  const char* func_name,
                                                                  TypeInfo** param_type_info,
                                                                  int param_count) {
    if (!ctx) return NULL;

    FunctionSpecialization* current = ctx->specializations;
    while (current) {
        if (strcmp(current->function_name, func_name) == 0 &&
            current->param_count == param_count &&
            type_infos_match(current->param_type_info, param_type_info, param_count)) {
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

// Helper to get type name as string for display
static const char* type_info_to_string(TypeInfo* type_info) {
    if (!type_info) return "unknown";
    
    if (type_info == Type_Int) return "int";
    if (type_info == Type_Double) return "double";
    if (type_info == Type_String) return "string";
    if (type_info == Type_Bool) return "bool";
    if (type_info == Type_Void) return "void";
    
    if (type_info_is_array(type_info)) {
        return "array";
    }
    
    if (type_info_is_object(type_info)) {
        return type_info->type_name ? type_info->type_name : "object";
    }
    
    return "unknown";
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
                             type_info_to_string(current->param_type_info[i]));
        }

        const char* body_status = current->specialized_body ? " ✓" : "";
        log_verbose_indent(1, "%s(%s) -> %s [%s]%s",
                          current->function_name,
                          param_str,
                          type_info_to_string(current->return_type_info),
                          current->specialized_name,
                          body_status);

        current = current->next;
    }
}
