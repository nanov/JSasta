#include "common/jsasta_compiler.h"
#include "common/format_string.h"
#include "common/traits.h"
#include "common/logger.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declarations for io and test assert functions
LLVMValueRef io_eprint_codegen(void* context, ASTNode* node);
LLVMValueRef io_eprintln_codegen(void* context, ASTNode* node);
LLVMValueRef test_assert_pass_codegen(void* context, ASTNode* node);
LLVMValueRef test_assert_fail_codegen(void* context, ASTNode* node);

// Helper: get or create the FILE struct type (reuses existing if already created)
static LLVMTypeRef get_file_type(void) {
    LLVMTypeRef file_type = LLVMGetTypeByName2(LLVMGetGlobalContext(), "struct._IO_FILE");
    if (!file_type) {
        file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
    }
    return file_type;
}

// Helper: get or create stdout and return the loaded pointer
static LLVMValueRef get_stdout(CodeGen* gen) {
    LLVMTypeRef file_type = get_file_type();
    LLVMValueRef global_stdout = LLVMGetNamedGlobal(gen->module, "__jsasta_stdout");
    if (!global_stdout) {
        global_stdout = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stdout");
        LLVMSetLinkage(global_stdout, LLVMExternalLinkage);
    }
    return LLVMBuildLoad2(gen->builder, LLVMPointerType(file_type, 0), global_stdout, "stdout");
}

// Helper: get or create stderr and return the loaded pointer
static LLVMValueRef get_stderr(CodeGen* gen) {
    LLVMTypeRef file_type = get_file_type();
    LLVMValueRef global_stderr = LLVMGetNamedGlobal(gen->module, "__jsasta_stderr");
    if (!global_stderr) {
        global_stderr = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stderr");
        LLVMSetLinkage(global_stderr, LLVMExternalLinkage);
    }
    return LLVMBuildLoad2(gen->builder, LLVMPointerType(file_type, 0), global_stderr, "stderr");
}

// Helper: get or create fprintf function
static LLVMValueRef get_fprintf(CodeGen* gen) {
    LLVMValueRef fprintf_fn = LLVMGetNamedFunction(gen->module, "fprintf");
    if (!fprintf_fn) {
        LLVMTypeRef file_type = get_file_type();
        LLVMTypeRef fprintf_type = LLVMFunctionType(
            LLVMInt32Type(),
            (LLVMTypeRef[]){LLVMPointerType(file_type, 0), LLVMPointerType(LLVMInt8Type(), 0)},
            2, true);
        fprintf_fn = LLVMAddFunction(gen->module, "fprintf", fprintf_type);
    }
    return fprintf_fn;
}

// Helper: print a string literal to a stream using fprintf
static void print_string_to_stream(CodeGen* gen, LLVMValueRef stream_ptr, const char* str, const char* label_name) {
    LLVMValueRef fprintf_fn = get_fprintf(gen);
    LLVMValueRef str_ptr = LLVMBuildGlobalStringPtr(gen->builder, str, label_name);
    LLVMValueRef args[] = {stream_ptr, str_ptr};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, args, 2, "");
}

// Helper: get or create abort function
static LLVMValueRef get_abort(CodeGen* gen) {
    LLVMValueRef abort_fn = LLVMGetNamedFunction(gen->module, "abort");
    if (!abort_fn) {
        LLVMTypeRef abort_type = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
        abort_fn = LLVMAddFunction(gen->module, "abort", abort_type);
    }
    return abort_fn;
}

// Helper: call abort() and mark block as unreachable
static void call_abort(CodeGen* gen) {
    LLVMValueRef abort_fn = get_abort(gen);
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(abort_fn), abort_fn, NULL, 0, "");
    LLVMBuildUnreachable(gen->builder);
}

// Helper: print variable name with separator if node is an identifier
static void print_identifier_prefix(CodeGen* gen, ASTNode* node, LLVMValueRef stream_ptr, const char* separator) {
    if (node->type == AST_IDENTIFIER) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%s%s", node->identifier.name, separator);
        print_string_to_stream(gen, stream_ptr, buffer, "var_prefix");
    }
}

// Helper: call Display trait's fmt method to print a value to a stream
// Returns true on success, false if Display trait not found
static bool display_value_to_stream(CodeGen* gen, LLVMValueRef value, TypeInfo* type, LLVMValueRef stream_ptr) {
    // Find Display trait implementation
    TraitImpl* display_impl = trait_find_impl(Trait_Display, type, NULL, 0);
    if (!display_impl) {
        return false;
    }
    
    // Find fmt method
    MethodImpl* fmt_method = NULL;
    for (int i = 0; i < display_impl->method_count; i++) {
        if (strcmp(display_impl->methods[i].method_name, "fmt") == 0) {
            fmt_method = &display_impl->methods[i];
            break;
        }
    }
    
    if (!fmt_method) {
        return false;
    }
    
    // Special handling for enum types - generate inline switch statement
    if (type_info_is_enum(type) && fmt_method->kind == METHOD_EXTERNAL) {
        LLVMValueRef fprintf_fn = get_fprintf(gen);
        
        // Generate switch statement on enum discriminant
        int variant_count = type->data.enum_type.variant_count;
        
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
                     type->data.enum_type.variant_names[i]);
            variant_blocks[i] = LLVMAppendBasicBlock(
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
                block_name);
        }
        
        // Build switch
        LLVMValueRef switch_inst = LLVMBuildSwitch(gen->builder, value, default_block, variant_count);
        
        // Generate case for each variant
        for (int i = 0; i < variant_count; i++) {
            LLVMAddCase(switch_inst, LLVMConstInt(LLVMInt32Type(), i, false), variant_blocks[i]);
            
            LLVMPositionBuilderAtEnd(gen->builder, variant_blocks[i]);
            
            // Create format string for this variant name
            const char* variant_name = type->data.enum_type.variant_names[i];
            LLVMValueRef format_str = LLVMBuildGlobalStringPtr(gen->builder, variant_name, "variant_name_fmt");
            
            // Call fprintf(stream, variant_name)
            LLVMValueRef args[] = {stream_ptr, format_str};
            LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, args, 2, "");
            
            LLVMBuildBr(gen->builder, end_block);
        }
        
        // Default case: print "Unknown"
        LLVMPositionBuilderAtEnd(gen->builder, default_block);
        LLVMValueRef unknown_str = LLVMBuildGlobalStringPtr(gen->builder, "Unknown", "unknown_fmt");
        LLVMValueRef unknown_args[] = {stream_ptr, unknown_str};
        LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, unknown_args, 2, "");
        LLVMBuildBr(gen->builder, end_block);
        
        // End block
        LLVMPositionBuilderAtEnd(gen->builder, end_block);
        
        free(variant_blocks);
        
        return true;
    }
    
    // For external functions
    if (fmt_method->kind == METHOD_EXTERNAL) {
        if (!fmt_method->external_name) {
            return false;
        }
        
        // Create formatter struct
        LLVMTypeRef file_type = get_file_type();
        LLVMTypeRef formatter_type = LLVMStructType(
            (LLVMTypeRef[]){LLVMPointerType(file_type, 0)}, 1, false);
        LLVMValueRef formatter = LLVMBuildAlloca(gen->builder, formatter_type, "formatter");
        
        // Store stream in formatter.stream
        LLVMValueRef stream_field_ptr = LLVMBuildStructGEP2(gen->builder, formatter_type, formatter, 0, "stream_ptr");
        LLVMBuildStore(gen->builder, stream_ptr, stream_field_ptr);
        
        // Get or declare the external display function
        LLVMValueRef display_fn = LLVMGetNamedFunction(gen->module, fmt_method->external_name);
        if (!display_fn) {
            LLVMTypeRef display_fn_type = LLVMFunctionType(
                LLVMVoidType(),
                (LLVMTypeRef[]){get_llvm_type(gen, type), LLVMPointerType(formatter_type, 0)},
                2, false);
            display_fn = LLVMAddFunction(gen->module, fmt_method->external_name, display_fn_type);
        }
        
        // Call display_<type>(value, &formatter)
        LLVMValueRef display_args[] = {value, formatter};
        LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(display_fn), display_fn, display_args, 2, "");
        
        return true;
    }
    
    // For intrinsic functions
    if (fmt_method->kind == METHOD_INTRINSIC && fmt_method->codegen) {
        // Create formatter struct
        LLVMTypeRef file_type = get_file_type();
        LLVMTypeRef formatter_type = LLVMStructType(
            (LLVMTypeRef[]){LLVMPointerType(file_type, 0)}, 1, false);
        LLVMValueRef formatter = LLVMBuildAlloca(gen->builder, formatter_type, "formatter");
        
        // Store stream in formatter.stream
        LLVMValueRef stream_field_ptr = LLVMBuildStructGEP2(gen->builder, formatter_type, formatter, 0, "stream_ptr");
        LLVMBuildStore(gen->builder, stream_ptr, stream_field_ptr);
        
        // Call intrinsic codegen function
        LLVMValueRef intrinsic_args[] = {value, formatter};
        fmt_method->codegen(gen, intrinsic_args, 2, fmt_method->function_ptr);
        
        return true;
    }
    
    return false;
}

// Helper: generate format output code (shared by print/println/format)
// Returns true on success, false on error
static bool generate_format_output(CodeGen* gen, ASTNode* node, LLVMValueRef output_stream) {
    ASTNode* format_arg = node->method_call.args[0];
    const char* format_str = format_arg->string.value;

    // Parse the format string (already validated)
    FormatString* fs = format_string_parse(format_str);

    // Get fprintf function for writing to specific stream
    LLVMValueRef fprintf_fn = get_fprintf(gen);

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

            // Generate the argument value
            LLVMValueRef arg_val = codegen_node(gen, arg);
            if (!arg_val) {
                format_string_free(fs);
                return false;
            }

            // Use helper to display value to output stream
            if (!display_value_to_stream(gen, arg_val, arg_type, output_stream)) {
                format_string_free(fs);
                return false;
            }
        }
    }

    format_string_free(fs);
    return true;
}

// Codegen callback for io.println
LLVMValueRef io_println_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Get stdout
    LLVMValueRef stdout_ptr = get_stdout(gen);

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
    LLVMValueRef stdout_ptr = get_stdout(gen);

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
    LLVMValueRef stderr_ptr = get_stderr(gen);

    // Generate format output
    generate_format_output(gen, node, stderr_ptr);

    // Add newline (using fprintf to stderr)
    LLVMValueRef fprintf_fn = get_fprintf(gen);

    LLVMValueRef newline_str = LLVMBuildGlobalStringPtr(gen->builder, "\n", "newline");
    LLVMValueRef args[] = {stderr_ptr, newline_str};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, args, 2, "");

    return NULL;
}

// Codegen callback for io.eprint (stderr without newline)
LLVMValueRef io_eprint_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Get stderr
    LLVMValueRef stderr_ptr = get_stderr(gen);

    // Generate format output (no newline)
    generate_format_output(gen, node, stderr_ptr);

    return NULL;
}

// Codegen callback for debug.assert
LLVMValueRef debug_assert_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // If debug mode is not enabled, don't generate any code (becomes a no-op)
    if (!gen->enable_debug) {
        return NULL;
    }

    // Get the condition argument
    ASTNode* condition_arg = node->method_call.args[0];
    LLVMValueRef condition = codegen_node(gen, condition_arg);

    // Create blocks for assertion failure and continuation
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "assert_fail");
    LLVMBasicBlockRef continue_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "assert_continue");

    // Branch: if condition is false, go to fail_block, otherwise continue
    LLVMBuildCondBr(gen->builder, condition, continue_block, fail_block);

    // Generate fail block: reuse test_assert_fail_codegen with error message
    LLVMPositionBuilderAtEnd(gen->builder, fail_block);

    // Create a synthetic AST node with simple error message
    // (location will be added automatically by test_assert_fail_codegen)
    ASTNode msg_node = {0};
    msg_node.type = AST_STRING;
    msg_node.string.value = "Assertion failed";
    msg_node.type_info = Type_Str;
    msg_node.loc = condition_arg->loc;  // Preserve location for auto-prefix
    
    ASTNode* msg_nodes[] = {&msg_node};
    ASTNode fail_node = *node;
    fail_node.method_call.args = msg_nodes;
    fail_node.method_call.arg_count = 1;
    test_assert_fail_codegen(context, &fail_node);

    // Continue block: normal execution
    LLVMPositionBuilderAtEnd(gen->builder, continue_block);

    return NULL;
}

// test.assert.that(condition: bool, msg: string, ...): void
// Reuses assert.true (pass) and assert.false (fail) after evaluating condition
LLVMValueRef test_assert_that_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Get condition argument
    ASTNode* condition_arg = node->method_call.args[0];
    
    // Generate condition value
    LLVMValueRef condition_val = codegen_node(gen, condition_arg);

    // Create blocks for true (pass) and false (fail) branches
    LLVMBasicBlockRef pass_block = LLVMAppendBasicBlock(
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
        "assert_that_pass"
    );
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
        "assert_that_fail"
    );
    LLVMBasicBlockRef continue_block = LLVMAppendBasicBlock(
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
        "assert_that_continue"
    );

    // Branch: if condition is true, call assert.true (pass), otherwise call assert.false (fail)
    LLVMBuildCondBr(gen->builder, condition_val, pass_block, fail_block);

    // Pass block: reuse test_assert_pass_codegen
    LLVMPositionBuilderAtEnd(gen->builder, pass_block);
    test_assert_pass_codegen(context, node);
    LLVMBuildBr(gen->builder, continue_block);

    // Fail block: reuse test_assert_fail_codegen
    // If there's a message (2+ args), pass it to assert.false
    // Otherwise, call assert.false with a default message
    LLVMPositionBuilderAtEnd(gen->builder, fail_block);
    
    if (node->method_call.arg_count >= 2) {
        // We have a message, so create a new node with the message args (skip condition arg)
        ASTNode fail_node = *node;
        fail_node.method_call.args = node->method_call.args + 1;  // Skip condition (pointer arithmetic)
        fail_node.method_call.arg_count = node->method_call.arg_count - 1;
        test_assert_fail_codegen(context, &fail_node);
    } else {
        // No message provided, create a simple message
        // (location will be added automatically by test_assert_fail_codegen)
        ASTNode msg_node = {0};
        msg_node.type = AST_STRING;
        msg_node.string.value = "Assertion failed";
        msg_node.type_info = Type_Str;
        msg_node.loc = condition_arg->loc;  // Preserve location for auto-prefix
        
        ASTNode* msg_nodes[] = {&msg_node};
        ASTNode fail_node = *node;
        fail_node.method_call.args = msg_nodes;
        fail_node.method_call.arg_count = 1;
        test_assert_fail_codegen(context, &fail_node);
    }
    // Note: test_assert_fail_codegen ends with abort/unreachable, so no branch needed

    // Continue block: normal execution after pass
    LLVMPositionBuilderAtEnd(gen->builder, continue_block);

    return NULL;
}

// test.assert.fail(msg: string, ...): void
// Reuses io.eprintln to print the error message, then aborts
// Automatically adds location info before the message
LLVMValueRef test_assert_fail_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Print location info first (if available) using io.eprint
    if (node->method_call.arg_count > 0 && node->method_call.args[0]->loc.filename) {
        ASTNode* first_arg = node->method_call.args[0];
        const char* filename = first_arg->loc.filename;
        
        char location_msg[512];
        snprintf(location_msg, sizeof(location_msg), "[%s:%zu:%zu] ",
                 filename, first_arg->loc.line, first_arg->loc.column);
        
        // Create synthetic node to call io.eprint with location string
        ASTNode location_str_node = {0};
        location_str_node.type = AST_STRING;
        location_str_node.string.value = location_msg;
        location_str_node.type_info = Type_Str;
        
        ASTNode* location_args[] = {&location_str_node};
        ASTNode eprint_node = {0};
        eprint_node.type = AST_METHOD_CALL;
        eprint_node.method_call.args = location_args;
        eprint_node.method_call.arg_count = 1;
        
        // Call io.eprint to print location prefix (no newline)
        io_eprint_codegen(context, &eprint_node);
    }

    // Reuse io.eprintln to print formatted message to stderr
    io_eprintln_codegen(context, node);

    // Call abort() to terminate
    call_abort(gen);

    // Create unreachable continuation block so caller can continue generating code
    LLVMBasicBlockRef unreachable_cont = LLVMAppendBasicBlock(
        LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), 
        "assert_fail_unreachable_cont"
    );
    LLVMPositionBuilderAtEnd(gen->builder, unreachable_cont);

    return NULL;
}

// test.assert.pass(): void
LLVMValueRef test_assert_pass_codegen(void* context, ASTNode* node) {
    (void)context;
    (void)node;
    // No-op: always succeeds
    return NULL;
}

// test.assert.equals(expected: T, actual: T): void
LLVMValueRef test_assert_equals_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Get expected and actual arguments
    ASTNode* expected_arg = node->method_call.args[0];
    ASTNode* actual_arg = node->method_call.args[1];
    
    TypeInfo* arg_type = expected_arg->type_info;

    // Generate values
    LLVMValueRef expected_val = codegen_node(gen, expected_arg);
    LLVMValueRef actual_val = codegen_node(gen, actual_arg);

    // Use Eq trait for comparison (Rhs = Self for assert_equals)
    TypeInfo* type_params[] = { arg_type };
    TraitImpl* eq_impl = trait_find_impl(Trait_Eq, arg_type, type_params, 1);
    if (!eq_impl) {
        log_error("Type does not implement Eq trait for assert_equals (type: %s)", 
                 arg_type->type_name ? arg_type->type_name : "unknown");
        // For now, fall back to direct comparison for primitive integer types
        if (arg_type == Type_Bool || arg_type == Type_I8 || arg_type == Type_I16 || 
            arg_type == Type_I32 || arg_type == Type_I64) {
            LLVMValueRef is_equal = LLVMBuildICmp(gen->builder, LLVMIntEQ, expected_val, actual_val, "eq");
            
            // Create blocks for assertion failure and continuation
            LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "equals_fail");
            LLVMBasicBlockRef continue_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "equals_continue");

            // Branch: if equal, continue, otherwise fail
            LLVMBuildCondBr(gen->builder, is_equal, continue_block, fail_block);

            // Generate fail block: reuse test_assert_fail_codegen
            // Note: We can't use Display trait here since type doesn't have Eq trait
            LLVMPositionBuilderAtEnd(gen->builder, fail_block);
            
            ASTNode msg_node = {0};
            msg_node.type = AST_STRING;
            msg_node.string.value = "Assertion failed: values not equal";
            msg_node.type_info = Type_Str;
            msg_node.loc = actual_arg->loc;
            
            ASTNode* msg_nodes[] = {&msg_node};
            ASTNode fail_node = *node;
            fail_node.method_call.args = msg_nodes;
            fail_node.method_call.arg_count = 1;
            test_assert_fail_codegen(context, &fail_node);

            // Continue block
            LLVMPositionBuilderAtEnd(gen->builder, continue_block);
            return NULL;
        }
        return NULL;
    }
    
    MethodImpl* eq_method = NULL;
    for (int i = 0; i < eq_impl->method_count; i++) {
        if (strcmp(eq_impl->methods[i].method_name, "eq") == 0) {
            eq_method = &eq_impl->methods[i];
            break;
        }
    }
    
    if (!eq_method || !eq_method->codegen) {
        log_error("Eq trait implementation missing eq method");
        return NULL;
    }
    
    // Call the eq method codegen
    LLVMValueRef args[] = {expected_val, actual_val};
    LLVMValueRef is_equal = eq_method->codegen(gen, args, 2, eq_method->function_ptr);

    // Create blocks for assertion failure and continuation
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "equals_fail");
    LLVMBasicBlockRef continue_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "equals_continue");

    // Branch: if equal, continue, otherwise fail
    LLVMBuildCondBr(gen->builder, is_equal, continue_block, fail_block);

    // Generate fail block
    LLVMPositionBuilderAtEnd(gen->builder, fail_block);

    // Get stderr
    LLVMValueRef stderr_ptr = get_stderr(gen);
    LLVMTypeRef file_type = get_file_type();

    // Get fprintf function
    LLVMValueRef fprintf_fn = get_fprintf(gen);

    // Build error message with symbol name if actual is a variable
    const char* prefix = "";
    if (actual_arg->type == AST_IDENTIFIER) {
        // Include symbol name
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%s - ", actual_arg->identifier.name);
        prefix = buffer;
        LLVMValueRef prefix_str = LLVMBuildGlobalStringPtr(gen->builder, prefix, "prefix");
        LLVMValueRef prefix_args[] = {stderr_ptr, prefix_str};
        LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, prefix_args, 2, "");
    }

    // Print "Expected: "
    LLVMValueRef expected_label = LLVMBuildGlobalStringPtr(gen->builder, "Expected: ", "expected_label");
    LLVMValueRef label_args[] = {stderr_ptr, expected_label};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, label_args, 2, "");

    // Print expected value using Display trait helper
    display_value_to_stream(gen, expected_val, arg_type, stderr_ptr);

    // Print " Actual: "
    LLVMValueRef actual_label = LLVMBuildGlobalStringPtr(gen->builder, " Actual: ", "actual_label");
    LLVMValueRef actual_label_args[] = {stderr_ptr, actual_label};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, actual_label_args, 2, "");

    // Print actual value using Display trait helper
    display_value_to_stream(gen, actual_val, arg_type, stderr_ptr);

    // Print newline
    LLVMValueRef newline_str = LLVMBuildGlobalStringPtr(gen->builder, "\n", "newline");
    LLVMValueRef newline_args[] = {stderr_ptr, newline_str};
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(fprintf_fn), fprintf_fn, newline_args, 2, "");

    // Call abort() to terminate
    call_abort(gen);

    // Continue block: normal execution
    LLVMPositionBuilderAtEnd(gen->builder, continue_block);

    return NULL;
}

// Codegen callback for test.assert.not_equals - reuses code from test_assert_equals_codegen
LLVMValueRef test_assert_not_equals_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Get arguments: not_equals(not_expected: T, actual: T)
    ASTNode* not_expected_arg = node->method_call.args[0];
    ASTNode* actual_arg = node->method_call.args[1];
    
    TypeInfo* arg_type = actual_arg->type_info;

    // Generate values
    LLVMValueRef actual_val = codegen_node(gen, actual_arg);
    LLVMValueRef not_expected_val = codegen_node(gen, not_expected_arg);

    // Use Eq trait for comparison (Rhs = Self for assert_not_equals)
    TypeInfo* type_params[] = { arg_type };
    TraitImpl* eq_impl = trait_find_impl(Trait_Eq, arg_type, type_params, 1);
    if (!eq_impl) {
        log_error("Type does not implement Eq trait for assert_not_equals (type: %s)", 
                 arg_type->type_name ? arg_type->type_name : "unknown");
        return NULL;
    }
    
    MethodImpl* eq_method = NULL;
    for (int i = 0; i < eq_impl->method_count; i++) {
        if (strcmp(eq_impl->methods[i].method_name, "eq") == 0) {
            eq_method = &eq_impl->methods[i];
            break;
        }
    }
    
    if (!eq_method || !eq_method->codegen) {
        log_error("Eq trait implementation missing eq method");
        return NULL;
    }
    
    // Call the eq method codegen
    LLVMValueRef args[] = {actual_val, not_expected_val};
    LLVMValueRef is_equal = eq_method->codegen(gen, args, 2, eq_method->function_ptr);

    // Create blocks for assertion failure and continuation
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "not_equals_fail");
    LLVMBasicBlockRef continue_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "not_equals_continue");

    // Branch: if NOT equal, continue, otherwise fail (opposite of assert_equals)
    LLVMBuildCondBr(gen->builder, is_equal, fail_block, continue_block);

    // Generate fail block
    LLVMPositionBuilderAtEnd(gen->builder, fail_block);

    // Get stderr
    LLVMValueRef stderr_ptr = get_stderr(gen);

    // Print variable name if actual is an identifier
    print_identifier_prefix(gen, actual_arg, stderr_ptr, " - ");

    // Print "Not expected: "
    print_string_to_stream(gen, stderr_ptr, "Not expected: ", "not_expected_label");

    // Print the value using Display trait helper
    display_value_to_stream(gen, actual_val, arg_type, stderr_ptr);

    // Print newline
    print_string_to_stream(gen, stderr_ptr, "\n", "newline");

    // Call abort() to terminate
    call_abort(gen);

    // Continue block: normal execution
    LLVMPositionBuilderAtEnd(gen->builder, continue_block);

    return NULL;
}

// Codegen callback for test.assert (always active, not dependent on debug mode)
LLVMValueRef test_assert_codegen(void* context, ASTNode* node) {
    CodeGen* gen = (CodeGen*)context;

    // Get the condition argument
    ASTNode* condition_arg = node->method_call.args[0];
    LLVMValueRef condition = codegen_node(gen, condition_arg);

    // Create blocks for assertion failure and continuation
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "test_assert_fail");
    LLVMBasicBlockRef continue_block = LLVMAppendBasicBlock(LLVMGetBasicBlockParent(LLVMGetInsertBlock(gen->builder)), "test_assert_continue");

    // Branch: if condition is false, go to fail_block, otherwise continue
    LLVMBuildCondBr(gen->builder, condition, continue_block, fail_block);

    // Generate fail block: reuse test_assert_fail_codegen with error message
    LLVMPositionBuilderAtEnd(gen->builder, fail_block);

    // Create a synthetic AST node with simple error message
    // (location will be added automatically by test_assert_fail_codegen)
    ASTNode msg_node = {0};
    msg_node.type = AST_STRING;
    msg_node.string.value = "Test assertion failed";
    msg_node.type_info = Type_Str;
    msg_node.loc = condition_arg->loc;  // Preserve location for auto-prefix
    
    ASTNode* msg_nodes[] = {&msg_node};
    ASTNode fail_node = *node;
    fail_node.method_call.args = msg_nodes;
    fail_node.method_call.arg_count = 1;
    test_assert_fail_codegen(context, &fail_node);

    // Continue block: normal execution
    LLVMPositionBuilderAtEnd(gen->builder, continue_block);

    return NULL;
}
