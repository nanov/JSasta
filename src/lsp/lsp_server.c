#include "../common/jsasta_compiler.h"
#include "../common/logger.h"
#include "../common/diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// TODO: Add json-c for JSON-RPC protocol
// For now, just a minimal stub that can be compiled

typedef struct {
    char* uri;
    char* content;
    ASTNode* ast;
    SymbolTable* symbols;
    DiagnosticContext* diagnostics;
} Document;

typedef struct {
    Document** documents;
    int document_count;
    int document_capacity;
    TypeContext* type_ctx;
} LSPServer;

LSPServer* lsp_server_create(void) {
    LSPServer* server = malloc(sizeof(LSPServer));
    server->documents = malloc(sizeof(Document*) * 10);
    server->document_count = 0;
    server->document_capacity = 10;
    server->type_ctx = type_context_create();
    return server;
}

void lsp_server_free(LSPServer* server) {
    for (int i = 0; i < server->document_count; i++) {
        Document* doc = server->documents[i];
        free(doc->uri);
        free(doc->content);
        if (doc->ast) ast_free(doc->ast);
        if (doc->diagnostics) diagnostic_context_free(doc->diagnostics);
        free(doc);
    }
    free(server->documents);
    type_context_free(server->type_ctx);
    free(server);
}

void lsp_server_run(LSPServer* server) {
    (void)server;
    
    // TODO: Implement JSON-RPC protocol
    // For now, just print a message
    fprintf(stderr, "JSasta LSP Server starting...\n");
    fprintf(stderr, "LSP protocol implementation is pending.\n");
    fprintf(stderr, "This requires json-c library for JSON-RPC.\n");
    
    // Keep server alive for testing
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        if (strncmp(buffer, "exit", 4) == 0) {
            break;
        }
    }
}

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options]\n", program_name);
    fprintf(stderr, "\nJSasta Language Server (LSP Daemon)\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --stdio        Use stdio for communication (default)\n");
    fprintf(stderr, "  --socket=PORT  Use socket on PORT for communication\n");
    fprintf(stderr, "  -h, --help     Show this help message\n");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    // Initialize logger to error-only for LSP
    logger_init(LOG_ERROR);
    
    log_info("JSasta Language Server starting...");
    
    LSPServer* server = lsp_server_create();
    lsp_server_run(server);
    lsp_server_free(server);
    
    log_info("JSasta Language Server stopped.");
    
    return 0;
}
