#pragma once

#include "common/string_utils.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/*
================================================================================
 I. PRE-REQUISITES: USER-PROVIDED JSON PARSER API
================================================================================
 This implementation assumes the existence of the following JSON parser functions.
 A mock implementation is provided below if you want to test this file directly.
--------------------------------------------------------------------------------
*/

/*
================================================================================
 II. LSP ENUMS AND FORWARD DECLARATIONS
================================================================================
*/

// An enum to uniquely identify the LSP method for efficient dispatching.
typedef enum {
    LSP_METHOD_UNKNOWN,
    // Lifecycle
    LSP_METHOD_INITIALIZE,
    LSP_METHOD_INITIALIZED,
    LSP_METHOD_SHUTDOWN,
    LSP_METHOD_EXIT,
    // Text Synchronization
    LSP_METHOD_TEXTDOCUMENT_DID_OPEN,
    LSP_METHOD_TEXTDOCUMENT_DID_CHANGE,
    LSP_METHOD_TEXTDOCUMENT_DID_CLOSE,
    LSP_METHOD_TEXTDOCUMENT_DID_SAVE,
    // Language Features
    LSP_METHOD_TEXTDOCUMENT_HOVER,
    LSP_METHOD_TEXTDOCUMENT_COMPLETION,
    LSP_METHOD_TEXTDOCUMENT_DEFINITION,
    LSP_METHOD_TEXTDOCUMENT_REFERENCES,
    // Workspace
    LSP_METHOD_WORKSPACE_DID_CHANGE_CONFIGURATION,
    // Generic
    LSP_METHOD_CANCEL_REQUEST
} LspJsonMethodType;

// Forward declaration of all primary structs
typedef struct LspJsonMessage LspJsonMessage;
typedef struct LspJsonPosition LspJsonPosition;
typedef struct LspJsonRange LspJsonRange;
typedef struct LspJsonTextDocumentIdentifier LspJsonTextDocumentIdentifier;
typedef struct LspJsonTextDocumentItem LspJsonTextDocumentItem;
typedef struct LspJsonVersionedTextDocumentIdentifier LspJsonVersionedTextDocumentIdentifier;
typedef struct LspJsonTextDocumentContentChangeEvent LspJsonTextDocumentContentChangeEvent;
typedef struct LspJsonInitializeParams LspJsonInitializeParams;
typedef struct LspJsonDidChangeTextDocumentParams LspJsonDidChangeTextDocumentParams;
typedef struct LspJsonDidOpenTextDocumentParams LspJsonDidOpenTextDocumentParams;
typedef struct LspJsonDidCloseTextDocumentParams LspJsonDidCloseTextDocumentParams;
typedef struct LspJsonDidSaveTextDocumentParams LspJsonDidSaveTextDocumentParams;
typedef struct LspJsonHoverParams LspJsonHoverParams;
typedef struct LspJsonCompletionParams LspJsonCompletionParams;
typedef struct LspJsonDidChangeConfigurationParams LspJsonDidChangeConfigurationParams;
typedef struct LspJsonCancelParams LspJsonCancelParams;
typedef struct LspJsonTextDocumentPositionParams LspJsonTextDocumentPositionParams;

/*
================================================================================
 III. LSP DATA STRUCTURE DEFINITIONS
================================================================================
*/

// --- Text Document Types ---

struct LspJsonTextDocumentIdentifier {
    char* uri;
};

struct LspJsonTextDocumentItem {
    char* uri;
    char* languageId;
    int version;
    char* text;
};

struct LspJsonVersionedTextDocumentIdentifier {
    char* uri;
    int version;
};

struct LspJsonTextDocumentContentChangeEvent {
    TextRange range;       // Optional
    bool has_range;
    // uint32_t* rangeLength; // Optional
    char* text;
};

// --- Params for Specific Methods ---

struct LspJsonInitializeParams {
    int64_t processId;
    char* rootUri;
    // Client capabilities, workspace folders etc. are omitted for brevity
    // but would be parsed into their own structs here.
};

struct LspJsonDidOpenTextDocumentParams {
    LspJsonTextDocumentItem textDocument;
};

struct LspJsonDidChangeTextDocumentParams {
    LspJsonVersionedTextDocumentIdentifier textDocument;
    LspJsonTextDocumentContentChangeEvent* contentChanges;
    size_t changes_length;
    size_t changes_capacity;
};

struct LspJsonDidCloseTextDocumentParams {
    LspJsonTextDocumentIdentifier textDocument;
};

struct LspJsonDidSaveTextDocumentParams {
    LspJsonTextDocumentIdentifier textDocument;
    char* text; // Optional
};

struct LspJsonTextDocumentPositionParams {
    LspJsonTextDocumentIdentifier textDocument;
    TextPosition position;
};

struct LspJsonHoverParams {
    LspJsonTextDocumentIdentifier textDocument;
    TextPosition position;
};

struct LspJsonCompletionParams {
    LspJsonTextDocumentIdentifier textDocument;
    TextPosition position;
    // CompletionContext would be parsed here
};

struct LspJsonDidChangeConfigurationParams {
    // The 'settings' field can be any JSON value, so we skip it for this typed parser.
    // A real implementation might store it as a raw JSON string.
};

struct LspJsonCancelParams {
    int64_t id;
};

// --- Main Message Structure ---

typedef enum {
    LSP_JSON_MSG_NONE,
    LSP_JSON_MSG_REQUEST,
    LSP_JSON_MSG_RESPONSE,
    LSP_JSON_MSG_NOTIFICATION
} LspJsonMessageType;

struct LspJsonMessage {
    LspJsonMessageType message_type;
    char* jsonrpc;

    union {
        // Request and Notification
        struct {
            char* method_str;
            LspJsonMethodType method_type;
            bool has_id;
            int64_t id;

            // A union for all possible params objects
            union {
                LspJsonInitializeParams initialize;
                LspJsonDidOpenTextDocumentParams didOpen;
                LspJsonDidChangeTextDocumentParams didChange;
                LspJsonDidCloseTextDocumentParams didClose;
                LspJsonDidSaveTextDocumentParams didSave;
                LspJsonHoverParams hover;
                LspJsonCompletionParams completion;
                LspJsonDidChangeConfigurationParams didChangeConfiguration;
                LspJsonCancelParams cancelRequest;
                LspJsonTextDocumentPositionParams definition;
                LspJsonTextDocumentPositionParams references;
                // 'initialized', 'shutdown', 'exit' have no params
            } params;

        } notification_or_request;

        // Response
        struct {
            int64_t id;
            // Result and Error are skipped for this incoming-message-focused parser
        } response;
    };
};

// parsing message from json string, caller is owner of the message
int lsp_json_parse_to_message(const char* json, size_t size, LspJsonMessage* message);

void lsp_json_free_message(LspJsonMessage* message);
void lsp_json_inner_free_message(LspJsonMessage* message);
