// commit.c — Commit creation and history traversal
//
// Commit object format (stored as text, one field per line):
//
//   tree <64-char-hex-hash>
//   parent <64-char-hex-hash>        ← omitted for the first commit
//   author <name> <unix-timestamp>
//   committer <name> <unix-timestamp>
//
//   <commit message>
//
// Note: there is a blank line between the headers and the message.

#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

// Forward declarations (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── commit_parse ─────────────────────────────────────────────────────────────

int commit_parse(const void *data, size_t len, Commit *commit_out) {
    (void)len;
    const char *p = (const char *)data;
    char hex[HASH_HEX_SIZE + 1];

    // "tree <hex>\n"
    if (sscanf(p, "tree %64s\n", hex) != 1) return -1;
    if (hex_to_hash(hex, &commit_out->tree) != 0) return -1;
    p = strchr(p, '\n') + 1;

    // optional "parent <hex>\n"
    if (strncmp(p, "parent ", 7) == 0) {
        if (sscanf(p, "parent %64s\n", hex) != 1) return -1;
        if (hex_to_hash(hex, &commit_out->parent) != 0) return -1;
        commit_out->has_parent = 1;
        p = strchr(p, '\n') + 1;
    } else {
        commit_out->has_parent = 0;
    }

    // "author <name> <timestamp>\n"
    char author_buf[256];
    if (sscanf(p, "author %255[^\n]\n", author_buf) != 1) return -1;
    // Split off trailing timestamp
    char *last_space = strrchr(author_buf, ' ');
    if (!last_space) return -1;
    commit_out->timestamp = (uint64_t)strtoull(last_space + 1, NULL, 10);
    *last_space = '\0';
    snprintf(commit_out->author, sizeof(commit_out->author), "%s", author_buf);

    p = strchr(p, '\n') + 1;  // skip author line
    p = strchr(p, '\n') + 1;  // skip committer line
    p = strchr(p, '\n') + 1;  // skip blank line

    snprintf(commit_out->message, sizeof(commit_out->message), "%s", p);
    return 0;
}

// ─── commit_serialize ─────────────────────────────────────────────────────────

int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char tree_hex[HASH_HEX_SIZE + 1];
    char parent_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&commit->tree, tree_hex);

    char buf[8192];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "tree %s\n", tree_hex);
    if (commit->has_parent) {
        hash_to_hex(&commit->parent, parent_hex);
        n += snprintf(buf + n, sizeof(buf) - n, "parent %s\n", parent_hex);
    }
    n += snprintf(buf + n, sizeof(buf) - n,
                  "author %s %" PRIu64 "\n"
                  "committer %s %" PRIu64 "\n"
                  "\n"
                  "%s",
                  commit->author, commit->timestamp,
                  commit->author, commit->timestamp,
                  commit->message);

    *data_out = malloc((size_t)n + 1);
    if (!*data_out) return -1;
    memcpy(*data_out, buf, (size_t)n + 1);
    *len_out = (size_t)n;
    return 0;
}

// ─── commit_walk ──────────────────────────────────────────────────────────────

int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;

    while (1) {
        ObjectType type;
        void *raw;
        size_t raw_len;
        if (object_read(&id, &type, &raw, &raw_len) != 0) return -1;

        Commit c;
        int rc = commit_parse(raw, raw_len, &c);
        free(raw);
        if (rc != 0) return -1;

        callback(&id, &c, ctx);

        if (!c.has_parent) break;
        id = c.parent;
    }
    return 0;
}

// ─── head_read ────────────────────────────────────────────────────────────────

int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';

    char ref_path[512];
    if (strncmp(line, "ref: ", 5) == 0) {
        snprintf(ref_path, sizeof(ref_path), "%s/%s", PES_DIR, line + 5);
        f = fopen(ref_path, "r");
        if (!f) return -1; // Branch exists but has no commits yet
        if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
        fclose(f);
        line[strcspn(line, "\r\n")] = '\0';
    }
    return hex_to_hash(line, id_out);
}

// ─── head_update ──────────────────────────────────────────────────────────────

int head_update(const ObjectID *new_commit) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';

    char target_path[520];
    if (strncmp(line, "ref: ", 5) == 0) {
        snprintf(target_path, sizeof(target_path), "%s/%s", PES_DIR, line + 5);
    } else {
        snprintf(target_path, sizeof(target_path), "%s", HEAD_FILE); // Detached HEAD
    }

    // Ensure parent directory exists (e.g. .pes/refs/heads/)
    char dir[520];
    snprintf(dir, sizeof(dir), "%s", target_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        // mkdir -p equivalent (two levels deep is enough for refs/heads)
        mkdir(dir, 0755);
    }

    char tmp_path[528];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", target_path);

    f = fopen(tmp_path, "w");
    if (!f) return -1;

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);
    fprintf(f, "%s\n", hex);

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return rename(tmp_path, target_path);
}

// ─── commit_create ────────────────────────────────────────────────────────────
//
// Creates a new commit from the current staging area.
//
// Steps:
//   1. Load the index
//   2. Build a tree from the index → get root tree hash
//   3. Try to read the current HEAD as the parent commit (may not exist)
//   4. Fill in the Commit struct with tree, parent, author, timestamp, message
//   5. Serialize the commit struct to text
//   6. Write it to the object store as OBJ_COMMIT
//   7. Update HEAD to point to the new commit

int commit_create(const char *message, ObjectID *commit_id_out) {
    // 1. Load the index
    Index index;
    if (index_load(&index) != 0) {
        fprintf(stderr, "commit_create: failed to load index\n");
        return -1;
    }

    if (index.count == 0) {
        fprintf(stderr, "Nothing to commit (index is empty)\n");
        return -1;
    }

    // 2. Build tree from index
    ObjectID tree_id;
    if (tree_from_index(&index, &tree_id) != 0) {
        fprintf(stderr, "commit_create: failed to build tree\n");
        return -1;
    }

    // 3. Prepare commit struct
    Commit commit;
    memset(&commit, 0, sizeof(commit));

    commit.tree = tree_id;

    // Try to read current HEAD as parent
    ObjectID parent_id;
    if (head_read(&parent_id) == 0) {
        commit.parent     = parent_id;
        commit.has_parent = 1;
    } else {
        commit.has_parent = 0; // First commit — no parent
    }

    // 4. Fill author, timestamp, message
    snprintf(commit.author, sizeof(commit.author), "%s", pes_author());
    commit.timestamp = (uint64_t)time(NULL);
    snprintf(commit.message, sizeof(commit.message), "%s", message);

    // 5. Serialize commit to text
    void *commit_data;
    size_t commit_len;
    if (commit_serialize(&commit, &commit_data, &commit_len) != 0) {
        fprintf(stderr, "commit_create: serialize failed\n");
        return -1;
    }

    // 6. Write commit object
    if (object_write(OBJ_COMMIT, commit_data, commit_len, commit_id_out) != 0) {
        fprintf(stderr, "commit_create: object_write failed\n");
        free(commit_data);
        return -1;
    }
    free(commit_data);

    // 7. Update HEAD to point to new commit
    if (head_update(commit_id_out) != 0) {
        fprintf(stderr, "commit_create: head_update failed\n");
        return -1;
    }

    return 0;
}
