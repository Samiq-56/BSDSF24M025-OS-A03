#include "shell.h"

/*
 * Feature 6 - background jobs list and zombie reaping.
 *
 * The jobs list is a small fixed array (MAX_JOBS entries). Each entry
 * stores the background child's PID, a job number (1, 2, 3, ... shown
 * by `jobs`, like bash's [1], [2]) and a copy of the command string
 * so `jobs` can show what is running.
 *
 * Why a zombie needs reaping: when a background child finishes, it
 * becomes a "zombie" - its exit status sits in the process table
 * until the parent collects it with waitpid(). If the shell never
 * calls waitpid() for background children, zombies pile up forever.
 * reap_zombies() runs before every prompt (see main.c) and calls
 * waitpid(-1, &status, WNOHANG) in a loop: WNOHANG makes it
 * non-blocking, so it collects every finished child and returns at
 * once when none have finished - the shell never freezes waiting.
 */

#define JOB_CMD_LEN 120 /* stored command text is truncated to this */

typedef struct {
    pid_t pid;
    int number; /* job number shown by `jobs`, e.g. [1] */
    char cmd[JOB_CMD_LEN];
} Job;

static Job jobs[MAX_JOBS];
static int job_count = 0;
static int next_job_number = 1;

/* Find a job by PID, or NULL if it is not in the list. */
static Job* find_job(pid_t pid) {
    int i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

/*
 * add_job - record a newly forked background child.
 * Returns the job number, or -1 if the list is full (the child still
 * runs, it is just not tracked - the shell prints a warning).
 */
int add_job(pid_t pid, const char* cmd) {
    Job* job;
    if (job_count >= MAX_JOBS) {
        fprintf(stderr, "myshell: too many background jobs\n");
        return -1;
    }
    job = &jobs[job_count++];
    job->pid = pid;
    job->number = next_job_number++;
    strncpy(job->cmd, cmd, JOB_CMD_LEN - 1);
    job->cmd[JOB_CMD_LEN - 1] = '\0';
    return job->number;
}

/* remove_job - drop a PID from the list (called after it is reaped). */
void remove_job(pid_t pid) {
    int i;
    for (i = 0; i < job_count; i++) {
        if (jobs[i].pid == pid) {
            /* Shift the later entries left to close the gap. */
            memmove(&jobs[i], &jobs[i + 1],
                    sizeof(Job) * (job_count - i - 1));
            job_count--;
            return;
        }
    }
}

/* print_jobs - the `jobs` built-in: list live background jobs. */
void print_jobs(void) {
    int i;
    if (job_count == 0) {
        printf("No background jobs.\n");
        return;
    }
    for (i = 0; i < job_count; i++) {
        printf("[%d]  %d  %s\n", jobs[i].number,
               (int) jobs[i].pid, jobs[i].cmd);
    }
}

/*
 * reap_zombies - collect finished background children.
 *
 * waitpid(-1, ...) means "any child". Each reaped PID is removed
 * from the jobs list and a short "[n] Done <cmd>" line is printed,
 * so the user sees the job finish (bash does the same).
 * A foreground child is never reaped here: execute() already
 * waitpid()s for its own child before returning.
 */
void reap_zombies(void) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Job* job = find_job(pid);
        if (job != NULL) {
            printf("[%d] Done %s\n", job->number, job->cmd);
            fflush(stdout); /* order the message before the next prompt */
            remove_job(pid);
        }
        /* A PID we do not track (should not happen) is just dropped. */
    }
}
