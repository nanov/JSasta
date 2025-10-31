#include "module_loader.h"
#include "logger.h"
#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

// === Module Registry Functions ===

ModuleRegistry* module_registry_create(const char* entry_file) {
    ModuleRegistry* registry = (ModuleRegistry*)malloc(sizeof(ModuleRegistry));
    
    registry->modules = NULL;
    registry->module_count = 0;
    
    // Find project root (directory containing entry file)
    char* entry_abs = module_get_absolute_path(entry_file);
    registry->project_root = module_get_directory(entry_abs);
    free(entry_abs);
    
    // Create shared type context and diagnostics (use DIRECT mode for immediate error output)
    registry->type_ctx = type_context_create();
    registry->diagnostics = diagnostic_context_create_with_mode(DIAG_MODE_DIRECT, stderr);
    
    log_verbose("Module registry created with project root: %s", registry->project_root);
    
    return registry;
}

void module_registry_free(ModuleRegistry* registry) {
    if (!registry) return;
    
    // IMPORTANT: Free TypeContext FIRST, before freeing ASTs
    // AST nodes contain references to TypeInfo objects owned by TypeContext
    // If we free ASTs first, they try to free arrays that reference TypeInfo objects
    // which causes double-free when TypeContext is freed
    type_context_free(registry->type_ctx);
    registry->type_ctx = NULL;
    
    // Free all modules
    Module* current = registry->modules;
    while (current) {
        Module* next = current->next;
        
        // Free module data
        free(current->absolute_path);
        free(current->relative_path);
        free(current->module_prefix);
        free(current->source_code);
        
        // Free AST (now safe since TypeContext is already freed)
        // ast_free also frees the module_scope (symbol table) attached to it
        if (current->ast) ast_free(current->ast);
        
        // Free exports
        ExportedSymbol* exp = current->exports;
        while (exp) {
            ExportedSymbol* next_exp = exp->next;
            free(exp->name);
            // Note: declaration is part of AST, freed above
            free(exp);
            exp = next_exp;
        }
        
        // Free dependencies array
        free(current->dependencies);
        
        free(current);
        current = next;
    }
    
    // Free remaining shared resources
    diagnostic_context_free(registry->diagnostics);
    free(registry->project_root);
    free(registry);
}

// === Module Loading Functions ===

Module* module_load(ModuleRegistry* registry, const char* path, Module* current_module) {
    // Resolve the path
    char* absolute_path = module_resolve_path(registry, path, current_module);
    
    if (!absolute_path) {
        log_error("Failed to resolve module path: %s", path);
        return NULL;
    }
    
    // Check if module is already loaded
    Module* existing = module_find(registry, absolute_path);
    if (existing) {
        free(absolute_path);
        
        // Check for cyclic import
        if (existing->is_loading) {
            log_error("Cyclic import detected: %s is already being loaded", existing->relative_path);
            return NULL;
        }
        
        log_verbose("Module already loaded: %s", existing->relative_path);
        return existing;
    }
    
    // Create new module
    Module* module = module_get_or_create(registry, absolute_path);
    free(absolute_path);
    
    if (!module) {
        return NULL;
    }
    
    // Mark as loading (for cyclic import detection)
    module->is_loading = true;
    
    // Parse the module
    if (!module_parse(module, registry)) {
        log_error("Failed to parse module: %s", module->relative_path);
        module->is_loading = false;
        return NULL;
    }
    
    // Collect exports
    if (!module_collect_exports(module)) {
        log_error("Failed to collect exports from module: %s", module->relative_path);
        module->is_loading = false;
        return NULL;
    }
    
    // Load imported modules (recursive) - this is where cyclic imports would be detected
    if (!module_load_imports(module, registry)) {
        log_error("Failed to load imports for module: %s", module->relative_path);
        module->is_loading = false;
        return NULL;
    }
    
    // Mark as done loading
    module->is_loading = false;
    
    log_info("Loaded module: %s (%d exports, %d dependencies)", 
             module->relative_path, module->export_count, module->dependency_count);
    
    return module;
}

Module* module_get_or_create(ModuleRegistry* registry, const char* absolute_path) {
    // Check if exists
    Module* existing = module_find(registry, absolute_path);
    if (existing) return existing;
    
    // Create new module
    Module* module = (Module*)calloc(1, sizeof(Module));
    
    module->absolute_path = strdup(absolute_path);
    module->relative_path = module_get_relative_path(registry, absolute_path);
    module->module_prefix = module_generate_prefix(module->relative_path);
    
    module->source_code = NULL;
    module->ast = NULL;
    module->module_scope = NULL;
    module->type_ctx = type_context_create();
    module->type_ctx->module_prefix = strdup(module->module_prefix);
    module->diagnostics = registry->diagnostics;
    
    module->exports = NULL;
    module->export_count = 0;
    
    module->dependencies = NULL;
    module->dependency_count = 0;
    
    module->is_loading = false;
    module->is_parsed = false;
    
    // Add to registry
    module->next = registry->modules;
    registry->modules = module;
    registry->module_count++;
    
    log_verbose("Created module: %s (prefix: %s)", module->relative_path, module->module_prefix);
    
    return module;
}

Module* module_find(ModuleRegistry* registry, const char* absolute_path) {
    for (Module* m = registry->modules; m != NULL; m = m->next) {
        if (strcmp(m->absolute_path, absolute_path) == 0) {
            return m;
        }
    }
    return NULL;
}

// === Module Parsing and Analysis ===

bool module_parse(Module* module, ModuleRegistry* registry) {
    if (module->is_parsed) return true;
    
    // Read source file
    module->source_code = module_read_file(module->absolute_path);
    if (!module->source_code) {
        log_error("Failed to read module file: %s", module->absolute_path);
        return false;
    }
    
    log_verbose("Parsing module: %s", module->relative_path);
    
    // Parse
    // Use module's own TypeContext so types are registered in the same context used by type inference
    Parser* parser = parser_create(module->source_code, module->absolute_path, 
                                   module->type_ctx, registry->diagnostics);
    module->ast = parser_parse(parser);
    parser_free(parser);
    
    if (!module->ast) {
        log_error("Failed to parse module AST: %s", module->relative_path);
        return false;
    }
    
    // Check if there were parse errors (diagnostics already printed in DIRECT mode)
    if (registry->diagnostics && diagnostic_has_errors(registry->diagnostics)) {
        return false;
    }
    
    module->is_parsed = true;
    return true;
}

bool module_collect_exports(Module* module) {
    if (!module->ast || module->ast->type != AST_PROGRAM) {
        return false;
    }
    
    // Scan all top-level statements for export declarations
    for (int i = 0; i < module->ast->program.count; i++) {
        ASTNode* stmt = module->ast->program.statements[i];
        
        if (stmt->type == AST_EXPORT_DECL) {
            ASTNode* decl = stmt->export_decl.declaration;
            
            // Get the name of the exported symbol
            const char* name = NULL;
            
            switch (decl->type) {
                case AST_FUNCTION_DECL:
                    name = decl->func_decl.name;
                    break;
                    
                case AST_VAR_DECL:
                    name = decl->var_decl.name;
                    break;
                    
                case AST_STRUCT_DECL:
                    name = decl->struct_decl.name;
                    break;
                    
                default:
                    log_error("Unsupported export declaration type");
                    continue;
            }
            
            if (name) {
                module_add_export(module, name, decl);
                log_verbose("  Exported: %s", name);
            }
        }
    }
    
    return true;
}

// Recursively load all imported modules for this module
bool module_load_imports(Module* module, ModuleRegistry* registry) {
    if (!module->ast || module->ast->type != AST_PROGRAM) {
        return false;
    }
    
    // Scan for import declarations
    for (int i = 0; i < module->ast->program.count; i++) {
        ASTNode* stmt = module->ast->program.statements[i];
        
        if (stmt->type == AST_IMPORT_DECL) {
            const char* import_path = stmt->import_decl.module_path;
            const char* namespace_name = stmt->import_decl.namespace_name;
            
            // Check for parse errors (NULL path means parse error occurred)
            if (!import_path || !namespace_name) {
                log_error("Import declaration has missing information (likely a parse error)");
                return false;
            }
            
            log_verbose("  Importing: %s as %s", import_path, namespace_name);
            
            // Load the imported module (recursively loads its imports)
            Module* imported_module = module_load(registry, import_path, module);
            
            if (!imported_module) {
                log_error("Failed to load imported module: %s", import_path);
                return false;
            }
            
            // Add to dependencies
            module->dependency_count++;
            module->dependencies = (Module**)realloc(module->dependencies, 
                sizeof(Module*) * module->dependency_count);
            module->dependencies[module->dependency_count - 1] = imported_module;
            
            // Store reference to imported module in the AST node
            stmt->import_decl.imported_module = imported_module;
            
            log_verbose("    Loaded dependency: %s (%d exports)", 
                       imported_module->relative_path, imported_module->export_count);
        }
    }
    
    return true;
}

// Add namespaced symbols for all imports to the given symbol table
bool module_setup_import_symbols(Module* module, SymbolTable* symbols) {
    if (!module->ast || module->ast->type != AST_PROGRAM) {
        return false;
    }
    
    if (!symbols) {
        log_error("Symbol table is NULL for module: %s", module->relative_path);
        return false;
    }
    
    log_verbose("Setting up import symbols for module: %s", module->relative_path);
    
    // For each import declaration in the module
    for (int i = 0; i < module->ast->program.count; i++) {
        ASTNode* stmt = module->ast->program.statements[i];
        
        if (stmt->type == AST_IMPORT_DECL) {
            const char* namespace_name = stmt->import_decl.namespace_name;
            Module* imported_module = (Module*)stmt->import_decl.imported_module;
            
            if (!imported_module) {
                log_error("Import declaration missing imported_module pointer for namespace: %s", namespace_name);
                continue;
            }
            
            // Set the module_prefix on the import node for name mangling
            if (!stmt->import_decl.module_prefix) {
                stmt->import_decl.module_prefix = strdup(imported_module->module_prefix);
            }
            
            // Add the namespace to the symbol table, storing the import AST node
            // This allows lookup of: import node -> module -> ast -> type_ctx/symbol_table
            symbol_table_insert_namespace(symbols, namespace_name, stmt);
            
            log_verbose("  Added namespace: %s (from %s, %d exports, prefix: %s)", 
                       namespace_name, imported_module->relative_path, 
                       imported_module->export_count, stmt->import_decl.module_prefix);
        }
    }
    
    return true;
}



// === Export Management ===

void module_add_export(Module* module, const char* name, ASTNode* declaration) {
    ExportedSymbol* symbol = (ExportedSymbol*)malloc(sizeof(ExportedSymbol));
    symbol->name = strdup(name);
    symbol->declaration = declaration; // Reference to AST node
    symbol->next = module->exports;
    
    module->exports = symbol;
    module->export_count++;
}

ExportedSymbol* module_find_export(Module* module, const char* name) {
    for (ExportedSymbol* exp = module->exports; exp != NULL; exp = exp->next) {
        if (strcmp(exp->name, name) == 0) {
            return exp;
        }
    }
    return NULL;
}

// === Path Resolution ===

char* module_resolve_path(ModuleRegistry* registry, const char* import_path, Module* current_module) {
    // Handle relative paths (./ or ../)
    if (import_path[0] == '.') {
        char* base_dir;
        
        if (current_module) {
            // Resolve relative to current module's directory
            base_dir = module_get_directory(current_module->absolute_path);
        } else {
            // Resolve relative to project root
            base_dir = strdup(registry->project_root);
        }
        
        // Build full path
        char temp_path[PATH_MAX];
        snprintf(temp_path, sizeof(temp_path), "%s/%s", base_dir, import_path);
        free(base_dir);
        
        return module_get_absolute_path(temp_path);
    }
    
    // Absolute or project-relative path
    return module_get_absolute_path(import_path);
}

char* module_get_relative_path(ModuleRegistry* registry, const char* absolute_path) {
    const char* root = registry->project_root;
    size_t root_len = strlen(root);
    
    // Check if path starts with project root
    if (strncmp(absolute_path, root, root_len) == 0) {
        const char* relative = absolute_path + root_len;
        
        // Skip leading slashes
        while (*relative == '/') {
            relative++;
        }
        
        // Remove .jsa extension
        char* result = strdup(relative);
        char* ext = strstr(result, ".jsa");
        if (ext) {
            *ext = '\0';
        }
        
        return result;
    }
    
    // Not under project root, use full path
    char* result = strdup(absolute_path);
    char* ext = strstr(result, ".jsa");
    if (ext) {
        *ext = '\0';
    }
    return result;
}

char* module_generate_prefix(const char* relative_path) {
    // "src/utils/math" -> "src_utils_math"
    char* prefix = strdup(relative_path);
    
    // Replace / with _
    for (char* p = prefix; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == '.' || *p == '-') {
            *p = '_';
        }
    }
    
    return prefix;
}

// === Symbol Mangling ===

char* module_mangle_symbol(const char* module_prefix, const char* symbol_name) {
    // "math_lib" + "add" -> "math_lib__add"
    size_t len = strlen(module_prefix) + strlen(symbol_name) + 3; // "__" + null
    char* mangled = (char*)malloc(len);
    snprintf(mangled, len, "%s__%s", module_prefix, symbol_name);
    return mangled;
}

// === Utility Functions ===

char* module_read_file(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read content
    char* content = (char*)malloc(size + 1);
    fread(content, 1, size, file);
    content[size] = '\0';
    
    fclose(file);
    return content;
}

char* module_get_absolute_path(const char* path) {
    char resolved[PATH_MAX];
    
    if (realpath(path, resolved) == NULL) {
        // If realpath fails, just copy the path
        return strdup(path);
    }
    
    return strdup(resolved);
}

char* module_get_directory(const char* path) {
    char* path_copy = strdup(path);
    char* dir = dirname(path_copy);
    char* result = strdup(dir);
    free(path_copy);
    return result;
}
