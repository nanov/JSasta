#include "jsasta_compiler.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Create specialized function name from TypeInfo array
// Add or find a specialization using TypeInfo (wrapper for new API)
FunctionSpecialization* specialization_context_add_by_type_info(TypeContext* ctx, const char* func_name,
                                TypeInfo** param_type_info, int param_count) {
    if (!ctx || !func_name) return NULL;

    // Find or create the function type
    TypeInfo* func_type = type_context_find_function_type(ctx, func_name);
    if (!func_type) {
        // Function type doesn't exist yet - will be created during function signature collection
        log_warning("Function type '%s' not found, cannot add specialization yet", func_name);
        return NULL;
    }

    // Add specialization to the function type
    FunctionSpecialization* spec = type_context_add_specialization(ctx, func_type, param_type_info, param_count);

    if (spec) {
        log_verbose_indent(2, "Specialization: %s -> %s", func_name, spec->specialized_name);
    }

    return spec;
}

// Find specialization by TypeInfo array (wrapper for new API)
FunctionSpecialization* specialization_context_find_by_type_info(TypeContext* ctx,
                                                                  const char* func_name,
                                                                  TypeInfo** param_type_info,
                                                                  int param_count) {
    if (!ctx || !func_name) return NULL;

    // Find the function type
    TypeInfo* func_type = type_context_find_function_type(ctx, func_name);
    if (!func_type) {
        return NULL;
    }

    // Find specialization in the function type
    return type_context_find_specialization(ctx, func_type, param_type_info, param_count);
}

// Print all specializations (for debugging)
void specialization_context_print(TypeContext* ctx) {
    if (!ctx) return;

    log_verbose("Function Specializations:");

    // Iterate through all types in TypeContext
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_FUNCTION) {
            TypeInfo* func_type = entry->type;
            FunctionSpecialization* spec = func_type->data.function.specializations;

            // Print each specialization for this function
            while (spec) {
                // Build parameter type string
                char param_str[256] = "";
                int offset = 0;
                for (int i = 0; i < spec->param_count; i++) {
                    if (i > 0) offset += snprintf(param_str + offset, 256 - offset, ", ");
                    offset += snprintf(param_str + offset, 256 - offset, "%s", spec->param_type_info[i]->type_name);
                }

                const char* body_status = spec->specialized_body ? " ✓" : "";
                log_verbose_indent(1, "%s(%s) -> %s [%s]%s",
                                  func_type->type_name,
                                  param_str,spec->return_type_info->type_name,
                                  spec->specialized_name,
                                  body_status);

                spec = spec->next;
            }
        }
        entry = entry->next;
    }
}
