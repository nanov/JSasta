#include "lsp_server.h"
#include "lsp_protocol.h"
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
static void* diagnostic_worker_thread(void* arg);
static void lsp_document_reparse(LSPServer* server, LSPDocument* doc, const char* uri);

// === AnalysisWork Functions ===

void analysis_work_free(AnalysisWork* work) {
    if (!work) return;

    free(work->uri);
    free(work->filename);
    if (work->ast) ast_free(work->ast);
    // Note: Don't free work->symbols here - it's owned by the AST and freed by ast_free()
    if (work->type_ctx) type_context_free(work->type_ctx);
    if (work->diagnostics) diagnostic_context_free(work->diagnostics);

    free(work);
}

// Parse content into analysis work (WITHOUT running type inference)
// Type inference should be run separately by the worker thread
// Code index is NOT built here - caller should build it separately if needed
AnalysisWork* analysis_work_parse(const char* content, const char* uri, const char* filename) {
    AnalysisWork* work = calloc(1, sizeof(AnalysisWork));

    work->uri = strdup(uri);
    work->filename = strdup(filename);
    work->type_ctx = type_context_create();
    work->diagnostics = diagnostic_context_create_with_mode(DIAG_MODE_COLLECT, NULL);
    work->symbols = symbol_table_create(NULL);

    LSP_LOG("Parsing: %s", filename);

    // Reset logger error count before parsing (important for multi-file LSP)
    // The logger has global state that accumulates errors, but parser_parse() checks
    // logger_has_errors() and returns NULL if true. We need to reset it for each document.
    logger_reset_error_count();

    // Parse
    Parser* parser = parser_create(content, filename, work->type_ctx, work->diagnostics);
    work->ast = parser_parse(parser);
    parser_free(parser);

    if (!work->ast) {
        LSP_LOG("Parse failed - no AST");
    }

    return work;
}

// === Document Parsing Helper ===

// Parse document content and queue for type inference
// This is the common logic used by both document_open and document_update
static void lsp_document_reparse(LSPServer* server, LSPDocument* doc, const char* uri) {
    // Get content as C string (no copy, just pointer)
    const char* content_str = jsa_string_builder_cstr(doc->content);

    // Parse once to create analysis work
    AnalysisWork* work = analysis_work_parse(content_str, uri, doc->filename);

    if (!work) {
        LSP_LOG("Failed to create analysis work for %s", uri);
        return;
    }

    // Build code index for LSP features (only needed in main thread)
    if (work->ast) {
        CodeIndex* old_index = doc->code_index;
        doc->code_index = code_index_create();
        code_index_build(doc->code_index, work->ast, work->symbols);
        LSP_LOG("Code index %s with %d positions",
                old_index ? "rebuilt" : "built",
                doc->code_index->position_count);

        if (old_index) {
            code_index_free(old_index);
        }
    }

    // Queue work for type inference worker on this document
    pthread_mutex_lock(&server->work_mutex);
    if (doc->pending_work) {
        analysis_work_free(doc->pending_work);
    }
    doc->pending_work = work;
    pthread_cond_signal(&server->work_available);  // Wake up worker thread
    pthread_mutex_unlock(&server->work_mutex);

    LSP_LOG("Type inference work queued for %s", uri);
}

// === Type Inference Worker (Background Thread) ===

// Persistent worker thread - runs in infinite loop
static void* diagnostic_worker_thread(void* arg) {
    LSPServer* server = (LSPServer*)arg;

    LSP_LOG("Type inference worker thread starting");

    while (server->worker_running) {
        AnalysisWork* work = NULL;
        LSPDocument* work_doc = NULL;

        pthread_mutex_lock(&server->work_mutex);
        while (server->worker_running && !work) {
            // Scan all documents for pending work
            for (int i = 0; i < server->document_count; i++) {
                if (server->documents[i]->pending_work) {
                    work_doc = server->documents[i];
                    work = work_doc->pending_work;
                    work_doc->pending_work = NULL;  // Take ownership
                    break;
                }
            }

            if (!work && server->worker_running) {
                pthread_cond_wait(&server->work_available, &server->work_mutex);
            }
        }
        pthread_mutex_unlock(&server->work_mutex);

        if (!work) {
            continue;
        }

        LSP_LOG("Worker processing type inference for %s", work->uri);

        // Run type inference on the work item
        type_inference_with_diagnostics(work->ast, work->symbols, work->type_ctx, work->diagnostics);

        LSP_LOG("Type inference complete, errors: %d, warnings: %d",
                work->diagnostics->error_count,
                work->diagnostics->warning_count);

        // Extract and send diagnostics
        LSPDiagnostic* diagnostics = NULL;
        int count = 0;

        if (work->diagnostics) {
            Diagnostic* diag_msg = work->diagnostics->head;

            while (diag_msg) {
                count++;
                diag_msg = diag_msg->next;
            }

            if (count > 0) {
                diagnostics = malloc(sizeof(LSPDiagnostic) * count);
                diag_msg = work->diagnostics->head;
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
            }
        }

        LSP_LOG("Sending %d type inference diagnostics for %s", count, work->uri);
        char* diag_params = lsp_create_diagnostics_notification(work->uri, diagnostics, count);
        char* notification = lsp_serialize_notification("textDocument/publishDiagnostics", diag_params);
        lsp_write_message(notification);

        if (count > 0 && diagnostics) {
            for (int i = 0; i < count; i++) {
                free(diagnostics[i].code);
                free(diagnostics[i].source);
                free(diagnostics[i].message);
            }
            free(diagnostics);
        }
        free(diag_params);
        free(notification);

        // Store completed work for code index rebuild (with type information)
        // Use atomic_exchange to safely pass the work to main thread
        AnalysisWork* old_work = atomic_exchange(&work_doc->completed_work, work);
        if (old_work) {
            // Free any previous completed work that wasn't consumed yet
            analysis_work_free(old_work);
            LSP_LOG("Replaced unconsume completed work for %s", work->uri);
        }
        
        LSP_LOG("Worker finished processing, stored completed work for code index rebuild");
    }

    LSP_LOG("Type inference worker thread exiting");
    return NULL;
}

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

    // Initialize work queue for type inference worker
    pthread_mutex_init(&server->work_mutex, NULL);
    pthread_cond_init(&server->work_available, NULL);
    server->worker_running = true;

    // Start persistent worker thread
    pthread_create(&server->worker_thread, NULL, diagnostic_worker_thread, server);

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

    // Stop worker thread
    pthread_mutex_lock(&server->work_mutex);
    server->worker_running = false;
    pthread_cond_signal(&server->work_available);  // Wake up worker so it can exit
    pthread_mutex_unlock(&server->work_mutex);

    pthread_join(server->worker_thread, NULL);  // Wait for worker to finish

    // Free any pending work from all documents
    pthread_mutex_lock(&server->work_mutex);
    for (int i = 0; i < server->document_count; i++) {
        if (server->documents[i]->pending_work) {
            analysis_work_free(server->documents[i]->pending_work);
            server->documents[i]->pending_work = NULL;
        }
        // Also free any completed work
        AnalysisWork* completed = atomic_load(&server->documents[i]->completed_work);
        if (completed) {
            analysis_work_free(completed);
        }
    }
    pthread_mutex_unlock(&server->work_mutex);

    pthread_cond_destroy(&server->work_available);
    pthread_mutex_destroy(&server->work_mutex);

    free(server->root_uri);
    free(server->client_name);

    for (int i = 0; i < server->document_count; i++) {
        LSPDocument* doc = server->documents[i];

        free(doc->uri);
        free(doc->filename);
        free(doc->content);

        // Free code index
        if (doc->code_index) {
            code_index_free(doc->code_index);
        }

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

CodeIndex* lsp_document_get_code_index(LSPDocument* doc) {
    if (!doc) return NULL;
    
    // Check if type inference completed and we need to rebuild code index
    AnalysisWork* completed = atomic_exchange(&doc->completed_work, NULL);
    if (completed) {
        LSP_LOG("Rebuilding code index with type information");
        
        // Rebuild code index with typed AST
        CodeIndex* old_index = doc->code_index;
        doc->code_index = code_index_create();
        code_index_build(doc->code_index, completed->ast, completed->symbols);
        
        if (old_index) {
            code_index_free(old_index);
        }
        
        analysis_work_free(completed);
        LSP_LOG("Code index rebuilt with %d positions", doc->code_index->position_count);
    }
    
    return doc->code_index;
}

LSPDocument* lsp_document_open(LSPServer* server, const char* uri, const char* language_id, int version, const char* text) {
    (void)language_id; // Unused for now

    // Check if already open
    LSPDocument* doc = lsp_document_find(server, uri);
    if (doc) {
        // Update existing document
        jsa_string_builder_free(doc->content);
        doc->content = jsa_string_builder_from_string(text);
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
    doc->content = jsa_string_builder_from_string(text);
    doc->version = version;
    doc->code_index = NULL;
    doc->pending_work = NULL;
    atomic_init(&doc->completed_work, NULL);
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

    // Parse and queue for type inference
    // Don't send parse diagnostics - wait for type inference to avoid jiggle
    lsp_document_reparse(server, doc, uri);

    return doc;
}

void lsp_document_update(LSPServer* server, const char* uri, int version, const TextRange* range, const char* text) {
    LSPDocument* doc = lsp_document_find(server, uri);
    if (!doc) {
        fprintf(stderr, "[LSP] Warning: Trying to update unopened document: %s\n", uri);
        return;
    }

    // Update content
    if (range) {
        // Incremental update - apply edit to existing content
        LSP_LOG("Incremental update: %s (version %d) range [%zu:%zu - %zu:%zu]",
                uri, version, range->start.line, range->start.character,
                range->end.line, range->end.character);
        jsa_string_builder_apply_edit(doc->content, range, text);
    } else {
        // Full sync - replace entire content
        LSP_LOG("Full sync update: %s (version %d)", uri, version);
        jsa_string_builder_clear(doc->content);
        jsa_string_builder_append(doc->content, text);
    }

    doc->version = version;

    // Parse and queue for type inference
    lsp_document_reparse(server, doc, uri);
}

void lsp_document_close(LSPServer* server, const char* uri) {
    for (int i = 0; i < server->document_count; i++) {
        if (strcmp(server->documents[i]->uri, uri) == 0) {
            LSPDocument* doc = server->documents[i];

            free(doc->uri);
            if (doc->filename) free(doc->filename);
            jsa_string_builder_free(doc->content);

            // Free code index
            if (doc->code_index) {
                code_index_free(doc->code_index);
            }

            // Free pending work
            pthread_mutex_lock(&server->work_mutex);
            if (doc->pending_work) {
                analysis_work_free(doc->pending_work);
            }
            pthread_mutex_unlock(&server->work_mutex);

            // Free any unconsumed completed work
            AnalysisWork* completed = atomic_load(&doc->completed_work);
            if (completed) {
                analysis_work_free(completed);
            }

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
void lsp_document_get_diagnostics(AnalysisWork* work, LSPDiagnostic** out_diagnostics, int* out_count) {
    LSP_LOG("Getting diagnostics for document");

    if (!work || !work->diagnostics) {
        LSP_LOG("No diagnostics context");
        *out_diagnostics = NULL;
        *out_count = 0;
        return;
    }

    Diagnostic* diag_msg = work->diagnostics->head;
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
    diag_msg = work->diagnostics->head;
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
