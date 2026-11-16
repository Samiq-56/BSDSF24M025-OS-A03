#include "shell.h"
#include <readline/history.h> /* Feature 4: add_history() */

int main() {
    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {
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

        /* Feature 3: store the command (empty lines are skipped inside). */
        add_to_history(cmdline);

        /* Feature 4: the same (resolved) line goes into readline's own
           history list, so the up/down arrow keys cycle through past
           commands. Whitespace-only lines are skipped, like Feature 3. */
        {
            const char* p = cmdline;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0') {
                add_history(cmdline);
            }
        }

        /* Feature 5: pipe. If the line contains '|', split it into two
           commands at the pipe, tokenize each side separately and run
           them through execute_pipe(). The split happens here, before
           tokenize(), so neither side's argument list contains '|'.
           (Only one pipe per line is supported; a second '|' would land
           inside the right side's token and fail as "Command not found".
           Built-ins do not work inside a pipe - both sides are exec'd.) */
        {
            char* pipe_pos = strchr(cmdline, '|');
            if (pipe_pos != NULL) {
                char** left;
                char** right;
                *pipe_pos = '\0'; /* cut the line in two, in place */
                left = tokenize(cmdline);
                right = tokenize(pipe_pos + 1);
                if (left != NULL && right != NULL) {
                    execute_pipe(left, right);
                } else {
                    fprintf(stderr, "myshell: syntax error near '|'\n");
                }
                free_arglist(left);
                free_arglist(right);
                free(cmdline);
                continue;
            }
        }

        if ((arglist = tokenize(cmdline)) != NULL) {
            /* Built-ins run in the shell itself, before any fork().
               Only external commands reach execute(). */
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            free_arglist(arglist);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}