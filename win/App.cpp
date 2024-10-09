// XEdit.cpp : main source file for XEdit.exe
//

#include "pch.h"

#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlctrlw.h>
#include <atlscrl.h>

#include "resource.h"

#include "View.h"
#include "WinDlg.h"
#include "MainFrm.h"

CAppModule _Module;

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
	return 0;
}

static int AppTerm(HINSTANCE hInstance = NULL)
{
	return 0;
}

int WINAPI _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpstrCmdLine, _In_ int nCmdShow)
{
	int nRet;

	HRESULT hRes = ::CoInitialize(NULL);
	ATLASSERT(SUCCEEDED(hRes));

	AtlInitCommonControls(ICC_COOL_CLASSES | ICC_BAR_CLASSES);	// add flags to support other controls

	hRes = _Module.Init(NULL, hInstance);
	ATLASSERT(SUCCEEDED(hRes));

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
