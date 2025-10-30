#include "jsasta_compiler.h"
#include "logger.h"
#include "diagnostics.h"
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

void compile_file(const char* input_file, const char* output_file, bool enable_debug) {
    // Read source file
    char* source = read_file(input_file);
    if (!source) {
        return;
    }

    log_info("Compiling %s...", input_file);
    if (enable_debug) {
        log_verbose("Debug symbols enabled");
    }

    // Create diagnostic context for collecting errors
    DiagnosticContext* diagnostics = diagnostic_context_create();

    // Create TypeContext (shared between parser and type inference)
    TypeContext* type_ctx = type_context_create();

    // Parse
    log_section("Parsing");
    Parser* parser = parser_create(source, input_file, type_ctx, diagnostics);
    ASTNode* ast = parser_parse(parser);
    parser_free(parser);

    // Check for parse errors
    if (diagnostic_has_errors(diagnostics)) {
        diagnostic_report_console(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_context_free(diagnostics);
        free(source);
        type_context_free(type_ctx);
        return;
    }

    if (!ast) {
        log_error("Parse error");
        diagnostic_context_free(diagnostics);
        free(source);
        type_context_free(type_ctx);
        return;
    }

    log_verbose("Parsing complete");

    // Type inference (infer types from usage)
    log_section("Type Inference");
    SymbolTable* symbols = symbol_table_create(NULL);
    type_inference_with_diagnostics(ast, symbols, type_ctx, diagnostics);

    // Check for type inference errors
    if (diagnostic_has_errors(diagnostics)) {
        diagnostic_report_console(diagnostics);
        diagnostic_print_summary(diagnostics);
        diagnostic_context_free(diagnostics);
        ast_free(ast);
        type_context_free(type_ctx);
        return;
    }

    // Print specializations if any
    if (ast->type_ctx) {
        specialization_context_print(ast->type_ctx);
    }

    // Disabled - rework to actually check types
    // // Type analysis (validate types)
    // type_analyze(ast, symbols);
    // Note: Don't free symbols here - it's now stored in ast->symbol_table and will be freed with AST

    log_verbose("Type checking complete");

    // Code generation
    log_section("Code Generation");
    CodeGen* gen = codegen_create("js_module");
    gen->type_ctx = type_ctx;  // Set type context for codegen
    gen->enable_debug = enable_debug;  // Set debug flag
    if (enable_debug) {
        gen->source_filename = input_file;
    }
    codegen_generate(gen, ast);

    log_info("Code generation complete");

    // Emit LLVM IR
    codegen_emit_llvm_ir(gen, output_file);

    log_info("LLVM IR written to %s", output_file);
    diagnostic_print_summary(diagnostics);

    // Cleanup
    ast_free(ast);
    codegen_free(gen);
    diagnostic_context_free(diagnostics);
    free(source);
}

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] <input.js> [output.ll]\n", program_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -g, --debug        Generate debug symbols\n");
    fprintf(stderr, "  -v, --verbose      Enable verbose output\n");
    fprintf(stderr, "  -q, --quiet        Suppress info messages (warnings and errors only)\n");
    fprintf(stderr, "  -h, --help         Show this help message\n");
}

int main(int argc, char** argv) {
    LogLevel log_level = LOG_INFO;
    bool enable_debug = false;

    // Parse command-line options
    static struct option long_options[] = {
        {"debug",   no_argument, 0, 'g'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet",   no_argument, 0, 'q'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "gvqh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'g':
                enable_debug = true;
                break;
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

    compile_file(input_file, output_file, enable_debug);

    return 0;
}
