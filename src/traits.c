#include "traits.h"
#include "jsasta_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global trait references
Trait* Trait_Add = NULL;
Trait* Trait_Sub = NULL;
Trait* Trait_Mul = NULL;
Trait* Trait_Div = NULL;
Trait* Trait_Rem = NULL;
Trait* Trait_BitAnd = NULL;
Trait* Trait_BitOr = NULL;
Trait* Trait_BitXor = NULL;
Trait* Trait_Shl = NULL;
Trait* Trait_Shr = NULL;
Trait* Trait_Eq = NULL;
Trait* Trait_Ord = NULL;
Trait* Trait_Not = NULL;
Trait* Trait_Neg = NULL;
Trait* Trait_AddAssign = NULL;
Trait* Trait_SubAssign = NULL;
Trait* Trait_MulAssign = NULL;
Trait* Trait_DivAssign = NULL;
Trait* Trait_Index = NULL;
Trait* Trait_RefIndex = NULL;
Trait* Trait_Length = NULL;

// === Core Trait Registry Functions ===

TraitRegistry* trait_registry_create(void) {
    TraitRegistry* registry = malloc(sizeof(TraitRegistry));
    registry->first_trait = NULL;
    registry->trait_count = 0;
    return registry;
}

void trait_registry_destroy(TraitRegistry* registry) {
    if (!registry) return;

    Trait* trait = registry->first_trait;
    while (trait) {
        Trait* next_trait = trait->next;

        // Free implementations
        TraitImpl* impl = trait->first_impl;
        while (impl) {
            TraitImpl* next_impl = impl->next;

            free(impl->type_param_bindings);
            free(impl->assoc_type_bindings);
            free(impl->methods);
            free(impl);

            impl = next_impl;
        }

        // Free trait data
        free(trait->type_params);
        free(trait->assoc_types);
        free(trait->method_names);
        free(trait->method_signatures);
        free(trait);

        trait = next_trait;
    }

    free(registry);
}

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
) {
    Trait* trait = malloc(sizeof(Trait));
    trait->name = name;

    // Copy type parameters
    if (type_param_count > 0) {
        trait->type_params = malloc(sizeof(TraitTypeParam) * type_param_count);
        memcpy(trait->type_params, type_params, sizeof(TraitTypeParam) * type_param_count);
    } else {
        trait->type_params = NULL;
    }
    trait->type_param_count = type_param_count;

    // Copy associated types
    if (assoc_type_count > 0) {
        trait->assoc_types = malloc(sizeof(TraitAssocType) * assoc_type_count);
        memcpy(trait->assoc_types, assoc_types, sizeof(TraitAssocType) * assoc_type_count);
    } else {
        trait->assoc_types = NULL;
    }
    trait->assoc_type_count = assoc_type_count;

    // Copy method signatures
    if (method_count > 0) {
        trait->method_names = malloc(sizeof(char*) * method_count);
        memcpy(trait->method_names, method_names, sizeof(char*) * method_count);

        trait->method_signatures = malloc(sizeof(TypeInfo*) * method_count);
        memcpy(trait->method_signatures, method_signatures, sizeof(TypeInfo*) * method_count);
    } else {
        trait->method_names = NULL;
        trait->method_signatures = NULL;
    }
    trait->method_count = method_count;

    trait->first_impl = NULL;

    // Add to registry
    trait->next = registry->first_trait;
    registry->first_trait = trait;
    registry->trait_count++;

    return trait;
}

Trait* trait_define_simple(
    TraitRegistry* registry,
    const char* name,
    const char** method_names,
    TypeInfo** method_signatures,
    int method_count
) {
    return trait_define_full(registry, name, NULL, 0, NULL, 0,
                            method_names, method_signatures, method_count);
}

Trait* trait_find(TraitRegistry* registry, const char* name) {
    for (Trait* t = registry->first_trait; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }
    return NULL;
}

// === Trait Implementation Functions ===

void trait_impl_full(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count,
    TypeInfo** assoc_type_bindings,
    int assoc_type_count,
    MethodImpl* method_impls,
    int method_count
) {
    TraitImpl* impl = malloc(sizeof(TraitImpl));
    impl->trait = trait;
    impl->impl_type = impl_type;

    // Copy type parameter bindings
    if (type_param_count > 0) {
        impl->type_param_bindings = malloc(sizeof(TypeInfo*) * type_param_count);
        memcpy(impl->type_param_bindings, type_param_bindings,
               sizeof(TypeInfo*) * type_param_count);
    } else {
        impl->type_param_bindings = NULL;
    }
    impl->type_param_count = type_param_count;

    // Copy associated type bindings
    if (assoc_type_count > 0) {
        impl->assoc_type_bindings = malloc(sizeof(TypeInfo*) * assoc_type_count);
        memcpy(impl->assoc_type_bindings, assoc_type_bindings,
               sizeof(TypeInfo*) * assoc_type_count);
    } else {
        impl->assoc_type_bindings = NULL;
    }
    impl->assoc_type_count = assoc_type_count;

    // Copy method implementations
    if (method_count > 0) {
        impl->methods = malloc(sizeof(MethodImpl) * method_count);
        memcpy(impl->methods, method_impls, sizeof(MethodImpl) * method_count);
    } else {
        impl->methods = NULL;
    }
    impl->method_count = method_count;

    // Add to trait's implementation list
    impl->next = trait->first_impl;
    trait->first_impl = impl;
}

void trait_impl_simple(
    Trait* trait,
    TypeInfo* impl_type,
    MethodImpl* method_impls,
    int method_count
) {
    trait_impl_full(trait, impl_type, NULL, 0, NULL, 0, method_impls, method_count);
}

void trait_impl_binary(
    Trait* trait,
    TypeInfo* left_type,
    TypeInfo* rhs_type,
    TypeInfo* output_type,
    MethodImpl* method_impl
) {
    TypeInfo* type_param_bindings[] = { rhs_type };
    TypeInfo* assoc_type_bindings[] = { output_type };

    trait_impl_full(trait, left_type,
                   type_param_bindings, 1,
                   assoc_type_bindings, 1,
                   method_impl, 1);
}

void trait_impl_unary(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo* output_type,
    MethodImpl* method_impl
) {
    TypeInfo* assoc_type_bindings[] = { output_type };

    trait_impl_full(trait, impl_type,
                   NULL, 0,
                   assoc_type_bindings, 1,
                   method_impl, 1);
}

// === Trait Query Functions ===

// Helper: Compare type parameter bindings
static int type_params_match(
    TypeInfo** bindings1, int count1,
    TypeInfo** bindings2, int count2
) {
    if (count1 != count2) return 0;

    for (int i = 0; i < count1; i++) {
        // Resolve aliases before comparing
        TypeInfo* resolved1 = type_info_resolve_alias(bindings1[i]);
        TypeInfo* resolved2 = type_info_resolve_alias(bindings2[i]);
        if (resolved1 != resolved2) {
            return 0;
        }
    }

    return 1;
}

TraitImpl* trait_find_impl(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count
) {
    // Resolve aliases to get the actual type
    impl_type = type_info_resolve_alias(impl_type);

    for (TraitImpl* impl = trait->first_impl; impl; impl = impl->next) {
        TypeInfo* resolved_impl_type = type_info_resolve_alias(impl->impl_type);
        if (resolved_impl_type == impl_type &&
            type_params_match(impl->type_param_bindings, impl->type_param_count,
                            type_param_bindings, type_param_count)) {
            return impl;
        }
    }
    return NULL;
}

TypeInfo* trait_get_assoc_type(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count,
    const char* assoc_type_name
) {
    TraitImpl* impl = trait_find_impl(trait, impl_type, type_param_bindings, type_param_count);
    if (!impl) return NULL;

    // Find the associated type index
    for (int i = 0; i < trait->assoc_type_count; i++) {
        if (strcmp(trait->assoc_types[i].name, assoc_type_name) == 0) {
            if (i < impl->assoc_type_count) {
                return impl->assoc_type_bindings[i];
            }
            return NULL;
        }
    }

    return NULL;
}

MethodImpl* trait_get_method(
    Trait* trait,
    TypeInfo* impl_type,
    TypeInfo** type_param_bindings,
    int type_param_count,
    const char* method_name
) {
    TraitImpl* impl = trait_find_impl(trait, impl_type, type_param_bindings, type_param_count);
    if (!impl) return NULL;

    // Find the method index
    for (int i = 0; i < trait->method_count; i++) {
        if (strcmp(trait->method_names[i], method_name) == 0) {
            if (i < impl->method_count) {
                return &impl->methods[i];
            }
            return NULL;
        }
    }

    return NULL;
}

// === Convenience Functions for Binary Operations ===

TypeInfo* trait_get_binary_output(
    Trait* trait,
    TypeInfo* left_type,
    TypeInfo* right_type
) {
    TypeInfo* type_param_bindings[] = { right_type };
    return trait_get_assoc_type(trait, left_type, type_param_bindings, 1, "Output");
}

MethodImpl* trait_get_binary_method(
    Trait* trait,
    TypeInfo* left_type,
    TypeInfo* right_type,
    const char* method_name
) {
    TypeInfo* type_param_bindings[] = { right_type };
    return trait_get_method(trait, left_type, type_param_bindings, 1, method_name);
}

// === Convenience Functions for Unary Operations ===

TypeInfo* trait_get_unary_output(
    Trait* trait,
    TypeInfo* operand_type
) {
    return trait_get_assoc_type(trait, operand_type, NULL, 0, "Output");
}

MethodImpl* trait_get_unary_method(
    Trait* trait,
    TypeInfo* operand_type,
    const char* method_name
) {
    return trait_get_method(trait, operand_type, NULL, 0, method_name);
}

// === Built-in Trait Initialization ===

void traits_init_builtins(TraitRegistry* registry, TypeContext* type_ctx) {
		(void) type_ctx;
    // Add<Rhs> { type Output; fn add(self, rhs: Rhs) -> Output }
    TraitTypeParam add_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType add_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* add_method_names[] = { "add" };
    TypeInfo* add_method_sigs[] = { NULL }; // Placeholder

    Trait_Add = trait_define_full(registry, "Add",
                                  add_type_params, 1,
                                  add_assoc_types, 1,
                                  add_method_names, add_method_sigs, 1);

    // Sub<Rhs> { type Output; fn sub(self, rhs: Rhs) -> Output }
    TraitTypeParam sub_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType sub_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* sub_method_names[] = { "sub" };
    TypeInfo* sub_method_sigs[] = { NULL };

    Trait_Sub = trait_define_full(registry, "Sub",
                                  sub_type_params, 1,
                                  sub_assoc_types, 1,
                                  sub_method_names, sub_method_sigs, 1);

    // Mul<Rhs> { type Output; fn mul(self, rhs: Rhs) -> Output }
    TraitTypeParam mul_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType mul_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* mul_method_names[] = { "mul" };
    TypeInfo* mul_method_sigs[] = { NULL };

    Trait_Mul = trait_define_full(registry, "Mul",
                                  mul_type_params, 1,
                                  mul_assoc_types, 1,
                                  mul_method_names, mul_method_sigs, 1);

    // Div<Rhs> { type Output; fn div(self, rhs: Rhs) -> Output }
    TraitTypeParam div_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType div_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* div_method_names[] = { "div" };
    TypeInfo* div_method_sigs[] = { NULL };

    Trait_Div = trait_define_full(registry, "Div",
                                  div_type_params, 1,
                                  div_assoc_types, 1,
                                  div_method_names, div_method_sigs, 1);

    // Rem<Rhs> { type Output; fn rem(self, rhs: Rhs) -> Output }
    TraitTypeParam rem_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType rem_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* rem_method_names[] = { "rem" };
    TypeInfo* rem_method_sigs[] = { NULL };

    Trait_Rem = trait_define_full(registry, "Rem",
                                  rem_type_params, 1,
                                  rem_assoc_types, 1,
                                  rem_method_names, rem_method_sigs, 1);

    // BitAnd<Rhs> { type Output; fn bitand(self, rhs: Rhs) -> Output }
    TraitTypeParam bitand_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType bitand_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* bitand_method_names[] = { "bitand" };
    TypeInfo* bitand_method_sigs[] = { NULL };

    Trait_BitAnd = trait_define_full(registry, "BitAnd",
                                     bitand_type_params, 1,
                                     bitand_assoc_types, 1,
                                     bitand_method_names, bitand_method_sigs, 1);

    // BitOr<Rhs> { type Output; fn bitor(self, rhs: Rhs) -> Output }
    TraitTypeParam bitor_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType bitor_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* bitor_method_names[] = { "bitor" };
    TypeInfo* bitor_method_sigs[] = { NULL };

    Trait_BitOr = trait_define_full(registry, "BitOr",
                                    bitor_type_params, 1,
                                    bitor_assoc_types, 1,
                                    bitor_method_names, bitor_method_sigs, 1);

    // BitXor<Rhs> { type Output; fn bitxor(self, rhs: Rhs) -> Output }
    TraitTypeParam bitxor_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType bitxor_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* bitxor_method_names[] = { "bitxor" };
    TypeInfo* bitxor_method_sigs[] = { NULL };

    Trait_BitXor = trait_define_full(registry, "BitXor",
                                     bitxor_type_params, 1,
                                     bitxor_assoc_types, 1,
                                     bitxor_method_names, bitxor_method_sigs, 1);

    // Shl<Rhs> { type Output; fn shl(self, rhs: Rhs) -> Output }
    TraitTypeParam shl_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType shl_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* shl_method_names[] = { "shl" };
    TypeInfo* shl_method_sigs[] = { NULL };

    Trait_Shl = trait_define_full(registry, "Shl",
                                  shl_type_params, 1,
                                  shl_assoc_types, 1,
                                  shl_method_names, shl_method_sigs, 1);

    // Shr<Rhs> { type Output; fn shr(self, rhs: Rhs) -> Output }
    TraitTypeParam shr_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType shr_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* shr_method_names[] = { "shr" };
    TypeInfo* shr_method_sigs[] = { NULL };

    Trait_Shr = trait_define_full(registry, "Shr",
                                  shr_type_params, 1,
                                  shr_assoc_types, 1,
                                  shr_method_names, shr_method_sigs, 1);

    // Eq<Rhs> { type Output; fn eq(self, rhs: Rhs) -> Output; fn ne(self, rhs: Rhs) -> Output }
    TraitTypeParam eq_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType eq_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* eq_method_names[] = { "eq", "ne" };
    TypeInfo* eq_method_sigs[] = { NULL, NULL };

    Trait_Eq = trait_define_full(registry, "Eq",
                                 eq_type_params, 1,
                                 eq_assoc_types, 1,
                                 eq_method_names, eq_method_sigs, 2);

    // Ord<Rhs> { type Output; fn lt(self, rhs: Rhs) -> Output; fn le(self, rhs: Rhs) -> Output;
    //            fn gt(self, rhs: Rhs) -> Output; fn ge(self, rhs: Rhs) -> Output }
    TraitTypeParam ord_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType ord_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* ord_method_names[] = { "lt", "le", "gt", "ge" };
    TypeInfo* ord_method_sigs[] = { NULL, NULL, NULL, NULL };

    Trait_Ord = trait_define_full(registry, "Ord",
                                  ord_type_params, 1,
                                  ord_assoc_types, 1,
                                  ord_method_names, ord_method_sigs, 4);

    // Not { type Output; fn not(self) -> Output }
    TraitAssocType not_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* not_method_names[] = { "not" };
    TypeInfo* not_method_sigs[] = { NULL };

    Trait_Not = trait_define_full(registry, "Not",
                                  NULL, 0,
                                  not_assoc_types, 1,
                                  not_method_names, not_method_sigs, 1);

    // Neg { type Output; fn neg(self) -> Output }
    TraitAssocType neg_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* neg_method_names[] = { "neg" };
    TypeInfo* neg_method_sigs[] = { NULL };

    Trait_Neg = trait_define_full(registry, "Neg",
                                  NULL, 0,
                                  neg_assoc_types, 1,
                                  neg_method_names, neg_method_sigs, 1);

    // AddAssign<Rhs> { fn add_assign(&mut self, rhs: Rhs) }
    TraitTypeParam add_assign_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    const char* add_assign_method_names[] = { "add_assign" };
    TypeInfo* add_assign_method_sigs[] = { NULL };

    Trait_AddAssign = trait_define_full(registry, "AddAssign",
                                        add_assign_type_params, 1,
                                        NULL, 0,
                                        add_assign_method_names, add_assign_method_sigs, 1);

    // SubAssign<Rhs> { fn sub_assign(&mut self, rhs: Rhs) }
    TraitTypeParam sub_assign_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    const char* sub_assign_method_names[] = { "sub_assign" };
    TypeInfo* sub_assign_method_sigs[] = { NULL };

    Trait_SubAssign = trait_define_full(registry, "SubAssign",
                                        sub_assign_type_params, 1,
                                        NULL, 0,
                                        sub_assign_method_names, sub_assign_method_sigs, 1);

    // MulAssign<Rhs> { fn mul_assign(&mut self, rhs: Rhs) }
    TraitTypeParam mul_assign_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    const char* mul_assign_method_names[] = { "mul_assign" };
    TypeInfo* mul_assign_method_sigs[] = { NULL };

    Trait_MulAssign = trait_define_full(registry, "MulAssign",
                                        mul_assign_type_params, 1,
                                        NULL, 0,
                                        mul_assign_method_names, mul_assign_method_sigs, 1);

    // DivAssign<Rhs> { fn div_assign(&mut self, rhs: Rhs) }
    TraitTypeParam div_assign_type_params[] = {
        { .name = "Rhs", .default_type = NULL, .constraint = NULL }
    };
    const char* div_assign_method_names[] = { "div_assign" };
    TypeInfo* div_assign_method_sigs[] = { NULL };

    Trait_DivAssign = trait_define_full(registry, "DivAssign",
                                        div_assign_type_params, 1,
                                        NULL, 0,
                                        div_assign_method_names, div_assign_method_sigs, 1);

    // Index<Idx> { type Output; fn index(self, idx: Idx) -> Output }
    TraitTypeParam index_type_params[] = {
        { .name = "Idx", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType index_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* index_method_names[] = { "index" };
    TypeInfo* index_method_sigs[] = { NULL };

    Trait_Index = trait_define_full(registry, "Index",
                                    index_type_params, 1,
                                    index_assoc_types, 1,
                                    index_method_names, index_method_sigs, 1);

    // RefIndex<Idx> { type Output; fn ref_index(self, idx: Idx) -> ref<Output> }
    // Used for mutable indexing (assignment)
    TraitTypeParam ref_index_type_params[] = {
        { .name = "Idx", .default_type = NULL, .constraint = NULL }
    };
    TraitAssocType ref_index_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* ref_index_method_names[] = { "ref_index" };
    TypeInfo* ref_index_method_sigs[] = { NULL };

    Trait_RefIndex = trait_define_full(registry, "RefIndex",
                                       ref_index_type_params, 1,
                                       ref_index_assoc_types, 1,
                                       ref_index_method_names, ref_index_method_sigs, 1);

    // Length { type Output; fn length(self) -> Output }
    // Returns the length of a collection (arrays, strings, etc.)
    // For arrays: Output = u32
    TraitAssocType length_assoc_types[] = {
        { .name = "Output", .constraint = NULL }
    };
    const char* length_method_names[] = { "length" };
    TypeInfo* length_method_sigs[] = { NULL };

    Trait_Length = trait_define_full(registry, "Length",
                                     NULL, 0,  // No type parameters
                                     length_assoc_types, 1,
                                     length_method_names, length_method_sigs, 1);
}

// === Array Index Intrinsic ===

// Intrinsic codegen for array indexing: array[index] -> element
// This intrinsic is special - it's not called directly with args
// Instead, codegen handles it inline because it needs AST node context
// to determine if the object is an identifier (for symbol lookup)
// and whether it's a stack vs heap array
//
// For future intrinsics that can be fully self-contained, they should
// be callable with just args[] and return a value.
static LLVMValueRef intrinsic_array_index(CodeGen* gen, LLVMValueRef* args, int arg_count) {
    (void)gen;
    (void)args;
    (void)arg_count;

    // Not called - codegen handles array indexing inline
    // This exists as a marker that array indexing is intrinsic
    return NULL;
}

// === On-Demand Trait Implementation for Builtins ===

void trait_ensure_index_impl(TypeInfo* type, TypeContext* type_ctx) {
    if (!type || !Trait_Index) {
        return; // Nothing to do
    }

    // For arrays: implement Index<i32> -> ElementType
    if (type->kind == TYPE_KIND_ARRAY) {
        TypeInfo* idx_type = type_ctx->int_type;
        TypeInfo* type_param_bindings[] = { idx_type };

        // Check if already implemented
        TraitImpl* existing = trait_find_impl(Trait_Index, type, type_param_bindings, 1);
        if (existing) {
            return;
        }

        // Get element type as the output
        TypeInfo* elem_type = type->data.array.element_type;

        // Create method implementation
        MethodImpl method_impl = {
            .method_name = "index",
            .signature = NULL,
            .kind = METHOD_INTRINSIC,
            .codegen = intrinsic_array_index,
            .function_ptr = NULL,
            .external_name = NULL
        };

        // Implement Index<i32> for this array type with Output = ElementType
        TypeInfo* assoc_type_bindings[] = { elem_type };

        trait_impl_full(Trait_Index, type,
                       type_param_bindings, 1,
                       assoc_type_bindings, 1,
                       &method_impl, 1);
    }

    // For strings: implement Index<i32> -> u8
    if (type == Type_String) {
        TypeInfo* idx_type = type_ctx->int_type;
        TypeInfo* type_param_bindings[] = { idx_type };

        // Check if already implemented
        TraitImpl* existing = trait_find_impl(Trait_Index, type, type_param_bindings, 1);
        if (existing) {
            return;
        }

        // String indexing returns u8 (byte)
        TypeInfo* output_type = type_ctx->u8_type;

        // Create method implementation
        MethodImpl method_impl = {
            .method_name = "index",
            .signature = NULL,
            .kind = METHOD_INTRINSIC,
            .codegen = intrinsic_array_index,  // We'll reuse the same placeholder
            .function_ptr = NULL,
            .external_name = NULL
        };

        // Implement Index<i32> for string with Output = u8
        TypeInfo* assoc_type_bindings[] = { output_type };

        trait_impl_full(Trait_Index, type,
                       type_param_bindings, 1,
                       assoc_type_bindings, 1,
                       &method_impl, 1);
    }

    // TODO: For other builtin types that support indexing
}

// Intrinsic for array ref_index - returns a reference to the element
// This is a placeholder that is never actually called - the codegen for
// AST_INDEX_ASSIGNMENT handles the implementation inline because it needs
// access to the AST node structure and symbol table information
static LLVMValueRef intrinsic_array_ref_index(CodeGen* gen, LLVMValueRef* args, int arg_count) {
    (void)gen;
    (void)args;
    (void)arg_count;

    // Not called - codegen handles array index assignment inline
    return NULL;
}

void trait_ensure_ref_index_impl(TypeInfo* type, TypeContext* type_ctx) {
    if (!type || !Trait_RefIndex) {
        return; // Nothing to do
    }

    // For arrays: implement RefIndex<i32> -> ref<ElementType>
    if (type->kind == TYPE_KIND_ARRAY) {
        TypeInfo* idx_type = type_ctx->int_type;
        TypeInfo* type_param_bindings[] = { idx_type };

        // Check if already implemented
        TraitImpl* existing = trait_find_impl(Trait_RefIndex, type, type_param_bindings, 1);
        if (existing) {
            return;
        }

        // Get element type as the output
        TypeInfo* elem_type = type->data.array.element_type;

        // Create method implementation
        MethodImpl method_impl = {
            .method_name = "ref_index",
            .signature = NULL,
            .kind = METHOD_INTRINSIC,
            .codegen = intrinsic_array_ref_index,
            .function_ptr = NULL,
            .external_name = NULL
        };

        // Implement RefIndex<i32> for this array type with Output = ElementType
        // Note: The actual return type is ref<ElementType>, but we store ElementType
        // in the associated type and wrap it with ref when needed
        TypeInfo* assoc_type_bindings[] = { elem_type };

        trait_impl_full(Trait_RefIndex, type,
                       type_param_bindings, 1,
                       assoc_type_bindings, 1,
                       &method_impl, 1);
    }

    // TODO: For strings: implement RefIndex<i32> -> ref<u8>
    // TODO: For other builtin types that support indexing
}

// Intrinsic for array length - returns the length of the array
// This is a placeholder that is never actually called - the codegen for
// member access to "length" handles the implementation inline
static LLVMValueRef intrinsic_array_length(CodeGen* gen, LLVMValueRef* args, int arg_count) {
    (void)gen;
    (void)args;
    (void)arg_count;

    // Not called - codegen handles array length inline
    return NULL;
}

void trait_ensure_length_impl(TypeInfo* type, TypeContext* type_ctx) {
    if (!type || !Trait_Length) {
        return; // Nothing to do
    }

    // For arrays: implement Length with Output = u32
    if (type->kind == TYPE_KIND_ARRAY) {
        // Check if already implemented
        TraitImpl* existing = trait_find_impl(Trait_Length, type, NULL, 0);
        if (existing) {
            return;
        }

        // Create method implementation
        MethodImpl method_impl = {
            .method_name = "length",
            .signature = NULL,
            .kind = METHOD_INTRINSIC,
            .codegen = intrinsic_array_length,
            .function_ptr = NULL,
            .external_name = NULL
        };

        // Implement Length for this array type with Output = u32
        TypeInfo* u32_type = type_ctx->u32_type;
        TypeInfo* assoc_type_bindings[] = { u32_type };

        trait_impl_full(Trait_Length, type,
                       NULL, 0,  // No type parameters
                       assoc_type_bindings, 1,
                       &method_impl, 1);
    }

    // TODO: For strings: implement Length with Output = u32
    // TODO: For other builtin types with length
}
