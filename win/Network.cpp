#include "pch.h"
#include "App.h"

#define ZT_FILE_MAX_SIZE       (1<<28)

typedef struct
{
    HWND hWnd;
    U32 total;
    U32 curr;
    U8* buffer;
} HTTPDownload;

char* g_textData = NULL;
U32 g_textLen = 0;

FileInfo g_fileInfo = { 0 };

static volatile LONG  g_threadCount = 0;
static volatile LONG  g_Quit = 0;

static void DoOpenFileWork(HWND hWnd, LPTSTR path);
static void DoOpenURLWork(HWND hWnd, LPTSTR docId);

static DWORD WINAPI workthreadfunc(void* param);

void StarUpWorkThread(FileInfo* pFI)
{
    DWORD threadid = 0;
    HANDLE hThread = CreateThread(NULL, 0, workthreadfunc, pFI, 0, &threadid);
    if (hThread) /* we don't need the thread handle */
    {
        CloseHandle(hThread);
    }
}

int ztInitNetworkResource()
{
    ATLASSERT(g_textData == NULL);

    g_textData = NULL;
    g_textLen = 0;

	return 0;
}

void ztShutdownNetworkThread()
{
    UINT tries = 20;

    // tell all threads to quit
    InterlockedIncrement(&g_Quit);

    // wait for all threads to quit gracefully
    while (g_threadCount && tries > 0)
    {
        Sleep(1000);
        tries--;
    }

    ATLASSERT(g_threadCount == 0);

    if (g_textData)
    {
        std::free(g_textData);
    }

    g_textData = NULL;
    g_textLen = 0;
}

static DWORD WINAPI workthreadfunc(void* param)
{
    FileInfo* pfi = static_cast<FileInfo*>(param);

    if (pfi && ::IsWindow(pfi->hWnd))
    {
        InterlockedIncrement(&g_threadCount);

        if(pfi->path[0] != L'\0')
            DoOpenFileWork(pfi->hWnd, pfi->path);
        else if(pfi->docId[0] != L'\0')
            DoOpenURLWork(pfi->hWnd, pfi->docId);

        InterlockedDecrement(&g_threadCount);
    }

    return 0;
}

static void DoOpenURLWork(HWND hWnd, LPTSTR docId)
{

}

static void DoOpenFileWork(HWND hWnd, LPTSTR path)
{
    U8* unzipBuf = NULL;

    if (g_textData)
    {
        std::free(g_textData);
    }
    g_textData = NULL;
    g_textLen = 0;

    int fd = _wopen(path, _O_RDONLY | _O_BINARY);
    if (fd >= 0)
    {
        long fileSize = _lseek(fd, 0, SEEK_END);

        if (fileSize > 12 && fileSize < ZT_FILE_MAX_SIZE)
        {
            U8* bindata = static_cast<U8*>(std::malloc(fileSize));
            if (bindata)
            {
                long bytes;
                _lseek(fd, 0, SEEK_SET);
                
                bytes = _read(fd, bindata, fileSize);
                if (bytes == fileSize)
                {
                    uLongf zipSize;
                    U32* p32 = reinterpret_cast<U32*>(bindata);
                    zipSize = *p32;
                    if (zipSize == fileSize)
                    {
                        uLongf  unzipSize;
                        p32 = reinterpret_cast<U32*>(bindata + 4);
                        unzipSize = *p32;
                        if (unzipSize > 4)
                        {
                            unzipBuf = static_cast<U8*>(std::malloc(unzipSize + 1));
                            if (unzipBuf)
                            {
                                uLongf destLen = unzipSize;
                                uLongf sourceLen = zipSize - 8;
                                int rc = uncompress2(unzipBuf, &destLen, bindata + 8, &sourceLen);

                                if (rc == Z_OK && destLen == unzipSize && destLen > 8)
                                {
                                    U32 crc32A, crc32B;
                                    p32 = reinterpret_cast<U32*>(unzipBuf);
                                    crc32A = *p32;
                                    crc32B = zt_crc32(unzipBuf + 4, destLen - 4);
                                    if (crc32A == crc32B)
                                    {
                                        unzipBuf[unzipSize] = 0;
                                        g_textData = reinterpret_cast<char*>(unzipBuf);
                                        g_textLen = destLen;
                                    }
                                }
                            }
                        }
                    }
                }
                std::free(bindata);
            }
        }
        _close(fd);

        if (g_textData == NULL)
        {
            if (unzipBuf != NULL)
            {
                std::free(unzipBuf);
                unzipBuf = NULL;
            }
        }
        else
        {
            if (::IsWindow(hWnd))
            {
                ::PostMessage(hWnd, WM_WINEVENT, 1, 0);
            }
        }
    }
}

