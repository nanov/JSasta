#include "jsasta_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Create a new TypeContext with pre-registered primitive types
TypeContext* type_context_create() {
    TypeContext* ctx = (TypeContext*)malloc(sizeof(TypeContext));
    ctx->type_table = NULL;
    ctx->type_count = 0;
    ctx->next_anonymous_id = 0;
    ctx->specializations = NULL;
    ctx->functions_processed = 0;

    // Pre-register primitive types and cache them
    Type_Unknown = type_info_create(TYPE_KIND_UNKNOWN, strdup("unknown"));
    type_context_register_type(ctx, Type_Unknown);

    Type_Int = type_info_create_primitive(strdup("int"));
    ctx->int_type = type_context_register_type(ctx, Type_Int);

    Type_Double = type_info_create_primitive(strdup("double"));
    ctx->double_type = type_context_register_type(ctx, Type_Double);

    Type_String = type_info_create_primitive(strdup("string"));
    ctx->string_type = type_context_register_type(ctx, Type_String);

    Type_Bool = type_info_create_primitive(strdup("bool"));
    ctx->bool_type = type_context_register_type(ctx, Type_Bool);

    Type_Void = type_info_create_primitive(strdup("void"));
    ctx->void_type = type_context_register_type(ctx, Type_Void);

    // Create array types
    Type_Array_Int = type_info_create(TYPE_KIND_ARRAY, strdup("int[]"));
    Type_Array_Int->data.array.element_type = Type_Int;
    type_context_register_type(ctx, Type_Array_Int);

    Type_Array_Double = type_info_create(TYPE_KIND_ARRAY, strdup("double[]"));
    Type_Array_Double->data.array.element_type = Type_Double;
    type_context_register_type(ctx, Type_Array_Double);

    Type_Array_Bool = type_info_create(TYPE_KIND_ARRAY, strdup("bool[]"));
    Type_Array_Bool->data.array.element_type = Type_Bool;
    type_context_register_type(ctx, Type_Array_Bool);

    Type_Array_String = type_info_create(TYPE_KIND_ARRAY, strdup("string[]"));
    Type_Array_String->data.array.element_type = Type_String;
    type_context_register_type(ctx, Type_Array_String);

    // Create object type placeholder
    Type_Object = type_info_create(TYPE_KIND_OBJECT, strdup("object"));
    type_context_register_type(ctx, Type_Object);

    return ctx;
}

// Free TypeContext and all registered types
void type_context_free(TypeContext* ctx) {
    if (!ctx) return;

    // Free all types in the type table (linked list)
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        TypeEntry* next = entry->next;
        type_info_free(entry->type);
        free(entry);
        entry = next;
    }

    // Free specializations
    FunctionSpecialization* spec = ctx->specializations;
    while (spec) {
        FunctionSpecialization* next = spec->next;
        free(spec->function_name);
        free(spec->specialized_name);
        if (spec->param_type_info) {
            for (int i = 0; i < spec->param_count; i++) {
                if (spec->param_type_info[i]) {
                    type_info_free(spec->param_type_info[i]);
                }
            }
            free(spec->param_type_info);
        }
        if (spec->specialized_body) {
            ast_free(spec->specialized_body);
        }
        free(spec);
        spec = next;
    }

    free(ctx);
}

// Register a type in the type table (linked list)
TypeInfo* type_context_register_type(TypeContext* ctx, TypeInfo* type) {
    if (!ctx || !type) return NULL;

    // Create new entry
    TypeEntry* entry = (TypeEntry*)malloc(sizeof(TypeEntry));
    entry->type = type;
    entry->next = NULL;

    // Assign type ID
    type->type_id = ctx->type_count;

    // Add to head of linked list
    if (ctx->type_table == NULL) {
        ctx->type_table = entry;
    } else {
        entry->next = ctx->type_table;
        ctx->type_table = entry;
    }

    ctx->type_count++;

    return type;
}

// Find a type by name
TypeInfo* type_context_find_type(TypeContext* ctx, const char* type_name) {
    if (!ctx || !type_name) return NULL;

    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->type_name && strcmp(entry->type->type_name, type_name) == 0) {
            return entry->type;
        }
        entry = entry->next;
    }

    return NULL;
}

// Compare two TypeInfo structures for equality
bool type_info_equals(TypeInfo* a, TypeInfo* b) {
    if (!a || !b) return a == b;

    if (a==b)
    	return true;

    // Different kinds or base types
    if (a->kind != b->kind) {
        return false;
    }

    // Objects: check property count and all properties
    if (a->kind == TYPE_KIND_OBJECT) {
        if (a->data.object.property_count != b->data.object.property_count) {
            return false;
        }

        for (int i = 0; i < a->data.object.property_count; i++) {
            // Check property name
            if (strcmp(a->data.object.property_names[i], b->data.object.property_names[i]) != 0) {
                return false;
            }

            // Check property type (recursive)
            // Both must have property types for proper structural comparison
            if (!a->data.object.property_types || !b->data.object.property_types) {
                return false;  // Can't determine equality without property types
            }
            
            if (!type_info_equals(a->data.object.property_types[i], b->data.object.property_types[i])) {
                return false;
            }
        }

        return true;
    }

    // Arrays: check element type
    if (a->kind == TYPE_KIND_ARRAY) {
        return type_info_equals(a->data.array.element_type, b->data.array.element_type);
    }

    return false;
}

// Create TypeInfo from object literal AST node with structural type sharing
// This is the main entry point for creating object types - it handles:
// 1. Delegating to type_info.c to create TypeInfo from AST
// 2. Searching for existing structurally equivalent type
// 3. Registering new type if no match found
// TypeContext owns all TypeInfo memory - callers just get references
TypeInfo* type_context_create_object_type_from_literal(TypeContext* ctx, ASTNode* obj_literal) {
    if (!ctx || !obj_literal || obj_literal->type != AST_OBJECT_LITERAL) {
        return NULL;
    }

    // Delegate to type_info.c to create TypeInfo from AST (contains the logic)
    TypeInfo* info = type_info_create_from_object_literal(obj_literal);
    if (!info) {
        return NULL;
    }

    // Search for existing structurally equivalent type
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_OBJECT) {
            if (type_info_equals(entry->type, info)) {
                // Found match - free temp TypeInfo (shallow, property types are references)
                type_info_free_shallow(info);
                return entry->type;
            }
        }
        entry = entry->next;
    }

    // No match - generate name and register this new type
    char name[64];
    snprintf(name, sizeof(name), "Object_%d", ctx->next_anonymous_id++);
    info->type_name = strdup(name);

    return type_context_register_type(ctx, info);
}

// Find an existing object type or create and register a new one (type interning)
TypeInfo* type_context_find_or_create_object_type(TypeContext* ctx, TypeInfo* obj_type) {
    if (!ctx || !obj_type || obj_type->kind != TYPE_KIND_OBJECT) {
        return NULL;
    }

    // Must have property types for structural comparison
    if (!obj_type->data.object.property_types) {
        return NULL;
    }

    // Search for existing equivalent type (same property names AND types)
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_OBJECT) {
            if (type_info_equals(entry->type, obj_type)) {
                // Found existing type - free temp TypeInfo (shallow, types are references)
                type_info_free_shallow(obj_type);
                return entry->type;
            }
        }
        entry = entry->next;
    }

    // No existing type found - generate name and register this one
    if (!obj_type->type_name) {
        char name[64];
        snprintf(name, sizeof(name), "Object_%d", ctx->next_anonymous_id++);
        obj_type->type_name = strdup(name);
    }

    return type_context_register_type(ctx, obj_type);
}

// Primitive type accessors (return cached types for O(1) access)
TypeInfo* type_context_get_int(TypeContext* ctx) {
    return ctx ? ctx->int_type : NULL;
}

TypeInfo* type_context_get_double(TypeContext* ctx) {
    return ctx ? ctx->double_type : NULL;
}

TypeInfo* type_context_get_string(TypeContext* ctx) {
    return ctx ? ctx->string_type : NULL;
}

TypeInfo* type_context_get_bool(TypeContext* ctx) {
    return ctx ? ctx->bool_type : NULL;
}

TypeInfo* type_context_get_void(TypeContext* ctx) {
    return ctx ? ctx->void_type : NULL;
}
