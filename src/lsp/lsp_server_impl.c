#include "lsp_server.h"
#include "lsp_protocol.h"
#include "lsp_json.h"
#include "../common/logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

// Debug logging to file
#define LSP_LOG(fmt, ...) do { \
    FILE* f = fopen("/tmp/jsasta_lsp.log", "a"); \
    if (f) { \
        time_t now = time(NULL); \
        char* timestamp = ctime(&now); \
        timestamp[strlen(timestamp)-1] = '\0'; \
        fprintf(f, "[%s] " fmt "\n", timestamp, ##__VA_ARGS__); \
        fflush(f); \
        fclose(f); \
    } \
} while(0)

// Debounce delay for parsing in milliseconds
#define PARSE_DEBOUNCE_MS 300

// Forward declarations
static ParsedSnapshot* lsp_parse_snapshot(const char* content, const char* filename, bool run_type_inference);
static void* diagnostic_worker_thread(void* arg);
static void lsp_schedule_diagnostic_worker(LSPDocument* doc);

// === ParsedSnapshot Functions ===

ParsedSnapshot* parsed_snapshot_create(void) {
    ParsedSnapshot* snapshot = malloc(sizeof(ParsedSnapshot));
    snapshot->ast = NULL;
    snapshot->symbols = NULL;
    snapshot->type_ctx = NULL;
    snapshot->diagnostics = NULL;
    snapshot->code_index = NULL;
    snapshot->content = NULL;
    snapshot->filename = NULL;
    return snapshot;
}

void parsed_snapshot_free(ParsedSnapshot* snapshot) {
    if (!snapshot) return;
    
    if (snapshot->ast) ast_free(snapshot->ast);
    if (snapshot->symbols) symbol_table_free(snapshot->symbols);
    if (snapshot->type_ctx) type_context_free(snapshot->type_ctx);
    if (snapshot->diagnostics) diagnostic_context_free(snapshot->diagnostics);
    if (snapshot->code_index) code_index_free(snapshot->code_index);
    if (snapshot->content) free(snapshot->content);
    if (snapshot->filename) free(snapshot->filename);
    
    free(snapshot);
}

// Parse content into a new snapshot (can be called from any thread)
static ParsedSnapshot* lsp_parse_snapshot(const char* content, const char* filename, bool run_type_inference) {
    ParsedSnapshot* snapshot = parsed_snapshot_create();
    
    snapshot->content = strdup(content);
    snapshot->filename = strdup(filename);
    snapshot->type_ctx = type_context_create();
    snapshot->diagnostics = diagnostic_context_create_with_mode(DIAG_MODE_COLLECT, NULL);
    snapshot->symbols = symbol_table_create(NULL);
    
    LSP_LOG("Parsing snapshot: %s (type inference: %s)", filename, run_type_inference ? "yes" : "no");
    
    // Parse
    Parser* parser = parser_create(content, filename, snapshot->type_ctx, snapshot->diagnostics);
    snapshot->ast = parser_parse(parser);
    parser_free(parser);
    
    if (!snapshot->ast) {
        LSP_LOG("Parse failed - no AST");
        return snapshot;
    }
    
    // Build code index (needed for LSP features)
    snapshot->code_index = code_index_create();
    if (snapshot->code_index) {
        code_index_build(snapshot->code_index, snapshot->ast, snapshot->symbols);
        LSP_LOG("Code index built with %d positions", snapshot->code_index->position_count);
    }
    
    // Optionally run type inference
    if (run_type_inference && !diagnostic_has_errors(snapshot->diagnostics)) {
        LSP_LOG("Running type inference");
        type_inference_with_diagnostics(snapshot->ast, snapshot->symbols, snapshot->type_ctx, snapshot->diagnostics);
        LSP_LOG("Type inference complete, errors: %d, warnings: %d", 
                snapshot->diagnostics->error_count,
                snapshot->diagnostics->warning_count);
    }
    
    return snapshot;
}

// === Diagnostic Worker (Background Thread) ===

// Worker thread function - sleeps 300ms, then parses and runs diagnostics
static void* diagnostic_worker_thread(void* arg) {
    LSPDocument* doc = (LSPDocument*)arg;
    
    // Sleep for debounce delay
    usleep(PARSE_DEBOUNCE_MS * 1000);
    
    LSP_LOG("Diagnostic worker waking up for %s", doc->uri);
    
    // Get current content (need to copy since doc->content might change)
    char* content_copy = strdup(doc->content);
    char* filename_copy = strdup(doc->filename);
    
    // Parse WITHOUT type inference first (fast)
    LSP_LOG("Parsing snapshot (no type inference yet)");
    ParsedSnapshot* snapshot = lsp_parse_snapshot(content_copy, filename_copy, false);
    
    // Check if there are parse errors - send them immediately for fast feedback
    if (snapshot->diagnostics && diagnostic_has_errors(snapshot->diagnostics)) {
        LSP_LOG("Parse errors found, sending immediately");
        
        // Extract and send parse error diagnostics
        LSPDiagnostic* diagnostics;
        int count;
        lsp_document_get_diagnostics(snapshot, &diagnostics, &count);
        
        if (count > 0) {
            char* diag_params = lsp_create_diagnostics_notification(doc->uri, diagnostics, count);
            char* notification = lsp_serialize_notification("textDocument/publishDiagnostics", diag_params);
            lsp_write_message(notification);
            
            for (int i = 0; i < count; i++) {
                free(diagnostics[i].code);
                free(diagnostics[i].source);
                free(diagnostics[i].message);
            }
            free(diagnostics);
            free(diag_params);
            free(notification);
        }
        
        // Clear parse errors so they don't duplicate in type inference phase
        diagnostic_clear(snapshot->diagnostics);
    }
    
    // Swap the main snapshot for LSP features
    pthread_mutex_lock(&doc->swap_mutex);
    ParsedSnapshot* old_main = doc->main_snapshot;
    doc->main_snapshot = snapshot;
    pthread_mutex_unlock(&doc->swap_mutex);
    
    // Free old main
    if (old_main) {
        parsed_snapshot_free(old_main);
    }
    
    LSP_LOG("Main snapshot swapped, LSP features updated");
    
    // Now run type inference on a COPY for diagnostics (slow part)
    LSP_LOG("Running type inference for diagnostics");
    ParsedSnapshot* diag_snapshot = lsp_parse_snapshot(content_copy, filename_copy, true);
    
    free(content_copy);
    free(filename_copy);
    
    // Get server reference
    LSPServer* server = (LSPServer*)doc->server;
    if (!server) {
        LSP_LOG("Warning: No server reference");
        parsed_snapshot_free(diag_snapshot);
        return NULL;
    }
    
    // Send diagnostics
    LSPDiagnostic* diagnostics;
    int count;
    
    // Extract diagnostics from the diagnostic snapshot
    if (diag_snapshot->diagnostics) {
        DiagnosticContext* ctx = diag_snapshot->diagnostics;
        Diagnostic* diag_msg = ctx->head;
        count = 0;
        
        while (diag_msg) {
            count++;
            diag_msg = diag_msg->next;
        }
        
        if (count > 0) {
            diagnostics = malloc(sizeof(LSPDiagnostic) * count);
            diag_msg = ctx->head;
            int i = 0;
            
            while (diag_msg) {
                LSPDiagnostic* diag = &diagnostics[i++];
                
                int line = (diag_msg->location.line > 0) ? (int)diag_msg->location.line - 1 : 0;
                int col = (diag_msg->location.column > 0) ? (int)diag_msg->location.column - 1 : 0;
                
                diag->range.start.line = line;
                diag->range.start.character = col;
                diag->range.end.line = line;
                diag->range.end.character = col + 1;
                
                diag->severity = (diag_msg->severity == DIAG_ERROR) ? LSP_DIAGNOSTIC_ERROR : LSP_DIAGNOSTIC_WARNING;
                diag->code = diag_msg->code ? strdup(diag_msg->code) : NULL;
                diag->source = strdup("jsasta");
                diag->message = strdup(diag_msg->message);
                
                diag_msg = diag_msg->next;
            }
            
            char* diag_params = lsp_create_diagnostics_notification(doc->uri, diagnostics, count);
            char* notification = lsp_serialize_notification("textDocument/publishDiagnostics", diag_params);
            lsp_write_message(notification);
            
            for (int i = 0; i < count; i++) {
                free(diagnostics[i].code);
                free(diagnostics[i].source);
                free(diagnostics[i].message);
            }
            free(diagnostics);
            free(diag_params);
            free(notification);
        }
    }
    
    LSP_LOG("Diagnostics sent, freeing diagnostic snapshot");
    parsed_snapshot_free(diag_snapshot);
    
    return NULL;
}

static void lsp_schedule_diagnostic_worker(LSPDocument* doc) {
    LSP_LOG("Scheduling diagnostic worker");
    
    pthread_t thread;
    pthread_create(&thread, NULL, diagnostic_worker_thread, doc);
    pthread_detach(thread);
}

// (Old queue/throttle code removed - using double-buffer now)

// === Server Lifecycle ===

LSPServer* lsp_server_create(void) {
    LSPServer* server = malloc(sizeof(LSPServer));
    
    server->client_pid = 0;
    server->root_uri = NULL;
    server->client_name = NULL;
    
    server->initialized = false;
    server->shutdown_requested = false;
    
    server->documents = malloc(sizeof(LSPDocument*) * 10);
    server->document_count = 0;
    server->document_capacity = 10;
    
    // Initialize write mutex for thread-safe stdout writes
    pthread_mutex_init(&server->write_mutex, NULL);
    
    // Setup capabilities
    server->capabilities.text_document_sync = true;
    server->capabilities.hover_provider = true;
    server->capabilities.completion_provider = true;
    server->capabilities.definition_provider = true; // Implemented with CodeIndex
    server->capabilities.references_provider = true; // Implemented with CodeIndex
    server->capabilities.document_symbol_provider = false; // TODO: implement
    server->capabilities.diagnostic_provider = true;
    
    return server;
}

void lsp_server_free(LSPServer* server) {
    if (!server) return;
    
    free(server->root_uri);
    free(server->client_name);
    
    for (int i = 0; i < server->document_count; i++) {
        LSPDocument* doc = server->documents[i];
        
        free(doc->uri);
        free(doc->filename);
        free(doc->content);
        
        // Free snapshot
        pthread_mutex_lock(&doc->swap_mutex);
        if (doc->main_snapshot) {
            parsed_snapshot_free(doc->main_snapshot);
        }
        pthread_mutex_unlock(&doc->swap_mutex);
        pthread_mutex_destroy(&doc->swap_mutex);
        
        free(doc);
    }
    free(server->documents);
    
    free(server);
}

// Main server loop
void lsp_server_run(LSPServer* server) {
    fprintf(stderr, "[LSP] JSasta Language Server starting...\n");
    
    while (!server->shutdown_requested) {
        // Check if stdin has data with 100ms timeout
        int has_data = lsp_check_stdin(100);
        
        if (has_data < 0) {
            fprintf(stderr, "[LSP] Error checking stdin, exiting\n");
            break;
        }
        
        // If no data available, continue to next iteration
        if (has_data == 0) {
            continue;
        }
        
        LSPMessage* msg = lsp_read_message();
        
        if (!msg) {
            fprintf(stderr, "[LSP] Failed to read message, exiting\n");
            break;
        }
        
        if (msg->type == LSP_MSG_REQUEST) {
            fprintf(stderr, "[LSP] Request: %s (id=%lld)\n", msg->request.method, (long long)msg->request.id);
            
            char* response_json = NULL;
            
            if (strcmp(msg->request.method, "initialize") == 0) {
                char* result = lsp_handle_initialize(server, msg->request.params);
                response_json = lsp_serialize_response(msg->request.id, result);
                free(result);
            } else if (strcmp(msg->request.method, "shutdown") == 0) {
                char* result = lsp_handle_shutdown(server);
                response_json = lsp_serialize_response(msg->request.id, result);
                free(result);
            } else if (strcmp(msg->request.method, "textDocument/hover") == 0) {
                char* result = lsp_handle_hover(server, msg->request.params);
                response_json = lsp_serialize_response(msg->request.id, result);
                free(result);
            } else if (strcmp(msg->request.method, "textDocument/completion") == 0) {
                char* result = lsp_handle_completion(server, msg->request.params);
                response_json = lsp_serialize_response(msg->request.id, result);
                free(result);
            } else if (strcmp(msg->request.method, "textDocument/definition") == 0) {
                char* result = lsp_handle_definition(server, msg->request.params);
                response_json = lsp_serialize_response(msg->request.id, result);
                free(result);
            } else if (strcmp(msg->request.method, "textDocument/references") == 0) {
                char* result = lsp_handle_references(server, msg->request.params);
                response_json = lsp_serialize_response(msg->request.id, result);
                free(result);
            } else {
                // Method not found
                response_json = lsp_serialize_error(msg->request.id, -32601, "Method not found");
            }
            
            if (response_json) {
                lsp_write_message(response_json);
                free(response_json);
            }
            
        } else if (msg->type == LSP_MSG_NOTIFICATION) {
            fprintf(stderr, "[LSP] Notification: %s\n", msg->notification.method);
            
            if (strcmp(msg->notification.method, "initialized") == 0) {
                lsp_handle_initialized(server);
            } else if (strcmp(msg->notification.method, "exit") == 0) {
                lsp_handle_exit(server);
            } else if (strcmp(msg->notification.method, "textDocument/didOpen") == 0) {
                lsp_handle_did_open(server, msg->notification.params);
            } else if (strcmp(msg->notification.method, "textDocument/didChange") == 0) {
                lsp_handle_did_change(server, msg->notification.params);
            } else if (strcmp(msg->notification.method, "textDocument/didClose") == 0) {
                lsp_handle_did_close(server, msg->notification.params);
            } else if (strcmp(msg->notification.method, "textDocument/didSave") == 0) {
                lsp_handle_did_save(server, msg->notification.params);
            }
        }
        
        lsp_message_free(msg);
    }
    
    fprintf(stderr, "[LSP] JSasta Language Server stopped.\n");
}

// === Document Management ===

LSPDocument* lsp_document_find(LSPServer* server, const char* uri) {
    for (int i = 0; i < server->document_count; i++) {
        if (strcmp(server->documents[i]->uri, uri) == 0) {
            return server->documents[i];
        }
    }
    return NULL;
}

LSPDocument* lsp_document_open(LSPServer* server, const char* uri, const char* language_id, int version, const char* text) {
    (void)language_id; // Unused for now
    
    // Check if already open
    LSPDocument* doc = lsp_document_find(server, uri);
    if (doc) {
        // Update existing document
        free(doc->content);
        doc->content = strdup(text);
        doc->version = version;
        doc->needs_reparse = true;
        return doc;
    }
    
    // Create new document
    doc = malloc(sizeof(LSPDocument));
    doc->uri = strdup(uri);
    doc->filename = lsp_uri_to_path(uri);
    if (!doc->filename) {
        doc->filename = strdup(uri);
    }
    doc->content = strdup(text);
    doc->version = version;
    doc->main_snapshot = NULL;
    pthread_mutex_init(&doc->swap_mutex, NULL);
    doc->needs_reparse = true;
    doc->debounce_timer = NULL;
    doc->timer_active = false;
    doc->server = server;
    
    // Add to server
    if (server->document_count >= server->document_capacity) {
        server->document_capacity *= 2;
        server->documents = realloc(server->documents, sizeof(LSPDocument*) * server->document_capacity);
    }
    server->documents[server->document_count++] = doc;
    
    LSP_LOG("Document opened: %s", uri);
    
    // Parse immediately with full type inference (initial load)
    doc->main_snapshot = lsp_parse_snapshot(text, doc->filename, true);
    LSP_LOG("Initial parse complete");
    
    // Send initial diagnostics
    if (doc->main_snapshot && doc->main_snapshot->diagnostics) {
        DiagnosticContext* ctx = doc->main_snapshot->diagnostics;
        Diagnostic* diag_msg = ctx->head;
        int count = 0;
        
        while (diag_msg) {
            count++;
            diag_msg = diag_msg->next;
        }
        
        if (count > 0) {
            LSPDiagnostic* diagnostics = malloc(sizeof(LSPDiagnostic) * count);
            diag_msg = ctx->head;
            int i = 0;
            
            while (diag_msg) {
                LSPDiagnostic* diag = &diagnostics[i++];
                
                int line = (diag_msg->location.line > 0) ? (int)diag_msg->location.line - 1 : 0;
                int col = (diag_msg->location.column > 0) ? (int)diag_msg->location.column - 1 : 0;
                
                diag->range.start.line = line;
                diag->range.start.character = col;
                diag->range.end.line = line;
                diag->range.end.character = col + 1;
                
                diag->severity = (diag_msg->severity == DIAG_ERROR) ? LSP_DIAGNOSTIC_ERROR : LSP_DIAGNOSTIC_WARNING;
                diag->code = diag_msg->code ? strdup(diag_msg->code) : NULL;
                diag->source = strdup("jsasta");
                diag->message = strdup(diag_msg->message);
                
                diag_msg = diag_msg->next;
            }
            
            char* diag_params = lsp_create_diagnostics_notification(uri, diagnostics, count);
            char* notification = lsp_serialize_notification("textDocument/publishDiagnostics", diag_params);
            lsp_write_message(notification);
            
            for (int i = 0; i < count; i++) {
                free(diagnostics[i].code);
                free(diagnostics[i].source);
                free(diagnostics[i].message);
            }
            free(diagnostics);
            free(diag_params);
            free(notification);
        }
    }
    
    return doc;
}

void lsp_document_update(LSPServer* server, const char* uri, int version, const char* text) {
    LSPDocument* doc = lsp_document_find(server, uri);
    if (!doc) {
        fprintf(stderr, "[LSP] Warning: Trying to update unopened document: %s\n", uri);
        return;
    }
    
    // Just update content - don't parse yet
    free(doc->content);
    doc->content = strdup(text);
    doc->version = version;
    doc->needs_reparse = true;
    
    LSP_LOG("Document updated: %s (version %d) - scheduling worker", uri, version);
    
    // Schedule diagnostic worker (will sleep 300ms, then parse + diagnose)
    lsp_schedule_diagnostic_worker(doc);
}

void lsp_document_close(LSPServer* server, const char* uri) {
    for (int i = 0; i < server->document_count; i++) {
        if (strcmp(server->documents[i]->uri, uri) == 0) {
            LSPDocument* doc = server->documents[i];
            
            free(doc->uri);
            if (doc->filename) free(doc->filename);
            free(doc->content);
            
            // Free snapshot
            pthread_mutex_lock(&doc->swap_mutex);
            if (doc->main_snapshot) {
                parsed_snapshot_free(doc->main_snapshot);
            }
            pthread_mutex_unlock(&doc->swap_mutex);
            pthread_mutex_destroy(&doc->swap_mutex);
            
            free(doc);
            
            // Remove from array
            for (int j = i; j < server->document_count - 1; j++) {
                server->documents[j] = server->documents[j + 1];
            }
            server->document_count--;
            
            return;
        }
    }
}

// (Old parse functions removed - using ParsedSnapshot now)

// Convert diagnostics from compiler to LSP format
void lsp_document_get_diagnostics(ParsedSnapshot* snapshot, LSPDiagnostic** out_diagnostics, int* out_count) {
    LSP_LOG("Getting diagnostics for document");
    
    if (!snapshot || !snapshot->diagnostics) {
        LSP_LOG("No diagnostics context");
        *out_diagnostics = NULL;
        *out_count = 0;
        return;
    }
    
    Diagnostic* diag_msg = snapshot->diagnostics->head;
    int count = 0;
    
    // Count messages
    while (diag_msg) {
        count++;
        diag_msg = diag_msg->next;
    }
    
    LSP_LOG("Found %d diagnostics", count);
    
    if (count == 0) {
        *out_diagnostics = NULL;
        *out_count = 0;
        return;
    }
    
    // Convert to LSP format
    LSPDiagnostic* diagnostics = malloc(sizeof(LSPDiagnostic) * count);
    diag_msg = snapshot->diagnostics->head;
    int i = 0;
    
    while (diag_msg) {
        LSPDiagnostic* diag = &diagnostics[i++];
        
        // Convert to 0-based LSP positions, but handle invalid locations (line/col 0)
        // If location is missing/invalid, default to line 0 (first line)
        int line = (diag_msg->location.line > 0) ? (int)diag_msg->location.line - 1 : 0;
        int col = (diag_msg->location.column > 0) ? (int)diag_msg->location.column - 1 : 0;
        
        LSP_LOG("Diagnostic %d: %s:%d:%d - %s", i-1, 
                diag_msg->location.filename ? diag_msg->location.filename : "null",
                line, col, diag_msg->message);
        
        diag->range.start.line = line;
        diag->range.start.character = col;
        diag->range.end.line = line;
        diag->range.end.character = col + 1; // TODO: better range
        
        diag->severity = (diag_msg->severity == DIAG_ERROR) ? LSP_DIAGNOSTIC_ERROR : LSP_DIAGNOSTIC_WARNING;
        diag->code = diag_msg->code ? strdup(diag_msg->code) : NULL;
        diag->source = strdup("jsasta");
        diag->message = strdup(diag_msg->message);
        
        diag_msg = diag_msg->next;
    }
    
    *out_diagnostics = diagnostics;
    *out_count = count;
}

// === Helper Functions ===

char* lsp_uri_to_path(const char* uri) {
    if (!uri) return NULL;
    
    // Simple file:// URI parsing
    if (strncmp(uri, "file://", 7) == 0) {
        return strdup(uri + 7);
    }
    
    return strdup(uri);
}

char* lsp_path_to_uri(const char* path) {
    if (!path) return NULL;
    
    size_t len = strlen(path) + 8; // "file://" + path + null
    char* uri = malloc(len);
    snprintf(uri, len, "file://%s", path);
    return uri;
}

// Find document by URI
LSPDocument* lsp_server_find_document(LSPServer* server, const char* uri) {
    for (int i = 0; i < server->document_count; i++) {
        if (strcmp(server->documents[i]->uri, uri) == 0) {
            return server->documents[i];
        }
    }
    return NULL;
}

// Helper to recursively find identifier at position
static ASTNode* find_identifier_recursive(ASTNode* node, int line, int character) {
    if (!node) return NULL;
    
    // Check if this node is an identifier at the requested position
    if (node->type == AST_IDENTIFIER && node->loc.line > 0) {
        int node_line = (int)node->loc.line - 1;  // Convert to 0-based
        int node_col = (int)node->loc.column - 1;
        int name_len = (int)strlen(node->identifier.name);
        
        // Check if position is within this identifier
        if (node_line == line && character >= node_col && character < node_col + name_len) {
            return node;
        }
    }
    
    // Recursively search child nodes based on node type
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->program.count; i++) {
                ASTNode* result = find_identifier_recursive(node->program.statements[i], line, character);
                if (result) return result;
            }
            break;
            
        case AST_FUNCTION_DECL:
            return find_identifier_recursive(node->func_decl.body, line, character);
            
        case AST_VAR_DECL:
            return find_identifier_recursive(node->var_decl.init, line, character);
            
        case AST_RETURN:
            return find_identifier_recursive(node->return_stmt.value, line, character);
            
        case AST_IF:
            {
                ASTNode* result = find_identifier_recursive(node->if_stmt.condition, line, character);
                if (result) return result;
                result = find_identifier_recursive(node->if_stmt.then_branch, line, character);
                if (result) return result;
                return find_identifier_recursive(node->if_stmt.else_branch, line, character);
            }
            
        case AST_WHILE:
            {
                ASTNode* result = find_identifier_recursive(node->while_stmt.condition, line, character);
                if (result) return result;
                return find_identifier_recursive(node->while_stmt.body, line, character);
            }
            
        case AST_FOR:
            {
                ASTNode* result = find_identifier_recursive(node->for_stmt.init, line, character);
                if (result) return result;
                result = find_identifier_recursive(node->for_stmt.condition, line, character);
                if (result) return result;
                result = find_identifier_recursive(node->for_stmt.update, line, character);
                if (result) return result;
                return find_identifier_recursive(node->for_stmt.body, line, character);
            }
            
        case AST_EXPR_STMT:
            return find_identifier_recursive(node->expr_stmt.expression, line, character);
            
        case AST_BINARY_OP:
            {
                ASTNode* result = find_identifier_recursive(node->binary_op.left, line, character);
                if (result) return result;
                return find_identifier_recursive(node->binary_op.right, line, character);
            }
            
        case AST_UNARY_OP:
            return find_identifier_recursive(node->unary_op.operand, line, character);
            
        case AST_CALL:
            {
                ASTNode* result = find_identifier_recursive(node->call.callee, line, character);
                if (result) return result;
                for (int i = 0; i < node->call.arg_count; i++) {
                    result = find_identifier_recursive(node->call.args[i], line, character);
                    if (result) return result;
                }
            }
            break;
            
        case AST_ASSIGNMENT:
            return find_identifier_recursive(node->assignment.value, line, character);
            
        case AST_MEMBER_ACCESS:
            return find_identifier_recursive(node->member_access.object, line, character);
            
        case AST_INDEX_ACCESS:
            {
                ASTNode* result = find_identifier_recursive(node->index_access.object, line, character);
                if (result) return result;
                return find_identifier_recursive(node->index_access.index, line, character);
            }
            
        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->array_literal.count; i++) {
                ASTNode* result = find_identifier_recursive(node->array_literal.elements[i], line, character);
                if (result) return result;
            }
            break;
            
        default:
            // For other node types, no recursion needed
            break;
    }
    
    return NULL;
}

// Find identifier at position (line and character are 0-based)
ASTNode* lsp_find_identifier_at_position(ASTNode* ast, int line, int character) {
    return find_identifier_recursive(ast, line, character);
}

SymbolEntry* lsp_find_symbol_at_position(LSPDocument* doc, LSPPosition pos) {
    // TODO: Implement proper position-based symbol lookup
    (void)doc;
    (void)pos;
    return NULL;
}

ASTNode* lsp_find_node_at_position(ASTNode* ast, LSPPosition pos) {
    // TODO: Implement proper position-based AST node lookup
    (void)ast;
    (void)pos;
    return NULL;
}

char* lsp_get_hover_info(ASTNode* node, SymbolEntry* symbol) {
    // TODO: Generate hover information from node/symbol
    if (symbol && symbol->type_info) {
        char* info = malloc(256);
        snprintf(info, 256, "**%s**\n\nType: `%s`", 
                 symbol->name, 
                 symbol->type_info->type_name ? symbol->type_info->type_name : "unknown");
        return info;
    }
    
    if (node && node->type_info) {
        char* info = malloc(256);
        snprintf(info, 256, "Type: `%s`", 
                 node->type_info->type_name ? node->type_info->type_name : "unknown");
        return info;
    }
    
    return NULL;
}
