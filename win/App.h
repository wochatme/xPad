#pragma once

#include "ztlib.h"
#include "Setting.h"
#include "Network.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)

#define WM_WINEVENT				(WM_USER + 100)

/* ZT_ALIGN() is only to be used to align on a power of 2 boundary */
#define ZT_ALIGN(size, boundary)   (((size) + ((boundary) -1)) & ~((boundary) - 1))
#define ZT_ALIGN_DEFAULT32(size)   ZT_ALIGN(size, 4)
#define ZT_ALIGN_DEFAULT64(size)   ZT_ALIGN(size, 8)      /** Default alignment */
#define ZT_ALIGN_PAGE1K(size)      ZT_ALIGN(size, (1<<10))  
#define ZT_ALIGN_PAGE4K(size)      ZT_ALIGN(size, (1<<12))  
#define ZT_ALIGN_PAGE8K(size)      ZT_ALIGN(size, (1<<13))  
#define ZT_ALIGN_PAGE64K(size)     ZT_ALIGN(size, (1<<16))
#define ZT_ALIGN_TRUETYPE(size)    ZT_ALIGN(size, 64)    

// Release an IUnknown* and set to nullptr.
// While IUnknown::Release must be noexcept, it isn't marked as such so produces
// warnings which are avoided by the catch.
template <class T>
inline void ReleaseUnknown(T*& ppUnknown) noexcept
{
	if (ppUnknown)
	{
		try {
			ppUnknown->Release();
		}
		catch (...) {
			// Never occurs
		}
		ppUnknown = nullptr;
	}
}

/// Find a function in a DLL and convert to a function pointer.
/// This avoids undefined and conditionally defined behaviour.
template<typename T>
inline T DLLFunction(HMODULE hModule, LPCSTR lpProcName) noexcept {
	if (!hModule) {
		return nullptr;
	}
	FARPROC function = ::GetProcAddress(hModule, lpProcName);
	static_assert(sizeof(T) == sizeof(function));
	T fp{};
	memcpy(&fp, &function, sizeof(T));
	return fp;
}

extern DWORD guiState;

extern ID2D1Factory1* g_pD2DFactory;

#define GUISTATE_DARKMODE		(0x00000001)
#define GUISTATE_TOPMOST		(0x00000002)
#define GUISTATE_DRAGFULL		(0x00000004)
#define GUISTATE_DOCREADY		(0x00000008)

inline bool AppIsDocIdReady()
{
	return static_cast<bool>(guiState & GUISTATE_DOCREADY);
}

inline void AppSetDocIdReady(bool ready = true)
{
	guiState = ready ? (guiState | GUISTATE_DOCREADY) : (guiState & ~GUISTATE_DOCREADY);
}


