#include "pes.h"
#include "commit.h"
#include "index.h"
#include "tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/**
 * Commit format (text):
 * 
 * tree <40-char-hash>
 * parent <40-char-hash>
 * author <name> <email> <timestamp>
 * committer <name> <email> <timestamp>
 * 
 * <blank line>
 * <commit message>
 * 
 * Note: parent line is omitted for first commit
 */

/**
 * head_read: Read the current HEAD commit hash
 * 
 * 1. Read .pes/HEAD - contains "ref: refs/heads/main"
 * 2. Follow that reference
 * 3. Read and return the commit hash
 */
char *head_read() {
    FILE *f = fopen(".pes/HEAD", "r");
    if (!f) {
        return NULL;  // HEAD doesn't exist (first commit)
    }
    
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    
    // Remove trailing newline
    line[strcspn(line, "\n")] = '\0';
    
    // Parse "ref: refs/heads/main"
    char *ref_path = NULL;
    if (strncmp(line, "ref: ", 5) == 0) {
        ref_path = line + 5;
        
        // Read the referenced file
        f = fopen(ref_path, "r");
        if (!f) {
            return NULL;  // Branch doesn't exist yet
        }
        
        char hash_line[65];
        if (!fgets(hash_line, sizeof(hash_line), f)) {
            fclose(f);
            return NULL;
        }
        fclose(f);
        
        // Remove trailing newline
        hash_line[strcspn(hash_line, "\n")] = '\0';
        
        char *result = malloc(65);
        strcpy(result, hash_line);
        return result;
    }
    
    // Direct hash in HEAD
    char *result = malloc(65);
    strcpy(result, line);
    return result;
}

/**
 * head_update: Update HEAD to point to a new commit hash
 * 
 * Atomically updates the branch ref to the new commit hash.
 */
void head_update(const char *commit_hash) {
    // Read current HEAD to get the branch name
    FILE *f = fopen(".pes/HEAD", "r");
    if (!f) {
        // First time - create HEAD and main branch
        f = fopen(".pes/HEAD", "w");
        fprintf(f, "ref: refs/heads/main\n");
        fclose(f);
        
        mkdir(".pes/refs", 0755);
        mkdir(".pes/refs/heads", 0755);
        
        f = fopen(".pes/refs/heads/main", "w");
        fprintf(f, "%s\n", commit_hash);
        fclose(f);
        return;
    }
    
    char line[256];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return;
    }
    fclose(f);
    
    // Remove trailing newline
    line[strcspn(line, "\n")] = '\0';
    
    // Parse the ref
    char *ref_path = NULL;
    if (strncmp(line, "ref: ", 5) == 0) {
        ref_path = line + 5;
    } else {
        // Direct hash - update it
        f = fopen(".pes/HEAD", "w");
        fprintf(f, "%s\n", commit_hash);
        fclose(f);
        return;
    }
    
    // Write to temp file
    FILE *temp_f = fopen(".pes/.refs.tmp", "w");
    fprintf(temp_f, "%s\n", commit_hash);
    fflush(temp_f);
    fsync(fileno(temp_f));
    fclose(temp_f);
    
    // Atomically rename
    rename(".pes/.refs.tmp", ref_path);
}

/**
 * commit_create: Create a commit object
 * 
 * 1. Build tree from index
 * 2. Get parent commit (may be NULL for first commit)
 * 3. Build commit text
 * 4. Write commit object
 * 5. Update HEAD
 * 6. Return commit hash
 */
char *commit_create(Index *index, const char *message) {
    // Build tree from index
    char *tree_hash = tree_from_index(index);
    if (!tree_hash) {
        fprintf(stderr, "error: cannot create tree from index\n");
        return NULL;
    }
    
    // Get parent commit (may be NULL)
    char *parent_hash = head_read();
    
    // Get author and timestamp
    const char *author = pes_author();
    time_t now = time(NULL);
    
    // Build commit text
    char commit_text[4096];
    char *p = commit_text;
    
    // tree line
    p += sprintf(p, "tree %s\n", tree_hash);
    
    // parent line (if not first commit)
    if (parent_hash) {
        p += sprintf(p, "parent %s\n", parent_hash);
    }
    
    // author line
    p += sprintf(p, "author %s %ld\n", author, now);
    
    // committer line
    p += sprintf(p, "committer %s %ld\n", author, now);
    
    // blank line
    p += sprintf(p, "\n");
    
    // message
    p += sprintf(p, "%s", message);
    
    size_t commit_len = p - commit_text;
    
    // Write commit object
    char *commit_hash = object_write("commit", commit_text, commit_len);
    if (!commit_hash) {
        fprintf(stderr, "error: cannot write commit object\n");
        free(tree_hash);
        if (parent_hash) free(parent_hash);
        return NULL;
    }
    
    // Update HEAD
    head_update(commit_hash);
    
    free(tree_hash);
    if (parent_hash) free(parent_hash);
    
    return commit_hash;
}

/**
 * commit_parse: Parse commit text into Commit struct
 * (PROVIDED - do not modify)
 */
Commit *commit_parse(const unsigned char *data, size_t len) {
    Commit *commit = malloc(sizeof(Commit));
    memset(commit, 0, sizeof(Commit));
    
    const char *text = (const char *)data;
    const char *end = text + len;
    
    // Parse headers
    while (text < end) {
        const char *newline = strchr(text, '\n');
        if (!newline) break;
        
        if (text[0] == '\n') {
            // Blank line - message starts after
            text++;
            size_t msg_len = end - text;
            commit->message = malloc(msg_len + 1);
            strncpy(commit->message, text, msg_len);
            commit->message[msg_len] = '\0';
            break;
        }
        
        // Parse header line
        if (strncmp(text, "tree ", 5) == 0) {
            sscanf(text + 5, "%64s", commit->tree);
        } else if (strncmp(text, "parent ", 7) == 0) {
            sscanf(text + 7, "%64s", commit->parent);
        } else if (strncmp(text, "author ", 7) == 0) {
            // author <name> <email> <timestamp>
            const char *space1 = strchr(text + 7, ' ');
            const char *space2 = strrchr(text, ' ');
            if (space1 && space2) {
                size_t name_len = space1 - (text + 7);
                strncpy(commit->author_name, text + 7, name_len);
                commit->author_name[name_len] = '\0';
                
                size_t email_len = space2 - space1 - 2;  // Skip "< "
                strncpy(commit->author_email, space1 + 2, email_len - 1);
                
                sscanf(space2 + 1, "%ld", &commit->author_time);
            }
        }
        
        text = newline + 1;
    }
    
    return commit;
}

/**
 * commit_serialize: Convert Commit struct to text
 * (PROVIDED - do not modify)
 */
unsigned char *commit_serialize(Commit *commit, size_t *len_out) {
    char buffer[4096];
    char *p = buffer;
    
    p += sprintf(p, "tree %s\n", commit->tree);
    if (strlen(commit->parent) > 0) {
        p += sprintf(p, "parent %s\n", commit->parent);
    }
    p += sprintf(p, "author %s <%s> %ld\n", commit->author_name, commit->author_email, commit->author_time);
    p += sprintf(p, "committer %s <%s> %ld\n", commit->author_name, commit->author_email, commit->author_time);
    p += sprintf(p, "\n%s", commit->message);
    
    size_t len = p - buffer;
    unsigned char *result = malloc(len);
    memcpy(result, buffer, len);
    *len_out = len;
    
    return result;
}

/**
 * commit_walk: Walk commit history and call callback for each
 * (PROVIDED - do not modify)
 */
void commit_walk(const char *start_hash, void (*callback)(Commit *)) {
    if (!start_hash) return;
    
    char current_hash[65];
    strcpy(current_hash, start_hash);
    
    while (strlen(current_hash) > 0) {
        char type[64];
        size_t len;
        unsigned char *data = object_read(current_hash, type, &len);
        
        if (!data || strcmp(type, "commit") != 0) {
            break;
        }
        
        Commit *commit = commit_parse(data, len);
        callback(commit);
        
        free(data);
        
        if (strlen(commit->parent) == 0) {
            free(commit);
            break;
        }
        
        strcpy(current_hash, commit->parent);
        free(commit);
    }
}

/**
 * commit_free: Free a commit
 */
void commit_free(Commit *commit) {
    if (!commit) return;
    if (commit->message) free(commit->message);
    free(commit);
}
