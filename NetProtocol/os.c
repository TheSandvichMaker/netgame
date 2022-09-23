// ------------------------------------------------------------------
// standard library and OS includes

// get rid of nonsense deprecation warnings on perfectly safe 
// snprintf style functions.
#define _CRT_SECURE_NO_WARNINGS

#include <string.h>
#include <stdio.h>
#include <stdint.h>

// this stops windows.h from defining 'min' and 'max' as macros
#define NOMINMAX
// and this stops it from including less often used headers
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// ------------------------------------------------------------------
// internal includes

#include "os.h"
#include "util.h"

// ------------------------------------------------------------------
// os.c: implements various functions to abstract OS APIs through a
// platform independent interface


// ------------------------------------------------------------------
// high resolution timer

static LARGE_INTEGER g_qpcfreq;

os_time_t OS_GetHiresTime(void)
{
    if (g_qpcfreq.QuadPart == 0)
    {
        if (!QueryPerformanceFrequency(&g_qpcfreq))
        {
            OS_PError("OS_GetHiresTime, QueryPerformanceFrequency");
            return 0;
        }
    }

	LARGE_INTEGER result = { 0 };
	QueryPerformanceCounter(&result);
	return result.QuadPart;
}

double OS_GetSecondsElapsed(os_time_t start, os_time_t end)
{
	return ((double)end - (double)start) / (double)(g_qpcfreq.QuadPart);
}

// ------------------------------------------------------------------
// error reporting

void OS_PError(char *message)
{
	wchar_t buffer[1024];

    // it is undocumented what happens to buffer if FormatMessage fails, 
    // so set up a null terminator just in case
    buffer[0] = 0;

    // this also returns error messages for winsock that the MSDN says to
    // get via WSAGetLastError(), so we can use OS_PError with winsock code.
	int err = GetLastError();

	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
				   NULL,
				   err, 
				   MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
				   buffer,              
				   ARRAY_COUNT(buffer),    
				   NULL);               

    if (!buffer[0])
    {
	    // provide error # if no string available
        _snwprintf(buffer, ARRAY_COUNT(buffer), L"%S: (%d)", message, err); 
    }

    fwprintf(stderr, L"%S: (%d) %s", message, err, buffer);
}

// ------------------------------------------------------------------
// spawning new processes

int OS_Execute(char *command, int *exit_code)
{
    int command_count = (int)strlen(command);
    int wide_count = MultiByteToWideChar(CP_UTF8, 0, command, command_count, NULL, 0);

    wchar_t *wide = HeapAlloc(GetProcessHeap(), 0, sizeof(wchar_t)*wide_count);
    MultiByteToWideChar(CP_UTF8, 0, command, command_count, wide, wide_count);

    HANDLE stdout_read, stdout_write;
    HANDLE stderr_read, stderr_write;

    SECURITY_ATTRIBUTES s_attr = {
        .nLength        = sizeof(s_attr),
        .bInheritHandle = TRUE,
    };

    if (!CreatePipe(&stdout_read, &stdout_write, &s_attr, 0))
        return 0;

    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0))
        return 0;

    if (!CreatePipe(&stderr_read, &stderr_write, &s_attr, 0))
        return 0;

    if (!SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0))
        return 0;

    STARTUPINFOW startup = {
        .cb = sizeof(startup),
        .hStdOutput = stdout_write,
        .hStdError  = stderr_write,
        .hStdInput  = GetStdHandle(STD_INPUT_HANDLE),
        .dwFlags    = STARTF_USESTDHANDLES,
    };

    PROCESS_INFORMATION info;
    BOOL result = CreateProcessW(NULL,
                                 wide,
                                 NULL,
                                 NULL,
                                 TRUE,
                                 0,
                                 NULL,
                                 NULL,
                                 &startup,
                                 &info);

    HeapFree(GetProcessHeap(), 0, wide);

    if (!result)
    {
        return 0;
    }

    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    if (result)
    {
        HANDLE standard_out = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE standard_err = GetStdHandle(STD_ERROR_HANDLE);

        enum { BUFFER_SIZE = 4096 };
        char buffer[BUFFER_SIZE];
        DWORD read, written;
        BOOL success;

        for (;;)
        {
            success = ReadFile(stdout_read, buffer, BUFFER_SIZE, &read, NULL);
            if (!success || read == 0)  break;

            success = WriteFile(standard_out, buffer, read, &written, NULL);
            if (!success)  break;
        }

        for (;;)
        {
            success = ReadFile(stderr_read, buffer, BUFFER_SIZE, &read, NULL);
            if (!success || read == 0)  break;

            success = WriteFile(standard_err, buffer, read, &written, NULL);
            if (!success)  break;
        }
    }

    if (exit_code)
    {
        GetExitCodeProcess(info.hProcess, (DWORD *)exit_code);
    }

    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);

    CloseHandle(stdout_read);
    CloseHandle(stderr_read);

    return result;
}

int OS_StartProcess(char *command)
{
    int command_count = (int)strlen(command);
    int wide_count = MultiByteToWideChar(CP_UTF8, 0, command, command_count, NULL, 0);

    wchar_t *wide = HeapAlloc(GetProcessHeap(), 0, sizeof(wchar_t)*wide_count);
    MultiByteToWideChar(CP_UTF8, 0, command, command_count, wide, wide_count);

    SECURITY_ATTRIBUTES s_attr = {
        .nLength        = sizeof(s_attr),
        .bInheritHandle = TRUE,
    };

    STARTUPINFOW startup = {
        .cb = sizeof(startup),
    };

    PROCESS_INFORMATION info;
    BOOL result = CreateProcessW(NULL,
                                 wide,
                                 NULL,
                                 NULL,
                                 FALSE,
                                 0,
                                 NULL,
                                 NULL,
                                 &startup,
                                 &info);

    HeapFree(GetProcessHeap(), 0, wide);

    if (!result)
    {
        return 0;
    }

    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);

    return result;
}

// ------------------------------------------------------------------
// zzz...

void OS_Sleep(unsigned milliseconds)
{
    Sleep((DWORD)milliseconds);
}
