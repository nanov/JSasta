#include "jsasta_compiler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

char* read_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        log_error("Could not open file %s", filename);
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

    log_info("Compiling %s...", input_file);

    // Create TypeContext (shared between parser and type inference)
    TypeContext* type_ctx = type_context_create();

    // Parse
    log_section("Parsing");
    Parser* parser = parser_create(source, input_file, type_ctx);
    ASTNode* ast = parser_parse(parser);
    parser_free(parser);

    if (!ast) {
        log_error("Parse error");
        free(source);
        type_context_free(type_ctx);
        return;
    }

    log_verbose("Parsing complete");

    // Type inference (infer types from usage)
    log_section("Type Inference");
    SymbolTable* symbols = symbol_table_create(NULL);
    type_inference_with_context(ast, symbols, type_ctx);

    // Print specializations if any
    if (ast->specialization_ctx) {
        specialization_context_print(ast->specialization_ctx);
    }

    // Disabled - rework to actually check types
    // // Type analysis (validate types)
    // type_analyze(ast, symbols);
    symbol_table_free(symbols);

    log_verbose("Type checking complete");

    // Code generation
    log_section("Code Generation");
    CodeGen* gen = codegen_create("js_module");
    gen->type_ctx = type_ctx;  // Set type context for codegen
    codegen_generate(gen, ast);

    log_info("Code generation complete");

    // Emit LLVM IR
    codegen_emit_llvm_ir(gen, output_file);

    log_info("LLVM IR written to %s", output_file);

    // Cleanup
    ast_free(ast);
    codegen_free(gen);
    free(source);
}

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] <input.js> [output.ll]\n", program_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -v, --verbose      Enable verbose output\n");
    fprintf(stderr, "  -q, --quiet        Suppress info messages (warnings and errors only)\n");
    fprintf(stderr, "  -h, --help         Show this help message\n");
}

int main(int argc, char** argv) {
    LogLevel log_level = LOG_INFO;

    // Parse command-line options
    static struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"quiet",   no_argument, 0, 'q'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "vqh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v':
                log_level = LOG_VERBOSE;
                break;
            case 'q':
                log_level = LOG_WARNING;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Initialize logger
    logger_init(log_level);

    // Check for input file
    if (optind >= argc) {
        log_error("No input file specified");
        print_usage(argv[0]);
        return 1;
    }

    const char* input_file = argv[optind];
    const char* output_file = (optind + 1 < argc) ? argv[optind + 1] : "output.ll";

    compile_file(input_file, output_file);

    return 0;
}
