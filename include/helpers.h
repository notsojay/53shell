// A header file for helpers.c
// Declare any additional functions in this file
#ifndef HELPERS_H
#define HELPERS_H

#include "linkedlist.h"
#include "icssh.h"

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#define CMD_FAIL -1
#define CMD_SUCC 0
#define CMD_NOT_FOUND 1
#define BUFFER_SIZE 256
#define CHILD 0

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

//#define DEBUG_PRINT
#ifdef DEBUG_PRINT
#define debug_print(fmt, ...) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) do {} while(0)
#endif

#endif // !HELPERS_H