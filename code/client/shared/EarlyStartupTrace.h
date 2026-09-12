#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * Text tracing is only used while no FiveM thread has been deliberately
 * suspended by MinHook.
 */
static __inline void CfxEarlyTextTrace(const char* format, ...)
{
	DWORD savedLastError = GetLastError();
	static const WCHAR suffix[] = L".early-startup.log";
	WCHAR tracePath[MAX_PATH + 32];
	char message[1024];
	char line[1280];
	DWORD pathLength;
	int lineLength;
	HANDLE file;
	DWORD written;
	va_list args;

	pathLength = GetModuleFileNameW(
		NULL,
		tracePath,
		(DWORD)(sizeof(tracePath) / sizeof(tracePath[0])));

	if (pathLength == 0 ||
		pathLength + (sizeof(suffix) / sizeof(suffix[0])) >
			(sizeof(tracePath) / sizeof(tracePath[0])))
	{
		SetLastError(savedLastError);
		return;
	}

	memcpy(&tracePath[pathLength], suffix, sizeof(suffix));

	va_start(args, format);
	_vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);

	lineLength = _snprintf_s(
		line,
		sizeof(line),
		_TRUNCATE,
		"%llu pid=%lu tid=%lu %s\r\n",
		(unsigned long long)GetTickCount64(),
		(unsigned long)GetCurrentProcessId(),
		(unsigned long)GetCurrentThreadId(),
		message);

	if (lineLength < 0)
	{
		lineLength = (int)strlen(line);
	}

	file = CreateFileW(
		tracePath,
		FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (file != INVALID_HANDLE_VALUE)
	{
		if (lineLength > 0)
		{
			WriteFile(file, line, (DWORD)lineLength, &written, NULL);
		}

		CloseHandle(file);
	}

	SetLastError(savedLastError);
}
