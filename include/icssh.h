#ifndef ICSSH_H
#define ICSSH_H


#include "linkedlist.h"
#include "helpers.h"
#include "debug.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>


#define RD_ERR "REDIRECTION ERROR: Invalid operators or file combination.\n"
#define BG_TERM "Background process %d: %s, has terminated.\n"
#define DIR_ERR "DIRECTORY ERROR: Directory does not exist.\n"
#define EXEC_ERR "EXEC ERROR: Cannot execute %s.\n"
#define WAIT_ERR "WAIT ERROR: An error ocured while waiting for the process.\n"
#define PID_ERR "PROCESS ERROR: Process pid does not exist.\n"
#define PIPE_ERR "PIPE ERROR: Invalid use of pipe operators.\n"
#define BG_ERR "BG ERROR: Maximum background processes exceeded.\n"

#define CMD_FAIL -1
#define CMD_SUCC 0
#define CMD_NOT_FOUND 1
#define BUFFER_SIZE 256
#define CHILD 0

#ifdef DEBUG_PRINT
#define debug_print(fmt, ...) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) do {} while(0)
#endif


typedef struct proc_info {
	char *err_file;               // name of file that stderr redirects to
	int argc;                     // number of args
	char **argv;                  // arguments for program; argv[0] is the program to run
	char *cmd;                    // name of the program (argv[0])
	struct proc_info *next_proc;  // next program (process) in job; NULL if this is the last one
} proc_info;

typedef struct {
	bool bg;           // is this a background job?
	char *line;        // original commandline for this job
	int nproc;         // number of processes in this job
	char *in_file;     // name of file that stdin redirects from
	char *out_file;    // name of file that stdout redirects to
	proc_info *procs;  // list of processes in this job
} job_info;

typedef struct bgentry {
	job_info *job;   // the job that the bgentry refers to
	pid_t pid;       // pid of the (first) background process
	time_t seconds;  // time at which the command recieved by the shell
} bgentry_t;

// Contains all the data information of the currently running shell
typedef struct {
    job_info *job;
	pid_t pid;
	int max_bgprocs;
	pid_t wait_result;
	int exec_result;
	int exit_status;
	char* line;
	char* last_dir;
	char* prompt;
} Shell_Info;


// Main part:
void checkMainCmdArgs(int argc, char* argv[], Shell_Info *currShell);

char* getShellPrompt();

void evalShell(Shell_Info *currShell);

pid_t doFork(Shell_Info *currShell);

void reportUnixError(char *msg);

void freeMemory(Shell_Info *currShell);

// Foreground Job & Built-in commands part: 
void execSingleFgProcs(Shell_Info *currShell);

void execMultFgProcs(Shell_Info *currShell);

int8_t execBuiltInCmd(Shell_Info *currShell);

int8_t exitShell(Shell_Info *currShell);

int8_t changeDir(Shell_Info *currShell);

int8_t printCurrDir(Shell_Info *currShell);

int8_t printLastChldProcExitStatus(Shell_Info *currShell);

int8_t listRunnBgJobs(Shell_Info *currShell);

int8_t moveToForeground(Shell_Info *currShell);

int8_t printUCILogo(Shell_Info *currShell);

int8_t printNumRunnBgJobs(Shell_Info *currShell);

int8_t printCurrSigchldFlag(Shell_Info *currShell);

size_t getNumOfCmds();

// Background Job part: 
void execSingleBgProcs(Shell_Info *currShell);

void execMultBgProcs(Shell_Info *currShell);

void reapTerminatedBgJobs(Shell_Info *currShell);

bool isReachMaxBgProcs(int count, int maxBgProcs);

void addBgEntryToList(Shell_Info *currShell);

void removeBgEntryFromList(pid_t pid);

void clearList();

void findBgEntry(node_t **current, node_t **previous, pid_t pid);

int bgJobListComparator(const void *time1, const void *time2);

void bgJobListPrinter(void *data, void *fp);

void bgJobListDeleter(void *data);

// Redirection part:
void setRedirs(int redir_fds[]);

void setInRedir(int redir_fds[]);

void setOutRedir(int redir_fds[]);

void setErrRedir(int redir_fds[]);

bool openFiles(Shell_Info *currShell, int redir_fds[]);

void closeFiles(int redir_fds[]);

bool isRedirValid(Shell_Info *currShell);

// Pipe part:
void initPipes(Shell_Info *currShell, int pipes[][2], int pipesNum);

void setOutPipe(int pipes[][2], int pipesNum, int procIndex);

void setInPipe(int pipes[][2], int pipesNum, int procIndex);

void closePipes(int pipes[][2], int pipesNum);

// Signal handlers part:
void installSignals();

void sigchld_handler(int sigNum);

void sigusr2_handler(int sigNum);

void updateCurrentTime();

void sio_error(const char str[]);

ssize_t sio_putl(const long val);

void sio_put_time(long val, int width);

ssize_t sio_puts(const char str[]);

void sio_ltoa(long val, char str[], int base);

void sio_reverse(char str[]);

/*
 * Print a job_info stuct for information and debugging purposes
 */
void debug_print_job(job_info *job);

/*
 * Free a job_info struct and all dynamically allocated components,
 * except line
 */
void free_job(job_info *job);

/* 
 * Accepts a command line and validates it.
 * Returns a valid job_info struct if successful, 
 * returns NULL if any error occurs.
 */
job_info *validate_input(char *line);

/* 
 * Prints out a single bgentry struct to STDERR
 */
void print_bgentry(bgentry_t *p);

/*
 * Prints message to STDERR prior to termination. 
 * Let's you know the SEGFAULT occured in your shell code, not the grader.
 */
void sigsegv_handler();

#endif