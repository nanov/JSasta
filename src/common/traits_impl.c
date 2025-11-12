#include "traits.h"
#include "jsasta_compiler.h"
#include <llvm-c/Core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// === Helper Functions for Type Conversion ===

// Promote two integer values to a common type
static void promote_int_operands(CodeGen* gen, LLVMValueRef* left, LLVMValueRef* right) {
    LLVMTypeRef left_type = LLVMTypeOf(*left);
    LLVMTypeRef right_type = LLVMTypeOf(*right);

    // If types are the same, no conversion needed
    if (left_type == right_type) {
        return;
    }

    unsigned left_width = LLVMGetIntTypeWidth(left_type);
    unsigned right_width = LLVMGetIntTypeWidth(right_type);

    // Promote to the larger width
    if (left_width < right_width) {
        // Extend left to match right (use sign extend for now, could be improved)
        *left = LLVMBuildSExtOrBitCast(gen->builder, *left, right_type, "promote");
    } else if (right_width < left_width) {
        // Extend right to match left
        *right = LLVMBuildSExtOrBitCast(gen->builder, *right, left_type, "promote");
    }
}

// === Intrinsic Codegen Functions for int Operations ===
// Using macros to reduce duplication while keeping debuggability

#define DEFINE_INT_BINARY_INTRINSIC(name, llvm_build_fn, result_name) \
    static LLVMValueRef intrinsic_int_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)arg_count; \
        (void)context; \
        promote_int_operands(gen, &args[0], &args[1]); \
        return llvm_build_fn(gen->builder, args[0], args[1], result_name); \
    }

#define DEFINE_INT_CMP_INTRINSIC(name, cmp_op, result_name) \
    static LLVMValueRef intrinsic_int_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)arg_count; \
        (void)context; \
        promote_int_operands(gen, &args[0], &args[1]); \
        return LLVMBuildICmp(gen->builder, cmp_op, args[0], args[1], result_name); \
    }

// Arithmetic operations
DEFINE_INT_BINARY_INTRINSIC(add, LLVMBuildAdd, "add")
DEFINE_INT_BINARY_INTRINSIC(sub, LLVMBuildSub, "sub")
DEFINE_INT_BINARY_INTRINSIC(mul, LLVMBuildMul, "mul")
DEFINE_INT_BINARY_INTRINSIC(div, LLVMBuildSDiv, "div")
DEFINE_INT_BINARY_INTRINSIC(rem, LLVMBuildSRem, "rem")

// Bitwise operations
DEFINE_INT_BINARY_INTRINSIC(bitand, LLVMBuildAnd, "and")
DEFINE_INT_BINARY_INTRINSIC(bitor, LLVMBuildOr, "or")
DEFINE_INT_BINARY_INTRINSIC(bitxor, LLVMBuildXor, "xor")

// Shift operations
DEFINE_INT_BINARY_INTRINSIC(shl, LLVMBuildShl, "shl")
DEFINE_INT_BINARY_INTRINSIC(shr, LLVMBuildAShr, "shr")

// Comparison operations
DEFINE_INT_CMP_INTRINSIC(eq, LLVMIntEQ, "eq")
DEFINE_INT_CMP_INTRINSIC(ne, LLVMIntNE, "ne")
DEFINE_INT_CMP_INTRINSIC(lt, LLVMIntSLT, "lt")
DEFINE_INT_CMP_INTRINSIC(le, LLVMIntSLE, "le")
DEFINE_INT_CMP_INTRINSIC(gt, LLVMIntSGT, "gt")
DEFINE_INT_CMP_INTRINSIC(ge, LLVMIntSGE, "ge")

// Unary operations
static LLVMValueRef intrinsic_int_neg(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)arg_count;
    (void)context;
    return LLVMBuildNeg(gen->builder, args[0], "neg");
}

// === Intrinsic Codegen Functions for AddAssign and SubAssign ===
// These perform the operation and return the new value to be stored

#define DEFINE_INT_ASSIGN_INTRINSIC(name, llvm_build_fn, result_name) \
    static LLVMValueRef intrinsic_int_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)arg_count; \
        (void)context; \
        promote_int_operands(gen, &args[0], &args[1]); \
        return llvm_build_fn(gen->builder, args[0], args[1], result_name); \
    }

DEFINE_INT_ASSIGN_INTRINSIC(add_assign, LLVMBuildAdd, "add_assign")
DEFINE_INT_ASSIGN_INTRINSIC(sub_assign, LLVMBuildSub, "sub_assign")
DEFINE_INT_ASSIGN_INTRINSIC(mul_assign, LLVMBuildMul, "mul_assign")

// Division needs signed/unsigned variants
static LLVMValueRef intrinsic_int_div_assign(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    promote_int_operands(gen, &args[0], &args[1]);
    return LLVMBuildSDiv(gen->builder, args[0], args[1], "div_assign");
}

static LLVMValueRef intrinsic_uint_div_assign(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    promote_int_operands(gen, &args[0], &args[1]);
    return LLVMBuildUDiv(gen->builder, args[0], args[1], "div_assign");
}

#define DEFINE_DOUBLE_ASSIGN_INTRINSIC(name, llvm_build_fn, result_name) \
    static LLVMValueRef intrinsic_double_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        return llvm_build_fn(gen->builder, args[0], args[1], result_name); \
    }

DEFINE_DOUBLE_ASSIGN_INTRINSIC(add_assign, LLVMBuildFAdd, "add_assign")
DEFINE_DOUBLE_ASSIGN_INTRINSIC(sub_assign, LLVMBuildFSub, "sub_assign")
DEFINE_DOUBLE_ASSIGN_INTRINSIC(mul_assign, LLVMBuildFMul, "mul_assign")
DEFINE_DOUBLE_ASSIGN_INTRINSIC(div_assign, LLVMBuildFDiv, "div_assign")

// === Intrinsic Codegen Functions for int + double (with promotion) ===

static LLVMValueRef intrinsic_int_double_add(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFAdd(gen->builder, left_promoted, args[1], "fadd");
}

static LLVMValueRef intrinsic_int_double_sub(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFSub(gen->builder, left_promoted, args[1], "fsub");
}

static LLVMValueRef intrinsic_int_double_mul(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFMul(gen->builder, left_promoted, args[1], "fmul");
}

static LLVMValueRef intrinsic_int_double_div(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFDiv(gen->builder, left_promoted, args[1], "fdiv");
}

static LLVMValueRef intrinsic_int_double_eq(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOEQ, left_promoted, args[1], "feq");
}

static LLVMValueRef intrinsic_int_double_ne(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealONE, left_promoted, args[1], "fne");
}

static LLVMValueRef intrinsic_int_double_lt(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOLT, left_promoted, args[1], "flt");
}

static LLVMValueRef intrinsic_int_double_le(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOLE, left_promoted, args[1], "fle");
}

static LLVMValueRef intrinsic_int_double_gt(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOGT, left_promoted, args[1], "fgt");
}

static LLVMValueRef intrinsic_int_double_ge(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef left_promoted = LLVMBuildSIToFP(gen->builder, args[0],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOGE, left_promoted, args[1], "fge");
}

// === Double Intrinsics ===
#define DEFINE_DOUBLE_BINARY_INTRINSIC(name, llvm_build_fn, result_name) \
    static LLVMValueRef intrinsic_double_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        return llvm_build_fn(gen->builder, args[0], args[1], result_name); \
    }

#define DEFINE_DOUBLE_CMP_INTRINSIC(name, cmp_op, result_name) \
    static LLVMValueRef intrinsic_double_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        return LLVMBuildFCmp(gen->builder, cmp_op, args[0], args[1], result_name); \
    }

// Arithmetic operations
DEFINE_DOUBLE_BINARY_INTRINSIC(add, LLVMBuildFAdd, "fadd")
DEFINE_DOUBLE_BINARY_INTRINSIC(sub, LLVMBuildFSub, "fsub")
DEFINE_DOUBLE_BINARY_INTRINSIC(mul, LLVMBuildFMul, "fmul")
DEFINE_DOUBLE_BINARY_INTRINSIC(div, LLVMBuildFDiv, "fdiv")

// Comparison operations
DEFINE_DOUBLE_CMP_INTRINSIC(eq, LLVMRealOEQ, "feq")
DEFINE_DOUBLE_CMP_INTRINSIC(ne, LLVMRealONE, "fne")
DEFINE_DOUBLE_CMP_INTRINSIC(lt, LLVMRealOLT, "flt")
DEFINE_DOUBLE_CMP_INTRINSIC(le, LLVMRealOLE, "fle")
DEFINE_DOUBLE_CMP_INTRINSIC(gt, LLVMRealOGT, "fgt")
DEFINE_DOUBLE_CMP_INTRINSIC(ge, LLVMRealOGE, "fge")

// Unary operations
static LLVMValueRef intrinsic_double_neg(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    return LLVMBuildFNeg(gen->builder, args[0], "fneg");
}

// === Intrinsic Codegen Functions for double + int (with promotion) ===

static LLVMValueRef intrinsic_double_int_add(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFAdd(gen->builder, args[0], right_promoted, "fadd");
}

static LLVMValueRef intrinsic_double_int_sub(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFSub(gen->builder, args[0], right_promoted, "fsub");
}

static LLVMValueRef intrinsic_double_int_mul(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFMul(gen->builder, args[0], right_promoted, "fmul");
}

static LLVMValueRef intrinsic_double_int_div(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFDiv(gen->builder, args[0], right_promoted, "fdiv");
}

static LLVMValueRef intrinsic_double_int_eq(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOEQ, args[0], right_promoted, "feq");
}

static LLVMValueRef intrinsic_double_int_ne(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealONE, args[0], right_promoted, "fne");
}

static LLVMValueRef intrinsic_double_int_lt(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOLT, args[0], right_promoted, "flt");
}

static LLVMValueRef intrinsic_double_int_le(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOLE, args[0], right_promoted, "fle");
}

static LLVMValueRef intrinsic_double_int_gt(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOGT, args[0], right_promoted, "fgt");
}

static LLVMValueRef intrinsic_double_int_ge(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    LLVMValueRef right_promoted = LLVMBuildSIToFP(gen->builder, args[1],
        LLVMDoubleTypeInContext(gen->context), "itod");
    return LLVMBuildFCmp(gen->builder, LLVMRealOGE, args[0], right_promoted, "fge");
}

// === Intrinsic Codegen Functions for unsigned int Operations ===

// === Unsigned Integer Intrinsics ===
// Unsigned operations that differ from signed (div, rem, shr, comparisons)
#define DEFINE_UINT_BINARY_INTRINSIC(name, llvm_build_fn, result_name) \
    static LLVMValueRef intrinsic_uint_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        promote_int_operands(gen, &args[0], &args[1]); \
        return llvm_build_fn(gen->builder, args[0], args[1], result_name); \
    }

#define DEFINE_UINT_CMP_INTRINSIC(name, cmp_op, result_name) \
    static LLVMValueRef intrinsic_uint_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        promote_int_operands(gen, &args[0], &args[1]); \
        return LLVMBuildICmp(gen->builder, cmp_op, args[0], args[1], result_name); \
    }

DEFINE_UINT_BINARY_INTRINSIC(div, LLVMBuildUDiv, "udiv")
DEFINE_UINT_BINARY_INTRINSIC(rem, LLVMBuildURem, "urem")
DEFINE_UINT_BINARY_INTRINSIC(shr, LLVMBuildLShr, "ushr")
DEFINE_UINT_CMP_INTRINSIC(lt, LLVMIntULT, "ult")
DEFINE_UINT_CMP_INTRINSIC(le, LLVMIntULE, "ule")
DEFINE_UINT_CMP_INTRINSIC(gt, LLVMIntUGT, "ugt")
DEFINE_UINT_CMP_INTRINSIC(ge, LLVMIntUGE, "uge")

// === Bool Intrinsics ===
#define DEFINE_BOOL_BINARY_INTRINSIC(name, llvm_build_fn, result_name) \
    static LLVMValueRef intrinsic_bool_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        return llvm_build_fn(gen->builder, args[0], args[1], result_name); \
    }

#define DEFINE_BOOL_CMP_INTRINSIC(name, cmp_op, result_name) \
    static LLVMValueRef intrinsic_bool_##name(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) { \
        (void)context; \
        (void)arg_count; \
        return LLVMBuildICmp(gen->builder, cmp_op, args[0], args[1], result_name); \
    }

// Bitwise operations (same as logical for bool)
DEFINE_BOOL_BINARY_INTRINSIC(bitand, LLVMBuildAnd, "and")
DEFINE_BOOL_BINARY_INTRINSIC(bitor, LLVMBuildOr, "or")
DEFINE_BOOL_BINARY_INTRINSIC(bitxor, LLVMBuildXor, "xor")

// Comparison operations
DEFINE_BOOL_CMP_INTRINSIC(eq, LLVMIntEQ, "eq")
DEFINE_BOOL_CMP_INTRINSIC(ne, LLVMIntNE, "ne")

// Unary operations
static LLVMValueRef intrinsic_bool_not(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    return LLVMBuildNot(gen->builder, args[0], "not");
}

// === Helper Function for C# Type Promotion Rules ===

// Returns the promoted type for binary operations following C# rules
static TypeInfo* get_promoted_type(TypeInfo* left, TypeInfo* right) {
    // If both types are the same, no promotion needed
    if (left == right) {
        return left;
    }

    // Handle double promotions (any int + double -> double)
    if (type_info_is_double(left)) return left;
    if (type_info_is_double(right)) return right;

    // Both are integers - apply C# integer promotion rules
    if (type_info_is_integer(left) && type_info_is_integer(right)) {
        int left_width = type_info_get_int_width(left);
        int right_width = type_info_get_int_width(right);
        bool left_signed = type_info_is_signed_int(left);
        bool right_signed = type_info_is_signed_int(right);

        // Promote to the larger width
        if (left_width > right_width) return left;
        if (right_width > left_width) return right;

        // Same width: unsigned wins in C#
        if (!left_signed) return left;
        if (!right_signed) return right;

        // Both signed, same width
        return left;
    }

    // Default: return left
    return left;
}

// === Helper Macro for Registering Implementations ===

#define REGISTER_BINARY_OP(trait, left, rhs, output, method_name_str, codegen_fn) \
    do { \
        MethodImpl impl; \
        impl.method_name = method_name_str; \
        impl.signature = NULL; \
        impl.kind = METHOD_INTRINSIC; \
        impl.codegen = codegen_fn; \
        impl.function_ptr = NULL; \
        impl.external_name = NULL; \
        trait_impl_binary(trait, left, rhs, output, &impl); \
    } while(0)

#define REGISTER_UNARY_OP(trait, impl_type, output, method_name_str, codegen_fn) \
    do { \
        MethodImpl impl; \
        impl.method_name = method_name_str; \
        impl.signature = NULL; \
        impl.kind = METHOD_INTRINSIC; \
        impl.codegen = codegen_fn; \
        impl.function_ptr = NULL; \
        impl.external_name = NULL; \
        trait_impl_unary(trait, impl_type, output, &impl); \
    } while(0)

#define REGISTER_COMPARISON_OPS(trait, left, rhs) \
    do { \
        TypeInfo* bool_type = Type_Bool; \
        if (left == Type_I32 && rhs == Type_Double) { \
            MethodImpl impls[4] = { \
                { .method_name = "lt", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_double_lt, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "le", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_double_le, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "gt", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_double_gt, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ge", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_double_ge, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 4); \
        } else if (left == Type_Double && rhs == Type_I32) { \
            MethodImpl impls[4] = { \
                { .method_name = "lt", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_int_lt, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "le", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_int_le, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "gt", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_int_gt, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ge", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_int_ge, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 4); \
        } else if (left == Type_Double && rhs == Type_Double) { \
            MethodImpl impls[4] = { \
                { .method_name = "lt", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_lt, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "le", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_le, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "gt", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_gt, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ge", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_ge, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 4); \
        } \
    } while(0)

#define REGISTER_EQUALITY_OPS(trait, left, rhs) \
    do { \
        TypeInfo* bool_type = Type_Bool; \
        if (left == Type_I32 && rhs == Type_I32) { \
            MethodImpl impls[2] = { \
                { .method_name = "eq", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_eq, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ne", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_ne, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 2); \
        } else if (left == Type_I32 && rhs == Type_Double) { \
            MethodImpl impls[2] = { \
                { .method_name = "eq", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_double_eq, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ne", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_int_double_ne, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 2); \
        } else if (left == Type_Double && rhs == Type_I32) { \
            MethodImpl impls[2] = { \
                { .method_name = "eq", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_int_eq, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ne", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_int_ne, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 2); \
        } else if (left == Type_Double && rhs == Type_Double) { \
            MethodImpl impls[2] = { \
                { .method_name = "eq", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_eq, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ne", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_double_ne, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 2); \
        } else if (left == Type_Bool && rhs == Type_Bool) { \
            MethodImpl impls[2] = { \
                { .method_name = "eq", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_bool_eq, .function_ptr = NULL, .external_name = NULL }, \
                { .method_name = "ne", .signature = NULL, .kind = METHOD_INTRINSIC, \
                  .codegen = intrinsic_bool_ne, .function_ptr = NULL, .external_name = NULL } \
            }; \
            TypeInfo* type_params[] = { rhs }; \
            TypeInfo* assoc_types[] = { bool_type }; \
            trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 2); \
        } \
    } while(0)

// === Macros for Registering Integer Type Operations ===

// Helper to determine which intrinsic to use based on signedness
#define GET_DIV_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_div : intrinsic_uint_div)
#define GET_DIV_ASSIGN_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_div_assign : intrinsic_uint_div_assign)
#define GET_REM_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_rem : intrinsic_uint_rem)
#define GET_SHR_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_shr : intrinsic_uint_shr)
#define GET_LT_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_lt : intrinsic_uint_lt)
#define GET_LE_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_le : intrinsic_uint_le)
#define GET_GT_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_gt : intrinsic_uint_gt)
#define GET_GE_INTRINSIC(type) (type_info_is_signed_int(type) ? intrinsic_int_ge : intrinsic_uint_ge)

// Register arithmetic operations for all integer type combinations
#define REGISTER_INT_ARITHMETIC(trait, method_name_str, add_fn, sub_fn, mul_fn, div_fn, rem_fn) \
    do { \
        TypeInfo* int_types[] = { \
            Type_I8, Type_I16, Type_I32, Type_I64, \
            Type_U8, Type_U16, Type_U32, Type_U64 \
        }; \
        for (int i = 0; i < 8; i++) { \
            for (int j = 0; j < 8; j++) { \
                TypeInfo* left = int_types[i]; \
                TypeInfo* right = int_types[j]; \
                TypeInfo* result = get_promoted_type(left, right); \
                LLVMValueRef (*intrinsic)(CodeGen*, LLVMValueRef*, int, void*); \
                if (strcmp(method_name_str, "add") == 0 || strcmp(method_name_str, "add_assign") == 0) intrinsic = add_fn; \
                else if (strcmp(method_name_str, "sub") == 0 || strcmp(method_name_str, "sub_assign") == 0) intrinsic = sub_fn; \
                else if (strcmp(method_name_str, "mul") == 0 || strcmp(method_name_str, "mul_assign") == 0) intrinsic = mul_fn; \
                else if (strcmp(method_name_str, "div") == 0) intrinsic = GET_DIV_INTRINSIC(result); \
                else if (strcmp(method_name_str, "div_assign") == 0) intrinsic = GET_DIV_ASSIGN_INTRINSIC(result); \
                else if (strcmp(method_name_str, "rem") == 0) intrinsic = GET_REM_INTRINSIC(result); \
                else intrinsic = add_fn; \
                REGISTER_BINARY_OP(trait, left, right, result, method_name_str, intrinsic); \
            } \
        } \
    } while(0)

// Register bitwise operations for all integer types
#define REGISTER_INT_BITWISE_ALL(trait, method_name_str, intrinsic_fn) \
    do { \
        TypeInfo* int_types[] = { \
            Type_I8, Type_I16, Type_I32, Type_I64, \
            Type_U8, Type_U16, Type_U32, Type_U64 \
        }; \
        for (int i = 0; i < 8; i++) { \
            for (int j = 0; j < 8; j++) { \
                TypeInfo* left = int_types[i]; \
                TypeInfo* right = int_types[j]; \
                TypeInfo* result = get_promoted_type(left, right); \
                REGISTER_BINARY_OP(trait, left, right, result, method_name_str, intrinsic_fn); \
            } \
        } \
    } while(0)

// Register shift operations for all integer types
#define REGISTER_INT_SHIFT_ALL(trait, method_name_str, shl_fn, shr_fn) \
    do { \
        TypeInfo* int_types[] = { \
            Type_I8, Type_I16, Type_I32, Type_I64, \
            Type_U8, Type_U16, Type_U32, Type_U64 \
        }; \
        for (int i = 0; i < 8; i++) { \
            for (int j = 0; j < 8; j++) { \
                TypeInfo* left = int_types[i]; \
                TypeInfo* right = int_types[j]; \
                LLVMValueRef (*intrinsic)(CodeGen*, LLVMValueRef*, int, void*); \
                if (strcmp(method_name_str, "shl") == 0) intrinsic = shl_fn; \
                else intrinsic = GET_SHR_INTRINSIC(left); \
                REGISTER_BINARY_OP(trait, left, right, left, method_name_str, intrinsic); \
            } \
        } \
    } while(0)

// Register comparison operations for all integer types
#define REGISTER_INT_COMPARISONS_ALL(trait) \
    do { \
        TypeInfo* int_types[] = { \
            Type_I8, Type_I16, Type_I32, Type_I64, \
            Type_U8, Type_U16, Type_U32, Type_U64 \
        }; \
        for (int i = 0; i < 8; i++) { \
            for (int j = 0; j < 8; j++) { \
                TypeInfo* left = int_types[i]; \
                TypeInfo* right = int_types[j]; \
                TypeInfo* promoted = get_promoted_type(left, right); \
                MethodImpl impls[4]; \
                impls[0].method_name = "lt"; \
                impls[0].signature = NULL; \
                impls[0].kind = METHOD_INTRINSIC; \
                impls[0].codegen = GET_LT_INTRINSIC(promoted); \
                impls[0].function_ptr = NULL; \
                impls[0].external_name = NULL; \
                impls[1].method_name = "le"; \
                impls[1].signature = NULL; \
                impls[1].kind = METHOD_INTRINSIC; \
                impls[1].codegen = GET_LE_INTRINSIC(promoted); \
                impls[1].function_ptr = NULL; \
                impls[1].external_name = NULL; \
                impls[2].method_name = "gt"; \
                impls[2].signature = NULL; \
                impls[2].kind = METHOD_INTRINSIC; \
                impls[2].codegen = GET_GT_INTRINSIC(promoted); \
                impls[2].function_ptr = NULL; \
                impls[2].external_name = NULL; \
                impls[3].method_name = "ge"; \
                impls[3].signature = NULL; \
                impls[3].kind = METHOD_INTRINSIC; \
                impls[3].codegen = GET_GE_INTRINSIC(promoted); \
                impls[3].function_ptr = NULL; \
                impls[3].external_name = NULL; \
                TypeInfo* type_params[] = { right }; \
                TypeInfo* assoc_types[] = { Type_Bool }; \
                trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 4); \
            } \
        } \
    } while(0)

// Register equality operations for all integer types
#define REGISTER_INT_EQUALITY_ALL(trait) \
    do { \
        TypeInfo* int_types[] = { \
            Type_I8, Type_I16, Type_I32, Type_I64, \
            Type_U8, Type_U16, Type_U32, Type_U64 \
        }; \
        for (int i = 0; i < 8; i++) { \
            for (int j = 0; j < 8; j++) { \
                TypeInfo* left = int_types[i]; \
                TypeInfo* right = int_types[j]; \
                MethodImpl impls[2]; \
                impls[0].method_name = "eq"; \
                impls[0].signature = NULL; \
                impls[0].kind = METHOD_INTRINSIC; \
                impls[0].codegen = intrinsic_int_eq; \
                impls[0].function_ptr = NULL; \
                impls[0].external_name = NULL; \
                impls[1].method_name = "ne"; \
                impls[1].signature = NULL; \
                impls[1].kind = METHOD_INTRINSIC; \
                impls[1].codegen = intrinsic_int_ne; \
                impls[1].function_ptr = NULL; \
                impls[1].external_name = NULL; \
                TypeInfo* type_params[] = { right }; \
                TypeInfo* assoc_types[] = { Type_Bool }; \
                trait_impl_full(trait, left, type_params, 1, assoc_types, 1, impls, 2); \
            } \
        } \
    } while(0)

// Register unary operations for all integer types
#define REGISTER_INT_UNARY_ALL(trait, method_name_str, intrinsic_fn) \
    do { \
        TypeInfo* int_types[] = { \
            Type_I8, Type_I16, Type_I32, Type_I64, \
            Type_U8, Type_U16, Type_U32, Type_U64 \
        }; \
        for (int i = 0; i < 8; i++) { \
            REGISTER_UNARY_OP(trait, int_types[i], int_types[i], method_name_str, intrinsic_fn); \
        } \
    } while(0)

// === Register All Built-in Type Implementations ===

void traits_register_builtin_impls(TraitRegistry* registry) {
    (void)registry; // Traits are already defined

    TypeInfo* int_type = Type_I32;
    TypeInfo* double_type = Type_Double;
    TypeInfo* bool_type = Type_Bool;

    // === Add Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_Add, "add", intrinsic_int_add, NULL, NULL, NULL, NULL);
    // int + double -> double
    REGISTER_BINARY_OP(Trait_Add, int_type, double_type, double_type, "add", intrinsic_int_double_add);
    // double + int -> double
    REGISTER_BINARY_OP(Trait_Add, double_type, int_type, double_type, "add", intrinsic_double_int_add);
    // double + double -> double
    REGISTER_BINARY_OP(Trait_Add, double_type, double_type, double_type, "add", intrinsic_double_add);

    // === Sub Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_Sub, "sub", NULL, intrinsic_int_sub, NULL, NULL, NULL);
    // int - double -> double
    REGISTER_BINARY_OP(Trait_Sub, int_type, double_type, double_type, "sub", intrinsic_int_double_sub);
    // double - int -> double
    REGISTER_BINARY_OP(Trait_Sub, double_type, int_type, double_type, "sub", intrinsic_double_int_sub);
    // double - double -> double
    REGISTER_BINARY_OP(Trait_Sub, double_type, double_type, double_type, "sub", intrinsic_double_sub);

    // === Mul Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_Mul, "mul", NULL, NULL, intrinsic_int_mul, NULL, NULL);
    // int * double -> double
    REGISTER_BINARY_OP(Trait_Mul, int_type, double_type, double_type, "mul", intrinsic_int_double_mul);
    // double * int -> double
    REGISTER_BINARY_OP(Trait_Mul, double_type, int_type, double_type, "mul", intrinsic_double_int_mul);
    // double * double -> double
    REGISTER_BINARY_OP(Trait_Mul, double_type, double_type, double_type, "mul", intrinsic_double_mul);

    // === Div Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_Div, "div", NULL, NULL, NULL, intrinsic_int_div, NULL);
    // int / double -> double
    REGISTER_BINARY_OP(Trait_Div, int_type, double_type, double_type, "div", intrinsic_int_double_div);
    // double / int -> double
    REGISTER_BINARY_OP(Trait_Div, double_type, int_type, double_type, "div", intrinsic_double_int_div);
    // double / double -> double
    REGISTER_BINARY_OP(Trait_Div, double_type, double_type, double_type, "div", intrinsic_double_div);

    // === Rem Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_Rem, "rem", NULL, NULL, NULL, NULL, intrinsic_int_rem);

    // === BitAnd Trait ===

    // All integer type combinations
    REGISTER_INT_BITWISE_ALL(Trait_BitAnd, "bitand", intrinsic_int_bitand);
    // bool & bool -> bool
    REGISTER_BINARY_OP(Trait_BitAnd, bool_type, bool_type, bool_type, "bitand", intrinsic_bool_bitand);

    // === BitOr Trait ===

    // All integer type combinations
    REGISTER_INT_BITWISE_ALL(Trait_BitOr, "bitor", intrinsic_int_bitor);
    // bool | bool -> bool
    REGISTER_BINARY_OP(Trait_BitOr, bool_type, bool_type, bool_type, "bitor", intrinsic_bool_bitor);

    // === BitXor Trait ===

    // All integer type combinations
    REGISTER_INT_BITWISE_ALL(Trait_BitXor, "bitxor", intrinsic_int_bitxor);
    // bool ^ bool -> bool
    REGISTER_BINARY_OP(Trait_BitXor, bool_type, bool_type, bool_type, "bitxor", intrinsic_bool_bitxor);

    // === Shl Trait ===

    // All integer type combinations
    REGISTER_INT_SHIFT_ALL(Trait_Shl, "shl", intrinsic_int_shl, NULL);

    // === Shr Trait ===

    // All integer type combinations
    REGISTER_INT_SHIFT_ALL(Trait_Shr, "shr", NULL, intrinsic_int_shr);

    // === Eq Trait ===

    // All integer type combinations
    REGISTER_INT_EQUALITY_ALL(Trait_Eq);
    REGISTER_EQUALITY_OPS(Trait_Eq, int_type, double_type);
    REGISTER_EQUALITY_OPS(Trait_Eq, double_type, int_type);
    REGISTER_EQUALITY_OPS(Trait_Eq, double_type, double_type);
    REGISTER_EQUALITY_OPS(Trait_Eq, bool_type, bool_type);

    // === Ord Trait ===

    // All integer type combinations
    REGISTER_INT_COMPARISONS_ALL(Trait_Ord);
    REGISTER_COMPARISON_OPS(Trait_Ord, int_type, double_type);
    REGISTER_COMPARISON_OPS(Trait_Ord, double_type, int_type);
    REGISTER_COMPARISON_OPS(Trait_Ord, double_type, double_type);

    // === Not Trait ===

    // !bool -> bool
    REGISTER_UNARY_OP(Trait_Not, bool_type, bool_type, "not", intrinsic_bool_not);

    // === Neg Trait ===

    // All integer types
    REGISTER_INT_UNARY_ALL(Trait_Neg, "neg", intrinsic_int_neg);
    // -double -> double
    REGISTER_UNARY_OP(Trait_Neg, double_type, double_type, "neg", intrinsic_double_neg);

    // === AddAssign Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_AddAssign, "add_assign", intrinsic_int_add_assign, NULL, NULL, NULL, NULL);
    // double += double
    REGISTER_BINARY_OP(Trait_AddAssign, double_type, double_type, double_type, "add_assign", intrinsic_double_add_assign);

    // === SubAssign Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_SubAssign, "sub_assign", NULL, intrinsic_int_sub_assign, NULL, NULL, NULL);
    // double -= double
    REGISTER_BINARY_OP(Trait_SubAssign, double_type, double_type, double_type, "sub_assign", intrinsic_double_sub_assign);

    // === MulAssign Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_MulAssign, "mul_assign", NULL, NULL, intrinsic_int_mul_assign, NULL, NULL);
    // double *= double
    REGISTER_BINARY_OP(Trait_MulAssign, double_type, double_type, double_type, "mul_assign", intrinsic_double_mul_assign);

    // === DivAssign Trait ===

    // All integer type combinations
    REGISTER_INT_ARITHMETIC(Trait_DivAssign, "div_assign", NULL, NULL, NULL, intrinsic_int_div_assign, NULL);
    // double /= double
    REGISTER_BINARY_OP(Trait_DivAssign, double_type, double_type, double_type, "div_assign", intrinsic_double_div_assign);

    // === Display Trait ===
    // Display trait implementations for builtin types
    // These call external C functions in runtime/display.c
    
    #define REGISTER_DISPLAY(type_info, external_fn_name) \
        do { \
            MethodImpl impl; \
            impl.method_name = "fmt"; \
            impl.signature = NULL; \
            impl.kind = METHOD_EXTERNAL; \
            impl.codegen = NULL; \
            impl.function_ptr = NULL; \
            impl.external_name = external_fn_name; \
            trait_impl_simple(Trait_Display, type_info, &impl, 1); \
        } while(0)
    
    REGISTER_DISPLAY(Type_I32, "display_i32");
    REGISTER_DISPLAY(Type_I64, "display_i64");
    REGISTER_DISPLAY(Type_I8, "display_i8");
    REGISTER_DISPLAY(Type_I16, "display_i16");
    REGISTER_DISPLAY(Type_U32, "display_u32");
    REGISTER_DISPLAY(Type_U64, "display_u64");
    REGISTER_DISPLAY(Type_U8, "display_u8");
    REGISTER_DISPLAY(Type_U16, "display_u16");
    REGISTER_DISPLAY(Type_Bool, "display_bool");
    REGISTER_DISPLAY(Type_Str, "display_str");
    REGISTER_DISPLAY(Type_Double, "display_f64");
    
    #undef REGISTER_DISPLAY
    
    // Register Eq trait for str type
    trait_register_eq_for_str(registry);
    
    // Register Add trait for str type
    trait_register_add_for_str(registry);
    
    // Register CStr trait for str type
    trait_ensure_cstr_impl(Type_Str);
    
    // Register From<str> for c_str type
    trait_ensure_implicit_from_impl(Type_CStr, Type_Str);
    
    // Register From<int types> for usize (for indexing operations)
    TypeInfo* int_types[] = {
        Type_I8, Type_I16, Type_I32, Type_I64,
        Type_U8, Type_U16, Type_U32, Type_U64
    };
    for (int i = 0; i < 8; i++) {
        trait_ensure_implicit_from_impl(Type_Usize, int_types[i]);
    }
}

// === Enum Equality Intrinsics ===

// Intrinsic for enum equality (==)
// For enums, we compare the tag/discriminant values directly
// This works for both unit variants and data variants (tag comparison first)
static LLVMValueRef intrinsic_enum_eq(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    // args[0] and args[1] are enum values (represented as integers for unit variants)
    // For now, we do simple integer comparison (tag comparison)
    // TODO: For data variants, we need deeper comparison of fields
    return LLVMBuildICmp(gen->builder, LLVMIntEQ, args[0], args[1], "enum_eq");
}

// Intrinsic for enum inequality (!=)
static LLVMValueRef intrinsic_enum_ne(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    return LLVMBuildICmp(gen->builder, LLVMIntNE, args[0], args[1], "enum_ne");
}

// === Auto-implement Eq trait for enum types ===

// Register Eq trait implementation for an enum type
// This is called automatically when an enum is declared
void trait_register_eq_for_enum(TypeInfo* enum_type, TraitRegistry* registry) {
    if (!enum_type || !registry || enum_type->kind != TYPE_KIND_ENUM) {
        return;
    }

    // Check if Eq trait implementation already exists
    TypeInfo* type_params[] = { enum_type };
    TraitImpl* existing = trait_find_impl(Trait_Eq, enum_type, type_params, 1);
    if (existing) {
        return; // Already implemented
    }

    // Create method implementations for eq and ne
    MethodImpl methods[2];
    
    // eq method
    methods[0].method_name = "eq";
    methods[0].signature = NULL;  // Will be filled in by trait system
    methods[0].kind = METHOD_INTRINSIC;
    methods[0].codegen = intrinsic_enum_eq;
    methods[0].function_ptr = NULL;
    methods[0].external_name = NULL;

    // ne method  
    methods[1].method_name = "ne";
    methods[1].signature = NULL;
    methods[1].kind = METHOD_INTRINSIC;
    methods[1].codegen = intrinsic_enum_ne;
    methods[1].function_ptr = NULL;
    methods[1].external_name = NULL;

    // Register the trait implementation
    // For Eq<Rhs>, we use Rhs=Self (comparing enum with itself)
    TypeInfo* rhs_binding[] = { enum_type };
    TypeInfo* output_binding[] = { Type_Bool };  // Eq returns bool
    
    trait_impl_full(Trait_Eq, enum_type, rhs_binding, 1, output_binding, 1, methods, 2);
}

// Context structure for enum Display trait implementation
typedef struct {
    TypeInfo* enum_type;
} EnumDisplayContext;

// Generate Display implementation intrinsic for enums
// The context parameter contains the TypeInfo for this specific enum
static LLVMValueRef generate_enum_display_impl(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    if (arg_count != 2 || !context) return NULL;
    
    EnumDisplayContext* ctx = (EnumDisplayContext*)context;
    TypeInfo* enum_type = ctx->enum_type;
    
    LLVMValueRef enum_val = args[0];       // The enum discriminant value (i32)
    LLVMValueRef formatter_ptr = args[1];  // Pointer to Formatter struct
    
    // Get FILE* from Formatter.stream (field 0)
    LLVMTypeRef file_struct = LLVMGetTypeByName2(gen->context, "struct._IO_FILE");
    if (!file_struct) {
        file_struct = LLVMStructCreateNamed(gen->context, "struct._IO_FILE");
    }
    
    LLVMTypeRef formatter_type = LLVMStructType(
        (LLVMTypeRef[]){LLVMPointerType(file_struct, 0)}, 1, false);
    
    LLVMValueRef stream_ptr_ptr = LLVMBuildStructGEP2(gen->builder, formatter_type, 
                                                        formatter_ptr, 0, "stream_ptr");
    LLVMValueRef stream = LLVMBuildLoad2(gen->builder, LLVMPointerType(file_struct, 0),
                                         stream_ptr_ptr, "stream");
    
    // Get fprintf function
    LLVMValueRef fprintf_fn = LLVMGetNamedFunction(gen->module, "fprintf");
    if (!fprintf_fn) {
        LLVMTypeRef fprintf_type = LLVMFunctionType(
            LLVMInt32Type(),
            (LLVMTypeRef[]){LLVMPointerType(file_struct, 0), LLVMPointerType(LLVMInt8Type(), 0)},
            2, true);
        fprintf_fn = LLVMAddFunction(gen->module, "fprintf", fprintf_type);
    }
    
    // Generate switch statement on enum discriminant
    int variant_count = enum_type->data.enum_type.variant_count;
    
    // Create blocks for each variant + default
    LLVMBasicBlockRef* variant_blocks = malloc(sizeof(LLVMBasicBlockRef) * variant_count);
    LLVMBasicBlockRef default_block = LLVMAppendBasicBlock(
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
        "enum_display_default");
    LLVMBasicBlockRef end_block = LLVMAppendBasicBlock(
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
        "enum_display_end");
    
    for (int i = 0; i < variant_count; i++) {
        char block_name[256];
        snprintf(block_name, sizeof(block_name), "enum_display_%s", 
                 enum_type->data.enum_type.variant_names[i]);
        variant_blocks[i] = LLVMAppendBasicBlock(
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
            block_name);
    }
    
    // Build switch
    LLVMValueRef switch_inst = LLVMBuildSwitch(gen->builder, enum_val, default_block, variant_count);
    
    // Generate case for each variant
    for (int i = 0; i < variant_count; i++) {
        LLVMAddCase(switch_inst, LLVMConstInt(LLVMInt32Type(), i, false), variant_blocks[i]);
        
        LLVMPositionBuilderAtEnd(gen->builder, variant_blocks[i]);
        
        // Create format string for this variant name
        const char* variant_name = enum_type->data.enum_type.variant_names[i];
        LLVMValueRef format_str = LLVMBuildGlobalStringPtr(gen->builder, variant_name, "variant_name_fmt");
        
        // Call fprintf(stream, variant_name)
        LLVMValueRef fprintf_args[] = {stream, format_str};
        LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), 
                      fprintf_fn, fprintf_args, 2, "");
        
        LLVMBuildBr(gen->builder, end_block);
    }
    
    // Default case: print "Unknown"
    LLVMPositionBuilderAtEnd(gen->builder, default_block);
    LLVMValueRef unknown_str = LLVMBuildGlobalStringPtr(gen->builder, "Unknown", "unknown_fmt");
    LLVMValueRef unknown_args[] = {stream, unknown_str};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), 
                  fprintf_fn, unknown_args, 2, "");
    LLVMBuildBr(gen->builder, end_block);
    
    // End block
    LLVMPositionBuilderAtEnd(gen->builder, end_block);
    
    free(variant_blocks);
    
    return NULL; // Display.fmt returns void
}

void trait_register_display_for_enum(TypeInfo* enum_type, TraitRegistry* registry) {
    if (!enum_type || !registry || enum_type->kind != TYPE_KIND_ENUM) {
        return;
    }

    // Check if Display trait implementation already exists
    TraitImpl* existing = trait_find_impl(Trait_Display, enum_type, NULL, 0);
    if (existing) {
        return; // Already implemented
    }

    // Allocate context for this specific enum
    EnumDisplayContext* context = malloc(sizeof(EnumDisplayContext));
    context->enum_type = enum_type;

    // Create method implementation for fmt
    MethodImpl methods[1];
    
    // fmt method - uses intrinsic codegen with context
    methods[0].method_name = "fmt";
    methods[0].signature = NULL;  // Will be filled in by trait system
    methods[0].kind = METHOD_INTRINSIC;
    methods[0].codegen = generate_enum_display_impl;
    methods[0].function_ptr = context;  // Pass TypeInfo through context
    methods[0].external_name = NULL;

    // Register the trait implementation
    // Display has no type parameters
    trait_impl_full(Trait_Display, enum_type, NULL, 0, NULL, 0, methods, 1);
}

// === String (str) Equality Intrinsics ===

// Intrinsic for str == str comparison using memcmp
static LLVMValueRef intrinsic_str_eq(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    (void)arg_count;
    
    // args[0] and args[1] are %str struct VALUES (str is a value type)
    // Extract the data and length fields directly from the struct values
    
    LLVMValueRef left = args[0];
    LLVMValueRef right = args[1];
    
    // Extract fields from struct values using ExtractValue
    LLVMValueRef left_data = LLVMBuildExtractValue(gen->builder, left, 0, "left_data");
    LLVMValueRef left_len = LLVMBuildExtractValue(gen->builder, left, 1, "left_len");
    
    LLVMValueRef right_data = LLVMBuildExtractValue(gen->builder, right, 0, "right_data");
    LLVMValueRef right_len = LLVMBuildExtractValue(gen->builder, right, 1, "right_len");
    
    // First check if lengths are equal
    LLVMValueRef len_eq = LLVMBuildICmp(gen->builder, LLVMIntEQ, left_len, right_len, "len_eq");
    
    // Create blocks for control flow
    LLVMBasicBlockRef entry_block = LLVMGetInsertBlock(gen->builder);
    LLVMValueRef function = LLVMGetBasicBlockParent(entry_block);
    
    LLVMBasicBlockRef memcmp_block = LLVMAppendBasicBlockInContext(gen->context, function, "memcmp");
    LLVMBasicBlockRef result_block = LLVMAppendBasicBlockInContext(gen->context, function, "result");
    
    // If lengths differ, strings are not equal
    LLVMBuildCondBr(gen->builder, len_eq, memcmp_block, result_block);
    
    // memcmp_block: lengths are equal, compare content
    LLVMPositionBuilderAtEnd(gen->builder, memcmp_block);
    
    // Call memcmp(left_data, right_data, left_len)
    LLVMTypeRef memcmp_type = LLVMFunctionType(
        LLVMInt32TypeInContext(gen->context),
        (LLVMTypeRef[]){
            LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
            LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
            LLVMInt64TypeInContext(gen->context)
        },
        3,
        false
    );
    
    LLVMValueRef memcmp_fn = LLVMGetNamedFunction(gen->module, "memcmp");
    if (!memcmp_fn) {
        memcmp_fn = LLVMAddFunction(gen->module, "memcmp", memcmp_type);
    }
    
    // Convert usize length to i64 for memcmp
    LLVMValueRef len_i64 = left_len;  // On 64-bit, usize is already i64
    #if !defined(__LP64__) && !defined(_WIN64) && !defined(__x86_64__) && !defined(__aarch64__)
        // On 32-bit, extend u32 to i64
        len_i64 = LLVMBuildZExt(gen->builder, left_len, LLVMInt64TypeInContext(gen->context), "len_ext");
    #endif
    
    LLVMValueRef memcmp_args[] = { left_data, right_data, len_i64 };
    LLVMValueRef memcmp_result = LLVMBuildCall2(gen->builder, memcmp_type, memcmp_fn, memcmp_args, 3, "memcmp");
    
    // memcmp returns 0 if equal
    LLVMValueRef data_eq = LLVMBuildICmp(gen->builder, LLVMIntEQ, memcmp_result, 
        LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, false), "data_eq");
    
    LLVMBuildBr(gen->builder, result_block);
    LLVMBasicBlockRef memcmp_end_block = LLVMGetInsertBlock(gen->builder);
    
    // result_block: phi node to merge results
    LLVMPositionBuilderAtEnd(gen->builder, result_block);
    
    LLVMValueRef phi = LLVMBuildPhi(gen->builder, LLVMInt1TypeInContext(gen->context), "str_eq");
    LLVMValueRef false_val = LLVMConstInt(LLVMInt1TypeInContext(gen->context), 0, false);
    LLVMAddIncoming(phi, &false_val, &entry_block, 1);  // Length mismatch -> false
    LLVMAddIncoming(phi, &data_eq, &memcmp_end_block, 1);  // memcmp result
    
    return phi;
}

// Intrinsic for str != str comparison
static LLVMValueRef intrinsic_str_ne(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    // Reuse eq intrinsic and negate
    LLVMValueRef eq_result = intrinsic_str_eq(gen, args, arg_count, context);
    return LLVMBuildNot(gen->builder, eq_result, "str_ne");
}

// Register Eq trait for str type
void trait_register_eq_for_str(TraitRegistry* registry) {
    (void)registry;  // Not used, Trait_Eq is already initialized
    
    // Check if already registered
    TypeInfo* type_param_bindings[] = { Type_Str };  // str == str
    TraitImpl* existing = trait_find_impl(Trait_Eq, Type_Str, type_param_bindings, 1);
    if (existing) {
        return;
    }
    
    // Create method implementations
    MethodImpl methods[2];
    
    // eq method
    methods[0].method_name = "eq";
    methods[0].signature = NULL;
    methods[0].kind = METHOD_INTRINSIC;
    methods[0].codegen = intrinsic_str_eq;
    methods[0].function_ptr = NULL;
    methods[0].external_name = NULL;
    
    // ne method
    methods[1].method_name = "ne";
    methods[1].signature = NULL;
    methods[1].kind = METHOD_INTRINSIC;
    methods[1].codegen = intrinsic_str_ne;
    methods[1].function_ptr = NULL;
    methods[1].external_name = NULL;
    
    // Register: Eq<str> for str with Output = bool
    TypeInfo* assoc_type_bindings[] = { Type_Bool };
    
    trait_impl_full(Trait_Eq, Type_Str,
                   type_param_bindings, 1,      // Rhs = str
                   assoc_type_bindings, 1,      // Output = bool
                   methods, 2);
}

// Intrinsic implementation for str + str concatenation
// args[0] = left str (struct value)
// args[1] = right str (struct value)
// Returns: new str (struct value) with concatenated data
static LLVMValueRef intrinsic_str_add(CodeGen* gen, LLVMValueRef* args, int arg_count, void* context) {
    (void)context;
    if (arg_count != 2) return NULL;
    
    LLVMValueRef left = args[0];
    LLVMValueRef right = args[1];
    
    // Extract fields from left string
    LLVMValueRef left_data = LLVMBuildExtractValue(gen->builder, left, 0, "left_data");
    LLVMValueRef left_len = LLVMBuildExtractValue(gen->builder, left, 1, "left_len");
    
    // Extract fields from right string
    LLVMValueRef right_data = LLVMBuildExtractValue(gen->builder, right, 0, "right_data");
    LLVMValueRef right_len = LLVMBuildExtractValue(gen->builder, right, 1, "right_len");
    
    // Calculate total length: left_len + right_len
    LLVMValueRef total_len = LLVMBuildAdd(gen->builder, left_len, right_len, "total_len");
    
    // Get or declare jsasta_alloc_string function
    // StrWrapper jsasta_alloc_string(int64_t length)
    LLVMTypeRef str_type = get_str_type(gen);
    LLVMTypeRef alloc_fn_type = LLVMFunctionType(
        str_type,
        (LLVMTypeRef[]){LLVMInt64TypeInContext(gen->context)},
        1, false
    );
    LLVMValueRef alloc_fn = LLVMGetNamedFunction(gen->module, "jsasta_alloc_string");
    if (!alloc_fn) {
        alloc_fn = LLVMAddFunction(gen->module, "jsasta_alloc_string", alloc_fn_type);
    }
    
    // Call jsasta_alloc_string(total_len) to get new string
    LLVMValueRef new_str = LLVMBuildCall2(gen->builder, alloc_fn_type, alloc_fn, 
                                          (LLVMValueRef[]){total_len}, 1, "new_str");
    
    // Extract data pointer from new string
    LLVMValueRef new_data = LLVMBuildExtractValue(gen->builder, new_str, 0, "new_data");
    
    // Get memcpy function
    // void @llvm.memcpy.p0.p0.i64(i8* dest, i8* src, i64 len, i1 isvolatile)
    LLVMTypeRef memcpy_params[] = {
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),  // dest
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),  // src
        LLVMInt64TypeInContext(gen->context),                     // len
        LLVMInt1TypeInContext(gen->context)                       // isvolatile
    };
    LLVMTypeRef memcpy_type = LLVMFunctionType(LLVMVoidType(), memcpy_params, 4, false);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(gen->module, "llvm.memcpy.p0.p0.i64");
    if (!memcpy_fn) {
        memcpy_fn = LLVMAddFunction(gen->module, "llvm.memcpy.p0.p0.i64", memcpy_type);
    }
    
    // Copy left string: memcpy(new_data, left_data, left_len)
    LLVMValueRef memcpy_args1[] = {
        new_data,
        left_data,
        left_len,
        LLVMConstInt(LLVMInt1TypeInContext(gen->context), 0, 0)  // not volatile
    };
    LLVMBuildCall2(gen->builder, memcpy_type, memcpy_fn, memcpy_args1, 4, "");
    
    // Calculate destination for right string: new_data + left_len
    LLVMValueRef right_dest = LLVMBuildGEP2(gen->builder, 
                                             LLVMInt8TypeInContext(gen->context),
                                             new_data, 
                                             (LLVMValueRef[]){left_len}, 
                                             1, "right_dest");
    
    // Copy right string: memcpy(new_data + left_len, right_data, right_len)
    LLVMValueRef memcpy_args2[] = {
        right_dest,
        right_data,
        right_len,
        LLVMConstInt(LLVMInt1TypeInContext(gen->context), 0, 0)  // not volatile
    };
    LLVMBuildCall2(gen->builder, memcpy_type, memcpy_fn, memcpy_args2, 4, "");
    
    // Return the new string
    return new_str;
}

// Register Add trait for str type
void trait_register_add_for_str(TraitRegistry* registry) {
    (void)registry;
    
    // Check if already registered
    TypeInfo* type_param_bindings[] = { Type_Str };  // str + str
    TraitImpl* existing = trait_find_impl(Trait_Add, Type_Str, type_param_bindings, 1);
    if (existing) {
        return;
    }
    
    // Create method implementation
    MethodImpl methods[1];
    
    // add method
    methods[0].method_name = "add";
    methods[0].signature = NULL;
    methods[0].kind = METHOD_INTRINSIC;
    methods[0].codegen = intrinsic_str_add;
    methods[0].function_ptr = NULL;
    methods[0].external_name = NULL;
    
    // Register: Add<str> for str with Output = str
    TypeInfo* assoc_type_bindings[] = { Type_Str };
    
    trait_impl_full(Trait_Add, Type_Str,
                   type_param_bindings, 1,      // Rhs = str
                   assoc_type_bindings, 1,      // Output = str
                   methods, 1);
}
