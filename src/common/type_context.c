#include "jsasta_compiler.h"
#include "traits.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Flag to track if global types have been initialized
static bool global_types_initialized = false;

#define INIT_GLOBAL_TYPE(TARGET, INIT) {\
	TARGET = INIT; \
	TARGET->is_global = true; \
}

#define INIT_ARRAY_TYPE(TARGET, NAME, ELEM) {\
	TARGET = INIT; \
	TARGET->is_global = true; \
}
// Initialize global type variables once at program startup
void type_system_init_global_types() {
    if (global_types_initialized) {
        return; // Already initialized
    }

    // Pre-register primitive types and cache them
    INIT_GLOBAL_TYPE(Type_Unknown,type_info_create(TYPE_KIND_UNKNOWN, strdup("unknown")));

    // Signed integer types
    INIT_GLOBAL_TYPE(Type_I8, type_info_create_integer(strdup("i8"), 8, true))
    INIT_GLOBAL_TYPE(Type_I16, type_info_create_integer(strdup("i16"), 16, true));
    INIT_GLOBAL_TYPE(Type_I32, type_info_create_integer(strdup("i32"), 32, true));
    INIT_GLOBAL_TYPE(Type_I64, type_info_create_integer(strdup("i64"), 64, true));

    // Unsigned integer types
    INIT_GLOBAL_TYPE(Type_U8, type_info_create_integer(strdup("u8"), 8, false))
    INIT_GLOBAL_TYPE(Type_U16, type_info_create_integer(strdup("u16"), 16, false))
    INIT_GLOBAL_TYPE(Type_U32, type_info_create_integer(strdup("u32"), 32, false))
    INIT_GLOBAL_TYPE(Type_U64, type_info_create_integer(strdup("u64"), 64, false))

    // Legacy "int" type (alias for i32)
    INIT_GLOBAL_TYPE(Type_Int, type_info_create_alias("int", Type_I32))

    INIT_GLOBAL_TYPE(Type_Double, type_info_create_primitive(strdup("double")))
    INIT_GLOBAL_TYPE(Type_String, type_info_create_primitive(strdup("string")))
    INIT_GLOBAL_TYPE(Type_Bool, type_info_create_primitive(strdup("bool")))
    INIT_GLOBAL_TYPE(Type_Void, type_info_create_primitive(strdup("void")))

    // Create array types for all integer types
    INIT_GLOBAL_TYPE(Type_Array_I8, type_info_create_array(Type_I8))
    INIT_GLOBAL_TYPE(Type_Array_I16, type_info_create_array(Type_I16));
    INIT_GLOBAL_TYPE(Type_Array_I32, type_info_create_array(Type_I32));
    INIT_GLOBAL_TYPE(Type_Array_I64, type_info_create_array(Type_I64));
    INIT_GLOBAL_TYPE(Type_Array_U8, type_info_create_array(Type_U8));
    INIT_GLOBAL_TYPE(Type_Array_U16, type_info_create_array(Type_U16));
    INIT_GLOBAL_TYPE(Type_Array_U32, type_info_create_array(Type_U32));
    INIT_GLOBAL_TYPE(Type_Array_U64, type_info_create_array(Type_U64));
    INIT_GLOBAL_TYPE(Type_Array_Int, type_info_create_array(Type_Int));
    INIT_GLOBAL_TYPE(Type_Array_Double, type_info_create_array(Type_Double));
    INIT_GLOBAL_TYPE(Type_Array_Bool, type_info_create_array(Type_Bool));
    INIT_GLOBAL_TYPE(Type_Array_String, type_info_create_array(Type_String));

    // Create object type placeholder
    INIT_GLOBAL_TYPE(Type_Object, type_info_create(TYPE_KIND_OBJECT, strdup("object")));

    // Create platform-specific type aliases
    #if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
        // 64-bit platform
        INIT_GLOBAL_TYPE(Type_Usize, type_info_create_alias(strdup("usize"), Type_U64));
        INIT_GLOBAL_TYPE(Type_Nint, type_info_create_alias(strdup("nint"), Type_I64));
        INIT_GLOBAL_TYPE(Type_Uint, type_info_create_alias(strdup("uint"), Type_U64));
    #else
        // 32-bit platform
        INIT_GLOBAL_TYPE(Type_Usize, type_info_create_alias(strdup("usize"), Type_U32));
        INIT_GLOBAL_TYPE(Type_Nint, type_info_create_alias(strdup("nint"), Type_I32));
        INIT_GLOBAL_TYPE(Type_Uint, type_info_create_alias(strdup("uint"), Type_U32));
    #endif

    global_types_initialized = true;
}

// Create a new TypeContext with pre-registered primitive types
TypeContext* type_context_create() {
    TypeContext* ctx = (TypeContext*)malloc(sizeof(TypeContext));
    ctx->type_table = NULL;
    ctx->type_count = 0;
    ctx->next_anonymous_id = 0;
    ctx->specialization_count = 0;
    ctx->trait_registry = NULL;
    ctx->module_prefix = NULL;  // Will be set by module loader

    // Register global types for lookup (doesn't mutate them - we skip type_id for globals)
    type_context_register_type(ctx, Type_Unknown);
    type_context_register_type(ctx, Type_I8);
    type_context_register_type(ctx, Type_I16);
    type_context_register_type(ctx, Type_I32);
    type_context_register_type(ctx, Type_I64);
    type_context_register_type(ctx, Type_U8);
    type_context_register_type(ctx, Type_U16);
    type_context_register_type(ctx, Type_U32);
    type_context_register_type(ctx, Type_U64);
    type_context_register_type(ctx, Type_Double);
    type_context_register_type(ctx, Type_String);
    type_context_register_type(ctx, Type_Bool);
    type_context_register_type(ctx, Type_Void);

    // Register array types
    type_context_register_type(ctx, Type_Array_I8);
    type_context_register_type(ctx, Type_Array_I16);
    type_context_register_type(ctx, Type_Array_I32);
    type_context_register_type(ctx, Type_Array_I64);
    type_context_register_type(ctx, Type_Array_U8);
    type_context_register_type(ctx, Type_Array_U16);
    type_context_register_type(ctx, Type_Array_U32);
    type_context_register_type(ctx, Type_Array_U64);
    type_context_register_type(ctx, Type_Array_Double);
    type_context_register_type(ctx, Type_Array_Bool);
    type_context_register_type(ctx, Type_Array_String);
    type_context_register_type(ctx, Type_Object);

    // Register platform-specific type aliases
    type_context_register_type(ctx, Type_Int);
    type_context_register_type(ctx, Type_Usize);
    type_context_register_type(ctx, Type_Nint);
    type_context_register_type(ctx, Type_Uint);

    // Initialize trait registry with built-in traits
    // Each TypeContext gets its own trait registry, but all reference the same global types
    ctx->trait_registry = trait_registry_create();
    traits_init_builtins(ctx->trait_registry);
    traits_register_builtin_impls(ctx->trait_registry);

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
    // Note: type_info_free will skip global types automatically
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        TypeEntry* next = entry->next;

        // type_info_free will check if it's a global type and skip freeing it
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

    if (!type->is_global) {
        type->type_id = ctx->type_count;
    }

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

    // First check the context's type table
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->type_name && strcmp(entry->type->type_name, type_name) == 0) {
            return entry->type;
        }
        entry = entry->next;
    }

    // If not found, check global types
    if (strcmp(type_name, "unknown") == 0) return Type_Unknown;
    if (strcmp(type_name, "i8") == 0) return Type_I8;
    if (strcmp(type_name, "i16") == 0) return Type_I16;
    if (strcmp(type_name, "i32") == 0) return Type_I32;
    if (strcmp(type_name, "i64") == 0) return Type_I64;
    if (strcmp(type_name, "u8") == 0) return Type_U8;
    if (strcmp(type_name, "u16") == 0) return Type_U16;
    if (strcmp(type_name, "u32") == 0) return Type_U32;
    if (strcmp(type_name, "u64") == 0) return Type_U64;
    if (strcmp(type_name, "double") == 0) return Type_Double;
    if (strcmp(type_name, "string") == 0) return Type_String;
    if (strcmp(type_name, "bool") == 0) return Type_Bool;
    if (strcmp(type_name, "void") == 0) return Type_Void;

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
    (void)ctx;
    return type_info_resolve_alias(Type_I32);
}

TypeInfo* type_context_get_double(TypeContext* ctx) {
    (void)ctx;
    return type_info_resolve_alias(Type_Double);
}

TypeInfo* type_context_get_string(TypeContext* ctx) {
    (void)ctx;
    return type_info_resolve_alias(Type_String);
}

TypeInfo* type_context_get_bool(TypeContext* ctx) {
    (void)ctx;
    return type_info_resolve_alias(Type_Bool);
}

TypeInfo* type_context_get_void(TypeContext* ctx) {
    (void)ctx;
    return type_info_resolve_alias(Type_Void);
}

// Get or create a reference type to target_type
// This ensures we reuse the same ref type for the same target
TypeInfo* type_context_get_or_create_ref_type(TypeContext* ctx, TypeInfo* target_type, bool is_mutable) {
    if (!ctx || !target_type) return NULL;

    // Generate the ref type name
    char type_name[256];
    snprintf(type_name, sizeof(type_name), "ref<%s>",
            target_type->type_name ? target_type->type_name : "?");

    // Check if this ref type already exists
    TypeInfo* existing = type_context_find_type(ctx, type_name);
    if (existing && existing->kind == TYPE_KIND_REF) {
        return existing;
    }

    // Create new ref type
    TypeInfo* ref_type = type_info_create(TYPE_KIND_REF, strdup(type_name));
    ref_type->data.ref.target_type = target_type;
    ref_type->data.ref.is_mutable = is_mutable;

    // Register and return
    return type_context_register_type(ctx, ref_type);
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

    // IMPORTANT: Make a copy of param_types array to avoid double-free
    // The AST node's param_type_hints array will be freed when the AST is freed
    // This function type needs its own copy that will be freed by type_context_free
    if (param_types && param_count > 0) {
        func_type->data.function.param_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * param_count);
        memcpy(func_type->data.function.param_types, param_types, sizeof(TypeInfo*) * param_count);
    } else {
        func_type->data.function.param_types = NULL;
    }

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

    // IMPORTANT: Make copies of the arrays to avoid double-free
    // The AST node's arrays will be freed when the AST is freed
    // This TypeInfo needs its own copies that will be freed by type_context_free

    // Copy property names
    struct_type->data.object.property_names = (char**)malloc(sizeof(char*) * property_count);
    for (int i = 0; i < property_count; i++) {
        struct_type->data.object.property_names[i] = strdup(property_names[i]);
    }

    // Copy property types array (but the TypeInfo pointers themselves are shared references)
    struct_type->data.object.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * property_count);
    memcpy(struct_type->data.object.property_types, property_types, sizeof(TypeInfo*) * property_count);

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

// Create and register an enum type
TypeInfo* type_context_create_enum_type(TypeContext* ctx, const char* enum_name,
                                         char** variant_names, char*** variant_field_names,
                                         TypeInfo*** variant_field_types, int* variant_field_counts,
                                         int variant_count, ASTNode* enum_decl_node) {
    if (!ctx || !enum_name) return NULL;

    // Check if enum type already exists
    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_ENUM &&
            entry->type->type_name &&
            strcmp(entry->type->type_name, enum_name) == 0) {
            log_error("Enum '%s' is already defined", enum_name);
            return entry->type; // Return existing
        }
        entry = entry->next;
    }

    // Create new enum type
    TypeInfo* enum_type = type_info_create(TYPE_KIND_ENUM, strdup(enum_name));

    // Copy variant names
    enum_type->data.enum_type.variant_names = (char**)malloc(sizeof(char*) * variant_count);
    for (int i = 0; i < variant_count; i++) {
        enum_type->data.enum_type.variant_names[i] = strdup(variant_names[i]);
    }

    // Copy variant field names (deep copy)
    enum_type->data.enum_type.variant_field_names = (char***)malloc(sizeof(char**) * variant_count);
    for (int i = 0; i < variant_count; i++) {
        if (variant_field_names[i]) {
            enum_type->data.enum_type.variant_field_names[i] = (char**)malloc(sizeof(char*) * variant_field_counts[i]);
            for (int j = 0; j < variant_field_counts[i]; j++) {
                enum_type->data.enum_type.variant_field_names[i][j] = strdup(variant_field_names[i][j]);
            }
        } else {
            enum_type->data.enum_type.variant_field_names[i] = NULL;
        }
    }

    // Copy variant field types array (TypeInfo pointers are shared references)
    enum_type->data.enum_type.variant_field_types = (TypeInfo***)malloc(sizeof(TypeInfo**) * variant_count);
    for (int i = 0; i < variant_count; i++) {
        if (variant_field_types[i]) {
            enum_type->data.enum_type.variant_field_types[i] = (TypeInfo**)malloc(sizeof(TypeInfo*) * variant_field_counts[i]);
            memcpy(enum_type->data.enum_type.variant_field_types[i], variant_field_types[i],
                   sizeof(TypeInfo*) * variant_field_counts[i]);
        } else {
            enum_type->data.enum_type.variant_field_types[i] = NULL;
        }
    }

    // Copy variant field counts
    enum_type->data.enum_type.variant_field_counts = (int*)malloc(sizeof(int) * variant_count);
    memcpy(enum_type->data.enum_type.variant_field_counts, variant_field_counts, sizeof(int) * variant_count);

    enum_type->data.enum_type.variant_count = variant_count;
    enum_type->data.enum_type.enum_decl_node = enum_decl_node;  // Store reference

    // Create struct types for each variant with data fields
    // This allows pattern matching with struct bindings: if (expr is Variant(let m)) { m.field }
    for (int i = 0; i < variant_count; i++) {
        if (variant_field_counts[i] > 0) {
            // Create struct type name: "EnumName.VariantName"
            size_t type_name_len = strlen(enum_name) + strlen(variant_names[i]) + 2; // +2 for '.' and '\0'
            char* struct_type_name = (char*)malloc(type_name_len);
            snprintf(struct_type_name, type_name_len, "%s.%s", enum_name, variant_names[i]);

            // Create the struct type (using TYPE_KIND_OBJECT which has the fields we need)
            TypeInfo* struct_type = type_info_create(TYPE_KIND_OBJECT, struct_type_name);
            struct_type->data.object.property_names = (char**)malloc(sizeof(char*) * variant_field_counts[i]);
            struct_type->data.object.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * variant_field_counts[i]);

            // Copy field names and types from the variant
            for (int j = 0; j < variant_field_counts[i]; j++) {
                struct_type->data.object.property_names[j] = strdup(variant_field_names[i][j]);
                struct_type->data.object.property_types[j] = variant_field_types[i][j];
            }

            struct_type->data.object.property_count = variant_field_counts[i];
            struct_type->data.object.struct_decl_node = NULL;  // No declaration node for generated types

            // Register the struct type in the type context
            type_context_register_type(ctx, struct_type);

            log_verbose("Created struct type '%s' for enum variant with %d fields",
                       struct_type_name, variant_field_counts[i]);
        }
    }

    TypeInfo* registered_enum = type_context_register_type(ctx, enum_type);
    
    // Auto-implement Eq and Display traits for this enum
    if (ctx->trait_registry) {
        trait_register_eq_for_enum(registered_enum, ctx->trait_registry);
        trait_register_display_for_enum(registered_enum, ctx->trait_registry);
    }
    
    return registered_enum;
}

// Find an enum type by name
TypeInfo* type_context_find_enum_type(TypeContext* ctx, const char* enum_name) {
    if (!ctx || !enum_name) return NULL;

    TypeEntry* entry = ctx->type_table;
    while (entry) {
        if (entry->type->kind == TYPE_KIND_ENUM &&
            entry->type->type_name &&
            strcmp(entry->type->type_name, enum_name) == 0) {
            return entry->type;
        }
        entry = entry->next;
    }

    return NULL;
}

// Helper: Check if two TypeInfo arrays match (with implicit ref conversion and alias resolution)
static bool type_arrays_match(TypeInfo** types1, TypeInfo** types2, int count) {
    for (int i = 0; i < count; i++) {
        TypeInfo* t1 = types1[i];
        TypeInfo* t2 = types2[i];

        // Exact match
        if (t1 == t2) continue;

        // Resolve aliases on both sides
        t1 = type_info_resolve_alias(t1);
        t2 = type_info_resolve_alias(t2);

        // Check again after alias resolution
        if (t1 == t2) continue;

        // Check for implicit ref conversion: T <-> ref<T>
        // Unwrap one level of ref on both sides (returns the type itself if not a ref)
        // Then compare: this allows counter_t to match ref<counter_t>
        if (type_info_resolve_alias(type_info_get_ref_target(t1)) ==
            type_info_resolve_alias(type_info_get_ref_target(t2))) {
            continue;
        }

        return false;
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

    // Generate specialized name using type names with module prefix
    char name[256];
    int offset = 0;

    // Add module prefix if available (with __ separator)
    if (ctx->module_prefix && ctx->module_prefix[0]) {
        offset = snprintf(name, 256, "%s__%s", ctx->module_prefix, func_type->type_name);
    } else {
        offset = snprintf(name, 256, "%s", func_type->type_name);
    }

    // Add parameter types
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
    spec->llvm_func = NULL;         // Will be set during codegen_declare_functions

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
        // For variadic functions, allow call with more arguments than declared parameters
        bool param_count_matches;
        if (func_type->data.function.is_variadic) {
            // Call must have at least as many args as required params (excluding ...)
            param_count_matches = (param_count >= spec->param_count);
        } else {
            // Non-variadic: exact match required
            param_count_matches = (spec->param_count == param_count);
        }

        // Match required parameters (variadic only checks the required params, not extra args)
        int params_to_check = spec->param_count;
        if (param_count_matches &&
            type_arrays_match(spec->param_type_info, param_type_info, params_to_check)) {
            return spec;
        }
        spec = spec->next;
    }

    return NULL;
}
