#include "../common/jsasta_compiler.h"
#include "../common/logger.h"
#include "../common/diagnostics.h"
#include "../common/module_loader.h"
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
    log_info("Compiling %s...", input_file);
    if (enable_debug) {
        log_verbose("Debug symbols enabled");
    }

    // Create module registry
    log_section("Module Loading");
    ModuleRegistry* registry = module_registry_create(input_file);
    
    // Load entry module (will recursively load imports)
    Module* entry_module = module_load(registry, input_file, NULL);
    
    if (!entry_module) {
        log_error("Failed to load entry module");
        module_registry_free(registry);
        return;
    }
    
    // Check for parse errors
    if (diagnostic_has_errors(registry->diagnostics)) {
        diagnostic_report_console(registry->diagnostics);
        diagnostic_print_summary(registry->diagnostics);
        module_registry_free(registry);
        return;
    }
    
    log_verbose("Loaded %d module(s)", registry->module_count);

    // Type inference for all modules
    log_section("Type Inference");
    
    // Run type inference on all modules in dependency order
    // Start with imported modules (dependencies) first, then the entry module
    
    // First, run type inference on all imported modules (they have no imports themselves for now)
    for (Module* mod = registry->modules; mod != NULL; mod = mod->next) {
        if (mod == entry_module) continue; // Skip entry module, do it last
        
        log_verbose("Running type inference on module: %s", mod->relative_path);
        
        // Create symbol table for the module if it doesn't have one
        if (!mod->module_scope) {
            mod->module_scope = symbol_table_create(NULL);
        }
        
        type_inference_with_diagnostics(mod->ast, mod->module_scope, registry->type_ctx, registry->diagnostics);
        
        if (diagnostic_has_errors(registry->diagnostics)) {
            log_error("Type inference failed for module: %s", mod->relative_path);
            diagnostic_report_console(registry->diagnostics);
            diagnostic_print_summary(registry->diagnostics);
            module_registry_free(registry);
            return;
        }
    }
    
    // Create symbol table for entry module and setup imports
    if (!entry_module->module_scope) {
        entry_module->module_scope = symbol_table_create(NULL);
    }
    
    log_verbose("Setting up imports for entry module");
    if (!module_setup_import_symbols(entry_module, entry_module->module_scope)) {
        log_error("Failed to setup import symbols");
        module_registry_free(registry);
        return;
    }
    
    // Finally, run type inference on the entry module
    log_verbose("Running type inference on entry module: %s", entry_module->relative_path);
    type_inference_with_diagnostics(entry_module->ast, entry_module->module_scope, registry->type_ctx, registry->diagnostics);

    // Check for type inference errors
    if (diagnostic_has_errors(registry->diagnostics)) {
        diagnostic_report_console(registry->diagnostics);
        diagnostic_print_summary(registry->diagnostics);
        module_registry_free(registry);
        return;
    }

    // Print specializations if any
    if (entry_module->ast->type_ctx) {
        specialization_context_print(entry_module->ast->type_ctx);
    }

    log_verbose("Type checking complete");

    // Code generation
    log_section("Code Generation");
    CodeGen* gen = codegen_create("js_module");
    gen->type_ctx = registry->type_ctx;  // Set type context for codegen
    gen->enable_debug = enable_debug;  // Set debug flag
    if (enable_debug) {
        gen->source_filename = input_file;
    }
    
    // Generate code for all modules in dependency order (dependencies first, then entry)
    for (Module* mod = registry->modules; mod != NULL; mod = mod->next) {
        if (mod == entry_module) continue; // Skip entry module, do it last
        
        log_verbose("Generating code for module: %s", mod->relative_path);
        codegen_generate(gen, mod->ast, false); // Not entry module
    }
    
    // Finally, generate code for the entry module
    log_verbose("Generating code for entry module: %s", entry_module->relative_path);
    codegen_generate(gen, entry_module->ast, true); // Is entry module

    log_info("Code generation complete");

    // Emit LLVM IR
    codegen_emit_llvm_ir(gen, output_file);

    log_info("LLVM IR written to %s", output_file);
    diagnostic_print_summary(registry->diagnostics);

    // Cleanup
    codegen_free(gen);
    module_registry_free(registry);
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
