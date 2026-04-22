#ifndef PES_H
#define PES_H

#include <stdint.h>
#include <stddef.h>

// ─── Constants ───────────────────────────────────────────────────────────────

#define PES_DIR        ".pes"
#define OBJECTS_DIR    ".pes/objects"
#define REFS_DIR       ".pes/refs/heads"
#define HEAD_FILE      ".pes/HEAD"

#define HASH_SIZE      32          // SHA-256 = 32 bytes
#define HASH_HEX_SIZE  64          // 32 bytes * 2 hex chars

// ─── Object ID (SHA-256 hash) ─────────────────────────────────────────────────

typedef struct {
    unsigned char hash[HASH_SIZE];
} ObjectID;

// ─── Object Types ─────────────────────────────────────────────────────────────

typedef enum {
    OBJ_BLOB   = 1,
    OBJ_TREE   = 2,
    OBJ_COMMIT = 3,
} ObjectType;

// ─── Hash Utilities (implemented inline here) ────────────────────────────────

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static inline void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        snprintf(hex_out + i * 2, 3, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

static inline int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) != HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return -1;
        id_out->hash[i] = (unsigned char)byte;
    }
    return 0;
}

// ─── Author ──────────────────────────────────────────────────────────────────

static inline const char *pes_author(void) {
    const char *a = getenv("PES_AUTHOR");
    return a ? a : "PES User <pes@localhost>";
}

#endif // PES_H
