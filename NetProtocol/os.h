#pragma once

#include <stdint.h>

typedef uint64_t os_time_t;

// returns an opaque high resolution timestamp, use OS_GetSecondsElapsed
// to turn it into seconds in some meaningful way
os_time_t OS_GetHiresTime(void);

// returns the difference between two OS_GetHiresTime timestamps in
// seconds
double   OS_GetSecondsElapsed(os_time_t start, os_time_t end);

// prints the last error code (GetLastError() on win32) with the passed
// in message
void OS_PError(char *message);

// start a process, pipes its stdout and stderr to ours, and waits
// for the process to finish before returning
int OS_Execute(char *command, int *exit_code);

// starts a process and returns immediately
int OS_StartProcess(char *command);

// sleep... zzz...
void OS_Sleep(unsigned milliseconds);
