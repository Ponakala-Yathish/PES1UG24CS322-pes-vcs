// index.c — Staging area (the .pes/index file)
//
// Index file format (one entry per line):
//   <mode-octal> <hash-hex> <mtime> <size> <path>
// Example:
//   100644 a1b2c3d4e5f6... 1699900000 1024 src/main.c

#include "pes.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// Forward declarations for object.c
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── index_load ───────────────────────────────────────────────────────────────
//
// Reads .pes/index into memory.
// If the file doesn't exist, initialises an empty index — this is not an error.
//
// Format per line: "<mode-octal> <hash-hex64> <mtime> <size> <path>\n"

int index_load(Index *index) {
    index->count = 0;
    memset(index->entries, 0, sizeof(index->entries));

    FILE *f = fopen(".pes/index", "r");
    if (!f) {
        // File doesn't exist yet — empty index is fine
        return 0;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        if (index->count >= MAX_INDEX_ENTRIES) break; // Safety guard

        IndexEntry *e = &index->entries[index->count];

        // Parse: "<mode> <hash> <mtime> <size> <path>"
        char hash_hex[HASH_HEX_SIZE + 1];
        unsigned int mtime, size, mode;
        char path[1024];

        if (sscanf(line, "%o %64s %u %u %1023s",
                   &mode, hash_hex, &mtime, &size, path) != 5) {
            continue; // Skip malformed lines
        }

        e->mode  = mode;
        e->mtime = mtime;
        e->size  = size;
        strncpy(e->path, path, sizeof(e->path) - 1);
        e->path[sizeof(e->path) - 1] = '\0';

        if (hex_to_hash(hash_hex, &e->hash) != 0) continue;

        index->count++;
    }

    fclose(f);
    return 0;
}

// ─── index_save ───────────────────────────────────────────────────────────────
//
// Writes the index atomically:
//   1. Sort entries by path
//   2. Write to a temp file (.pes/.index.tmp)
//   3. fsync the temp file
//   4. rename() to .pes/index

int index_save(Index *index) {
    // Sort entries by path for deterministic output
    qsort(index->entries, index->count, sizeof(IndexEntry), index_entry_cmp);

    // Write to temp file
    FILE *f = fopen(".pes/.index.tmp", "w");
    if (!f) {
        perror("index_save: fopen temp");
        return -1;
    }

    for (int i = 0; i < index->count; i++) {
        IndexEntry *e = &index->entries[i];

        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hash_hex);

        fprintf(f, "%o %s %u %u %s\n",
                e->mode, hash_hex, e->mtime, e->size, e->path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    // Atomically rename
    if (rename(".pes/.index.tmp", ".pes/index") != 0) {
        perror("index_save: rename");
        return -1;
    }
    return 0;
}

// ─── index_add ────────────────────────────────────────────────────────────────
//
// Stages a file:
//   1. Read the file from disk
//   2. Call object_write(OBJ_BLOB, ...) to store its contents
//   3. Find or create an index entry for this path
//   4. Update the entry (mode, hash, mtime, size)
//   5. Save the index

int index_add(Index *index, const char *path) {
    // Get file metadata
    struct stat st;
    if (stat(path, &st) != 0) {
        perror(path);
        return -1;
    }

    // Read file content
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    unsigned char *content = malloc(file_size + 1);
    if (!content) { fclose(f); return -1; }

    size_t got = fread(content, 1, file_size, f);
    fclose(f);
    if (got != file_size) {
        perror("index_add: fread");
        free(content);
        return -1;
    }

    // Store as blob
    ObjectID blob_id;
    if (object_write(OBJ_BLOB, content, file_size, &blob_id) != 0) {
        free(content);
        return -1;
    }
    free(content);

    // Find existing entry or create a new one
    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        if (index->count >= MAX_INDEX_ENTRIES) {
            fprintf(stderr, "index_add: index full\n");
            return -1;
        }
        entry = &index->entries[index->count++];
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->path, path, sizeof(entry->path) - 1);
    }

    entry->mode  = S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR)
                   ? 0100755 : 0100644;
    entry->hash  = blob_id;
    entry->mtime = (uint32_t)st.st_mtime;
    entry->size  = (uint32_t)st.st_size;

    // Save updated index to disk
    if (index_save(index) != 0) return -1;

    printf("staged:  %s\n", path);
    return 0;
}

// ─── index_status ─────────────────────────────────────────────────────────────
//
// Prints a summary of the staging area.
// For this implementation we show all indexed files as staged.

void index_status(const Index *index) {
    printf("Staged changes:\n");
    int showed = 0;
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        showed = 1;
    }
    if (!showed) printf("  (nothing to show)\n");

    printf("\nUnstaged changes:\n");
    printf("  (nothing to show)\n");

    printf("\nUntracked files:\n");
    printf("  (nothing to show)\n");
}

// ─── PROVIDED helpers ────────────────────────────────────────────────────────

IndexEntry *index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

void index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            for (int j = i; j < index->count - 1; j++)
                index->entries[j] = index->entries[j + 1];
            index->count--;
            return;
        }
    }
}

int index_entry_cmp(const void *a, const void *b) {
    const IndexEntry *ea = (const IndexEntry *)a;
    const IndexEntry *eb = (const IndexEntry *)b;
    return strcmp(ea->path, eb->path);
}
