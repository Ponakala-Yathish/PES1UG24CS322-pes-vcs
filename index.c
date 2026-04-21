#include "pes.h"
#include "index.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/**
 * index_load: Read .pes/index into memory
 * 
 * Format: "<mode> <hash-hex> <mtime> <size> <path>\n"
 * 
 * If file doesn't exist, returns empty index (not an error)
 */
Index *index_load() {
    Index *index = malloc(sizeof(Index));
    index->entries = NULL;
    index->count = 0;
    
    FILE *f = fopen(".pes/index", "r");
    if (!f) {
        // File doesn't exist - return empty index
        return index;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        // Remove trailing newline
        line[strcspn(line, "\n")] = '\0';
        
        if (strlen(line) == 0) continue;
        
        // Parse: "<mode> <hash> <mtime> <size> <path>"
        uint32_t mode;
        char hash_hex[65];
        uint32_t mtime;
        uint32_t size;
        char path[1024];
        
        if (sscanf(line, "%o %64s %u %u %1023s", &mode, hash_hex, &mtime, &size, path) != 5) {
            continue;  // Skip malformed lines
        }
        
      
/**
 * index_save: Write index to .pes/index atomically
 * 
 * Sorts entries by path before writing.
 * Uses temp file + rename pattern.
 */
void index_save(Index *index) {
    // Sort entries by path
    qsort(index->entries, index->count, sizeof(IndexEntry),
          (int (*)(const void *, const void *))index_entry_cmp);
    
    // Write to temp file
    FILE *f = fopen(".pes/.index.tmp", "w");
    if (!f) {
        perror("fopen temp index");
        return;
    }
    
    for (int i = 0; i < index->count; i++) {
        IndexEntry *entry = &index->entries[i];
        
        // Convert hash to hex
        char hash_hex[65];
        for (int j = 0; j < 32; j++) {
            snprintf(hash_hex + j * 2, 3, "%02x", entry->hash[j]);
        }
        hash_hex[64] = '\0';
        
        // Write: "<mode> <hash> <mtime> <size> <path>\n"
        fprintf(f, "%o %s %u %u %s\n", 
                entry->mode, hash_hex, entry->mtime, entry->size, entry->path);
    }
    
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    
    // Atomically rename
    rename(".pes/.index.tmp", ".pes/index");
}

/**
 * index_add: Stage a file
 * 
 * 1. Read file from disk
 * 2. Compute blob hash and store blob
 * 3. Update index entry
 * 4. Save index
 */
void index_add(Index *index, const char *path) {
    // Read file from disk
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return;
    }
    
    // Get file size
    struct stat st;
    stat(path, &st);
    size_t file_size = st.st_size;
    
    // Read file content
    unsigned char *content = malloc(file_size);
    if (fread(content, 1, file_size, f) != file_size) {
        perror("fread");
        free(content);
        fclose(f);
        return;
    }
    fclose(f);
    
    // Write blob to object store
    char *blob_hash = object_write("blob", content, file_size);
    
    // Find or create index entry
    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        // Create new entry
        index->count++;
        index->entries = realloc(index->entries, index->count * sizeof(IndexEntry));
        entry = &index->entries[index->count - 1];
        strcpy(entry->path, path);
    }
    
    // Update entry
    entry->mode = 0o100644;  // Regular file
    
    // Convert hash hex to bytes
    for (int i = 0; i < 32; i++) {
        sscanf(blob_hash + i * 2, "%2hhx", &entry->hash[i]);
    }
    
    entry->mtime = st.st_mtime;
    entry->size = st.st_size;
    
    // Save index
    index_save(index);
    
    free(blob_hash);
    free(content);
    
    printf("Added: %s\n", path);
}

/**
 * index_status: Print status of working directory
 * 
 * Shows:
 * - Staged changes (in index, not in HEAD)
 * - Unstaged changes (modified but not staged)
 * - Untracked files (not in index)
 */
void index_status(Index *index) {
    // TODO: Implement status logic
    // For now, simple implementation
    
    printf("Staged changes:\n");
    for (int i = 0; i < index->count; i++) {
        printf("    new file:   %s\n", index->entries[i].path);
    }
    
    if (index->count == 0) {
        printf("    (nothing to show)\n");
    }
    
    printf("\nUnstaged changes:\n");
    printf("    (nothing to show)\n");
    
    printf("\nUntracked files:\n");
    printf("    (nothing to show)\n");
}

/**
 * index_find: Find an entry in the index by path
 * (PROVIDED - do not modify)
 */
IndexEntry *index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            return &index->entries[i];
        }
    }
    return NULL;
}

/**
 * index_remove: Remove an entry from the index
 * (PROVIDED - do not modify)
 */
void index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            // Shift remaining entries
            for (int j = i; j < index->count - 1; j++) {
                index->entries[j] = index->entries[j + 1];
            }
            index->count--;
            return;
        }
    }
}

/**
 * index_entry_cmp: Compare two index entries by path (for qsort)
 */
int index_entry_cmp(const IndexEntry *a, const IndexEntry *b) {
    return strcmp(a->path, b->path);
}

/**
 * index_free: Free an index
 */
void index_free(Index *index) {
    if (!index) return;
    free(index->entries);
    free(index);
}
