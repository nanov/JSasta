#include "lsp_protocol.h"
#include "lsp_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>

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

// Check if stdin has data available (with timeout)
// Returns: 1 if data available, 0 if timeout, -1 if error
int lsp_check_stdin(int timeout_ms) {
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    
    if (result < 0 && errno != EINTR) {
        LSP_LOG("select() error: %d", errno);
        return -1;
    }
    
    return result > 0 ? 1 : 0;
}

// Read a Content-Length header and message from stdin
LSPMessage* lsp_read_message(void) {
    char header[256];
    int content_length = -1;
    
    LSP_LOG("Reading LSP message...");
    
    // Read headers
    while (fgets(header, sizeof(header), stdin)) {
        LSP_LOG("Read header: '%s'", header);
        
        // Check for Content-Length
        if (strncmp(header, "Content-Length:", 15) == 0) {
            content_length = atoi(header + 15);
            LSP_LOG("Content-Length: %d", content_length);
        }
        
        // Empty line marks end of headers
        if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) {
            LSP_LOG("End of headers");
            break;
        }
    }
    
    if (content_length < 0) {
        LSP_LOG("No Content-Length found");
        return NULL;
    }
    
    // Read content
    char* content = malloc(content_length + 1);
    size_t read = fread(content, 1, content_length, stdin);
    content[read] = '\0';
    
    LSP_LOG("Read %zu bytes (expected %d)", read, content_length);
    LSP_LOG("Content: %s", content);
    
    if (read != (size_t)content_length) {
        LSP_LOG("Read size mismatch");
        free(content);
        return NULL;
    }
    
    // Parse JSON
    LSPMessage* msg = lsp_parse_message(content);
    free(content);
    
    return msg;
}

// Write a message to stdout with Content-Length header
void lsp_write_message(const char* json_content) {
    FILE* log = fopen("/tmp/jsasta_lsp.log", "a");
    if (log) {
        fprintf(log, "[WRITE] About to write %zu bytes to stdout:\n%s\n", strlen(json_content), json_content);
        fflush(log);
        fclose(log);
    }
    
    size_t len = strlen(json_content);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", len, json_content);
    fflush(stdout);
    
    log = fopen("/tmp/jsasta_lsp.log", "a");
    if (log) {
        fprintf(log, "[WRITE] Wrote and flushed to stdout\n");
        fflush(log);
        fclose(log);
    }
}

// Parse JSON-RPC message
LSPMessage* lsp_parse_message(const char* json) {
    JSONValue* root = json_parse(json);
    if (!root || root->type != JSON_OBJECT) {
        if (root) json_value_free(root);
        return NULL;
    }
    
    LSPMessage* msg = malloc(sizeof(LSPMessage));
    memset(msg, 0, sizeof(LSPMessage));
    
    // Check jsonrpc version
    JSONValue* jsonrpc = json_object_get(root, "jsonrpc");
    if (!jsonrpc || strcmp(json_get_string(jsonrpc), "2.0") != 0) {
        json_value_free(root);
        free(msg);
        return NULL;
    }
    msg->jsonrpc = strdup("2.0");
    
    // Determine message type
    JSONValue* id = json_object_get(root, "id");
    JSONValue* method = json_object_get(root, "method");
    JSONValue* result = json_object_get(root, "result");
    JSONValue* error = json_object_get(root, "error");
    
    if (method && id) {
        // Request
        msg->type = LSP_MSG_REQUEST;
        msg->request.id = json_get_number(id);
        msg->request.method = strdup(json_get_string(method));
        
        JSONValue* params = json_object_get(root, "params");
        if (params) {
            // Serialize just the params object
            msg->request.params = json_value_to_string(params);
        } else {
            msg->request.params = NULL;
        }
    } else if (method && !id) {
        // Notification
        msg->type = LSP_MSG_NOTIFICATION;
        msg->notification.method = strdup(json_get_string(method));
        
        JSONValue* params = json_object_get(root, "params");
        if (params) {
            // Serialize just the params object
            msg->notification.params = json_value_to_string(params);
        } else {
            msg->notification.params = NULL;
        }
    } else if (id && (result || error)) {
        // Response
        msg->type = LSP_MSG_RESPONSE;
        msg->response.id = json_get_number(id);
        
        if (result) {
            msg->response.result = strdup(json); // FIXME: extract just result
            msg->response.error = NULL;
        } else {
            msg->response.result = NULL;
            msg->response.error = strdup(json); // FIXME: extract just error
        }
    } else {
        json_value_free(root);
        free(msg->jsonrpc);
        free(msg);
        return NULL;
    }
    
    json_value_free(root);
    return msg;
}

void lsp_message_free(LSPMessage* msg) {
    if (!msg) return;
    
    free(msg->jsonrpc);
    
    switch (msg->type) {
        case LSP_MSG_REQUEST:
            free(msg->request.method);
            free(msg->request.params);
            break;
        case LSP_MSG_RESPONSE:
            free(msg->response.result);
            free(msg->response.error);
            break;
        case LSP_MSG_NOTIFICATION:
            free(msg->notification.method);
            free(msg->notification.params);
            break;
    }
    
    free(msg);
}

// Serialize response to JSON
char* lsp_serialize_response(int64_t id, const char* result) {
    JSONBuilder* builder = json_builder_create();
    
    json_start_object(builder);
    json_add_string_field(builder, "jsonrpc", "2.0");
    json_add_number_field(builder, "id", id);
    json_add_raw_field(builder, "result", result);
    json_end_object(builder);
    
    char* json = json_builder_to_string(builder);
    json_builder_free(builder);
    return json;
}

// Serialize error response
char* lsp_serialize_error(int64_t id, int code, const char* message) {
    JSONBuilder* builder = json_builder_create();
    
    json_start_object(builder);
    json_add_string_field(builder, "jsonrpc", "2.0");
    json_add_number_field(builder, "id", id);
    
    json_add_key(builder, "error");
    json_start_object(builder);
    json_add_number_field(builder, "code", code);
    json_add_string_field(builder, "message", message);
    json_end_object(builder);
    
    json_end_object(builder);
    
    char* json = json_builder_to_string(builder);
    json_builder_free(builder);
    return json;
}

// Serialize notification
char* lsp_serialize_notification(const char* method, const char* params) {
    JSONBuilder* builder = json_builder_create();
    
    json_start_object(builder);
    json_add_string_field(builder, "jsonrpc", "2.0");
    json_add_string_field(builder, "method", method);
    if (params) {
        json_add_raw_field(builder, "params", params);
    }
    json_end_object(builder);
    
    char* json = json_builder_to_string(builder);
    json_builder_free(builder);
    return json;
}

// Create initialize response
char* lsp_create_initialize_response(LSPServerCapabilities* caps) {
    JSONBuilder* builder = json_builder_create();
    
    json_start_object(builder);
    
    // capabilities
    json_add_key(builder, "capabilities");
    json_start_object(builder);
    
    json_add_number_field(builder, "textDocumentSync", 1); // Full sync
    
    if (caps->hover_provider) {
        json_add_bool_field(builder, "hoverProvider", true);
    }
    
    if (caps->completion_provider) {
        json_add_key(builder, "completionProvider");
        json_start_object(builder);
        json_add_key(builder, "triggerCharacters");
        json_start_array(builder);
        json_add_string(builder, ".");
        json_end_array(builder);
        json_end_object(builder);
    }
    
    if (caps->definition_provider) {
        json_add_bool_field(builder, "definitionProvider", true);
    }
    
    if (caps->references_provider) {
        json_add_bool_field(builder, "referencesProvider", true);
    }
    
    if (caps->document_symbol_provider) {
        json_add_bool_field(builder, "documentSymbolProvider", true);
    }
    
    json_end_object(builder); // capabilities
    
    // serverInfo
    json_add_key(builder, "serverInfo");
    json_start_object(builder);
    json_add_string_field(builder, "name", "jsasta-lsp");
    json_add_string_field(builder, "version", "1.0.0");
    json_end_object(builder);
    
    json_end_object(builder);
    
    char* result = json_builder_to_string(builder);
    json_builder_free(builder);
    return result;
}

// Create diagnostics notification
char* lsp_create_diagnostics_notification(const char* uri, LSPDiagnostic* diagnostics, int count) {
    JSONBuilder* builder = json_builder_create();
    
    json_start_object(builder);
    json_add_string_field(builder, "uri", uri);
    
    json_add_key(builder, "diagnostics");
    json_start_array(builder);
    
    for (int i = 0; i < count; i++) {
        LSPDiagnostic* diag = &diagnostics[i];
        
        json_start_object(builder);
        
        // range
        json_add_key(builder, "range");
        json_start_object(builder);
        json_add_key(builder, "start");
        json_start_object(builder);
        json_add_number_field(builder, "line", diag->range.start.line);
        json_add_number_field(builder, "character", diag->range.start.character);
        json_end_object(builder);
        json_add_key(builder, "end");
        json_start_object(builder);
        json_add_number_field(builder, "line", diag->range.end.line);
        json_add_number_field(builder, "character", diag->range.end.character);
        json_end_object(builder);
        json_end_object(builder);
        
        json_add_number_field(builder, "severity", diag->severity);
        if (diag->code) {
            json_add_string_field(builder, "code", diag->code);
        }
        if (diag->source) {
            json_add_string_field(builder, "source", diag->source);
        }
        json_add_string_field(builder, "message", diag->message);
        
        json_end_object(builder);
    }
    
    json_end_array(builder);
    json_end_object(builder);
    
    char* params = json_builder_to_string(builder);
    json_builder_free(builder);
    return params;
}

// Create hover response
char* lsp_create_hover_response(LSPHover* hover) {
    if (!hover || !hover->contents) {
        return strdup("null");
    }
    
    JSONBuilder* builder = json_builder_create();
    
    json_start_object(builder);
    
    // contents
    json_add_key(builder, "contents");
    json_start_object(builder);
    json_add_string_field(builder, "kind", "markdown");
    json_add_string_field(builder, "value", hover->contents);
    json_end_object(builder);
    
    // range (optional)
    if (hover->range) {
        json_add_key(builder, "range");
        json_start_object(builder);
        json_add_key(builder, "start");
        json_start_object(builder);
        json_add_number_field(builder, "line", hover->range->start.line);
        json_add_number_field(builder, "character", hover->range->start.character);
        json_end_object(builder);
        json_add_key(builder, "end");
        json_start_object(builder);
        json_add_number_field(builder, "line", hover->range->end.line);
        json_add_number_field(builder, "character", hover->range->end.character);
        json_end_object(builder);
        json_end_object(builder);
    }
    
    json_end_object(builder);
    
    char* result = json_builder_to_string(builder);
    json_builder_free(builder);
    return result;
}

// Create completion response
char* lsp_create_completion_response(LSPCompletionItem* items, int count) {
    JSONBuilder* builder = json_builder_create();
    
    json_start_array(builder);
    
    for (int i = 0; i < count; i++) {
        LSPCompletionItem* item = &items[i];
        
        json_start_object(builder);
        json_add_string_field(builder, "label", item->label);
        json_add_number_field(builder, "kind", item->kind);
        
        if (item->detail) {
            json_add_string_field(builder, "detail", item->detail);
        }
        if (item->documentation) {
            json_add_string_field(builder, "documentation", item->documentation);
        }
        if (item->insert_text) {
            json_add_string_field(builder, "insertText", item->insert_text);
        }
        
        json_end_object(builder);
    }
    
    json_end_array(builder);
    
    char* result = json_builder_to_string(builder);
    json_builder_free(builder);
    return result;
}

// Create locations response
char* lsp_create_locations_response(LSPLocation* locations, int count) {
    if (count == 0) {
        return strdup("null");
    }
    
    JSONBuilder* builder = json_builder_create();
    
    if (count == 1) {
        // Single location
        LSPLocation* loc = &locations[0];
        
        json_start_object(builder);
        json_add_string_field(builder, "uri", loc->uri);
        
        json_add_key(builder, "range");
        json_start_object(builder);
        json_add_key(builder, "start");
        json_start_object(builder);
        json_add_number_field(builder, "line", loc->range.start.line);
        json_add_number_field(builder, "character", loc->range.start.character);
        json_end_object(builder);
        json_add_key(builder, "end");
        json_start_object(builder);
        json_add_number_field(builder, "line", loc->range.end.line);
        json_add_number_field(builder, "character", loc->range.end.character);
        json_end_object(builder);
        json_end_object(builder);
        
        json_end_object(builder);
    } else {
        // Array of locations
        json_start_array(builder);
        
        for (int i = 0; i < count; i++) {
            LSPLocation* loc = &locations[i];
            
            json_start_object(builder);
            json_add_string_field(builder, "uri", loc->uri);
            
            json_add_key(builder, "range");
            json_start_object(builder);
            json_add_key(builder, "start");
            json_start_object(builder);
            json_add_number_field(builder, "line", loc->range.start.line);
            json_add_number_field(builder, "character", loc->range.start.character);
            json_end_object(builder);
            json_add_key(builder, "end");
            json_start_object(builder);
            json_add_number_field(builder, "line", loc->range.end.line);
            json_add_number_field(builder, "character", loc->range.end.character);
            json_end_object(builder);
            json_end_object(builder);
            
            json_end_object(builder);
        }
        
        json_end_array(builder);
    }
    
    char* result = json_builder_to_string(builder);
    json_builder_free(builder);
    return result;
}
