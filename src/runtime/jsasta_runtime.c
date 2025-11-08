#include <stdlib.h>
#include <stdint.h>
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

int get_errno() {
	return errno;
}
