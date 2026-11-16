#include "shell.h"
#include <readline/history.h> /* Feature 4: add_history() */

/*
 * Feature 6 - command chaining (;) and background execution (&).
 *
 * The main loop splits each input line into segments at ';' and '&'
 * (both act as separators). A segment ending at '&' runs in the
 * background; a segment ending at ';' (or at end-of-line) runs in the
 * foreground. Each segment is then handled by run_single_command(),
 * which reuses the existing pipe / built-in / execute logic:
 *
 *   "echo first ; sleep 2 ; echo second"  - three foreground commands,
 *       run one after another (execute() waitpid()s each one).
 *   "sleep 10 &"                          - background: execute() does
 *       NOT waitpid(), records the child in the jobs list, and the
 *       prompt returns at once. reap_zombies() (waitpid(-1, WNOHANG)
 *       loop, called before every prompt) collects the child when it
 *       finishes, so no zombie is left behind.
 *   "sleep 5 & echo done"                 - '&' also separates, so the
 *       sleep runs in the background and echo runs right away.
 *
 * Notes (viva): '&&' / '||' are NOT implemented - "a && b" behaves
 * like "a & b" (the empty segment between the two & is skipped).
 * A built-in followed by '&' (e.g. "cd /tmp &") just runs in the
 * foreground: built-ins execute in the shell process itself, so there
 * is no child to put in the background. A background pipe
 * ("cat file | grep x &") is run in one extra forked child that
 * calls execute_pipe() and exits; the parent tracks that child PID.
 */

/* Skip leading blanks and cut trailing blanks off a segment, in place. */
static char* trim_segment(char* s) {
    char* end;
    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) {
        *--end = '\0';
    }
    return s;
}

/*
 * run_single_command - execute one ;- or &-separated segment.
 * background = 1 if the segment was terminated by '&'.
 */
static void run_single_command(char* segment, int background) {
    char* pipe_pos;
    char** arglist;

    segment = trim_segment(segment);
    if (*segment == '\0') {
        return; /* empty segment, e.g. a leading ';' or "a && b" */
    }

    /* Feature 5: pipe. If the segment contains '|', split it into two
       commands at the pipe, tokenize each side separately and run them
       through execute_pipe(). (Only one pipe per segment; built-ins do
       not work inside a pipe - both sides are exec'd.) */
    pipe_pos = strchr(segment, '|');
    if (pipe_pos != NULL) {
        char** left;
        char** right;
        *pipe_pos = '\0'; /* cut the segment in two, in place */
        left = tokenize(segment);
        right = tokenize(pipe_pos + 1);
        *pipe_pos = '|'; /* restore: tokenize() copied the tokens, and
                            the jobs list should show the full pipeline */
        if (left != NULL && right != NULL) {
            if (background) {
                /* Run the whole pipeline in one forked child so the
                   shell itself is not blocked by execute_pipe()'s
                   waitpid() calls; track that child as the job. */
                pid_t pid = fork();
                if (pid == -1) {
                    perror("fork failed");
                } else if (pid == 0) {
                    execute_pipe(left, right);
                    _exit(0); /* never return into the main loop */
                } else {
                    int jobnum = add_job(pid, segment);
                    if (jobnum > 0) {
                        printf("[%d] %d\n", jobnum, (int) pid);
                        fflush(stdout); /* see execute.c: flush before
                                           the next fork inherits the
                                           stdio buffer */
                    }
                }
            } else {
                execute_pipe(left, right);
            }
        } else {
            fprintf(stderr, "myshell: syntax error near '|'\n");
        }
        free_arglist(left);
        free_arglist(right);
        return;
    }

    if ((arglist = tokenize(segment)) != NULL) {
        /* Built-ins run in the shell itself, before any fork().
           Only external commands reach execute(). */
        if (!handle_builtin(arglist)) {
            execute(arglist, background);
        }
        free_arglist(arglist);
    }
}

int main() {
    char* cmdline;

    while (1) {
        /* Feature 6: reap finished background children BEFORE the next
           prompt, so zombies never accumulate and `jobs` stays fresh. */
        reap_zombies();

        if ((cmdline = read_cmd(PROMPT, stdin)) == NULL) {
            break; /* EOF (Ctrl-D) */
        }

        /* Feature 3: "!n" re-execution, handled BEFORE tokenization
           and BEFORE adding anything to history. */
        if (cmdline[0] == '!') {
            char* resolved = expand_history(cmdline);
            if (resolved == NULL) {
                /* Bad history number: error already printed, skip line. */
                free(cmdline);
                continue;
            }
            free(cmdline);
            cmdline = resolved; /* proceed as if the user typed it */
        }

        /* Feature 3: store the whole line (empty lines skipped inside).
           Feature 4: the same (resolved) line goes into readline's own
           history list, so up/down arrow keys cycle through past
           commands. */
        add_to_history(cmdline);
        {
            const char* p = cmdline;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0') {
                add_history(cmdline);
            }
        }

        /* Feature 6: split the line into segments at ';' and '&' and
           run them in order. The delimiter decides the segment's mode:
           '&' = background, ';' (or end-of-line) = foreground. */
        {
            char* rest = cmdline;
            while (rest != NULL) {
                char* delim = rest;
                char* segment;
                int background = 0;
                while (*delim != '\0' && *delim != ';' && *delim != '&') {
                    delim++;
                }
                if (*delim == '&') {
                    background = 1;
                }
                segment = rest;
                if (*delim == '\0') {
                    rest = NULL;
                } else {
                    *delim = '\0'; /* cut the segment off, in place */
                    rest = delim + 1;
                }
                run_single_command(segment, background);
            }
        }

        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
