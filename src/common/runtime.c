#include "jsasta_compiler.h"
#include "llvm-c/Types.h"
#include <llvm-c/Core.h>

// Initialize runtime by declaring C library functions needed by the compiler
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

    // Declare jsasta_alloc - allocates memory (wraps calloc for now, future: ARC/GC)
    // Returns zeroed memory
    LLVMTypeRef jsasta_alloc_args[] = {
        LLVMInt64TypeInContext(gen->context)  // size in bytes
    };
    LLVMTypeRef jsasta_alloc_type = LLVMFunctionType(
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
        jsasta_alloc_args,
        1,
        0
    );
    LLVMAddFunction(gen->module, "jsasta_alloc", jsasta_alloc_type);

    // Declare jsasta_free - deallocates memory (wraps free for now, future: ARC/GC)
    LLVMTypeRef jsasta_free_args[] = {
        LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0)  // pointer to free
    };
    LLVMTypeRef jsasta_free_type = LLVMFunctionType(
        LLVMVoidTypeInContext(gen->context),
        jsasta_free_args,
        1,
        0
    );
    LLVMAddFunction(gen->module, "jsasta_free", jsasta_free_type);
}
