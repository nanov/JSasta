#include "jsasta_compiler.h"
#include <stdlib.h>
#include <string.h>

// Create a basic TypeInfo with just a base type
TypeInfo* type_info_create(ValueType base_type) {
    TypeInfo* info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->base_type = base_type;
    info->property_names = NULL;
    info->property_types = NULL;
    info->property_count = 0;
    info->type_name = NULL;
    return info;
}

// Create TypeInfo from an object literal AST node
TypeInfo* type_info_create_from_object_literal(ASTNode* obj_literal) {
    if (!obj_literal || obj_literal->type != AST_OBJECT_LITERAL) {
        return NULL;
    }
    
    TypeInfo* info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->base_type = TYPE_OBJECT;
    info->property_count = obj_literal->object_literal.count;
    info->type_name = NULL;
    
    // Allocate arrays for property metadata
    info->property_names = (char**)malloc(sizeof(char*) * info->property_count);
    info->property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * info->property_count);
    
    // Copy property information
    for (int i = 0; i < info->property_count; i++) {
        info->property_names[i] = strdup(obj_literal->object_literal.keys[i]);
        
        // Create TypeInfo for each property
        ValueType prop_type = obj_literal->object_literal.values[i]->value_type;
        info->property_types[i] = type_info_create(prop_type);
        
        // If property is also an object, recursively create its TypeInfo
        if (prop_type == TYPE_OBJECT && 
            obj_literal->object_literal.values[i]->type == AST_OBJECT_LITERAL) {
            type_info_free(info->property_types[i]);
            info->property_types[i] = type_info_create_from_object_literal(
                obj_literal->object_literal.values[i]);
        }
    }
    
    return info;
}

// Free TypeInfo and all its nested data
void type_info_free(TypeInfo* type_info) {
    if (!type_info) return;
    
    // Free property names and types
    for (int i = 0; i < type_info->property_count; i++) {
        free(type_info->property_names[i]);
        type_info_free(type_info->property_types[i]);
    }
    
    free(type_info->property_names);
    free(type_info->property_types);
    free(type_info->type_name);
    free(type_info);
}

// Clone a TypeInfo (deep copy)
TypeInfo* type_info_clone(TypeInfo* type_info) {
    if (!type_info) return NULL;
    
    TypeInfo* clone = (TypeInfo*)malloc(sizeof(TypeInfo));
    clone->base_type = type_info->base_type;
    clone->property_count = type_info->property_count;
    clone->type_name = type_info->type_name ? strdup(type_info->type_name) : NULL;
    
    if (type_info->property_count > 0) {
        clone->property_names = (char**)malloc(sizeof(char*) * clone->property_count);
        clone->property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * clone->property_count);
        
        for (int i = 0; i < clone->property_count; i++) {
            clone->property_names[i] = strdup(type_info->property_names[i]);
            clone->property_types[i] = type_info_clone(type_info->property_types[i]);
        }
    } else {
        clone->property_names = NULL;
        clone->property_types = NULL;
    }
    
    return clone;
}

// Find property index by name, returns -1 if not found
int type_info_find_property(TypeInfo* type_info, const char* property_name) {
    if (!type_info || !property_name) return -1;
    
    for (int i = 0; i < type_info->property_count; i++) {
        if (strcmp(type_info->property_names[i], property_name) == 0) {
            return i;
        }
    }
    
    return -1;
}
