#include "operator_utils.h"
#include <string.h>

// Operator lookup table
// Using pointers to global trait variables allows dynamic initialization
static const OperatorMapping OPERATOR_TABLE[] = {
    {"+",  &Trait_Add,    "add"},
    {"-",  &Trait_Sub,    "sub"},
    {"*",  &Trait_Mul,    "mul"},
    {"/",  &Trait_Div,    "div"},
    {"%",  &Trait_Rem,    "rem"},
    {"&",  &Trait_BitAnd, "bitand"},
    {"|",  &Trait_BitOr,  "bitor"},
    {"^",  &Trait_BitXor, "bitxor"},
    {"<<", &Trait_Shl,    "shl"},
    {">>", &Trait_Shr,    "shr"},
    {"==", &Trait_Eq,     "eq"},
    {"!=", &Trait_Eq,     "ne"},
    {"<",  &Trait_Ord,    "lt"},
    {"<=", &Trait_Ord,    "le"},
    {">",  &Trait_Ord,    "gt"},
    {">=", &Trait_Ord,    "ge"},
    {"+=", &Trait_AddAssign, "add_assign"},
    {"-=", &Trait_SubAssign, "sub_assign"},
    {"*=", &Trait_MulAssign, "mul_assign"},
    {"/=", &Trait_DivAssign, "div_assign"},
    {NULL, NULL,          NULL}  // Sentinel
};

Trait* operator_to_trait(const char* op) {
    if (!op) return NULL;
    
    for (int i = 0; OPERATOR_TABLE[i].operator_str != NULL; i++) {
        if (strcmp(op, OPERATOR_TABLE[i].operator_str) == 0) {
            return *OPERATOR_TABLE[i].trait_ptr;
        }
    }
    return NULL;
}

const char* operator_to_method(const char* op) {
    if (!op) return NULL;
    
    for (int i = 0; OPERATOR_TABLE[i].operator_str != NULL; i++) {
        if (strcmp(op, OPERATOR_TABLE[i].operator_str) == 0) {
            return OPERATOR_TABLE[i].method_name;
        }
    }
    return NULL;
}

void operator_get_trait_and_method(const char* op, Trait** out_trait, const char** out_method) {
    if (!op) {
        if (out_trait) *out_trait = NULL;
        if (out_method) *out_method = NULL;
        return;
    }
    
    for (int i = 0; OPERATOR_TABLE[i].operator_str != NULL; i++) {
        if (strcmp(op, OPERATOR_TABLE[i].operator_str) == 0) {
            if (out_trait) *out_trait = *OPERATOR_TABLE[i].trait_ptr;
            if (out_method) *out_method = OPERATOR_TABLE[i].method_name;
            return;
        }
    }
    
    if (out_trait) *out_trait = NULL;
    if (out_method) *out_method = NULL;
}
