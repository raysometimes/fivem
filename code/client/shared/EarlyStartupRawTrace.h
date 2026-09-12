#pragma once

#include <windows.h>
#include <stdint.h>

typedef enum CfxEarlyRawTraceMarker
{
	CfxRawTrace_FreezeEnter = 1,
	CfxRawTrace_SnapshotBegin = 2,
	CfxRawTrace_SnapshotEnd = 3,
	CfxRawTrace_Thread32FirstBegin = 4,
	CfxRawTrace_Thread32FirstEnd = 5,
	CfxRawTrace_Thread32NextBegin = 6,
	CfxRawTrace_Thread32NextEnd = 7,
	CfxRawTrace_CandidateThread = 8,
	CfxRawTrace_OpenThreadBegin = 9,
	CfxRawTrace_OpenThreadEnd = 10,
	CfxRawTrace_SuspendThreadBegin = 11,
	CfxRawTrace_SuspendThreadEnd = 12,
	CfxRawTrace_GetThreadContextBegin = 13,
	CfxRawTrace_GetThreadContextEnd = 14,
	CfxRawTrace_SetThreadContextBegin = 15,
	CfxRawTrace_SetThreadContextEnd = 16,
	CfxRawTrace_FreezeExit = 17,
	CfxRawTrace_UnfreezeOpenThreadBegin = 18,
	CfxRawTrace_UnfreezeOpenThreadEnd = 19,
	CfxRawTrace_ResumeThreadBegin = 20,
	CfxRawTrace_ResumeThreadEnd = 21
} CfxEarlyRawTraceMarker;

#pragma pack(push, 1)
typedef struct CfxEarlyRawTraceRecord
{
	uint32_t marker;
	uint32_t processId;
	uint32_t threadId;
	uint64_t value1;
	uint64_t value2;
	uint32_t lastError;
} CfxEarlyRawTraceRecord;
#pragma pack(pop)

typedef char CfxEarlyRawTraceRecordSizeMustBe32[
	(sizeof(CfxEarlyRawTraceRecord) == 32) ? 1 : -1];

#ifdef __cplusplus
extern "C"
{
#endif

DWORD CfxEarlyRawTraceOpen(void);
VOID CfxEarlyRawTraceClose(void);
VOID CfxEarlyRawTraceWrite(
	uint32_t marker,
	uint64_t value1,
	uint64_t value2,
	uint32_t lastError);

#ifdef __cplusplus
}
#endif
