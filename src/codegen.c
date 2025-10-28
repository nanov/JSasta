#include "jsasta_compiler.h"
#include "traits.h"
#include "operator_utils.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static LLVMTypeRef codegen_lookup_object_type(CodeGen* gen, TypeInfo* type_info);

// Helper function to emit debug location for a node
static void codegen_set_debug_location(CodeGen* gen, ASTNode* node) {
    if (!gen->di_builder || !node || !gen->current_di_scope) {
        return;
    }
    
    unsigned line = (unsigned)node->loc.line;
    unsigned col = (unsigned)node->loc.column;
    
    LLVMMetadataRef loc = LLVMDIBuilderCreateDebugLocation(
        gen->context, line, col, gen->current_di_scope, NULL
    );
    
    LLVMSetCurrentDebugLocation2(gen->builder, loc);
}

// Runtime function registry
void codegen_register_runtime_function(CodeGen* gen, const char* name, TypeInfo* return_type,
                                       LLVMValueRef (*handler)(CodeGen*, ASTNode*)) {
    RuntimeFunction* rf = (RuntimeFunction*)malloc(sizeof(RuntimeFunction));
    rf->name = strdup(name);
    rf->return_type = return_type;
    rf->handler = handler;
    rf->next = gen->runtime_functions;
    gen->runtime_functions = rf;
}

TypeInfo* codegen_get_runtime_function_type(CodeGen* gen, const char* name) {
    RuntimeFunction* rf = gen->runtime_functions;
    while (rf) {
        if (strcmp(rf->name, name) == 0) {
            return rf->return_type;
        }
        rf = rf->next;
    }
    return Type_Unknown;
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
    gen->type_ctx = NULL;             // Will be set during generation (contains types and specializations)
    gen->trait_registry = NULL;       // Will be shared with type_ctx
    gen->loop_exit_block = NULL;      // Initialize loop control blocks
    gen->loop_continue_block = NULL;
    
    // Initialize debug info (will be configured later if -g is passed)
    gen->enable_debug = false;
    gen->source_filename = NULL;
    gen->di_builder = NULL;
    gen->di_compile_unit = NULL;
    gen->di_file = NULL;
    gen->current_di_scope = NULL;

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

    // Dispose debug info builder if it was created
    if (gen->di_builder) {
        LLVMDisposeDIBuilder(gen->di_builder);
    }

    symbol_table_free(gen->symbols);
    LLVMDisposeBuilder(gen->builder);
    LLVMDisposeModule(gen->module);
    LLVMContextDispose(gen->context);
    free(gen);
}

static LLVMTypeRef get_llvm_type(CodeGen* gen, TypeInfo* type_info) {
    if (!type_info) return LLVMInt32TypeInContext(gen->context);
    
    // Note: type_info should already be resolved if it came from type_context getters
    // But we resolve here as a safety measure for types from other sources
    type_info = type_info_resolve_alias(type_info);
    
    TypeContext* ctx = gen->type_ctx;
    
    // Check integer types by bit width
    if (type_info_is_integer(type_info)) {
        int bit_width = type_info_get_int_width(type_info);
        return LLVMIntTypeInContext(gen->context, bit_width);
    }
    
    // Check other primitive types by pointer comparison
    if (type_info == ctx->double_type) {
        return LLVMDoubleTypeInContext(gen->context);
    } else if (type_info == ctx->string_type) {
        return LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
    } else if (type_info == ctx->bool_type) {
        return LLVMInt1TypeInContext(gen->context);
    } else if (type_info == ctx->void_type) {
        return LLVMVoidTypeInContext(gen->context);
    }
    
    // Check by kind
    if (type_info->kind == TYPE_KIND_ARRAY && type_info->data.array.element_type) {
        // Array type - determine element type
        if (type_info->data.array.element_type == ctx->int_type) {
            return LLVMPointerType(LLVMInt32TypeInContext(gen->context), 0);
        } else if (type_info->data.array.element_type == ctx->double_type) {
            return LLVMPointerType(LLVMDoubleTypeInContext(gen->context), 0);
        } else if (type_info->data.array.element_type == ctx->string_type) {
            return LLVMPointerType(LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0), 0);
        }
    } else if (type_info->kind == TYPE_KIND_OBJECT) {
        // For objects, we use opaque pointers
        return LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
    }
    
    // Default fallback
    return LLVMInt32TypeInContext(gen->context);
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

// Helper to convert a value to string using sprintf
static LLVMValueRef codegen_value_to_string_sprintf(CodeGen* gen, LLVMValueRef value, 
                                                     const char* format, int buffer_size, 
                                                     const char* buf_name) {
    LLVMValueRef malloc_func = LLVMGetNamedFunction(gen->module, "malloc");
    LLVMValueRef sprintf_func = LLVMGetNamedFunction(gen->module, "sprintf");

    // Allocate buffer
    LLVMValueRef size = LLVMConstInt(LLVMInt64TypeInContext(gen->context), buffer_size, 0);
    LLVMValueRef malloc_args[] = { size };
    LLVMValueRef buffer = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(malloc_func),
                                         malloc_func, malloc_args, 1, buf_name);

    // Format the value
    LLVMValueRef format_str = LLVMBuildGlobalStringPtr(gen->builder, format, "fmt");
    LLVMValueRef sprintf_args[] = { buffer, format_str, value };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(sprintf_func),
                  sprintf_func, sprintf_args, 3, "");

    return buffer;
}

static LLVMValueRef codegen_int_to_string(CodeGen* gen, LLVMValueRef value) {
    return codegen_value_to_string_sprintf(gen, value, "%d", 32, "int_buf");
}

static LLVMValueRef codegen_double_to_string(CodeGen* gen, LLVMValueRef value) {
    return codegen_value_to_string_sprintf(gen, value, "%f", 64, "double_buf");
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
    
    // Set debug location for this node
    codegen_set_debug_location(gen, node);

    switch (node->type) {
        case AST_NUMBER:
            if (type_info_is_double(node->type_info)) {
                return LLVMConstReal(LLVMDoubleTypeInContext(gen->context), node->number.value);
            } else {
                // Use the actual integer type from type_info
                LLVMTypeRef int_type = get_llvm_type(gen, node->type_info);
                bool is_signed = type_info_is_signed_int(node->type_info);
                return LLVMConstInt(int_type, (long long)node->number.value, is_signed);
            }

        case AST_STRING:
            return LLVMBuildGlobalStringPtr(gen->builder, node->string.value, "str");

        case AST_BOOLEAN:
            return LLVMConstInt(LLVMInt1TypeInContext(gen->context), node->boolean.value, 0);

        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->identifier.name);
            if (entry && entry->value) {
                // Use node->type_info if available, otherwise fall back to entry->type_info
                TypeInfo* type_info = node->type_info ? node->type_info : entry->type_info;
                
                // For objects and arrays, return the pointer directly (don't load)
                // Objects are already stack-allocated structs, we pass by pointer
                // Arrays are heap-allocated, the alloca holds the pointer
                if (type_info_is_object(type_info)) {
                    return entry->value;
                }
                if (type_info_is_array(type_info)) {
                    return LLVMBuildLoad2(gen->builder, LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
                                         entry->value, node->identifier.name);
                }
                // For other types, load the value
                return LLVMBuildLoad2(gen->builder, get_llvm_type(gen, type_info),
                                     entry->value, node->identifier.name);
            }
            log_error_at(&node->loc, "Undefined variable: %s", node->identifier.name);
            return NULL;
        }

        case AST_BINARY_OP: {
            LLVMValueRef left = codegen_node(gen, node->binary_op.left);
            LLVMValueRef right = codegen_node(gen, node->binary_op.right);
            
            // Special handling for logical operators (not traits yet)
            if (strcmp(node->binary_op.op, "&&") == 0) {
                return LLVMBuildAnd(gen->builder, left, right, "andtmp");
            } else if (strcmp(node->binary_op.op, "||") == 0) {
                return LLVMBuildOr(gen->builder, left, right, "ortmp");
            }
            
            // Special handling for string concatenation (will use traits later)
            if (strcmp(node->binary_op.op, "+") == 0 &&
                (type_info_is_string(node->binary_op.left->type_info) ||
                 type_info_is_string(node->binary_op.right->type_info))) {
                
                // Convert non-strings to strings if needed
                if (type_info_is_int(node->binary_op.left->type_info)) {
                    left = codegen_int_to_string(gen, left);
                } else if (type_info_is_double(node->binary_op.left->type_info)) {
                    left = codegen_double_to_string(gen, left);
                } else if (type_info_is_bool(node->binary_op.left->type_info)) {
                    left = codegen_bool_to_string(gen, left);
                }
                
                if (type_info_is_int(node->binary_op.right->type_info)) {
                    right = codegen_int_to_string(gen, right);
                } else if (type_info_is_double(node->binary_op.right->type_info)) {
                    right = codegen_double_to_string(gen, right);
                } else if (type_info_is_bool(node->binary_op.right->type_info)) {
                    right = codegen_bool_to_string(gen, right);
                }
                
                return codegen_string_concat(gen, left, right);
            }
            
            // Use trait system for all other binary operations
            Trait* trait;
            const char* method_name;
            operator_get_trait_and_method(node->binary_op.op, &trait, &method_name);
            
            if (trait && method_name) {
                TypeInfo* left_type = node->binary_op.left->type_info;
                TypeInfo* right_type = node->binary_op.right->type_info;
                
                if (left_type && right_type) {
                    MethodImpl* method = trait_get_binary_method(trait, left_type, right_type, method_name);
                    
                    if (method && method->kind == METHOD_INTRINSIC && method->codegen) {
                        // Call intrinsic codegen function
                        LLVMValueRef args[] = { left, right };
                        return method->codegen(gen, args, 2);
                    } else {
                        log_error_at(&node->loc, "No trait implementation found for %s %s %s",
                                   left_type->type_name, node->binary_op.op, right_type->type_name);
                    }
                }
            }
            
            log_error_at(&node->loc, "Unsupported binary operation: %s", node->binary_op.op);
            return NULL;
        }

        case AST_UNARY_OP: {
            LLVMValueRef operand = codegen_node(gen, node->unary_op.operand);

            if (strcmp(node->unary_op.op, "-") == 0) {
                if (type_info_is_double(node->unary_op.operand->type_info)) {
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
            const char* var_name = node->prefix_op.name;
            const char* op = node->prefix_op.op;
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, var_name);
            
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in prefix operator: %s", var_name);
                return NULL;
            }
            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot modify const variable: %s", var_name);
                return NULL;
            }

            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type_info),
                                                  entry->value, var_name);
            LLVMValueRef one, new_value;
            bool is_increment = (strcmp(op, "++") == 0);

            if (type_info_is_double(entry->type_info)) {
                one = LLVMConstReal(LLVMDoubleTypeInContext(gen->context), 1.0);
                new_value = is_increment ? LLVMBuildFAdd(gen->builder, current, one, "preinc")
                                         : LLVMBuildFSub(gen->builder, current, one, "predec");
            } else {
                one = LLVMConstInt(get_llvm_type(gen, entry->type_info), 1, 0);
                new_value = is_increment ? LLVMBuildAdd(gen->builder, current, one, "preinc")
                                         : LLVMBuildSub(gen->builder, current, one, "predec");
            }

            LLVMBuildStore(gen->builder, new_value, entry->value);
            return new_value;  // Return the new value
        }

        case AST_POSTFIX_OP: {
            // i++ or i--: return old value, then increment/decrement
            const char* var_name = node->postfix_op.name;
            const char* op = node->postfix_op.op;
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, var_name);
            
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in postfix operator: %s", var_name);
                return NULL;
            }
            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot modify const variable: %s", var_name);
                return NULL;
            }

            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type_info),
                                                  entry->value, var_name);
            
            // Use trait system for increment/decrement
            bool is_increment = (strcmp(op, "++") == 0);
            Trait* trait = is_increment ? Trait_AddAssign : Trait_SubAssign;
            const char* method_name = is_increment ? "add_assign" : "sub_assign";
            
            LLVMValueRef one;
            if (type_info_is_double(entry->type_info)) {
                one = LLVMConstReal(LLVMDoubleTypeInContext(gen->context), 1.0);
            } else {
                one = LLVMConstInt(get_llvm_type(gen, entry->type_info), 1, 0);
            }
            
            LLVMValueRef new_value = NULL;
            if (trait && entry->type_info) {
                MethodImpl* method = trait_get_binary_method(trait, entry->type_info, entry->type_info, method_name);
                
                if (method && method->kind == METHOD_INTRINSIC && method->codegen) {
                    LLVMValueRef args[] = { current, one };
                    new_value = method->codegen(gen, args, 2);
                }
            }
            
            // Fallback if trait lookup failed
            if (!new_value) {
                if (type_info_is_double(entry->type_info)) {
                    new_value = is_increment ? LLVMBuildFAdd(gen->builder, current, one, "postinc")
                                             : LLVMBuildFSub(gen->builder, current, one, "postdec");
                } else {
                    new_value = is_increment ? LLVMBuildAdd(gen->builder, current, one, "postinc")
                                             : LLVMBuildSub(gen->builder, current, one, "postdec");
                }
            }

            LLVMBuildStore(gen->builder, new_value, entry->value);
            return current;  // Return the old value
        }

        case AST_VAR_DECL: {
            // Special handling for function references
            if (type_info_is_function_ctx(node->type_info) && node->var_decl.init &&
                node->var_decl.init->type == AST_IDENTIFIER) {
                // Look up the function being referenced
                const char* func_name = node->var_decl.init->identifier.name;
                SymbolEntry* func_entry = symbol_table_lookup(gen->symbols, func_name);

                if (!func_entry) {
                    log_error_at(&node->loc, "Function not found: %s", func_name);
                    return NULL;
                }
                if (!type_info_is_function_ctx(func_entry->type_info)) {
                    log_error_at(&node->loc, "Not a function type: %s", func_name);
                    return NULL;
                }
                if (!func_entry->value) {
                    log_error_at(&node->loc, "Function has no value reference: %s", func_name);
                    return NULL;
                }

                // Store the function reference in the variable's symbol entry
                SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
                entry->name = strdup(node->var_decl.name);
                entry->type_info = func_entry->type_info;  // Copy function type
                entry->is_const = node->var_decl.is_const;
                entry->node = func_entry->node;  // May be NULL
                entry->llvm_type = NULL;
                entry->value = func_entry->value;  // Copy LLVM function reference
                entry->next = gen->symbols->head;
                gen->symbols->head = entry;
                return NULL;
            }

            // Special handling for objects - they already return a pointer from AST_OBJECT_LITERAL
            if (type_info_is_object(node->type_info) && node->var_decl.init &&
                node->var_decl.init->type == AST_OBJECT_LITERAL) {

                // Generate the object literal first
                LLVMValueRef obj_ptr = codegen_node(gen, node->var_decl.init);

                // Lookup pre-generated struct type from type table
                ASTNode* obj_lit = node->var_decl.init;
                LLVMTypeRef struct_type = codegen_lookup_object_type(gen, obj_lit->type_info);
                
                if (!struct_type) {
                    log_error_at(&node->loc, "Could not find pre-generated struct type for object");
                    return NULL;
                }

                // TypeInfo should already exist from type inference - just clone it
                TypeInfo* type_info = NULL;
                if (obj_lit->type_info) {
                    type_info = type_info_clone(obj_lit->type_info);
                }

                // Store in symbol table with both value and TypeInfo
                SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
                entry->name = strdup(node->var_decl.name);
                entry->type_info = type_info;    // Type metadata from type inference
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
            TypeInfo* var_type_info = node->type_info;
            
            // Determine the type
            LLVMTypeRef var_llvm_type;
            if (var_type_info && type_info_is_array(var_type_info)) {
                // For arrays, we'll determine the type from init_value later
                var_llvm_type = NULL;
            } else {
                var_llvm_type = get_llvm_type(gen, var_type_info);
            }
            
            // Check if this is a global variable (parent scope is NULL)
            bool is_global = (gen->symbols->parent == NULL);
            
            if (is_global) {
                // Global variable - use LLVMAddGlobal
                if (node->var_decl.init) {
                    init_value = codegen_node(gen, node->var_decl.init);
                    
                    if (!var_type_info && node->var_decl.init->type_info) {
                        var_type_info = node->var_decl.init->type_info;
                    }
                    
                    // For arrays, use the actual type
                    if (var_type_info && type_info_is_array(var_type_info)) {
                        var_llvm_type = LLVMTypeOf(init_value);
                    } else if (!var_llvm_type) {
                        var_llvm_type = get_llvm_type(gen, var_type_info);
                    }
                }
                
                // Create global variable
                LLVMValueRef global = LLVMAddGlobal(gen->module, var_llvm_type, node->var_decl.name);
                
                // Set initializer (must be constant)
                if (init_value && LLVMIsConstant(init_value)) {
                    LLVMSetInitializer(global, init_value);
                } else {
                    // Non-constant initializer - set to zero and store the value in main
                    LLVMSetInitializer(global, LLVMConstInt(var_llvm_type, 0, 0));
                    if (init_value) {
                        LLVMBuildStore(gen->builder, init_value, global);
                    }
                }
                
                symbol_table_insert(gen->symbols, node->var_decl.name, var_type_info, global, node->var_decl.is_const);
                return global;
            } else {
                // Local variable - use alloca as before
                if (node->var_decl.init) {
                    init_value = codegen_node(gen, node->var_decl.init);
                    
                    if (!var_type_info && node->var_decl.init->type_info) {
                        var_type_info = node->var_decl.init->type_info;
                    }
                }  else {
                    init_value = LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0);
                }

                // For arrays, use the actual type of the init_value
                if (var_type_info && type_info_is_array(var_type_info) && init_value) {
                    var_llvm_type = LLVMTypeOf(init_value);
                } else if (!var_llvm_type) {
                    var_llvm_type = get_llvm_type(gen, var_type_info);
                }

                LLVMValueRef alloca = LLVMBuildAlloca(gen->builder, var_llvm_type, node->var_decl.name);
                LLVMBuildStore(gen->builder, init_value, alloca);
                symbol_table_insert(gen->symbols, node->var_decl.name, var_type_info, alloca, node->var_decl.is_const);

                return alloca;
            }
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
            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, entry->type_info),
                                                  entry->value, node->compound_assignment.name);

            // Generate the right-hand side value
            LLVMValueRef rhs = codegen_node(gen, node->compound_assignment.value);

            // Use trait system for all compound assignments
            const char* op = node->compound_assignment.op;
            Trait* trait;
            const char* method_name;
            operator_get_trait_and_method(op, &trait, &method_name);
            
            LLVMValueRef new_value = NULL;
            if (trait && method_name && entry->type_info && node->compound_assignment.value->type_info) {
                MethodImpl* method = trait_get_binary_method(trait, entry->type_info, 
                                                             node->compound_assignment.value->type_info, 
                                                             method_name);
                
                if (method && method->kind == METHOD_INTRINSIC && method->codegen) {
                    LLVMValueRef args[] = { current, rhs };
                    new_value = method->codegen(gen, args, 2);
                }
            }
            
            if (!new_value) {
                log_error_at(&node->loc, "No trait implementation for compound assignment operator: %s", op);
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
            if (type_info_is_string(node->index_assignment.object->type_info)) {
                log_error("String index assignment is not supported. Strings are immutable");
                return NULL;
            }

            // Array assignment: modify element at index
            LLVMValueRef object = codegen_node(gen, node->index_assignment.object);

            LLVMTypeRef elem_type;
            TypeInfo* arr_type = node->index_assignment.object->type_info;
            if (type_info_is_array_of(arr_type, gen->type_ctx->int_type)) {
                elem_type = LLVMInt32TypeInContext(gen->context);
            } else if (type_info_is_array_of(arr_type, gen->type_ctx->double_type)) {
                elem_type = LLVMDoubleTypeInContext(gen->context);
            } else if (type_info_is_array_of(arr_type, gen->type_ctx->string_type)) {
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

            // Special handling for Array() constructor
            if (strcmp(func_name, "Array") == 0 && node->call.arg_count == 1) {
                LLVMValueRef size_arg = codegen_node(gen, node->call.args[0]);
                LLVMValueRef calloc_func = LLVMGetNamedFunction(gen->module, "calloc");
                
                // Allocate array: calloc(size, element_size)
                // Default to int array (4 bytes per element)
                LLVMValueRef elem_size = LLVMConstInt(LLVMInt64TypeInContext(gen->context), 4, 0);
                LLVMValueRef calloc_args[] = { size_arg, elem_size };
                LLVMValueRef array_ptr = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(calloc_func),
                                                         calloc_func, calloc_args, 2, "array_calloc");
                
                return array_ptr;
            }

            // Check if this is a function variable (e.g., var a = print; a("text");)
            SymbolEntry* callee_entry = symbol_table_lookup(gen->symbols, func_name);
            if (callee_entry && type_info_is_function_ctx(callee_entry->type_info)) {
                // Get function declaration node from TypeInfo
                ASTNode* func_decl_node = callee_entry->type_info->data.function.func_decl_node;
                if (func_decl_node && func_decl_node->type == AST_FUNCTION_DECL) {
                    // It's a function variable - use the actual function name from the func_decl
                    func_name = func_decl_node->func_decl.name;
                    const char* func_type = func_decl_node->func_decl.body ? "Function" : "External function";
                    log_verbose("%s variable '%s' resolves to '%s'", 
                               func_type, node->call.callee->identifier.name, func_name);
                } else {
                    log_verbose("Function variable '%s' has no func_decl_node in TypeInfo", 
                               node->call.callee->identifier.name);
                }
            }

            // Generate arguments first to get their types
            LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * node->call.arg_count);
            TypeInfo** arg_type_infos = (TypeInfo**)malloc(sizeof(TypeInfo*) * node->call.arg_count);

            for (int i = 0; i < node->call.arg_count; i++) {
                args[i] = codegen_node(gen, node->call.args[i]);
                arg_type_infos[i] = node->call.args[i]->type_info;
                
                // For variadic functions (like printf), promote bool (i1) to i32 for proper printing
                if (arg_type_infos[i] && type_info_is_bool(arg_type_infos[i])) {
                    LLVMTypeRef arg_llvm_type = LLVMTypeOf(args[i]);
                    if (LLVMGetTypeKind(arg_llvm_type) == LLVMIntegerTypeKind && 
                        LLVMGetIntTypeWidth(arg_llvm_type) == 1) {
                        // Extend i1 to i32
                        args[i] = LLVMBuildZExt(gen->builder, args[i], 
                                               LLVMInt32TypeInContext(gen->context), "bool_to_int");
                    }
                }
            }

            // Try to find specialized version
            LLVMValueRef func = NULL;
            if (gen->type_ctx && node->call.arg_count > 0) {
                FunctionSpecialization* spec = specialization_context_find_by_type_info(
                    gen->type_ctx, func_name, arg_type_infos, node->call.arg_count);

                if (spec) {
                    // Populate TypeInfo for object arguments if not already set
                    for (int i = 0; i < node->call.arg_count; i++) {
                        if (type_info_is_object(arg_type_infos[i])) {
                            // Get TypeInfo from the argument
                            ASTNode* arg_node = node->call.args[i];
                            if (arg_node->type == AST_IDENTIFIER) {
                                SymbolEntry* entry = symbol_table_lookup(gen->symbols, arg_node->identifier.name);
                                if (entry && entry->type_info && !spec->param_type_info[i]) {
                                    // Clone TypeInfo for the specialization
                                    spec->param_type_info[i] = type_info_clone(entry->type_info);
                                }
                            }
                        }
                    }

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
                    free(arg_type_infos);
                    return runtime_result;
                }

                // Function not found anywhere
                log_error_at(&node->loc, "Undefined function: %s", func_name);
                free(args);
                free(arg_type_infos);
                return NULL;
            }

            // Don't name void function calls
            const char* call_name = (type_info_is_void_ctx(node->type_info, gen->type_ctx)) ? "" : "calltmp";
            LLVMValueRef result = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(func),
                                                func, args, node->call.arg_count, call_name);
            free(args);
            free(arg_type_infos);

            return result;
        }

        case AST_MEMBER_ACCESS: {
            // Get the object (should be a pointer to struct)
            LLVMValueRef obj_ptr = NULL;
            TypeInfo* obj_type_info = NULL;
            LLVMTypeRef struct_type = NULL;
            ASTNode* obj_node = node->member_access.object;

            // Handle identifier case - load from symbol table
            if (obj_node->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->value) {
                    obj_ptr = entry->value;
                    obj_type_info = entry->type_info;
                    struct_type = entry->llvm_type;
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

                // For direct literals, create TypeInfo on the fly
                if (obj_node->type == AST_OBJECT_LITERAL) {
                    obj_type_info = type_info_create_from_object_literal(obj_node);
                }
            }

            // Check that we have type info for the object
            if (!obj_type_info || !type_info_is_object(obj_type_info)) {
                if (obj_node->type == AST_IDENTIFIER) {
                    log_error_at(&node->loc, "Cannot access property of non-object (variable '%s' has no TypeInfo)",
                                obj_node->identifier.name);
                } else {
                    log_error_at(&node->loc, "Cannot access property of non-object");
                }
                return NULL;
            }

            // Find the property index using TypeInfo
            int prop_index = type_info_find_property(obj_type_info, node->member_access.property);

            if (prop_index == -1) {
                log_error_at(&node->loc, "Property '%s' not found in object", node->member_access.property);
                return NULL;
            }

            if (!struct_type) {
                log_error_at(&node->loc, "Could not find struct type for object");
                return NULL;
            }

            // Get the type of this property from TypeInfo
            TypeInfo* prop_type_info = obj_type_info->data.object.property_types[prop_index];
            LLVMTypeRef prop_llvm_type = get_llvm_type(gen, prop_type_info);

            // Use GEP to get pointer to the field
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(gen->builder, struct_type, obj_ptr,
                                                          (unsigned)prop_index, "field_ptr");

            // Load the value
            return LLVMBuildLoad2(gen->builder, prop_llvm_type, field_ptr, "field_value");
        }

        case AST_MEMBER_ASSIGNMENT: {
            // Get the object (should be a pointer to struct)
            LLVMValueRef obj_ptr = NULL;
            TypeInfo* obj_type_info = NULL;
            LLVMTypeRef struct_type = NULL;
            ASTNode* obj_node = node->member_assignment.object;

            // Handle identifier case - load from symbol table
            if (obj_node->type == AST_IDENTIFIER) {
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
                if (entry && entry->value) {
                    obj_ptr = entry->value;
                    obj_type_info = entry->type_info;
                    struct_type = entry->llvm_type;
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

                // For direct literals, create TypeInfo on the fly
                if (obj_node->type == AST_OBJECT_LITERAL) {
                    obj_type_info = type_info_create_from_object_literal(obj_node);
                }
            }

            // Check that we have type info for the object
            if (!obj_type_info || !type_info_is_object(obj_type_info)) {
                log_error_at(&node->loc, "Cannot access property of non-object");
                return NULL;
            }

            // Find the property index using TypeInfo
            int prop_index = type_info_find_property(obj_type_info, node->member_assignment.property);

            if (prop_index == -1) {
                log_error_at(&node->loc, "Property '%s' not found in object", node->member_assignment.property);
                return NULL;
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
            if (type_info_is_double(node->type_info) && type_info_is_int(node->ternary.true_expr->type_info)) {
                true_val = LLVMBuildSIToFP(gen->builder, true_val,
                                          LLVMDoubleTypeInContext(gen->context), "inttodouble");
            }

            LLVMBasicBlockRef then_end_bb = LLVMGetInsertBlock(gen->builder);
            LLVMBuildBr(gen->builder, merge_bb);

            // Generate false branch
            LLVMPositionBuilderAtEnd(gen->builder, else_bb);
            LLVMValueRef false_val = codegen_node(gen, node->ternary.false_expr);

            // Type conversion if needed
            if (type_info_is_double(node->type_info) && type_info_is_int(node->ternary.false_expr->type_info)) {
                false_val = LLVMBuildSIToFP(gen->builder, false_val,
                                           LLVMDoubleTypeInContext(gen->context), "inttodouble");
            }

            LLVMBasicBlockRef else_end_bb = LLVMGetInsertBlock(gen->builder);
            LLVMBuildBr(gen->builder, merge_bb);

            // Merge block with PHI node
            LLVMPositionBuilderAtEnd(gen->builder, merge_bb);
            LLVMTypeRef result_type = get_llvm_type(gen, node->type_info);
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
            if (type_info_is_array_of(node->type_info, gen->type_ctx->int_type)) {
                elem_type = LLVMInt32TypeInContext(gen->context);
                elem_size = 4;
            } else if (type_info_is_array_of(node->type_info, gen->type_ctx->double_type)) {
                elem_type = LLVMDoubleTypeInContext(gen->context);
                elem_size = 8;
            } else if (type_info_is_array_of(node->type_info, gen->type_ctx->string_type)) {
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
            // Lookup pre-generated struct type from type table
            LLVMTypeRef struct_type = codegen_lookup_object_type(gen, node->type_info);
            
            if (!struct_type) {
                log_error_at(&node->loc, "Could not find pre-generated struct type for object literal");
                return NULL;
            }

            // Allocate struct on the stack
            LLVMValueRef obj_ptr = LLVMBuildAlloca(gen->builder, struct_type, "obj");

            // Store each property value
            int prop_count = node->object_literal.count;
            for (int i = 0; i < prop_count; i++) {
                LLVMValueRef prop_value = codegen_node(gen, node->object_literal.values[i]);
                
                // Get the expected field type and convert if needed
                TypeInfo* field_type = node->type_info->data.object.property_types[i];
                LLVMTypeRef expected_llvm_type = get_llvm_type(gen, field_type);
                LLVMTypeRef actual_type = LLVMTypeOf(prop_value);
                
                // Convert integer types if they don't match
                if (LLVMGetTypeKind(expected_llvm_type) == LLVMIntegerTypeKind && 
                    LLVMGetTypeKind(actual_type) == LLVMIntegerTypeKind &&
                    expected_llvm_type != actual_type) {
                    // Truncate or extend to match the target type
                    unsigned expected_width = LLVMGetIntTypeWidth(expected_llvm_type);
                    unsigned actual_width = LLVMGetIntTypeWidth(actual_type);
                    
                    if (actual_width > expected_width) {
                        prop_value = LLVMBuildTrunc(gen->builder, prop_value, expected_llvm_type, "trunc");
                    } else {
                        // Check if source type is signed or unsigned to use appropriate extension
                        TypeInfo* src_type = node->object_literal.values[i]->type_info;
                        if (type_info_is_signed_int(src_type)) {
                            prop_value = LLVMBuildSExt(gen->builder, prop_value, expected_llvm_type, "sext");
                        } else {
                            prop_value = LLVMBuildZExt(gen->builder, prop_value, expected_llvm_type, "zext");
                        }
                    }
                }
                
                LLVMValueRef field_ptr = LLVMBuildStructGEP2(gen->builder, struct_type, obj_ptr,
                                                              (unsigned)i, "field_ptr");
                LLVMBuildStore(gen->builder, prop_value, field_ptr);
            }

            return obj_ptr;
        }

        case AST_INDEX_ACCESS: {
            LLVMValueRef object = codegen_node(gen, node->index_access.object);
            LLVMValueRef index = codegen_node(gen, node->index_access.index);

            // String indexing: return character as u8 (byte value)
            if (type_info_is_string(node->index_access.object->type_info)) {
                // Get pointer to character at index
                LLVMValueRef char_ptr = LLVMBuildGEP2(gen->builder,
                                                       LLVMInt8TypeInContext(gen->context),
                                                       object, &index, 1, "char_ptr");

                // Load and return the byte value
                return LLVMBuildLoad2(gen->builder, LLVMInt8TypeInContext(gen->context),
                                     char_ptr, "char");
            }
            // Array indexing: return element
            else {
                LLVMTypeRef elem_type;
                TypeInfo* array_type = node->index_access.object->type_info;
                if (type_info_is_array_of(array_type, gen->type_ctx->int_type)) {
                    elem_type = LLVMInt32TypeInContext(gen->context);
                } else if (type_info_is_array_of(array_type, gen->type_ctx->double_type)) {
                    elem_type = LLVMDoubleTypeInContext(gen->context);
                } else if (type_info_is_array_of(array_type, gen->type_ctx->string_type)) {
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

        case AST_BREAK: {
            if (gen->loop_exit_block) {
                return LLVMBuildBr(gen->builder, gen->loop_exit_block);
            } else {
                fprintf(stderr, "Error: 'break' statement outside of loop\n");
                return NULL;
            }
        }

        case AST_CONTINUE: {
            if (gen->loop_continue_block) {
                return LLVMBuildBr(gen->builder, gen->loop_continue_block);
            } else {
                fprintf(stderr, "Error: 'continue' statement outside of loop\n");
                return NULL;
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

            // Save previous loop blocks for nested loops
            LLVMBasicBlockRef prev_exit = gen->loop_exit_block;
            LLVMBasicBlockRef prev_continue = gen->loop_continue_block;
            
            // Set current loop blocks
            gen->loop_exit_block = end_bb;
            gen->loop_continue_block = cond_bb;

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

            // Restore previous loop blocks
            gen->loop_exit_block = prev_exit;
            gen->loop_continue_block = prev_continue;

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

            // Save previous loop blocks for nested loops
            LLVMBasicBlockRef prev_exit = gen->loop_exit_block;
            LLVMBasicBlockRef prev_continue = gen->loop_continue_block;
            
            // Set current loop blocks (continue goes to update block in for loops)
            gen->loop_exit_block = end_bb;
            gen->loop_continue_block = update_bb;

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

            // Restore previous loop blocks
            gen->loop_exit_block = prev_exit;
            gen->loop_continue_block = prev_continue;

            LLVMPositionBuilderAtEnd(gen->builder, end_bb);

            return NULL;
        }

        case AST_FUNCTION_DECL: {
            // Save current builder position (we're generating at module level)
            LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(gen->builder);

            // Create parameter types array
            LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * node->func_decl.param_count);
            for (int i = 0; i < node->func_decl.param_count; i++) {
                // Use param_type_hints if available, otherwise default to int
                TypeInfo* param_type_info = (node->func_decl.param_type_hints && node->func_decl.param_type_hints[i]) ?
                    node->func_decl.param_type_hints[i] : 
                    type_context_get_int(gen->type_ctx);
                param_types[i] = get_llvm_type(gen, param_type_info);
            }

            // Create function type
            TypeInfo* ret_type_info = node->func_decl.return_type_hint ? 
                node->func_decl.return_type_hint : 
                type_context_get_int(gen->type_ctx);
            LLVMTypeRef ret_type = get_llvm_type(gen, ret_type_info);

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

                TypeInfo* param_type_info = (node->func_decl.param_type_hints && node->func_decl.param_type_hints[i]) ?
                    node->func_decl.param_type_hints[i] : 
                    type_context_get_int(gen->type_ctx);
                symbol_table_insert(gen->symbols, node->func_decl.params[i], param_type_info, alloca, false);
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

// Helper: Generate a specialized function
static LLVMValueRef codegen_specialized_function(CodeGen* gen, FunctionSpecialization* spec, TypeInfo* func_type) {
    // CRITICAL: Use the cloned body from spec->specialized_body, not the original!
    if (!spec->specialized_body) {
        log_error("No specialized body for %s", spec->specialized_name);
        return NULL;
    }

    ASTNode* body = spec->specialized_body;
    
    // Get parameter names from the original function declaration
    ASTNode* func_decl = func_type->data.function.func_decl_node;
    if (!func_decl) {
        log_error("No function declaration node for %s", spec->specialized_name);
        return NULL;
    }
    // All functions now use func_decl structure (whether user or external)
    char** param_names = func_decl->func_decl.params;

    // Get the already-declared function
    LLVMValueRef func = LLVMGetNamedFunction(gen->module, spec->specialized_name);
    if (!func) {
        log_error("Function %s not declared", spec->specialized_name);
        return NULL;
    }

    // Create parameter types for symbol table (from specialized types)
    LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * spec->param_count);
    for (int i = 0; i < spec->param_count; i++) {
        param_types[i] = get_llvm_type(gen, spec->param_type_info[i]);
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

    // Store parameters with specialized types
    for (int i = 0; i < spec->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(func, i);
        LLVMSetValueName(param, param_names[i]);

        LLVMValueRef param_value;

        // For objects, use the parameter pointer directly (it's already a pointer to the struct)
        // For other types, allocate and store as usual
        if (type_info_is_object(spec->param_type_info[i])) {
            param_value = param;  // Use pointer directly
        } else {
            LLVMValueRef alloca = LLVMBuildAlloca(gen->builder, param_types[i],
                                                  param_names[i]);
            LLVMBuildStore(gen->builder, param, alloca);
            param_value = alloca;
        }

        // Create symbol entry with TypeInfo for objects
        SymbolEntry* param_entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
        param_entry->name = strdup(param_names[i]);
        param_entry->type_info = spec->param_type_info[i];
        param_entry->is_const = false;
        param_entry->value = param_value;
        param_entry->node = NULL;
        param_entry->llvm_type = NULL;

        // For object parameters, copy TypeInfo from specialization
        if (type_info_is_object(spec->param_type_info[i]) && spec->param_type_info[i]) {
            param_entry->type_info = type_info_clone(spec->param_type_info[i]);

            // Lookup pre-generated LLVM struct type for this object
            param_entry->llvm_type = codegen_lookup_object_type(gen, param_entry->type_info);
            
            if (!param_entry->llvm_type) {
                log_warning("Could not find pre-generated struct type for parameter '%s'",
                           param_names[i]);
            } else {
                log_verbose_indent(2, "Parameter '%s' has TypeInfo with %d properties",
                                 param_entry->name, param_entry->type_info->data.object.property_count);
            }
        } else if (type_info_is_object(spec->param_type_info[i])) {
            log_warning("Parameter '%s' is TYPE_OBJECT but has no TypeInfo!",
                       param_names[i]);
            param_entry->type_info = NULL;
        } else {
            param_entry->type_info = NULL;
        }

        param_entry->next = gen->symbols->head;
        gen->symbols->head = param_entry;
    }

    // Generate body from cloned and type-analyzed AST
    log_verbose_indent(2, "Generating function body for %s", spec->specialized_name);
    codegen_node(gen, body);
    log_verbose_indent(2, "Completed function body for %s", spec->specialized_name);

    // Add return if missing
    LLVMTypeRef ret_type = get_llvm_type(gen, spec->return_type_info);
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

// Lookup pre-generated LLVM struct type by TypeInfo
static LLVMTypeRef codegen_lookup_object_type(CodeGen* gen, TypeInfo* type_info) {
    if (!gen->type_ctx || !type_info || !type_info_is_object(type_info)) {
        return NULL;
    }

    // If type_info doesn't have a type_name, we can't look it up
    if (!type_info->type_name) {
        return NULL;
    }

    // Search through type table by type_name (handles cloned TypeInfo)
    TypeEntry* entry = gen->type_ctx->type_table;
    while (entry) {
        if (entry->type && entry->type->type_name && 
            strcmp(entry->type->type_name, type_info->type_name) == 0 && 
            entry->llvm_type) {
            return entry->llvm_type;
        }
        entry = entry->next;
    }

    return NULL;
}

// Initialize all types from TypeContext: pre-generate object structs and declare function prototypes
static void codegen_initialize_types(CodeGen* gen) {
    if (!gen->type_ctx) {
        return;
    }

    TypeEntry* entry = gen->type_ctx->type_table;
    while (entry) {
        TypeInfo* type = entry->type;
        
        if (type->kind == TYPE_KIND_OBJECT && type->data.object.property_count > 0) {
            // Skip if already generated
            if (entry->llvm_type) {
                entry = entry->next;
                continue;
            }

            // Build array of field types
            LLVMTypeRef* field_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * type->data.object.property_count);
            for (int i = 0; i < type->data.object.property_count; i++) {
                field_types[i] = get_llvm_type(gen, type->data.object.property_types[i]);
            }

            // Create named struct
            LLVMTypeRef struct_type = LLVMStructCreateNamed(gen->context, type->type_name);
            LLVMStructSetBody(struct_type, field_types, type->data.object.property_count, 0);
            
            // Store in entry
            entry->llvm_type = struct_type;
            
            log_verbose("Pre-generated LLVM struct type '%s' with %d fields", 
                       type->type_name, type->data.object.property_count);
            
            free(field_types);
        } else if (type->kind == TYPE_KIND_FUNCTION) {
            // Register function in codegen symbol table
            SymbolEntry* sym_entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
            sym_entry->name = strdup(type->type_name);
            sym_entry->type_info = type;
            sym_entry->is_const = false;
            sym_entry->value = NULL;
            sym_entry->node = NULL;  // Not needed for function references
            sym_entry->llvm_type = NULL;
            sym_entry->next = gen->symbols->head;
            gen->symbols->head = sym_entry;
            
            // Declare all function specializations (includes fully typed and external)
            FunctionSpecialization* spec = type->data.function.specializations;
            
            while (spec) {
                // Create parameter types
                LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * spec->param_count);
                for (int j = 0; j < spec->param_count; j++) {
                    param_types[j] = get_llvm_type(gen, spec->param_type_info[j]);
                }

                // Create return type
                LLVMTypeRef ret_type = get_llvm_type(gen, spec->return_type_info);

                // Check if variadic (only for external functions)
                int is_var_arg = type->data.function.is_variadic ? 1 : 0;

                // Create function type
                LLVMTypeRef llvm_func_type = LLVMFunctionType(ret_type, param_types, spec->param_count, is_var_arg);

                // Declare function (just add to module, don't generate body yet)
                LLVMAddFunction(gen->module, spec->specialized_name, llvm_func_type);

                log_verbose_indent(1, "Declared: %s with %d params%s", spec->specialized_name, spec->param_count,
                                  is_var_arg ? " (variadic)" : "");

                free(param_types);
                spec = spec->next;
            }
        }
        
        entry = entry->next;
    }
}

void codegen_generate(CodeGen* gen, ASTNode* ast) {
    // Store type context from type inference (contains types and specializations)
    gen->type_ctx = ast->type_ctx;
    gen->trait_registry = ast->type_ctx ? ast->type_ctx->trait_registry : NULL;

    // Initialize debug info if enabled
    if (gen->enable_debug && gen->source_filename) {
        gen->di_builder = LLVMCreateDIBuilder(gen->module);
        
        // Create debug info file
        const char* filename = gen->source_filename;
        const char* directory = ".";
        gen->di_file = LLVMDIBuilderCreateFile(gen->di_builder, filename, strlen(filename),
                                                directory, strlen(directory));
        
        // Create compile unit
        const char* producer = "JSasta Compiler";
        bool is_optimized = false;
        const char* flags = "";
        unsigned runtime_version = 0;
        const char* split_name = "";
        const char* sysroot = "";
        const char* sdk = "";
        LLVMDWARFSourceLanguage source_lang = LLVMDWARFSourceLanguageC; // Closest match
        
        gen->di_compile_unit = LLVMDIBuilderCreateCompileUnit(
            gen->di_builder,
            source_lang,
            gen->di_file,
            producer, strlen(producer),
            is_optimized,
            flags, strlen(flags),
            runtime_version,
            split_name, strlen(split_name),
            LLVMDWARFEmissionFull,
            0, false, false,
            sysroot, strlen(sysroot),
            sdk, strlen(sdk)
        );
        
        // Add module flags for debug info
        // Debug Info Version (LLVM expects version 3 for modern DWARF)
        LLVMMetadataRef debug_version = LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt32TypeInContext(gen->context), 3, 0)
        );
        LLVMAddModuleFlag(gen->module, LLVMModuleFlagBehaviorWarning, 
                         "Debug Info Version", 18, debug_version);
        
        // Dwarf Version
        LLVMMetadataRef dwarf_version = LLVMValueAsMetadata(
            LLVMConstInt(LLVMInt32TypeInContext(gen->context), 4, 0)
        );
        LLVMAddModuleFlag(gen->module, LLVMModuleFlagBehaviorWarning,
                         "Dwarf Version", 13, dwarf_version);
        
        gen->current_di_scope = gen->di_file;
        log_verbose("Debug info initialized for %s", filename);
    }

    // PASS 0: Initialize all types - objects and function prototypes
    // This allows forward references and recursive calls
    codegen_initialize_types(gen);

    // Create main function
    LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32TypeInContext(gen->context), NULL, 0, 0);
    LLVMValueRef main_func = LLVMAddFunction(gen->module, "main", main_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(gen->context, main_func, "entry");
    LLVMPositionBuilderAtEnd(gen->builder, entry);

    gen->current_function = main_func;
    
    // Create debug info for main function
    if (gen->di_builder) {
        LLVMMetadataRef param_types[] = {};
        LLVMMetadataRef func_type = LLVMDIBuilderCreateSubroutineType(
            gen->di_builder, gen->di_file, param_types, 0, LLVMDIFlagZero
        );
        
        LLVMMetadataRef di_func = LLVMDIBuilderCreateFunction(
            gen->di_builder,
            gen->di_file,           // scope
            "main", 4,              // name
            "main", 4,              // linkage name
            gen->di_file,           // file
            1,                      // line number
            func_type,              // type
            false,                  // is local to unit
            true,                   // is definition
            1,                      // scope line
            LLVMDIFlagZero,         // flags
            false                   // is optimized
        );
        
        LLVMSetSubprogram(main_func, di_func);
        gen->current_di_scope = di_func;
    }

    // PASS 0.5: Generate global variables first (before functions)
    // This ensures globals are in the symbol table when functions reference them
    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.statements[i];
            
            // Only process variable declarations in this pass
            if (stmt->type == AST_VAR_DECL) {
                log_verbose_indent(1, "Generating global variable: %s", stmt->var_decl.name);
                codegen_node(gen, stmt);
            }
        }
    }

    // PASS 1: Generate function bodies
    if (gen->type_ctx) {
        TypeEntry* entry_iter = gen->type_ctx->type_table;
        while (entry_iter) {
            if (entry_iter->type->kind == TYPE_KIND_FUNCTION) {
                TypeInfo* func_type = entry_iter->type;
                FunctionSpecialization* spec = func_type->data.function.specializations;
                LLVMValueRef first_func_ref = NULL;

                while (spec) {
                    // Skip external functions (no body)
                    if (!spec->specialized_body) {
                        log_verbose_indent(1, "Skipping external: %s", spec->specialized_name);
                        spec = spec->next;
                        continue;
                    }

                    log_verbose_indent(1, "Generating: %s", spec->specialized_name);

                    // Generate the specialized function body
                    codegen_specialized_function(gen, spec, func_type);

                    // Store the first specialization reference
                    if (!first_func_ref) {
                        first_func_ref = LLVMGetNamedFunction(gen->module, spec->specialized_name);
                    }

                    // Restore builder to main
                    LLVMPositionBuilderAtEnd(gen->builder, entry);

                    spec = spec->next;
                }
                
                // Update the symbol table entry with the function reference
                if (first_func_ref) {
                    SymbolEntry* sym_entry = symbol_table_lookup(gen->symbols, func_type->type_name);
                    if (sym_entry) {
                        sym_entry->value = first_func_ref;
                    }
                }
            }
            entry_iter = entry_iter->next;
        }
    }
    
    // PASS 2: Generate non-function, non-variable statements in main
    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.statements[i];

            // Skip function declarations (already handled in PASS 1)
            if (stmt->type == AST_FUNCTION_DECL) {
                continue;
            }
            
            // Skip variable declarations (already handled in PASS 0.5)
            if (stmt->type == AST_VAR_DECL) {
                continue;
            }

            // Generate the statement normally (function calls, expressions, etc.)
            codegen_node(gen, stmt);

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
    
    // Finalize debug info
    if (gen->di_builder) {
        LLVMDIBuilderFinalize(gen->di_builder);
        log_verbose("Debug info finalized");
    }
}

void codegen_emit_llvm_ir(CodeGen* gen, const char* filename) {
    char* error = NULL;
    if (LLVMPrintModuleToFile(gen->module, filename, &error) != 0) {
        fprintf(stderr, "Error writing LLVM IR: %s\n", error);
        LLVMDisposeMessage(error);
    }
}
