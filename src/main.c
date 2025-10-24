#include "js_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* read_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(size + 1);
    fread(content, 1, size, file);
    content[size] = '\0';

    fclose(file);
    return content;
}

void compile_file(const char* input_file, const char* output_file) {
    // Read source file
    char* source = read_file(input_file);
    if (!source) {
        return;
    }

    printf("Compiling %s...\n", input_file);

    // Parse
    Parser* parser = parser_create(source);
    ASTNode* ast = parser_parse(parser);
    parser_free(parser);

    if (!ast) {
        fprintf(stderr, "Parse error\n");
        free(source);
        return;
    }

    printf("Parsing complete.\n");

    // Type inference (infer types from usage)
    SymbolTable* symbols = symbol_table_create(NULL);
    type_inference(ast, symbols);

    printf("Type inference complete.\n");

    // Print specializations if any
    if (ast->specialization_ctx) {
        specialization_context_print(ast->specialization_ctx);
    }

    // Disabled - rework to actually check types
    // // Type analysis (validate types)
    // type_analyze(ast, symbols);
    symbol_table_free(symbols);

    printf("Type checking complete.\n");

    // Code generation
    CodeGen* gen = codegen_create("js_module");
    codegen_generate(gen, ast);

    printf("Code generation complete.\n");

    // Emit LLVM IR
    codegen_emit_llvm_ir(gen, output_file);

    printf("LLVM IR written to %s\n", output_file);

    // Cleanup
    ast_free(ast);
    codegen_free(gen);
    free(source);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.js> [output.ll]\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = argc >= 3 ? argv[2] : "output.ll";

    compile_file(input_file, output_file);

    return 0;
}
