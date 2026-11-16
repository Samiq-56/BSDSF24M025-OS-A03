#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h> /* open(), O_RDONLY / O_WRONLY / O_CREAT / O_TRUNC (F5) */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_LEN 512
#define MAXARGS 10
#define ARGLEN 30
#define PROMPT "FCIT> "
#define HISTORY_SIZE 20 /* keep at least the last 20 commands (Feature 3) */

// Function prototypes
char* read_cmd(char* prompt, FILE* fp);
char** tokenize(char* cmdline);
void free_arglist(char** arglist);
int execute(char** arglist);
/* Feature 5: run "left | right" as a two-process pipeline. */
int execute_pipe(char** left, char** right);
int handle_builtin(char** arglist);

/* Feature 3 - command history */
void add_to_history(const char* cmdline);
int history_count(void);
void print_history(void);
/* Expand a "!n" line into a copy of history entry n (1-based).
   Returns NULL and prints an error if n is not a valid history number. */
char* expand_history(const char* cmdline);

#endif // SHELL_H
