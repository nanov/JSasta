#include "lsp_server.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// === Helper Functions ===

// Compare two position entries for sorting (by filename, then line, then column)
static int compare_position_entries(const void* a, const void* b) {
    const PositionEntry* entry_a = (const PositionEntry*)a;
    const PositionEntry* entry_b = (const PositionEntry*)b;
    
    // Compare filenames first
    if (entry_a->range.filename && entry_b->range.filename) {
        int filename_cmp = strcmp(entry_a->range.filename, entry_b->range.filename);
        if (filename_cmp != 0) return filename_cmp;
    } else if (entry_a->range.filename) {
        return 1;
    } else if (entry_b->range.filename) {
        return -1;
    }
    
    // Compare lines
    if (entry_a->range.start_line < entry_b->range.start_line) return -1;
    if (entry_a->range.start_line > entry_b->range.start_line) return 1;
    
    // Compare columns
    if (entry_a->range.start_column < entry_b->range.start_column) return -1;
    if (entry_a->range.start_column > entry_b->range.start_column) return 1;
    
    return 0;
}

static SourceRange source_range_from_location(SourceLocation loc) {
    SourceRange range;
    range.filename = loc.filename;
    range.start_line = loc.line;
    range.start_column = loc.column;
    range.end_line = loc.line;
    range.end_column = loc.column;
    return range;
}

static bool position_in_range(SourceRange range, const char* filename, size_t line, size_t column) {
    if (!filename || !range.filename || strcmp(filename, range.filename) != 0) {
        return false;
    }
    
    // Check if position is within range
    if (line < range.start_line || line > range.end_line) {
        return false;
    }
    
    if (line == range.start_line && column < range.start_column) {
        return false;
    }
    
    if (line == range.end_line && column > range.end_column) {
        return false;
    }
    
    return true;
}

static CodeInfo* find_code_info_by_decl(CodeIndex* index, ASTNode* decl_node) {
    CodeInfo* current = index->code_items;
    while (current) {
        if (current->decl_node == decl_node) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// === Public API ===

CodeIndex* code_index_create(void) {
    CodeIndex* index = (CodeIndex*)malloc(sizeof(CodeIndex));
    if (!index) return NULL;
    
    index->code_items = NULL;
    index->positions = NULL;
    index->position_count = 0;
    index->position_capacity = 0;
    
    return index;
}

void code_index_free(CodeIndex* index) {
    if (!index) return;
    
    // Free code items
    CodeInfo* code_current = index->code_items;
    while (code_current) {
        CodeInfo* next = code_current->next;
        
        free(code_current->name);
        free(code_current->description);
        if (code_current->temp_references) {
            free(code_current->temp_references);
        }
        free(code_current);
        code_current = next;
    }
    
    // Free position array
    if (index->positions) {
        free(index->positions);
    }
    
    free(index);
}

void code_index_add_definition(CodeIndex* index, ASTNode* decl_node, const char* name, 
                                int kind, TypeInfo* type_info, SourceRange range) {
    if (!index || !decl_node || !name) return;
    
    // Check if we already have this declaration
    CodeInfo* existing = find_code_info_by_decl(index, decl_node);
    if (existing) {
        return; // Already added
    }
    
    // Create new code info
    CodeInfo* info = (CodeInfo*)malloc(sizeof(CodeInfo));
    if (!info) return;
    
    info->name = strdup(name);
    info->kind = kind;
    info->type_info = type_info;
    info->definition = range;
    info->description = NULL;
    info->decl_node = decl_node;
    info->temp_references = NULL;
    info->temp_reference_count = 0;
    info->temp_reference_capacity = 0;
    info->next = index->code_items;
    
    index->code_items = info;
}

void code_index_add_reference(CodeIndex* index, ASTNode* decl_node, SourceRange range) {
    if (!index || !decl_node) return;
    
    // Find the code info for this declaration
    CodeInfo* info = find_code_info_by_decl(index, decl_node);
    if (!info) {
        return; // Declaration not found - shouldn't happen
    }
    
    // Add to temporary references array
    if (info->temp_reference_count >= info->temp_reference_capacity) {
        int new_capacity = info->temp_reference_capacity == 0 ? 4 : info->temp_reference_capacity * 2;
        SourceRange* new_refs = (SourceRange*)realloc(info->temp_references, 
                                                       new_capacity * sizeof(SourceRange));
        if (!new_refs) return;
        
        info->temp_references = new_refs;
        info->temp_reference_capacity = new_capacity;
    }
    
    info->temp_references[info->temp_reference_count++] = range;
}

PositionEntry* code_index_find_at_position(CodeIndex* index, const char* filename, 
                                           size_t line, size_t column) {
    if (!index || !filename || !index->positions || index->position_count == 0) {
        return NULL;
    }
    
    // Binary search to find the first entry that could match
    // We look for entries at the target line or earlier
    int left = 0;
    int right = index->position_count - 1;
    int best_match = -1;
    
    // First, binary search for entries in the same file and around the target line
    while (left <= right) {
        int mid = left + (right - left) / 2;
        PositionEntry* entry = &index->positions[mid];
        
        // Check filename
        if (!entry->range.filename) {
            left = mid + 1;
            continue;
        }
        
        int filename_cmp = strcmp(entry->range.filename, filename);
        if (filename_cmp < 0) {
            left = mid + 1;
        } else if (filename_cmp > 0) {
            right = mid - 1;
        } else {
            // Same file, check if position matches
            if (position_in_range(entry->range, filename, line, column)) {
                return entry;
            }
            
            // Check line number
            if (entry->range.start_line < line) {
                best_match = mid;
                left = mid + 1;
            } else if (entry->range.start_line > line) {
                right = mid - 1;
            } else {
                // Same line, check column
                if (entry->range.start_column <= column) {
                    best_match = mid;
                    left = mid + 1;
                } else {
                    right = mid - 1;
                }
            }
        }
    }
    
    // Linear search around best_match to find exact match
    // Check a few entries before and after in case ranges overlap
    if (best_match >= 0) {
        int start = best_match - 5 >= 0 ? best_match - 5 : 0;
        int end = best_match + 5 < index->position_count ? best_match + 5 : index->position_count;
        
        for (int i = start; i < end; i++) {
            PositionEntry* entry = &index->positions[i];
            if (position_in_range(entry->range, filename, line, column)) {
                return entry;
            }
        }
    }
    
    return NULL;
}

// === AST Traversal to Build Index ===

static void build_index_from_node(CodeIndex* index, ASTNode* node, SymbolTable* symbols);

static void add_identifier_reference(CodeIndex* index, ASTNode* identifier_node, ASTNode* decl_node) {
    if (!identifier_node || !decl_node || !identifier_node->loc.filename) {
        return;
    }
    
    SourceRange range = source_range_from_location(identifier_node->loc);
    
    // For identifiers, estimate end column by adding name length
    if (identifier_node->type == AST_IDENTIFIER && identifier_node->identifier.name) {
        range.end_column = range.start_column + strlen(identifier_node->identifier.name);
    }
    
    code_index_add_reference(index, decl_node, range);
}

static void build_index_from_var_decl(CodeIndex* index, ASTNode* node, SymbolTable* symbols) {
    if (!node->var_decl.name || !node->loc.filename) {
        return;
    }
    
    SourceRange range = source_range_from_location(node->loc);
    range.end_column = range.start_column + strlen(node->var_decl.name);
    
    int kind = node->var_decl.is_const ? CODE_VARIABLE : CODE_VARIABLE;
    
    code_index_add_definition(index, node, node->var_decl.name, kind, 
                              node->type_info, range);
    
    // Traverse the initialization expression to find references
    if (node->var_decl.init) {
        build_index_from_node(index, node->var_decl.init, symbols);
    }
}

static void build_index_from_function_decl(CodeIndex* index, ASTNode* node) {
    if (!node->func_decl.name || !node->loc.filename) {
        return;
    }
    
    SourceRange range = source_range_from_location(node->loc);
    range.end_column = range.start_column + strlen(node->func_decl.name);
    
    code_index_add_definition(index, node, node->func_decl.name, CODE_FUNCTION,
                              node->type_info, range);
    
    // Note: Parameters in func_decl are just char* strings, not ASTNodes
    // We would need to track parameter positions separately if needed
    // For now, skip parameter indexing since we don't have their source locations
    
    // Traverse function body
    if (node->func_decl.body) {
        build_index_from_node(index, node->func_decl.body, node->func_decl.body->symbol_table);
    }
}

static void build_index_from_struct_decl(CodeIndex* index, ASTNode* node) {
    if (!node->struct_decl.name || !node->loc.filename) {
        return;
    }
    
    SourceRange range = source_range_from_location(node->loc);
    range.end_column = range.start_column + strlen(node->struct_decl.name);
    
    code_index_add_definition(index, node, node->struct_decl.name, CODE_TYPE,
                              node->type_info, range);
    
    // Note: Struct members are stored as property_names (char**), not ASTNodes
    // We don't have source locations for individual members
    // For now, skip member indexing
    
    // Traverse methods
    for (int i = 0; i < node->struct_decl.method_count; i++) {
        ASTNode* method = node->struct_decl.methods[i];
        if (method) {
            build_index_from_function_decl(index, method);
        }
    }
}

static void build_index_from_identifier(CodeIndex* index, ASTNode* node, SymbolTable* symbols) {
    if (!node->identifier.name || !symbols) {
        return;
    }
    
    // Look up the symbol to find its declaration
    SymbolEntry* entry = symbol_table_lookup(symbols, node->identifier.name);
    if (!entry) {
        return;
    }
    
    // SymbolEntry stores declaration in 'node' field
    ASTNode* decl_node = entry->node;
    if (decl_node) {
        add_identifier_reference(index, node, decl_node);
    }
}

static void build_index_from_node(CodeIndex* index, ASTNode* node, SymbolTable* symbols) {
    if (!node) return;
    
    switch (node->type) {
        case AST_VAR_DECL:
            build_index_from_var_decl(index, node, symbols);
            break;
            
        case AST_FUNCTION_DECL:
            build_index_from_function_decl(index, node);
            break;
            
        case AST_STRUCT_DECL:
            build_index_from_struct_decl(index, node);
            break;
            
        case AST_IDENTIFIER:
            build_index_from_identifier(index, node, symbols);
            break;
            
        case AST_BLOCK:
            // Use block's symbol table if it has one
            if (node->symbol_table) {
                symbols = node->symbol_table;
            }
            for (int i = 0; i < node->block.count; i++) {
                build_index_from_node(index, node->block.statements[i], symbols);
            }
            break;
            
        case AST_EXPR_STMT:
            build_index_from_node(index, node->expr_stmt.expression, symbols);
            break;
            
        case AST_BINARY_OP:
            build_index_from_node(index, node->binary_op.left, symbols);
            build_index_from_node(index, node->binary_op.right, symbols);
            break;
            
        case AST_UNARY_OP:
            build_index_from_node(index, node->unary_op.operand, symbols);
            break;
            
        case AST_CALL:
            build_index_from_node(index, node->call.callee, symbols);
            for (int i = 0; i < node->call.arg_count; i++) {
                build_index_from_node(index, node->call.args[i], symbols);
            }
            break;
            
        case AST_IF:
            build_index_from_node(index, node->if_stmt.condition, symbols);
            build_index_from_node(index, node->if_stmt.then_branch, symbols);
            build_index_from_node(index, node->if_stmt.else_branch, symbols);
            break;
            
        case AST_WHILE:
            build_index_from_node(index, node->while_stmt.condition, symbols);
            build_index_from_node(index, node->while_stmt.body, symbols);
            break;
            
        case AST_FOR:
            // Use for loop's symbol table if it has one
            if (node->symbol_table) {
                symbols = node->symbol_table;
            }
            build_index_from_node(index, node->for_stmt.init, symbols);
            build_index_from_node(index, node->for_stmt.condition, symbols);
            build_index_from_node(index, node->for_stmt.update, symbols);
            build_index_from_node(index, node->for_stmt.body, symbols);
            break;
            
        case AST_RETURN:
            build_index_from_node(index, node->return_stmt.value, symbols);
            break;
            
        case AST_ASSIGNMENT:
            build_index_from_node(index, node->assignment.value, symbols);
            // Note: assignment.name is just a string, not an identifier node
            break;
            
        case AST_MEMBER_ACCESS:
            build_index_from_node(index, node->member_access.object, symbols);
            // Don't traverse member as identifier - it's a field name
            break;
            
        case AST_MEMBER_ASSIGNMENT:
            build_index_from_node(index, node->member_assignment.object, symbols);
            build_index_from_node(index, node->member_assignment.value, symbols);
            // Don't traverse property as identifier - it's a field name
            break;
            
        case AST_INDEX_ACCESS:
            build_index_from_node(index, node->index_access.object, symbols);
            build_index_from_node(index, node->index_access.index, symbols);
            break;
            
        case AST_INDEX_ASSIGNMENT:
            build_index_from_node(index, node->index_assignment.object, symbols);
            build_index_from_node(index, node->index_assignment.index, symbols);
            build_index_from_node(index, node->index_assignment.value, symbols);
            break;
            
        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                build_index_from_node(index, node->array_literal.elements[i], symbols);
            }
            break;
            
        case AST_OBJECT_LITERAL:
            for (int i = 0; i < node->object_literal.count; i++) {
                build_index_from_node(index, node->object_literal.values[i], symbols);
            }
            break;
            
        default:
            // Literals and other leaf nodes don't need indexing
            break;
    }
}

// Build the flat positions array from all CodeInfo items (after traversal)
static void build_positions_array(CodeIndex* index) {
    if (!index) return;
    
    // First, count total positions (definitions + all references)
    int total_positions = 0;
    CodeInfo* code = index->code_items;
    while (code) {
        total_positions++; // Definition
        total_positions += code->temp_reference_count; // References
        code = code->next;
    }
    
    if (total_positions == 0) return;
    
    // Allocate positions array
    index->positions = (PositionEntry*)malloc(total_positions * sizeof(PositionEntry));
    if (!index->positions) return;
    
    index->position_capacity = total_positions;
    index->position_count = 0;
    
    // Fill positions array
    code = index->code_items;
    while (code) {
        // Add definition
        index->positions[index->position_count].range = code->definition;
        index->positions[index->position_count].code_info = code;
        index->positions[index->position_count].is_definition = true;
        index->position_count++;
        
        // Add all references
        for (int i = 0; i < code->temp_reference_count; i++) {
            index->positions[index->position_count].range = code->temp_references[i];
            index->positions[index->position_count].code_info = code;
            index->positions[index->position_count].is_definition = false;
            index->position_count++;
        }
        
        code = code->next;
    }
    
    // Sort positions by filename, line, then column for binary search
    qsort(index->positions, index->position_count, sizeof(PositionEntry), compare_position_entries);
}

void code_index_build(CodeIndex* index, ASTNode* ast, SymbolTable* symbols) {
    if (!index || !ast || !symbols) {
        return;
    }
    
    // Traverse the AST and collect definitions + references
    if (ast->type == AST_PROGRAM) {
        for (int i = 0; i < ast->program.count; i++) {
            build_index_from_node(index, ast->program.statements[i], symbols);
        }
    } else {
        build_index_from_node(index, ast, symbols);
    }
    
    // Build and sort the flat positions array
    build_positions_array(index);
}
