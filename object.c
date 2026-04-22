// object.c — Content-addressable object store
//
// Object format stored on disk:
//   "<type> <size>\0<data>"
// where type is "blob", "tree", or "commit"
//
// Objects are stored at:
//   .pes/objects/XX/YYYY...
// where XX = first 2 hex chars of SHA-256, YYYY... = remaining 62 chars.

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <errno.h>

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Returns the string name for an ObjectType
static const char *type_to_str(ObjectType type) {
    switch (type) {
        case OBJ_BLOB:   return "blob";
        case OBJ_TREE:   return "tree";
        case OBJ_COMMIT: return "commit";
        default:         return "unknown";
    }
}

// Parses a type string back to ObjectType
static int str_to_type(const char *str, ObjectType *out) {
    if (strcmp(str, "blob")   == 0) { *out = OBJ_BLOB;   return 0; }
    if (strcmp(str, "tree")   == 0) { *out = OBJ_TREE;   return 0; }
    if (strcmp(str, "commit") == 0) { *out = OBJ_COMMIT; return 0; }
    return -1;
}

// Build the filesystem path for an object given its ID
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    // First 2 chars = directory, rest = filename
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

// Check if an object already exists in the store
int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── object_write ─────────────────────────────────────────────────────────────
//
// Stores an object in the content-addressable store.
//
// Steps:
//   1. Build the full object: header + null byte + data
//      Header format: "blob 42" or "tree 128" or "commit 217"
//   2. Compute SHA-256 of the full object → this is the ObjectID
//   3. If the object already exists (deduplication), skip writing
//   4. Create the shard directory (.pes/objects/XX/)
//   5. Write to a temp file, fsync, then rename atomically
//
// Returns 0 on success, -1 on error.

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str = type_to_str(type);

    // Build header: "blob 42\0" style
    char header[128];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    // header_len does NOT include the null terminator we'll write manually

    // Total object = header + '\0' + data
    size_t full_len = (size_t)header_len + 1 + len;
    unsigned char *full_obj = malloc(full_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_len);
    full_obj[header_len] = '\0';
    memcpy(full_obj + header_len + 1, data, len);

    // Compute SHA-256 of the full object
    SHA256(full_obj, full_len, id_out->hash);

    // Deduplication: if already stored, skip
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    // Ensure .pes/objects/ directory exists
    mkdir(PES_DIR, 0755);
    mkdir(OBJECTS_DIR, 0755);

    // Build shard dir path: .pes/objects/XX/
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);

    char shard_dir[256];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755);

    // Write to temp file first
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.tmp_%s", shard_dir, hex + 2);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0444);
    if (fd < 0) {
        perror("object_write: open temp");
        free(full_obj);
        return -1;
    }

    ssize_t written = write(fd, full_obj, full_len);
    free(full_obj);

    if (written < 0 || (size_t)written != full_len) {
        perror("object_write: write");
        close(fd);
        unlink(tmp_path);
        return -1;
    }

    fsync(fd);
    close(fd);

    // Atomically rename to final path
    char final_path[512];
    snprintf(final_path, sizeof(final_path), "%s/%s", shard_dir, hex + 2);

    if (rename(tmp_path, final_path) != 0) {
        perror("object_write: rename");
        unlink(tmp_path);
        return -1;
    }

    // Make file readable and writable for testing
    chmod(final_path, 0644);

    return 0;
}

// ─── object_read ──────────────────────────────────────────────────────────────
//
// Reads an object from the store and verifies its integrity.
//
// Steps:
//   1. Build path from id → read the file
//   2. Recompute SHA-256 of the file content → compare to id (integrity check)
//   3. Parse the header to extract type and data length
//   4. Return a malloc'd buffer containing just the data (caller must free)
//
// Returns 0 on success, -1 on error (including integrity failure).

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    // Open and read the entire file
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }

    size_t file_size = (size_t)st.st_size;
    unsigned char *raw = malloc(file_size);
    if (!raw) { close(fd); return -1; }

    ssize_t got = read(fd, raw, file_size);
    close(fd);
    if (got < 0 || (size_t)got != file_size) {
        free(raw);
        return -1;
    }

    // Integrity check: recompute hash and compare to the id we were given
    unsigned char computed[HASH_SIZE];
    SHA256(raw, file_size, computed);
    if (memcmp(computed, id->hash, HASH_SIZE) != 0) {
        // Corruption detected
        free(raw);
        return -1;
    }

    // Parse header: "type size\0data"
    char *null_pos = memchr(raw, '\0', file_size);
    if (!null_pos) { free(raw); return -1; }

    size_t header_len = (size_t)(null_pos - (char *)raw);
    char header[256];
    if (header_len >= sizeof(header)) { free(raw); return -1; }
    memcpy(header, raw, header_len);
    header[header_len] = '\0';

    char type_str[64];
    size_t data_len;
    if (sscanf(header, "%63s %zu", type_str, &data_len) != 2) {
        free(raw);
        return -1;
    }

    if (str_to_type(type_str, type_out) != 0) { free(raw); return -1; }

    // Copy out just the data portion
    *data_out = malloc(data_len + 1); // +1 so caller can safely treat as string
    if (!*data_out) { free(raw); return -1; }
    memcpy(*data_out, null_pos + 1, data_len);
    ((char *)*data_out)[data_len] = '\0';
    *len_out = data_len;

    free(raw);
    return 0;
}
