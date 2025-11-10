#include "../common/jsasta_compiler.h"
#include "../common/logger.h"
#include "../common/diagnostics.h"
#include "../common/module_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

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

// Compile LLVM module to object or assembly file
int compile_to_object_or_asm(LLVMModuleRef module, const char* output_file, int opt_level, bool emit_assembly) {
    // Initialize LLVM targets
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    // Get the default target triple
    char* target_triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, target_triple);

    // Get the target
    LLVMTargetRef target;
    char* error = NULL;
    if (LLVMGetTargetFromTriple(target_triple, &target, &error)) {
        log_error("Failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(target_triple);
        return 1;
    }

    // Create target machine
    LLVMCodeGenOptLevel llvm_opt_level;
    switch (opt_level) {
        case 0: llvm_opt_level = LLVMCodeGenLevelNone; break;
        case 1: llvm_opt_level = LLVMCodeGenLevelLess; break;
        case 2: llvm_opt_level = LLVMCodeGenLevelDefault; break;
        case 3: llvm_opt_level = LLVMCodeGenLevelAggressive; break;
        default: llvm_opt_level = LLVMCodeGenLevelNone; break;
    }

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target,
        target_triple,
        "",  // CPU
        "",  // Features
        llvm_opt_level,
        LLVMRelocPIC,
        LLVMCodeModelDefault
    );

    LLVMDisposeMessage(target_triple);

    if (!machine) {
        log_error("Failed to create target machine");
        return 1;
    }

    // Emit object or assembly file
    LLVMCodeGenFileType file_type = emit_assembly ? LLVMAssemblyFile : LLVMObjectFile;
    if (LLVMTargetMachineEmitToFile(machine, module, (char*)output_file, file_type, &error)) {
        log_error("Failed to emit %s: %s", emit_assembly ? "assembly" : "object file", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(machine);
        return 1;
    }

    log_verbose("%s written to %s", emit_assembly ? "Assembly" : "Object file", output_file);
    LLVMDisposeTargetMachine(machine);
    return 0;
}

// Link object file with runtime to create executable
int link_executable(const char* obj_file, const char* output_file, const char* sanitizer, bool debug_symbols) {
    // Get the compiler's directory to find runtime relative to it
    char compiler_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", compiler_path, sizeof(compiler_path) - 1);
    if (len == -1) {
        // Fallback for macOS
        uint32_t size = sizeof(compiler_path);
        if (_NSGetExecutablePath(compiler_path, &size) != 0) {
            log_error("Failed to get compiler path");
            return 1;
        }
    } else {
        compiler_path[len] = '\0';
    }

    // Get directory of compiler binary
    char* last_slash = strrchr(compiler_path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    // Runtime is in ../runtime relative to compiler binary
    char runtime_path[PATH_MAX];
    snprintf(runtime_path, sizeof(runtime_path), "%s/runtime", compiler_path);

    // Build clang command - use system clang to avoid Homebrew LLVM 21 LTO bug
    char command[4096];
    int pos = snprintf(command, sizeof(command), "clang %s", obj_file);

    // Add runtime object files
    pos += snprintf(command + pos, sizeof(command) - pos, " %s/display.o", runtime_path);
    pos += snprintf(command + pos, sizeof(command) - pos, " %s/jsasta_runtime.o", runtime_path);

    // Add sanitizer flags
    if (sanitizer) {
        pos += snprintf(command + pos, sizeof(command) - pos, " -fsanitize=%s", sanitizer);
    }

    // Add debug symbols flag
    if (debug_symbols) {
        pos += snprintf(command + pos, sizeof(command) - pos, " -g");
    }

    // Add output file
    pos += snprintf(command + pos, sizeof(command) - pos, " -o %s", output_file);

    log_verbose("Linking: %s", command);

    int result = system(command);
    if (result != 0) {
        log_error("Linking failed");
        return 1;
    }

    log_info("Executable written to %s", output_file);
    return 0;
}

int compile_file(const char* input_file, const char* output_file,
                 bool emit_llvm, bool emit_asm, bool compile_only, int opt_level,
                 const char* sanitizer, bool enable_debug_symbols, bool enable_debug) {
    log_info("Compiling %s...", input_file);
    if (enable_debug_symbols) {
        log_verbose("Debug symbols enabled");
    }
    if (enable_debug) {
        log_verbose("Debug mode enabled");
    }

    // Create module registry
    log_section("Module Loading");
    ModuleRegistry* registry = module_registry_create(input_file);

    // Load entry module (will recursively load imports)
    // Convert to absolute path first to avoid double-pathing with relative paths like ./file.jsa
    char* entry_abs = module_get_absolute_path(input_file);
    Module* entry_module = module_load(registry, entry_abs, NULL);
    free(entry_abs);

    if (!entry_module) {
        log_error("Failed to load entry module");
        module_registry_free(registry);
        return 404;
    }

    // Check for parse errors
    if (diagnostic_has_errors(registry->diagnostics)) {
        diagnostic_report_console(registry->diagnostics);
        diagnostic_print_summary(registry->diagnostics);
        module_registry_free(registry);
        return 500;
    }

    log_verbose("Loaded %d module(s)", registry->module_count);

    // Type inference for all modules
    log_section("Type Inference");

    // Run type inference on all modules in dependency order
    // Start with imported modules (dependencies) first, then the entry module

    // First, run type inference on all imported modules
    for (Module* mod = registry->modules; mod != NULL; mod = mod->next) {
        if (mod == entry_module) continue; // Skip entry module, do it last

        log_verbose("Running type inference on module: %s", mod->relative_path);

        // Create symbol table for the module if it doesn't have one
        if (!mod->module_scope) {
            mod->module_scope = symbol_table_create(NULL);
        }

        // Setup import symbols for this module
        log_verbose("Setting up imports for module: %s", mod->relative_path);
        if (!module_setup_import_symbols(mod, mod->module_scope)) {
            log_error("Failed to setup import symbols for module: %s", mod->relative_path);
            module_registry_free(registry);
            return 404;
        }

        // Use the module's own TypeContext
        type_inference_with_diagnostics(mod->ast, mod->module_scope, mod->type_ctx, registry->diagnostics);

        if (diagnostic_has_errors(registry->diagnostics)) {
            log_error("Type inference failed for module: %s", mod->relative_path);
            diagnostic_report_console(registry->diagnostics);
            diagnostic_print_summary(registry->diagnostics);
            module_registry_free(registry);
            return 404;
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
        return 404;
    }

    // Finally, run type inference on the entry module
    log_verbose("Running type inference on entry module: %s", entry_module->relative_path);
    // Use the entry module's own TypeContext
    type_inference_with_diagnostics(entry_module->ast, entry_module->module_scope, entry_module->type_ctx, registry->diagnostics);

    // Check for type inference errors
    if (diagnostic_has_errors(registry->diagnostics)) {
        diagnostic_report_console(registry->diagnostics);
        diagnostic_print_summary(registry->diagnostics);
        module_registry_free(registry);
        return 500;
    }

    // Print specializations if any
    if (entry_module->ast->type_ctx) {
        specialization_context_print(entry_module->ast->type_ctx);
    }

    log_verbose("Type checking complete");

    // Code generation
    log_section("Code Generation");
    CodeGen* gen = codegen_create("js_module");
    gen->enable_debug_symbols = enable_debug_symbols;  // Set debug symbols flag
    gen->enable_debug = enable_debug;  // Set debug mode flag
    if (enable_debug_symbols) {
        gen->source_filename = input_file;
    }

    // Generate code for all modules in dependency order (dependencies first, then entry)
    for (Module* mod = registry->modules; mod != NULL; mod = mod->next) {
        if (mod == entry_module) continue; // Skip entry module, do it last

        log_verbose("Generating code for module: %s", mod->relative_path);
        codegen_generate(gen, mod->ast, false, registry->diagnostics); // Not entry module
    }

    // Finally, generate code for the entry module
    log_verbose("Generating code for entry module: %s", entry_module->relative_path);
    codegen_generate(gen, entry_module->ast, true, registry->diagnostics); // Is entry module

    log_info("Code generation complete");

    // Determine what to do based on flags
    if (emit_llvm) {
        // Emit LLVM IR
        codegen_emit_llvm_ir(gen, output_file);
        log_info("LLVM IR written to %s", output_file);
    } else if (emit_asm) {
        // Emit assembly
        log_section("Compilation");
        if (compile_to_object_or_asm(gen->module, output_file, opt_level, true) != 0) {
            codegen_free(gen);
            module_registry_free(registry);
            return 1;
        }
    } else {
        // Compile to object file or binary
        char temp_obj_file[256];
        const char* obj_file;
        
        if (compile_only) {
            obj_file = output_file;
        } else {
            // Create unique temporary file using process ID to avoid collisions in parallel execution
            snprintf(temp_obj_file, sizeof(temp_obj_file), "/tmp/jsasta_temp_%d.o", getpid());
            obj_file = temp_obj_file;
        }

        log_section("Compilation");
        if (compile_to_object_or_asm(gen->module, obj_file, opt_level, false) != 0) {
            codegen_free(gen);
            module_registry_free(registry);
            return 1;
        }

        if (!compile_only) {
            // Link to create executable
            log_section("Linking");
            if (link_executable(obj_file, output_file, sanitizer, enable_debug_symbols) != 0) {
                codegen_free(gen);
                module_registry_free(registry);
                return 1;
            }

            // Clean up temporary object file
            unlink(obj_file);
        }
    }

    diagnostic_print_summary(registry->diagnostics);

    // Cleanup
    codegen_free(gen);
    module_registry_free(registry);
    return 0;
}

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options] <input.jsa>\n", program_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o <file>          Output file (default: a.out)\n");
    fprintf(stderr, "  -c                 Compile to object file only (don't link)\n");
    fprintf(stderr, "  -S                 Emit assembly only (don't assemble or link)\n");
    fprintf(stderr, "  -L, --emit-llvm    Emit LLVM IR instead of native code\n");
    fprintf(stderr, "  -O<level>          Optimization level: 0, 1, 2, 3 (default: 0)\n");
    fprintf(stderr, "  --sanitize=<type>  Enable sanitizer: address, memory, thread, undefined\n");
    fprintf(stderr, "  -g, --debug        Generate debug symbols (DWARF)\n");
    fprintf(stderr, "  -d, --debug-mode   Enable debug mode (enables debug.assert)\n");
    fprintf(stderr, "  -v, --verbose      Enable verbose output\n");
    fprintf(stderr, "  -q, --quiet        Suppress info messages (warnings and errors only)\n");
    fprintf(stderr, "  -h, --help         Show this help message\n");
}

int main(int argc, char** argv) {
    // Initialize global type system FIRST (before any parsing/type inference)
    type_system_init_global_types();

    LogLevel log_level = LOG_INFO;
    bool enable_debug_symbols = false;
    bool enable_debug = false;
    bool emit_llvm = false;
    bool emit_asm = false;
    bool compile_only = false;
    int opt_level = 0;
    const char* sanitizer = NULL;
    const char* output_file = NULL;  // Will be set based on mode

    // Parse command-line options
    static struct option long_options[] = {
        {"debug",      no_argument,       0, 'g'},
        {"debug-mode", no_argument,       0, 'd'},
        {"verbose",    no_argument,       0, 'v'},
        {"quiet",      no_argument,       0, 'q'},
        {"help",       no_argument,       0, 'h'},
        {"emit-llvm",  no_argument,       0, 'L'},
        {"sanitize",   required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    // Parse -O flags first (before getopt processes them)
    // We need to do this manually because getopt doesn't handle -O3 style flags well
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'O') {
            if (argv[i][2] >= '0' && argv[i][2] <= '3' && argv[i][3] == '\0') {
                opt_level = argv[i][2] - '0';
            } else if (argv[i][2] != '\0') {
                fprintf(stderr, "Invalid optimization flag: %s (use -O0, -O1, -O2, or -O3)\n", argv[i]);
                return 1;
            }
        }
    }

    int opt;
    int option_index = 0;
    opterr = 0;  // Suppress getopt error messages for -O flags
    while ((opt = getopt_long(argc, argv, "o:cSLgdvqh", long_options, &option_index)) != -1) {
        if (opt == '?') {
            // Check if it's an -O flag (which we already handled)
            if (optopt == 'O' || (optind > 1 && argv[optind-1][0] == '-' && argv[optind-1][1] == 'O')) {
                continue;  // Skip -O flags, we already handled them
            }
            // Otherwise, it's an actual unknown option
            print_usage(argv[0]);
            return 1;
        }
        switch (opt) {
            case 'o':
                output_file = optarg;
                break;
            case 'c':
                compile_only = true;
                break;
            case 'S':
                emit_asm = true;
                break;
            case 'L':
                emit_llvm = true;
                break;
            case 'g':
                enable_debug_symbols = true;
                break;
            case 'd':
                enable_debug = true;
                break;
            case 'v':
                log_level = LOG_VERBOSE;
                break;
            case 'q':
                log_level = LOG_WARNING;
                break;
            case 's':
                sanitizer = optarg;
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

    // Set default output file if not specified
    if (!output_file) {
        if (emit_llvm) {
            output_file = "output.ll";
        } else if (emit_asm) {
            output_file = "output.s";
        } else if (compile_only) {
            output_file = "output.o";
        } else {
            output_file = "a.out";
        }
    } else {
        // Auto-detect mode from output file extension if not explicitly set
        size_t len = strlen(output_file);
        if (!emit_llvm && !emit_asm && !compile_only) {
            if (len > 3 && strcmp(output_file + len - 3, ".ll") == 0) {
                emit_llvm = true;
            } else if (len > 2 && strcmp(output_file + len - 2, ".s") == 0) {
                emit_asm = true;
            } else if (len > 2 && strcmp(output_file + len - 2, ".o") == 0) {
                compile_only = true;
            }
        }
    }

    return compile_file(input_file, output_file, emit_llvm, emit_asm, compile_only,
                       opt_level, sanitizer, enable_debug_symbols, enable_debug);
}
