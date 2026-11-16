# OS Assignment 03 — Custom UNIX Shell: Implementation Report

**Student:** Usman Hassan (BSDSF24M025)
**Course:** Operating Systems, FCIT (University of the Punjab)
**Repository:** [UsmanXTech/BSDSF24M025-OS-A03](https://github.com/UsmanXTech/BSDSF24M025-OS-A03)
**Instructor:** Muhammad Arif Butt, PhD

This report documents every feature of the shell, how it was implemented
(key functions and system calls), and the test evidence for each release.
One section per feature, in release order.

---

## Feature 1 — The Base Shell (Release 1, tag `v1.0-base`, 5 marks)

**What was done:** Set up the public repository `UsmanXTech/BSDSF24M025-OS-A03`
and populated it with the instructor's unmodified starter code from
`arifpucit/OS-Codes`, `Programming-Assignments/PA-03/base-assignment-03`
(`src/main.c`, `src/shell.c`, `src/execute.c`, `include/shell.h`).

**How it works:** The main loop in `main.c` repeats three steps —
`read_cmd()` reads one line and prints the `FCIT> ` prompt, `tokenize()`
splits it into an argument list, and `execute()` runs it with the classic
fork–exec–wait cycle (`fork()`, child calls `execvp()`, parent `waitpid()`).
Limits: `MAX_LEN` 512 characters per line, `MAXARGS` 10 arguments,
`ARGLEN` 30 characters per argument.

**Build notes:** The instructor's base project ships no `Makefile`, so one
was written (in the style of PA-02): sources in `src/`, headers in
`include/`, objects in `obj/`, final binary `bin/myshell` (the PDF's name;
the course README says `bin/psh`, but the assignment PDF is the spec).

**Portability fix (after the tag, commit `228dd2e`):** The starter code
uses `bzero()` in `shell.c`. GCC 14 (Kali) rejects the implicit declaration
as an error, while GCC 13 only warned about it. Fixed by replacing
`bzero(arglist[i], ARGLEN)` with `memset(arglist[i], 0, ARGLEN)`.
The `v1.0-base` tag itself stays the unmodified starter code, exactly as
the assignment requires; the fix lives on `main` after the tag.

**Test evidence:** `make` builds cleanly (one warning before the fix, none
after). Scripted run `printf 'ls -l\npwd\nwhoami\nexit\n' | ./bin/myshell`
— `ls -l`, `pwd` and `whoami` all executed correctly as external commands.

**Release:** [Release 1 - The Base Shell](https://github.com/UsmanXTech/BSDSF24M025-OS-A03/releases/tag/v1.0-base)
(tag `v1.0-base`, binary `myshell` attached, 16,680 bytes).

---

## Feature 2 — Built-in Commands (Release 2, tag `v2`, 10 marks)

**What was done:** Added the built-in commands `exit`, `cd`, `help` and
`jobs`. They run inside the shell process itself — no `fork()` — because a
child process cannot change its parent's working directory (`cd`) and
exiting a child would leave the shell running (`exit`).

**How it was implemented:**
- New file `src/builtin.c` with `int handle_builtin(char** arglist)` (prototype
  in `include/shell.h`). It returns `1` if the command was a built-in and was
  handled, `0` otherwise.
- `main.c` now checks built-ins before forking:
  `if (!handle_builtin(arglist)) execute(arglist);`
- `exit` — prints `Shell exited.` and calls `exit(0)`.
- `cd <directory>` — calls the `chdir()` system call; on failure `perror("cd")`
  prints e.g. `cd: No such file or directory`. A bare `cd` (no argument)
  changes to `$HOME` (bash behaviour); if `HOME` is unset it prints an error.
- `help` — prints the list of all four built-ins with a one-line description each.
- `jobs` — placeholder for now: prints `Job control not yet implemented.`
  (real job control arrives in Feature 6).

**Test evidence** (scripted piped input, all passed):
1. `printf 'help\njobs\nexit\n' | ./bin/myshell` — `help` listed all four
   built-ins, `jobs` printed the placeholder, `exit` closed the shell
   gracefully (exit code 0).
2. `printf 'pwd\ncd /tmp\npwd\ncd /nonexistent_dir_xyz\npwd\ncd\npwd\nexit\n' | ./bin/myshell`
   — `pwd` showed the repo directory, then `/tmp` after `cd /tmp` (proving
   `cd` changed the shell's own directory, since `pwd` runs as an external
   child), `cd` to a missing directory printed
   `cd: No such file or directory` and left the directory unchanged, and
   bare `cd` returned to `$HOME` (`/home/hatch`).
3. `printf 'echo hello-from-external\nwhoami\nexit\n' | ./bin/myshell` —
   external commands still work normally after the change.

**Build:** `make` clean — no errors, no warnings (`-Wall -Wextra -std=c11`).

**Release:** [Release 2 - Built-in Commands](https://github.com/UsmanXTech/BSDSF24M025-OS-A03/releases/tag/v2)
(tag `v2`, binary `myshell` attached).

---

## Feature 3 — Command History (Release 3, tag `v3`, 10 marks)

**What was done:** The shell now remembers commands. A `history` built-in
lists stored commands with line numbers starting from 1, and `!n`
re-executes history entry number `n`.

**How it was implemented:**
- Storage: a plain array `char* history[HISTORY_SIZE]` with
  `HISTORY_SIZE 20` in `src/shell.c`, plus `hist_count` (how many are
  stored). When the array is full, the oldest entry (index 0) is freed
  and the rest shift one slot left (`memmove`), so the newest command
  always lands in the last slot — the last 20 commands are kept.
- `add_to_history()` (in `shell.c`) stores a copy of each command line
  read by the main loop. Empty / whitespace-only lines are skipped, so
  they never take up a history slot.
- `history` built-in: added to `handle_builtin()` in `src/builtin.c`;
  it calls `print_history()`, which prints `%d  %s` — line number from 1,
  then the command.
- `!n` re-execution: handled in the `main.c` loop BEFORE tokenization
  and BEFORE adding anything to history, exactly as the PDF requires —
  the literal `!n` text is never stored. `expand_history()` (in
  `shell.c`) parses `n` with `strtol()`, returns a fresh copy of entry
  `n`, and the loop proceeds as if the user had typed that command
  directly (so the re-executed command IS added to history, like bash).
  A non-numeric `!` (`!abc`) or an out-of-bounds number (`!0`, `!99`
  when fewer entries exist) prints
  `myshell: !n: event not found` and the line is skipped.
- `help` now lists `history` and the `!n` syntax.

**Test evidence** (scripted piped input, all passed):
1. `printf 'echo one\necho two\necho three\nhistory\n!2\nhistory\n!99\n!0\n!abc\nexit\n' | ./bin/myshell`
   — `history` listed `1 echo one` … `4 history`; `!2` re-ran `echo two`
   (printed `two` again); the second `history` showed the re-executed
   `echo two` stored as entry 5 (not the literal `!2`); `!99`, `!0` and
   `!abc` each printed `myshell: !n: event not found` on stderr and the
   shell kept running.
2. 25 `echo cmdNN` commands followed by `history` — exactly 20 entries
   shown, oldest `1 echo cmd07` (cmd01–cmd06 dropped, `history` itself
   stored as entry 20). Proves the overwrite-oldest rule.
3. `printf '\n\necho first\n\nhelp\nhistory\n!1\nexit\n' | ./bin/myshell`
   — blank lines produced no history entries (`history` showed only
   `1 echo first`, `2 help`, `3 history`); `!1` re-ran `echo first`;
   `help` lists `history` and the `!n` hint.

**Build:** `make` clean — no errors, no warnings (`-Wall -Wextra -std=c11`).

**Release:** [Release 3 - Command History](https://github.com/UsmanXTech/BSDSF24M025-OS-A03/releases/tag/v3)
(tag `v3`, binary `myshell` attached).

---

## Feature 4 — Tab Completion with GNU Readline (Release 4, tag `v4`, 15 marks)

**What was implemented:** the starter's `getc` input loop was replaced with
the GNU Readline library, giving the shell professional line editing: Tab
completion (default readline completion — file/command names) and up/down
arrow-key history navigation.

**How (key functions / changes):**
- `src/shell.c` — `read_cmd()` now just calls `readline(prompt)`. Readline
  prints the prompt, reads the line and returns a `malloc`'d string
  (`NULL` on EOF / Ctrl-D, same contract as before, so the main loop is
  unchanged). The `fp` parameter is kept so the signature matches the
  starter code; readline always reads from stdin. Includes added:
  `#include <readline/readline.h>` and `#include <readline/history.h>`.
- `src/main.c` — after each command is read (and after any `!n` expansion,
  so the *resolved* line is stored, matching Feature 3), the line is fed
  to readline's own history with `add_history(cmdline)` — non-empty lines
  only (whitespace-only skipped, like Feature 3). This is what the
  up/down arrows cycle through; the `history` built-in still prints from
  the shell's own Feature 3 array, and both lists stay in sync.
- `Makefile` — `LDFLAGS = -lreadline`. Build needs the `libreadline-dev`
  package (headers); it was installed on this machine with
  `sudo apt-get install -y libreadline-dev` (runtime `libreadline8` was
  already present). No custom completion function was written — the PDF
  says to start with readline's default completion behaviour.

**Test evidence (2026-11-16, branch `feature-readline`, commit `1a04f61`):**
- `make clean && make` — clean build, **no errors, no warnings**.
- Scripted piped input
  (`printf 'echo hello-readline\npwd\nhistory\n!1\nexit\n' | ./bin/myshell`)
  works with readline: prompt printed, `echo`/`pwd` ran, `history` listed
  3 commands, `!1` re-ran `echo hello-readline`, `exit` printed
  "Shell exited." — Feature 3 behaviour unchanged.
- Interactive PTY test (Python `pty` module): typed `echo arrow-works`,
  pressed Up-arrow — the line `echo arrow-works` reappeared; Enter
  re-ran it and printed `arrow-works`. Typed `READ` + Tab — completed to
  `README.md `; typed `ls src/exe` + Tab — completed to
  `ls src/execute.c `. Ctrl-D printed "Shell exited." with exit code 0.
- Release: tag `v4` (annotated), merged to main (`bb5d017`),
  [Release 4](https://github.com/UsmanXTech/BSDSF24M025-OS-A03/releases/tag/v4)
  with compiled `bin/myshell` asset (17,408 bytes).

---

## Feature 5 — I/O Redirection and Pipes (Release 5, tag `v5`, 15 marks)

_Pending._

---

## Feature 6 — Command Chaining and Background Execution (Release 6, tag `v6`, 15 marks)

_Pending._

---

## Feature 7 — The `if-then-else-fi` Control Structure (Release 7, tag `v7`, 10 marks)

_Pending._

---

## Feature 8 — Shell Variables (Release 8, tag `v8`, 10 marks)

_Pending._

---

## Feature 9 — Git Workflow with a Secondary Account

_Pending — needs the student's secondary GitHub account; cannot be done by Muse._
