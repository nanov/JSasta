#include "jsasta_compiler.h"
#include <stdlib.h>
#include <string.h>

// Create a basic TypeInfo with just a base type
TypeInfo* type_info_create(TypeKind kind, char* name) {
    TypeInfo* info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->type_id = -1;  // Not registered yet
    info->type_name = name;
    info->kind = kind;
    
    // Initialize union data to zero
    memset(&info->data, 0, sizeof(info->data));
    
    return info;
}

TypeInfo* type_info_create_primitive(char* name) {
	return type_info_create(TYPE_KIND_PRIMITIVE, name);
}

// Create a fresh unknown type instance
TypeInfo* type_info_create_unknown() {
    return type_info_create(TYPE_KIND_UNKNOWN, strdup("unknown"));
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
    if (!type_info) return;

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
    if (!type_info) return;

    // Free type-specific data based on kind
    if (type_info->kind == TYPE_KIND_OBJECT) {
        // Free property names and types (recursively)
        for (int i = 0; i < type_info->data.object.property_count; i++) {
            free(type_info->data.object.property_names[i]);
            type_info_free(type_info->data.object.property_types[i]);
        }
        free(type_info->data.object.property_names);
        free(type_info->data.object.property_types);
    } else if (type_info->kind == TYPE_KIND_ARRAY) {
        // Free element type for arrays
        if (type_info->data.array.element_type) {
            type_info_free(type_info->data.array.element_type);
        }
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
    if (!type_info) return NULL;

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
            clone->data.object.property_types[i] = type_info_clone_internal(type_info->data.object.property_types[i], ctx);
        }
    }

    return clone;
}

// Check if a TypeInfo is a global primitive/singleton that shouldn't be cloned
bool type_info_is_global_singleton(TypeInfo* type_info) {
    if (!type_info) return false;

    // Check if it's one of the global singleton types
    return type_info == Type_Unknown ||
           type_info == Type_Bool ||
           type_info == Type_Void ||
           type_info == Type_Int ||
           type_info == Type_Double ||
           type_info == Type_Object ||
           type_info == Type_String ||
           type_info == Type_Array_Int ||
           type_info == Type_Array_Bool ||
           type_info == Type_Array_Double ||
           type_info == Type_Array_String;
}

// Clone a TypeInfo (deep copy with cycle detection)
// Returns the original pointer for global singletons
TypeInfo* type_info_clone(TypeInfo* type_info) {
    if (!type_info) return NULL;

    // Don't clone global singleton types - preserve the reference
    if (type_info_is_global_singleton(type_info)) {
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
