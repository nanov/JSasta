#include "lsp_server.h"
#include "../common/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [options]\n", program_name);
    fprintf(stderr, "\nJSasta Language Server Protocol (LSP) Daemon\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --stdio        Use stdio for communication (default)\n");
    fprintf(stderr, "  -h, --help     Show this help message\n");
    fprintf(stderr, "\nThe LSP server communicates via JSON-RPC over stdin/stdout.\n");
    fprintf(stderr, "It is designed to be used with editors like VSCode, Neovim, Zed, etc.\n");
}

int main(int argc, char** argv) {
    // Initialize global type system FIRST (before any threads)
    type_system_init_global_types();
    
    // EMERGENCY DEBUG: Write immediately
    FILE* emergency = fopen("/tmp/jsasta_emergency.log", "w");
    if (emergency) {
        fprintf(emergency, "STARTED! argc=%d\n", argc);
        for (int i = 0; i < argc; i++) {
            fprintf(emergency, "  argv[%d]=%s\n", i, argv[i]);
        }
        fflush(emergency);
        fclose(emergency);
    }
    
    // Parse command-line options
    static struct option long_options[] = {
        {"stdio", no_argument, 0, 's'},
        {"help",  no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                // stdio mode (default, nothing to do)
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Initialize logger - write to file for debugging (stdout is for protocol)
    FILE* log_file = fopen("/tmp/jsasta_lsp.log", "a");
    if (log_file) {
        fprintf(log_file, "\n=== LSP Server Starting ===\n");
        fflush(log_file);
        fclose(log_file);
    }
    logger_init(LOG_VERBOSE);
    
    // Create and run LSP server
    LSPServer* server = lsp_server_create();
    lsp_server_run(server);
    lsp_server_free(server);

    return 0;
}
