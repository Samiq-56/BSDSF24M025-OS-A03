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

        if ((arglist = tokenize(cmdline)) != NULL) {
            /* Built-ins run in the shell itself, before any fork().
               Only external commands reach execute(). */
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            // Free the memory allocated by tokenize()
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}