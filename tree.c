#include "pes.h"
#include "tree.h"
#include "index.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * tree_parse: Parse binary tree data into a Tree struct.
 * 
 * Binary format per entry:
 *   "<mode> <name>\0<32-byte-hash>"
 * 
 * Example:
 *   "100644 README.md\0" (16 bytes) + 32 bytes hash
 */
Tree *tree_parse(const unsigned char *data, size_t len) {
    Tree *tree = malloc(sizeof(Tree));
    tree->entries = NULL;
    tree->count = 0;
    
    const unsigned char *p = data;
    const unsigned char *end = data + len;
    
    while (p < end) {
        // Find the space: "<mode> <name>"
        const unsigned char *space = (unsigned char *)memchr(p, ' ', end - p);
        if (!space) break;
        
        // Parse mode
        char mode_str[32];
        size_t mode_len = space - p;
        strncpy(mode_str, (char *)p, mode_len);
        mode_str[mode_len] = '\0';
        uint32_t mode = strtoul(mode_str, NULL, 8);
        p = space + 1;
        
        // Find the null terminator: "<name>\0"
        const unsigned char *null_pos = (unsigned char *)memchr(p, '\0', end - p);
        if (!null_pos) break;
        
        // Extract name
        char name[256];
        size_t name_len = null_pos - p;
        strncpy(name, (char *)p, name_len);
        name[name_len] = '\0';
        p = null_pos + 1;
        
        // Next 32 bytes are the hash
        if (p + 32 > end) break;
        unsigned char hash[32];
        memcpy(hash, p, 32);
        p += 32;
        
        // Add entry to tree
        tree->count++;
        tree->entries = realloc(tree->entries, tree->count * sizeof(TreeEntry));
        TreeEntry *entry = &tree->entries[tree->count - 1];
        entry->mode = mode;
        strcpy(entry->name, name);
        memcpy(entry->hash, hash, 32);
    }
    
    return tree;
}

/**
 * tree_serialize: Convert Tree struct to binary format.
 * 
 * IMPORTANT: Must sort entries by name first!
 * This ensures deterministic hashing.
 * 
 * Returns malloc'd buffer with binary data
 */
unsigned char *tree_serialize(Tree *tree, size_t *len_out) {
    // Sort entries by name
    qsort(tree->entries, tree->count, sizeof(TreeEntry), 
          (int (*)(const void *, const void *))tree_entry_cmp);
    
    // Calculate total size
    size_t total_len = 0;
    for (int i = 0; i < tree->count; i++) {
        TreeEntry *entry = &tree->entries[i];
        // "<mode> <name>\0" + 32 bytes hash
        total_len += snprintf(NULL, 0, "%o", entry->mode) + 1 + strlen(entry->name) + 1 + 32;
    }
    
    // Serialize
    unsigned char *data = malloc(total_len);
    unsigned char *p = data;
    
    for (int i = 0; i < tree->count; i++) {
        TreeEntry *entry = &tree->entries[i];
        
        // Write "<mode> <name>\0"
        int mode_len = sprintf((char *)p, "%o ", entry->mode);
        p += mode_len;
        
        int name_len = strlen(entry->name);
        strcpy((char *)p, entry->name);
        p += name_len;
        *p++ = '\0';
        
        // Write 32-byte hash
        memcpy(p, entry->hash, 32);
        p += 32;
    }
    
    *len_out = total_len;
    return data;
}

/**
 * tree_from_index: Build tree hierarchy from index.
 * 
 * The index has flat entries like "src/main.c", but trees are hierarchical.
 * This function:
 * 1. Creates root tree
 * 2. For each index entry, creates intermediate trees as needed
 * 3. Writes all trees to object store
 * 4. Returns root tree hash
 */
char *tree_from_index(Index *index) {
    if (index->count == 0) {
        return NULL;  // Empty index
    }
    
    // Root tree
    Tree *root = malloc(sizeof(Tree));
    root->entries = malloc(index->count * sizeof(TreeEntry));
    root->count = 0;
    
    // Track created subtrees
    Tree *current_tree = root;
    char current_path[1024] = "";
    
    // Sort index entries by path
    qsort(index->entries, index->count, sizeof(IndexEntry),
          (int (*)(const void *, const void *))index_entry_cmp);
    
    // Process each index entry
    for (int i = 0; i < index->count; i++) {
        IndexEntry *entry = &index->entries[i];
        char path[1024];
        strcpy(path, entry->path);
        
        // Split path by '/'
        char *last_slash = strrchr(path, '/');
        
        if (!last_slash) {
            // File at root level
            TreeEntry *te = &root->entries[root->count++];
            te->mode = entry->mode;
            strcpy(te->name, entry->path);
            memcpy(te->hash, entry->hash, 32);
        
    // Write root tree to object store
    size_t tree_len;
    unsigned char *tree_data = tree_serialize(root, &tree_len);
    char *root_hash = object_write("tree", tree_data, tree_len);
    
    free(tree_data);
    free(root->entries);
    free(root);
    
    return root_hash;
}

/**
 * tree_free: Free a tree and its entries
 */
void tree_free(Tree *tree) {
    if (!tree) return;
    free(tree->entries);
    free(tree);
}

/**
 * tree_entry_cmp: Compare two tree entries by name (for qsort)
 */
int tree_entry_cmp(const TreeEntry *a, const TreeEntry *b) {
    return strcmp(a->name, b->name);
}
