#ifndef JSASTA_OPERATOR_UTILS_H
#define JSASTA_OPERATOR_UTILS_H

#include "traits.h"

// Operator mapping structure
typedef struct {
    const char* operator_str;  // Operator string (e.g., "+", "-")
    Trait** trait_ptr;         // Pointer to global trait variable
    const char* method_name;   // Method name (e.g., "add", "sub")
} OperatorMapping;

// Get trait for an operator string
Trait* operator_to_trait(const char* op);

// Get method name for an operator string
const char* operator_to_method(const char* op);

// Get both trait and method (more efficient when both are needed)
void operator_get_trait_and_method(const char* op, Trait** out_trait, const char** out_method);

#endif // JSASTA_OPERATOR_UTILS_H
