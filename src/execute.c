#include "shell.h"

/*
 * Feature 5 - I/O redirection (< and >) and pipes (|).
 *
 * The idea, in one line: a file descriptor is just a number, and
 * dup2() lets us point the standard numbers (0 = stdin, 1 = stdout)
 * at a file or at a pipe end. The redirection is always set up in the
 * CHILD, after fork() but before execvp(), so the shell's own stdin /
 * stdout are never touched.
 *
 * Redirection (handled inside execute()):
 *   "cmd > file"  - open(file, O_WRONLY|O_CREAT|O_TRUNC, 0644), then
 *                   dup2(fd, STDOUT_FILENO): the command's stdout
 *                   writes into the file (created if missing, emptied
 *                   if it already exists).
 *   "cmd < file"  - open(file, O_RDONLY), then dup2(fd, STDIN_FILENO):
 *                   the command reads its stdin from the file.
 *   The ">" / "<" tokens and their filenames are stripped out of the
 *   argument list before execvp(), so the program never sees them.
 *   Redirection applies to external commands only; a built-in with
 *   ">" is handled by handle_builtin() before execute() is reached.
 *
 * Pipes (execute_pipe(), called from main when the line contains '|'):
 *   pipe() in the parent creates a read/write fd pair. Two forks make
 *   the two children: the left child dup2()s the pipe's write end onto
 *   its stdout, the right child dup2()s the pipe's read end onto its
 *   stdin. Each side may also redirect (< / >) - the same parser runs
 *   in each child. ALL unused pipe ends are closed in ALL THREE
 *   processes (both children + parent) - if the parent kept the write
 *   end open, the reader would never see EOF and the pipe would hang.
 */

/*
 * parse_redirection - shared helper for execute() and execute_pipe().
 *
 * Scans arglist for '<' / '>' operators, records the filenames, and
 * builds a clean argv (operators + filenames removed) for execvp().
 * Returns the clean argc, or -1 (after printing an error) if a
 * filename is missing. argv must hold at least MAXARGS + 1 entries.
 */
static int parse_redirection(char** arglist, char** argv,
                             char** infile, char** outfile) {
    int argc = 0;
    int i;

    *infile = NULL;
    *outfile = NULL;
    for (i = 0; arglist[i] != NULL; i++) {
        if (strcmp(arglist[i], ">") == 0) {
            if (arglist[i + 1] == NULL) {
                fprintf(stderr, "myshell: syntax error: missing filename after '>'\n");
                return -1;
            }
            *outfile = arglist[++i]; /* skip the filename too */
        } else if (strcmp(arglist[i], "<") == 0) {
            if (arglist[i + 1] == NULL) {
                fprintf(stderr, "myshell: syntax error: missing filename after '<'\n");
                return -1;
            }
            *infile = arglist[++i];
        } else {
            argv[argc++] = arglist[i];
        }
    }
    argv[argc] = NULL;
    return argc;
}

/*
 * setup_redirection - open the files and dup2() them onto stdin/stdout.
 * Call in the CHILD, after fork() but before execvp(). Exits the child
 * (status 1) if a file cannot be opened, so the shell itself survives.
 */
static void setup_redirection(const char* infile, const char* outfile) {
    if (infile != NULL) {
        int fd = open(infile, O_RDONLY);
        if (fd < 0) {
            perror(infile);
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd); /* the original fd is no longer needed */
    }
    if (outfile != NULL) {
        int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(outfile);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

/*
 * execute - fork + execvp one external command.
 *
 * background = 0 (foreground): the parent waitpid()s for the child
 *   before returning, so the shell blocks until the command finishes.
 *   Returns the child's exit status (WEXITSTATUS): Feature 7's
 *   if-condition is judged by this value (0 = success).
 * background = 1 (Feature 6, command ended with '&'): the parent
 *   does NOT wait - it records the child's PID in the jobs list with
 *   add_job(), prints "[n] <pid>" like bash, and returns at once so
 *   the prompt comes back immediately. The child is reaped later by
 *   reap_zombies() (waitpid(-1, WNOHANG) before each prompt).
 *   Returns 0 (the job's status is not known yet).
 */
int execute(char* arglist[], int background) {
    int status;
    int cpid;
    char* infile;
    char* outfile;
    char* argv[MAXARGS + 1]; /* arglist minus redirection tokens */
    int argc = parse_redirection(arglist, argv, &infile, &outfile);

    if (argc < 0) {
        return 1; /* syntax error already printed */
    }
    if (argc == 0) {
        /* The line was only redirections, e.g. "> file": nothing to run. */
        return 0;
    }

    cpid = fork();

    switch (cpid) {
        case -1:
            perror("fork failed");
            exit(1);
        case 0: // Child process
            setup_redirection(infile, outfile);
            execvp(argv[0], argv);
            perror("Command not found"); // This line runs only if execvp fails
            exit(1);
        default: // Parent process
            if (background) {
                /* Build a display string for the jobs list by joining
                   the clean argv back together with spaces. */
                char cmdtext[MAX_LEN];
                int n = 0, i;
                cmdtext[0] = '\0';
                for (i = 0; i < argc; i++) {
                    n += snprintf(cmdtext + n, (size_t) (MAX_LEN - n),
                                  "%s%s", i > 0 ? " " : "", argv[i]);
                    if (n >= MAX_LEN - 1) {
                        break; /* truncated, still NUL-terminated */
                    }
                }
                {
                    int jobnum = add_job(cpid, cmdtext);
                    if (jobnum > 0) {
                        printf("[%d] %d\n", jobnum, (int) cpid);
                        /* Flush now: the next forked child inherits
                           the stdio buffer, and without this the
                           "[n] pid" line could appear after the
                           next command's output in piped runs. */
                        fflush(stdout);
                    }
                }
                return 0; /* prompt returns at once, no waiting */
            }
            waitpid(cpid, &status, 0);
            // printf("Child pid:%d exited with status %d\n", cpid, status >> 8);
            /* Feature 7: report the child's real exit code so an
               if-condition can be judged (0 = success). A child
               killed by a signal counts as failure (status 1). */
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
}

/*
 * execute_pipe - run "left | right".
 *
 * Parent calls pipe() once, then forks twice. Left child writes into
 * the pipe, right child reads from it, parent closes both ends (so
 * the reader gets EOF when the writer exits) and waits for both.
 * Returns the RIGHT child's exit status (bash rule: a pipeline's
 * status is its last command's status) - Feature 7 judges a piped
 * if-condition by this value.
 */
int execute_pipe(char** left, char** right) {
    int pipefd[2]; /* pipefd[0] = read end, pipefd[1] = write end */
    pid_t left_pid, right_pid;
    int left_status, right_status;

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return 1;
    }

    left_pid = fork();
    if (left_pid == -1) {
        perror("fork failed");
        return 1;
    }
    if (left_pid == 0) {
        /* Left child (writer): stdout -> pipe write end. */
        char* argv[MAXARGS + 1];
        char* infile;
        char* outfile;
        close(pipefd[0]); /* we never read */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]); /* dup2 made a copy; close the original */
        /* A side may also redirect, e.g. "cat < file | grep x". */
        if (parse_redirection(left, argv, &infile, &outfile) < 0) {
            exit(1);
        }
        setup_redirection(infile, outfile);
        execvp(argv[0], argv);
        perror("Command not found");
        exit(1);
    }

    right_pid = fork();
    if (right_pid == -1) {
        perror("fork failed");
        return 1;
    }
    if (right_pid == 0) {
        /* Right child (reader): stdin <- pipe read end. */
        char* argv[MAXARGS + 1];
        char* infile;
        char* outfile;
        close(pipefd[1]); /* we never write */
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        if (parse_redirection(right, argv, &infile, &outfile) < 0) {
            exit(1);
        }
        setup_redirection(infile, outfile);
        execvp(argv[0], argv);
        perror("Command not found");
        exit(1);
    }

    /* Parent: close both ends (both children have their own copies),
       otherwise the right child would never see end-of-input. */
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(left_pid, &left_status, 0);
    waitpid(right_pid, &right_status, 0);
    return WIFEXITED(right_status) ? WEXITSTATUS(right_status) : 1;
}
