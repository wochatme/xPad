// View.h : interface of the CViewDocument class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once
#if 0
class CViewDocument : public CWindowImpl<CViewDocument>
{
public:
	DECLARE_WND_CLASS(NULL)

	BOOL PreTranslateMessage(MSG* pMsg)
	{
		pMsg;
		return FALSE;
	}

	BEGIN_MSG_MAP(CViewDocument)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)
};
#endif 

class CViewDocument
{
public:
	HWND m_hWnd = NULL;

	HWND Create(
		_In_opt_ HWND hWndParent,
		_In_ _U_RECT rect = NULL,
		_In_opt_z_ LPCTSTR szWindowName = NULL,
		_In_ DWORD dwStyle = 0,
		_In_ DWORD dwExStyle = 0,
		_In_ _U_MENUorID MenuOrID = 0U,
		_In_opt_ LPVOID lpCreateParam = NULL)
	{
		m_hWnd = CreateWindowExW(0, L"Scintilla", NULL,
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
			0, 0, 16, 16, hWndParent, NULL, HINST_THISCOMPONENT, NULL);

		if (::IsWindow(m_hWnd))
		{
			::SendMessage(m_hWnd, SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DIRECTWRITE, 0);
			::SendMessage(m_hWnd, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
			::SendMessage(m_hWnd, SCI_SETEOLMODE, SC_EOL_LF, 0);
			::SendMessage(m_hWnd, SCI_SETWRAPMODE, SC_WRAP_WORD, 0);
			::SendMessage(m_hWnd, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Courier New");
			::SendMessage(m_hWnd, SCI_STYLESETSIZEFRACTIONAL, STYLE_DEFAULT, 1300);

			//::SendMessage(m_hWnd, SCI_STYLESETBACK, STYLE_LINENUMBER, RGB(255, 255, 255));
			//::SendMessage(m_hWnd, SCI_SETMARGINWIDTHN, 1, 2);
			::SendMessage(m_hWnd, SCI_SETPHASESDRAW, SC_PHASES_MULTIPLE, 0);
#if 0
			//::SendMessage(m_hWnd, SCI_SETTEXT, 0, (LPARAM)ai_greeting);
			//::SendMessage(m_hWnd, SCI_SETREADONLY, 1, 0);
			::SendMessage(m_hWnd, SCI_SETMARGINWIDTHN, 0, 0);
			::SendMessage(m_hWnd, SCI_SETMARGINS, 0, 0);
			::SendMessage(m_hWnd, SCI_SETMARGINLEFT, 0, 0);
#endif 

#if 0
			::SetWindowTheme(m_hWnd, L"DarkMode_Explorer", nullptr);
			::SendMessage(m_hWnd, SCI_SETVSCROLLBAR, 0, 0);
#endif 
		}
		return m_hWnd;
	}

	operator HWND() const throw()
	{
		return m_hWnd;
	}

	BOOL IsWindow() const throw()
	{
		return ::IsWindow(m_hWnd);
	}

	BOOL GetFocus()
	{
		return (m_hWnd == ::GetFocus());
	}

	BOOL ShowWindow(int nCmdShow)
	{
		BOOL bRet = FALSE;

		if (IsWindow())
		{
			bRet = ::ShowWindow(m_hWnd, nCmdShow);
		}

		return bRet;
	}

	int SetText(const char* text)
	{
		if (IsWindow() && text)
		{
			::SendMessage(m_hWnd, SCI_SETREADONLY, 0, 0);
			::SendMessage(m_hWnd, SCI_SETTEXT, 0, (LPARAM)text);
			::SendMessage(m_hWnd, SCI_SETREADONLY, 1, 0);
		}
		return 0;
	}

};

