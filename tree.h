#ifndef TREE_H
#define TREE_H
#include "pes.h"
#include "index.h"  // ← ADD THIS LINE
#include "pes.h"

// Maximum entries in a single tree (generous limit)
#define MAX_TREE_ENTRIES 1024

// ─── Tree Entry ───────────────────────────────────────────────────────────────
// Represents one entry in a tree object: a file (blob) or directory (tree).

typedef struct {
    uint32_t  mode;               // 0100644, 0100755, or 0040000
    char      name[256];          // Filename (not full path)
    ObjectID  hash;               // Hash of the blob or subtree
} TreeEntry;

// ─── Tree ─────────────────────────────────────────────────────────────────────

typedef struct {
    TreeEntry entries[MAX_TREE_ENTRIES];
    int       count;
} Tree;

// ─── Function Declarations ────────────────────────────────────────────────────

// Parse binary tree data into a Tree struct.
int tree_parse(const void *data, size_t len, Tree *tree_out);

// Serialize a Tree struct to binary format. Sorts entries by name first.
// Caller must free(*data_out).
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out);

// Build a tree hierarchy from the index and write all trees to the object store.
// Returns 0 and sets *root_id_out to the root tree's hash on success, -1 on error.
int tree_from_index(struct Index *index, ObjectID *root_id_out);

// Compare two TreeEntry structs by name (for qsort).
int tree_entry_cmp(const void *a, const void *b);

// Free a dynamically allocated tree (not needed for stack-allocated trees,
// but provided for consistency).
void tree_free(Tree *tree);

#endif // TREE_H
