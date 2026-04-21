// Add this to pes.c - the cmd_commit function and related code

#include "pes.h"
#include "commit.h"
#include "index.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/**
 * cmd_init: Initialize a new repository
 * (PROVIDED - do not modify)
 */
void cmd_init() {
    mkdir(".pes", 0755);
    mkdir(".pes/objects", 0755);
    mkdir(".pes/refs", 0755);
    mkdir(".pes/refs/heads", 0755);
    
    FILE *f = fopen(".pes/HEAD", "w");
    fprintf(f, "ref: refs/heads/main\n");
    fclose(f);
    
    printf("Initialized empty PES repository in .pes/\n");
}

/**
 * cmd_add: Stage files
 * (PROVIDED - do not modify)
 */
void cmd_add(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: pes add <file>...\n");
        return;
    }
    
    Index *index = index_load();
    
    for (int i = 2; i < argc; i++) {
        index_add(index, argv[i]);
    }
    
    index_free(index);
}

/**
 * cmd_status: Show status
 * (PROVIDED - do not modify)
 */
void cmd_status() {
    Index *index = index_load();
    index_status(index);
    index_free(index);
}

/**
 * cmd_commit: Create a commit
 * 
 * Usage: pes commit -m "message"
 */
void cmd_commit(int argc, char *argv[]) {
    const char *message = NULL;
    
    // Parse -m flag
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            message = argv[i + 1];
            break;
        }
    }
    
    if (!message) {
        fprintf(stderr, "error: commit requires a message (-m \"message\")\n");
        return;
    }
    
    // Load index
    Index *index = index_load();
    
    // Create commit
    char *commit_hash = commit_create(index, message);
    
    if (commit_hash) {
        // Print the first 12 characters of the hash
        printf("Committed: %.12s... %s\n", commit_hash, message);
        free(commit_hash);
    } else {
        fprintf(stderr, "error: failed to create commit\n");
    }
    
    index_free(index);
}

/**
 * commit_callback: Callback for commit_walk (used by cmd_log)
 * (PROVIDED - do not modify)
 */
void commit_callback(Commit *commit) {
    printf("commit %s\n", commit->parent);
    printf("Author: %s <%s>\n", commit->author_name, commit->author_email);
    
    // Format timestamp
    time_t t = commit->author_time;
    struct tm *tm = localtime(&t);
    printf("Date:   %s", asctime(tm));
    
    printf("\n    %s\n\n", commit->message);
}

/**
 * cmd_log: Show commit history
 * (PROVIDED - do not modify)
 */
void cmd_log() {
    char *head = head_read();
    if (!head) {
        printf("fatal: not a valid repository or HEAD not found\n");
        return;
    }
    
    commit_walk(head, commit_callback);
    free(head);
}

/**
 * main: Entry point
 * (PROVIDED - do not modify)
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: pes <command> [<args>]\n");
        fprintf(stderr, "commands:\n");
        fprintf(stderr, "    init              Initialize repository\n");
        fprintf(stderr, "    add <file>...     Stage files\n");
        fprintf(stderr, "    status            Show status\n");
        fprintf(stderr, "    commit -m <msg>   Create commit\n");
        fprintf(stderr, "    log               Show history\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "init") == 0) {
        cmd_init();
    } else if (strcmp(cmd, "add") == 0) {
        cmd_add(argc, argv);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcmp(cmd, "commit") == 0) {
        cmd_commit(argc, argv);
    } else if (strcmp(cmd, "log") == 0) {
        cmd_log();
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        return 1;
    }
    
    return 0;
}
