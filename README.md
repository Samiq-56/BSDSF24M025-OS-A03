# BSDSF24M025-OS-A03 — PUCIT's Custom UNIX Shell

A simple command-line interpreter (shell) built in C for the Operating Systems
course (Programming Assignment 03). Started from the instructor's base shell
(`arifpucit/OS-Codes`, `Programming-Assignments/PA-03/base-assignment-03`)
and extended feature by feature.

## Quick Start

### Build the Shell

```bash
make
```

### Run the Shell

The compiled executable is in the `bin/` directory.

```bash
./bin/myshell
```

### Clean the Project

```bash
make clean
```

## Directory Structure

- `src/` — all C source files (`.c`)
- `include/` — all header files (`.h`)
- `bin/` — the final compiled executable (`myshell`)
- `obj/` — intermediate object files (`.o`)
- `Makefile` — the build script

## Features (releases)

| Version | Feature |
|---------|---------|
| v1.0-base | The Base Shell (starter code, unmodified) |
| v2 | Built-in Commands (`exit`, `cd`, `help`, `jobs`) |
| v3 | Command History (`history`, `!n`) |
| v4 | Tab Completion with GNU Readline |
| v5 | I/O Redirection (`<`, `>`) and Pipes (`\|`) |
| v6 | Command Chaining (`;`) and Background Execution (`&`) |
| v7 | The `if-then-else-fi` Control Structure |
| v8 | Shell Variables (`VAR=value`, `$VAR`, `set`) |

See `REPORT.md` for the full per-feature write-up.
