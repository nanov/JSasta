#include "jsasta_compiler.h"
#include "llvm-c/Types.h"
#include <llvm-c/Core.h>
#include <string.h>
#include <stdio.h>

// Forward declare handlers
static LLVMValueRef runtime_console_log(CodeGen* gen, ASTNode* call_node);
static LLVMValueRef runtime_array(CodeGen* gen, ASTNode* call_node);

// Runtime function type lookup table (for type inference before codegen)
ValueType runtime_get_function_type(const char* name) {
    if (strcmp(name, "Array") == 0) {
        return TYPE_ARRAY_INT;
    }
    if (strcmp(name, "console.log") == 0) {
        return TYPE_VOID;
    }
    return TYPE_UNKNOWN;
}

void runtime_init(CodeGen* gen) {
    // Declare printf for console.log implementation
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(gen->context),
        printf_args,
        1,
        1 // variadic
    );
    LLVMAddFunction(gen->module, "printf", printf_type);

    // Declare puts for simple string output
    LLVMTypeRef puts_args[] = { LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0) };
    LLVMTypeRef puts_type = LLVMFunctionType(
        LLVMInt32TypeInContext(gen->context),
        puts_args,
        1,
        0
    );
    LLVMAddFunction(gen->module, "puts", puts_type);

    // Declare malloc for string operations
    LLVMTypeRef malloc_args[] = { LLVMInt64TypeInContext(gen->context) };
    LLVMTypeRef malloc_type = LLVMFunctionType(
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        malloc_args,
        1,
        0
    );
    LLVMAddFunction(gen->module, "malloc", malloc_type);

    // Declare sprintf for string formatting
    LLVMTypeRef sprintf_args[] = {
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0)
    };
    LLVMTypeRef sprintf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(gen->context),
        sprintf_args,
        2,
        1 // variadic
    );
    LLVMAddFunction(gen->module, "sprintf", sprintf_type);

    // Declare strcat for string concatenation
    LLVMTypeRef strcat_args[] = {
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0)
    };
    LLVMTypeRef strcat_type = LLVMFunctionType(
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        strcat_args,
        2,
        0
    );
    LLVMAddFunction(gen->module, "strcat", strcat_type);

    // Declare strcpy for string copying
    LLVMTypeRef strcpy_args[] = {
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0)
    };
    LLVMTypeRef strcpy_type = LLVMFunctionType(
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        strcpy_args,
        2,
        0
    );
    LLVMAddFunction(gen->module, "strcpy", strcpy_type);

    // Declare strlen for string length
    LLVMTypeRef strlen_args[] = {
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0)
    };
    LLVMTypeRef strlen_type = LLVMFunctionType(
        LLVMInt64TypeInContext(gen->context),
        strlen_args,
        1,
        0
    );
    LLVMAddFunction(gen->module, "strlen", strlen_type);

    // Declare calloc for zeroed array allocation
    LLVMTypeRef calloc_args[] = {
        LLVMInt64TypeInContext(gen->context),
        LLVMInt64TypeInContext(gen->context)
    };
    LLVMTypeRef calloc_type = LLVMFunctionType(
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        calloc_args,
        2,
        0
    );
    LLVMAddFunction(gen->module, "calloc", calloc_type);

    // Register runtime functions
    codegen_register_runtime_function(gen, "console.log", TYPE_VOID, runtime_console_log);
    codegen_register_runtime_function(gen, "Array", TYPE_ARRAY_INT, runtime_array);

    // Add more runtime functions here as needed:
    // codegen_register_runtime_function(gen, "console.error", runtime_console_error);
    // codegen_register_runtime_function(gen, "Math.sqrt", runtime_math_sqrt);
    // etc.
}

// Implementation of console.log
static LLVMValueRef runtime_console_log(CodeGen* gen, ASTNode* call_node) {
    LLVMValueRef printf_func = LLVMGetNamedFunction(gen->module, "printf");

    for (int i = 0; i < call_node->call.arg_count; i++) {
        LLVMValueRef arg = codegen_node(gen, call_node->call.args[i]);
        LLVMValueRef format_str;
        LLVMValueRef args[2];

        if (call_node->call.args[i]->value_type == TYPE_STRING) {
            format_str = LLVMBuildGlobalStringPtr(gen->builder, "%s", "fmt");
            args[0] = format_str;
            args[1] = arg;
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(printf_func),
                          printf_func, args, 2, "");
        } else if (call_node->call.args[i]->value_type == TYPE_INT) {
            format_str = LLVMBuildGlobalStringPtr(gen->builder, "%d", "fmt");
            args[0] = format_str;
            args[1] = arg;
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(printf_func),
                          printf_func, args, 2, "");
        } else if (call_node->call.args[i]->value_type == TYPE_DOUBLE) {
            format_str = LLVMBuildGlobalStringPtr(gen->builder, "%f", "fmt");
            args[0] = format_str;
            args[1] = arg;
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(printf_func),
                          printf_func, args, 2, "");
        } else if (call_node->call.args[i]->value_type == TYPE_BOOL) {
            LLVMValueRef true_str = LLVMBuildGlobalStringPtr(gen->builder, "true", "true_str");
            LLVMValueRef false_str = LLVMBuildGlobalStringPtr(gen->builder, "false", "false_str");
            LLVMValueRef result = LLVMBuildSelect(gen->builder, arg, true_str, false_str, "bool_str");

            format_str = LLVMBuildGlobalStringPtr(gen->builder, "%s", "fmt");
            args[0] = format_str;
            args[1] = result;
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(printf_func),
                          printf_func, args, 2, "");
        }

        if (i < call_node->call.arg_count - 1) {
            LLVMValueRef space = LLVMBuildGlobalStringPtr(gen->builder, " ", "space");
            LLVMValueRef space_args[] = { space };
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(printf_func),
                          printf_func, space_args, 1, "");
        }
    }

    // Print newline
    LLVMValueRef newline = LLVMBuildGlobalStringPtr(gen->builder, "\n", "newline");
    LLVMValueRef newline_args[] = { newline };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(printf_func),
                  printf_func, newline_args, 1, "");

    return LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0);
}

// Implementation of Array(size) - creates a zeroed integer array
static LLVMValueRef runtime_array(CodeGen* gen, ASTNode* call_node) {
    if (call_node->call.arg_count != 1) {
        fprintf(stderr, "Error: Array() requires exactly 1 argument (size)\n");
        return NULL;
    }

    // Get the size argument
    LLVMValueRef size_arg = codegen_node(gen, call_node->call.args[0]);

    // Always create integer arrays (i32*)
    LLVMTypeRef elem_type = LLVMInt32TypeInContext(gen->context);
    int elem_size = 4;

    // Get calloc function
    LLVMValueRef calloc_func = LLVMGetNamedFunction(gen->module, "calloc");

    // Convert size to i64 if needed
    LLVMValueRef size_i64 = size_arg;
    if (LLVMTypeOf(size_arg) != LLVMInt64TypeInContext(gen->context)) {
        size_i64 = LLVMBuildZExt(gen->builder, size_arg,
                                 LLVMInt64TypeInContext(gen->context), "size_i64");
    }

    // Call calloc(count, elem_size) - allocates and zeros memory
    LLVMValueRef calloc_args[] = {
        size_i64,
        LLVMConstInt(LLVMInt64TypeInContext(gen->context), elem_size, 0)
    };
    LLVMValueRef array_ptr = LLVMBuildCall2(gen->builder,
                                             LLVMGlobalGetValueType(calloc_func),
                                             calloc_func, calloc_args, 2, "array_calloc");

    // Cast to i32* pointer type
    LLVMValueRef typed_array = LLVMBuildBitCast(gen->builder, array_ptr,
                                                 LLVMPointerType(elem_type, 0),
                                                 "array_ptr");

    return typed_array;
}
