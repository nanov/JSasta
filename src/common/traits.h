#ifndef JSASTA_TRAITS_H
#define JSASTA_TRAITS_H

#include "jsasta_compiler.h"
#include <llvm-c/Core.h>

// Forward declarations
typedef struct TypeInfo TypeInfo;
typedef struct TypeContext TypeContext;
typedef struct CodeGen CodeGen;
typedef struct Trait Trait;
typedef struct TraitImpl TraitImpl;
typedef struct TraitRegistry TraitRegistry;

// Method implementation kinds
typedef enum {
    METHOD_INTRINSIC,  // Built-in LLVM IR generation
    METHOD_FUNCTION,   // Call to JSasta function
    METHOD_EXTERNAL    // Call to external C function
} MethodKind;

// Generic type parameter for traits (e.g., "Rhs" in Add<Rhs>)
typedef struct {
    const char* name;        // e.g., "Rhs", "T"
    TypeInfo* default_type;  // Default binding (Self for Rhs in Add)
    TypeInfo* constraint;    // Optional trait constraint
} TraitTypeParam;

// Associated type for traits (e.g., "Output" in Add)
typedef struct {
    const char* name;        // e.g., "Output", "Item"
    TypeInfo* constraint;    // Optional trait constraint
} TraitAssocType;

// Method implementation
typedef struct {
    const char* method_name;       // Name of the method
    TypeInfo* signature;           // Function type signature
    MethodKind kind;               // How to invoke this method

    // For METHOD_INTRINSIC: direct LLVM codegen
    LLVMValueRef (*codegen)(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context);

    // For METHOD_FUNCTION: compiled JSasta function or context for intrinsics
    void* function_ptr;

    // For METHOD_EXTERNAL: external C function
    const char* external_name;
} MethodImpl;

// Trait definition
struct Trait {
    const char* name;

    // Generic type parameters (e.g., [Rhs] for Add<Rhs>)
    TraitTypeParam* type_params;
    int type_param_count;

    // Associated types (e.g., [Output] for Add)
    TraitAssocType* assoc_types;
    int assoc_type_count;

    // Method signatures required by this trait
    const char** method_names;
    TypeInfo** method_signatures;
    int method_count;

    // Linked list of implementations
    TraitImpl* first_impl;

    struct Trait* next;
};

// Trait implementation (links a type to method implementations)
struct TraitImpl {
    Trait* trait;
    TypeInfo* impl_type;                // Type implementing trait (e.g., int)

    // Type parameter bindings (e.g., [double] for Add<double>)
    TypeInfo** type_param_bindings;
    int type_param_count;

    // Associated type bindings (e.g., [double] for Output)
    TypeInfo** assoc_type_bindings;
    int assoc_type_count;

    // Method implementations
    MethodImpl* methods;
    int method_count;

    struct TraitImpl* next;
};

// Trait registry
struct TraitRegistry {
    Trait* first_trait;
    int trait_count;
};

// Global trait references (for quick access to common traits)
extern Trait* Trait_Add;
extern Trait* Trait_Sub;
extern Trait* Trait_Mul;
extern Trait* Trait_Div;
extern Trait* Trait_Rem;
extern Trait* Trait_BitAnd;
extern Trait* Trait_BitOr;
extern Trait* Trait_BitXor;
extern Trait* Trait_Shl;
extern Trait* Trait_Shr;
extern Trait* Trait_Eq;
extern Trait* Trait_Ord;
extern Trait* Trait_Not;
extern Trait* Trait_Neg;
extern Trait* Trait_AddAssign;
extern Trait* Trait_SubAssign;
extern Trait* Trait_MulAssign;
extern Trait* Trait_DivAssign;
extern Trait* Trait_Index;
extern Trait* Trait_RefIndex;
extern Trait* Trait_Length;
extern Trait* Trait_Display;

// === Core Trait Registry Functions ===

// Create a new trait registry
TraitRegistry* trait_registry_create(void);

// Destroy a trait registry
void trait_registry_destroy(TraitRegistry* registry);

// Define a new trait with generic type parameters and associated types
Trait* trait_define_full(
    TraitRegistry* registry,
    const char* name,
    TraitTypeParam* type_params,
    int type_param_count,
    TraitAssocType* assoc_types,
    int assoc_type_count,
    const char** method_names,
    TypeInfo** method_signatures,
    int method_count
);

// Simplified trait definition (no generics, no associated types)
Trait* trait_define_simple(
    TraitRegistry* registry,
    const char* name,
    const char** method_names,
    TypeInfo** method_signatures,
    int method_count
);

// Find a trait by name
Trait* trait_find(TraitRegistry* registry, const char* name);

// === Trait Implementation Functions ===

// Implement a trait for a type with full generic and associated type bindings
void trait_impl_full(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count,
    TypeInfo** assoc_type_bindings,
    int assoc_type_count,
    MethodImpl* method_impls,
    int method_count
);

// Simplified implementation (no generics)
void trait_impl_simple(
    Trait* trait,
    TypeInfo* impl_type,
    MethodImpl* method_impls,
    int method_count
);

// Convenience function for binary operator traits
// For Add<Rhs, Output>: impl_binary(Trait_Add, int, double, double, impl)
void trait_impl_binary(
    Trait* trait,
    TypeInfo* left_type,
    TypeInfo* rhs_type,
    TypeInfo* output_type,
    MethodImpl* method_impl
);

// Convenience function for unary operator traits
void trait_impl_unary(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo* output_type,
    MethodImpl* method_impl
);

// === Trait Query Functions ===

// Find trait implementation for a type with specific type parameter bindings
TraitImpl* trait_find_impl(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count
);

// Get associated type from a trait implementation
TypeInfo* trait_get_assoc_type(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count,
    const char* assoc_type_name
);

// Get method implementation
MethodImpl* trait_get_method(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count,
    const char* method_name
);

// === Convenience Functions for Binary Operations ===

// Get output type for binary operation (e.g., int + double -> double)
TypeInfo* trait_get_binary_output(
    Trait* trait,
    TypeInfo* left_type,
    TypeInfo* right_type
);

// Get method for binary operation
MethodImpl* trait_get_binary_method(
    Trait* trait,
    TypeInfo* left_type,
    TypeInfo* right_type,
    const char* method_name
);

// === Convenience Functions for Unary Operations ===

// Get output type for unary operation
TypeInfo* trait_get_unary_output(
    Trait* trait,
    TypeInfo* operand_type
);

// Get method for unary operation
MethodImpl* trait_get_unary_method(
    Trait* trait,
    TypeInfo* operand_type,
    const char* method_name
);

// === Built-in Trait Initialization ===

// Initialize all built-in traits (Add, Sub, Mul, etc.)
void traits_init_builtins(TraitRegistry* registry);

// Register all built-in type implementations
void traits_register_builtin_impls(TraitRegistry* registry);

// === On-Demand Trait Implementation for Builtins ===

// Ensure Index<Idx> is implemented for builtin indexable types (arrays, strings, etc.)
// Implements on-demand for generic builtin types that can't be registered upfront
// For arrays: implements Index<i32> -> ElementType
// User-defined types must implement Index explicitly
void trait_ensure_index_impl(TypeInfo* type);

// Ensure RefIndex<Idx> is implemented for builtin indexable types (arrays, strings, etc.)
void trait_ensure_ref_index_impl(TypeInfo* type);

// Ensure Length is implemented for builtin types with length (arrays, strings, etc.)
void trait_ensure_length_impl(TypeInfo* type);

// === Auto-implement Eq trait for enum types ===

// Register Eq trait implementation for an enum type
// This is called automatically when an enum is declared
void trait_register_eq_for_enum(TypeInfo* enum_type, TraitRegistry* registry);
void trait_register_display_for_enum(TypeInfo* enum_type, TraitRegistry* registry);

#endif // JSASTA_TRAITS_H
