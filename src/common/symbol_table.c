#include "jsasta_compiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

SymbolTable* symbol_table_create(SymbolTable* parent) {
    SymbolTable* table = (SymbolTable*)malloc(sizeof(SymbolTable));
    table->head = NULL;
    table->parent = parent;
    return table;
}

void symbol_table_free(SymbolTable* table) {
    SymbolEntry* current = table->head;
    while (current) {
        SymbolEntry* next = current->next;
        free(current->name);
        free(current);
        current = next;
    }
    free(table);
}

void symbol_table_insert(SymbolTable* table, const char* name, TypeInfo* type_info, LLVMValueRef value, bool is_const) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type_info = type_info;
    entry->is_const = is_const;
    entry->value = value;
    entry->node = NULL;
    entry->llvm_type = NULL;
    entry->array_size = 0;
    entry->next = table->head;
    table->head = entry;
}

void symbol_table_insert_var_declaration(SymbolTable* table, const char* name, TypeInfo* type_info, bool is_const, ASTNode* var_decl_node) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type_info = type_info;
    entry->is_const = is_const;
    entry->value = NULL;  // Will be set during codegen
    entry->node = var_decl_node;  // Store the AST node for looking up object properties
    entry->llvm_type = NULL;  // Will be set during codegen for objects
    entry->array_size = var_decl_node ? var_decl_node->var_decl.array_size : 0;  // Store array size from AST
    entry->next = table->head;
    table->head = entry;
}

void symbol_table_insert_func_declaration(SymbolTable* table, const char* name, ASTNode* node) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type_info = NULL;  // Functions don't have type_info yet (could add in future)
    entry->is_const = false; // Functions are not const variables
    entry->value = NULL;
    entry->node = node;
    entry->llvm_type = NULL;
    entry->array_size = 0;
    entry->next = table->head;
    table->head = entry;
}

SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name) {
    static int depth = 0;
    depth++;
    if (depth > 100) {
        fprintf(stderr, "ERROR: symbol_table_lookup depth exceeded 100 - possible infinite recursion\n");
        depth = 0;
        return NULL;
    }
    
    SymbolEntry* current = table->head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            depth--;
            return current;
        }
        current = current->next;
    }

    if (table->parent) {
        SymbolEntry* result = symbol_table_lookup(table->parent, name);
        depth--;
        return result;
    }

    depth--;
    return NULL;
}
