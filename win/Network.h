#pragma once

typedef struct FileInfo
{
	HWND hWnd;
	WCHAR docId[16];
	WCHAR path[MAX_PATH + 1];
} FileInfo;

extern FileInfo g_fileInfo;

extern char* g_textData;
extern U32 g_textLen;

void StarUpWorkThread(FileInfo* pFI);

int ztInitNetworkResource();

void ztShutdownNetworkThread();


