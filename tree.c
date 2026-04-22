// tree.c — Tree object serialization and construction
//
// Binary format per entry:
//   "<mode-octal> <name>\0<32-byte-hash>"
// Entries must be sorted by name before serialization (for deterministic hashing).

#include "pes.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for object.c
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── tree_entry_cmp ───────────────────────────────────────────────────────────

int tree_entry_cmp(const void *a, const void *b) {
    const TreeEntry *ea = (const TreeEntry *)a;
    const TreeEntry *eb = (const TreeEntry *)b;
    return strcmp(ea->name, eb->name);
}

// ─── tree_serialize ───────────────────────────────────────────────────────────
//
// Converts a Tree struct to the binary on-disk format.
// Sorts entries by name first (required for deterministic hashing).
//
// Each entry:   "<mode> <name>\0" + 32 raw hash bytes
//
// Caller must free(*data_out).

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // Make a mutable copy so we can sort it
    Tree sorted = *tree;
    qsort(sorted.entries, sorted.count, sizeof(TreeEntry), tree_entry_cmp);

    // Calculate total size first
    size_t total = 0;
    for (int i = 0; i < sorted.count; i++) {
        // "%o " + name + '\0' + 32 bytes
        char mode_str[16];
        int mlen = snprintf(mode_str, sizeof(mode_str), "%o ", sorted.entries[i].mode);
        total += (size_t)mlen + strlen(sorted.entries[i].name) + 1 + HASH_SIZE;
    }

    unsigned char *buf = malloc(total);
    if (!buf) return -1;

    unsigned char *p = buf;
    for (int i = 0; i < sorted.count; i++) {
        TreeEntry *e = &sorted.entries[i];

        // Write "<mode> "
        int mlen = sprintf((char *)p, "%o ", e->mode);
        p += mlen;

        // Write "<name>\0"
        size_t nlen = strlen(e->name);
        memcpy(p, e->name, nlen);
        p += nlen;
        *p++ = '\0';

        // Write 32-byte raw hash
        memcpy(p, e->hash.hash, HASH_SIZE);
        p += HASH_SIZE;
    }

    *data_out = buf;
    *len_out  = total;
    return 0;
}

// ─── tree_parse ───────────────────────────────────────────────────────────────
//
// Parses binary tree data into a Tree struct.
// Each entry: "<mode> <name>\0" + 32 raw hash bytes

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;

    const unsigned char *p   = (const unsigned char *)data;
    const unsigned char *end = p + len;

    while (p < end) {
        if (tree_out->count >= MAX_TREE_ENTRIES) break;

        // Find space separating mode from name
        const unsigned char *sp = memchr(p, ' ', (size_t)(end - p));
        if (!sp) break;

        // Parse octal mode
        char mode_str[32];
        size_t mode_len = (size_t)(sp - p);
        if (mode_len >= sizeof(mode_str)) break;
        memcpy(mode_str, p, mode_len);
        mode_str[mode_len] = '\0';
        uint32_t mode = (uint32_t)strtoul(mode_str, NULL, 8);
        p = sp + 1;

        // Find null terminator ending the name
        const unsigned char *nl = memchr(p, '\0', (size_t)(end - p));
        if (!nl) break;

        // Extract name
        size_t name_len = (size_t)(nl - p);
        if (name_len >= sizeof(tree_out->entries[0].name)) break;
        char name[256];
        memcpy(name, p, name_len);
        name[name_len] = '\0';
        p = nl + 1;

        // Read 32-byte hash
        if (p + HASH_SIZE > end) break;
        unsigned char hash[HASH_SIZE];
        memcpy(hash, p, HASH_SIZE);
        p += HASH_SIZE;

        // Store entry
        TreeEntry *e = &tree_out->entries[tree_out->count++];
        e->mode = mode;
        strcpy(e->name, name);
        memcpy(e->hash.hash, hash, HASH_SIZE);
    }

    return 0;
}

// ─── tree_free ────────────────────────────────────────────────────────────────

void tree_free(Tree *tree) {
    (void)tree; // Stack-allocated Trees need no freeing
}

// ─── tree_from_index ──────────────────────────────────────────────────────────
//
// Builds a tree hierarchy from the flat index and writes all tree objects to
// the object store. Handles nested paths like "src/lib/foo.c".
//
// Strategy:
//   - Sort index entries by path
//   - For each unique directory prefix, build a sub-Tree and write it
//   - Use a recursive helper that processes a slice of the sorted index
//
// Returns 0 on success (with *root_id_out set), -1 on error.

// Helper: find the first '/' in path, return its offset or -1 if none.
static int first_slash(const char *path) {
    const char *p = strchr(path, '/');
    return p ? (int)(p - path) : -1;
}

// Recursive helper: build a tree for entries[start..end) where all paths
// are relative (the directory prefix has been stripped).
// Writes the tree to the object store and returns 0, filling *id_out.
static int build_tree(IndexEntry *entries, int start, int end, ObjectID *id_out) {
    Tree t;
    t.count = 0;

    int i = start;
    while (i < end) {
        const char *path = entries[i].path;
        int slash = first_slash(path);

        if (slash < 0) {
            // Plain file at this level
            TreeEntry *te = &t.entries[t.count++];
            te->mode = entries[i].mode;
            strncpy(te->name, path, sizeof(te->name) - 1);
            te->name[sizeof(te->name) - 1] = '\0';
            te->hash = entries[i].hash;
            i++;
        } else {
            // Directory: collect all entries sharing the same first component
            char dir_name[256];
            if ((size_t)slash >= sizeof(dir_name)) { i++; continue; }
            memcpy(dir_name, path, (size_t)slash);
            dir_name[slash] = '\0';

            // Find the range [i, j) with the same first component
            int j = i;
            while (j < end) {
                const char *p2 = entries[j].path;
                int s2 = first_slash(p2);
                if (s2 != slash) break;
                if (strncmp(p2, dir_name, (size_t)slash) != 0) break;
                j++;
            }

            // Strip the "dir/" prefix from entries[i..j) temporarily
            // We'll do it by saving and restoring
            char saved[j - i][1024];
            for (int k = i; k < j; k++) {
                strcpy(saved[k - i], entries[k].path);
                // Advance path past "dir/"
                memmove(entries[k].path,
                        entries[k].path + slash + 1,
                        strlen(entries[k].path) - slash);
            }

            // Recurse to build the subtree
            ObjectID sub_id;
            if (build_tree(entries, i, j, &sub_id) != 0) return -1;

            // Restore paths
            for (int k = i; k < j; k++) {
                strcpy(entries[k].path, saved[k - i]);
            }

            // Add directory entry to current tree
            TreeEntry *te = &t.entries[t.count++];
            te->mode = 0040000;
            strncpy(te->name, dir_name, sizeof(te->name) - 1);
            te->name[sizeof(te->name) - 1] = '\0';
            te->hash = sub_id;

            i = j;
        }
    }

    // Serialize and write this tree
    void *data;
    size_t dlen;
    if (tree_serialize(&t, &data, &dlen) != 0) return -1;
    int rc = object_write(OBJ_TREE, data, dlen, id_out);
    free(data);
    return rc;
}

int tree_from_index(struct Index *index, ObjectID *root_id_out) {
    if (index->count == 0) return -1; // Nothing staged

    // Sort index by path
    qsort(index->entries, index->count, sizeof(IndexEntry), index_entry_cmp);

    return build_tree(index->entries, 0, index->count, root_id_out);
}
