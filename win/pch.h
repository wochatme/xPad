// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//  are changed infrequently
//

#pragma once

// Including SDKDDKVer.h defines the highest available Windows platform.
// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.
#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
// C RunTime Header Files
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <Mmsystem.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#include <atlbase.h>
#include <atlapp.h>
#include <atlcoll.h>
#include <atlstr.h>

extern CAppModule _Module;

#include <atlwin.h>

#include <mutex>
#include <memory>
#include <array>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <cmath>
#include <stack>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dcomp.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "Dcomp.lib")

#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "Imm32.lib")

#include "scintilla/include/Sci_Position.h"
#include "scintilla/include/scintilla.h"

#if defined _M_IX86
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
  #pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
