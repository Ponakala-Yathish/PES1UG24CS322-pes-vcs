#include "pes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <errno.h>

/**
 * object_write: Store a blob, tree, or commit object in the object store.
 * 
 * The object is stored at .pes/objects/XX/YYY... where XX is the first 2 hex
 * chars of the SHA-256 hash and YYY... is the rest.
 * 
 * Format: "<type> <size>\0<data>"
 * where type is "blob", "tree", or "commit"
 * 
 * Returns: malloc'd 64-char hex string (the hash), or NULL on error
 */
char *object_write(const char *type, const void *data, size_t len) {
    unsigned char full_object[65536];  // Buffer for header + data
    unsigned char hash[32];
    char hash_hex[65];
    char header[256];
    
    // Build the header: "blob <size>" or "tree <size>" or "commit <size>"
    int header_len = snprintf(header, sizeof(header), "%s %zu", type, len);
    
    // Full object = header + null byte + data
    memcpy(full_object, header, header_len);
    full_object[header_len] = '\0';
    memcpy(full_object + header_len + 1, data, len);
    size_t full_len = header_len + 1 + len;
    
    // Compute SHA-256 hash
    SHA256(full_object, full_len, hash);
    
  
    
    // Sync to disk before rename
    fsync(fd);
    close(fd);
    
    // Atomically rename temp to final location
    char final_path[512];
    snprintf(final_path, sizeof(final_path), "%s/%s", obj_dir, hash_hex + 2);
    
    if (rename(temp_path, final_path) < 0) {
        perror("rename");
        unlink(temp_path);
        return NULL;
    }
    
    // Return allocated hash string
    char *result = malloc(65);
    strcpy(result, hash_hex);
    return result;
}

/**
 * object_read: Retrieve an object from the object store.
 * 
 * Reads the object at .pes/objects/XX/YYY..., verifies its integrity,
 * and returns the data portion (without header).
 * 
 * Args:
 *   hash: 64-char hex SHA-256 hash
 *   type_out: pointer to receive type string ("blob", "tree", "commit")
 *   len_out: pointer to receive data length
 * 
 * Returns: malloc'd data buffer (content only, no header), or NULL if not found/corrupted
 */
unsigned char *object_read(const char *hash, char *type_out, size_t *len_out) {
    unsigned char hash_bytes[32];
    unsigned char expected_hash[32];
    char expected_hex[65];
    
    // Build path from hash
    char obj_path[512];
    snprintf(obj_path, sizeof(obj_path), ".pes/objects/%.2s/%s", hash, hash + 2);
    
    // Read the entire file
    int fd = open(obj_path, O_RDONLY);
    if (fd < 0) {
        return NULL;  // File doesn't exist
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }
    
    size_t file_size = st.st_size;
    unsigned char *file_content = malloc(file_size);
    
    if (read(fd, file_content, file_size) != (ssize_t)file_size) {
        perror("read");
        close(fd);
        free(file_content);
        return NULL;
    }
    close(fd);
    
    // Parse the header: "type size\0<data>"
    // Find the null terminator
    char *null_pos = (char *)memchr(file_content, '\0', file_size);
    if (!null_pos) {
        free(file_content);
        return NULL;
    }
    
    size_t header_len = null_pos - (char *)file_content;
    char header[256];
    strncpy(header, (char *)file_content, header_len);
    header[header_len] = '\0';
    
    // Parse "type size" from header
    char type[64];
    size_t data_len;
    if (sscanf(header, "%63s %zu", type, &data_len) != 2) {
        free(file_content);
        return NULL;
    }
    
    // Verify integrity: recompute hash and compare
    SHA256(file_content, file_size, expected_hash);
    
    for (int i = 0; i < 32; i++) {
        snprintf(expected_hex + i * 2, 3, "%02x", expected_hash[i]);
    }
    expected_hex[64] = '\0';
    
    if (strcmp(expected_hex, hash) != 0) {
        // Hash mismatch - corruption detected!
        free(file_content);
        return NULL;
    }
    
    // Extract just the data (after header + null byte)
    unsigned char *data = malloc(data_len);
    memcpy(data, file_content + header_len + 1, data_len);
    free(file_content);
    
    // Return type and length
    strcpy(type_out, type);
    *len_out = data_len;
    
    return data;
}
