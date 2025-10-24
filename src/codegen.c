#include "js_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime function registry
void codegen_register_runtime_function(CodeGen* gen, const char* name,
                                       LLVMValueRef (*handler)(CodeGen*, ASTNode*)) {
    RuntimeFunction* rf = (RuntimeFunction*)malloc(sizeof(RuntimeFunction));
    rf->name = strdup(name);
    rf->handler = handler;
    rf->next = gen->runtime_functions;
    gen->runtime_functions = rf;
}

LLVMValueRef codegen_call_runtime_function(CodeGen* gen, const char* name, ASTNode* call_node) {
    RuntimeFunction* rf = gen->runtime_functions;
    while (rf) {
        if (strcmp(rf->name, name) == 0) {
            return rf->handler(gen, call_node);
        }
        rf = rf->next;
    }
    return NULL;
}

CodeGen* codegen_create(const char* module_name) {
    CodeGen* gen = (CodeGen*)malloc(sizeof(CodeGen));
    gen->context = LLVMContextCreate();
    gen->module = LLVMModuleCreateWithNameInContext(module_name, gen->context);
    gen->builder = LLVMCreateBuilderInContext(gen->context);
    gen->symbols = symbol_table_create(NULL);
    gen->current_function = NULL;
    gen->runtime_functions = NULL;
    gen->specialization_ctx = NULL;  // Will be set during generation

    // Initialize runtime library
    runtime_init(gen);

    return gen;
}

void codegen_free(CodeGen* gen) {
    // Free runtime function registry
    RuntimeFunction* rf = gen->runtime_functions;
    while (rf) {
        RuntimeFunction* next = rf->next;
        free(rf->name);
        free(rf);
        rf = next;
    }

    symbol_table_free(gen->symbols);
    LLVMDisposeBuilder(gen->builder);
    LLVMDisposeModule(gen->module);
    LLVMContextDispose(gen->context);
    free(gen);
}

static LLVMTypeRef get_llvm_type(CodeGen* gen, ValueType type) {
    switch (type) {
        case TYPE_INT:
            return LLVMInt32TypeInContext(gen->context);
        case TYPE_DOUBLE:
            return LLVMDoubleTypeInContext(gen->context);
        case TYPE_STRING:
            return LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
        case TYPE_BOOL:
            return LLVMInt1TypeInContext(gen->context);
        case TYPE_VOID:
            return LLVMVoidTypeInContext(gen->context);
        default:
            return LLVMInt32TypeInContext(gen->context);
    }
}

static LLVMValueRef codegen_string_concat(CodeGen* gen, LLVMValueRef left, LLVMValueRef right) {
    LLVMValueRef strlen_func = LLVMGetNamedFunction(gen->module, "strlen");
    LLVMValueRef malloc_func = LLVMGetNamedFunction(gen->module, "malloc");
    LLVMValueRef strcpy_func = LLVMGetNamedFunction(gen->module, "strcpy");
    LLVMValueRef strcat_func = LLVMGetNamedFunction(gen->module, "strcat");

    // Get lengths
    LLVMValueRef len1_args[] = { left };
    LLVMValueRef len1 = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(strlen_func),
                                       strlen_func, len1_args, 1, "len1");

    LLVMValueRef len2_args[] = { right };
    LLVMValueRef len2 = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(strlen_func),
                                       strlen_func, len2_args, 1, "len2");

    // Calculate total size (len1 + len2 + 1 for null terminator)
    LLVMValueRef total = LLVMBuildAdd(gen->builder, len1, len2, "total_len");
    total = LLVMBuildAdd(gen->builder, total,
                        LLVMConstInt(LLVMInt64TypeInContext(gen->context), 1, 0), "total_size");

    // Allocate memory
    LLVMValueRef malloc_args[] = { total };
    LLVMValueRef result = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(malloc_func),
                                         malloc_func, malloc_args, 1, "concat_buf");

    // Copy strings
    LLVMValueRef strcpy_args[] = { result, left };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(strcpy_func),
                  strcpy_func, strcpy_args, 2, "");

    LLVMValueRef strcat_args[] = { result, right };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(strcat_func),
                  strcat_func, strcat_args, 2, "");

    return result;
}

static LLVMValueRef codegen_int_to_string(CodeGen* gen, LLVMValueRef value) {
    LLVMValueRef malloc_func = LLVMGetNamedFunction(gen->module, "malloc");
    LLVMValueRef sprintf_func = LLVMGetNamedFunction(gen->module, "sprintf");

    // Allocate 32 bytes for the string (enough for any 32-bit int)
    LLVMValueRef size = LLVMConstInt(LLVMInt64TypeInContext(gen->context), 32, 0);
    LLVMValueRef malloc_args[] = { size };
    LLVMValueRef buffer = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(malloc_func),
                                         malloc_func, malloc_args, 1, "int_buf");

    // Format the integer
    LLVMValueRef format = LLVMBuildGlobalStringPtr(gen->builder, "%d", "int_fmt");
    LLVMValueRef sprintf_args[] = { buffer, format, value };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(sprintf_func),
                  sprintf_func, sprintf_args, 3, "");

    return buffer;
}

static LLVMValueRef codegen_double_to_string(CodeGen* gen, LLVMValueRef value) {
    LLVMValueRef malloc_func = LLVMGetNamedFunction(gen->module, "malloc");
    LLVMValueRef sprintf_func = LLVMGetNamedFunction(gen->module, "sprintf");

    // Allocate 64 bytes for the string (enough for any double)
    LLVMValueRef size = LLVMConstInt(LLVMInt64TypeInContext(gen->context), 64, 0);
    LLVMValueRef malloc_args[] = { size };
    LLVMValueRef buffer = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(malloc_func),
                                         malloc_func, malloc_args, 1, "double_buf");

    // Format the double
    LLVMValueRef format = LLVMBuildGlobalStringPtr(gen->builder, "%f", "double_fmt");
    LLVMValueRef sprintf_args[] = { buffer, format, value };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(sprintf_func),
                  sprintf_func, sprintf_args, 3, "");

    return buffer;
}

static LLVMValueRef codegen_bool_to_string(CodeGen* gen, LLVMValueRef value) {
    // Create "true" and "false" strings
    LLVMValueRef true_str = LLVMBuildGlobalStringPtr(gen->builder, "true", "true_str");
    LLVMValueRef false_str = LLVMBuildGlobalStringPtr(gen->builder, "false", "false_str");

    // Select based on boolean value
    return LLVMBuildSelect(gen->builder, value, true_str, false_str, "bool_str");
}


LLVMValueRef codegen_node(CodeGen* gen, ASTNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_NUMBER:
            if (node->value_type == TYPE_DOUBLE) {
                return LLVMConstReal(LLVMDoubleTypeInContext(gen->context), node->number.value);
            } else {
                return LLVMConstInt(LLVMInt32TypeInContext(gen->context), (int)node->number.value, 0);
            }

        case AST_STRING:
            return LLVMBuildGlobalStringPtr(gen->builder, node->string.value, "str");

        case AST_BOOLEAN:
            return LLVMConstInt(LLVMInt1TypeInContext(gen->context), node->boolean.value, 0);

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->identifier.name);
            if (entry && entry->value) {
                return LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type),
                                     entry->value, node->identifier.name);
            }
            fprintf(stderr, "Undefined variable: %s\n", node->identifier.name);
            return NULL;
        }

        case AST_BINARY_OP: {
            LLVMValueRef left = codegen_node(gen, node->binary_op.left);
            LLVMValueRef right = codegen_node(gen, node->binary_op.right);

            // Handle string concatenation
            if (strcmp(node->binary_op.op, "+") == 0 &&
                (node->binary_op.left->value_type == TYPE_STRING ||
                 node->binary_op.right->value_type == TYPE_STRING)) {

                // Convert non-strings to strings if needed
                if (node->binary_op.left->value_type == TYPE_INT) {
                    left = codegen_int_to_string(gen, left);
                } else if (node->binary_op.left->value_type == TYPE_DOUBLE) {
                    left = codegen_double_to_string(gen, left);
                } else if (node->binary_op.left->value_type == TYPE_BOOL) {
                    left = codegen_bool_to_string(gen, left);
                }

                if (node->binary_op.right->value_type == TYPE_INT) {
                    right = codegen_int_to_string(gen, right);
                } else if (node->binary_op.right->value_type == TYPE_DOUBLE) {
                    right = codegen_double_to_string(gen, right);
                } else if (node->binary_op.right->value_type == TYPE_BOOL) {
                    right = codegen_bool_to_string(gen, right);
                }

                return codegen_string_concat(gen, left, right);
            }

            // Arithmetic operations
            if (strcmp(node->binary_op.op, "+") == 0) {
                if (node->value_type == TYPE_DOUBLE) {
                    // Convert int operands to double if needed
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFAdd(gen->builder, left, right, "addtmp");
                }
                return LLVMBuildAdd(gen->builder, left, right, "addtmp");
            } else if (strcmp(node->binary_op.op, "-") == 0) {
                if (node->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFSub(gen->builder, left, right, "subtmp");
                }
                return LLVMBuildSub(gen->builder, left, right, "subtmp");
            } else if (strcmp(node->binary_op.op, "*") == 0) {
                if (node->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFMul(gen->builder, left, right, "multmp");
                }
                return LLVMBuildMul(gen->builder, left, right, "multmp");
            } else if (strcmp(node->binary_op.op, "/") == 0) {
                if (node->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFDiv(gen->builder, left, right, "divtmp");
                }
                return LLVMBuildSDiv(gen->builder, left, right, "divtmp");
            }

            // Comparison operations
            else if (strcmp(node->binary_op.op, "<") == 0) {
                if (node->binary_op.left->value_type == TYPE_DOUBLE ||
                    node->binary_op.right->value_type == TYPE_DOUBLE) {
                    // Convert int to double if needed
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFCmp(gen->builder, LLVMRealOLT, left, right, "cmptmp");
                }
                return LLVMBuildICmp(gen->builder, LLVMIntSLT, left, right, "cmptmp");
            } else if (strcmp(node->binary_op.op, ">") == 0) {
                if (node->binary_op.left->value_type == TYPE_DOUBLE ||
                    node->binary_op.right->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFCmp(gen->builder, LLVMRealOGT, left, right, "cmptmp");
                }
                return LLVMBuildICmp(gen->builder, LLVMIntSGT, left, right, "cmptmp");
            } else if (strcmp(node->binary_op.op, "<=") == 0) {
                if (node->binary_op.left->value_type == TYPE_DOUBLE ||
                    node->binary_op.right->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFCmp(gen->builder, LLVMRealOLE, left, right, "cmptmp");
                }
                return LLVMBuildICmp(gen->builder, LLVMIntSLE, left, right, "cmptmp");
            } else if (strcmp(node->binary_op.op, ">=") == 0) {
                if (node->binary_op.left->value_type == TYPE_DOUBLE ||
                    node->binary_op.right->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFCmp(gen->builder, LLVMRealOGE, left, right, "cmptmp");
                }
                return LLVMBuildICmp(gen->builder, LLVMIntSGE, left, right, "cmptmp");
            } else if (strcmp(node->binary_op.op, "==") == 0) {
                if (node->binary_op.left->value_type == TYPE_DOUBLE ||
                    node->binary_op.right->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFCmp(gen->builder, LLVMRealOEQ, left, right, "cmptmp");
                }
                return LLVMBuildICmp(gen->builder, LLVMIntEQ, left, right, "cmptmp");
            } else if (strcmp(node->binary_op.op, "!=") == 0) {
                if (node->binary_op.left->value_type == TYPE_DOUBLE ||
                    node->binary_op.right->value_type == TYPE_DOUBLE) {
                    if (node->binary_op.left->value_type == TYPE_INT) {
                        left = LLVMBuildSIToFP(gen->builder, left,
                                              LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->binary_op.right->value_type == TYPE_INT) {
                        right = LLVMBuildSIToFP(gen->builder, right,
                                               LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    return LLVMBuildFCmp(gen->builder, LLVMRealONE, left, right, "cmptmp");
                }
                return LLVMBuildICmp(gen->builder, LLVMIntNE, left, right, "cmptmp");
            }

            // Logical operations
            else if (strcmp(node->binary_op.op, "&&") == 0) {
                return LLVMBuildAnd(gen->builder, left, right, "andtmp");
            } else if (strcmp(node->binary_op.op, "||") == 0) {
                return LLVMBuildOr(gen->builder, left, right, "ortmp");
            }

            return NULL;
        }

        case AST_UNARY_OP: {
            LLVMValueRef operand = codegen_node(gen, node->unary_op.operand);

            if (strcmp(node->unary_op.op, "-") == 0) {
                if (node->unary_op.operand->value_type == TYPE_DOUBLE) {
                    return LLVMBuildFNeg(gen->builder, operand, "negtmp");
                }
                return LLVMBuildNeg(gen->builder, operand, "negtmp");
            } else if (strcmp(node->unary_op.op, "!") == 0) {
                return LLVMBuildNot(gen->builder, operand, "nottmp");
            }

            return NULL;
        }

        case AST_VAR_DECL: {
            LLVMValueRef init_value = NULL;
            if (node->var_decl.init) {
                init_value = codegen_node(gen, node->var_decl.init);
            } else {
                init_value = LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0);
            }

            LLVMValueRef alloca = LLVMBuildAlloca(gen->builder,
                                                  get_llvm_type(gen, node->value_type),
                                                  node->var_decl.name);
            LLVMBuildStore(gen->builder, init_value, alloca);

            symbol_table_insert(gen->symbols, node->var_decl.name, node->value_type, alloca);

            return alloca;
        }

        case AST_ASSIGNMENT: {
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->assignment.name);
            if (!entry || !entry->value) {
                fprintf(stderr, "Undefined variable in assignment: %s\n", node->assignment.name);
                return NULL;
            }

            LLVMValueRef value = codegen_node(gen, node->assignment.value);
            LLVMBuildStore(gen->builder, value, entry->value);

            return value;
        }

        case AST_CALL: {
            // Check if this is a member access call (e.g., console.log())
            if (node->call.callee->type == AST_MEMBER_ACCESS) {
                ASTNode* obj = node->call.callee->member_access.object;
                char* prop = node->call.callee->member_access.property;

                // Build fully qualified name (e.g., "console.log")
                if (obj->type == AST_IDENTIFIER) {
                    char full_name[256];
                    snprintf(full_name, sizeof(full_name), "%s.%s",
                            obj->identifier.name, prop);

                    // Try runtime function first
                    LLVMValueRef result = codegen_call_runtime_function(gen, full_name, node);
                    if (result) {
                        return result;
                    }
                }

                fprintf(stderr, "Undefined method: %s.%s\n",
                        obj->type == AST_IDENTIFIER ? obj->identifier.name : "object",
                        prop);
                return NULL;
            }

            // Regular function call (identifier only)
            if (node->call.callee->type != AST_IDENTIFIER) {
                fprintf(stderr, "Invalid function call\n");
                return NULL;
            }

            const char* func_name = node->call.callee->identifier.name;

            // Generate arguments first to get their types
            LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * node->call.arg_count);
            ValueType* arg_types = (ValueType*)malloc(sizeof(ValueType) * node->call.arg_count);

            for (int i = 0; i < node->call.arg_count; i++) {
                args[i] = codegen_node(gen, node->call.args[i]);
                arg_types[i] = node->call.args[i]->value_type;
            }

            // Try to find specialized version
            LLVMValueRef func = NULL;
            if (gen->specialization_ctx && node->call.arg_count > 0) {
                FunctionSpecialization* spec = specialization_context_find(
                    gen->specialization_ctx, func_name, arg_types, node->call.arg_count);

                if (spec) {
                    // Use specialized version
                    func = LLVMGetNamedFunction(gen->module, spec->specialized_name);
                }
            }

            // Fall back to original function name if no specialization found
            if (!func) {
                func = LLVMGetNamedFunction(gen->module, func_name);
            }

            if (!func) {
                fprintf(stderr, "Undefined function: %s\n", func_name);
                free(args);
                free(arg_types);
                return NULL;
            }

            LLVMValueRef result = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(func),
                                                func, args, node->call.arg_count, "calltmp");
            free(args);
            free(arg_types);

            return result;
        }

        case AST_MEMBER_ACCESS: {
            // Member access without call - just return the object for now
            // This would be used for property reads in phase 2
            return codegen_node(gen, node->member_access.object);
        }

        case AST_RETURN: {
            if (node->return_stmt.value) {
                LLVMValueRef ret_val = codegen_node(gen, node->return_stmt.value);
                return LLVMBuildRet(gen->builder, ret_val);
            } else {
                return LLVMBuildRetVoid(gen->builder);
            }
        }

        case AST_IF: {
            LLVMValueRef cond = codegen_node(gen, node->if_stmt.condition);

            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "then");
            LLVMBasicBlockRef else_bb = node->if_stmt.else_branch ?
                LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "else") : NULL;
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "ifcont");

            if (else_bb) {
                LLVMBuildCondBr(gen->builder, cond, then_bb, else_bb);
            } else {
                LLVMBuildCondBr(gen->builder, cond, then_bb, merge_bb);
            }

            // Then branch
            LLVMPositionBuilderAtEnd(gen->builder, then_bb);
            codegen_node(gen, node->if_stmt.then_branch);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
                LLVMBuildBr(gen->builder, merge_bb);
            }

            // Else branch
            if (else_bb) {
                LLVMPositionBuilderAtEnd(gen->builder, else_bb);
                codegen_node(gen, node->if_stmt.else_branch);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
                    LLVMBuildBr(gen->builder, merge_bb);
                }
            }

            LLVMPositionBuilderAtEnd(gen->builder, merge_bb);

            return NULL;
        }

        case AST_WHILE: {
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "whilecond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "whilebody");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "whileend");

            LLVMBuildBr(gen->builder, cond_bb);

            // Condition
            LLVMPositionBuilderAtEnd(gen->builder, cond_bb);
            LLVMValueRef cond = codegen_node(gen, node->while_stmt.condition);
            LLVMBuildCondBr(gen->builder, cond, body_bb, end_bb);

            // Body
            LLVMPositionBuilderAtEnd(gen->builder, body_bb);
            codegen_node(gen, node->while_stmt.body);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
                LLVMBuildBr(gen->builder, cond_bb);
            }

            LLVMPositionBuilderAtEnd(gen->builder, end_bb);

            return NULL;
        }

        case AST_FOR: {
            // Initialize
            if (node->for_stmt.init) {
                codegen_node(gen, node->for_stmt.init);
            }

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "forcond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "forbody");
            LLVMBasicBlockRef update_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "forupdate");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "forend");

            LLVMBuildBr(gen->builder, cond_bb);

            // Condition
            LLVMPositionBuilderAtEnd(gen->builder, cond_bb);
            if (node->for_stmt.condition) {
                LLVMValueRef cond = codegen_node(gen, node->for_stmt.condition);
                LLVMBuildCondBr(gen->builder, cond, body_bb, end_bb);
            } else {
                LLVMBuildBr(gen->builder, body_bb);
            }

            // Body
            LLVMPositionBuilderAtEnd(gen->builder, body_bb);
            codegen_node(gen, node->for_stmt.body);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
                LLVMBuildBr(gen->builder, update_bb);
            }

            // Update
            LLVMPositionBuilderAtEnd(gen->builder, update_bb);
            if (node->for_stmt.update) {
                codegen_node(gen, node->for_stmt.update);
            }
            LLVMBuildBr(gen->builder, cond_bb);

            LLVMPositionBuilderAtEnd(gen->builder, end_bb);

            return NULL;
        }

        case AST_FUNCTION_DECL: {
            // Save current builder position (we're generating at module level)
            LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(gen->builder);

            // Create parameter types array
            LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * node->func_decl.param_count);
            for (int i = 0; i < node->func_decl.param_count; i++) {
                // Default to int for unknown types
                param_types[i] = get_llvm_type(gen,
                    node->func_decl.param_types[i] != TYPE_UNKNOWN ?
                    node->func_decl.param_types[i] : TYPE_INT);
            }

            // Create function type
            LLVMTypeRef ret_type = get_llvm_type(gen,
                node->func_decl.return_type != TYPE_UNKNOWN ?
                node->func_decl.return_type : TYPE_INT);

            LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types,
                                                     node->func_decl.param_count, 0);

            // Create function
            LLVMValueRef func = LLVMAddFunction(gen->module, node->func_decl.name, func_type);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(gen->context, func, "entry");
            LLVMPositionBuilderAtEnd(gen->builder, entry);

            // Save previous function and scope
            LLVMValueRef prev_func = gen->current_function;
            gen->current_function = func;

            SymbolTable* func_scope = symbol_table_create(gen->symbols);
            SymbolTable* prev_scope = gen->symbols;
            gen->symbols = func_scope;

            // Store parameters
            for (int i = 0; i < node->func_decl.param_count; i++) {
                LLVMValueRef param = LLVMGetParam(func, i);
                LLVMSetValueName(param, node->func_decl.params[i]);

                LLVMValueRef alloca = LLVMBuildAlloca(gen->builder, param_types[i],
                                                      node->func_decl.params[i]);
                LLVMBuildStore(gen->builder, param, alloca);

                ValueType param_type = node->func_decl.param_types[i] != TYPE_UNKNOWN ?
                                      node->func_decl.param_types[i] : TYPE_INT;
                symbol_table_insert(gen->symbols, node->func_decl.params[i], param_type, alloca);
            }

            // Generate body
            codegen_node(gen, node->func_decl.body);

            // Add return if missing
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
                if (ret_type == LLVMVoidTypeInContext(gen->context)) {
                    LLVMBuildRetVoid(gen->builder);
                } else {
                    LLVMBuildRet(gen->builder, LLVMConstInt(ret_type, 0, 0));
                }
            }

            // Restore scope and function
            symbol_table_free(func_scope);
            gen->symbols = prev_scope;
            gen->current_function = prev_func;

            // CRITICAL: Restore builder position to where we were before the function
            // This prevents the builder from being stuck in a terminated block
            if (saved_block) {
                LLVMPositionBuilderAtEnd(gen->builder, saved_block);
            }

            free(param_types);

            return func;
        }

        case AST_BLOCK:
        case AST_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                // Check if current block is already terminated
                LLVMBasicBlockRef current_block = LLVMGetInsertBlock(gen->builder);
                if (current_block && LLVMGetBasicBlockTerminator(current_block)) {
                    // Block already terminated, skip remaining statements
                    break;
                }
                codegen_node(gen, node->program.statements[i]);
            }
            return NULL;

        case AST_EXPR_STMT:
            return codegen_node(gen, node->expr_stmt.expression);

        default:
            return NULL;
    }
}

// Helper: Generate a specialstatic
LLVMValueRef codegen_specialized_function(CodeGen* gen, FunctionSpecialization* spec) {
    // CRITICAL: Use the cloned AST from spec->specialized_body, not the original!
    if (!spec->specialized_body) {
        fprintf(stderr, "Error: No specialized body for %s\n", spec->specialized_name);
        return NULL;
    }

    ASTNode* specialized_node = spec->specialized_body;

    // Get the already-declared function
    LLVMValueRef func = LLVMGetNamedFunction(gen->module, spec->specialized_name);
    if (!func) {
        fprintf(stderr, "Error: Function %s not declared\n", spec->specialized_name);
        return NULL;
    }

    // Create parameter types for symbol table (from specialized types)
    LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * spec->param_count);
    for (int i = 0; i < spec->param_count; i++) {
        param_types[i] = get_llvm_type(gen, spec->param_types[i]);
    }

    // Create entry block
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(gen->context, func, "entry");
    LLVMPositionBuilderAtEnd(gen->builder, entry);

    // Save previous function and scope
    LLVMValueRef prev_func = gen->current_function;
    gen->current_function = func;

    SymbolTable* func_scope = symbol_table_create(gen->symbols);
    SymbolTable* prev_scope = gen->symbols;
    gen->symbols = func_scope;

    // Store parameters with specialized types from the CLONED AST
    for (int i = 0; i < spec->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(func, i);
        LLVMSetValueName(param, specialized_node->func_decl.params[i]);

        LLVMValueRef alloca = LLVMBuildAlloca(gen->builder, param_types[i],
                                              specialized_node->func_decl.params[i]);
        LLVMBuildStore(gen->builder, param, alloca);

        symbol_table_insert(gen->symbols, specialized_node->func_decl.params[i],
                          spec->param_types[i], alloca);
    }

    // Generate body from CLONED AST (this is the key change!)
    // The cloned body has correct type annotations from type analysis
    codegen_node(gen, specialized_node->func_decl.body);

    // Add return if missing
    LLVMTypeRef ret_type = get_llvm_type(gen, spec->return_type);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
        if (ret_type == LLVMVoidTypeInContext(gen->context)) {
            LLVMBuildRetVoid(gen->builder);
        } else {
            LLVMBuildRet(gen->builder, LLVMConstInt(ret_type, 0, 0));
        }
    }

    // Restore scope and function
    symbol_table_free(func_scope);
    gen->symbols = prev_scope;
    gen->current_function = prev_func;

    free(param_types);

    return func;
}

void codegen_generate(CodeGen* gen, ASTNode* ast) {
    // Store specialization context from type inference
    gen->specialization_ctx = ast->specialization_ctx;

    // PASS 1: Declare all function prototypes first
    // This allows forward references and recursive calls
    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.statements[i];

            if (stmt->type == AST_FUNCTION_DECL && gen->specialization_ctx) {
                // Iterate through ALL specializations and declare matching ones
                FunctionSpecialization* spec = gen->specialization_ctx->specializations;

                while (spec) {
                    // Check if this specialization is for our function
                    if (strcmp(spec->function_name, stmt->func_decl.name) == 0) {
                        // Verify we have a specialized body
                        if (!spec->specialized_body) {
                            fprintf(stderr, "Warning: Specialization %s has no body\n",
                                   spec->specialized_name);
                            spec = spec->next;
                            continue;
                        }

                        // Create parameter types
                        LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * spec->param_count);
                        for (int j = 0; j < spec->param_count; j++) {
                            param_types[j] = get_llvm_type(gen, spec->param_types[j]);
                        }

                        // Create return type
                        LLVMTypeRef ret_type = get_llvm_type(gen, spec->return_type);

                        // Create function type
                        LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, spec->param_count, 0);

                        // Declare function (just add to module, don't generate body yet)
                        LLVMAddFunction(gen->module, spec->specialized_name, func_type);

                        printf("  Declared: %s with %d params\n", spec->specialized_name, spec->param_count);

                        free(param_types);
                    }

                    spec = spec->next;
                }
            }
        }
    }

    // Create main function
    LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32TypeInContext(gen->context), NULL, 0, 0);
    LLVMValueRef main_func = LLVMAddFunction(gen->module, "main", main_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(gen->context, main_func, "entry");
    LLVMPositionBuilderAtEnd(gen->builder, entry);

    gen->current_function = main_func;

    // PASS 2: Generate function bodies
    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.statements[i];

            // For function declarations, generate specialized versions
            if (stmt->type == AST_FUNCTION_DECL && gen->specialization_ctx) {
                // Iterate through ALL specializations and generate matching ones
                FunctionSpecialization* spec = gen->specialization_ctx->specializations;
                bool found_any = false;

                while (spec) {
                    // Check if this specialization is for our function
                    if (strcmp(spec->function_name, stmt->func_decl.name) == 0) {
                        found_any = true;
                        printf("  Generating: %s (using cloned AST)\n", spec->specialized_name);

                        // CRITICAL CHANGE: Pass only spec, not the original node
                        // The function will use spec->specialized_body instead
                        codegen_specialized_function(gen, spec);

                        // Restore builder to main
                        LLVMPositionBuilderAtEnd(gen->builder, entry);
                    }

                    spec = spec->next;
                }

                if (!found_any) {
                    // No specializations, generate default version
                    codegen_node(gen, stmt);
                    if (stmt->type == AST_FUNCTION_DECL) {
                        LLVMPositionBuilderAtEnd(gen->builder, entry);
                    }
                }
            } else {
                // Generate the statement normally
                codegen_node(gen, stmt);

                // After function declarations, ensure we're back in main
                if (stmt->type == AST_FUNCTION_DECL) {
                    LLVMPositionBuilderAtEnd(gen->builder, entry);
                }
            }

            // Stop if main block is already terminated
            if (LLVMGetBasicBlockTerminator(entry)) {
                break;
            }
        }
    } else {
        codegen_node(gen, ast);
    }

    // Add return 0 if not present
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
        LLVMBuildRet(gen->builder, LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0));
    }
}

void codegen_emit_llvm_ir(CodeGen* gen, const char* filename) {
    char* error = NULL;
    if (LLVMPrintModuleToFile(gen->module, filename, &error) != 0) {
        fprintf(stderr, "Error writing LLVM IR: %s\n", error);
        LLVMDisposeMessage(error);
    }
}
