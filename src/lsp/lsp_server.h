#ifndef LSP_SERVER_H
#define LSP_SERVER_H

#include "../common/jsasta_compiler.h"
#include "../common/diagnostics.h"
#include "lsp_protocol.h"
#include <stdbool.h>

// === CodeIndex for LSP Features ===

// Source code range for LSP (covers a span of text)
typedef struct SourceRange {
    const char* filename;
    size_t start_line;
    size_t start_column;
    size_t end_line;
    size_t end_column;
} SourceRange;

// Information about a code element for LSP features
typedef struct CodeInfo {
    char* name;
    
    enum {
        CODE_TYPE,        // struct/type definition
        CODE_FUNCTION,    
        CODE_VARIABLE,
        CODE_PARAMETER,
        CODE_NAMESPACE,
        CODE_MEMBER       // struct member
    } kind;
    
    TypeInfo* type_info;
    SourceRange definition;
    
    char* description;  // For hover
    
    // Key: Use ASTNode* declaration as identifier
    ASTNode* decl_node;
    
    // Temporary storage during traversal - converted to positions array later
    SourceRange* temp_references;
    int temp_reference_count;
    int temp_reference_capacity;
    
    struct CodeInfo* next;
} CodeInfo;

// Position-based lookup entry
typedef struct PositionEntry {
    SourceRange range;
    CodeInfo* code_info;
    bool is_definition;
} PositionEntry;

// The complete index for LSP
typedef struct CodeIndex {
    CodeInfo* code_items;
    PositionEntry* positions;      // Array of positions (sorted by line, then column)
    int position_count;
    int position_capacity;
} CodeIndex;

// Forward declare for timer
typedef struct LSPDocument LSPDocument;

// Parsed snapshot of a document (can be in main or diagnostic thread)
typedef struct ParsedSnapshot {
    ASTNode* ast;
    SymbolTable* symbols;
    TypeContext* type_ctx;
    DiagnosticContext* diagnostics;
    CodeIndex* code_index;
    char* content;           // Content that was parsed
    char* filename;          // Filename used for parsing
} ParsedSnapshot;

// Document state in the LSP server
struct LSPDocument {
    char* uri;               // Document URI (file:///path/to/file.jsa)
    char* filename;          // Filesystem path (for AST location info)
    char* content;           // Current document content
    int version;             // Document version (incremented on changes)
    
    // Main thread's parsed state (for LSP features - Go to Definition, etc)
    ParsedSnapshot* main_snapshot;
    
    // Mutex to protect main_snapshot swapping
    pthread_mutex_t swap_mutex;
    
    bool needs_reparse;      // Flag to track if document needs reparsing
    
    // Debouncing for document changes
    void* debounce_timer;    // Platform-specific timer (timer_t on POSIX)
    bool timer_active;       // Whether a timer is currently scheduled
    void* server;            // Back-reference to LSPServer for timer callback
};

// LSP Server state
typedef struct {
    // Client info
    int client_pid;
    char* root_uri;
    char* client_name;
    
    // Server state
    bool initialized;
    bool shutdown_requested;
    
    // Documents
    LSPDocument** documents;
    int document_count;
    int document_capacity;
    
    // Server capabilities
    LSPServerCapabilities capabilities;
    
    // Mutex for stdout writes (worker threads send diagnostics)
    pthread_mutex_t write_mutex;
} LSPServer;

// === Server Lifecycle ===

// Create LSP server
LSPServer* lsp_server_create(void);

// Free LSP server
void lsp_server_free(LSPServer* server);

// Main server loop (reads messages from stdin, writes to stdout)
void lsp_server_run(LSPServer* server);

// === Document Management ===

// Open a document (didOpen notification)
LSPDocument* lsp_document_open(LSPServer* server, const char* uri, const char* language_id, int version, const char* text);

// Update a document (didChange notification)
void lsp_document_update(LSPServer* server, const char* uri, int version, const char* text);

// Close a document (didClose notification)
void lsp_document_close(LSPServer* server, const char* uri);

// Find document by URI
LSPDocument* lsp_document_find(LSPServer* server, const char* uri);

// Get diagnostics for a document (from a parsed snapshot)
void lsp_document_get_diagnostics(ParsedSnapshot* snapshot, LSPDiagnostic** out_diagnostics, int* out_count);

// === LSP Request Handlers ===

// Handle initialize request
char* lsp_handle_initialize(LSPServer* server, const char* params);

// Handle shutdown request
char* lsp_handle_shutdown(LSPServer* server);

// Handle textDocument/hover request
char* lsp_handle_hover(LSPServer* server, const char* params);

// Handle textDocument/completion request
char* lsp_handle_completion(LSPServer* server, const char* params);

// Handle textDocument/definition request
char* lsp_handle_definition(LSPServer* server, const char* params);

// Handle textDocument/references request
char* lsp_handle_references(LSPServer* server, const char* params);

// Handle textDocument/documentSymbol request
char* lsp_handle_document_symbol(LSPServer* server, const char* params);

// === LSP Notification Handlers ===

// Handle initialized notification
void lsp_handle_initialized(LSPServer* server);

// Handle exit notification
void lsp_handle_exit(LSPServer* server);

// Handle textDocument/didOpen notification
void lsp_handle_did_open(LSPServer* server, const char* params);

// Handle textDocument/didChange notification
void lsp_handle_did_change(LSPServer* server, const char* params);

// Handle textDocument/didClose notification
void lsp_handle_did_close(LSPServer* server, const char* params);

// Handle textDocument/didSave notification
void lsp_handle_did_save(LSPServer* server, const char* params);

// === Helper Functions ===

// Convert file:// URI to file path
char* lsp_uri_to_path(const char* uri);

// Convert file path to file:// URI
char* lsp_path_to_uri(const char* path);

// Find symbol at position in document
SymbolEntry* lsp_find_symbol_at_position(LSPDocument* doc, LSPPosition pos);

// Find node at position in AST
ASTNode* lsp_find_node_at_position(ASTNode* ast, LSPPosition pos);

// Find identifier at position (line, character are 0-based)
ASTNode* lsp_find_identifier_at_position(ASTNode* ast, int line, int character);

// Find document by URI
LSPDocument* lsp_server_find_document(LSPServer* server, const char* uri);

// Get hover information for a node
char* lsp_get_hover_info(ASTNode* node, SymbolEntry* symbol);

// === ParsedSnapshot Functions ===

// Create a new parsed snapshot
ParsedSnapshot* parsed_snapshot_create(void);

// Free a parsed snapshot
void parsed_snapshot_free(ParsedSnapshot* snapshot);

// === CodeIndex Functions ===

// Create a new code index
CodeIndex* code_index_create(void);

// Free a code index
void code_index_free(CodeIndex* index);

// Add a definition to the code index
void code_index_add_definition(CodeIndex* index, ASTNode* decl_node, const char* name, 
                                int kind, TypeInfo* type_info, SourceRange range);

// Add a reference to an existing code item
void code_index_add_reference(CodeIndex* index, ASTNode* decl_node, SourceRange range);

// Find code info at a specific position (returns NULL if nothing found)
PositionEntry* code_index_find_at_position(CodeIndex* index, const char* filename, 
                                           size_t line, size_t column);

// Build code index from AST (called after type inference)
void code_index_build(CodeIndex* index, ASTNode* ast, SymbolTable* symbols);

#endif // LSP_SERVER_H
