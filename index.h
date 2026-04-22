#ifndef INDEX_H
#define INDEX_H

#include "pes.h"

#define MAX_INDEX_ENTRIES 4096

// ─── Index Entry ──────────────────────────────────────────────────────────────
// One staged file: mode, blob hash, metadata, and path.

typedef struct {
    uint32_t mode;          // File mode (e.g. 0100644)
    ObjectID hash;          // Blob object hash
    uint32_t mtime;         // File modification time (unix timestamp)
    uint32_t size;          // File size in bytes
    char     path[1024];    // Relative path from repo root
} IndexEntry;

// ─── Index ────────────────────────────────────────────────────────────────────

typedef struct Index {
    IndexEntry entries[MAX_INDEX_ENTRIES];
    int        count;
} Index;

// ─── Function Declarations ────────────────────────────────────────────────────

// Load the index from .pes/index. Initializes empty index if file doesn't exist.
// Returns 0 on success, -1 on error.
int index_load(Index *index);

// Save the index to .pes/index atomically (sort → temp file → rename).
// Returns 0 on success, -1 on error.
int index_save(Index *index);

// Stage a file: read it, store blob, update index entry, save index.
// Returns 0 on success, -1 on error.
int index_add(Index *index, const char *path);

// Print working directory status: staged / unstaged / untracked.
void index_status(const Index *index);

// Find an entry by path. Returns pointer into index->entries, or NULL.
IndexEntry *index_find(Index *index, const char *path);

// Remove an entry from the index by path.
void index_remove(Index *index, const char *path);

// Compare two IndexEntry pointers by path (for qsort).
int index_entry_cmp(const void *a, const void *b);

#endif // INDEX_H
