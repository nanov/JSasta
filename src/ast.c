#include <stdlib.h>
#include <string.h>

#include "jsasta_compiler.h"

ASTNode* ast_create(ASTNodeType type) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    node->type_info = NULL;
    node->loc.filename = NULL;
    node->loc.line = 0;
    node->loc.column = 0;
    return node;
}

ASTNode* ast_create_with_loc(ASTNodeType type, SourceLocation loc) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    node->type_info = NULL;
    node->loc = loc;
    return node;
}

void ast_free(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                ast_free(node->program.statements[i]);
            }
            free(node->program.statements);
            break;

        case AST_VAR_DECL:
            free(node->var_decl.name);
            ast_free(node->var_decl.init);
            ast_free(node->var_decl.array_size_expr);
            // Note: type_hint is a reference to TypeContext, don't free it
            break;

        case AST_FUNCTION_DECL:
            free(node->func_decl.name);
            for (int i = 0; i < node->func_decl.param_count; i++) {
                free(node->func_decl.params[i]);
            }
            free(node->func_decl.params);
            // Note: param_type_hints are references to TypeContext, don't free the TypeInfo objects
            if (node->func_decl.param_type_hints) {
                free(node->func_decl.param_type_hints);
            }
            // Note: return_type_hint is a reference to TypeContext, don't free it
            ast_free(node->func_decl.body);
            break;

        case AST_STRUCT_DECL:
            free(node->struct_decl.name);
            for (int i = 0; i < node->struct_decl.property_count; i++) {
                free(node->struct_decl.property_names[i]);
                ast_free(node->struct_decl.default_values[i]);
                ast_free(node->struct_decl.property_array_size_exprs[i]);
            }
            free(node->struct_decl.property_names);
            // Note: property_types are references to TypeContext, don't free the TypeInfo objects
            if (node->struct_decl.property_types) {
                free(node->struct_decl.property_types);
            }
            if (node->struct_decl.default_values) {
                free(node->struct_decl.default_values);
            }
            if (node->struct_decl.property_array_sizes) {
                free(node->struct_decl.property_array_sizes);
            }
            if (node->struct_decl.property_array_size_exprs) {
                free(node->struct_decl.property_array_size_exprs);
            }
            // Free methods
            for (int i = 0; i < node->struct_decl.method_count; i++) {
                ast_free(node->struct_decl.methods[i]);
            }
            if (node->struct_decl.methods) {
                free(node->struct_decl.methods);
            }
            break;

        case AST_RETURN:
            ast_free(node->return_stmt.value);
            break;

        case AST_IF:
            ast_free(node->if_stmt.condition);
            ast_free(node->if_stmt.then_branch);
            ast_free(node->if_stmt.else_branch);
            break;

        case AST_FOR:
            ast_free(node->for_stmt.init);
            ast_free(node->for_stmt.condition);
            ast_free(node->for_stmt.update);
            ast_free(node->for_stmt.body);
            break;

        case AST_WHILE:
            ast_free(node->while_stmt.condition);
            ast_free(node->while_stmt.body);
            break;

        case AST_EXPR_STMT:
            ast_free(node->expr_stmt.expression);
            break;

        case AST_BINARY_OP:
            free(node->binary_op.op);
            ast_free(node->binary_op.left);
            ast_free(node->binary_op.right);
            break;

        case AST_UNARY_OP:
            free(node->unary_op.op);
            ast_free(node->unary_op.operand);
            break;

        case AST_PREFIX_OP:
            free(node->prefix_op.op);
            if (node->prefix_op.name) {
                free(node->prefix_op.name);
            }
            if (node->prefix_op.target) {
                ast_free(node->prefix_op.target);
            }
            break;

        case AST_POSTFIX_OP:
            free(node->postfix_op.op);
            if (node->postfix_op.name) {
                free(node->postfix_op.name);
            }
            if (node->postfix_op.target) {
                ast_free(node->postfix_op.target);
            }
            break;

        case AST_CALL:
            ast_free(node->call.callee);
            for (int i = 0; i < node->call.arg_count; i++) {
                ast_free(node->call.args[i]);
            }
            free(node->call.args);
            break;

        case AST_METHOD_CALL:
            ast_free(node->method_call.object);
            free(node->method_call.method_name);
            for (int i = 0; i < node->method_call.arg_count; i++) {
                ast_free(node->method_call.args[i]);
            }
            free(node->method_call.args);
            break;

        case AST_IDENTIFIER:
            free(node->identifier.name);
            break;

        case AST_STRING:
            free(node->string.value);
            break;

        case AST_ASSIGNMENT:
            free(node->assignment.name);
            ast_free(node->assignment.value);
            break;

        case AST_COMPOUND_ASSIGNMENT:
            if (node->compound_assignment.name) {
                free(node->compound_assignment.name);
            }
            if (node->compound_assignment.target) {
                ast_free(node->compound_assignment.target);
            }
            free(node->compound_assignment.op);
            ast_free(node->compound_assignment.value);
            break;

        case AST_MEMBER_ACCESS:
            ast_free(node->member_access.object);
            free(node->member_access.property);
            break;

        case AST_MEMBER_ASSIGNMENT:
            ast_free(node->member_assignment.object);
            free(node->member_assignment.property);
            ast_free(node->member_assignment.value);
            break;

        case AST_TERNARY:
            ast_free(node->ternary.condition);
            ast_free(node->ternary.true_expr);
            ast_free(node->ternary.false_expr);
            break;

        case AST_INDEX_ACCESS:
            ast_free(node->index_access.object);
            ast_free(node->index_access.index);
            break;

        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                ast_free(node->array_literal.elements[i]);
            }
            free(node->array_literal.elements);
            break;

        case AST_OBJECT_LITERAL:
            for (int i = 0; i < node->object_literal.count; i++) {
                free(node->object_literal.keys[i]);
                ast_free(node->object_literal.values[i]);
            }
            free(node->object_literal.keys);
            free(node->object_literal.values);
            // Type info is stored in node->type_info (handled generically)
            break;

        case AST_INDEX_ASSIGNMENT:
            ast_free(node->index_assignment.object);
            ast_free(node->index_assignment.index);
            ast_free(node->index_assignment.value);
            break;

        default:
            break;
    }

    // Free symbol table if present (for AST_PROGRAM and AST_BLOCK)
    if (node->symbol_table) {
        symbol_table_free(node->symbol_table);
    }

    free(node);
}

// Helper to clone a symbol table (clones entries but not parent - parent will be set later)
static SymbolTable* symbol_table_clone(SymbolTable* table) {
    if (!table) return NULL;
    
    // Create new table with NULL parent (will be set up properly later)
    SymbolTable* clone = symbol_table_create(NULL);
    
    // Clone all entries
    SymbolEntry* current = table->head;
    while (current) {
        SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
        entry->name = strdup(current->name);
        entry->type_info = current->type_info; // Reference, not cloned
        entry->is_const = current->is_const;
        entry->value = current->value;
        entry->node = current->node;
        entry->llvm_type = current->llvm_type;
        entry->array_size = current->array_size;
        entry->next = clone->head;
        clone->head = entry;
        current = current->next;
    }
    
    return clone;
}

ASTNode* ast_clone(ASTNode* node) {
    if (!node) return NULL;

    ASTNode* clone = (ASTNode*)calloc(1, sizeof(ASTNode));
    clone->type = node->type;
    clone->type_info = node->type_info ? type_info_clone(node->type_info) : NULL;
    clone->type_ctx = NULL; // Don't clone type context
    clone->symbol_table = symbol_table_clone(node->symbol_table); // Clone symbol table

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            clone->program.count = node->program.count;
            clone->program.statements = (ASTNode**)malloc(sizeof(ASTNode*) * node->program.count);
            for (int i = 0; i < node->program.count; i++) {
                clone->program.statements[i] = ast_clone(node->program.statements[i]);
            }
            break;

        case AST_VAR_DECL:
            clone->var_decl.name = strdup(node->var_decl.name);
            clone->var_decl.init = ast_clone(node->var_decl.init);
            clone->var_decl.is_const = node->var_decl.is_const;
            clone->var_decl.array_size = node->var_decl.array_size;
            clone->var_decl.array_size_expr = ast_clone(node->var_decl.array_size_expr);
            // Copy type hint reference (don't clone - it's managed by TypeContext)
            clone->var_decl.type_hint = node->var_decl.type_hint;
            // Don't copy symbol_entry - it will be set during type inference on the cloned AST
            clone->var_decl.symbol_entry = NULL;
            break;

        case AST_FUNCTION_DECL:
            clone->func_decl.name = strdup(node->func_decl.name);
            clone->func_decl.param_count = node->func_decl.param_count;

            clone->func_decl.params = (char**)malloc(sizeof(char*) * node->func_decl.param_count);
            for (int i = 0; i < node->func_decl.param_count; i++) {
                clone->func_decl.params[i] = strdup(node->func_decl.params[i]);
            }

            // Copy type hint references (don't clone - they're managed by TypeContext)
            if (node->func_decl.param_type_hints) {
                clone->func_decl.param_type_hints = (TypeInfo**)malloc(sizeof(TypeInfo*) * node->func_decl.param_count);
                for (int i = 0; i < node->func_decl.param_count; i++) {
                    clone->func_decl.param_type_hints[i] = node->func_decl.param_type_hints[i];
                }
            } else {
                clone->func_decl.param_type_hints = NULL;
            }

            clone->func_decl.body = ast_clone(node->func_decl.body);
            clone->func_decl.return_type_hint = node->func_decl.return_type_hint;
            clone->func_decl.is_variadic = node->func_decl.is_variadic;
            break;

        case AST_STRUCT_DECL:
            clone->struct_decl.name = strdup(node->struct_decl.name);
            clone->struct_decl.property_count = node->struct_decl.property_count;

            clone->struct_decl.property_names = (char**)malloc(sizeof(char*) * node->struct_decl.property_count);
            for (int i = 0; i < node->struct_decl.property_count; i++) {
                clone->struct_decl.property_names[i] = strdup(node->struct_decl.property_names[i]);
            }

            // Copy type references (don't clone - they're managed by TypeContext)
            if (node->struct_decl.property_types) {
                clone->struct_decl.property_types = (TypeInfo**)malloc(sizeof(TypeInfo*) * node->struct_decl.property_count);
                for (int i = 0; i < node->struct_decl.property_count; i++) {
                    clone->struct_decl.property_types[i] = node->struct_decl.property_types[i];
                }
            } else {
                clone->struct_decl.property_types = NULL;
            }

            // Clone default values
            if (node->struct_decl.default_values) {
                clone->struct_decl.default_values = (ASTNode**)malloc(sizeof(ASTNode*) * node->struct_decl.property_count);
                for (int i = 0; i < node->struct_decl.property_count; i++) {
                    clone->struct_decl.default_values[i] = ast_clone(node->struct_decl.default_values[i]);
                }
            } else {
                clone->struct_decl.default_values = NULL;
            }
            
            // Copy array sizes
            if (node->struct_decl.property_array_sizes) {
                clone->struct_decl.property_array_sizes = (int*)malloc(sizeof(int) * node->struct_decl.property_count);
                memcpy(clone->struct_decl.property_array_sizes, node->struct_decl.property_array_sizes, 
                       sizeof(int) * node->struct_decl.property_count);
            } else {
                clone->struct_decl.property_array_sizes = NULL;
            }
            
            // Clone array size expressions
            if (node->struct_decl.property_array_size_exprs) {
                clone->struct_decl.property_array_size_exprs = (ASTNode**)malloc(sizeof(ASTNode*) * node->struct_decl.property_count);
                for (int i = 0; i < node->struct_decl.property_count; i++) {
                    clone->struct_decl.property_array_size_exprs[i] = ast_clone(node->struct_decl.property_array_size_exprs[i]);
                }
            } else {
                clone->struct_decl.property_array_size_exprs = NULL;
            }
            
            // Clone methods
            clone->struct_decl.method_count = node->struct_decl.method_count;
            if (node->struct_decl.methods && node->struct_decl.method_count > 0) {
                clone->struct_decl.methods = (ASTNode**)malloc(sizeof(ASTNode*) * node->struct_decl.method_count);
                for (int i = 0; i < node->struct_decl.method_count; i++) {
                    clone->struct_decl.methods[i] = ast_clone(node->struct_decl.methods[i]);
                }
            } else {
                clone->struct_decl.methods = NULL;
            }
            break;

        case AST_RETURN:
            clone->return_stmt.value = ast_clone(node->return_stmt.value);
            break;

        case AST_BREAK:
        case AST_CONTINUE:
            // No fields to clone for break/continue
            break;

        case AST_IF:
            clone->if_stmt.condition = ast_clone(node->if_stmt.condition);
            clone->if_stmt.then_branch = ast_clone(node->if_stmt.then_branch);
            clone->if_stmt.else_branch = ast_clone(node->if_stmt.else_branch);
            break;

        case AST_FOR:
            clone->for_stmt.init = ast_clone(node->for_stmt.init);
            clone->for_stmt.condition = ast_clone(node->for_stmt.condition);
            clone->for_stmt.update = ast_clone(node->for_stmt.update);
            clone->for_stmt.body = ast_clone(node->for_stmt.body);
            break;

        case AST_WHILE:
            clone->while_stmt.condition = ast_clone(node->while_stmt.condition);
            clone->while_stmt.body = ast_clone(node->while_stmt.body);
            break;

        case AST_EXPR_STMT:
            clone->expr_stmt.expression = ast_clone(node->expr_stmt.expression);
            break;

        case AST_BINARY_OP:
            clone->binary_op.op = strdup(node->binary_op.op);
            clone->binary_op.left = ast_clone(node->binary_op.left);
            clone->binary_op.right = ast_clone(node->binary_op.right);
            break;

        case AST_UNARY_OP:
            clone->unary_op.op = strdup(node->unary_op.op);
            clone->unary_op.operand = ast_clone(node->unary_op.operand);
            break;

        case AST_PREFIX_OP:
            clone->prefix_op.op = strdup(node->prefix_op.op);
            clone->prefix_op.name = node->prefix_op.name ? strdup(node->prefix_op.name) : NULL;
            clone->prefix_op.target = node->prefix_op.target ? ast_clone(node->prefix_op.target) : NULL;
            break;

        case AST_POSTFIX_OP:
            clone->postfix_op.op = strdup(node->postfix_op.op);
            clone->postfix_op.name = node->postfix_op.name ? strdup(node->postfix_op.name) : NULL;
            clone->postfix_op.target = node->postfix_op.target ? ast_clone(node->postfix_op.target) : NULL;
            break;

        case AST_CALL:
            clone->call.callee = ast_clone(node->call.callee);
            clone->call.arg_count = node->call.arg_count;
            clone->call.args = (ASTNode**)malloc(sizeof(ASTNode*) * node->call.arg_count);
            for (int i = 0; i < node->call.arg_count; i++) {
                clone->call.args[i] = ast_clone(node->call.args[i]);
            }
            break;

        case AST_METHOD_CALL:
            clone->method_call.object = ast_clone(node->method_call.object);
            clone->method_call.method_name = strdup(node->method_call.method_name);
            clone->method_call.arg_count = node->method_call.arg_count;
            clone->method_call.is_static = node->method_call.is_static;
            if (node->method_call.arg_count > 0) {
                clone->method_call.args = (ASTNode**)malloc(sizeof(ASTNode*) * node->method_call.arg_count);
                for (int i = 0; i < node->method_call.arg_count; i++) {
                    clone->method_call.args[i] = ast_clone(node->method_call.args[i]);
                }
            } else {
                clone->method_call.args = NULL;
            }
            break;

        case AST_IDENTIFIER:
            clone->identifier.name = strdup(node->identifier.name);
            break;

        case AST_NUMBER:
            clone->number.value = node->number.value;
            break;

        case AST_STRING:
            clone->string.value = strdup(node->string.value);
            break;

        case AST_BOOLEAN:
            clone->boolean.value = node->boolean.value;
            break;

        case AST_ASSIGNMENT:
            clone->assignment.name = strdup(node->assignment.name);
            clone->assignment.value = ast_clone(node->assignment.value);
            clone->assignment.symbol_entry = NULL;  // Will be set during type inference
            break;

        case AST_COMPOUND_ASSIGNMENT:
            clone->compound_assignment.name = node->compound_assignment.name ? strdup(node->compound_assignment.name) : NULL;
            clone->compound_assignment.target = node->compound_assignment.target ? ast_clone(node->compound_assignment.target) : NULL;
            clone->compound_assignment.op = strdup(node->compound_assignment.op);
            clone->compound_assignment.value = ast_clone(node->compound_assignment.value);
            break;

        case AST_MEMBER_ACCESS:
            clone->member_access.object = ast_clone(node->member_access.object);
            clone->member_access.property = strdup(node->member_access.property);
            break;

        case AST_MEMBER_ASSIGNMENT:
            clone->member_assignment.object = ast_clone(node->member_assignment.object);
            clone->member_assignment.property = strdup(node->member_assignment.property);
            clone->member_assignment.value = ast_clone(node->member_assignment.value);
            break;

        case AST_TERNARY:
            clone->ternary.condition = ast_clone(node->ternary.condition);
            clone->ternary.true_expr = ast_clone(node->ternary.true_expr);
            clone->ternary.false_expr = ast_clone(node->ternary.false_expr);
            break;

        case AST_INDEX_ACCESS:
            clone->index_access.object = ast_clone(node->index_access.object);
            clone->index_access.index = ast_clone(node->index_access.index);
            break;

        case AST_ARRAY_LITERAL:
            clone->array_literal.count = node->array_literal.count;
            clone->array_literal.elements = (ASTNode**)malloc(sizeof(ASTNode*) * node->array_literal.count);
            for (int i = 0; i < node->array_literal.count; i++) {
                clone->array_literal.elements[i] = ast_clone(node->array_literal.elements[i]);
            }
            break;

        case AST_OBJECT_LITERAL:
            clone->object_literal.count = node->object_literal.count;
            clone->object_literal.keys = (char**)malloc(sizeof(char*) * node->object_literal.count);
            clone->object_literal.values = (ASTNode**)malloc(sizeof(ASTNode*) * node->object_literal.count);
            for (int i = 0; i < node->object_literal.count; i++) {
                clone->object_literal.keys[i] = strdup(node->object_literal.keys[i]);
                clone->object_literal.values[i] = ast_clone(node->object_literal.values[i]);
            }
            // TypeInfo is cloned in the generic clone->type_info at the top of ast_clone
            break;

        case AST_INDEX_ASSIGNMENT:
            clone->index_assignment.object = ast_clone(node->index_assignment.object);
            clone->index_assignment.index = ast_clone(node->index_assignment.index);
            clone->index_assignment.value = ast_clone(node->index_assignment.value);
            break;
    }

    return clone;
}
