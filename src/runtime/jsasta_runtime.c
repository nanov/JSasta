#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

// JSasta runtime memory allocation functions
// These wrap malloc/calloc/free for now, but can be replaced with ARC or GC in the future

// Allocate zeroed memory
// Returns pointer to allocated memory, or NULL on failure
void* jsasta_alloc(uint64_t size) {
    if (size == 0) {
        return NULL;
    }
    return calloc(1, size);
}

// Free allocated memory
// Safe to call with NULL pointer (no-op)
void jsasta_free(void* ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

// String structure matching JSasta str type: { i8* data, i64 length }
typedef struct {
    char* data;
    int64_t length;
} StrWrapper;

// Allocate a new string with the given length
// The data buffer will be allocated with length + 1 bytes (for null terminator)
// Returns a StrWrapper with allocated data, or {NULL, 0} on failure
StrWrapper jsasta_alloc_string(int64_t length) {
    if (length < 0) {
        return (StrWrapper){NULL, 0};
    }
    
    // Allocate buffer with extra byte for null terminator
    char* data = (char*)malloc(length + 1);
    if (!data) {
        return (StrWrapper){NULL, 0};
    }
    
    // Null terminate the string
    data[length] = '\0';
    
    return (StrWrapper){data, length};
}

// Free a string allocated by jsasta_alloc_string
// Safe to call with {NULL, 0}
void jsasta_free_string(StrWrapper str) {
    if (str.data != NULL) {
        free(str.data);
    }
}

int get_errno() {
	return errno;
}
