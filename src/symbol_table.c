#include "js_compiler.h"
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

void symbol_table_insert(SymbolTable* table, const char* name, ValueType type, LLVMValueRef value) {
    SymbolEntry* entry = (SymbolEntry*)malloc(sizeof(SymbolEntry));
    entry->name = strdup(name);
    entry->type = type;
    entry->value = value;
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
