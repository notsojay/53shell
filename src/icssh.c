#include "icssh.h"
#include "linkedlist.h"
#include "helpers.h"
#include "debug.h"

#include <readline/readline.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifdef DEBUG
#define DEBUG_FLAG 1
#else
#define DEBUG_FLAG 0
#endif

/*************************************************************
*************************************************************
*************************************************************
*************************************************************


Set up the necessary functions and global variables/constants
	 
	 
*************************************************************
*************************************************************
*************************************************************
************************************************************/

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

// A flag Identifies whether there are currently terminated background processes that need to be reap:
static volatile sig_atomic_t g_isAnyBgJobTerminated;

// Global variables for signal-safe time formatting:
static volatile sig_atomic_t g_currSec;
static volatile sig_atomic_t g_currMin;
static volatile sig_atomic_t g_currHour;
static volatile sig_atomic_t g_currMonthday;
static volatile sig_atomic_t g_currMonth;
static volatile sig_atomic_t g_currYear;
static volatile sig_atomic_t g_currWeekDay;

static const char *G_WEEKDAY[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *G_MONTH[] = {
	"Jan", "Feb", "Mar",
	"Apr", "May", "Jun",
	"Jul", "Aug", "Sep",
	"Oct", "Nov", "Dec"
};

// Global Constant for the full list of names of foreground Job built-in commands:
static const char *G_BUILTIN_CMD_ROLES[] = {
	"exit",
	"cd",
	"pwd",
	"estatus",
	"bglist",
	"fg",
	"ascii53",
	"bgcount",
	"flag_sigchld"
};

// Global Constant function jump table for foreground Job built-in commands: 
static const int8_t (*G_BUILTIN_CMD_MAP[])(Shell_Info*) = {
	&exitShell,
	&changeDir,
	&printCurrDir,
	&printLastChldProcExitStatus,
	&listRunnBgJobs,
	&moveToForeground,
	&printUCILogo,
	&printNumRunnBgJobs,
	&printCurrSigchldFlag
};

// Global variable for storing information of background jobs in running:
static list_t *g_bgJobList = NULL;

/************************************************************
*************************************************************
*************************************************************
*************************************************************


 	     Shell Program entrance & Main Part
	 
	 
*************************************************************
*************************************************************
*************************************************************
************************************************************/
int
main(int argc, char* argv[]) 
{
	Shell_Info currShell;
	memset( &currShell, 0, sizeof(Shell_Info) );
	currShell.max_bgprocs = -1;
	g_isAnyBgJobTerminated = 0;
	g_bgJobList = CreateList( &bgJobListComparator,
				  			  &bgJobListPrinter, 
			          		  &bgJobListDeleter    );

#ifdef GS
    rl_outstream = fopen("/dev/null", "w");
#endif

	checkMainCmdArgs(argc, argv, &currShell);
	installSignals();
	evalShell(&currShell);
	freeMemory(&currShell);

#ifndef GS
	fclose(rl_outstream);
#endif
	return 0;
}

void
checkMainCmdArgs(int argc, char* argv[], Shell_Info *currShell)
{
	/*
		The 53shell main optionally accepts 1 command-line argument. This argument will set the max_bgprocs variable (default: -1). Use this variable to restrict the number of background processes allowed by your shell at one time. The default value of -1 represents unlimited background processes. If the command-line argument is specified, N, then at most N bg processes can be run at one time.
	*/
	if(argc > 1)
	{
		int check = atoi(argv[1]);
		
        if(check != 0)
			currShell->max_bgprocs = check;
        else 
            printf("Invalid command line argument value\n"), 
			exit(EXIT_FAILURE);
    }
}

char*
getShellPrompt()
{
#ifdef DEBUG
	char cwdBuffer[PATH_MAX];
	char hostBuffer[BUFFER_SIZE];
	char* username = getenv("USER");

    if( !getcwd(cwdBuffer, sizeof(cwdBuffer)) ) 
		return fprintf(stderr, DIR_ERR), NULL;

	if(gethostname(hostBuffer, sizeof(hostBuffer)) < 0) 
       		return fprintf(stderr, "Error getting hostname\n"), NULL;

    if(!username) 
	username = "";

	int length = snprintf( NULL, 
						   0, 
			       		   ICS_GRE "[" 
			       	 	   ICS_MAG "%s@" 
			      	 	   ICS_BLU "%s:" 
			       		   ICS_MAG "%s" 
			       		   ICS_GRE "(master)~]$ " 
			       		   ICS_NRM, 
			       		   username, hostBuffer, cwdBuffer );

	char* prompt = (char*)malloc( (length+1) * sizeof(char) ); // +1 for the null-terminator

    sprintf( prompt,
	    	 ICS_GRE "[" 
	    	 ICS_GRE "%s@" 
	     	 ICS_BLU "%s:" 
	     	 ICS_MAG "%s" 
	     	 ICS_GRE "(master)~]$ " 
	     	 ICS_NRM, 
			 username, hostBuffer, cwdBuffer );

    	return prompt;
#else
	return ""; 
#endif
}

void
evalShell(Shell_Info *currShell)
{
	// Main loop
    // Print the prompt & wait for the user to enter commands string:
	while( (currShell->prompt = getShellPrompt()) != NULL && 
		   (currShell->line = readline(currShell->prompt)) != NULL ) 
	{
        	// MAGIC HAPPENS! Command string is parsed into a job struct
        	// Will print out error message if command string is invalid
		currShell->job = validate_input(currShell->line);

        if(currShell->job == NULL)  // Command was empty string or invalid
		{
			free(currShell->line);
			continue;
		}

        	// Prints out the job linked list struture for debugging
        	#ifdef DEBUG   // If DEBUG flag removed in makefile, this will not longer print
            		debug_print_job(currShell->job);
        	#endif
		
		updateCurrentTime();

		if(g_isAnyBgJobTerminated)
			reapTerminatedBgJobs(currShell);

		if(currShell->job->bg) 
    		(currShell->job->nproc - 1 > 0) ? 
				execMultBgProcs(currShell) : execSingleBgProcs(currShell);
		else 
   			(currShell->job->nproc - 1 > 0) ? 
				execMultFgProcs(currShell) : execSingleFgProcs(currShell);

		if(currShell->line)
			free(currShell->line);
		
		if(DEBUG_FLAG && currShell->prompt)
			free(currShell->prompt);
	}
}

void
reportUnixError(char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

pid_t
doFork(Shell_Info *currShell)
{
  	pid_t pid;
  	if( (pid = fork()) < 0 ) 
	{
		freeMemory(currShell);
		reportUnixError("fork error");
	}
  	return pid;
}

void
freeMemory(Shell_Info *currShell)
{
	// Cleaning up to make Valgrind happy 
	// (not necessary because child will exit. Resources will be reaped by parent)
	if(currShell && currShell->job) free_job(currShell->job);
	if(currShell && currShell->line) free(currShell->line);
	if(currShell->last_dir) free(currShell->last_dir);
	if(DEBUG_FLAG && currShell->prompt) free(currShell->prompt);
	if(g_bgJobList) clearList(g_bgJobList), free(g_bgJobList);
	validate_input(NULL); // calling validate_input with NULL will free the memory it has allocated
}

/************************************************************
*************************************************************
*************************************************************
*************************************************************

 	
	    Foreground Job & Built-in commands
	

*************************************************************
*************************************************************
*************************************************************
************************************************************/
void
execSingleFgProcs(Shell_Info *currShell)
{
	// redir_fds[0] = stdin
	// redir_fds[1]= stdout
	// redir_fds[2] = stderror
	int redir_fds[3] = {-1, -1, -1};

	// If it's a built-in command, then executed and returns
	if( execBuiltInCmd(currShell) != CMD_NOT_FOUND ||
		// The same file cannot be shared between any two types of redirection 
		// Redirection of the same stream can not occur to 2 different places			
	    !isRedirValid(currShell) ||
	    !openFiles(currShell, redir_fds) )
	{
		if(currShell->job) free_job(currShell->job), currShell->job = NULL;
		closeFiles(redir_fds);
		return;
	}
	
    	// Create the child proccess:
	currShell->pid = doFork(currShell);
	
	// If zero, then it's the child process:
	if(currShell->pid == CHILD)
   	{
		setpgid(0, 0);
		setRedirs(redir_fds);
		closeFiles(redir_fds);
		// Get the first command in the job list to execute
		proc_info* currProc = currShell->job->procs;
		currShell->exec_result = execvp(currProc->cmd, currProc->argv);
		if(currShell->exec_result < 0) // Exec error checking:
			printf(EXEC_ERR, currProc->cmd), exit(EXIT_FAILURE);
		// Child process ends here.
	}
	
	// Parent here
	closeFiles(redir_fds);
	// As the parent, wait for the foreground job to finish
	currShell->wait_result = waitpid(currShell->pid, &currShell->exit_status, 0);
	// It's the parent process:
	if(currShell->wait_result < 0) 
	{
		printf(WAIT_ERR);
		freeMemory(currShell);
		exit(EXIT_FAILURE);
	}
	// If a foreground job, we no longer need the data
	if(currShell->job) free_job(currShell->job), currShell->job = NULL;
}

void
execMultFgProcs(Shell_Info *currShell)
{
	int pipesNum = currShell->job->nproc - 1; // Beacuse include parent
	int pipes[pipesNum][2];
	int redir_fds[] = {-1, -1, -1};
	pid_t procsPid[currShell->job->nproc];
	int procIndex = 0;
	proc_info *currProc = currShell->job->procs;

	if( !isRedirValid(currShell) ||
	    !openFiles(currShell, redir_fds) )							
	{
		if(currShell->job) free_job(currShell->job), currShell->job = NULL;
		closeFiles(redir_fds);
		return;
	}
	
	initPipes(currShell, pipes, pipesNum);

	while(currProc)
	{
		procsPid[procIndex] = doFork(currShell);
		// If zero, then it's the child process:
		if(procsPid[procIndex] == CHILD)
		{
			setErrRedir(redir_fds);

			// If the current command is not the first command), 
			// then its standard input (STDIN_FILENO) is connected to the piped output of the previous command.
			if(procIndex > 0) 
				setInPipe(pipes, pipesNum, procIndex);
			else if(procIndex == 0)
				setpgid(0,0), setInRedir(redir_fds);

			// If the current command is not the last command, 
			// then its standard output (STDOUT_FILENO) is connected to the pipeline input of the next command.
			if(currProc->next_proc)
				setOutPipe(pipes, pipesNum, procIndex);
			else
				setOutRedir(redir_fds);

			closeFiles(redir_fds);
			closePipes(pipes, pipesNum);

			currShell->exec_result = execvp(currProc->cmd, currProc->argv);
			if(currShell->exec_result < 0)
				printf(EXEC_ERR, currProc->cmd), exit(EXIT_FAILURE);
		}
		// It's the parent process:
		currProc = currProc->next_proc;
		++procIndex;
	}
	
	// It's the parent process:
	closePipes(pipes, pipesNum);
	closeFiles(redir_fds);
	for(int i = 0; i < currShell->job->nproc; ++i)
	{
		currShell->wait_result = waitpid(procsPid[i], &currShell->exit_status, 0);
		if(currShell->wait_result < 0) 
		{
			printf(WAIT_ERR);
			freeMemory(currShell);
			exit(EXIT_FAILURE);
		}
	}
	if(currShell->job) free_job(currShell->job), currShell->job = NULL;
}

int8_t
execBuiltInCmd(Shell_Info *currShell) 
{
	if(!currShell) return CMD_FAIL;
	for(size_t i = 0; i < getNumOfCmds(); ++i) 
	{
		if(strcmp(currShell->job->procs->cmd, G_BUILTIN_CMD_ROLES[i]) == 0) 
			return (*G_BUILTIN_CMD_MAP[i])(currShell);
	}
	return CMD_NOT_FOUND;
}

int8_t
exitShell(Shell_Info *currShell)
{
	freeMemory(currShell);
	exit(EXIT_SUCCESS);
}

int8_t
changeDir(Shell_Info *currShell) 
{
	char *dstPath = *((currShell->job->procs->argv)+1);
	char cwdBuffer[PATH_MAX];

	if(dstPath && strcmp(dstPath, "-") != 0)
	{
		if(currShell->last_dir) free(currShell->last_dir);
		currShell->last_dir = strdup(cwdBuffer);
	}
	// The dash '-' tells the shell to change directory to the most recent previous directory path
	// If the directory change is successful, the absolute pathname of the new working directory is written 
	if(dstPath && strcmp(dstPath, "-") == 0)
	{
		if(currShell->last_dir)
			dstPath = currShell->last_dir;
		else
			return fprintf(stderr, DIR_ERR), CMD_FAIL;
	}
	else if( !dstPath && !(dstPath = getenv("HOME")) ) // If there are no parameters
	{
		fprintf(stderr, DIR_ERR);
		return CMD_FAIL;
	}

	if( chdir(dstPath) != 0 || !getcwd(cwdBuffer, sizeof(cwdBuffer)) ) 
		return fprintf(stderr, DIR_ERR), CMD_FAIL;

	fprintf(stdout, "%s\n", cwdBuffer);

	return CMD_SUCC;
}

int8_t
printCurrDir(Shell_Info *currShell)
{
	char cwdBuffer[PATH_MAX];
	if( getcwd(cwdBuffer, sizeof(cwdBuffer)) ) 
		return fprintf(stdout, "%s\n", cwdBuffer), CMD_SUCC;
	return CMD_FAIL;
}

int8_t
printLastChldProcExitStatus(Shell_Info *currShell) 
{
	fprintf( stdout, "%d\n", WEXITSTATUS(currShell->exit_status) );
	return CMD_SUCC;
}

int8_t
listRunnBgJobs(Shell_Info *currShell)
{
	if(g_bgJobList->length == 0) return CMD_FAIL;
	PrintLinkedList(g_bgJobList, stderr);
	return CMD_SUCC;
}

int8_t
moveToForeground(Shell_Info *currShell)
{
	node_t *current = g_bgJobList->head, *previous = NULL;
	pid_t target_pid;

	if( *((currShell->job->procs->argv)+1) )
	{
		target_pid = (pid_t)atoi( *((currShell->job->procs->argv)+1) );
		
		findBgEntry(&current, &previous, target_pid);
	
		if(!current)
			return fprintf(stderr, PID_ERR), CMD_FAIL;
		else if(current == g_bgJobList->head)
			g_bgJobList->head = g_bgJobList->head->next;
		else
			previous->next = current->next;
	}
	else
	{
		while(current)
		{
			previous = current;
			current = current->next;
		}
		current = previous;
		target_pid = ((bgentry_t*)current->data)->pid;
	}

	fprintf(stdout, "%s\n", ((bgentry_t*)current->data)->job->line);
	currShell->wait_result = waitpid(target_pid, &currShell->exit_status, 0);
	if(currShell->wait_result < 0) 
	{
		printf(WAIT_ERR);
		freeMemory(currShell);
		exit(EXIT_FAILURE);
	}
	g_bgJobList->deleter(current->data);
	free(current);
	current = NULL;
	--g_bgJobList->length;

	return CMD_SUCC;
}

int8_t
printUCILogo(Shell_Info *currShell)
{
	fprintf(stdout, "\n");
	fprintf(stdout, "██╗      ██╗   ████████╗  ███╗\n");
	fprintf(stdout, "██╗      ██╗   ████████╗  ███╗\n");
	fprintf(stdout, "██║      ██║  ███╔═════╝  ███║\n");
	fprintf(stdout, "██║      ██║  ███║        ███║\n");
	fprintf(stdout, "██║      ██║  ███║        ███║\n");
	fprintf(stdout, "██║      ██║  ███║        ███║\n");
	fprintf(stdout, "██║      ██║  ███║        ███║\n");
	fprintf(stdout, "╚█████████╔╝  ╚████████╗  ███║\n");
	fprintf(stdout, "╚█████████╔╝  ╚████████╗  ███║\n");
	fprintf(stdout, " ╚════════╝    ╚═══════╝  ╚══╝\n");
	fprintf(stdout, "\n");
}

int8_t
printNumRunnBgJobs(Shell_Info *currShell)
{
	fprintf(stdout, "%d\n", g_bgJobList->length);
	return CMD_SUCC;
}

int8_t
printCurrSigchldFlag(Shell_Info *currShell)
{
	fprintf(stdout, "%d\n", g_isAnyBgJobTerminated );
	return CMD_SUCC;
}

size_t
getNumOfCmds()
{
	return sizeof(G_BUILTIN_CMD_ROLES) / sizeof(char*);
}

/************************************************************
*************************************************************
*************************************************************
*************************************************************


 		      Background Job


*************************************************************
*************************************************************
*************************************************************
************************************************************/
void
execSingleBgProcs(Shell_Info *currShell)
{
	// redir_fds[0] = stdin
	// redir_fds[1]= stdout
	// redir_fds[2] = stderror
	int redir_fds[3] = {-1, -1, -1};
	sigset_t mask_all, mask_child, prev_one;

	if( isReachMaxBgProcs(g_bgJobList->length, currShell->max_bgprocs) ||
		!isRedirValid(currShell) ||
		!openFiles(currShell, redir_fds) ) 
	{
    		if(currShell->job) free_job(currShell->job), currShell->job = NULL;
		closeFiles(redir_fds);
    		return;
	}

	sigfillset(&mask_all);
	sigemptyset(&mask_child);
	sigaddset(&mask_child, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask_child, &prev_one);

	// Create the child proccess:
	currShell->pid = doFork(currShell);
	
	// If zero, then it's the child process:
	if(currShell->pid == CHILD)
    	{
		sigprocmask(SIG_SETMASK, &prev_one, NULL);
		setpgid(0,0);
		setRedirs(redir_fds);
		closeFiles(redir_fds);
		// Get the first command in the job list to execute
		proc_info* currProc = currShell->job->procs;
		currShell->exec_result = execvp(currProc->cmd, currProc->argv);
		if(currShell->exec_result < 0)
			printf(EXEC_ERR, currProc->cmd), exit(EXIT_FAILURE);
		// Child process ends here
	}
	
	// It's the parent process:
	sigprocmask(SIG_BLOCK, &mask_all, NULL);
	addBgEntryToList(currShell); 
	sigprocmask(SIG_SETMASK, &prev_one, NULL);
	closeFiles(redir_fds);
	sleep(0);
}

void
execMultBgProcs(Shell_Info *currShell)
{
	int pipesNum = currShell->job->nproc - 1; // Beacuse include parent
	int pipes[pipesNum][2];
	int redir_fds[3] = {-1, -1, -1};
	pid_t procsPid[currShell->job->nproc];
	int procIndex = 0;
	proc_info *currProc = currShell->job->procs;
	sigset_t mask_all, mask_child, prev_one;

	if( isReachMaxBgProcs(g_bgJobList->length, currShell->max_bgprocs) ||
	    !isRedirValid(currShell) ||
	    !openFiles(currShell, redir_fds) )							
	{
		if(currShell->job) free_job(currShell->job), currShell->job = NULL;
		closeFiles(redir_fds);
		return;
	}

	initPipes(currShell, pipes, pipesNum);

	sigfillset(&mask_all);
	sigemptyset(&mask_child);
	sigaddset(&mask_child, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask_child, &prev_one);

	while(currProc)
	{
		procsPid[procIndex] = doFork(currShell);
		// If zero, then it's the child process:
		if(procsPid[procIndex] == CHILD)
		{
			sigprocmask(SIG_SETMASK, &prev_one, NULL);
			setErrRedir(redir_fds);

			// If the current command is not the first command), 
			// then its standard input (STDIN_FILENO) is connected to the piped output of the previous command.
			if(procIndex > 0) 
				setInPipe(pipes, pipesNum, procIndex);
			else if(procIndex == 0)
				setpgid(0,0), setInRedir(redir_fds);

			// If the current command is not the last command, 
			// then its standard output (STDOUT_FILENO) is connected to the pipeline input of the next command.
			if(currProc->next_proc)
				setOutPipe(pipes, pipesNum, procIndex);
			else
				setOutRedir(redir_fds);

			closeFiles(redir_fds);
			closePipes(pipes, pipesNum);

			currShell->exec_result = execvp(currProc->cmd, currProc->argv);
			if(currShell->exec_result < 0) // Exec error checking:
				printf(EXEC_ERR, currProc->cmd), exit(EXIT_FAILURE);
		}
		// It's the parent process:
		currProc = currProc->next_proc;
		++procIndex;
	}
	
	// It's the parent process:
	sigprocmask(SIG_BLOCK, &mask_all, NULL);
	currShell->pid = procsPid[0];
	addBgEntryToList(currShell); 
	sigprocmask(SIG_SETMASK, &prev_one, NULL);
	closeFiles(redir_fds);
	closePipes(pipes, pipesNum);
	sleep(0);
}

void
reapTerminatedBgJobs(Shell_Info *currShell)
{
	sigset_t mask_all, prev_one;

   	sigfillset(&mask_all);
	sigprocmask(SIG_BLOCK, &mask_all, &prev_one);

   	 while( (currShell->wait_result = waitpid(-1, &currShell->exit_status, WNOHANG)) > 0 )
	 {
		if(currShell->wait_result < 0) 
		{
			printf(WAIT_ERR);
			freeMemory(currShell);
			exit(EXIT_FAILURE);
		}
		debug_print("(%d) START\n", currShell->wait_result);
        	// Remove the terminated background job from the list and print a message
        	removeBgEntryFromList(currShell->wait_result);
	 }

	g_isAnyBgJobTerminated = 0;
	debug_print("(%d) END:\n\t g_isAnyBgJobTerminated = %d\n", currShell->wait_result, g_isAnyBgJobTerminated);
	sigprocmask(SIG_SETMASK, &prev_one, NULL);
}

bool
isReachMaxBgProcs(int count, int maxBgProcs)
{
	return (count < maxBgProcs || maxBgProcs == -1) ? false : (fprintf(stderr, BG_ERR), true);
}

void
addBgEntryToList(Shell_Info *currShell)
{
	bgentry_t *currData = (bgentry_t*)malloc( sizeof(bgentry_t) );

	currData->job = currShell->job;
	currData->pid = currShell->pid;
	currData->seconds = time(NULL);
	InsertInOrder(g_bgJobList, (void*)currData);
}

void
removeBgEntryFromList(pid_t pid)
{
	if(!g_bgJobList || !g_bgJobList->head || !g_bgJobList->length) return;

	node_t *current = g_bgJobList->head, *previous = NULL;

	findBgEntry(&current, &previous, pid);

	if(!current) return;
	else if(current == g_bgJobList->head) g_bgJobList->head = g_bgJobList->head->next;
	else previous->next = current->next;

	fprintf( stdout,
		 	 BG_TERM,
		 	 ((bgentry_t*)current->data)->pid,
		 	 ((bgentry_t*)current->data)->job->line );

	g_bgJobList->deleter(current->data);
	free(current);
	current = NULL;
	--g_bgJobList->length;
}

void
clearList()
{
	if(!g_bgJobList || g_bgJobList->length == 0) return;
	while(g_bgJobList->head != NULL)
	{
		fprintf( stdout,
			 	 BG_TERM,
			 	 ((bgentry_t*)g_bgJobList->head->data)->pid,
			 	 ((bgentry_t*)g_bgJobList->head->data)->job->line );

		RemoveFromHead(g_bgJobList);
	}
	g_bgJobList->length = 0;
}

void
findBgEntry(node_t **current, node_t **previous, pid_t pid)
{
	while(*current && ((bgentry_t*)(*current)->data)->pid != pid)
	{
		*previous = *current;
		*current = (*current)->next;
	}
}

int
bgJobListComparator(const void *time1, const void *time2)
{
	return difftime(*(time_t*)time1, *(time_t*)time2);
}

void
bgJobListPrinter(void* data, void* fp)
{
	if(!data || !fp) return;
	bgentry_t *temp = (bgentry_t*)data;
	print_bgentry(temp);
}

void
bgJobListDeleter(void* data)
{
	if(!data) return;
	if( ((bgentry_t*)data)->job )
	{
		free_job( ((bgentry_t*)data)->job );
		((bgentry_t*)data)->job = NULL;
	}
	free(( (bgentry_t*)data) );
	data = NULL;
}

/************************************************************
*************************************************************
*************************************************************
*************************************************************


 		      		   Redirection 	
	 

*************************************************************
*************************************************************
*************************************************************
************************************************************/
void
setRedirs(int redir_fds[])
{
	setInRedir(redir_fds);
	setOutRedir(redir_fds);
	setErrRedir(redir_fds);
}

void
setInRedir(int redir_fds[])
{
	if(redir_fds[0] != -1)
	{
		if(dup2(redir_fds[0], STDIN_FILENO) == -1)
		{
			fprintf(stderr, RD_ERR);
			closeFiles(redir_fds);
			exit(EXIT_FAILURE);
		}
	}
}

void
setOutRedir(int redir_fds[])
{
	if(redir_fds[1] != -1)
	{
		if(dup2(redir_fds[1], STDOUT_FILENO) == -1)
		{
			fprintf(stderr, RD_ERR);
			closeFiles(redir_fds);
			exit(EXIT_FAILURE);
		}
	}
}

void
setErrRedir(int redir_fds[])
{
	if(redir_fds[2] != -1)
	{
		if(dup2(redir_fds[2], STDERR_FILENO) == -1)
		{
			fprintf(stderr, RD_ERR);
			closeFiles(redir_fds);
			exit(EXIT_FAILURE);
		}
	}
}

bool
openFiles(Shell_Info *currShell, int redir_fds[])
{
	// fd for stdin:
	if(currShell->job->in_file)
	{
		if( (redir_fds[0] = open(currShell->job->in_file, O_RDONLY)) == -1 )
		{
			fprintf(stderr, RD_ERR);
			return false;
		}
	}
	// fd for stdout:
	if(currShell->job->out_file)
	{
		if( (redir_fds[1] = open( currShell->job->out_file,
					  			  O_WRONLY | O_CREAT | O_TRUNC, 
					  			  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1 )
		{
			fprintf(stderr, RD_ERR);
			return false;
		}
	}
	// fd for stderr:
	if(currShell->job->procs->err_file)
	{
		if( (redir_fds[2] = open( currShell->job->procs->err_file,
					 			  O_WRONLY | O_CREAT | O_TRUNC,
					  			  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1 )
		{
			fprintf(stderr, RD_ERR);
			return false;
		}
	}
	return true;
}

void
closeFiles(int redir_fds[])
{
	int olderrno = errno;

	if(redir_fds[0] != -1)
		close(redir_fds[0]), redir_fds[0] = -1;

	if(redir_fds[1] != -1)
		close(redir_fds[1]), redir_fds[1] = -1;

	if(redir_fds[2] != -1)
		close(redir_fds[2]), redir_fds[2] = -1;

	errno = olderrno;
}

// The same file cannot be shared between any two types of redirection 
// Redirection of the same stream can not occur to 2 different places
bool
isRedirValid(Shell_Info *currShell)
{
	char *inFile = currShell->job->in_file;
	char *outFile = currShell->job->out_file;
	char *errFile = currShell->job->procs->err_file;

	if( (inFile && outFile && strcmp(inFile, outFile) == 0) ||
		(inFile && errFile && strcmp(inFile, errFile) == 0) ||
		(outFile && errFile && strcmp(outFile, errFile) == 0) )
	{
		fprintf(stderr, RD_ERR);
		return false;
	}
	if(currShell->job->procs->next_proc)
	{
		proc_info *currProc = currShell->job->procs->next_proc;
		while(currProc)
		{
			if( (errFile && currProc->err_file && strcmp(errFile, currProc->err_file) == 0) ||
			    (inFile && currProc->err_file && strcmp(inFile, currProc->err_file) == 0) ||
			    (outFile && currProc->err_file && strcmp(outFile, currProc->err_file) == 0 ) )
			{
				fprintf(stderr, RD_ERR);
				return false;
			}
			currProc = currProc->next_proc;
		}
	}
	return true;
}

/************************************************************
*************************************************************
*************************************************************
*************************************************************


 		          		  Pipes	


*************************************************************
*************************************************************
*************************************************************
************************************************************/
void
initPipes(Shell_Info *currShell, int pipes[][2], int pipesNum)
{
	for(int i = 0; i < pipesNum; ++i)
	{
		if(pipe(pipes[i]) < 0) 
		{
			freeMemory(currShell);
			closePipes(pipes, pipesNum);
			reportUnixError("pipe error");
		}
	}
}

void
setOutPipe(int pipes[][2], int pipesNum, int procIndex)
{
	if(dup2(pipes[procIndex][1], STDOUT_FILENO) == -1)
	{
		fprintf(stderr, PIPE_ERR);
		closePipes(pipes, pipesNum);
		exit(EXIT_FAILURE);
	}
}

void
setInPipe(int pipes[][2], int pipesNum, int procIndex)
{
	if(dup2(pipes[procIndex-1][0], STDIN_FILENO) == -1)
	{
		fprintf(stderr, PIPE_ERR);
		closePipes(pipes, pipesNum);
		exit(EXIT_FAILURE);
	}
}

void
closePipes(int pipes[][2], int pipesNum)
{
	int olderrno = errno;

	for(int i = 0; i < pipesNum; ++i)
	{
		if(pipes[i][0] != -1)
			close(pipes[i][0]), pipes[i][0] = -1;

		if(pipes[i][1] != -1)
			close(pipes[i][1]), pipes[i][1] = -1;
	}

	errno = olderrno;
}

/************************************************************
*************************************************************
*************************************************************
*************************************************************


 		      		Signal handlers	
	 

*************************************************************
*************************************************************
*************************************************************
************************************************************/
void
installSignals()
{
	struct sigaction sa;
	memset( &sa, 0, sizeof(struct sigaction) );
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
    	sa.sa_flags = SA_RESTART; // Automatically restart interrupted system calls

	// Install SIGCHLD handler:
    if(sigaction(SIGCHLD, &sa, NULL) == -1) 
		reportUnixError("Failed to set signal handler");

	// Install SIGUSR2 handler:
	memset( &sa, 0, sizeof(struct sigaction) );
	sa.sa_handler = sigusr2_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGUSR2, &sa, NULL) == -1) 
		reportUnixError("Failed to set signal handler");
		
	// Install SIGSEGV handler:
	if(signal(SIGSEGV, sigsegv_handler) == SIG_ERR) 
		reportUnixError("Failed to set signal handler");
}

void
sigchld_handler(int sigNum)
{
	debug_print("START:\n\tg_isAnyBgJobTerminated = %d\n", g_isAnyBgJobTerminated);
	int olderrno = errno;
	sigset_t mask_all, prev_one;
	sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_one);

	g_isAnyBgJobTerminated = 1;

	sigprocmask(SIG_SETMASK, &prev_one, NULL);
	errno = olderrno;
}

void
sigusr2_handler(int sigNum)
{
	int olderrno = errno;
	sigset_t mask_all, prev_one;
	sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_one);

	sio_puts(G_WEEKDAY[g_currWeekDay]); sio_puts(" ");
    sio_puts(G_MONTH[g_currMonth]); sio_puts(" ");
    sio_put_time(g_currMonthday, 2); sio_puts(" "); // Print day with width 2
   	sio_put_time(g_currHour, 2); sio_puts(":"); // Print hour with width 2
    sio_put_time(g_currMin, 2); sio_puts(":"); // Print minute with width 2
   	sio_put_time(g_currSec, 2); sio_puts(" "); // Print second with width 2
    sio_putl(g_currYear + 1900); sio_puts("\n");

	sigprocmask(SIG_SETMASK, &prev_one, NULL);
	errno = olderrno;
}

void
updateCurrentTime()
{
	struct timespec now;
    struct tm current_time;
    
	// Retrieves the current time of the specified clock:
    if(clock_gettime(CLOCK_REALTIME, &now) == -1) 
		reportUnixError("Error getting current time");

	// Convert the given time since epoch to the corresponding local time representation:
    if(localtime_r(&(now.tv_sec), &current_time) == NULL) 
		reportUnixError("Error converting to local time");
    
   	g_currSec = current_time.tm_sec;
    g_currMin = current_time.tm_min;
   	g_currHour = current_time.tm_hour;
    g_currMonthday = current_time.tm_mday;
    g_currMonth = current_time.tm_mon;
    g_currYear = current_time.tm_year;
    g_currWeekDay = current_time.tm_wday;
}

ssize_t
sio_puts(const char str[])
{
	return write(STDERR_FILENO, str, strlen(str));
}

ssize_t
sio_putl(const long val)
{
    char str[BUFFER_SIZE];
    sio_ltoa(val, str, 10);
    return sio_puts(str);
}

void
sio_put_time(long val, int width)
{
	char str[BUFFER_SIZE];
    sio_ltoa(val, str, 10);
    int len = strlen(str);
	
    for(int i = 0; i < width - len; ++i) 
	{
		sio_puts("0");
	}
    sio_puts(str);
}

void
sio_error(const char str[])
{
    sio_puts(str);
    _exit(EXIT_FAILURE);
}

void
sio_reverse(char str[])
{
    int ch, i, j;
	
    for(i = 0, j = strlen(str)-1; i < j; ++i, --j)
	{
        ch = str[i];
        str[i] = str[j];
        str[j] = ch;
    }
}

void
sio_ltoa(long val, char str[], int base)
{
    int currDigit, index = 0;
    int8_t isNegative = val < 0;
    if(isNegative) val = -val;

    do 
	{
        currDigit = val % base;
        str[index++] = (currDigit < 10) ? currDigit + '0' : currDigit - 10 + 'a';
    } while( (val /= base) > 0 );

    if(isNegative) str[index++] = '-';
    str[index] = '\0';
    sio_reverse(str);
}
