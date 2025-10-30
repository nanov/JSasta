#include "lsp_server.h"
#include "common/jsasta_compiler.h"
#include <stdio.h>
#include <stdlib.h>

// Simple test program to verify CodeIndex functionality
int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.jsa>\n", argv[0]);
        return 1;
    }
    
    const char* filename = argv[1];
    
    // Read file
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);
    
    printf("=== Testing CodeIndex on %s ===\n\n", filename);
    
    // Parse
    TypeContext* type_ctx = type_context_create();
    DiagnosticContext* diag = diagnostic_context_create_with_mode(DIAG_MODE_COLLECT, NULL);
    SymbolTable* symbols = symbol_table_create(NULL);
    
    Parser* parser = parser_create(content, filename, type_ctx, diag);
    ASTNode* ast = parser_parse(parser);
    parser_free(parser);
    
    if (!ast) {
        printf("ERROR: Parse failed\n");
        free(content);
        return 1;
    }
    
    if (diagnostic_has_errors(diag)) {
        printf("ERROR: Parse had errors\n");
        free(content);
        return 1;
    }
    
    // Type inference
    printf("Running type inference...\n");
    type_inference_with_diagnostics(ast, symbols, type_ctx, diag);
    
    // Build code index
    printf("Building code index...\n");
    CodeIndex* index = code_index_create();
    code_index_build(index, ast, symbols);
    
    printf("\n=== Code Index Results ===\n");
    printf("Total positions tracked: %d\n", index->position_count);
    
    // Count definitions vs references
    int def_positions = 0, ref_positions = 0;
    for (int i = 0; i < index->position_count; i++) {
        if (index->positions[i].is_definition) def_positions++;
        else ref_positions++;
    }
    printf("  - Definitions: %d\n", def_positions);
    printf("  - References: %d\n\n", ref_positions);
    
    // Print all code items
    printf("=== Definitions ===\n");
    CodeInfo* code = index->code_items;
    int def_count = 0;
    while (code) {
        const char* kind_str = "";
        switch (code->kind) {
            case CODE_TYPE: kind_str = "TYPE"; break;
            case CODE_FUNCTION: kind_str = "FUNCTION"; break;
            case CODE_VARIABLE: kind_str = "VARIABLE"; break;
            case CODE_PARAMETER: kind_str = "PARAMETER"; break;
            case CODE_NAMESPACE: kind_str = "NAMESPACE"; break;
            case CODE_MEMBER: kind_str = "MEMBER"; break;
        }
        
        // Count references from positions array
        int ref_count = 0;
        for (int i = 0; i < index->position_count; i++) {
            if (index->positions[i].code_info == code && !index->positions[i].is_definition) {
                ref_count++;
            }
        }
        
        printf("%3d. [%s] %s at %s:%zu:%zu (refs: %d)\n", 
               ++def_count,
               kind_str, 
               code->name,
               code->definition.filename ? code->definition.filename : "?",
               code->definition.start_line,
               code->definition.start_column,
               ref_count);
        
        // Print references
        for (int i = 0; i < index->position_count; i++) {
            if (index->positions[i].code_info == code && !index->positions[i].is_definition) {
                printf("     -> ref at %zu:%zu\n", 
                       index->positions[i].range.start_line,
                       index->positions[i].range.start_column);
            }
        }
        
        code = code->next;
    }
    
    // Test position lookup
    printf("\n=== Testing Position Lookup ===\n");
    
    // Test multiple positions
    size_t test_positions[][2] = {
        {1, 7},   // x definition
        {13, 25}, // x reference  
        {14, 22}, // multiply reference
        {8, 10},  // multiply definition
        {10, 12}, // temp reference in return statement
    };
    
    for (size_t i = 0; i < sizeof(test_positions) / sizeof(test_positions[0]); i++) {
        size_t line = test_positions[i][0];
        size_t col = test_positions[i][1];
        
        printf("\nLooking up position %zu:%zu...\n", line, col);
        PositionEntry* entry = code_index_find_at_position(index, filename, line, col);
        
        if (entry) {
            printf("  Found: %s (%s) at %zu:%zu\n",
                   entry->code_info->name,
                   entry->is_definition ? "definition" : "reference",
                   entry->range.start_line,
                   entry->range.start_column);
            
            if (!entry->is_definition) {
                printf("  -> Definition is at %zu:%zu\n",
                       entry->code_info->definition.start_line,
                       entry->code_info->definition.start_column);
            } else {
                // Count references
                int ref_count = 0;
                for (int j = 0; j < index->position_count; j++) {
                    if (index->positions[j].code_info == entry->code_info && !index->positions[j].is_definition) {
                        ref_count++;
                    }
                }
                printf("  -> Has %d references\n", ref_count);
            }
        } else {
            printf("  Nothing found at %zu:%zu\n", line, col);
        }
    }
    
    // Cleanup
    code_index_free(index);
    ast_free(ast);  // This also frees all symbol tables attached to AST nodes
    // Don't free symbols separately - ast_free handles it
    type_context_free(type_ctx);
    diagnostic_context_free(diag);
    free(content);
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
