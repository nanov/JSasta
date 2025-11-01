#include "jsasta_compiler.h"
#include "traits.h"
#include "operator_utils.h"
#include "logger.h"
#include "module_loader.h"
#include "format_string.h"
#include "llvm-c/Core.h"
#include "llvm-c/Types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper: Check if a symbol entry is a namespace (has an import node)
static inline bool symbol_is_namespace(SymbolEntry* entry) {
    return entry && entry->node && entry->node->type == AST_IMPORT_DECL;
}

// Helper: Get the imported module from a namespace symbol entry
static inline Module* symbol_get_imported_module(SymbolEntry* entry) {
    return symbol_is_namespace(entry) ? (Module*)entry->node->import_decl.imported_module : NULL;
}

// Forward declarations
static LLVMTypeRef codegen_lookup_object_type(CodeGen* gen, TypeInfo* type_info);
static LLVMValueRef codegen_member_access_ptr(CodeGen* gen, ASTNode* node);
static LLVMTypeRef codegen_get_llvm_type(CodeGen* gen, ASTNode* node);
static LLVMValueRef codegen_get_lvalue_ptr(CodeGen* gen, ASTNode* node);

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

// Helper function to create an alloca in the entry block of the current function
// This ensures all stack allocations happen at function entry, not in loops or nested blocks
static LLVMValueRef codegen_create_entry_block_alloca(CodeGen* gen, LLVMTypeRef type, const char* name) {
    if (!gen->entry_block) {
        // No entry block set - fallback to current position (shouldn't happen in well-formed code)
        return LLVMBuildAlloca(gen->builder, type, name);
    }

    // Save current position
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(gen->builder);

    // Position at the start of the entry block
    LLVMValueRef first_instr = LLVMGetFirstInstruction(gen->entry_block);
    if (first_instr) {
        LLVMPositionBuilderBefore(gen->builder, first_instr);
    } else {
        LLVMPositionBuilderAtEnd(gen->builder, gen->entry_block);
    }

    // Create the alloca
    LLVMValueRef alloca = LLVMBuildAlloca(gen->builder, type, name);

    // Restore builder position
    if (current_block) {
        LLVMPositionBuilderAtEnd(gen->builder, current_block);
    }

    return alloca;
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
    gen->symbols = NULL;              // Will be set from AST's symbol table during generation
    gen->current_function = NULL;
    gen->runtime_functions = NULL;
    gen->type_ctx = NULL;             // Will be set during generation (contains types and specializations)
    gen->trait_registry = NULL;       // Will be shared with type_ctx
    gen->loop_exit_block = NULL;      // Initialize loop control blocks
    gen->loop_continue_block = NULL;
    gen->entry_block = NULL;          // Initialize entry block for allocas

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

    // Note: gen->symbols is now owned by the AST and will be freed when AST is freed
    // Don't free it here to avoid double-free
    LLVMDisposeBuilder(gen->builder);
    LLVMDisposeModule(gen->module);
    LLVMContextDispose(gen->context);
    free(gen);
}

LLVMTypeRef get_llvm_type(CodeGen* gen, TypeInfo* type_info) {
    if (!type_info) return LLVMInt32TypeInContext(gen->context);

    // Note: type_info should already be resolved if it came from type_context getters
    // But we resolve here as a safety measure for types from other sources
    type_info = type_info_resolve_alias(type_info);

    // Check integer types by bit width
    if (type_info_is_integer(type_info)) {
        int bit_width = type_info_get_int_width(type_info);
        return LLVMIntTypeInContext(gen->context, bit_width);
    }

    // Check other primitive types by pointer comparison
    if (type_info == Type_Double) {
        return LLVMDoubleTypeInContext(gen->context);
    } else if (type_info == Type_String) {
        return LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
    } else if (type_info == Type_Bool) {
        return LLVMInt1TypeInContext(gen->context);
    } else if (type_info == Type_Void) {
        return LLVMVoidTypeInContext(gen->context);
    }

    // Check by kind
    if (type_info->kind == TYPE_KIND_REF) {
        // Ref type - get a pointer to the target type
        TypeInfo* target_type = type_info_get_ref_target(type_info);
        if (target_type) {
            LLVMTypeRef target_llvm_type = get_llvm_type(gen, target_type);
            return LLVMPointerType(target_llvm_type, 0);
        }
        // Fallback to opaque pointer if no target
        return LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0);
    } else if (type_info->kind == TYPE_KIND_ARRAY && type_info->data.array.element_type) {
        // Array type - determine element type
        if (type_info->data.array.element_type == Type_I32) {
            return LLVMPointerType(LLVMInt32TypeInContext(gen->context), 0);
        } else if (type_info->data.array.element_type == Type_Double) {
            return LLVMPointerType(LLVMDoubleTypeInContext(gen->context), 0);
        } else if (type_info->data.array.element_type == Type_String) {
            return LLVMPointerType(LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0), 0);
        }
    } else if (type_info->kind == TYPE_KIND_OBJECT) {
        // Look up the actual struct type
        LLVMTypeRef struct_type = codegen_lookup_object_type(gen, type_info);
        if (struct_type) {
            return struct_type;
        }
        // Return NULL if not found - this signals dependency not ready yet during type initialization
        // The iterative approach in codegen_initialize_types will retry
        return NULL;
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

// Helper to get a pointer to a member field (for use in assignments and inc/dec)
static LLVMValueRef codegen_member_access_ptr(CodeGen* gen, ASTNode* node) {
    if (!node || node->type != AST_MEMBER_ACCESS) return NULL;

    ASTNode* obj_node = node->member_access.object;

    // Get pointer to the object
    LLVMValueRef obj_ptr = NULL;
    if (obj_node->type == AST_IDENTIFIER) {
        // Simple identifier - load from symbol table
        SymbolEntry* entry = symbol_table_lookup(gen->symbols, obj_node->identifier.name);
        if (!entry || !entry->value) {
            log_error_at(&node->loc, "Undefined variable: %s", obj_node->identifier.name);
            return NULL;
        }
        obj_ptr = entry->value;
    } else if (obj_node->type == AST_MEMBER_ACCESS) {
        // Nested member access - recursively get pointer
        obj_ptr = codegen_member_access_ptr(gen, obj_node);
        if (!obj_ptr) {
            return NULL;
        }
    } else if (obj_node->type == AST_INDEX_ACCESS) {
        // Index access (e.g., arr[i].field) - get pointer to the element
        obj_ptr = codegen_get_lvalue_ptr(gen, obj_node);
        if (!obj_ptr) {
            log_error_at(&node->loc, "Failed to get pointer to indexed element");
            return NULL;
        }
    } else {
        // Other expression (object literal, function call, etc.)
        obj_ptr = codegen_node(gen, obj_node);
        if (!obj_ptr) {
            log_error_at(&node->loc, "Failed to generate code for object");
            return NULL;
        }
    }

    // Get the object's type and unwrap refs (parameters with ref types)
    TypeInfo* obj_type_info = obj_node->type_info;
    if (obj_type_info && type_info_is_ref(obj_type_info)) {
        obj_type_info = type_info_get_ref_target(obj_type_info);

        // Check if obj_ptr is a function parameter (ref parameters are already pointers)
        // Function parameters don't need dereferencing, only ref variables do
        bool is_function_param = (LLVMGetValueKind(obj_ptr) == LLVMArgumentValueKind);

        if (!is_function_param) {
            // Ref variable - need to load the pointer value first
            LLVMTypeRef ptr_type = LLVMPointerType(get_llvm_type(gen, obj_type_info), 0);
            obj_ptr = LLVMBuildLoad2(gen->builder, ptr_type, obj_ptr, "deref");
        }
        // For ref parameters, obj_ptr is already the pointer we need
    }

    if (!obj_type_info || !type_info_is_object(obj_type_info)) {
        log_error_at(&node->loc, "Cannot access property of non-object (type not inferred, kind=%d, property='%s')",
                    obj_type_info ? obj_type_info->kind : -1, node->member_access.property);
        return NULL;
    }

    // Use cached property index from type inference
    int prop_index = node->member_access.property_index;
    if (prop_index == -1) {
        log_error_at(&node->loc, "Property '%s' not found in object (property_index not set by type inference)",
                     node->member_access.property);
        return NULL;
    }

    // Get the struct type
    LLVMTypeRef struct_type = codegen_lookup_object_type(gen, obj_type_info);
    if (!struct_type) {
        log_error_at(&node->loc, "Could not find struct type for object");
        return NULL;
    }

    // Use GEP to get pointer to the field
    return LLVMBuildStructGEP2(gen->builder, struct_type, obj_ptr,
                               (unsigned)prop_index, "field_ptr");
}

// Helper function to get LLVM type for an expression (identifier or member access)
static LLVMTypeRef codegen_get_llvm_type(CodeGen* gen, ASTNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->identifier.name);
            if (entry && entry->llvm_type) {
                return entry->llvm_type;
            }
            return NULL;
        }

        case AST_MEMBER_ACCESS: {
            ASTNode* obj_node = node->member_access.object;

            // Recursively get the LLVM type of the object
            LLVMTypeRef obj_llvm_type = codegen_get_llvm_type(gen, obj_node);
            if (!obj_llvm_type) {
                return NULL;
            }

            // Get the object's type info (unwrap refs)
            TypeInfo* obj_type_info = obj_node->type_info;
            obj_type_info = type_info_get_ref_target(obj_type_info);

            // Handle ref types (unwrap to get the LLVM type from type_info)
            if (obj_node->type_info && type_info_is_ref(obj_node->type_info)) {
                // For ref types, we need to get the concrete LLVM type from type_info
                obj_llvm_type = get_llvm_type(gen, obj_type_info);
            }

            // Check if the object is a struct and get the field type
            if (LLVMGetTypeKind(obj_llvm_type) == LLVMStructTypeKind) {
                int prop_index = node->member_access.property_index;

                // If property_index wasn't cached during type inference, look it up now
                if (prop_index < 0 && obj_type_info && type_info_is_object(obj_type_info)) {
                    prop_index = type_info_find_property(obj_type_info, node->member_access.property);
                }

                if (prop_index >= 0) {
                    return LLVMStructGetTypeAtIndex(obj_llvm_type, (unsigned)prop_index);
                }
            }

            return NULL;
        }

        default:
            return NULL;
    }
}

// Helper: Get pointer to an lvalue (assignable location) and optionally return its type
// Handles identifier, member access, and index access
// If out_type is not NULL, it will be set to the type of the lvalue
static LLVMValueRef codegen_get_lvalue_ptr_with_type(CodeGen* gen, ASTNode* node, TypeInfo** out_type) {
    if (!node) return NULL;

    switch (node->type) {
        case AST_IDENTIFIER: {
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->identifier.name);
            if (entry && entry->value) {
                if (out_type) *out_type = entry->type_info;
                return entry->value;
            }
            return NULL;
        }

        case AST_MEMBER_ACCESS: {
            if (out_type) *out_type = node->type_info;
            return codegen_member_access_ptr(gen, node);
        }

        case AST_INDEX_ACCESS: {
            // For lvalue pointer, we need RefIndex trait (not Index which is stored in node)
            // because we need a mutable reference for assignment
            TypeInfo* object_type = node->index_access.object->type_info;
            TypeInfo* index_type = node->index_access.index->type_info;
            TypeInfo* index_target_type = type_info_get_ref_target(object_type);

            // Look up RefIndex trait implementation (should be set up during type inference)
            TypeInfo* type_param_bindings[] = { index_type };
            TraitImpl* trait_impl = trait_find_impl(Trait_RefIndex, index_target_type,
                                                    type_param_bindings, 1);
            if (!trait_impl) {
                return NULL;
            }

            // Get the ref_index method
            MethodImpl* ref_index_method = NULL;
            for (int i = 0; i < trait_impl->trait->method_count; i++) {
                if (strcmp(trait_impl->trait->method_names[i], "ref_index") == 0) {
                    ref_index_method = &trait_impl->methods[i];
                    break;
                }
            }

            if (!ref_index_method || ref_index_method->kind != METHOD_INTRINSIC) {
                return NULL;
            }

            // Generate index
            LLVMValueRef index = codegen_node(gen, node->index_access.index);

            if (type_info_is_array(index_target_type)) {
                // Array intrinsic - get pointer to element
                LLVMValueRef array_ptr = NULL;

                // Get array pointer based on object type
                if (node->index_access.object->type == AST_IDENTIFIER) {
                    // Direct identifier - use symbol entry
                    SymbolEntry* entry = node->index_access.symbol_entry;
                    if (!entry || !entry->value) {
                        return NULL;
                    }

                    // Check if this is a stack-allocated array or heap-allocated array
                    // Stack arrays have entry->llvm_type as [N x T], heap arrays have it as ptr
                    if (entry->llvm_type && LLVMGetTypeKind(entry->llvm_type) == LLVMArrayTypeKind) {
                        // Stack-allocated array - entry->value is already pointer to array
                        array_ptr = entry->value;
                    } else {
                        // Heap-allocated array - need to load the pointer
                        array_ptr = LLVMBuildLoad2(gen->builder,
                            LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),
                            entry->value, "array_ptr");
                    }
                } else if (node->index_access.object->type == AST_MEMBER_ACCESS) {
                    // Member access (e.g., c.arr) - get pointer to the member
                    array_ptr = codegen_member_access_ptr(gen, node->index_access.object);
                } else {
                    // For other cases (nested index, etc), recursively get the lvalue pointer
                    array_ptr = codegen_get_lvalue_ptr(gen, node->index_access.object);
                }

                if (!array_ptr) {
                    return NULL;
                }

                TypeInfo* output_type = trait_get_assoc_type(Trait_RefIndex, index_target_type,
                    (TypeInfo*[]){ node->index_access.index->type_info }, 1, "Output");
                LLVMTypeRef output_llvm_type = get_llvm_type(gen, output_type);

                if (out_type) *out_type = output_type;

                LLVMValueRef indices[] = { index };
                return LLVMBuildGEP2(gen->builder, output_llvm_type, array_ptr, indices, 1, "element_ptr");
            }

            return NULL;
        }

        default:
            return NULL;
    }
}

// Helper: Get pointer to an lvalue without type info (convenience wrapper)
static LLVMValueRef codegen_get_lvalue_ptr(CodeGen* gen, ASTNode* node) {
    return codegen_get_lvalue_ptr_with_type(gen, node, NULL);
}

// Helper: Perform struct copy using memcpy
static void codegen_struct_copy(CodeGen* gen, LLVMValueRef dest_ptr, LLVMValueRef src_ptr, TypeInfo* struct_type) {
    // Get or declare memcpy intrinsic
    LLVMValueRef memcpy_func = LLVMGetNamedFunction(gen->module, "llvm.memcpy.p0.p0.i64");
    if (!memcpy_func) {
        LLVMTypeRef memcpy_param_types[] = {
            LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),  // dest
            LLVMPointerType(LLVMInt8TypeInContext(gen->context), 0),  // src
            LLVMInt64TypeInContext(gen->context),                      // len
            LLVMInt1TypeInContext(gen->context)                        // isvolatile
        };
        LLVMTypeRef memcpy_type = LLVMFunctionType(
            LLVMVoidTypeInContext(gen->context),
            memcpy_param_types, 4, 0
        );
        memcpy_func = LLVMAddFunction(gen->module, "llvm.memcpy.p0.p0.i64", memcpy_type);
    }

    // Get struct size
    LLVMTypeRef llvm_struct_type = get_llvm_type(gen, struct_type);
    LLVMValueRef struct_size = LLVMSizeOf(llvm_struct_type);

    // Call memcpy(dest, src, size, isvolatile=false)
    LLVMValueRef memcpy_args[] = {
        dest_ptr,
        src_ptr,
        struct_size,
        LLVMConstInt(LLVMInt1TypeInContext(gen->context), 0, 0)
    };
    LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(memcpy_func),
                  memcpy_func, memcpy_args, 4, "");
}

// Helper: Store a value to a target pointer, handling struct copy if needed
// target_ptr: where to store
// value_node: the AST node containing the value to store
// target_type: the type of the target
static LLVMValueRef codegen_store_to_ptr(CodeGen* gen, LLVMValueRef target_ptr, ASTNode* value_node, TypeInfo* target_type, SourceLocation* loc) {
    // Check if we're storing a struct - need to copy, not store pointer
    if (target_type && type_info_is_object(target_type)) {
        // Get pointer to source struct
        LLVMValueRef src_ptr = codegen_get_lvalue_ptr(gen, value_node);
        if (!src_ptr) {
            log_error_at(loc, "Cannot get source pointer for struct assignment");
            return NULL;
        }

        // Copy the struct
        codegen_struct_copy(gen, target_ptr, src_ptr, target_type);
        return target_ptr;
    }

    // For non-struct types, generate value and store normally
    LLVMValueRef value = codegen_node(gen, value_node);
    if (!value) return NULL;

    LLVMBuildStore(gen->builder, value, target_ptr);
    return value;
}

// Helper: Unified assignment handler - works for all assignment types
// target_node: AST node representing the lvalue (identifier, member access, index)
// value_node: AST node for the value to assign
// loc: source location for error reporting
static LLVMValueRef codegen_assignment(CodeGen* gen, ASTNode* target_node, ASTNode* value_node, SourceLocation* loc) {
    // Get pointer to the target lvalue and its type
    TypeInfo* target_type = NULL;
    LLVMValueRef target_ptr = codegen_get_lvalue_ptr_with_type(gen, target_node, &target_type);
    if (!target_ptr) {
        log_error_at(loc, "Cannot get pointer to assignment target");
        return NULL;
    }

    // Use the store helper to handle both struct copy and regular assignment
    return codegen_store_to_ptr(gen, target_ptr, value_node, target_type, loc);
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
                // Use entry->llvm_type if available (for globals), otherwise compute it
                LLVMTypeRef load_type = entry->llvm_type ? entry->llvm_type : get_llvm_type(gen, type_info);
                return LLVMBuildLoad2(gen->builder, load_type,
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
            // Special handling for "ref" operator - return pointer, don't load
            if (strcmp(node->unary_op.op, "ref") == 0) {
                ASTNode* operand_node = node->unary_op.operand;

                // For identifiers, return the pointer to the variable
                if (operand_node->type == AST_IDENTIFIER) {
                    SymbolEntry* entry = symbol_table_lookup(gen->symbols, operand_node->identifier.name);
                    if (entry && entry->value) {
                        return entry->value;  // Return the pointer
                    }
                    log_error_at(&node->loc, "Undefined variable in ref expression: %s", operand_node->identifier.name);
                    return NULL;
                }

                // For member access, return pointer to the field
                if (operand_node->type == AST_MEMBER_ACCESS) {
                    return codegen_member_access_ptr(gen, operand_node);
                }

                log_error_at(&node->loc, "ref operator can only be applied to variables or member access");
                return NULL;
            }

            // For other unary operators, evaluate the operand normally
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
            const char* op = node->prefix_op.op;
            bool is_increment = (strcmp(op, "++") == 0);

            LLVMValueRef var_ptr = NULL;
            TypeInfo* var_type_info = NULL;

            // Handle either simple variable or member/index access
            if (node->prefix_op.target) {
                // Member or index access (e.g., ++obj.field, ++arr[i])
                ASTNode* target = node->prefix_op.target;

                if (target->type == AST_MEMBER_ACCESS) {
                    // Get pointer to the member using helper
                    var_ptr = codegen_member_access_ptr(gen, target);
                    var_type_info = target->type_info;

                    if (!var_ptr) {
                        return NULL;
                    }
                } else {
                    log_error_at(&node->loc, "Prefix operator on index access not yet supported");
                    return NULL;
                }
            } else {
                // Simple variable (e.g., ++i)
                const char* var_name = node->prefix_op.name;
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, var_name);

                if (!entry || !entry->value) {
                    log_error_at(&node->loc, "Undefined variable in prefix operator: %s", var_name);
                    return NULL;
                }
                if (entry->is_const) {
                    log_error_at(&node->loc, "Cannot modify const variable: %s", var_name);
                    return NULL;
                }

                var_ptr = entry->value;
                var_type_info = entry->type_info;
            }

            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, var_type_info),
                                                  var_ptr, "current");
            LLVMValueRef one, new_value;

            if (type_info_is_double(var_type_info)) {
                one = LLVMConstReal(LLVMDoubleTypeInContext(gen->context), 1.0);
                new_value = is_increment ? LLVMBuildFAdd(gen->builder, current, one, "preinc")
                                         : LLVMBuildFSub(gen->builder, current, one, "predec");
            } else {
                one = LLVMConstInt(get_llvm_type(gen, var_type_info), 1, 0);
                new_value = is_increment ? LLVMBuildAdd(gen->builder, current, one, "preinc")
                                         : LLVMBuildSub(gen->builder, current, one, "predec");
            }

            LLVMBuildStore(gen->builder, new_value, var_ptr);
            return new_value;  // Return the new value
        }

        case AST_POSTFIX_OP: {
            // i++ or i--: return old value, then increment/decrement
            const char* op = node->postfix_op.op;
            bool is_increment = (strcmp(op, "++") == 0);

            LLVMValueRef var_ptr = NULL;
            TypeInfo* var_type_info = NULL;

            // Handle either simple variable or member/index access
            if (node->postfix_op.target) {
                // Member or index access (e.g., obj.field++, arr[i]++)
                ASTNode* target = node->postfix_op.target;

                if (target->type == AST_MEMBER_ACCESS) {
                    // Get pointer to the member using helper
                    var_ptr = codegen_member_access_ptr(gen, target);
                    var_type_info = target->type_info;

                    if (!var_ptr) {
                        return NULL;
                    }
                } else {
                    log_error_at(&node->loc, "Postfix operator on index access not yet supported");
                    return NULL;
                }
            } else {
                // Simple variable (e.g., i++)
                const char* var_name = node->postfix_op.name;
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, var_name);

                if (!entry || !entry->value) {
                    log_error_at(&node->loc, "Undefined variable in postfix operator: %s", var_name);
                    return NULL;
                }
                if (entry->is_const) {
                    log_error_at(&node->loc, "Cannot modify const variable: %s", var_name);
                    return NULL;
                }

                var_ptr = entry->value;
                var_type_info = entry->type_info;
            }

            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, var_type_info),
                                                  var_ptr, "current");

            // Use trait system for increment/decrement
            Trait* trait = is_increment ? Trait_AddAssign : Trait_SubAssign;
            const char* method_name = is_increment ? "add_assign" : "sub_assign";

            LLVMValueRef one;
            if (type_info_is_double(var_type_info)) {
                one = LLVMConstReal(LLVMDoubleTypeInContext(gen->context), 1.0);
            } else {
                one = LLVMConstInt(get_llvm_type(gen, var_type_info), 1, 0);
            }

            LLVMValueRef new_value = NULL;
            if (trait && var_type_info) {
                MethodImpl* method = trait_get_binary_method(trait, var_type_info, var_type_info, method_name);

                if (method && method->kind == METHOD_INTRINSIC && method->codegen) {
                    LLVMValueRef args[] = { current, one };
                    new_value = method->codegen(gen, args, 2);
                }
            }

            // Fallback if trait lookup failed
            if (!new_value) {
                if (type_info_is_double(var_type_info)) {
                    new_value = is_increment ? LLVMBuildFAdd(gen->builder, current, one, "postinc")
                                             : LLVMBuildFSub(gen->builder, current, one, "postdec");
                } else {
                    new_value = is_increment ? LLVMBuildAdd(gen->builder, current, one, "postinc")
                                             : LLVMBuildSub(gen->builder, current, one, "postdec");
                }
            }

            LLVMBuildStore(gen->builder, new_value, var_ptr);
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
                entry->array_size = 0;
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

                // Update the symbol entry (created during type inference) with LLVM-specific data
                if (node->var_decl.symbol_entry) {
                    node->var_decl.symbol_entry->value = obj_ptr;          // LLVM pointer to the struct
                    node->var_decl.symbol_entry->llvm_type = struct_type;  // Struct type for GEP
                }

                return obj_ptr;
            }

            // Regular variable handling
            LLVMValueRef init_value = NULL;
            TypeInfo* var_type_info = node->type_info;

            // Determine the LLVM type
            LLVMTypeRef var_llvm_type;
            if (var_type_info && type_info_is_array(var_type_info) && node->var_decl.array_size > 0) {
                // For stack-allocated arrays with known size, create array type
                TypeInfo* elem_type = var_type_info->data.array.element_type;
                LLVMTypeRef elem_llvm_type = get_llvm_type(gen, elem_type);
                var_llvm_type = LLVMArrayType2(elem_llvm_type, node->var_decl.array_size);
            } else if (var_type_info && type_info_is_array(var_type_info) && node->var_decl.array_size == 0) {
                // Array size evaluation failed (error already reported), skip codegen
                log_error_at(&node->loc, "Cannot generate code for array with invalid size");
                return NULL;
            } else if (var_type_info && type_info_is_array(var_type_info)) {
                // For dynamic arrays, we'll determine the type from init_value later
                var_llvm_type = NULL;
            } else {
                var_llvm_type = get_llvm_type(gen, var_type_info);
            }

            // Check if this is a global variable (parent scope is NULL)
            bool is_global = (gen->symbols->parent == NULL);

            if (is_global) {
                // Check if the global variable already exists (from PASS 0.5)
                SymbolEntry* existing = symbol_table_lookup(gen->symbols, node->var_decl.name);

                if (existing && existing->value) {
                    // Variable already declared in PASS 0.5, now initialize it with non-constant value
                    if (node->var_decl.init) {
                        init_value = codegen_node(gen, node->var_decl.init);

                        // For struct types, if init_value is a pointer (from identifier),
                        // we need to load it for value semantics (copy)
                        if (var_type_info && type_info_is_object(var_type_info) &&
                            LLVMGetTypeKind(LLVMTypeOf(init_value)) == LLVMPointerTypeKind) {
                            init_value = LLVMBuildLoad2(gen->builder, var_llvm_type, init_value, "struct_copy");
                        }

                        if (init_value) {
                            LLVMBuildStore(gen->builder, init_value, existing->value);
                        }
                    }
                    return existing->value;
                }

                // First time seeing this variable (PASS 0.5) - declare it
                if (node->var_decl.init && !(var_type_info && type_info_is_array(var_type_info))) {
                    // Don't generate array literals during declaration - they need runtime initialization
                    init_value = codegen_node(gen, node->var_decl.init);

                    if (!var_type_info && node->var_decl.init->type_info) {
                        var_type_info = node->var_decl.init->type_info;
                    }

                    if (!var_llvm_type) {
                        var_llvm_type = get_llvm_type(gen, var_type_info);
                    }
                }

                // For arrays, create a pointer type global
                if (var_type_info && type_info_is_array(var_type_info) && !var_llvm_type) {
                    TypeInfo* elem_type = var_type_info->data.array.element_type;
                    LLVMTypeRef elem_llvm_type = get_llvm_type(gen, elem_type);
                    var_llvm_type = LLVMPointerType(elem_llvm_type, 0);
                }

                // Create global variable
                LLVMValueRef global = LLVMAddGlobal(gen->module, var_llvm_type, node->var_decl.name);

                // Always initialize globals to zero
                // Actual initialization happens in PASS 2 (in order with other statements)
                LLVMSetInitializer(global, LLVMConstNull(var_llvm_type));

                // Update the symbol entry (created during type inference) with LLVM value and type
                if (node->var_decl.symbol_entry) {
                    node->var_decl.symbol_entry->value = global;
                    node->var_decl.symbol_entry->llvm_type = var_llvm_type;
                }

                return global;
            } else {
                // Local variable - use alloca as before

                // Special handling for stack-allocated arrays
                if (var_type_info && type_info_is_array(var_type_info) && node->var_decl.array_size > 0) {
                    // Use the array type created earlier (var_llvm_type)
                    LLVMTypeRef array_type = var_llvm_type;

                    // Allocate on stack (in entry block)
                    LLVMValueRef alloca = codegen_create_entry_block_alloca(gen, array_type, node->var_decl.name);

                    // Zero-initialize the array
                    LLVMValueRef zero = LLVMConstNull(array_type);
                    LLVMBuildStore(gen->builder, zero, alloca);

                    // Update the symbol entry (created during type inference) with LLVM value and type
                    if (node->var_decl.symbol_entry) {
                        node->var_decl.symbol_entry->value = alloca;
                        node->var_decl.symbol_entry->llvm_type = array_type;
                    }

                    return alloca;
                }

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

                LLVMValueRef alloca = codegen_create_entry_block_alloca(gen, var_llvm_type, node->var_decl.name);
                LLVMBuildStore(gen->builder, init_value, alloca);

                // Update the symbol entry (created during type inference) with LLVM value
                if (node->var_decl.symbol_entry) {
                    node->var_decl.symbol_entry->value = alloca;
                    node->var_decl.symbol_entry->llvm_type = var_llvm_type;
                }

                return alloca;
            }
        }

        case AST_ASSIGNMENT: {
            // Check that the variable exists and is not const
            SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->assignment.name);
            if (!entry || !entry->value) {
                log_error_at(&node->loc, "Undefined variable in assignment: %s", node->assignment.name);
                return NULL;
            }
            if (entry->is_const) {
                log_error_at(&node->loc, "Cannot assign to const variable: %s", node->assignment.name);
                return NULL;
            }

            // Create an identifier node for the target and use unified assignment
            ASTNode target = {
                .type = AST_IDENTIFIER,
                .identifier = { .name = node->assignment.name }
            };
            return codegen_assignment(gen, &target, node->assignment.value, &node->loc);
        }

        case AST_COMPOUND_ASSIGNMENT: {
            // Create target node for unified handling
            ASTNode* target_node = NULL;
            ASTNode temp_identifier;

            if (node->compound_assignment.name) {
                // Simple identifier case - check const
                SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->compound_assignment.name);
                if (!entry || !entry->value) {
                    log_error_at(&node->loc, "Undefined variable in compound assignment: %s", node->compound_assignment.name);
                    return NULL;
                }
                if (entry->is_const) {
                    log_error_at(&node->loc, "Cannot assign to const variable: %s", node->compound_assignment.name);
                    return NULL;
                }

                // Create temporary identifier node
                temp_identifier = (ASTNode){
                    .type = AST_IDENTIFIER,
                    .identifier = { .name = node->compound_assignment.name }
                };
                target_node = &temp_identifier;
            } else if (node->compound_assignment.target) {
                // Member access or index access case
                target_node = node->compound_assignment.target;
            } else {
                log_error_at(&node->loc, "Invalid compound assignment - no target");
                return NULL;
            }

            // Get target pointer and type using unified helper
            TypeInfo* target_type = NULL;
            LLVMValueRef target_ptr = codegen_get_lvalue_ptr_with_type(gen, target_node, &target_type);

            if (!target_ptr) {
                log_error_at(&node->loc, "Cannot get pointer to compound assignment target");
                return NULL;
            }

            // Load current value
            LLVMValueRef current = LLVMBuildLoad2(gen->builder, get_llvm_type(gen, target_type),
                                                  target_ptr, "current");

            // Generate the right-hand side value
            LLVMValueRef rhs = codegen_node(gen, node->compound_assignment.value);

            // Use trait system for all compound assignments
            const char* op = node->compound_assignment.op;
            Trait* trait;
            const char* method_name;
            operator_get_trait_and_method(op, &trait, &method_name);

            LLVMValueRef new_value = NULL;
            if (trait && method_name && target_type && node->compound_assignment.value->type_info) {
                MethodImpl* method = trait_get_binary_method(trait, target_type,
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
            LLVMBuildStore(gen->builder, new_value, target_ptr);

            return new_value;
        }

        case AST_INDEX_ASSIGNMENT: {
            // Create a temporary index access node for the target and use unified assignment
            ASTNode target = {
                .type = AST_INDEX_ACCESS,
                .index_access = {
                    .object = node->index_assignment.object,
                    .index = node->index_assignment.index,
                    .trait_impl = node->index_assignment.trait_impl,
                    .symbol_entry = node->index_assignment.symbol_entry
                },
                .type_info = node->type_info
            };

            return codegen_assignment(gen, &target, node->index_assignment.value, &node->loc);
        }

        case AST_CALL: {
            // Check if this is a member access call (e.g., console.log() or math.add())
            if (node->call.callee->type == AST_MEMBER_ACCESS) {
                ASTNode* obj = node->call.callee->member_access.object;
                char* prop = node->call.callee->member_access.property;

                // Check if this is a namespace member access (e.g., math.add)
                if (obj->type == AST_IDENTIFIER) {
                    SymbolEntry* obj_entry = node->call.callee->member_access.symbol_entry;

                    // If the object is a namespace (imported module)
                    if (symbol_is_namespace(obj_entry)) {
                        Module* imported_module = symbol_get_imported_module(obj_entry);

                        // Find the exported symbol
                        ExportedSymbol* exported = module_find_export(imported_module, prop);
                        if (exported && exported->declaration && exported->declaration->type == AST_FUNCTION_DECL) {
                            // Use the mangled function name
                            char* mangled_name = module_mangle_symbol(imported_module->module_prefix, prop);

                            // Fall through to regular function call logic with the mangled name
                            // We'll handle this by treating it as a regular identifier call
                            // Create a temporary identifier node with the mangled name
                            ASTNode temp_callee = *node->call.callee;
                            temp_callee.type = AST_IDENTIFIER;
                            temp_callee.identifier.name = mangled_name;

                            // Temporarily replace the callee
                            node->call.callee = &temp_callee;

                            // Fall through to regular call handling below
                            goto handle_regular_call;
                        }
                    }

                    // Not a namespace, try runtime function
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

        handle_regular_call:

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

            // First pass: get argument type infos for function lookup
            TypeInfo** arg_type_infos = (TypeInfo**)malloc(sizeof(TypeInfo*) * node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                arg_type_infos[i] = node->call.args[i]->type_info;
            }

            // Try to find specialized version
            LLVMValueRef func = NULL;
            FunctionSpecialization* spec = NULL;
            if (gen->type_ctx && node->call.arg_count > 0) {
                spec = specialization_context_find_by_type_info(
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
                    free(arg_type_infos);
                    return runtime_result;
                }

                // Function not found anywhere
                log_error_at(&node->loc, "Undefined function: %s", func_name);
                free(arg_type_infos);
                return NULL;
            }

            // Look up function type to check parameter types (for both specialized and external functions)
            TypeInfo* func_type_info = type_context_find_function_type(gen->type_ctx, func_name);

            // Second pass: generate arguments, checking if param expects ref
            LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                ASTNode* arg_node = node->call.args[i];

                // Check if this parameter expects a ref type
                bool param_is_ref = false;

                // First try to get param type from specialization
                if (spec && i < spec->param_count && spec->param_type_info[i]) {
                    param_is_ref = type_info_is_ref(spec->param_type_info[i]);
                }
                // Fall back to function type (for external functions without specializations)
                else if (func_type_info && func_type_info->data.function.specializations) {
                    FunctionSpecialization* func_spec = func_type_info->data.function.specializations;
                    if (i < func_spec->param_count && func_spec->param_type_info[i]) {
                        param_is_ref = type_info_is_ref(func_spec->param_type_info[i]);
                    }
                }

                // For member access with struct type and ref parameter, use pointer
                if (param_is_ref && arg_node->type == AST_MEMBER_ACCESS &&
                    arg_node->type_info && type_info_is_object(arg_node->type_info)) {
                    args[i] = codegen_member_access_ptr(gen, arg_node);
                } else {
                    args[i] = codegen_node(gen, arg_node);
                }

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

            // Don't name void function calls
            const char* call_name = (type_info_is_void_ctx(node->type_info, gen->type_ctx)) ? "" : "calltmp";
            LLVMValueRef result = LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(func),
                                                func, args, node->call.arg_count, call_name);
            free(args);
            free(arg_type_infos);

            return result;
        }

        case AST_METHOD_CALL: {
            // Method call: obj.method(args) or Type.method(args)
            // Check if this is a static method call (Type.method) or instance method (obj.method)
            bool is_static = node->method_call.is_static;
            LLVMValueRef obj_ptr = NULL;
            TypeInfo* obj_type = NULL;

            if (is_static) {
                // Static method call: Type.method(args)
                // Type info should already be set by type inference
                obj_type = node->method_call.object->type_info;
                if (!obj_type) {
                    log_error_at(&node->loc, "Static method call missing type info");
                    return NULL;
                }
            } else {
                // Instance method call: obj.method(args)
                // First check if this is a namespace member call (e.g., math.add())
                if (node->method_call.object->type == AST_IDENTIFIER) {
                    SymbolEntry* entry = symbol_table_lookup(gen->symbols, node->method_call.object->identifier.name);

                    // Check if this is a namespace
                    if (symbol_is_namespace(entry)) {
                        // This is a namespace call! Handle it as a regular function call
                        Module* imported_module = symbol_get_imported_module(entry);
                        const char* member_name = node->method_call.method_name;

                        // Check if the exported function has a codegen callback
                        ExportedSymbol* exported = module_find_export(imported_module, member_name);
                        if (exported && exported->declaration && 
                            exported->declaration->type == AST_FUNCTION_DECL &&
                            exported->declaration->func_decl.codegen_callback) {
                            // Use the custom codegen callback
                            return exported->declaration->func_decl.codegen_callback(gen, node);
                        }

                        // Find the specialization in the module's TypeContext
                        TypeContext* module_type_ctx = imported_module->ast->type_ctx;
                        TypeInfo* func_type = type_context_find_function_type(module_type_ctx, member_name);

                        if (!func_type) {
                            log_error_at(&node->loc, "Function '%s' not found in module '%s'",
                                        member_name, imported_module->relative_path);
                            return NULL;
                        }

                        // Get argument types for specialization lookup
                        TypeInfo** arg_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * node->method_call.arg_count);
                        for (int i = 0; i < node->method_call.arg_count; i++) {
                            arg_types[i] = node->method_call.args[i]->type_info;
                        }

                        // Find the specialization
                        FunctionSpecialization* spec = specialization_context_find_by_type_info(
                            module_type_ctx, member_name, arg_types, node->method_call.arg_count);
                        free(arg_types);

                        if (!spec) {
                            log_error_at(&node->loc, "No specialization found for %s.%s",
                                        imported_module->relative_path, member_name);
                            return NULL;
                        }

                        // Get the function from the module
                        LLVMValueRef func = LLVMGetNamedFunction(gen->module, spec->specialized_name);
                        if (!func) {
                            log_error_at(&node->loc, "Function '%s' not generated", spec->specialized_name);
                            return NULL;
                        }

                        // Generate arguments
                        LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * node->method_call.arg_count);
                        for (int i = 0; i < node->method_call.arg_count; i++) {
                            args[i] = codegen_node(gen, node->method_call.args[i]);
                            if (!args[i]) {
                                free(args);
                                return NULL;
                            }
                        }

                        // Call the function
                        // Use empty name for void functions
                        const char* call_name = (node->type_info == Type_Void) ? "" : "namespace_call";
                        LLVMValueRef result = LLVMBuildCall2(gen->builder,
                            LLVMGlobalGetValueType(func), func, args, node->method_call.arg_count,
                            call_name);

                        free(args);
                        return result;
                    }

                    // Not a namespace - regular instance method call
                    if (!entry || !entry->value) {
                        log_error_at(&node->loc, "Undefined variable: %s", node->method_call.object->identifier.name);
                        return NULL;
                    }
                    obj_ptr = entry->value;
                } else if (node->method_call.object->type == AST_MEMBER_ACCESS) {
                    // Member access - we need to get a pointer to it
                    // First check if the method expects a ref for the first parameter
                    obj_type = node->method_call.object->type_info;
                    if (!obj_type || !type_info_is_object(obj_type)) {
                        log_error_at(&node->loc, "Cannot call method on non-object type");
                        return NULL;
                    }

                    const char* type_name = obj_type->type_name ? obj_type->type_name : "unknown";
                    char method_full_name[256];
                    snprintf(method_full_name, sizeof(method_full_name), "%s.%s",
                            type_name, node->method_call.method_name);

                    // Look up the method to check if first param is ref
                    TypeInfo* method_type = type_context_find_function_type(gen->type_ctx, method_full_name);
                    bool first_param_is_ref = false;
                    if (method_type && method_type->data.function.specializations) {
                        FunctionSpecialization* spec = method_type->data.function.specializations;
                        if (spec->param_count > 0 && spec->param_type_info[0]) {
                            first_param_is_ref = type_info_is_ref(spec->param_type_info[0]);
                        }
                    }

                    // Get pointer to the member access
                    if (first_param_is_ref) {
                        obj_ptr = codegen_member_access_ptr(gen, node->method_call.object);
                    } else {
                        obj_ptr = codegen_node(gen, node->method_call.object);
                    }
                } else {
                    // Complex expression - generate it
                    obj_ptr = codegen_node(gen, node->method_call.object);
                }

                if (!obj_ptr) {
                    log_error_at(&node->loc, "Failed to generate object for method call");
                    return NULL;
                }

                // Get the object's type (if not already set above for member access)
                if (!obj_type) {
                    obj_type = node->method_call.object->type_info;
                }
                if (obj_type && type_info_is_ref(obj_type)) {
                    // If it's a ref type, unwrap to get the target type
                    obj_type = type_info_get_ref_target(obj_type);

                    // Check if we need to dereference (same logic as member access)
                    bool is_function_param = (LLVMGetValueKind(obj_ptr) == LLVMArgumentValueKind);
                    if (!is_function_param) {
                        // Load the pointer for ref variables
                        LLVMTypeRef ptr_type = LLVMPointerType(get_llvm_type(gen, obj_type), 0);
                        obj_ptr = LLVMBuildLoad2(gen->builder, ptr_type, obj_ptr, "deref");
                    }
                }
            }

            if (!obj_type || !type_info_is_object(obj_type)) {
                log_error_at(&node->loc, "Cannot call method on non-object type");
                return NULL;
            }

            // Build the method name: TypeName.methodName
            const char* type_name = obj_type->type_name ? obj_type->type_name : "unknown";
            char method_full_name[256];
            snprintf(method_full_name, sizeof(method_full_name), "%s.%s",
                    type_name, node->method_call.method_name);

            // Look up the method function
            LLVMValueRef method_func = LLVMGetNamedFunction(gen->module, method_full_name);
            if (!method_func) {
                log_error_at(&node->loc, "Method '%s' not found", method_full_name);
                return NULL;
            }

            // Generate arguments
            // For instance methods, first arg is 'self' (pointer to object)
            // For static methods, no implicit self parameter
            int arg_offset = is_static ? 0 : 1;
            int total_args = arg_offset + node->method_call.arg_count;
            LLVMValueRef* args = (LLVMValueRef*)malloc(sizeof(LLVMValueRef) * total_args);

            if (!is_static) {
                args[0] = obj_ptr;  // self parameter
            }

            // Generate the rest of the arguments
            for (int i = 0; i < node->method_call.arg_count; i++) {
                args[arg_offset + i] = codegen_node(gen, node->method_call.args[i]);
                if (!args[arg_offset + i]) {
                    free(args);
                    log_error_at(&node->loc, "Failed to generate argument %d", i);
                    return NULL;
                }
            }

            // Call the method
            LLVMTypeRef method_type = LLVMGlobalGetValueType(method_func);

            // Check if method returns void - don't name void results
            LLVMTypeRef return_type = LLVMGetReturnType(method_type);
            const char* call_name = (LLVMGetTypeKind(return_type) == LLVMVoidTypeKind) ? "" : "method_call";

            LLVMValueRef result = LLVMBuildCall2(gen->builder, method_type, method_func,
                                                args, total_args, call_name);

            free(args);
            return result;
        }

        case AST_MEMBER_ACCESS: {
            // Check for trait-based properties like "length"
            if (strcmp(node->member_access.property, "length") == 0) {
                TypeInfo* obj_type = node->member_access.object->type_info;
                TypeInfo* target_type = type_info_get_ref_target(obj_type);

                // Check if this is an array with Length trait
                if (type_info_is_array(target_type)) {
                    // Use the helper function to get the LLVM type recursively
                    LLVMTypeRef array_llvm_type = codegen_get_llvm_type(gen, node->member_access.object);

                    if (array_llvm_type && LLVMGetTypeKind(array_llvm_type) == LLVMArrayTypeKind) {
                        unsigned length = LLVMGetArrayLength2(array_llvm_type);
                        return LLVMConstInt(LLVMInt32TypeInContext(gen->context), length, 0);
                    }

                    log_error_at(&node->loc, "Cannot get length of this array");
                    return NULL;
                }
            }

            // Get pointer to the field
            LLVMValueRef field_ptr = codegen_member_access_ptr(gen, node);
            if (!field_ptr) return NULL;

            TypeInfo* field_type_info = node->type_info;

            // For array types, return the pointer directly (arrays are used by reference)
            if (type_info_is_array(field_type_info)) {
                return field_ptr;
            }

            // For non-array types, load the value from the field
            LLVMTypeRef field_llvm_type = get_llvm_type(gen, field_type_info);
            return LLVMBuildLoad2(gen->builder, field_llvm_type, field_ptr, "field_value");
        }

        case AST_MEMBER_ASSIGNMENT: {
            // Create a member access node for the target
            // We need to set property_index for codegen_member_access_ptr to work
            TypeInfo* obj_type_info = node->member_assignment.object->type_info;
            if (obj_type_info && type_info_is_ref(obj_type_info)) {
                obj_type_info = type_info_get_ref_target(obj_type_info);
            }

            int prop_idx = -1;
            if (obj_type_info && type_info_is_object(obj_type_info)) {
                prop_idx = type_info_find_property(obj_type_info, node->member_assignment.property);
            }

            if (prop_idx == -1) {
                log_error_at(&node->loc, "Property '%s' not found", node->member_assignment.property);
                return NULL;
            }

            // Create temporary member access node with property_index set
            ASTNode target = {
                .type = AST_MEMBER_ACCESS,
                .member_access = {
                    .object = node->member_assignment.object,
                    .property = node->member_assignment.property,
                    .property_index = prop_idx
                },
                .type_info = obj_type_info->data.object.property_types[prop_idx]
            };

            return codegen_assignment(gen, &target, node->member_assignment.value, &node->loc);
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
            if (type_info_is_array_of(node->type_info, Type_I32)) {
                elem_type = LLVMInt32TypeInContext(gen->context);
                elem_size = 4;
            } else if (type_info_is_array_of(node->type_info, Type_Double)) {
                elem_type = LLVMDoubleTypeInContext(gen->context);
                elem_size = 8;
            } else if (type_info_is_array_of(node->type_info, Type_String)) {
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

            // Allocate struct on the stack (in entry block)
            LLVMValueRef obj_ptr = codegen_create_entry_block_alloca(gen, struct_type, "obj");

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
            // Use the trait implementation resolved during type inference
            TraitImpl* trait_impl = node->index_access.trait_impl;
            if (!trait_impl) {
                // Error already reported during type inference
                return NULL;
            }

            LLVMValueRef index = codegen_node(gen, node->index_access.index);
            TypeInfo* object_type = node->index_access.object->type_info;

            // If object is a ref type, we need to work with the target type for indexing
            TypeInfo* index_target_type = type_info_get_ref_target(object_type);
            bool is_ref = (object_type->kind == TYPE_KIND_REF);

            // Get the Index method from the stored trait implementation
            MethodImpl* index_method = NULL;
            for (int i = 0; i < trait_impl->trait->method_count; i++) {
                if (strcmp(trait_impl->trait->method_names[i], "index") == 0) {
                    index_method = &trait_impl->methods[i];
                    break;
                }
            }

            if (!index_method) {
                log_error_at(&node->loc, "Index trait implementation missing 'index' method");
                return NULL;
            }

            // Get the output type from the trait (should be in node->type_info already)
            TypeInfo* output_type = node->type_info;
            LLVMTypeRef output_llvm_type = get_llvm_type(gen, output_type);

            // Handle based on implementation kind
            if (index_method->kind == METHOD_INTRINSIC) {
                // For intrinsic implementations, we handle codegen inline here
                // because they need access to AST node structure and symbol table
                // that can't be easily passed through the intrinsic function pointer
                //
                // Future improvement: Pass additional context to intrinsics so they
                // can be fully self-contained (see intrinsic_array_index comment)

                // Array intrinsic implementation
                if (type_info_is_array(index_target_type)) {
                    // Use stored symbol entry (resolved during type inference)
                    SymbolEntry* entry = node->index_access.symbol_entry;

                    if (entry) {
                        // Object is an identifier - use symbol entry
                        if (!entry->value) {
                            log_error_at(&node->loc, "Array variable has no value");
                            return NULL;
                        }

                        LLVMValueRef array_ptr = entry->value;

                        // If object is a ref, dereference it first
                        if (is_ref) {
                            LLVMTypeRef target_llvm_type = get_llvm_type(gen, index_target_type);
                            array_ptr = LLVMBuildLoad2(gen->builder, target_llvm_type, array_ptr, "deref");
                        }

                        // For stack-allocated arrays, use GEP with [0, index]
                        if (!is_ref && entry->array_size > 0) {
                            LLVMValueRef indices[2] = {
                                LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0),
                                index
                            };
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(gen->builder, entry->llvm_type,
                                                                  array_ptr, indices, 2, "elem_ptr");
                            return LLVMBuildLoad2(gen->builder, output_llvm_type, elem_ptr, "elem");
                        } else {
                            // Dynamic array (heap-allocated) - single index GEP
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(gen->builder, output_llvm_type, array_ptr,
                                                                  &index, 1, "elem_ptr");
                            return LLVMBuildLoad2(gen->builder, output_llvm_type, elem_ptr, "elem");
                        }
                    } else {
                        // Complex expression - generate it
                        LLVMValueRef object = codegen_node(gen, node->index_access.object);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(gen->builder, output_llvm_type, object,
                                                              &index, 1, "elem_ptr");
                        return LLVMBuildLoad2(gen->builder, output_llvm_type, elem_ptr, "elem");
                    }
                }
                // String intrinsic implementation
                else if (type_info_is_string(index_target_type)) {
                    LLVMValueRef object = codegen_node(gen, node->index_access.object);
                    LLVMValueRef char_ptr = LLVMBuildGEP2(gen->builder,
                                                           LLVMInt8TypeInContext(gen->context),
                                                           object, &index, 1, "char_ptr");
                    return LLVMBuildLoad2(gen->builder, LLVMInt8TypeInContext(gen->context),
                                         char_ptr, "char");
                }
                else {
                    log_error_at(&node->loc, "Intrinsic Index implementation not supported for type '%s'",
                                object_type->type_name ? object_type->type_name : "?");
                    return NULL;
                }
            }
            // For user-defined Index implementations (future)
            else if (index_method->kind == METHOD_FUNCTION) {
                // TODO: Call user-defined function
                // LLVMValueRef object = codegen_node(gen, node->index_access.object);
                // LLVMValueRef args[] = { object, index };
                // return LLVMBuildCall2(gen->builder, ..., args, 2, "index_call");
                log_error_at(&node->loc, "User-defined Index trait not yet supported");
                return NULL;
            }
            else {
                log_error_at(&node->loc, "Unknown Index trait implementation kind");
                return NULL;
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
            // Use the for loop's scope (created during type inference)
            SymbolTable* prev_scope = gen->symbols;
            if (node->symbol_table) {
                gen->symbols = node->symbol_table;
            }

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

            // Restore previous scope
            gen->symbols = prev_scope;

            return NULL;
        }

        // AST_FUNCTION_DECL is never hit - functions are generated from the type table
        // via codegen_specialized_function in PASS 1, not by visiting AST nodes

        case AST_BLOCK:
        case AST_PROGRAM: {
            // Use the block's scope if it has one (created during type inference)
            SymbolTable* prev_scope = gen->symbols;
            if (node->symbol_table) {
                gen->symbols = node->symbol_table;
            }

            for (int i = 0; i < node->program.count; i++) {
                // Check if current block is already terminated
                LLVMBasicBlockRef current_block = LLVMGetInsertBlock(gen->builder);
                if (current_block && LLVMGetBasicBlockTerminator(current_block)) {
                    // Block already terminated, skip remaining statements
                    break;
                }
                codegen_node(gen, node->program.statements[i]);
            }

            // Restore previous scope
            gen->symbols = prev_scope;
            return NULL;
        }

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

    // Save previous function, entry block, and scope
    LLVMValueRef prev_func = gen->current_function;
    LLVMBasicBlockRef prev_entry = gen->entry_block;
    gen->current_function = func;
    gen->entry_block = entry;

    // Use the symbol table from the specialized body (created during type inference)
    // This contains parameters and local variables with their types already resolved
    // The parent chain already points to the global scope from type inference, which is
    // the same global scope being used during codegen (gen->symbols)
    SymbolTable* prev_scope = gen->symbols;
    if (body->symbol_table) {
        gen->symbols = body->symbol_table;
    } else {
        // Fallback: create a new scope (shouldn't happen for properly inferred functions)
        log_warning("Specialized body for %s has no symbol table, creating new one", spec->specialized_name);
        gen->symbols = symbol_table_create(gen->symbols);
    }

    // Update parameter entries with LLVM values
    // The symbol table already has parameters from type inference, we just need to populate LLVM values
    for (int i = 0; i < spec->param_count; i++) {
        LLVMValueRef param = LLVMGetParam(func, i);
        LLVMSetValueName(param, param_names[i]);

        // Look up the existing parameter entry from type inference
        SymbolEntry* param_entry = symbol_table_lookup(gen->symbols, param_names[i]);
        if (!param_entry) {
            log_error("Parameter '%s' not found in symbol table from type inference", param_names[i]);
            continue;
        }

        LLVMValueRef param_value;

        // Check if this is a ref type wrapping an object
        TypeInfo* param_type = spec->param_type_info[i];
        bool is_ref_to_object = type_info_is_ref(param_type) &&
                                type_info_is_object(type_info_get_ref_target(param_type));

        // For refs to objects, use the parameter pointer directly (it's already a pointer)
        // For all other types (including value objects), allocate and store
        if (is_ref_to_object) {
            param_value = param;  // Use pointer directly for ref types
        } else {
            LLVMValueRef alloca = codegen_create_entry_block_alloca(gen, param_types[i],
                                                                     param_names[i]);
            LLVMBuildStore(gen->builder, param, alloca);
            param_value = alloca;
        }

        // Update the existing symbol entry with LLVM value and type
        param_entry->value = param_value;
        param_entry->llvm_type = param_types[i];

        // For object parameters, ensure llvm_type is set to the struct type
        if (type_info_is_object(param_type) || is_ref_to_object) {
            TypeInfo* lookup_type = is_ref_to_object ?
                                    type_info_get_ref_target(param_type) : param_type;
            LLVMTypeRef struct_type = codegen_lookup_object_type(gen, lookup_type);
            if (struct_type) {
                param_entry->llvm_type = struct_type;
                log_verbose_indent(2, "Parameter '%s' has struct type with %d properties",
                                 param_names[i], lookup_type->data.object.property_count);
            }
        }
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

    // Restore scope, function, and entry block
    // Note: Don't free the symbol table - it's owned by the specialized_body AST node
    gen->symbols = prev_scope;
    gen->current_function = prev_func;
    gen->entry_block = prev_entry;

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

    // First, search through current module's type table by type_name (handles cloned TypeInfo)
    TypeEntry* entry = gen->type_ctx->type_table;
    while (entry) {
        if (entry->type && entry->type->type_name &&
            strcmp(entry->type->type_name, type_info->type_name) == 0 &&
            entry->llvm_type) {
            return entry->llvm_type;
        }
        entry = entry->next;
    }
    
    // Not found in current module - the type might be from an imported module
    // Since type_info was resolved during type inference, it should have an llvm_type
    // somewhere in one of the module TypeContexts. Search imported modules.
    if (gen->symbols) {
        SymbolEntry* sym_entry = gen->symbols->head;
        while (sym_entry) {
            if (symbol_is_namespace(sym_entry)) {
                Module* imported_module = symbol_get_imported_module(sym_entry);
                if (imported_module && imported_module->type_ctx) {
                    // Search this module's type table for matching type_name
                    TypeEntry* imported_entry = imported_module->type_ctx->type_table;
                    while (imported_entry) {
                        if (imported_entry->type && imported_entry->type->type_name &&
                            strcmp(imported_entry->type->type_name, type_info->type_name) == 0 &&
                            imported_entry->llvm_type) {
                            
                            log_verbose("Found struct type '%s' in imported module '%s'", 
                                       type_info->type_name, imported_module->relative_path);
                            
                            // Return the LLVM type directly - no need to cache since lookups are fast enough
                            return imported_entry->llvm_type;
                        }
                        imported_entry = imported_entry->next;
                    }
                }
            }
            sym_entry = sym_entry->next;
        }
    }

    return NULL;
}

// Initialize all types from TypeContext: pre-generate object structs and declare function prototypes
static void codegen_initialize_types(CodeGen* gen) {
    if (!gen->type_ctx) {
        return;
    }

    // Process structs iteratively until all are resolved
    // This handles dependencies without needing to reverse the list
    bool progress = true;
    while (progress) {
        progress = false;

        TypeEntry* entry = gen->type_ctx->type_table;
        while (entry) {
            TypeInfo* type = entry->type;

            if (type->kind == TYPE_KIND_OBJECT) {
                // Skip if already generated
                if (entry->llvm_type) {
                    entry = entry->next;
                    continue;
                }

                // Skip if struct has no properties (empty struct - just create opaque type)
                if (type->data.object.property_count == 0) {
                    // Create empty named struct for structs with no properties (only methods)
                    LLVMTypeRef struct_type = LLVMStructCreateNamed(gen->context, type->type_name);
                    LLVMStructSetBody(struct_type, NULL, 0, 0);
                    entry->llvm_type = struct_type;

                    log_verbose("Pre-generated empty LLVM struct type '%s' (0 fields)", type->type_name);
                    progress = true;
                    entry = entry->next;
                    continue;
                }

                // Try to build array of field types
                LLVMTypeRef* field_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * type->data.object.property_count);
                bool all_resolved = true;

                // Get the struct declaration node to access property_array_sizes
                ASTNode* struct_decl = type->data.object.struct_decl_node;

                for (int i = 0; i < type->data.object.property_count; i++) {
                    TypeInfo* prop_type = type->data.object.property_types[i];

                    // Check if this property is an array with a fixed size
                    if (struct_decl && type_info_is_array(prop_type) &&
                        struct_decl->struct_decl.property_array_sizes[i] > 0) {
                        // Fixed-size array - create LLVM array type
                        TypeInfo* elem_type = prop_type->data.array.element_type;
                        LLVMTypeRef elem_llvm_type = get_llvm_type(gen, elem_type);
                        if (!elem_llvm_type) {
                            all_resolved = false;
                            break;
                        }
                        field_types[i] = LLVMArrayType(elem_llvm_type, struct_decl->struct_decl.property_array_sizes[i]);
                    } else {
                        // Regular type or dynamic array
                        field_types[i] = get_llvm_type(gen, prop_type);
                        if (!field_types[i]) {
                            all_resolved = false;
                            break;
                        }
                    }
                }

                if (all_resolved) {
                    // Create named struct
                    LLVMTypeRef struct_type = LLVMStructCreateNamed(gen->context, type->type_name);
                    LLVMStructSetBody(struct_type, field_types, type->data.object.property_count, 0);

                    // Store in type entry - codegen_lookup_object_type will find it here
                    entry->llvm_type = struct_type;

                    log_verbose("Pre-generated LLVM struct type '%s' with %d fields",
                               type->type_name, type->data.object.property_count);

                    progress = true;
                }

                free(field_types);
            }

            entry = entry->next;
        }
    }

    // Now process function declarations
    TypeEntry* entry = gen->type_ctx->type_table;
    while (entry) {
        TypeInfo* type = entry->type;

        if (type->kind == TYPE_KIND_FUNCTION) {
        		/*
            // don't add it, it's alrady there we persist symbol table
            SymbolEntry* sym_entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
            sym_entry->name = strdup(type->type_name);
            sym_entry->type_info = type;
            sym_entry->is_const = false;
            sym_entry->value = NULL;
            sym_entry->node = NULL;  // Not needed for function references
            sym_entry->llvm_type = NULL;
            sym_entry->array_size = 0;
            sym_entry->next = gen->symbols->head;
            gen->symbols->head = sym_entry;
            */

            // Declare all function specializations (includes fully typed and external)
            FunctionSpecialization* spec = type->data.function.specializations;

            while (spec) {
		            // Check if variadic (only for external functions currenty)
		            int is_var_arg = type->data.function.is_variadic ? 1 : 0;

            		// it is external
            		if (function_specialization_is_external(spec) &&
              			LLVMGetNamedFunction(gen->module, spec->specialized_name) != NULL) {
	                 	log_verbose_indent(1, "Skipping redclaraton of extranl functiom %s with %d params%s",
																				  spec->specialized_name,
																				  spec->param_count,
	                                   			is_var_arg ? " (variadic)" : "");
										spec = spec->next;
										continue;
                }
                // Create parameter types
                LLVMTypeRef* param_types = (LLVMTypeRef*)malloc(sizeof(LLVMTypeRef) * spec->param_count);
                for (int j = 0; j < spec->param_count; j++) {
                    param_types[j] = get_llvm_type(gen, spec->param_type_info[j]);
                }

                // Create return type
                LLVMTypeRef ret_type = get_llvm_type(gen, spec->return_type_info);

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

void codegen_generate(CodeGen* gen, ASTNode* ast, bool is_entry_module) {
    // Store type context from type inference (contains types and specializations)
    gen->type_ctx = ast->type_ctx;
    gen->trait_registry = ast->type_ctx ? ast->type_ctx->trait_registry : NULL;

    // Use the symbol table from type inference instead of creating a new one
    gen->symbols = ast->symbol_table;

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

    // Only create wrapper main function for entry module
    LLVMValueRef main_func = NULL;
    LLVMBasicBlockRef entry = NULL;

    if (is_entry_module) {
        // Create main function
        LLVMTypeRef main_type = LLVMFunctionType(LLVMInt32TypeInContext(gen->context), NULL, 0, 0);
        main_func = LLVMAddFunction(gen->module, "main", main_type);
        entry = LLVMAppendBasicBlockInContext(gen->context, main_func, "entry");
        LLVMPositionBuilderAtEnd(gen->builder, entry);

        gen->current_function = main_func;
        gen->entry_block = entry;
        
        // Initialize standard streams (stdout, stderr, stdin) as global variables
        // Declare FILE as opaque struct
        LLVMTypeRef file_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), "struct._IO_FILE");
        
        // Declare get_stdout, get_stderr, get_stdin helper functions
        LLVMTypeRef get_stream_type = LLVMFunctionType(LLVMPointerType(file_type, 0), NULL, 0, false);
        LLVMValueRef get_stdout_fn = LLVMAddFunction(gen->module, "get_stdout", get_stream_type);
        LLVMValueRef get_stderr_fn = LLVMAddFunction(gen->module, "get_stderr", get_stream_type);
        LLVMValueRef get_stdin_fn = LLVMAddFunction(gen->module, "get_stdin", get_stream_type);
        
        // Create global variables for the streams
        LLVMValueRef global_stdout = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stdout");
        LLVMValueRef global_stderr = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stderr");
        LLVMValueRef global_stdin = LLVMAddGlobal(gen->module, LLVMPointerType(file_type, 0), "__jsasta_stdin");
        
        LLVMSetInitializer(global_stdout, LLVMConstNull(LLVMPointerType(file_type, 0)));
        LLVMSetInitializer(global_stderr, LLVMConstNull(LLVMPointerType(file_type, 0)));
        LLVMSetInitializer(global_stdin, LLVMConstNull(LLVMPointerType(file_type, 0)));
        
        // Call helper functions and store results at program start
        LLVMValueRef stdout_ptr = LLVMBuildCall2(gen->builder, get_stream_type, get_stdout_fn, NULL, 0, "stdout_init");
        LLVMBuildStore(gen->builder, stdout_ptr, global_stdout);
        
        LLVMValueRef stderr_ptr = LLVMBuildCall2(gen->builder, get_stream_type, get_stderr_fn, NULL, 0, "stderr_init");
        LLVMBuildStore(gen->builder, stderr_ptr, global_stderr);
        
        LLVMValueRef stdin_ptr = LLVMBuildCall2(gen->builder, get_stream_type, get_stdin_fn, NULL, 0, "stdin_init");
        LLVMBuildStore(gen->builder, stdin_ptr, global_stdin);
    }

    // Create debug info for main function
    if (is_entry_module && gen->di_builder) {
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

                    // Restore builder to main (only if entry module)
                    if (is_entry_module && entry) {
                        LLVMPositionBuilderAtEnd(gen->builder, entry);
                    }

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

    // PASS 2: Generate non-function, non-variable statements in main (only for entry module)
    if (is_entry_module) {
        if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
            for (int i = 0; i < ast->program.count; i++) {
                ASTNode* stmt = ast->program.statements[i];

                // Skip function declarations (already handled in PASS 1)
                if (stmt->type == AST_FUNCTION_DECL) {
                    continue;
                }

                // Process variable declarations with non-constant initializers
                // (constant initializers were handled in PASS 0.5)
                if (stmt->type == AST_VAR_DECL) {
                    // Only process if it has a non-constant initializer
                    if (stmt->var_decl.init) {
                        codegen_node(gen, stmt);
                    }
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

        // Call the entry module's main() function
        if (gen->type_ctx && gen->type_ctx->module_prefix) {
            // Construct the mangled main function name
            char mangled_main[256];
            snprintf(mangled_main, 256, "%s__main", gen->type_ctx->module_prefix);

            // Look up the function
            LLVMValueRef entry_main = LLVMGetNamedFunction(gen->module, mangled_main);
            if (entry_main) {
                // Call it
                LLVMBuildCall2(gen->builder, LLVMGlobalGetValueType(entry_main), entry_main, NULL, 0, "");
            }
        }

        // Add return 0 if not present
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(gen->builder))) {
            LLVMBuildRet(gen->builder, LLVMConstInt(LLVMInt32TypeInContext(gen->context), 0, 0));
        }
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
