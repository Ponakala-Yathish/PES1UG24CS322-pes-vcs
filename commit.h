#ifndef COMMIT_H
#define COMMIT_H

#include "pes.h"

// ─── Commit struct ────────────────────────────────────────────────────────────

typedef struct {
    ObjectID  tree;              // Root tree hash
    ObjectID  parent;            // Parent commit hash (if has_parent)
    int       has_parent;        // 1 if this is not the root commit
    char      author[256];       // Author string
    uint64_t  timestamp;         // Unix timestamp
    char      message[1024];     // Commit message
} Commit;

// ─── Callback type for commit_walk ───────────────────────────────────────────

typedef void (*commit_walk_fn)(const ObjectID *id, const Commit *commit, void *ctx);

// ─── Function Declarations ────────────────────────────────────────────────────

int commit_parse(const void *data, size_t len, Commit *commit_out);
int commit_serialize(const Commit *commit, void **data_out, size_t *len_out);
int commit_walk(commit_walk_fn callback, void *ctx);
int head_read(ObjectID *id_out);
int head_update(const ObjectID *new_commit);
int commit_create(const char *message, ObjectID *commit_id_out);

#endif // COMMIT_H
