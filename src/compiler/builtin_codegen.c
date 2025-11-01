#include "common/jsasta_compiler.h"
#include "common/format_string.h"
#include "common/traits.h"
#include "common/logger.h"
#include <string.h>

// Helper: generate format output code (shared by print/println/format)
// Returns true on success, false on error
static bool generate_format_output(CodeGen* gen, ASTNode* node, LLVMValueRef output_stream) {
    ASTNode* format_arg = node->method_call.args[0];
    const char* format_str = format_arg->string.value;
    
    // Parse the format string (already validated)
    FormatString* fs = format_string_parse(format_str);
    
    // Get fprintf function for writing to specific stream
    LLVMTypeRef file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
    LLVMValueRef fprintf_fn = LLVMGetNamedFunction(gen->module, "fprintf");
    if (!fprintf_fn) {
        LLVMTypeRef fprintf_type = LLVMFunctionType(
            LLVMInt32Type(),
            (LLVMTypeRef[]){LLVMPointerType(file_type, 0), LLVMPointerType(LLVMInt8Type(), 0)},
            2, true);
        fprintf_fn = LLVMAddFunction(gen->module, "fprintf", fprintf_type);
    }

    // Generate code to print each part
    for (int i = 0; i < fs->part_count; i++) {
        // Print literal part
        if (strlen(fs->parts[i]) > 0) {
            LLVMValueRef part_str = LLVMBuildGlobalStringPtr(gen->builder, fs->parts[i], "fmt_part");
            LLVMValueRef args[] = {output_stream, part_str};
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, args, 2, "");
        }

        // Print argument using Display trait (if not the last part)
        if (i < fs->placeholder_count) {
            ASTNode* arg = node->method_call.args[i + 1];  // +1 to skip format string
            TypeInfo* arg_type = arg->type_info;

            // Find Display trait implementation for this type
            TraitImpl* display_impl = trait_find_impl(Trait_Display, arg_type, NULL, 0);
            if (!display_impl) {
                format_string_free(fs);
                return false;
            }

            // Get the fmt method
            MethodImpl* fmt_method = NULL;
            for (int m = 0; m < display_impl->method_count; m++) {
                if (strcmp(display_impl->methods[m].method_name, "fmt") == 0) {
                    fmt_method = &display_impl->methods[m];
                    break;
                }
            }

            // Generate the argument value
            LLVMValueRef arg_val = codegen_node(gen, arg);
            if (!arg_val) {
                format_string_free(fs);
                return false;
            }

            // Call the external display function
            LLVMTypeRef formatter_type = LLVMStructType(
                (LLVMTypeRef[]){LLVMPointerType(file_type, 0)}, 1, false);
            LLVMValueRef formatter = LLVMBuildAlloca(gen->builder, formatter_type, "formatter");
            
            // Store output_stream in formatter.stream
            LLVMValueRef stream_ptr = LLVMBuildStructGEP2(gen->builder, formatter_type, formatter, 0, "stream_ptr");
            LLVMBuildStore(gen->builder, output_stream, stream_ptr);

            // Get or declare the external display function
            LLVMValueRef display_fn = LLVMGetNamedFunction(gen->module, fmt_method->external_name);
            if (!display_fn) {
                LLVMTypeRef display_fn_type = LLVMFunctionType(
                    LLVMVoidType(),
                    (LLVMTypeRef[]){get_llvm_type(gen, arg_type), LLVMPointerType(formatter_type, 0)},
                    2, false);
                display_fn = LLVMAddFunction(gen->module, fmt_method->external_name, display_fn_type);
            }

            // Call display_<type>(arg_val, &formatter)
            LLVMValueRef display_args[] = {arg_val, formatter};
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(display_fn), display_fn, display_args, 2, "");
        }
    }

    format_string_free(fs);
    return true;
}

// Codegen callback for io.println
LLVMValueRef io_println_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;
    
    // Get stdout
    LLVMTypeRef file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
    LLVMValueRef global_stdout = LLVMGetNamedGlobal(gen->module, "__jsasta_stdout");
    if (!global_stdout) {
        global_stdout = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stdout");
        LLVMSetLinkage(global_stdout, LLVMExternalLinkage);
    }
    LLVMValueRef stdout_ptr = LLVMBuildLoad2(gen->builder, LLVMPointerType(file_type, 0), global_stdout, "stdout");
    
    // Generate format output
    generate_format_output(gen, node, stdout_ptr);
    
    // Add newline using fprintf
    LLVMValueRef fprintf_fn = LLVMGetNamedFunction(gen->module, "fprintf");
    LLVMValueRef newline_str = LLVMBuildGlobalStringPtr(gen->builder, "\n", "newline");
    LLVMValueRef args[] = {stdout_ptr, newline_str};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, args, 2, "");
    
    return NULL;
}

// Codegen callback for io.print
LLVMValueRef io_print_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;
    
    // Get stdout
    LLVMTypeRef file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
    LLVMValueRef global_stdout = LLVMGetNamedGlobal(gen->module, "__jsasta_stdout");
    if (!global_stdout) {
        global_stdout = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stdout");
        LLVMSetLinkage(global_stdout, LLVMExternalLinkage);
    }
    LLVMValueRef stdout_ptr = LLVMBuildLoad2(gen->builder, LLVMPointerType(file_type, 0), global_stdout, "stdout");
    
    // Generate format output (no newline)
    generate_format_output(gen, node, stdout_ptr);
    
    return NULL;
}

// Codegen callback for io.format
LLVMValueRef io_format_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;
    (void)node; // TODO: use node to build formatted string
    // TODO: implement proper string building
    // For now, just return empty string
    return LLVMBuildGlobalStringPtr(gen->builder, "", "formatted");
}

// Codegen callback for io.eprintln (stderr with newline)
LLVMValueRef io_eprintln_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;
    
    // Get stderr
    LLVMTypeRef file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
    LLVMValueRef global_stderr = LLVMGetNamedGlobal(gen->module, "__jsasta_stderr");
    if (!global_stderr) {
        global_stderr = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stderr");
        LLVMSetLinkage(global_stderr, LLVMExternalLinkage);
    }
    LLVMValueRef stderr_ptr = LLVMBuildLoad2(gen->builder, LLVMPointerType(file_type, 0), global_stderr, "stderr");
    
    // Generate format output
    generate_format_output(gen, node, stderr_ptr);
    
    // Add newline (using fprintf to stderr)
    LLVMValueRef fprintf_fn = LLVMGetNamedFunction(gen->module, "fprintf");
    if (!fprintf_fn) {
        LLVMTypeRef fprintf_type = LLVMFunctionType(
            LLVMInt32Type(),
            (LLVMTypeRef[]){LLVMPointerType(file_type, 0), LLVMPointerType(LLVMInt8Type(), 0)},
            2, true);
        fprintf_fn = LLVMAddFunction(gen->module, "fprintf", fprintf_type);
    }
    
    LLVMValueRef newline_str = LLVMBuildGlobalStringPtr(gen->builder, "\n", "newline");
    LLVMValueRef args[] = {stderr_ptr, newline_str};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, args, 2, "");
    
    return NULL;
}

// Codegen callback for io.eprint (stderr without newline)
LLVMValueRef io_eprint_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;
    
    // Get stderr
    LLVMTypeRef file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
    LLVMValueRef global_stderr = LLVMGetNamedGlobal(gen->module, "__jsasta_stderr");
    if (!global_stderr) {
        global_stderr = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stderr");
        LLVMSetLinkage(global_stderr, LLVMExternalLinkage);
    }
    LLVMValueRef stderr_ptr = LLVMBuildLoad2(gen->builder, LLVMPointerType(file_type, 0), global_stderr, "stderr");
    
    // Generate format output (no newline)
    generate_format_output(gen, node, stderr_ptr);
    
    return NULL;
}
