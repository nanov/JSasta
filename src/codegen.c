#include "jsasta_compiler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Runtime function registry
void codegen_register_runtime_function(CodeGen* gen, const char* name, ValueType return_type,
                                       LLVMValueRef (*handler)(CodeGen*, ASTNode*)) {
    RuntimeFunction* rf = (RuntimeFunction*)malloc(sizeof(RuntimeFunction));
    rf->name = strdup(name);
    rf->return_type = return_type;
    rf->handler = handler;
    rf->next = gen->runtime_functions;
    gen->runtime_functions = rf;
}

ValueType codegen_get_runtime_function_type(CodeGen* gen, const char* name) {
    RuntimeFunction* rf = gen->runtime_functions;
    while (rf) {
        if (strcmp(rf->name, name) == 0) {
            return rf->return_type;
        }
        rf = rf->next;
    }
    return TYPE_UNKNOWN;
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
        case TYPE_ARRAY_INT:
            // Pointer to int32 (we'll use dynamic sizing)
            return LLVMPointerType(LLVMInt32TypeInContext(gen->context), 0);
        case TYPE_ARRAY_DOUBLE:
            // Pointer to double
            return LLVMPointerType(LLVMDoubleTypeInContext(gen->context), 0);
        case TYPE_ARRAY_STRING:
            // Pointer to pointer (array of strings)
            return LLVMPointerType(LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0), 0);
        case TYPE_OBJECT:
            // For objects, we use opaque pointers (the actual struct type is determined per object)
            return LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
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
            log_error_at(&node->loc, "Undefined variable: %s", node->identifier.name);
            return NULL;
        }

        case AST_BINARY_OP: {
            LLVMValueRef left = codegen_node(gen, node->binary_op.left);
            LLVMValueRef right = codegen_node(gen, node->binary_op.right);

            if (strcmp(node->binary_op.op, ">>") == 0 &&
                (node->binary_op.left->value_type == TYPE_INT ||
                 node->binary_op.right->value_type == TYPE_INT)) {
                 return LLVMBuildAShr(gen->builder, left, right, "ashrtmp");
            }
            if (strcmp(node->binary_op.op, "<<") == 0 &&
                (node->binary_op.left->value_type == TYPE_INT ||
                 node->binary_op.right->value_type == TYPE_INT)) {
                 return LLVMBuildShl(gen->builder, left, right, "shltmp");
            }
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
		        } else if (strcmp(node->binary_op.op, "&") == 0) {
								return LLVMBuildAnd(gen->builder, left, right, "bandtmp");;
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

        case AST_PREFIX_OP: {
            // ++i or --i: increment/decrement, then return new value
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->prefix_op.name);
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in prefix operator: %s", node->prefix_op.name);
                return NULL;
            }

            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot modify const variable: %s", node->prefix_op.name);
                return NULL;
            }

            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type),
                                                  entry->value, node->prefix_op.name);
            LLVMValueRef one;
            LLVMValueRef new_value;

            if (entry->type == TYPE_DOUBLE) {
                one = LLVMConstReal(LLVMDoubleTypeInContext(gen->context), 1.0);
                if (strcmp(node->prefix_op.op, "++") == 0) {
                    new_value = LLVMBuildFAdd(gen->builder, current, one, "preinc");
                } else {
                    new_value = LLVMBuildFSub(gen->builder, current, one, "predec");
                }
            } else {
                one = LLVMConstInt(get_llvm_type(gen, entry->type), 1, 0);
                if (strcmp(node->prefix_op.op, "++") == 0) {
                    new_value = LLVMBuildAdd(gen->builder, current, one, "preinc");
                } else {
                    new_value = LLVMBuildSub(gen->builder, current, one, "predec");
                }
            }

            LLVMBuildStore(gen->builder, new_value, entry->value);
            return new_value;  // Return the new value
        }

        case AST_POSTFIX_OP: {
            // i++ or i--: return old value, then increment/decrement
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->postfix_op.name);
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in postfix operator: %s", node->postfix_op.name);
                return NULL;
            }

            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot modify const variable: %s", node->postfix_op.name);
                return NULL;
            }

            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type),
                                                  entry->value, node->postfix_op.name);
            LLVMValueRef one;
            LLVMValueRef new_value;

            if (entry->type == TYPE_DOUBLE) {
                one = LLVMConstReal(LLVMDoubleTypeInContext(gen->context), 1.0);
                if (strcmp(node->postfix_op.op, "++") == 0) {
                    new_value = LLVMBuildFAdd(gen->builder, current, one, "postinc");
                } else {
                    new_value = LLVMBuildFSub(gen->builder, current, one, "postdec");
                }
            } else {
                one = LLVMConstInt(get_llvm_type(gen, entry->type), 1, 0);
                if (strcmp(node->postfix_op.op, "++") == 0) {
                    new_value = LLVMBuildAdd(gen->builder, current, one, "postinc");
                } else {
                    new_value = LLVMBuildSub(gen->builder, current, one, "postdec");
                }
            }

            LLVMBuildStore(gen->builder, new_value, entry->value);
            return current;  // Return the old value
        }

        case AST_VAR_DECL: {
            // Special handling for function references
            if (node->value_type == TYPE_FUNCTION && node->var_decl.init &&
                node->var_decl.init->type == AST_IDENTIFIER) {
                // Look up the function being referenced
                const char* func_name = node->var_decl.init->identifier.name;
                SymbolEntry* func_entry = symbol_table_lookup(gen->symbols, func_name);

                if (!func_entry || func_entry->type != TYPE_FUNCTION) {
                    log_error_at(&node->loc, "Cannot assign non-function to function variable: %s", func_name);
                    return NULL;
                }

                // Store the function declaration node in the variable's symbol entry
                SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
                entry->name = strdup(node->var_decl.name);
                entry->type = TYPE_FUNCTION;
                entry->is_const = node->var_decl.is_const;
                entry->node = func_entry->node;  // Store the func_decl node
                entry->next = gen->symbols->head;
                gen->symbols->head = entry;
                return NULL;
            }

            // Special handling for objects - they already return a pointer from AST_OBJECT_LITERAL
            if (node->value_type == TYPE_OBJECT && node->var_decl.init &&
                node->var_decl.init->type == AST_OBJECT_LITERAL) {

                // Generate the object literal first
                LLVMValueRef obj_ptr = codegen_node(gen, node->var_decl.init);

                // Build the struct type to store with the symbol
                ASTNode* obj_lit = node->var_decl.init;
                int prop_count = obj_lit->object_literal.count;
                LLVMTypeRef* field_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * prop_count);
                for (int i = 0; i < prop_count; i++) {
                    field_types[i] = get_llvm_type(gen, obj_lit->object_literal.values[i]->value_type);
                }
                LLVMTypeRef struct_type = LLVMStructTypeInContext(gen->context, field_types, prop_count, 0);
                free(field_types);

                // Store in symbol table with both value and node
                SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
                entry->name = strdup(node->var_decl.name);
                entry->type = TYPE_OBJECT;
                entry->is_const = node->var_decl.is_const;
                entry->value = obj_ptr;          // LLVM pointer to the struct
                entry->node = node;              // AST node for property lookup
                entry->llvm_type = struct_type;  // Struct type for GEP
                entry->next = gen->symbols->head;
                gen->symbols->head = entry;

                return obj_ptr;
            }

            // Regular variable handling
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
            symbol_table_insert(gen->symbols, node->var_decl.name, node->value_type, alloca, node->var_decl.is_const);

            return alloca;
        }

        case AST_ASSIGNMENT: {
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->assignment.name);
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in assignment: %s", node->assignment.name);
                return NULL;
            }

            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot assign to const variable: %s", node->assignment.name);
                return NULL;
            }

            LLVMValueRef value = codegen_node(gen, node->assignment.value);
            LLVMBuildStore(gen->builder, value, entry->value);

            return value;
        }

        case AST_COMPOUND_ASSIGNMENT: {
            // myVar += 1 is equivalent to myVar = myVar + 1
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->compound_assignment.name);
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in compound assignment: %s", node->compound_assignment.name);
                return NULL;
            }

            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot assign to const variable: %s", node->compound_assignment.name);
                return NULL;
            }

            // Load current value
            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type),
                                                  entry->value, node->compound_assignment.name);

            // Generate the right-hand side value
            LLVMValueRef rhs = codegen_node(gen, node->compound_assignment.value);

            // Perform the operation
            LLVMValueRef new_value;
            const char* op = node->compound_assignment.op;

            if (strcmp(op, "+=") == 0) {
                if (entry->type == TYPE_DOUBLE || node->compound_assignment.value->value_type == TYPE_DOUBLE) {
                    // Convert to double if needed
                    if (entry->type == TYPE_INT) {
                        current = LLVMBuildSIToFP(gen->builder, current,
                                                 LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->compound_assignment.value->value_type == TYPE_INT) {
                        rhs = LLVMBuildSIToFP(gen->builder, rhs,
                                             LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    new_value = LLVMBuildFAdd(gen->builder, current, rhs, "addassign");
                    // Convert back to int if variable is int type
                    if (entry->type == TYPE_INT) {
                        new_value = LLVMBuildFPToSI(gen->builder, new_value,
                                                   LLVMInt32TypeInContext(gen->context), "doubletoint");
                    }
                } else {
                    new_value = LLVMBuildAdd(gen->builder, current, rhs, "addassign");
                }
            } else if (strcmp(op, "-=") == 0) {
                if (entry->type == TYPE_DOUBLE || node->compound_assignment.value->value_type == TYPE_DOUBLE) {
                    if (entry->type == TYPE_INT) {
                        current = LLVMBuildSIToFP(gen->builder, current,
                                                 LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->compound_assignment.value->value_type == TYPE_INT) {
                        rhs = LLVMBuildSIToFP(gen->builder, rhs,
                                             LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    new_value = LLVMBuildFSub(gen->builder, current, rhs, "subassign");
                    // Convert back to int if variable is int type
                    if (entry->type == TYPE_INT) {
                        new_value = LLVMBuildFPToSI(gen->builder, new_value,
                                                   LLVMInt32TypeInContext(gen->context), "doubletoint");
                    }
                } else {
                    new_value = LLVMBuildSub(gen->builder, current, rhs, "subassign");
                }
            } else if (strcmp(op, "*=") == 0) {
                if (entry->type == TYPE_DOUBLE || node->compound_assignment.value->value_type == TYPE_DOUBLE) {
                    if (entry->type == TYPE_INT) {
                        current = LLVMBuildSIToFP(gen->builder, current,
                                                 LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->compound_assignment.value->value_type == TYPE_INT) {
                        rhs = LLVMBuildSIToFP(gen->builder, rhs,
                                             LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    new_value = LLVMBuildFMul(gen->builder, current, rhs, "mulassign");
                    // Convert back to int if variable is int type
                    if (entry->type == TYPE_INT) {
                        new_value = LLVMBuildFPToSI(gen->builder, new_value,
                                                   LLVMInt32TypeInContext(gen->context), "doubletoint");
                    }
                } else {
                    new_value = LLVMBuildMul(gen->builder, current, rhs, "mulassign");
                }
            } else if (strcmp(op, "/=") == 0) {
                if (entry->type == TYPE_DOUBLE || node->compound_assignment.value->value_type == TYPE_DOUBLE) {
                    if (entry->type == TYPE_INT) {
                        current = LLVMBuildSIToFP(gen->builder, current,
                                                 LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    if (node->compound_assignment.value->value_type == TYPE_INT) {
                        rhs = LLVMBuildSIToFP(gen->builder, rhs,
                                             LLVMDoubleTypeInContext(gen->context), "inttodouble");
                    }
                    new_value = LLVMBuildFDiv(gen->builder, current, rhs, "divassign");
                    // Convert back to int if variable is int type
                    if (entry->type == TYPE_INT) {
                        new_value = LLVMBuildFPToSI(gen->builder, new_value,
                                                   LLVMInt32TypeInContext(gen->context), "doubletoint");
                    }
                } else {
                    new_value = LLVMBuildSDiv(gen->builder, current, rhs, "divassign");
                }
            } else {
                log_error_at(&node->loc, "Unknown compound assignment operator: %s", op);
                return NULL;
            }

            // Store the result back
            LLVMBuildStore(gen->builder, new_value, entry->value);

            return new_value;
        }

        case AST_INDEX_ASSIGNMENT: {
            LLVMValueRef index = codegen_node(gen, node->index_assignment.index);
            LLVMValueRef value = codegen_node(gen, node->index_assignment.value);

            // Only support array assignment, not string assignment
            if (node->index_assignment.object->value_type == TYPE_STRING) {
                log_error("String index assignment is not supported. Strings are immutable");
                return NULL;
            }

            // Array assignment: modify element at index
            LLVMValueRef object = codegen_node(gen, node->index_assignment.object);

            LLVMTypeRef elem_type;
            if (node->index_assignment.object->value_type == TYPE_ARRAY_INT) {
                elem_type = LLVMInt32TypeInContext(gen->context);
            } else if (node->index_assignment.object->value_type == TYPE_ARRAY_DOUBLE) {
                elem_type = LLVMDoubleTypeInContext(gen->context);
            } else if (node->index_assignment.object->value_type == TYPE_ARRAY_STRING) {
                elem_type = LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
            } else {
                elem_type = LLVMInt32TypeInContext(gen->context);
            }

            LLVMValueRef elem_ptr = LLVMBuildGEP2(gen->builder, elem_type, object,
                                                   &index, 1, "elem_ptr");
            LLVMBuildStore(gen->builder, value, elem_ptr);
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

                log_error_at(&node->loc, "Undefined method: %s.%s",
                        obj->type == AST_IDENTIFIER ? obj->identifier.name : "object",
                        prop);
                return NULL;
            }

            // Regular function call (identifier only)
            if (node->call.callee->type != AST_IDENTIFIER) {
                log_error("Invalid function call");
                return NULL;
            }

            const char* func_name = node->call.callee->identifier.name;

            // Check if this is a function variable (e.g., var a = print; a("text");)
            SymbolEntry* callee_entry = symbol_table_lookup(gen->symbols, func_name);
            if (callee_entry && callee_entry->type == TYPE_FUNCTION && callee_entry->node &&
                callee_entry->node->type == AST_FUNCTION_DECL) {
                // It's a function variable - use the actual function name from the func_decl
                func_name = callee_entry->node->func_decl.name;
            }

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

            // If still not found, try runtime builtin functions
            if (!func) {
                LLVMValueRef runtime_result = codegen_call_runtime_function(gen, func_name, node);
                if (runtime_result) {
                    free(args);
                    free(arg_types);
                    return runtime_result;
                }

                // Function not found anywhere
                log_error_at(&node->loc, "Undefined function: %s", func_name);
                free(args);
                free(arg_types);
                return NULL;
            }

            // Don't name void function calls
            const char* call_name = (node->value_type == TYPE_VOID) ? "" : "calltmp";
            LLVMValueRef result = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(func),
                                                func, args, node->call.arg_count, call_name);
            free(args);
            free(arg_types);

            return result;
        }

        case AST_MEMBER_ACCESS: {
            // Get the object (should be a pointer to struct)
            LLVMValueRef obj_ptr = NULL;
            ASTNode* obj_node = node->member_access.object;

            // Handle identifier case - load from symbol table
            if (obj_node->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->value) {
                    obj_ptr = entry->value;
                } else {
                    log_error_at(&node->loc, "Undefined variable: %s", obj_node->identifier.name);
                    return NULL;
                }
            } else {
                // Direct object literal
                obj_ptr = codegen_node(gen, obj_node);
                if (!obj_ptr) {
                    log_error_at(&node->loc, "Failed to generate code for object");
                    return NULL;
                }
            }

            // Find the property index in the original object literal
            ASTNode* obj_literal = NULL;
            if (obj_node->type == AST_IDENTIFIER) {
                // Look up the variable to find its initialization
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->node && entry->node->type == AST_VAR_DECL) {
                    obj_literal = entry->node->var_decl.init;
                }
            } else if (obj_node->type == AST_OBJECT_LITERAL) {
                obj_literal = obj_node;
            }

            if (!obj_literal || obj_literal->type != AST_OBJECT_LITERAL) {
                log_error_at(&node->loc, "Cannot access property of non-object");
                return NULL;
            }

            // Find the property index
            int prop_index = -1;
            for (int i = 0; i < obj_literal->object_literal.count; i++) {
                if (strcmp(obj_literal->object_literal.keys[i], node->member_access.property) == 0) {
                    prop_index = i;
                    break;
                }
            }

            if (prop_index == -1) {
                log_error_at(&node->loc, "Property '%s' not found in object", node->member_access.property);
                return NULL;
            }

            // Get the struct type from the symbol table (stored when object was created)
            LLVMTypeRef struct_type = NULL;
            if (obj_node->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->llvm_type) {
                    struct_type = entry->llvm_type;
                }
            }

            if (!struct_type) {
                log_error_at(&node->loc, "Could not find struct type for object");
                return NULL;
            }

            // Get the type of this property
            ValueType prop_type = obj_literal->object_literal.values[prop_index]->value_type;
            LLVMTypeRef prop_llvm_type = get_llvm_type(gen, prop_type);

            // Use GEP to get pointer to the field
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(gen->builder, struct_type, obj_ptr,
                                                          (unsigned)prop_index, "field_ptr");

            // Load the value
            return LLVMBuildLoad2(gen->builder, prop_llvm_type, field_ptr, "field_value");
        }

        case AST_MEMBER_ASSIGNMENT: {
            // Get the object (should be a pointer to struct)
            LLVMValueRef obj_ptr = NULL;
            ASTNode* obj_node = node->member_assignment.object;

            // Handle identifier case - load from symbol table
            if (obj_node->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->value) {
                    obj_ptr = entry->value;
                } else {
                    log_error_at(&node->loc, "Undefined variable: %s", obj_node->identifier.name);
                    return NULL;
                }
            } else {
                // Direct object literal
                obj_ptr = codegen_node(gen, obj_node);
                if (!obj_ptr) {
                    log_error_at(&node->loc, "Failed to generate code for object");
                    return NULL;
                }
            }

            // Find the property index in the original object literal
            ASTNode* obj_literal = NULL;
            if (obj_node->type == AST_IDENTIFIER) {
                // Look up the variable to find its initialization
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->node && entry->node->type == AST_VAR_DECL) {
                    obj_literal = entry->node->var_decl.init;
                }
            } else if (obj_node->type == AST_OBJECT_LITERAL) {
                obj_literal = obj_node;
            }

            if (!obj_literal || obj_literal->type != AST_OBJECT_LITERAL) {
                log_error_at(&node->loc, "Cannot access property of non-object");
                return NULL;
            }

            // Find the property index
            int prop_index = -1;
            for (int i = 0; i < obj_literal->object_literal.count; i++) {
                if (strcmp(obj_literal->object_literal.keys[i], node->member_assignment.property) == 0) {
                    prop_index = i;
                    break;
                }
            }

            if (prop_index == -1) {
                log_error_at(&node->loc, "Property '%s' not found in object", node->member_assignment.property);
                return NULL;
            }

            // Get the struct type from the symbol table (stored when object was created)
            LLVMTypeRef struct_type = NULL;
            if (obj_node->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->llvm_type) {
                    struct_type = entry->llvm_type;
                }
            }

            if (!struct_type) {
                log_error_at(&node->loc, "Could not find struct type for object");
                return NULL;
            }

            // Generate the value to store
            LLVMValueRef value = codegen_node(gen, node->member_assignment.value);

            // Use GEP to get pointer to the field
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(gen->builder, struct_type, obj_ptr,
                                                          (unsigned)prop_index, "field_ptr");

            // Store the value
            LLVMBuildStore(gen->builder, value, field_ptr);

            return value;
        }

        case AST_TERNARY: {
            // Generate condition
            LLVMValueRef cond = codegen_node(gen, node->ternary.condition);

            // Create basic blocks for true and false branches
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "ternary_true");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "ternary_false");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(gen->context, gen->current_function, "ternary_merge");

            // Branch based on condition
            LLVMBuildCondBr(gen->builder, cond, then_bb, else_bb);

            // Generate true branch
            LLVMPositionBuilderAtEnd(gen->builder, then_bb);
            LLVMValueRef true_val = codegen_node(gen, node->ternary.true_expr);

            // Type conversion if needed
            if (node->value_type == TYPE_DOUBLE && node->ternary.true_expr->value_type == TYPE_INT) {
                true_val = LLVMBuildSIToFP(gen->builder, true_val,
                                          LLVMDoubleTypeInContext(gen->context), "inttodouble");
            }

            LLVMBasicBlockRef then_end_bb = LLVMGetInsertBlock(gen->builder);
            LLVMBuildBr(gen->builder, merge_bb);

            // Generate false branch
            LLVMPositionBuilderAtEnd(gen->builder, else_bb);
            LLVMValueRef false_val = codegen_node(gen, node->ternary.false_expr);

            // Type conversion if needed
            if (node->value_type == TYPE_DOUBLE && node->ternary.false_expr->value_type == TYPE_INT) {
                false_val = LLVMBuildSIToFP(gen->builder, false_val,
                                           LLVMDoubleTypeInContext(gen->context), "inttodouble");
            }

            LLVMBasicBlockRef else_end_bb = LLVMGetInsertBlock(gen->builder);
            LLVMBuildBr(gen->builder, merge_bb);

            // Merge block with PHI node
            LLVMPositionBuilderAtEnd(gen->builder, merge_bb);
            LLVMTypeRef result_type = get_llvm_type(gen, node->value_type);
            LLVMValueRef phi = LLVMBuildPhi(gen->builder, result_type, "ternary_result");

            LLVMValueRef incoming_values[] = { true_val, false_val };
            LLVMBasicBlockRef incoming_blocks[] = { then_end_bb, else_end_bb };
            LLVMAddIncoming(phi, incoming_values, incoming_blocks, 2);

            return phi;
        }

        case AST_ARRAY_LITERAL: {
            // Allocate memory for array on heap
            int elem_count = node->array_literal.count;
            LLVMValueRef malloc_func = LLVMGetNamedFunction(gen->module, "malloc");

            // Determine element type and size
            LLVMTypeRef elem_type;
            int elem_size;
            if (node->value_type == TYPE_ARRAY_INT) {
                elem_type = LLVMInt32TypeInContext(gen->context);
                elem_size = 4;
            } else if (node->value_type == TYPE_ARRAY_DOUBLE) {
                elem_type = LLVMDoubleTypeInContext(gen->context);
                elem_size = 8;
            } else if (node->value_type == TYPE_ARRAY_STRING) {
                elem_type = LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
                elem_size = 8; // pointer size
            } else {
                elem_type = LLVMInt32TypeInContext(gen->context);
                elem_size = 4;
            }

            // Allocate memory: malloc(element_count * element_size)
            LLVMValueRef array_size = LLVMConstInt(LLVMInt64TypeInContext(gen->context),
                                                    elem_count * elem_size, 0);
            LLVMValueRef malloc_args[] = { array_size };
            LLVMValueRef array_ptr = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(malloc_func),
                                                     malloc_func, malloc_args, 1, "array_malloc");

            // Cast to appropriate pointer type
            LLVMValueRef typed_array = LLVMBuildBitCast(gen->builder, array_ptr,
                                                         LLVMPointerType(elem_type, 0), "array_ptr");

            // Store each element
            for (int i = 0; i < elem_count; i++) {
                LLVMValueRef elem_value = codegen_node(gen, node->array_literal.elements[i]);
                LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32TypeInContext(gen->context), i, 0) };
                LLVMValueRef elem_ptr = LLVMBuildGEP2(gen->builder, elem_type, typed_array,
                                                       indices, 1, "elem_ptr");
                LLVMBuildStore(gen->builder, elem_value, elem_ptr);
            }

            return typed_array;
        }

        case AST_OBJECT_LITERAL: {
            // Create a struct type for this object
            // For now, we'll create an anonymous struct with fields matching the properties
            int prop_count = node->object_literal.count;
            LLVMTypeRef* field_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * prop_count);

            // Determine field types from property values
            for (int i = 0; i < prop_count; i++) {
                field_types[i] = get_llvm_type(gen, node->object_literal.values[i]->value_type);
            }

            // Create struct type
            LLVMTypeRef struct_type = LLVMStructTypeInContext(gen->context, field_types, prop_count, 0);
            free(field_types);

            // Allocate struct on the stack
            LLVMValueRef obj_ptr = LLVMBuildAlloca(gen->builder, struct_type, "obj");

            // Store each property value
            for (int i = 0; i < prop_count; i++) {
                LLVMValueRef prop_value = codegen_node(gen, node->object_literal.values[i]);
                LLVMValueRef indices[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0),
                    LLVMConstInt(LLVMInt32TypeInContext(gen->context), i, 0)
                };
                LLVMValueRef field_ptr = LLVMBuildGEP2(gen->builder, struct_type, obj_ptr,
                                                        indices, 2, "field_ptr");
                LLVMBuildStore(gen->builder, prop_value, field_ptr);
            }

            return obj_ptr;
        }

        case AST_INDEX_ACCESS: {
            LLVMValueRef object = codegen_node(gen, node->index_access.object);
            LLVMValueRef index = codegen_node(gen, node->index_access.index);

            // String indexing: return single character as string
            if (node->index_access.object->value_type == TYPE_STRING) {
                // Get pointer to character at index
                LLVMValueRef char_ptr = LLVMBuildGEP2(gen->builder,
                                                       LLVMInt8TypeInContext(gen->context),
                                                       object, &index, 1, "char_ptr");

                // Allocate memory for single-char string (2 bytes: char + null terminator)
                LLVMValueRef malloc_func = LLVMGetNamedFunction(gen->module, "malloc");
                LLVMValueRef size = LLVMConstInt(LLVMInt64TypeInContext(gen->context), 2, 0);
                LLVMValueRef malloc_args[] = { size };
                LLVMValueRef str_buf = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(malloc_func),
                                                       malloc_func, malloc_args, 1, "char_str");

                // Copy character
                LLVMValueRef ch = LLVMBuildLoad2(gen->builder, LLVMInt8TypeInContext(gen->context),
                                                  char_ptr, "ch");
                LLVMBuildStore(gen->builder, ch, str_buf);

                // Add null terminator
                LLVMValueRef one = LLVMConstInt(LLVMInt32TypeInContext(gen->context), 1, 0);
                LLVMValueRef null_ptr = LLVMBuildGEP2(gen->builder, LLVMInt8TypeInContext(gen->context),
                                                       str_buf, &one, 1, "null_ptr");
                LLVMBuildStore(gen->builder, LLVMConstInt(LLVMInt8TypeInContext(gen->context), 0, 0),
                              null_ptr);

                return str_buf;
            }
            // Array indexing: return element
            else {
                LLVMTypeRef elem_type;
                if (node->index_access.object->value_type == TYPE_ARRAY_INT) {
                    elem_type = LLVMInt32TypeInContext(gen->context);
                } else if (node->index_access.object->value_type == TYPE_ARRAY_DOUBLE) {
                    elem_type = LLVMDoubleTypeInContext(gen->context);
                } else if (node->index_access.object->value_type == TYPE_ARRAY_STRING) {
                    elem_type = LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
                } else {
                    elem_type = LLVMInt32TypeInContext(gen->context);
                }

                LLVMValueRef elem_ptr = LLVMBuildGEP2(gen->builder, elem_type, object,
                                                       &index, 1, "elem_ptr");
                return LLVMBuildLoad2(gen->builder, elem_type, elem_ptr, "elem");
            }
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
                symbol_table_insert(gen->symbols, node->func_decl.params[i], param_type, alloca, false);
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
        log_error("No specialized body for %s", spec->specialized_name);
        return NULL;
    }

    ASTNode* specialized_node = spec->specialized_body;

    // Get the already-declared function
    LLVMValueRef func = LLVMGetNamedFunction(gen->module, spec->specialized_name);
    if (!func) {
        log_error("Function %s not declared", spec->specialized_name);
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
                          spec->param_types[i], alloca, false);
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

    // PASS 0: Register all functions in symbol table for function references
    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.statements[i];
            if (stmt->type == AST_FUNCTION_DECL) {
                // Register function in codegen symbol table
                SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
                entry->name = strdup(stmt->func_decl.name);
                entry->type = TYPE_FUNCTION;
                entry->is_const = false;
                entry->node = stmt;
                entry->next = gen->symbols->head;
                gen->symbols->head = entry;
            }
        }
    }

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
                            log_warning("Specialization %s has no body", spec->specialized_name);
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

                        log_verbose_indent(1, "Declared: %s with %d params", spec->specialized_name, spec->param_count);

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
                        log_verbose_indent(1, "Generating: %s (using cloned AST)", spec->specialized_name);

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

            // Stop if current block is already terminated with a return
            LLVMBasicBlockRef current = LLVMGetInsertBlock(gen->builder);
            if (current && LLVMGetBasicBlockTerminator(current)) {
                LLVMValueRef term = LLVMGetBasicBlockTerminator(current);
                // Only break if it's a return instruction, not just any terminator
                if (LLVMGetInstructionOpcode(term) == LLVMRet) {
                    break;
                }
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
