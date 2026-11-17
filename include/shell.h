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
#define MAX_JOBS 64     /* max tracked background jobs (Feature 6) */

// Function prototypes
char* read_cmd(char* prompt, FILE* fp);
char** tokenize(char* cmdline);
void free_arglist(char** arglist);
/* background = 1: do not waitpid(), record the child as a job (F6). */
int execute(char** arglist, int background);
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

/* Feature 6 - background jobs (src/jobs.c) */
int add_job(pid_t pid, const char* cmd); /* returns job number, or -1 */
void remove_job(pid_t pid);
void print_jobs(void);   /* the `jobs` built-in */
void reap_zombies(void); /* waitpid(-1, WNOHANG) loop before each prompt */

/* Feature 8 - shell variables (src/variables.c, linked list) */
void set_variable(const char* name, const char* value);
const char* get_variable(const char* name); /* NULL if undefined;
                                                falls back to getenv() */
void print_variables(void); /* the `set` built-in */
int is_assignment_line(const char* line); /* VARNAME=value ? */
void handle_assignment(const char* line); /* store it, no fork */
void expand_variables(char** arglist); /* $VARNAME -> value, after
                                          tokenizing, before executing */

#endif // SHELL_H
