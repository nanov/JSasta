#ifndef LSP_SERVER_H
#define LSP_SERVER_H

#include "../common/jsasta_compiler.h"
#include "../common/diagnostics.h"
#include "../common/string_utils.h"
#include "lsp/tmp_protocol.h"
#include "lsp_protocol.h"
#include <stdbool.h>
#include <stdatomic.h>

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

// Forward declarations
typedef struct LSPDocument LSPDocument;

// Analysis work for a document (parsing + type inference)
// This represents a parsed document with all analysis data.
// Can be created in main thread and passed to worker thread for type inference.
typedef struct AnalysisWork {
    char* uri;                      // Document URI (for sending diagnostics)
    char* filename;                 // Filename for diagnostics and AST location info
    ASTNode* ast;                   // AST to run type inference on
    SymbolTable* symbols;           // Symbol table
    TypeContext* type_ctx;          // Type context
    DiagnosticContext* diagnostics; // Diagnostics context
} AnalysisWork;

// Document state in the LSP server
struct LSPDocument {
    char* uri;               // Document URI (file:///path/to/file.jsa)
    char* filename;          // Filesystem path (for AST location info)
    JsaStringBuilder* content;  // Current document content (mutable for incremental updates)
    int version;             // Document version (incremented on changes)

    // Code index for LSP features (Go to Definition, Hover, References, etc)
    // Only accessed by main thread, no synchronization needed
    CodeIndex* code_index;

    // Per-document work queue for type inference
    AnalysisWork* pending_work;  // NULL if no work, otherwise work for this document (protected by work_mutex)

    // Completed type inference work with typed AST for code index rebuild
    // Worker thread stores completed work here, main thread consumes it
    // If not NULL, code index needs to be rebuilt with type information
    _Atomic(AnalysisWork*) completed_work;  // Atomic pointer, no mutex needed

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

    // Work queue for type inference worker
    pthread_mutex_t work_mutex;        // Protects access to all documents' pending_work
    pthread_cond_t work_available;     // Condition variable to signal when work is ready
    pthread_t worker_thread;           // Persistent worker thread
    bool worker_running;               // Flag to stop worker on shutdown
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
// If range is NULL, it's a full document sync (text replaces everything)
// If range is provided, it's an incremental update (text replaces the range)
void lsp_document_update(LSPServer* server, LspJsonDidChangeTextDocumentParams* params); //  const char* uri, int version, const TextRange* range, const char* text);

// Close a document (didClose notification)
void lsp_document_close(LSPServer* server, const char* uri);

// Find document by URI
LSPDocument* lsp_document_find(LSPServer* server, const char* uri);

// Get code index for a document, rebuilding it if type inference completed
// Returns the code index, or NULL if not available
CodeIndex* lsp_document_get_code_index(LSPDocument* doc);

// Get diagnostics for a document (from analysis work)
void lsp_document_get_diagnostics(AnalysisWork* work, LSPDiagnostic** out_diagnostics, int* out_count);

// === LSP Request Handlers ===

// Handle initialize request
char* lsp_handle_initialize(LSPServer* server, LspJsonInitializeParams* params);

// Handle shutdown request
char* lsp_handle_shutdown(LSPServer* server);

// Handle textDocument/hover request
char* lsp_handle_hover(LSPServer* server, LspJsonHoverParams* params);

// Handle textDocument/completion request
char* lsp_handle_completion(LSPServer* server, LspJsonCompletionParams* params);

// Handle textDocument/definition request
char* lsp_handle_definition(LSPServer* server, LspJsonTextDocumentPositionParams* params);

// Handle textDocument/references request
char* lsp_handle_references(LSPServer* server, LspJsonTextDocumentPositionParams* params);

// Handle textDocument/inlayHint request
char* lsp_handle_inlay_hint(LSPServer* server, LspJsonInlayHintParams* params);

// Handle textDocument/documentSymbol request
char* lsp_handle_document_symbol(LSPServer* server, const char* params);

// === LSP Notification Handlers ===

// Handle initialized notification
void lsp_handle_initialized(LSPServer* server);

// Handle exit notification
void lsp_handle_exit(LSPServer* server);

// Handle textDocument/didOpen notification
void lsp_handle_did_open(LSPServer* server, LspJsonDidOpenTextDocumentParams* params);

// Handle textDocument/didChange notification
void lsp_handle_did_change(LSPServer* server, LspJsonDidChangeTextDocumentParams* params);

// Handle textDocument/didClose notification
void lsp_handle_did_close(LSPServer* server, LspJsonDidCloseTextDocumentParams* params);

// Handle textDocument/didSave notification
void lsp_handle_did_save(LSPServer* server, LspJsonDidSaveTextDocumentParams* params);

// === Helper Functions ===

// Convert file:// URI to file path
char* lsp_uri_to_path(const char* uri);

// Convert file path to file:// URI
char* lsp_path_to_uri(const char* path);

// Find symbol at position in document
SymbolEntry* lsp_find_symbol_at_position(LSPDocument* doc, TextPosition pos);

// Find node at position in AST
ASTNode* lsp_find_node_at_position(ASTNode* ast, TextPosition pos);

// Find identifier at position (line, character are 0-based)
ASTNode* lsp_find_identifier_at_position(ASTNode* ast, int line, int character);

// Find document by URI
LSPDocument* lsp_server_find_document(LSPServer* server, const char* uri);

// Get hover information for a node
char* lsp_get_hover_info(ASTNode* node, SymbolEntry* symbol);

// === AnalysisWork Functions ===

// Parse content into analysis work (WITHOUT running type inference yet)
AnalysisWork* analysis_work_parse(const char* content, const char* uri, const char* filename);

// Free an analysis work
void analysis_work_free(AnalysisWork* work);

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
