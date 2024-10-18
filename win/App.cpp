// XEdit.cpp : main source file for XEdit.exe
//

#include "pch.h"

#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlctrlw.h>
#include <atlscrl.h>

#include "resource.h"
#include "App.h"
#include "Setting.h"
#include "Network.h"

#include "ViewDocument.h"
#include "ViewFeedback.h"
#include "WinDlg.h"
#include "MainFrm.h"

CAppModule _Module;

DWORD guiState = 0;
ID2D1Factory1* g_pD2DFactory = nullptr;


static D2D1_DRAW_TEXT_OPTIONS d2dDrawTextOptions = D2D1_DRAW_TEXT_OPTIONS_NONE;
static HMODULE hDLLD2D{};

void LoadD2DOnce() noexcept
{
	DWORD loadLibraryFlags = 0;
	HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
	if (kernel32)
	{
		if (::GetProcAddress(kernel32, "SetDefaultDllDirectories"))
		{
			// Availability of SetDefaultDllDirectories implies Windows 8+ or
			// that KB2533623 has been installed so LoadLibraryEx can be called
			// with LOAD_LIBRARY_SEARCH_SYSTEM32.
			loadLibraryFlags = LOAD_LIBRARY_SEARCH_SYSTEM32;
		}
	}

	typedef HRESULT(WINAPI* D2D1CFSig)(D2D1_FACTORY_TYPE factoryType, REFIID riid,
		CONST D2D1_FACTORY_OPTIONS* pFactoryOptions, IUnknown** factory);

	hDLLD2D = ::LoadLibraryEx(TEXT("D2D1.DLL"), 0, loadLibraryFlags);
	D2D1CFSig fnD2DCF = DLLFunction<D2D1CFSig>(hDLLD2D, "D2D1CreateFactory");
	if (fnD2DCF) {
		// A multi threaded factory in case Scintilla is used with multiple GUI threads
		fnD2DCF(D2D1_FACTORY_TYPE_SINGLE_THREADED, /*D2D1_FACTORY_TYPE_MULTI_THREADED,*/
			__uuidof(ID2D1Factory),
			nullptr,
			reinterpret_cast<IUnknown**>(&g_pD2DFactory));
	}
}

static bool LoadD2D() noexcept
{
	static std::once_flag once;
	try {
		std::call_once(once, LoadD2DOnce);
	}
	catch (...) {
		// ignore
	}
	return (g_pD2DFactory != nullptr);
}


static int AppRun(LPTSTR /*lpstrCmdLine*/ = NULL, int nCmdShow = SW_SHOWDEFAULT)
{
	int nRet = 0;
	CMessageLoop theLoop;
	_Module.AddMessageLoop(&theLoop);

	//CMainFrame wndMain;
	const auto wndMain = std::make_shared<CMainFrame>();	

	if(wndMain->CreateEx() != NULL)
	{
		wndMain->ShowWindow(nCmdShow);
		nRet = theLoop.Run();
	}

	_Module.RemoveMessageLoop();
	return nRet;
}

static int AppInit(HINSTANCE hInstance)
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	if (Scintilla_RegisterClasses(hInstance) == 0)
		return 1;

	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
		return 2;

	if (!LoadD2D())
		return 3;

	if (ztInitNetworkResource())
		return 4;

	return 0;
}

static int AppTerm(HINSTANCE hInstance = NULL)
{
	ztShutdownNetworkThread();

	if (hDLLD2D)
	{
		FreeLibrary(hDLLD2D);
		hDLLD2D = {};
	}

	curl_global_cleanup();
	Scintilla_ReleaseResources();

	return 0;
}

int WINAPI _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpstrCmdLine, _In_ int nCmdShow)
{
	int nRet;

	HRESULT hRes = ::CoInitialize(NULL);
	ATLASSERT(SUCCEEDED(hRes));

	g_fileInfo.path[0] = L'\0';
	g_fileInfo.docId[0] = L'\0';

	AppSetDocIdReady(false);

	AtlInitCommonControls(ICC_COOL_CLASSES | ICC_BAR_CLASSES);	// add flags to support other controls

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

	// parse the document ID
	if (lpstrCmdLine)
	{
		size_t fileLen = wcslen(lpstrCmdLine);
		if (fileLen > 0)
		{
			WIN32_FILE_ATTRIBUTE_DATA fileInfo;
			if (GetFileAttributesExW(lpstrCmdLine, GetFileExInfoStandard, &fileInfo))
			{
				if (!(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					if (fileLen < MAX_PATH)
					{
						for(size_t i=0; i<fileLen; i++) g_fileInfo.path[i] = lpstrCmdLine[i];
						g_fileInfo.path[fileLen] = L'\0';
						AppSetDocIdReady(true);
					}
				}
			}
			else if (fileLen == 16)
			{
				if (zt_IsAlphabetStringW(lpstrCmdLine, 16))
				{
					for (int i = 0; i < 16; i++) g_fileInfo.docId[i] = lpstrCmdLine[i];
					AppSetDocIdReady(true);
				}
			}
		}
	}

	nRet = AppInit(hInstance);
	if (nRet == 0)
	{
		AppRun(lpstrCmdLine, nCmdShow);
	}

	AppTerm();

	_Module.Term();
	::CoUninitialize();

	return nRet;
}
