# Type System Refactoring Plan

## Problem Statement

Currently, JSasta has two parallel type representations:
1. `ValueType` enum (TYPE_INT, TYPE_STRING, etc.)
2. `TypeInfo` struct (with property information for objects)

This creates:
- Duplication and inconsistency
- Confusion about which to use
- Difficulty comparing types
- No proper type identity for objects

## Solution: Unified Type System

### Architecture

```
TypeContext (renamed from SpecializationContext)
├── type_table: TypeInfo*[]       // All registered types
├── type_count: int                // Number of types
├── next_anonymous_id: int         // Counter for Object_0, Object_1, etc.
├── specializations: ...           // Existing specialization data
└── functions: ...                 // Type table utilities
```

### TypeInfo Structure (Enhanced)

```c
struct TypeInfo {
    int type_id;                   // Unique ID in type table
    char* type_name;               // "int", "string", "Object_0", "Person", etc.
    TypeKind kind;                 // PRIMITIVE, OBJECT, ARRAY, FUNCTION
    
    // For objects
    char** property_names;
    TypeInfo** property_types;     // References to other types in table
    int property_count;
    
    // For arrays
    TypeInfo* element_type;
    
    // For functions (future)
    TypeInfo** param_types;
    TypeInfo* return_type;
    int param_count;
};

typedef enum {
    TYPE_KIND_PRIMITIVE,    // int, double, string, bool, void
    TYPE_KIND_OBJECT,       // { name: string, age: int }
    TYPE_KIND_ARRAY,        // int[], string[]
    TYPE_KIND_FUNCTION,     // (int, int) => int
    TYPE_KIND_UNKNOWN
} TypeKind;
```

### Benefits of Unified System

1. **Single Source of Truth**: TypeInfo is the only type representation
2. **Type Identity**: Can compare types by pointer/ID
3. **Type Interning**: Identical structures share types (memory efficient)
4. **Better Errors**: Full type information in error messages
5. **Future-Ready**: Easy to add user-defined types
6. **Cleaner Code**: No confusion about value_type vs type_info

### Migration Strategy

#### Phase 1: Create TypeContext with TypeTable
- Define TypeContext structure (extends SpecializationContext)
- Implement type registration functions
- Pre-register primitive types at initialization
- Add type lookup by name/ID
- Add type comparison functions

#### Phase 2: Migrate Core Structures
- Replace `ValueType value_type` with `TypeInfo* type` in ASTNode
- Replace `ValueType type` with `TypeInfo* type` in SymbolEntry
- Remove duplicate `type_info` field from SymbolEntry
- Update FunctionSpecialization to use TypeInfo consistently

#### Phase 3: Update All Code Paths
- Update parser to reference types from table
- Update type inference to use unified TypeInfo
- Update codegen to use TypeInfo
- Update specialization to use TypeInfo

#### Phase 4: Type Interning for Objects
- Implement object type comparison
- Create or reuse existing types for identical structures
- Ensure anonymous objects get unique names

#### Phase 5: Cleanup
- Remove or minimize ValueType enum usage
- Consolidate helper functions
- Update error messages
- Add comprehensive tests

## API Design

```c
// Type context management
TypeContext* type_context_create(void);
void type_context_free(TypeContext* ctx);

// Type registration
TypeInfo* type_register_primitive(TypeContext* ctx, const char* name);
TypeInfo* type_register_object(TypeContext* ctx, const char* name,
                                char** prop_names, TypeInfo** prop_types, 
                                int count);
TypeInfo* type_register_anonymous_object(TypeContext* ctx, 
                                          char** prop_names,
                                          TypeInfo** prop_types, 
                                          int count);
TypeInfo* type_register_array(TypeContext* ctx, TypeInfo* element_type);

// Type lookup
TypeInfo* type_lookup(TypeContext* ctx, const char* name);
TypeInfo* type_find_or_create_object(TypeContext* ctx, 
                                      char** prop_names,
                                      TypeInfo** prop_types,
                                      int count);

// Type operations
bool type_equals(TypeInfo* a, TypeInfo* b);
bool type_is_compatible(TypeInfo* from, TypeInfo* to);
bool type_is_primitive(TypeInfo* type);
bool type_is_object(TypeInfo* type);
bool type_is_array(TypeInfo* type);

// Utilities
const char* type_to_string(TypeInfo* type);
void type_print(TypeInfo* type, FILE* out);
```

## Anonymous Type Naming & Interning

```javascript
// Example: Type interning in action
var obj1 = { name: "Alice", age: 30 };  // Creates Object_0
var obj2 = { name: "Bob", age: 25 };    // Reuses Object_0 (same structure!)
var obj3 = { x: 10, y: 20 };            // Creates Object_1 (different structure)

// obj1 and obj2 share the same TypeInfo*
// obj3 has its own TypeInfo*
```

**Type Interning Algorithm**:
1. When creating object literal, compute "signature" (property names + types)
2. Check type table for existing type with same signature
3. If found: reuse existing TypeInfo
4. If not found: create new with unique name (Object_N)

## Implementation Priority

### Critical Path (Phase 1-2)
1. Create TypeContext structure
2. Implement type table with primitives
3. Add type lookup functions
4. Migrate ASTNode to use TypeInfo*

### High Priority (Phase 3)
5. Migrate SymbolEntry to use TypeInfo*
6. Update type inference
7. Update codegen

### Medium Priority (Phase 4)
8. Implement type interning
9. Update specializations

### Low Priority (Phase 5)
10. Remove ValueType usage
11. Cleanup and refactor
12. Comprehensive testing

## Example: Before vs After

### Before (Current - Confusing)

```c
// In multiple places, unclear which to use
if (node->value_type == TYPE_OBJECT && entry->type_info != NULL) {
    // Access entry->type_info for object details
    // But also check node->value_type?
}

// Duplication in symbol table
typedef struct SymbolEntry {
    ValueType type;        // Basic type
    TypeInfo* type_info;   // Detailed type (for objects)
    // Which one is authoritative?
} SymbolEntry;
```

### After (Proposed - Clean)

```c
// Single, clear representation
if (type_is_object(node->type)) {
    // node->type has ALL information
    for (int i = 0; i < node->type->property_count; i++) {
        printf("%s: %s\n", 
               node->type->property_names[i],
               node->type->property_types[i]->type_name);
    }
}

// Clean symbol table
typedef struct SymbolEntry {
    TypeInfo* type;        // Single source of truth
    // No confusion!
} SymbolEntry;
```

## Future Extensions

Once unified system is in place, easy to add:

### 1. User-Defined Types
```javascript
type Person = { name: string, age: int };
var p: Person = { name: "Alice", age: 30 };
```

### 2. Type Aliases
```javascript
type Age = int;
type Coordinate = { x: double, y: double };
```

### 3. Structural vs Nominal Typing
```javascript
// Structural (current): types match by structure
var obj1: { name: string } = { name: "Alice" };
var obj2: { name: string } = { name: "Bob" };  // Same type

// Nominal (future): types match by name
type Person = { name: string };
type Company = { name: string };
// Person !== Company even with same structure
```

## Testing Strategy

1. **Unit Tests**
   - Type registration
   - Type lookup
   - Type equality
   - Type interning

2. **Integration Tests**
   - Full compilation pipeline with new type system
   - Object type matching
   - Function specialization with objects

3. **Regression Tests**
   - All existing test cases must pass
   - Error messages must be clear

## Timeline Estimate

- **Phase 1**: Create infrastructure - 3-4 hours
- **Phase 2-3**: Migrate core + update code - 5-6 hours
- **Phase 4**: Type interning - 2-3 hours
- **Phase 5**: Cleanup and testing - 3-4 hours

**Total**: 13-17 hours of focused development

## Open Questions

1. **ValueType enum**: Keep minimal version or remove completely?
   - Recommendation: Keep for LLVM type mapping, but not for semantic analysis

2. **Type interning**: Automatic or opt-in?
   - Recommendation: Automatic for correctness

3. **Type printing**: How detailed in errors?
   - Recommendation: Show full structure for objects, names for primitives

4. **Type caching**: Performance optimization needed?
   - Recommendation: Start simple, optimize if needed

## Summary

This refactoring addresses a fundamental design issue in the type system by:
- **Eliminating duplication** between ValueType and TypeInfo
- **Providing type identity** for proper comparison
- **Enabling type interning** for efficiency
- **Preparing for future features** like user-defined types

The unified type system will make the codebase cleaner, more maintainable, and ready for advanced type system features.

---

**Status**: Design Document  
**Next Step**: Begin Phase 1 implementation  
**Estimated Completion**: 2-3 days of focused work
