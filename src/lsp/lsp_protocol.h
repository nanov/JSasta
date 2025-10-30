#ifndef LSP_PROTOCOL_H
#define LSP_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

// JSON-RPC message types
typedef enum {
    LSP_MSG_REQUEST,
    LSP_MSG_RESPONSE,
    LSP_MSG_NOTIFICATION
} LSPMessageType;

// LSP message structure (abstract)
typedef struct {
    LSPMessageType type;
    char* jsonrpc;  // Always "2.0"
    union {
        struct {
            int64_t id;      // Request ID (can be number or string, we use number)
            char* method;
            char* params;    // JSON string
        } request;
        struct {
            int64_t id;
            char* result;    // JSON string (NULL if error)
            char* error;     // JSON string (NULL if success)
        } response;
        struct {
            char* method;
            char* params;    // JSON string
        } notification;
    };
} LSPMessage;

// LSP Initialization options
typedef struct {
    int process_id;
    char* root_uri;
    char* client_name;
    char* client_version;
} LSPInitializeParams;

// LSP Position (line and character)
typedef struct {
    int line;       // 0-based
    int character;  // 0-based (UTF-16 code units)
} LSPPosition;

// LSP Range
typedef struct {
    LSPPosition start;
    LSPPosition end;
} LSPRange;

// LSP Location
typedef struct {
    char* uri;
    LSPRange range;
} LSPLocation;

// Diagnostic severity
typedef enum {
    LSP_DIAGNOSTIC_ERROR = 1,
    LSP_DIAGNOSTIC_WARNING = 2,
    LSP_DIAGNOSTIC_INFORMATION = 3,
    LSP_DIAGNOSTIC_HINT = 4
} LSPDiagnosticSeverity;

// LSP Diagnostic
typedef struct {
    LSPRange range;
    LSPDiagnosticSeverity severity;
    char* code;
    char* source;
    char* message;
} LSPDiagnostic;

// Text document identifier
typedef struct {
    char* uri;
} LSPTextDocumentIdentifier;

// Text document item (for didOpen)
typedef struct {
    char* uri;
    char* language_id;
    int version;
    char* text;
} LSPTextDocumentItem;

// Text document position params (for hover, completion, etc.)
typedef struct {
    LSPTextDocumentIdentifier text_document;
    LSPPosition position;
} LSPTextDocumentPositionParams;

// Hover result
typedef struct {
    char* contents;  // Markdown string
    LSPRange* range; // Optional
} LSPHover;

// Completion item
typedef struct {
    char* label;
    int kind;  // CompletionItemKind
    char* detail;
    char* documentation;
    char* insert_text;
} LSPCompletionItem;

// Server capabilities
typedef struct {
    bool text_document_sync;  // Full sync for now
    bool hover_provider;
    bool completion_provider;
    bool definition_provider;
    bool references_provider;
    bool document_symbol_provider;
    bool diagnostic_provider;
} LSPServerCapabilities;

// === JSON-RPC Protocol Functions ===

// Check if stdin has data available (with timeout in ms)
// Returns: 1 if data available, 0 if timeout, -1 if error
int lsp_check_stdin(int timeout_ms);

// Read a message from stdin (reads Content-Length header and body)
LSPMessage* lsp_read_message(void);

// Write a message to stdout
void lsp_write_message(const char* json_content);

// Free an LSP message
void lsp_message_free(LSPMessage* msg);

// === JSON Parsing/Serialization ===

// Parse JSON-RPC message from string
LSPMessage* lsp_parse_message(const char* json);

// Serialize response to JSON
char* lsp_serialize_response(int64_t id, const char* result);

// Serialize error response to JSON
char* lsp_serialize_error(int64_t id, int code, const char* message);

// Serialize notification to JSON
char* lsp_serialize_notification(const char* method, const char* params);

// === Helper Functions ===

// Create initialize response JSON
char* lsp_create_initialize_response(LSPServerCapabilities* caps);

// Create diagnostics notification JSON
char* lsp_create_diagnostics_notification(const char* uri, LSPDiagnostic* diagnostics, int count);

// Create hover response JSON
char* lsp_create_hover_response(LSPHover* hover);

// Create completion response JSON
char* lsp_create_completion_response(LSPCompletionItem* items, int count);

// Create locations response JSON (for definition, references)
char* lsp_create_locations_response(LSPLocation* locations, int count);

#endif // LSP_PROTOCOL_H
