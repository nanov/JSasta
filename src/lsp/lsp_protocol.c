#include "lsp_protocol.h"
#include "tmp_protocol.h"
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


// --- Forward Declarations of Static Callbacks ---
static JSONParserObjectCallback lsp_json_message_parser_callback;
static int lsp_json_params_parser_dispatcher(JSONParser* parser, LspJsonMessage* message);
// Add other forward declarations if your compiler needs them

// --- Helper to map method string to enum ---
static LspJsonMethodType lsp_json_method_to_enum(const char* method) {
    if (!method) return LSP_METHOD_UNKNOWN;
    if (strcmp(method, "initialize") == 0) return LSP_METHOD_INITIALIZE;
    if (strcmp(method, "initialized") == 0) return LSP_METHOD_INITIALIZED;
    if (strcmp(method, "shutdown") == 0) return LSP_METHOD_SHUTDOWN;
    if (strcmp(method, "exit") == 0) return LSP_METHOD_EXIT;
    if (strcmp(method, "textDocument/didOpen") == 0) return LSP_METHOD_TEXTDOCUMENT_DID_OPEN;
    if (strcmp(method, "textDocument/didChange") == 0) return LSP_METHOD_TEXTDOCUMENT_DID_CHANGE;
    if (strcmp(method, "textDocument/didClose") == 0) return LSP_METHOD_TEXTDOCUMENT_DID_CLOSE;
    if (strcmp(method, "textDocument/didSave") == 0) return LSP_METHOD_TEXTDOCUMENT_DID_SAVE;
    if (strcmp(method, "textDocument/hover") == 0) return LSP_METHOD_TEXTDOCUMENT_HOVER;
    if (strcmp(method, "textDocument/completion") == 0) return LSP_METHOD_TEXTDOCUMENT_COMPLETION;
    if (strcmp(method, "textDocument/definition") == 0) return LSP_METHOD_TEXTDOCUMENT_DEFINITION;
    if (strcmp(method, "textDocument/references") == 0) return LSP_METHOD_TEXTDOCUMENT_REFERENCES;
    if (strcmp(method, "textDocument/inlayHint") == 0) return LSP_METHOD_TEXTDOCUMENT_INLAY_HINT;
    if (strcmp(method, "workspace/didChangeConfiguration") == 0) return LSP_METHOD_WORKSPACE_DID_CHANGE_CONFIGURATION;
    if (strcmp(method, "$/cancelRequest") == 0) return LSP_METHOD_CANCEL_REQUEST;
    return LSP_METHOD_UNKNOWN;
}

// --- Bottom-Up Parser Implementations ---

static int lsp_json_position_parser_callback(JSONParser* p, const char* key, void* user_data) {
    TextPosition* pos = (TextPosition*)user_data;
    if (strcmp(key, "line") == 0) {
        int64_t val; json_get_fast_integer(p, &val); pos->line = (uint32_t)val; return 0;
    }
    if (strcmp(key, "character") == 0) {
        int64_t val; json_get_fast_integer(p, &val); pos->character = (uint32_t)val; return 0;
    }
    return -1;
}

static int lsp_json_range_parser_callback(JSONParser* p, const char* key, void* user_data) {
    TextRange* range = (TextRange*)user_data;
    if (strcmp(key, "start") == 0) return json_parse_fast_object(p, lsp_json_position_parser_callback, &range->start);
    if (strcmp(key, "end") == 0) return json_parse_fast_object(p, lsp_json_position_parser_callback, &range->end);
    return -1;
}

static int lsp_json_text_document_identifier_parser_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonTextDocumentIdentifier* doc = (LspJsonTextDocumentIdentifier*)user_data;
    if (strcmp(key, "uri") == 0) return json_get_fast_string(p, &doc->uri);
    return -1;
}

static int lsp_json_versioned_text_document_identifier_parser_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonVersionedTextDocumentIdentifier* doc = (LspJsonVersionedTextDocumentIdentifier*)user_data;
    if (strcmp(key, "uri") == 0) return json_get_fast_string(p, &doc->uri);
    if (strcmp(key, "version") == 0) {
        int64_t val; json_get_fast_integer(p, &val); doc->version = (int)val; return 0;
    }
    return -1;
}

static int lsp_json_text_document_item_parser_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonTextDocumentItem* item = (LspJsonTextDocumentItem*)user_data;
    if (strcmp(key, "uri") == 0) return json_get_fast_string(p, &item->uri);
    if (strcmp(key, "languageId") == 0) return json_get_fast_string(p, &item->languageId);
    if (strcmp(key, "version") == 0) {
        int64_t val; json_get_fast_integer(p, &val); item->version = (int)val; return 0;
    }
    if (strcmp(key, "text") == 0) return json_get_fast_string(p, &item->text);
    return -1;
}

// --- Method-Specific Params Parsers ---

static int lsp_json_initialize_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonInitializeParams* params = (LspJsonInitializeParams*)user_data;
    if (strcmp(key, "processId") == 0) return json_get_fast_integer(p, &params->processId);
    if (strcmp(key, "rootUri") == 0) return json_get_fast_string(p, &params->rootUri);
    // Skip capabilities, etc. for this example
    return -1;
}

static int lsp_json_did_open_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonDidOpenTextDocumentParams* params = (LspJsonDidOpenTextDocumentParams*)user_data;
    if (strcmp(key, "textDocument") == 0) {
        return json_parse_fast_object(p, lsp_json_text_document_item_parser_callback, &params->textDocument);
    }
    return -1;
}

static int lsp_json_content_change_object_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonTextDocumentContentChangeEvent* change = (LspJsonTextDocumentContentChangeEvent*)user_data;
    if (strcmp(key, "text") == 0) return json_get_fast_string(p, &change->text);
    if (strcmp(key, "range") == 0) {
        change->has_range = true; // = calloc(1, sizeof(LspJsonRange));
        return json_parse_fast_object(p, lsp_json_range_parser_callback, &change->range);
    }
    return -1;
}

static int lsp_json_content_changes_array_callback(JSONParser* p, int index, void* user_data) {
		(void)index;
    LspJsonDidChangeTextDocumentParams* params = (LspJsonDidChangeTextDocumentParams*)user_data;
    if (params->changes_length >= params->changes_capacity) {
        size_t new_cap = params->changes_capacity == 0 ? 4 : params->changes_capacity * 2;
        void* new_mem = realloc(params->contentChanges, new_cap * sizeof(LspJsonTextDocumentContentChangeEvent));
        if (!new_mem) return -1;
        params->contentChanges = new_mem;
        params->changes_capacity = new_cap;
    }
    LspJsonTextDocumentContentChangeEvent* current = &params->contentChanges[params->changes_length];
    memset(current, 0, sizeof(LspJsonTextDocumentContentChangeEvent));
    if (json_parse_fast_object(p, lsp_json_content_change_object_callback, current) == 0) {
        params->changes_length++;
    }
    return 0;
}

static int lsp_json_did_change_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonDidChangeTextDocumentParams* params = (LspJsonDidChangeTextDocumentParams*)user_data;
    if (strcmp(key, "textDocument") == 0) {
        return json_parse_fast_object(p, lsp_json_versioned_text_document_identifier_parser_callback, &params->textDocument);
    }
    if (strcmp(key, "contentChanges") == 0) {
        return json_parse_fast_array(p, lsp_json_content_changes_array_callback, params);
    }
    return -1;
}

static int lsp_json_did_close_params_callback(JSONParser* p, const char* key, void* user_data) {
     LspJsonDidCloseTextDocumentParams* params = (LspJsonDidCloseTextDocumentParams*)user_data;
     if (strcmp(key, "textDocument") == 0) {
        return json_parse_fast_object(p, lsp_json_text_document_identifier_parser_callback, &params->textDocument);
    }
    return -1;
}

static int lsp_json_did_save_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonDidSaveTextDocumentParams* params = (LspJsonDidSaveTextDocumentParams*)user_data;
    if (strcmp(key, "textDocument") == 0)
        return json_parse_fast_object(p, lsp_json_text_document_identifier_parser_callback, &params->textDocument);
    if (strcmp(key, "text") == 0)
        return json_get_fast_string(p, &params->text);
    return -1;
}

static int lsp_json_text_positions_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonTextDocumentPositionParams* params = (LspJsonTextDocumentPositionParams*)user_data;
    if (strcmp(key, "textDocument") == 0)
        return json_parse_fast_object(p, lsp_json_text_document_identifier_parser_callback, &params->textDocument);
    if (strcmp(key, "position") == 0)
		    return json_parse_fast_object(p, lsp_json_position_parser_callback, &params->position);
    return -1;
}

static int lsp_json_inlay_hint_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonInlayHintParams* params = (LspJsonInlayHintParams*)user_data;
    if (strcmp(key, "textDocument") == 0)
        return json_parse_fast_object(p, lsp_json_text_document_identifier_parser_callback, &params->textDocument);
    if (strcmp(key, "range") == 0)
        return json_parse_fast_object(p, lsp_json_range_parser_callback, &params->range);
    return -1;
}

static int lsp_json_hover_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonHoverParams* params = (LspJsonHoverParams*)user_data;
    if (strcmp(key, "textDocument") == 0)
        return json_parse_fast_object(p, lsp_json_text_document_identifier_parser_callback, &params->textDocument);
    if (strcmp(key, "position") == 0)
        return json_parse_fast_object(p, lsp_json_position_parser_callback, &params->position);
    return -1;
}

static int lsp_json_completion_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonCompletionParams* params = (LspJsonCompletionParams*)user_data;
    if (strcmp(key, "textDocument") == 0)
        return json_parse_fast_object(p, lsp_json_text_document_identifier_parser_callback, &params->textDocument);
    if (strcmp(key, "position") == 0)
        return json_parse_fast_object(p, lsp_json_position_parser_callback, &params->position);
    return -1;
}

static int lsp_json_cancel_params_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonCancelParams* params = (LspJsonCancelParams*)user_data;
    if (strcmp(key, "id") == 0) return json_get_fast_integer(p, &params->id);
    return -1;
}

// --- Params Dispatcher ---

static int lsp_json_params_parser_dispatcher(JSONParser* parser, LspJsonMessage* message) {
    switch (message->notification_or_request.method_type) {
        case LSP_METHOD_INITIALIZE:
            return json_parse_fast_object(parser, lsp_json_initialize_params_callback, &message->notification_or_request.params.initialize);
        case LSP_METHOD_TEXTDOCUMENT_DID_OPEN:
            return json_parse_fast_object(parser, lsp_json_did_open_params_callback, &message->notification_or_request.params.didOpen);
        case LSP_METHOD_TEXTDOCUMENT_DID_CHANGE:
            return json_parse_fast_object(parser, lsp_json_did_change_params_callback, &message->notification_or_request.params.didChange);
        case LSP_METHOD_TEXTDOCUMENT_DID_CLOSE:
            return json_parse_fast_object(parser, lsp_json_did_close_params_callback, &message->notification_or_request.params.didClose);
        case LSP_METHOD_TEXTDOCUMENT_DID_SAVE:
             return json_parse_fast_object(parser, lsp_json_did_save_params_callback, &message->notification_or_request.params.didSave);
        case LSP_METHOD_TEXTDOCUMENT_HOVER:
            return json_parse_fast_object(parser, lsp_json_hover_params_callback, &message->notification_or_request.params.hover);
        case LSP_METHOD_TEXTDOCUMENT_COMPLETION:
             return json_parse_fast_object(parser, lsp_json_completion_params_callback, &message->notification_or_request.params.completion);
        case LSP_METHOD_TEXTDOCUMENT_DEFINITION:
            return json_parse_fast_object(parser, lsp_json_text_positions_params_callback, &message->notification_or_request.params.definition);
        case LSP_METHOD_TEXTDOCUMENT_REFERENCES:
            return json_parse_fast_object(parser, lsp_json_text_positions_params_callback, &message->notification_or_request.params.references);
        case LSP_METHOD_TEXTDOCUMENT_INLAY_HINT:
            return json_parse_fast_object(parser, lsp_json_inlay_hint_params_callback, &message->notification_or_request.params.inlayHint);
        case LSP_METHOD_CANCEL_REQUEST:
             return json_parse_fast_object(parser, lsp_json_cancel_params_callback, &message->notification_or_request.params.cancelRequest);
        // Methods with no params are handled by skipping the value
        case LSP_METHOD_INITIALIZED:
        case LSP_METHOD_SHUTDOWN:
        case LSP_METHOD_EXIT:
        case LSP_METHOD_WORKSPACE_DID_CHANGE_CONFIGURATION:
        default:
            return -1;
    }
}

// --- Top-Level Message Parser ---


static int lsp_json_message_parser_callback(JSONParser* p, const char* key, void* user_data) {
    LspJsonMessage* msg = (LspJsonMessage*)user_data;

    if (strcmp(key, "jsonrpc") == 0) return json_get_fast_string(p, &msg->jsonrpc);

    if (strcmp(key, "id") == 0) {
        msg->notification_or_request.has_id = true;
        return json_get_fast_integer(p, &msg->notification_or_request.id);
    }

    if (strcmp(key, "method") == 0) {
        json_get_fast_string(p, &msg->notification_or_request.method_str);
        msg->notification_or_request.method_type = lsp_json_method_to_enum(msg->notification_or_request.method_str);
        return 0;
    }

    // After parsing other fields, determine message type
    if (msg->message_type == LSP_JSON_MSG_NONE) {
        if (msg->notification_or_request.method_str) {
            msg->message_type = msg->notification_or_request.has_id ? LSP_JSON_MSG_REQUEST : LSP_JSON_MSG_NOTIFICATION;
        } else {
            // If there's no method but there is an ID, it must be a response.
            if(msg->notification_or_request.has_id) {
                 msg->message_type = LSP_JSON_MSG_RESPONSE;
                 msg->response.id = msg->notification_or_request.id;
            }
        }
    }

    if (strcmp(key, "params") == 0) {
        if (msg->message_type == LSP_JSON_MSG_REQUEST || msg->message_type == LSP_JSON_MSG_NOTIFICATION) {
            return lsp_json_params_parser_dispatcher(p, msg);
        }
    }

    // For responses, we skip result/error
    if (strcmp(key, "result") == 0 || strcmp(key, "error") == 0) {
        msg->message_type = LSP_JSON_MSG_RESPONSE;
        msg->response.id = msg->notification_or_request.id;
        return -1;
    }

    return -1;
}

/**
 *
 *
 * Params
 *
 *
 */

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
int lsp_read_json_message(LspJsonMessage* message) {
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
        return 1;
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
        return 1;
    }
    lsp_json_parse_to_message(content, content_length, message);
    free(content);
    return 0;
}

/*
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
*/

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

/*
LSPMessage* lsp_parse_message(const char* json) {
	// printf(const char *restrict, ...)


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
*/

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

    // textDocumentSync - use object format to be explicit about Full sync
    json_add_key(builder, "textDocumentSync");
    json_start_object(builder);
    json_add_number_field(builder, "change", 1); // 1 = Full, 2 = Incremental
    json_add_bool_field(builder, "openClose", true);
    json_end_object(builder);

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

    if (caps->inlay_hint_provider) {
        json_add_key(builder, "inlayHintProvider");
        json_start_object(builder);
        json_add_bool_field(builder, "resolveProvider", false);
        json_end_object(builder);
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

int lsp_json_parse_to_message(const char* json, size_t size, LspJsonMessage* message) {
    if (!message) return 1;

    JSONParser parser = json_parser_create(json, size);
    if (json_parse_fast_object(&parser, lsp_json_message_parser_callback, message) != 0) {
        lsp_json_free_message(message);
        return 1;
    }

    // Final type check: if we saw a method, it must be a request or notification
     if (message->message_type == LSP_JSON_MSG_NONE && message->notification_or_request.method_str) {
        message->message_type = message->notification_or_request.has_id ? LSP_JSON_MSG_REQUEST : LSP_JSON_MSG_NOTIFICATION;
    }

    return 0;
}

void lsp_json_free_message(LspJsonMessage* message) {
	lsp_json_inner_free_message(message);
  free(message);
}


void lsp_json_inner_free_message(LspJsonMessage* message) {
	if (!message) return;

    free(message->jsonrpc);

    // TODO: ASAN Analyze allocations
    if (message->message_type == LSP_JSON_MSG_REQUEST || message->message_type == LSP_JSON_MSG_NOTIFICATION) {
    		// TODO: no need to allocate
        free(message->notification_or_request.method_str);

        // Free params based on method type
        switch (message->notification_or_request.method_type) {
            case LSP_METHOD_INITIALIZE:
                free(message->notification_or_request.params.initialize.rootUri);
                break;
            case LSP_METHOD_TEXTDOCUMENT_DID_OPEN: {
                LspJsonTextDocumentItem* item = &message->notification_or_request.params.didOpen.textDocument;
                free(item->uri);
                free(item->languageId);
                free(item->text);
                break;
            }
            case LSP_METHOD_TEXTDOCUMENT_DID_CHANGE: {
                LspJsonDidChangeTextDocumentParams* params = &message->notification_or_request.params.didChange;
                free(params->textDocument.uri);
                for (size_t i = 0; i < params->changes_length; ++i) {
                    free(params->contentChanges[i].text);
                }
                free(params->contentChanges);
                break;
            }
            case LSP_METHOD_TEXTDOCUMENT_DID_CLOSE:
                free(message->notification_or_request.params.didClose.textDocument.uri);
                break;
            case LSP_METHOD_TEXTDOCUMENT_DID_SAVE:
                free(message->notification_or_request.params.didSave.textDocument.uri);
                free(message->notification_or_request.params.didSave.text);
                break;
            case LSP_METHOD_TEXTDOCUMENT_REFERENCES: {
                free(message->notification_or_request.params.references.textDocument.uri);
            } break;
            case LSP_METHOD_TEXTDOCUMENT_DEFINITION: {
                free(message->notification_or_request.params.references.textDocument.uri);
            } break;
            case LSP_METHOD_TEXTDOCUMENT_INLAY_HINT: {
                free(message->notification_or_request.params.inlayHint.textDocument.uri);
            } break;
            case LSP_METHOD_TEXTDOCUMENT_HOVER:
            case LSP_METHOD_TEXTDOCUMENT_COMPLETION:
                free(message->notification_or_request.params.hover.textDocument.uri);
                break;
            default:
                break; // No params to free
        }
    }
}
