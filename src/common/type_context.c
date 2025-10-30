#include "jsasta_compiler.h"
#include "traits.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Create a new TypeContext with pre-registered primitive types
TypeContext* type_context_create() {
    TypeContext* ctx = (TypeContext*)malloc(sizeof(TypeContext));
    ctx->type_table = NULL;
    ctx->type_count = 0;
    ctx->next_anonymous_id = 0;
    ctx->specialization_count = 0;
    ctx->trait_registry = NULL;

    // Pre-register primitive types and cache them
    Type_Unknown = type_info_create(TYPE_KIND_UNKNOWN, strdup("unknown"));
    type_context_register_type(ctx, Type_Unknown);

    // Signed integer types
    Type_I8 = type_info_create_integer(strdup("i8"), 8, true);
    ctx->i8_type = type_context_register_type(ctx, Type_I8);

    Type_I16 = type_info_create_integer(strdup("i16"), 16, true);
    ctx->i16_type = type_context_register_type(ctx, Type_I16);

    Type_I32 = type_info_create_integer(strdup("i32"), 32, true);
    ctx->i32_type = type_context_register_type(ctx, Type_I32);

    Type_I64 = type_info_create_integer(strdup("i64"), 64, true);
    ctx->i64_type = type_context_register_type(ctx, Type_I64);

    // Unsigned integer types
    Type_U8 = type_info_create_integer(strdup("u8"), 8, false);
    ctx->u8_type = type_context_register_type(ctx, Type_U8);

    Type_U16 = type_info_create_integer(strdup("u16"), 16, false);
    ctx->u16_type = type_context_register_type(ctx, Type_U16);

    Type_U32 = type_info_create_integer(strdup("u32"), 32, false);
    ctx->u32_type = type_context_register_type(ctx, Type_U32);

    Type_U64 = type_info_create_integer(strdup("u64"), 64, false);
    ctx->u64_type = type_context_register_type(ctx, Type_U64);

    // Legacy "int" type (alias for i32)
    Type_Int = Type_I32;
    ctx->int_type = ctx->i32_type;

    Type_Double = type_info_create_primitive(strdup("double"));
    ctx->double_type = type_context_register_type(ctx, Type_Double);

    Type_String = type_info_create_primitive(strdup("string"));
    ctx->string_type = type_context_register_type(ctx, Type_String);

    Type_Bool = type_info_create_primitive(strdup("bool"));
    ctx->bool_type = type_context_register_type(ctx, Type_Bool);

    Type_Void = type_info_create_primitive(strdup("void"));
    ctx->void_type = type_context_register_type(ctx, Type_Void);

    // Create array types for all integer types
    Type_Array_I8 = type_info_create(TYPE_KIND_ARRAY, strdup("i8[]"));
    Type_Array_I8->data.array.element_type = Type_I8;
    type_context_register_type(ctx, Type_Array_I8);

    Type_Array_I16 = type_info_create(TYPE_KIND_ARRAY, strdup("i16[]"));
    Type_Array_I16->data.array.element_type = Type_I16;
    type_context_register_type(ctx, Type_Array_I16);

    Type_Array_I32 = type_info_create(TYPE_KIND_ARRAY, strdup("i32[]"));
    Type_Array_I32->data.array.element_type = Type_I32;
    type_context_register_type(ctx, Type_Array_I32);

    Type_Array_I64 = type_info_create(TYPE_KIND_ARRAY, strdup("i64[]"));
    Type_Array_I64->data.array.element_type = Type_I64;
    type_context_register_type(ctx, Type_Array_I64);

    Type_Array_U8 = type_info_create(TYPE_KIND_ARRAY, strdup("u8[]"));
    Type_Array_U8->data.array.element_type = Type_U8;
    type_context_register_type(ctx, Type_Array_U8);

    Type_Array_U16 = type_info_create(TYPE_KIND_ARRAY, strdup("u16[]"));
    Type_Array_U16->data.array.element_type = Type_U16;
    type_context_register_type(ctx, Type_Array_U16);

    Type_Array_U32 = type_info_create(TYPE_KIND_ARRAY, strdup("u32[]"));
    Type_Array_U32->data.array.element_type = Type_U32;
    type_context_register_type(ctx, Type_Array_U32);

    Type_Array_U64 = type_info_create(TYPE_KIND_ARRAY, strdup("u64[]"));
    Type_Array_U64->data.array.element_type = Type_U64;
    type_context_register_type(ctx, Type_Array_U64);

    // Legacy array type (alias for i32[])
    Type_Array_Int = Type_Array_I32;

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

    // Initialize trait registry with built-in traits
    ctx->trait_registry = trait_registry_create();
    traits_init_builtins(ctx->trait_registry, ctx);
    traits_register_builtin_impls(ctx->trait_registry, ctx);

    // Create platform-specific type aliases
    // Determine platform pointer size to decide which types to use
    #if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
        // 64-bit platform
        Type_Usize = type_info_create_alias(strdup("usize"), Type_U64);
        Type_Nint = type_info_create_alias(strdup("nint"), Type_I64);
        Type_Uint = type_info_create_alias(strdup("uint"), Type_U64);
    #else
        // 32-bit platform
        Type_Usize = type_info_create_alias(strdup("usize"), Type_U32);
        Type_Nint = type_info_create_alias(strdup("nint"), Type_I32);
        Type_Uint = type_info_create_alias(strdup("uint"), Type_U32);
    #endif
    
    type_context_register_type(ctx, Type_Usize);
    type_context_register_type(ctx, Type_Nint);
    type_context_register_type(ctx, Type_Uint);

    return ctx;
}

// Free TypeContext and all registered types
void type_context_free(TypeContext* ctx) {
    if (!ctx) return;

    // Free trait registry
    if (ctx->trait_registry) {
        trait_registry_destroy(ctx->trait_registry);
        ctx->trait_registry = NULL;
    }

    // Free all types in the type table (linked list)
    // Note: type_info_free will handle freeing specializations for function types
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        TypeEntry* next = entry->next;
        type_info_free(entry->type);
        free(entry);
        entry = next;
    }

    free(ctx);
}

// Register a type in the type table (linked list)
TypeInfo* type_context_register_type(TypeContext* ctx, TypeInfo* type) {
    if (!ctx || !type) return NULL;

    // Create new entry
    TypeEntry* entry = (TypeEntry*)malloc(sizeof(TypeEntry));
    entry->type = type;
    entry->llvm_type = NULL;  // Initialize to NULL
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

// Primitive type accessors (return actual types with aliases resolved)
TypeInfo* type_context_get_int(TypeContext* ctx) {
    return type_info_resolve_alias(ctx ? ctx->int_type : NULL);
}

TypeInfo* type_context_get_double(TypeContext* ctx) {
    return type_info_resolve_alias(ctx ? ctx->double_type : NULL);
}

TypeInfo* type_context_get_string(TypeContext* ctx) {
    return type_info_resolve_alias(ctx ? ctx->string_type : NULL);
}

TypeInfo* type_context_get_bool(TypeContext* ctx) {
    return type_info_resolve_alias(ctx ? ctx->bool_type : NULL);
}

TypeInfo* type_context_get_void(TypeContext* ctx) {
    return type_info_resolve_alias(ctx ? ctx->void_type : NULL);
}

// Create or find a function type
TypeInfo* type_context_create_function_type(TypeContext* ctx, const char* func_name,
                                            TypeInfo** param_types, int param_count,
                                            TypeInfo* return_type, ASTNode* original_body,
                                            bool is_variadic) {
    if (!ctx || !func_name) return NULL;

    // Check if function type already exists
    TypeInfo* existing = type_context_find_function_type(ctx, func_name);
    if (existing) {
        return existing;
    }

    // Create new function type
    TypeInfo* func_type = type_info_create(TYPE_KIND_FUNCTION, strdup(func_name));
    func_type->data.function.param_types = param_types;
    func_type->data.function.param_count = param_count;
    func_type->data.function.return_type = return_type;
    func_type->data.function.is_variadic = is_variadic;
    func_type->data.function.specializations = NULL;
    func_type->data.function.original_body = original_body;  // Store reference, don't own

    // Compute is_fully_typed flag (cached check)
    func_type->data.function.is_fully_typed = false;
    if (return_type && !type_info_is_unknown(return_type)) {
        bool all_params_typed = true;
        for (int i = 0; i < param_count; i++) {
            if (!param_types || !param_types[i] || type_info_is_unknown(param_types[i])) {
                all_params_typed = false;
                break;
            }
        }
        func_type->data.function.is_fully_typed = all_params_typed;
    }

    return type_context_register_type(ctx, func_type);
}

// Find a function type by name
TypeInfo* type_context_find_function_type(TypeContext* ctx, const char* func_name) {
    if (!ctx || !func_name) return NULL;

    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_FUNCTION &&
            entry->type->type_name &&
            strcmp(entry->type->type_name, func_name) == 0) {
            return entry->type;
        }
        entry = entry->next;
    }

    return NULL;
}

// Create and register a struct type from struct declaration
TypeInfo* type_context_create_struct_type(TypeContext* ctx, const char* struct_name,
                                          char** property_names, TypeInfo** property_types,
                                          int property_count, ASTNode* struct_decl_node) {
    if (!ctx || !struct_name) return NULL;

    // Check if struct type already exists
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_OBJECT &&
            entry->type->type_name &&
            strcmp(entry->type->type_name, struct_name) == 0) {
            log_error("Struct '%s' is already defined", struct_name);
            return entry->type; // Return existing
        }
        entry = entry->next;
    }

    // Create new struct type (as an object type with a specific name)
    TypeInfo* struct_type = type_info_create(TYPE_KIND_OBJECT, strdup(struct_name));
    struct_type->data.object.property_names = property_names;
    struct_type->data.object.property_types = property_types;
    struct_type->data.object.property_count = property_count;
    struct_type->data.object.struct_decl_node = struct_decl_node;  // Store reference for default values

    return type_context_register_type(ctx, struct_type);
}

// Find a struct type by name
TypeInfo* type_context_find_struct_type(TypeContext* ctx, const char* struct_name) {
    if (!ctx || !struct_name) return NULL;

    TypeEntry* entry = ctx->type_table;
    while (entry) {
        // Structs are registered as TYPE_KIND_OBJECT with explicit names
        // Anonymous objects have generated names like "Object_N"
        if (entry->type->kind == TYPE_KIND_OBJECT &&
            entry->type->type_name &&
            strcmp(entry->type->type_name, struct_name) == 0 &&
            strncmp(struct_name, "Object_", 7) != 0) { // Not an anonymous object
            return entry->type;
        }
        entry = entry->next;
    }

    return NULL;
}

// Helper: Check if two TypeInfo arrays match
static bool type_arrays_match(TypeInfo** types1, TypeInfo** types2, int count) {
    for (int i = 0; i < count; i++) {
        if (types1[i] != types2[i]) {
            return false;
        }
    }
    return true;
}

// Add a specialization to a function type
FunctionSpecialization* type_context_add_specialization(TypeContext* ctx, TypeInfo* func_type,
                                                        TypeInfo** param_type_info, int param_count) {
    if (!ctx || !func_type || func_type->kind != TYPE_KIND_FUNCTION) return NULL;

    // Check if specialization already exists
    FunctionSpecialization* existing = func_type->data.function.specializations;
    while (existing) {
        if (existing->param_count == param_count &&
            type_arrays_match(existing->param_type_info, param_type_info, param_count)) {
            return NULL;  // Already exists
        }
        existing = existing->next;
    }

    // Create new specialization
    FunctionSpecialization* spec = (FunctionSpecialization*)calloc(1, sizeof(FunctionSpecialization));
    
    // Generate specialized name using type names
    // Sanitize type names to be valid LLVM identifiers (replace < > with _ )
    char name[256];
    int offset = snprintf(name, 256, "%s", func_type->type_name);
    for (int i = 0; i < param_count; i++) {
        const char* type_name = param_type_info[i] && param_type_info[i]->type_name 
            ? param_type_info[i]->type_name 
            : "unknown";
        
        // Sanitize type name: replace < and > with underscores for valid LLVM identifiers
        char sanitized[128];
        int j = 0;
        for (int k = 0; type_name[k] && j < 127; k++) {
            if (type_name[k] == '<' || type_name[k] == '>') {
                sanitized[j++] = '_';
            } else {
                sanitized[j++] = type_name[k];
            }
        }
        sanitized[j] = '\0';
        
        offset += snprintf(name + offset, 256 - offset, "_%s", sanitized);
    }
    spec->specialized_name = strdup(name);
    
    spec->param_count = param_count;
    spec->param_type_info = (TypeInfo**)calloc(param_count, sizeof(TypeInfo*));
    for (int i = 0; i < param_count; i++) {
        spec->param_type_info[i] = param_type_info[i];  // Reference, not owned
    }
    
    spec->return_type_info = NULL;  // Will be inferred
    spec->specialized_body = NULL;  // Will be set during specialization pass
    
    // Add to head of list
    spec->next = func_type->data.function.specializations;
    func_type->data.function.specializations = spec;
    
    // Increment global specialization counter
    ctx->specialization_count++;
    
    return spec;
}

// Find a specialization in a function type
FunctionSpecialization* type_context_find_specialization(TypeContext* ctx, TypeInfo* func_type,
                                                         TypeInfo** param_type_info, int param_count) {
    (void)ctx;  // Unused
    
    if (!func_type || func_type->kind != TYPE_KIND_FUNCTION) return NULL;

    FunctionSpecialization* spec = func_type->data.function.specializations;
    while (spec) {
        if (spec->param_count == param_count &&
            type_arrays_match(spec->param_type_info, param_type_info, param_count)) {
            return spec;
        }
        spec = spec->next;
    }

    return NULL;
}
