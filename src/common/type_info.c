#include "jsasta_compiler.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Global type variable definitions (declared as extern in jsasta_compiler.h)
TypeInfo* Type_Unknown = NULL;
TypeInfo* Type_Bool = NULL;
TypeInfo* Type_Void = NULL;

TypeInfo* Type_I8 = NULL;
TypeInfo* Type_I16 = NULL;
TypeInfo* Type_I32 = NULL;
TypeInfo* Type_I64 = NULL;

TypeInfo* Type_U8 = NULL;
TypeInfo* Type_U16 = NULL;
TypeInfo* Type_U32 = NULL;
TypeInfo* Type_U64 = NULL;

TypeInfo* Type_Int = NULL;

TypeInfo* Type_Usize = NULL;
TypeInfo* Type_Nint = NULL;
TypeInfo* Type_Uint = NULL;

TypeInfo* Type_Double = NULL;
TypeInfo* Type_Object = NULL;
TypeInfo* Type_Str = NULL;
TypeInfo* Type_CStr = NULL;

TypeInfo* Type_Array_Int = NULL;
TypeInfo* Type_Array_I8 = NULL;
TypeInfo* Type_Array_I16 = NULL;
TypeInfo* Type_Array_I32 = NULL;
TypeInfo* Type_Array_I64 = NULL;
TypeInfo* Type_Array_U8 = NULL;
TypeInfo* Type_Array_U16 = NULL;
TypeInfo* Type_Array_U32 = NULL;
TypeInfo* Type_Array_U64 = NULL;
TypeInfo* Type_Array_Bool = NULL;
TypeInfo* Type_Array_Double = NULL;
TypeInfo* Type_Array_Str = NULL;

// Create a basic TypeInfo with just a base type
TypeInfo* type_info_create(TypeKind kind, char* name) {
    TypeInfo* info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->type_id = -1;  // Not registered yet
    info->type_name = name;
    info->kind = kind;
    info->is_global = false;

    // Initialize union data to zero
    memset(&info->data, 0, sizeof(info->data));

    return info;
}

TypeInfo* type_info_create_primitive(char* name) {
	return type_info_create(TYPE_KIND_PRIMITIVE, name);
}

// Create an integer type with specific bit width and signedness
TypeInfo* type_info_create_integer(char* name, int bit_width, bool is_signed) {
    TypeInfo* info = type_info_create(TYPE_KIND_PRIMITIVE, name);
    info->data.integer.bit_width = bit_width;
    info->data.integer.is_signed = is_signed;
    return info;
}

TypeInfo* type_info_create_array(TypeInfo* element_type) {
	static char buffer[1024];
	strcpy(buffer, element_type->type_name);
	strcat(buffer, "[]");
	TypeInfo* type = type_info_create(TYPE_KIND_ARRAY, buffer);
	type->data.array.element_type = element_type;
	return type;
}

// Create a fresh unknown type instance
TypeInfo* type_info_create_unknown() {
    return type_info_create(TYPE_KIND_UNKNOWN, strdup("unknown"));
}

// Create a type alias
TypeInfo* type_info_create_alias(char* alias_name, TypeInfo* target_type) {
    TypeInfo* info = type_info_create(TYPE_KIND_ALIAS, alias_name);
    info->data.alias.target_type = target_type;
    return info;
}

// Recursively resolve type aliases to get the actual type
TypeInfo* type_info_resolve_alias(TypeInfo* type_info) {
    if (!type_info) return NULL;

    // Keep following alias chain until we hit a non-alias type
    while (type_info && type_info->kind == TYPE_KIND_ALIAS) {
        type_info = type_info->data.alias.target_type;
    }

    return type_info;
}

// Create TypeInfo from an object literal AST node
TypeInfo* type_info_create_from_object_literal(ASTNode* obj_literal) {
    if (!obj_literal || obj_literal->type != AST_OBJECT_LITERAL) {
        return NULL;
    }

    TypeInfo* info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->type_id = -1;  // Not registered yet
    info->type_name = NULL;
    info->kind = TYPE_KIND_OBJECT;

    // Initialize union
    memset(&info->data, 0, sizeof(info->data));

    // Set object-specific data
    info->data.object.property_count = obj_literal->object_literal.count;
    info->data.object.property_names = (char**)malloc(sizeof(char*) * info->data.object.property_count);
    info->data.object.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * info->data.object.property_count);

    // Copy property information
    for (int i = 0; i < info->data.object.property_count; i++) {
        info->data.object.property_names[i] = strdup(obj_literal->object_literal.keys[i]);

        // Property types are references to already-inferred types in AST
        TypeInfo* prop_type_info = obj_literal->object_literal.values[i]->type_info;
        info->data.object.property_types[i] = prop_type_info;

        // If property is also an object, recursively create its TypeInfo
        if (type_info_is_object(prop_type_info) &&
        		obj_literal->object_literal.values[i]->type == AST_OBJECT_LITERAL) {
            type_info_free(info->data.object.property_types[i]);
            info->data.object.property_types[i] = type_info_create_from_object_literal(
                obj_literal->object_literal.values[i]);
        }
    }

    return info;
}

// Free TypeInfo without freeing referenced types (shallow free)
// Used when property_types and element_type are references owned by TypeContext
void type_info_free_shallow(TypeInfo* type_info) {
    if (!type_info || type_info->is_global) return;

    // Free type-specific data based on kind
    if (type_info->kind == TYPE_KIND_OBJECT) {
        // Free property names (strings we own)
        for (int i = 0; i < type_info->data.object.property_count; i++) {
            free(type_info->data.object.property_names[i]);
        }
        free(type_info->data.object.property_names);
        free(type_info->data.object.property_types);  // Free array, not the TypeInfos it points to
    }
    // For arrays: element_type is a reference, don't free
    // For functions: param_types and return_type are references, don't free

    free(type_info->type_name);
    free(type_info);
}

// Free TypeInfo and all its nested data (deep free)
// WARNING: Only use this when TypeInfo owns its nested types (not references)
void type_info_free(TypeInfo* type_info) {
    if (!type_info || type_info->is_global) return;

    // Free type-specific data based on kind
    if (type_info->kind == TYPE_KIND_OBJECT) {
        // Free property names
        for (int i = 0; i < type_info->data.object.property_count; i++) {
            free(type_info->data.object.property_names[i]);
        }
        free(type_info->data.object.property_names);

        // Note: property_types are references to types in TypeContext, don't free the TypeInfo objects
        // Just free the array itself
        free(type_info->data.object.property_types);
    } else if (type_info->kind == TYPE_KIND_ARRAY) {
        // Note: element_type is a reference to a type in TypeContext, don't free it
        // The TypeContext will free all registered types
    } else if (type_info->kind == TYPE_KIND_ALIAS) {
        // Note: target_type is a reference to a type in TypeContext, don't free it
        // The TypeContext will free all registered types
    } else if (type_info->kind == TYPE_KIND_FUNCTION) {
        // Note: param_types and return_type are references to types in TypeContext, don't free them
        // Just free the array itself
        if (type_info->data.function.param_types) {
            free(type_info->data.function.param_types);
        }

        // Free all specializations
        FunctionSpecialization* spec = type_info->data.function.specializations;
        while (spec) {
            FunctionSpecialization* next = spec->next;
            free(spec->specialized_name);
            if (spec->param_type_info) {
                // Note: param_type_info are references to types in TypeContext, don't free them
                free(spec->param_type_info);
            }
            // Note: param_names is a reference to the original function's params, don't free it
            if (spec->specialized_body) {
                ast_free(spec->specialized_body);
            }
            free(spec);
            spec = next;
        }

        // Note: original_body is a reference to AST, not owned by TypeInfo
        // Note: return_type is a reference to TypeContext, don't free it
    }

    free(type_info->type_name);
    free(type_info);
}

// Helper structure for tracking visited nodes during clone
typedef struct CloneContext {
    TypeInfo** visited_originals;  // Array of original pointers we've seen
    TypeInfo** visited_clones;     // Array of corresponding clones
    int count;
    int capacity;
} CloneContext;

// Internal clone function with cycle detection
static TypeInfo* type_info_clone_internal(TypeInfo* type_info, CloneContext* ctx) {
    if (!type_info || type_info->is_global) return NULL;


    // Check if we've already cloned this TypeInfo (cycle detection)
    for (int i = 0; i < ctx->count; i++) {
        if (ctx->visited_originals[i] == type_info) {
            // Return the existing clone instead of creating a new one
            return ctx->visited_clones[i];
        }
    }

    // Create the clone
    TypeInfo* clone = (TypeInfo*)malloc(sizeof(TypeInfo));
    clone->type_id = type_info->type_id;
    clone->type_name = type_info->type_name ? strdup(type_info->type_name) : NULL;
    clone->kind = type_info->kind;

    // Initialize union
    memset(&clone->data, 0, sizeof(clone->data));

    // Register this clone BEFORE recursing (to handle cycles)
    if (ctx->count >= ctx->capacity) {
        ctx->capacity *= 2;
        ctx->visited_originals = (TypeInfo**)realloc(ctx->visited_originals, sizeof(TypeInfo*) * ctx->capacity);
        ctx->visited_clones = (TypeInfo**)realloc(ctx->visited_clones, sizeof(TypeInfo*) * ctx->capacity);
    }
    ctx->visited_originals[ctx->count] = type_info;
    ctx->visited_clones[ctx->count] = clone;
    ctx->count++;

    // Now recursively clone type-specific data based on kind
    if (type_info->kind == TYPE_KIND_ARRAY && type_info->data.array.element_type) {
        clone->data.array.element_type = type_info_clone_internal(type_info->data.array.element_type, ctx);
    } else if (type_info->kind == TYPE_KIND_OBJECT && type_info->data.object.property_count > 0) {
        clone->data.object.property_count = type_info->data.object.property_count;
        clone->data.object.property_names = (char**)malloc(sizeof(char*) * clone->data.object.property_count);
        clone->data.object.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * clone->data.object.property_count);

        for (int i = 0; i < clone->data.object.property_count; i++) {
            clone->data.object.property_names[i] = strdup(type_info->data.object.property_names[i]);
            // Recursively clone property types, but primitives will return themselves
            clone->data.object.property_types[i] = type_info_clone_internal(type_info->data.object.property_types[i], ctx);
        }
    } else if (type_info->kind == TYPE_KIND_REF && type_info->data.ref.target_type) {
        clone->data.ref.target_type = type_info_clone_internal(type_info->data.ref.target_type, ctx);
        clone->data.ref.is_mutable = type_info->data.ref.is_mutable;
    }

    return clone;
}

// Clone a TypeInfo (deep copy with cycle detection)
// Returns the original pointer for global singletons
TypeInfo* type_info_clone(TypeInfo* type_info) {
    if (!type_info) return NULL;

    // Global types (like primitives) are shared, don't clone them - just return the reference
    if (type_info->is_global) return type_info;

    // ALL struct/object types registered in TypeContext should not be cloned
    // This includes both named structs (like "counter_t") and anonymous objects (like "Object_123")
    // They are managed by TypeContext and should be treated as singletons
    if (type_info->kind == TYPE_KIND_OBJECT && type_info->type_name) {
        // This is a struct/object type - don't clone, return the reference
        return type_info;
    }

    // Create context for tracking visited nodes
    CloneContext ctx;
    ctx.capacity = 16;
    ctx.count = 0;
    ctx.visited_originals = (TypeInfo**)malloc(sizeof(TypeInfo*) * ctx.capacity);
    ctx.visited_clones = (TypeInfo**)malloc(sizeof(TypeInfo*) * ctx.capacity);

    TypeInfo* clone = type_info_clone_internal(type_info, &ctx);

    // Clean up context
    free(ctx.visited_originals);
    free(ctx.visited_clones);

    return clone;
}

// Find property index by name, returns -1 if not found
int type_info_find_property(TypeInfo* type_info, const char* property_name) {
    if (!type_info || !property_name) return -1;

    if (type_info->kind != TYPE_KIND_OBJECT) return -1;

    for (int i = 0; i < type_info->data.object.property_count; i++) {
        if (strcmp(type_info->data.object.property_names[i], property_name) == 0) {
            return i;
        }
    }

    return -1;
}
