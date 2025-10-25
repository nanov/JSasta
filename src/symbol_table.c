#include "jsasta_compiler.h"
#include <stdlib.h>
#include <string.h>

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

void symbol_table_insert(SymbolTable* table, const char* name, ValueType type, LLVMValueRef value, bool is_const) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type = type;
    entry->is_const = is_const;
    entry->value = value;
    entry->next = table->head;
    table->head = entry;
}

void symbol_table_insert_var_declaration(SymbolTable* table, const char* name, ValueType type, bool is_const, ASTNode* var_decl_node) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type = type;
    entry->is_const = is_const;
    entry->value = NULL;  // Will be set during codegen
    entry->node = var_decl_node;  // Store the AST node for looking up object properties
    entry->llvm_type = NULL;  // Will be set during codegen for objects
    entry->next = table->head;
    table->head = entry;
}

void symbol_table_insert_func_declaration(SymbolTable* table, const char* name, ASTNode* node) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type = TYPE_FUNCTION;  // Mark this as a function
    entry->is_const = false; // Functions are not const variables
    entry->value = NULL;
    entry->node = node;
    entry->llvm_type = NULL;
    entry->next = table->head;
    table->head = entry;
}

SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name) {
    SymbolEntry* current = table->head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }

    if (table->parent) {
        return symbol_table_lookup(table->parent, name);
    }

    return NULL;
}
