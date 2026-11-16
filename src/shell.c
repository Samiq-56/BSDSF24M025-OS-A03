#include "shell.h"
#include <readline/readline.h>
#include <readline/history.h>

/*
 * Feature 4 - input via GNU Readline (replaces the starter's getc loop).
 *
 * readline() prints the prompt, reads one line with full line editing,
 * and returns a malloc'd string (NULL on EOF / Ctrl-D, same contract as
 * before, so the main loop is unchanged). Tab completion and up/down
 * history come free: readline's default completion completes file and
 * command names, and the arrow keys cycle through readline's history
 * list, which the main loop feeds with add_history() after each command.
 *
 * The fp parameter is kept so the signature matches the starter code;
 * readline() always reads from stdin.
 */
char* read_cmd(char* prompt, FILE* fp) {
    (void) fp; /* readline() reads stdin itself */
    return readline(prompt);
}

char** tokenize(char* cmdline) {    // Edge case: empty command line
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        memset(arglist[i], 0, ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip leading whitespace
        
        if (*cp == '\0') break; // Line was only whitespace

        start = cp;
        len = 1;
        while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            len++;
        }
        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for(int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

/* Free every string tokenize() allocated, then the array itself. */
void free_arglist(char** arglist) {
    int i;
    if (arglist == NULL) {
        return;
    }
    for (i = 0; arglist[i] != NULL; i++) {
        free(arglist[i]);
    }
    free(arglist);
}

/*
 * Feature 3 - command history.
 *
 * Storage: a plain array of strings, history[HISTORY_SIZE], holding the
 * last HISTORY_SIZE (20) commands. hist_count is how many are stored so
 * far. When the array is full, the oldest entry (index 0) is freed and
 * every remaining entry shifts one slot left, so the newest command
 * always lands in the last slot. Line numbers shown by `history` are
 * 1-based: entry 1 is the oldest stored command.
 */
static char* history[HISTORY_SIZE];
static int hist_count = 0;

/* Store one command line. Empty / whitespace-only lines are not commands
   and are skipped, so they never take up a history slot. */
void add_to_history(const char* cmdline) {
    const char* p;
    char* copy;

    if (cmdline == NULL) {
        return;
    }
    p = cmdline;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') {
        return; /* nothing but whitespace: not a command */
    }

    copy = (char*) malloc(strlen(cmdline) + 1);
    if (copy == NULL) {
        return;
    }
    strcpy(copy, cmdline);

    if (hist_count < HISTORY_SIZE) {
        history[hist_count++] = copy;
    } else {
        /* Full: drop the oldest, shift the rest left, append at the end. */
        free(history[0]);
        memmove(&history[0], &history[1],
                sizeof(char*) * (HISTORY_SIZE - 1));
        history[HISTORY_SIZE - 1] = copy;
    }
}

int history_count(void) {
    return hist_count;
}

/* Print every stored command, each preceded by its line number from 1. */
void print_history(void) {
    int i;
    for (i = 0; i < hist_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

/*
 * expand_history - handle the "!n" re-execution syntax.
 *
 * Called from the main loop BEFORE tokenization and BEFORE the line is
 * added to history (the PDF requires this order: the literal "!n" text
 * is never stored). The returned string is a fresh copy of history
 * entry n; the caller replaces the command line with it and lets the
 * main loop proceed as if the user had typed that command directly
 * (so the re-executed command IS added to history, like in bash).
 *
 * Returns NULL (after printing an error) if n is not a number or is
 * out of bounds (n < 1 or n > number of stored commands).
 */
char* expand_history(const char* cmdline) {
    char* end;
    char* copy;
    long n = strtol(cmdline + 1, &end, 10);

    if (end == cmdline + 1 || *end != '\0') {
        fprintf(stderr, "myshell: %s: event not found\n", cmdline);
        return NULL;
    }
    if (n < 1 || n > hist_count) {
        fprintf(stderr, "myshell: !%ld: event not found\n", n);
        return NULL;
    }

    copy = (char*) malloc(strlen(history[n - 1]) + 1);
    if (copy == NULL) {
        return NULL;
    }
    strcpy(copy, history[n - 1]);
    return copy;
}