#ifndef JSASTA_MODULE_LOADER_H
#define JSASTA_MODULE_LOADER_H

#include "jsasta_compiler.h"
#include <stdbool.h>

// Forward declarations
typedef struct Module Module;
typedef struct ModuleRegistry ModuleRegistry;
typedef struct ExportedSymbol ExportedSymbol;

// Represents a symbol exported from a module
typedef struct ExportedSymbol {
    char* name;                  // Original name in the module (e.g., "add")
    ASTNode* declaration;        // The AST node being exported (function, const, struct)
    struct ExportedSymbol* next; // Linked list
} ExportedSymbol;

// Represents a loaded module
struct Module {
    char* absolute_path;         // Absolute path to the module file
    char* relative_path;         // Relative path from project root
    char* module_prefix;         // Mangled prefix for symbols (e.g., "math_lib")

    char* source_code;           // Source code content
    ASTNode* ast;                // Parsed AST
    SymbolTable* module_scope;   // Module's global scope (NOT accessible from outside)
    TypeContext* type_ctx;       // Type context (shared with registry)
    struct DiagnosticContext* diagnostics; // Diagnostics for this module

    ExportedSymbol* exports;     // Linked list of exported symbols
    int export_count;

    Module** dependencies;       // Modules this module imports
    int dependency_count;

    bool is_loading;             // Currently being loaded (for cyclic import detection)
    bool is_parsed;              // Has been parsed

    struct Module* next;         // For registry linked list
};

// Registry that manages all loaded modules
struct ModuleRegistry {
    Module* modules;             // Linked list of all loaded modules
    int module_count;

    char* project_root;          // Absolute path to project root
    TypeContext* type_ctx;       // Shared type context
    struct DiagnosticContext* diagnostics; // Shared diagnostics
};

// === Module Registry Functions ===

// Create a new module registry
ModuleRegistry* module_registry_create(const char* entry_file);

// Free the module registry and all modules
void module_registry_free(ModuleRegistry* registry);

// === Module Loading Functions ===

// Load a module from a file path (resolves relative to current_module)
// Returns existing module if already loaded
Module* module_load(ModuleRegistry* registry, const char* path, Module* current_module);

// Get or create a module for a file path
Module* module_get_or_create(ModuleRegistry* registry, const char* absolute_path);

// Find a module by absolute path
Module* module_find(ModuleRegistry* registry, const char* absolute_path);

// === Module Parsing and Analysis ===

// Parse a module (read file, tokenize, parse AST)
bool module_parse(Module* module, ModuleRegistry* registry);

// Collect exports from a module (scan AST for export declarations)
bool module_collect_exports(Module* module);

// Load all imported modules recursively
bool module_load_imports(Module* module, ModuleRegistry* registry);

// Add namespaced symbols to module scope for all imports
// Creates symbols like "math.add" that reference the actual exported symbols
bool module_setup_import_symbols(Module* module, SymbolTable* symbols);

// Type-check a module (including resolving imports)


// === Export Management ===

// Add an exported symbol to a module
void module_add_export(Module* module, const char* name, ASTNode* declaration);

// Find an exported symbol by name
ExportedSymbol* module_find_export(Module* module, const char* name);

// Register a codegen callback for a builtin function (called from compiler layer)
void module_register_codegen_callback(Module* module, const char* func_name, BuiltinCodegenCallback callback);

// === Path Resolution ===

// Resolve a module path relative to current module or project root
// "./math.jsa" relative to "src/main.jsa" -> "/project/src/math.jsa"
char* module_resolve_path(ModuleRegistry* registry, const char* import_path, Module* current_module);

// Get relative path from project root
// "/project/src/utils/math.jsa" -> "src/utils/math"
char* module_get_relative_path(ModuleRegistry* registry, const char* absolute_path);

// Generate module prefix for name mangling
// "src/utils/math.jsa" -> "src_utils_math"
char* module_generate_prefix(const char* relative_path);

// === Symbol Mangling ===

// Get mangled name for an exported symbol
// module_prefix="math_lib", symbol="add" -> "math_lib__add"
char* module_mangle_symbol(const char* module_prefix, const char* symbol_name);

// === Utility Functions ===

// Read file contents into a string
char* module_read_file(const char* path);

// Get absolute path from relative path
char* module_get_absolute_path(const char* path);

// Get directory of a file path
char* module_get_directory(const char* path);

#endif // JSASTA_MODULE_LOADER_H
