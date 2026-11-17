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
 *
 * Feature 7 - if-then-else-fi (multi-line, see run_if_statement()).
 * When a line starts with the keyword "if", the main loop does NOT
 * split it at ';'/'&'; instead run_if_statement() reads further lines
 * (secondary prompt "> ") until a line starting with "fi", stores the
 * lines between "then"/"else" in two separate arrays, runs the
 * condition command, and then runs the chosen block. The branch is
 * picked from the condition's exit status via WEXITSTATUS(): 0 runs
 * the then-block, non-zero runs the else-block (else is optional).
 * Nested if-blocks are NOT supported: a line starting with "if"
 * inside a block would be run as an ordinary (unknown) command.
 */

/* Feature 7: max lines stored per then/else block. */
#define MAX_IF_LINES 32

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

/* True if the first whitespace-delimited word of s is exactly word
   (so "ifx" does not match "if", and "fi" does not match "fish"). */
static int first_token_is(const char* s, const char* word) {
    size_t n = strlen(word);
    return strncmp(s, word, n) == 0 &&
           (s[n] == ' ' || s[n] == '\t' || s[n] == '\0');
}

/* strdup() without relying on POSIX feature macros (we build -std=c11). */
static char* dup_string(const char* s) {
    char* copy = (char*) malloc(strlen(s) + 1);
    if (copy != NULL) {
        strcpy(copy, s);
    }
    return copy;
}

/*
 * run_single_command - execute one ;- or &-separated segment.
 * background = 1 if the segment was terminated by '&'.
 * Returns the command's exit status: 0 for a handled built-in or a
 * background job (whose status is not known yet), otherwise the
 * child's exit code from execute()/execute_pipe() (Feature 7 needs
 * this to decide the if-branch: 0 = success = then-block).
 */
static int run_single_command(char* segment, int background) {
    char* pipe_pos;
    char** arglist;

    segment = trim_segment(segment);
    if (*segment == '\0') {
        return 0; /* empty segment, e.g. a leading ';' or "a && b" */
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
                free_arglist(left);
                free_arglist(right);
                return 0; /* background: status not known yet */
            } else {
                int status = execute_pipe(left, right);
                free_arglist(left);
                free_arglist(right);
                return status;
            }
        } else {
            fprintf(stderr, "myshell: syntax error near '|'\n");
        }
        free_arglist(left);
        free_arglist(right);
        return 1;
    }

    if ((arglist = tokenize(segment)) != NULL) {
        int status;
        /* Built-ins run in the shell itself, before any fork().
           Only external commands reach execute(). */
        if (handle_builtin(arglist)) {
            status = 0; /* handled built-in: success (e.g. "cd /tmp"
                           as an if-condition counts as true) */
        } else {
            status = execute(arglist, background);
        }
        free_arglist(arglist);
        return status;
    }
    return 0;
}

/*
 * run_line - split one input line into segments at ';' and '&' and
 * run them in order (Feature 6). The delimiter decides the segment's
 * mode: '&' = background, ';' (or end-of-line) = foreground.
 * Returns the exit status of the LAST segment executed, so a line
 * used as an if-condition ("a; b") is judged by its final command.
 */
static int run_line(char* line) {
    char* rest = line;
    int last_status = 0;

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
        last_status = run_single_command(segment, background);
    }
    return last_status;
}

/*
 * run_if_statement - Feature 7: multi-line if-then-else-fi.
 *
 * Called with the full "if <condition>" line. Reads further lines
 * (secondary prompt "> ") until a line whose first word is "fi",
 * stores the then-block and else-block lines in two separate arrays,
 * then runs the condition and executes the chosen block.
 *
 *   if <command>        <- condition, run first
 *   then                <- marks the start of the then-block
 *   <commands...>       <- run if the condition exits 0
 *   else                <- optional; marks the else-block
 *   <commands...>       <- run if the condition exits non-zero
 *   fi                  <- end of the whole statement
 *
 * The block lines are stored as copies (they are re-run through
 * run_line(), so ';', '&' and '|' work inside a block exactly as on
 * a normal command line). Continuation lines are added to history,
 * like in bash. Returns 0 on success, 1 on a syntax error (in which
 * case nothing is executed).
 */
static int run_if_statement(char* if_line) {
    char* cond;
    char* then_lines[MAX_IF_LINES];
    char* else_lines[MAX_IF_LINES];
    int n_then = 0, n_else = 0;
    int seen_then = 0, in_else = 0, seen_fi = 0, ok = 1;
    char* line;
    int i, status;

    /* Condition = everything after the "if" keyword on this line. */
    cond = if_line;
    while (*cond == ' ' || *cond == '\t') cond++;
    cond += 2; /* skip "if" */
    cond = trim_segment(cond);
    if (*cond == '\0') {
        fprintf(stderr, "myshell: syntax error: expected a command after 'if'\n");
        ok = 0;
    }

    /* Read continuation lines until "fi" (or EOF / Ctrl-D). */
    while ((line = read_cmd("> ", stdin)) != NULL) {
        char* t = trim_segment(line);

        /* Keep continuation lines in both histories, like bash. */
        add_to_history(line);
        {
            const char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0') {
                add_history(line);
            }
        }

        if (first_token_is(t, "fi")) {
            seen_fi = 1;
            free(line);
            break;
        }
        if (first_token_is(t, "then")) {
            seen_then = 1;
            in_else = 0;
            free(line);
            continue;
        }
        if (first_token_is(t, "else")) {
            if (!seen_then) {
                fprintf(stderr, "myshell: syntax error: 'else' before 'then'\n");
                ok = 0;
            }
            in_else = 1;
            free(line);
            continue;
        }
        if (!seen_then) {
            fprintf(stderr, "myshell: syntax error: expected 'then'\n");
            ok = 0;
            free(line);
            continue;
        }
        if (!in_else) {
            if (n_then < MAX_IF_LINES) {
                then_lines[n_then++] = dup_string(line);
            }
        } else {
            if (n_else < MAX_IF_LINES) {
                else_lines[n_else++] = dup_string(line);
            }
        }
        free(line);
    }

    if (!seen_fi) {
        fprintf(stderr, "myshell: syntax error: unexpected end of input (expected 'fi')\n");
        ok = 0;
    }
    if (ok && !seen_then) {
        fprintf(stderr, "myshell: syntax error: missing 'then'\n");
        ok = 0;
    }

    if (ok) {
        /* Run the condition; WEXITSTATUS (inside execute()) decides
           the branch: 0 = success = then-block, non-zero = else-block. */
        status = run_line(cond);
        if (status == 0) {
            for (i = 0; i < n_then; i++) {
                if (then_lines[i] != NULL) {
                    run_line(then_lines[i]);
                }
            }
        } else {
            for (i = 0; i < n_else; i++) {
                if (else_lines[i] != NULL) {
                    run_line(else_lines[i]);
                }
            }
        }
    }

    for (i = 0; i < n_then; i++) {
        free(then_lines[i]);
    }
    for (i = 0; i < n_else; i++) {
        free(else_lines[i]);
    }
    return ok ? 0 : 1;
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

        /* Feature 7: a line starting with the keyword "if" opens a
           multi-line if-then-else-fi block; it is read and executed as
           one unit, NOT split at ';'/'&' here. Any other line goes
           through run_line() (Feature 6 chaining / background). */
        {
            const char* p = cmdline;
            while (*p == ' ' || *p == '\t') p++;
            if (first_token_is(p, "if")) {
                run_if_statement(cmdline);
            } else {
                run_line(cmdline);
            }
        }

        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
