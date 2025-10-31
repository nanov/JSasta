#include "lsp_server.h"
#include "lsp_protocol.h"
#include "lsp_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

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

// === LSP Request Handlers ===

char* lsp_handle_initialize(LSPServer* server, const char* params) {
    (void)params; // TODO: Parse initialize params

    server->initialized = true;

    return lsp_create_initialize_response(&server->capabilities);
}

char* lsp_handle_shutdown(LSPServer* server) {
    server->shutdown_requested = true;
    return strdup("null");
}

char* lsp_handle_hover(LSPServer* server, const char* params) {
    // TODO: Parse params to get URI and position
    (void)server;
    (void)params;

    // For now, return null (no hover)
    return strdup("null");
}

char* lsp_handle_completion(LSPServer* server, const char* params) {
    // TODO: Parse params and provide completions
    (void)server;
    (void)params;

    // For now, return empty array
    return strdup("[]");
}

char* lsp_handle_definition(LSPServer* server, const char* params) {
    LSP_LOG("definition request: %s", params);

    // Parse params
    JSONValue* root = json_parse(params);
    if (!root) {
        LSP_LOG("Failed to parse definition params");
        return strdup("null");
    }

    // Extract URI and position
    JSONValue* text_document = json_object_get(root, "textDocument");
    JSONValue* position = json_object_get(root, "position");

    if (!text_document || !position) {
        LSP_LOG("Missing textDocument or position");
        json_value_free(root);
        return strdup("null");
    }

    const char* uri = json_get_string(json_object_get(text_document, "uri"));
    int line = (int)json_get_number(json_object_get(position, "line"));
    int character = (int)json_get_number(json_object_get(position, "character"));

    if (!uri) {
        LSP_LOG("No URI in definition request");
        json_value_free(root);
        return strdup("null");
    }

    LSP_LOG("Finding definition at %s:%d:%d", uri, line, character);

    // Find the document
    LSPDocument* doc = lsp_server_find_document(server, uri);
    if (!doc) {
        LSP_LOG("Document not found");
        json_value_free(root);
        return strdup("null");
    }

    // Get code index (rebuilds if type inference completed)
    CodeIndex* code_index = lsp_document_get_code_index(doc);
    if (!code_index) {
        LSP_LOG("Code index not built");
        json_value_free(root);
        return strdup("null");
    }

    // Convert URI to filename
    char* filename = lsp_uri_to_path(uri);
    if (!filename) {
        LSP_LOG("Failed to convert URI to path");
        json_value_free(root);
        return strdup("null");
    }

    // Use CodeIndex to find position (LSP uses 0-based, we use 1-based)
    PositionEntry* entry = code_index_find_at_position(code_index, filename,
                                                       (size_t)line + 1, (size_t)character + 1);
    free(filename);

    if (!entry || !entry->code_info) {
        LSP_LOG("No code element found at position");
        json_value_free(root);
        return strdup("null");
    }

    // If we're on a reference, get its definition
    SourceRange def_range = entry->is_definition ? entry->range : entry->code_info->definition;

    LSP_LOG("Definition found: %s at %zu:%zu",
            entry->code_info->name, def_range.start_line, def_range.start_column);

    // Build response
    char* def_uri = lsp_path_to_uri(def_range.filename);

    JSONBuilder* builder = json_builder_create();
    json_start_object(builder);
    json_add_string_field(builder, "uri", def_uri);

    json_add_key(builder, "range");
    json_start_object(builder);

    json_add_key(builder, "start");
    json_start_object(builder);
    json_add_number_field(builder, "line", (int)def_range.start_line - 1); // Convert to 0-based
    json_add_number_field(builder, "character", (int)def_range.start_column - 1);
    json_end_object(builder);

    json_add_key(builder, "end");
    json_start_object(builder);
    json_add_number_field(builder, "line", (int)def_range.end_line - 1);
    json_add_number_field(builder, "character", (int)def_range.end_column - 1);
    json_end_object(builder);

    json_end_object(builder); // range
    json_end_object(builder); // response

    char* response = json_builder_to_string(builder);
    json_builder_free(builder);
    free(def_uri);
    json_value_free(root);

    LSP_LOG("Returning definition: %s", response);
    return response;
}

char* lsp_handle_references(LSPServer* server, const char* params) {
    LSP_LOG("references request: %s", params);

    // Parse params
    JSONValue* root = json_parse(params);
    if (!root) {
        LSP_LOG("Failed to parse references params");
        return strdup("[]");
    }

    // Extract URI and position
    JSONValue* text_document = json_object_get(root, "textDocument");
    JSONValue* position = json_object_get(root, "position");
    JSONValue* context = json_object_get(root, "context");

    if (!text_document || !position) {
        LSP_LOG("Missing textDocument or position");
        json_value_free(root);
        return strdup("[]");
    }

    const char* uri = json_get_string(json_object_get(text_document, "uri"));
    int line = (int)json_get_number(json_object_get(position, "line"));
    int character = (int)json_get_number(json_object_get(position, "character"));

    // Check if we should include the declaration
    bool include_declaration = false;
    if (context) {
        JSONValue* include_decl = json_object_get(context, "includeDeclaration");
        if (include_decl) {
            include_declaration = json_get_bool(include_decl);
        }
    }

    if (!uri) {
        LSP_LOG("No URI in references request");
        json_value_free(root);
        return strdup("[]");
    }

    LSP_LOG("Finding references at %s:%d:%d (includeDeclaration: %d)",
            uri, line, character, include_declaration);

    // Find the document
    LSPDocument* doc = lsp_server_find_document(server, uri);
    if (!doc) {
        LSP_LOG("Document not found");
        json_value_free(root);
        return strdup("[]");
    }

    // Get code index (rebuilds if type inference completed)
    CodeIndex* code_index = lsp_document_get_code_index(doc);
    if (!code_index) {
        LSP_LOG("Code index not built");
        json_value_free(root);
        return strdup("[]");
    }

    // Convert URI to filename
    char* filename = lsp_uri_to_path(uri);
    if (!filename) {
        LSP_LOG("Failed to convert URI to path");
        json_value_free(root);
        return strdup("[]");
    }

    // Use CodeIndex to find position (LSP uses 0-based, we use 1-based)
    PositionEntry* entry = code_index_find_at_position(code_index, filename,
                                                       (size_t)line + 1, (size_t)character + 1);
    free(filename);

    if (!entry || !entry->code_info) {
        LSP_LOG("No code element found at position");
        json_value_free(root);
        return strdup("[]");
    }

    LSP_LOG("Found code element: %s", entry->code_info->name);

    // Build array of references
    JSONBuilder* builder = json_builder_create();
    json_start_array(builder);

    // Add the definition if requested
    if (include_declaration) {
        SourceRange def_range = entry->code_info->definition;
        char* def_uri = lsp_path_to_uri(def_range.filename);

        json_start_object(builder);
        json_add_string_field(builder, "uri", def_uri);

        json_add_key(builder, "range");
        json_start_object(builder);

        json_add_key(builder, "start");
        json_start_object(builder);
        json_add_number_field(builder, "line", (int)def_range.start_line - 1);
        json_add_number_field(builder, "character", (int)def_range.start_column - 1);
        json_end_object(builder);

        json_add_key(builder, "end");
        json_start_object(builder);
        json_add_number_field(builder, "line", (int)def_range.end_line - 1);
        json_add_number_field(builder, "character", (int)def_range.end_column - 1);
        json_end_object(builder);

        json_end_object(builder); // range
        json_end_object(builder); // location

        free(def_uri);
    }

    // Add all references by searching the positions array
    for (int i = 0; i < code_index->position_count; i++) {
        PositionEntry* pos = &code_index->positions[i];

        // Skip if not a reference to our code element
        if (pos->is_definition || pos->code_info != entry->code_info) {
            continue;
        }

        char* ref_uri = lsp_path_to_uri(pos->range.filename);

        json_start_object(builder);
        json_add_string_field(builder, "uri", ref_uri);

        json_add_key(builder, "range");
        json_start_object(builder);

        json_add_key(builder, "start");
        json_start_object(builder);
        json_add_number_field(builder, "line", (int)pos->range.start_line - 1);
        json_add_number_field(builder, "character", (int)pos->range.start_column - 1);
        json_end_object(builder);

        json_add_key(builder, "end");
        json_start_object(builder);
        json_add_number_field(builder, "line", (int)pos->range.end_line - 1);
        json_add_number_field(builder, "character", (int)pos->range.end_column - 1);
        json_end_object(builder);

        json_end_object(builder); // range
        json_end_object(builder); // location

        free(ref_uri);
    }

    json_end_array(builder);

    char* response = json_builder_to_string(builder);
    json_builder_free(builder);
    json_value_free(root);

    LSP_LOG("Returning references: %s", response);
    return response;
}

char* lsp_handle_document_symbol(LSPServer* server, const char* params) {
    // TODO: Parse params and return document symbols
    (void)server;
    (void)params;

    return strdup("[]");
}

// === LSP Notification Handlers ===

void lsp_handle_initialized(LSPServer* server) {
    LSP_LOG("Client initialized");
    (void)server;
}

void lsp_handle_exit(LSPServer* server) {
    LSP_LOG("Exit notification received");
    server->shutdown_requested = true;
}

void lsp_handle_did_open(LSPServer* server, const char* params) {
    LSP_LOG("didOpen called with params: %s", params);

    // Parse params
    JSONValue* root = json_parse(params);
    if (!root) {
        LSP_LOG("Failed to parse didOpen params");
        fprintf(stderr, "[LSP] Failed to parse didOpen params\n");
        return;
    }

    LSP_LOG("Parsed JSON, type: %d", root->type);

    JSONValue* text_document = json_object_get(root, "textDocument");
    if (!text_document) {
        LSP_LOG("No textDocument in params, root type is: %d", root->type);
        if (root->type == JSON_OBJECT) {
            LSP_LOG("Object has %d entries", root->object_value.count);
            for (int i = 0; i < root->object_value.count; i++) {
                LSP_LOG("  Key %d: %s", i, root->object_value.keys[i]);
            }
        }
        json_value_free(root);
        return;
    }

    const char* uri = json_get_string(json_object_get(text_document, "uri"));
    const char* language_id = json_get_string(json_object_get(text_document, "languageId"));
    int version = (int)json_get_number(json_object_get(text_document, "version"));
    const char* text = json_get_string(json_object_get(text_document, "text"));

    if (!uri || !text) {
        LSP_LOG("Missing uri or text in didOpen");
        json_value_free(root);
        return;
    }

    LSP_LOG("Document opened: %s (version %d, language: %s)", uri, version, language_id ? language_id : "unknown");

    // Open document
    lsp_document_open(server, uri, language_id, version, text);
    LSP_LOG("Document opened successfully");

    // Don't send diagnostics here - worker thread will send them after type inference

    json_value_free(root);
}

void lsp_handle_did_change(LSPServer* server, const char* params) {
    // Parse params
    JSONValue* root = json_parse(params);
    if (!root) {
        LSP_LOG("Failed to parse didChange params");
        return;
    }

    JSONValue* text_document = json_object_get(root, "textDocument");
    JSONValue* content_changes = json_object_get(root, "contentChanges");

    if (!text_document || !content_changes) {
        json_value_free(root);
        return;
    }

    const char* uri = json_get_string(json_object_get(text_document, "uri"));
    int version = (int)json_get_number(json_object_get(text_document, "version"));

    if (!uri || content_changes->type != JSON_ARRAY || content_changes->array_value.count == 0) {
        json_value_free(root);
        return;
    }

    // Process changes - if there's a range, it's incremental; otherwise it's full sync
    JSONValue* change = content_changes->array_value.elements[0];
    JSONValue* range_obj = json_object_get(change, "range");
    const char* change_text = json_get_string(json_object_get(change, "text"));
    
    if (!change_text) {
        json_value_free(root);
        return;
    }
    
    // Parse range if present (incremental update)
    TextRange range_value;
    TextRange* range_ptr = NULL;
    
    if (range_obj) {
        JSONValue* start = json_object_get(range_obj, "start");
        JSONValue* end = json_object_get(range_obj, "end");
        
        if (start && end) {
            range_value.start.line = (size_t)json_get_number(json_object_get(start, "line"));
            range_value.start.character = (size_t)json_get_number(json_object_get(start, "character"));
            range_value.end.line = (size_t)json_get_number(json_object_get(end, "line"));
            range_value.end.character = (size_t)json_get_number(json_object_get(end, "character"));
            range_ptr = &range_value;
            
            LSP_LOG("Document changed: %s (version %d) - incremental update [%zu:%zu - %zu:%zu]", 
                    uri, version, range_value.start.line, range_value.start.character,
                    range_value.end.line, range_value.end.character);
        }
    } else {
        LSP_LOG("Document changed: %s (version %d) - full sync", uri, version);
    }

    lsp_document_update(server, uri, version, range_ptr, change_text);
    // Diagnostics will be sent after the debounced parse completes
    json_value_free(root);
}

void lsp_handle_did_close(LSPServer* server, const char* params) {
    // Parse params
    JSONValue* root = json_parse(params);
    if (!root) {
        LSP_LOG("Failed to parse didClose params");
        return;
    }

    JSONValue* text_document = json_object_get(root, "textDocument");
    if (!text_document) {
        json_value_free(root);
        return;
    }

    const char* uri = json_get_string(json_object_get(text_document, "uri"));
    if (!uri) {
        json_value_free(root);
        return;
    }

    LSP_LOG("Document closed: %s", uri);

    lsp_document_close(server, uri);

    json_value_free(root);
}

void lsp_handle_did_save(LSPServer* server, const char* params) {
    LSP_LOG("Document saved");
    (void)server;
    (void)params;
    // Nothing special to do on save for now
}
