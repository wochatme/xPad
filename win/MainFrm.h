// MainFrm.h : interface of the CMainFrame class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

static OPENFILENAME ofn = { 0 };       // common dialog box structure
TCHAR path[MAX_PATH + 1] = { 0 };

#define BTN_OPENFILE			1
#define BTN_SUBMITT				2

#define STATE_SET_CURSOR		(0x00000001)

#define FEEDBACK_WIN_HEIGHT		(96)
#define STATUS_HEIGHT			(36)

#define BKGCOLOR_LIGHT			(0xFFF0F0F0)
#define BKGCOLOR_DARK			(0xFFF0F0F0)


// Splitter panes constants
#define SPLIT_PANE_LEFT			 0
#define SPLIT_PANE_RIGHT		 1
#define SPLIT_PANE_TOP			 SPLIT_PANE_LEFT
#define SPLIT_PANE_BOTTOM		 SPLIT_PANE_RIGHT
#define SPLIT_PANE_NONE			-1

// Splitter extended styles
#define SPLIT_PROPORTIONAL		0x00000001
#define SPLIT_NONINTERACTIVE	0x00000002
#define SPLIT_RIGHTALIGNED		0x00000004
#define SPLIT_BOTTOMALIGNED		SPLIT_RIGHTALIGNED
#define SPLIT_GRADIENTBAR		0x00000008
#define SPLIT_FLATBAR			0x00000020
#define SPLIT_FIXEDBARSIZE		0x00000010

// Note: SPLIT_PROPORTIONAL and SPLIT_RIGHTALIGNED/SPLIT_BOTTOMALIGNED are 
// mutually exclusive. If both are set, splitter defaults to SPLIT_PROPORTIONAL.
// Also, SPLIT_FLATBAR overrides SPLIT_GRADIENTBAR if both are set.

class CMainFrame : 
	public CFrameWindowImpl<CMainFrame>, 
	public CUpdateUI<CMainFrame>,
	public CMessageFilter, public CIdleHandler
{
	DWORD m_dwState = 0;

	RECT m_rectBtn = { 0 };
	LPRECT m_lpRectPress = nullptr;

	HCURSOR m_hCursorNS = NULL;
	HCURSOR m_hCursorHand = NULL;

	U32* m_statusBuff = NULL;
	int  m_statusHeight = STATUS_HEIGHT;

	float m_deviceScaleFactor = 1.f;
	ID2D1HwndRenderTarget* m_pD2DRenderTarget = nullptr;
	ID2D1Bitmap* m_bitmapSplit = nullptr;

public:
	enum { m_nPanesCount = 2, m_nPropMax = INT_MAX, m_cxyStep = 1 };

	HWND m_hWndPane[m_nPanesCount] = { NULL, NULL };
	RECT m_rcSplitter = { 0 };
	int m_xySplitterPos = -1;            // splitter bar position
	int m_xySplitterPosNew = -1;         // internal - new position while moving
	HWND m_hWndFocusSave = NULL;
	int m_nDefActivePane = SPLIT_PANE_NONE;
	int m_cxySplitBar = 4;              // splitter bar width/height
	int m_cxyMin = 0;                   // minimum pane size
	int m_cxyBarEdge = 0;              	// splitter bar edge
	int m_cxyDragOffset = 0;		// internal
	int m_nProportionalPos = 0;
	bool m_bUpdateProportionalPos = true;
	int m_nSinglePane = SPLIT_PANE_NONE;              // single pane mode
	int m_xySplitterDefPos = -1;         // default position
	bool m_bProportionalDefPos = false;     // porportinal def pos

public:
	DECLARE_FRAME_WND_CLASS(NULL, IDR_MAINFRAME)

	CViewDocument m_viewDoc;
	CViewFeedback m_viewFed;

	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if(CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg))
			return TRUE;

		return FALSE; // m_view.PreTranslateMessage(pMsg);
	}

	virtual BOOL OnIdle()
	{
		UIUpdateToolBar();
		return FALSE;
	}

	BEGIN_UPDATE_UI_MAP(CMainFrame)
	END_UPDATE_UI_MAP()

	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_WINEVENT, OnWinEvent)
		MESSAGE_HANDLER(WM_SYSCOMMAND, OnSYSCommand)
		MESSAGE_HANDLER(WM_NCCREATE, OnNCCreate)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
#if 0
		COMMAND_ID_HANDLER(IDM_ABOUTAPP, OnAppAbout)
#endif
		CHAIN_MSG_MAP(CUpdateUI<CMainFrame>)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		return 1L;
	}

	LRESULT OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
		lpMMI->ptMinTrackSize.x = 800;
		lpMMI->ptMinTrackSize.y = 600;
		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		HRESULT hr;
		PAINTSTRUCT ps = { 0 };
		BeginPaint(&ps);

		DoPaint();

		EndPaint(&ps);
		return 0;
	}

	LRESULT OnNCCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		bHandled = FALSE;
		return 0L;
	}

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_HSCROLL | WS_VSCROLL;
		HMENU hmenuSys;

		// register object for message filtering and idle updates
		CMessageLoop* pLoop = _Module.GetMessageLoop();
		ATLASSERT(pLoop != NULL);
		pLoop->AddMessageFilter(this);
		pLoop->AddIdleHandler(this);

		Init();

		m_viewDoc.Create(m_hWnd, rcDefault, NULL, dwStyle);
		m_viewFed.Create(m_hWnd, rcDefault, NULL, dwStyle);

		if (!AppIsDocIdReady())
		{
			m_viewDoc.ShowWindow(SW_HIDE);
			m_viewFed.ShowWindow(SW_HIDE);
		}

		hmenuSys = GetSystemMenu(FALSE);
		if (hmenuSys)
		{
			AppendMenu(hmenuSys, MF_SEPARATOR, 0, 0);
			AppendMenu(hmenuSys, MF_ENABLED, IDM_OPENFILE, L"Open File");
			AppendMenu(hmenuSys, MF_ENABLED, IDM_OPENURL, L"Open URL");
			AppendMenu(hmenuSys, MF_ENABLED, IDM_DARKMODE, L"Dark Mode");
			AppendMenu(hmenuSys, MF_SEPARATOR, 0, 0);
			AppendMenu(hmenuSys, MF_ENABLED, IDM_ABOUTAPP, L"About MiniPad");
		}

		SetSplitterPanes(m_viewDoc.m_hWnd, m_viewFed.m_hWnd, false);

		if (g_fileInfo.path[0] != L'\0' || g_fileInfo.docId[0] != L'\0')
		{
			g_fileInfo.hWnd = m_hWnd;
			StarUpWorkThread(&g_fileInfo);
		}

		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
	{
		// unregister message filtering and idle updates
		CMessageLoop* pLoop = _Module.GetMessageLoop();
		ATLASSERT(pLoop != NULL);
		pLoop->RemoveMessageFilter(this);
		pLoop->RemoveIdleHandler(this);

		if (nullptr != m_statusBuff)
		{
			VirtualFree(m_statusBuff, 0, MEM_RELEASE);
		}
		m_statusBuff = nullptr;

		ReleaseUnknown(m_bitmapSplit);
		ReleaseUnknown(m_pD2DRenderTarget);

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnSYSCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		switch (wParam & ~0xF)
		{
		case IDM_OPENFILE:
			DoOpenFile();
			break;
		case IDM_OPENURL:
			{
				COpenURLDlg dlg;
				if (IDOK == dlg.DoModal())
				{
					//DoOpenURL();
				}
			}
			break;
		case IDM_ABOUTAPP:
			{
				CAboutDlg dlg;
				dlg.DoModal();
			}
			break;
		default:
			bHandled = FALSE;
			break;
		}
		return 0L;
	}

	void DoOpenFile()
	{
#if 0
		OPENFILENAME ofn = { 0 };       // common dialog box structure
		TCHAR path[MAX_PATH + 1] = { 0 };
#endif 
		// Initialize OPENFILENAME
		memset(&ofn, 0, sizeof(ofn));
		ofn.lStructSize = sizeof(OPENFILENAMEW);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFile = path;

		//
		// Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
		// use the contents of file to initialize itself.
		//
		ofn.lpstrFile[0] = _T('\0');
		ofn.nMaxFile = MAX_PATH; //sizeof(path) / sizeof(TCHAR);
		ofn.lpstrFilter = _T("All Files(*.*)\0*.*\0\0");
		ofn.nFilterIndex = 1;
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

		/* Display the Open dialog box. */
		if (GetOpenFileName(&ofn))
		{

		}
#if 0
		CFileDialog fileDlg(TRUE, 
			NULL, 
			NULL, 
			OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST,
			_T("All Files (*.*)\0*.*\0"), 
			m_hWnd
			);

		if (fileDlg.DoModal() == IDOK)
		{
			TCHAR szFile[MAX_PATH + 1] = { 0 };
			fileDlg.GetFilePath(szFile, MAX_PATH);

			StarUpWorkThread(m_hWnd);
		}
#endif 
#if 0
		OPENFILENAME ofn = { 0 };
		memset(&ofn, 0, sizeof(ofn));

		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = m_hWnd;
		ofn.lpstrFile = g_fileInfo.path;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrDefExt = NULL;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLESIZING;
		ofn.hInstance = HINST_THISCOMPONENT;
		ofn.lpstrFile[0] = '\0';
		ofn.lpstrFilter = _T("All Files (*.*)\0*.*\0");
		ofn.lpstrFileTitle = NULL;
		ofn.nMaxFileTitle = 0;
		ofn.lpstrInitialDir = NULL;

		// Display the Open dialog box 
		if (GetOpenFileName(&ofn))
		{
			MessageBox(ofn.lpstrFile, _T("Selected File"), MB_OK);
#if 0
			size_t len = wcslen(ofn.lpstrFile);
			if (len > 0 && len < MAX_PATH)
			{
				g_fileInfo.hWnd = m_hWnd;
				g_fileInfo.docId[0] = L'\0';

				for (size_t i = 0; i < len; i++)
				{
					g_fileInfo.path[i] = ofn.lpstrFile[i];
				}
				g_fileInfo.path[len] = L'\0';

				// tell the network thread to open the file
				StarUpWorkThread(m_hWnd);
			}
#endif 
		}
#endif 
	}

	LRESULT OnWinEvent(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		if (wParam == 0)
		{
			int idx = static_cast<int>(lParam);
			switch (idx)
			{
			case BTN_OPENFILE:
				DoOpenFile();
				break;
			default:
				break;
			}
		}
		else
		{
			if (g_textData && g_textLen > 8)
				m_viewDoc.SetText(g_textData + 8);
		}

		return 0;
	}

	LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		m_rcSplitter.left = m_rcSplitter.right = m_rcSplitter.top = m_rcSplitter.bottom = 0;

		if (nullptr != m_statusBuff)
		{
			VirtualFree(m_statusBuff, 0, MEM_RELEASE);
		}
		m_statusBuff = nullptr;

		if (wParam != SIZE_MINIMIZED)
		{
			DoSize();
		}
		return 0L;
	}

	LRESULT OnSetCursor(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		if (((HWND)wParam == m_hWnd) && (LOWORD(lParam) == HTCLIENT))
		{
#if 0
			DWORD dwPos = ::GetMessagePos();
			POINT ptPos = { GET_X_LPARAM(dwPos), GET_Y_LPARAM(dwPos) };
			ScreenToClient(&ptPos);
			if (IsOverSplitterBar(ptPos.x, ptPos.y))
				return 1;
#endif
			if (m_dwState & STATE_SET_CURSOR)
			{
				m_dwState &= ~STATE_SET_CURSOR;
				return 1;
			}
		}
		m_dwState &= ~STATE_SET_CURSOR;
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		int xPos = GET_X_LPARAM(lParam);
		int yPos = GET_Y_LPARAM(lParam);

		POINT pt = { xPos, yPos };

		if (AppIsDocIdReady())
		{
			if (::GetCapture() == m_hWnd)
			{
				if (m_lpRectPress == nullptr)
				{
					int h = m_rcSplitter.bottom - m_rcSplitter.top;

					int xyNewSplitPos = yPos - m_rcSplitter.top - m_cxyDragOffset;
					if (xyNewSplitPos == -1)   // avoid -1, that means default position
						xyNewSplitPos = -2;

					if (m_xySplitterPos != xyNewSplitPos)
					{
						if (xyNewSplitPos > 128 && xyNewSplitPos < h - 80)
						{
							DrawGhostBar();
							SetSplitterPos(xyNewSplitPos, false);
							DrawGhostBar();
						}
					}
				}
			}
			else		// not dragging, just set cursor
			{
				POINT pt = { xPos, yPos };
				if (AppIsDocIdReady() && IsOverSplitterBar(xPos, yPos))
				{
					m_dwState |= STATE_SET_CURSOR;
					::SetCursor(m_hCursorNS);
				}
#if 0
				else
				{
					int i;

					for (i = RECT_FIRST; i < RECT_LAST; i++)
					{
						if (::PtInRect(&m_rectButtons[i], pt))
							break;
					}
					if (i < RECT_LAST && (m_lpRectActive != &m_rectButtons[i]))
					{
						m_dwState |= STATE_SET_CURSOR;
						::SetCursor(m_hCursorHand);
					}
				}
				if (::PtInRect(&m_rectBtn, pt))
				{
					m_dwState |= STATE_SET_CURSOR;
					::SetCursor(m_hCursorHand);
				}
#endif 
				bHandled = FALSE;
			}
		}
		else if (::GetCapture() != m_hWnd)
		{
			if (::PtInRect(&m_rectBtn, pt))
			{
				m_dwState |= STATE_SET_CURSOR;
				::SetCursor(m_hCursorHand);
			}
		}

		return 0;
	}


	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		bool bHit = false;
		int xPos = GET_X_LPARAM(lParam);
		int yPos = GET_Y_LPARAM(lParam);
		
		POINT pt = { xPos, yPos };

		if (AppIsDocIdReady())
		{
			if ((::GetCapture() != m_hWnd) && IsOverSplitterBar(xPos, yPos))
			{
				m_xySplitterPosNew = m_xySplitterPos;
				SetCapture();
				m_hWndFocusSave = SetFocus();
				::SetCursor(m_hCursorNS);

				DrawGhostBar();

				m_cxyDragOffset = yPos - m_rcSplitter.top - m_xySplitterPos;
			}
			else if ((::GetCapture() == m_hWnd) && !IsOverSplitterBar(xPos, yPos))
			{
				::ReleaseCapture();
			}
		}
		else
		{
			if (::PtInRect(&m_rectBtn, pt))
			{
				bHit = true;
			}

			if ((::GetCapture() != m_hWnd) && bHit)
			{
				m_lpRectPress = &m_rectBtn;
				SetCapture();
				m_dwState |= STATE_SET_CURSOR;
				::SetCursor(m_hCursorHand);
				InvalidateRect(m_lpRectPress);
			}
		}

		if(!bHit)
			PostMessage(WM_NCLBUTTONDOWN, HTCAPTION, lParam);

		//bHandled = FALSE;
		return 1;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		if (::GetCapture() == m_hWnd)
		{
			if (AppIsDocIdReady())
			{
				m_xySplitterPosNew = m_xySplitterPos;
				::ReleaseCapture();
			}
			else
			{
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

				::ReleaseCapture();

				if (m_lpRectPress)
				{
					InvalidateRect(m_lpRectPress);
					m_lpRectPress = NULL;
				}

				if (::PtInRect(&m_rectBtn, pt))
				{
					PostMessage(WM_WINEVENT, 0, BTN_OPENFILE);
				}
			}
		}
		//bHandled = FALSE;
		return 1;
	}

	LRESULT OnCaptureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		if (AppIsDocIdReady())
		{
			DrawGhostBar();

			if (m_xySplitterPosNew != -1)
			{
				m_xySplitterPos = m_xySplitterPosNew;
				m_xySplitterPosNew = -1;
				UpdateSplitterLayout();

				UpdateWindow();
			}

			if (m_hWndFocusSave != NULL)
				::SetFocus(m_hWndFocusSave);
		}

		return 0;
	}

	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
	{
		if (AppIsDocIdReady() && ::GetCapture() == m_hWnd)
		{
			switch (wParam)
			{
			case VK_RETURN:
				m_xySplitterPosNew = m_xySplitterPos;
				// FALLTHROUGH
			case VK_ESCAPE:
				::ReleaseCapture();
				break;
			case VK_UP:
			case VK_DOWN:
				{
					POINT pt = {};
					::GetCursorPos(&pt);
					int xyPos = m_xySplitterPos + ((wParam == VK_UP) ? -m_cxyStep : m_cxyStep);
					int cxyMax = m_rcSplitter.bottom - m_rcSplitter.top;
					if (xyPos < (m_cxyMin + m_cxyBarEdge))
						xyPos = m_cxyMin;
					else if (xyPos > (cxyMax - m_cxySplitBar - m_cxyBarEdge - m_cxyMin))
						xyPos = cxyMax - m_cxySplitBar - m_cxyBarEdge - m_cxyMin;
					pt.y += xyPos - m_xySplitterPos;
					::SetCursorPos(pt.x, pt.y);
				}
				break;
			default:
				break;
			}
		}
		else
		{
			bHandled = FALSE;
		}

		return 0;
	}

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM, BOOL& bHandled)
	{
		if (AppIsDocIdReady() && ::GetCapture() != m_hWnd)
		{
			if (m_nSinglePane == SPLIT_PANE_NONE)
			{
				if ((m_nDefActivePane == SPLIT_PANE_LEFT) || (m_nDefActivePane == SPLIT_PANE_RIGHT))
					::SetFocus(m_hWndPane[m_nDefActivePane]);
			}
			else
			{
				::SetFocus(m_hWndPane[m_nSinglePane]);
			}
		}

		bHandled = FALSE;
		return 1;
	}

	LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		LRESULT lRet = DefWindowProc(uMsg, wParam, lParam);
		if (AppIsDocIdReady())
		{
			if ((lRet == MA_ACTIVATE) || (lRet == MA_ACTIVATEANDEAT))
			{
				DWORD dwPos = ::GetMessagePos();
				POINT pt = { GET_X_LPARAM(dwPos), GET_Y_LPARAM(dwPos) };
				ScreenToClient(&pt);
				RECT rcPane = {};
				for (int nPane = 0; nPane < m_nPanesCount; nPane++)
				{
					if (GetSplitterPaneRect(nPane, &rcPane) && (::PtInRect(&rcPane, pt) != FALSE))
					{
						m_nDefActivePane = nPane;
						break;
					}
				}
			}
		}
		return lRet;
	}

#if 0
	LRESULT OnAppAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		CAboutDlg dlg;
		dlg.DoModal();
		return 0;
	}
#endif 

	int GetFirstIntegralMultipleDeviceScaleFactor() const noexcept
	{
		return static_cast<int>(std::ceil(m_deviceScaleFactor));
	}

	D2D1_SIZE_U GetSizeUFromRect(const RECT& rc, const int scaleFactor) noexcept
	{
		const long width = rc.right - rc.left;
		const long height = rc.bottom - rc.top;
		const UINT32 scaledWidth = width * scaleFactor;
		const UINT32 scaledHeight = height * scaleFactor;
		return D2D1::SizeU(scaledWidth, scaledHeight);
	}

	HRESULT CreateDeviceDependantResource(int left, int top, int right, int bottom)
	{
		HRESULT hr = S_OK;
		if (nullptr == m_pD2DRenderTarget)  // Create a Direct2D render target.
		{
			RECT rc = { 0 };
			const int integralDeviceScaleFactor = GetFirstIntegralMultipleDeviceScaleFactor();
			D2D1_RENDER_TARGET_PROPERTIES drtp{};
			drtp.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
			drtp.usage = D2D1_RENDER_TARGET_USAGE_NONE;
			drtp.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
			drtp.dpiX = 96.f * integralDeviceScaleFactor;
			drtp.dpiY = 96.f * integralDeviceScaleFactor;
			// drtp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN);
			drtp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);

			rc.left = left; rc.top = top; rc.right = right; rc.bottom = bottom;
			D2D1_HWND_RENDER_TARGET_PROPERTIES dhrtp{};
			dhrtp.hwnd = m_hWnd;
			dhrtp.pixelSize = GetSizeUFromRect(rc, integralDeviceScaleFactor);
			dhrtp.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

			ATLASSERT(nullptr != g_pD2DFactory);

			ReleaseUnknown(m_bitmapSplit);

			//hr = g_pD2DFactory->CreateHwndRenderTarget(renderTargetProperties, 
			// hwndRenderTragetproperties, &m_pD2DRenderTarget);
			hr = g_pD2DFactory->CreateHwndRenderTarget(drtp, dhrtp, &m_pD2DRenderTarget);

			if (hr == S_OK && m_pD2DRenderTarget)
			{
				//U8 result = 0;
				U32 pixel[4] = { 0xFFF0F0F0, 0xFFF0F0F0,0xFFF0F0F0,0xFFF0F0F0 };
				//U32 pixel[4] = { 0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF };
				hr = m_pD2DRenderTarget->CreateBitmap(
					D2D1::SizeU(4, 1), pixel, 4 << 2,
					D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
					&m_bitmapSplit);
			}

		}
		return hr;
	}

	void UpdatePaneWindow(U32* dst, int width, int height, int offsetX, int offsetY)
	{
		int dx, dy;
		int wh = 32;
		U32* src;
		::ScreenFillColor(dst, static_cast<U32>(width * height), BKGCOLOR_LIGHT);

		dy = (height - wh) >> 1;
		dx = width - dy - wh;
		src = const_cast<U32*>(xbmpLSubmitN);
		ScreenDrawRect(dst, width, height, src, wh, wh, dx, dy);
	}

	void DrawPaneWindow()
	{
		ATLASSERT(m_statusBuff);

		int offsetX = m_rcSplitter.left;
		int offsetY = m_rcSplitter.bottom - m_statusHeight;

		if (offsetX >= 0 && offsetY >= 0)
		{
			HRESULT hr;
			ID2D1Bitmap* pBitmap = nullptr;

			int w = m_rcSplitter.right - m_rcSplitter.left;
			int h = m_statusHeight;

			UpdatePaneWindow(m_statusBuff, w, h, offsetX, offsetY);

			hr = m_pD2DRenderTarget->CreateBitmap(D2D1::SizeU(w, h), m_statusBuff, (w << 2),
				D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
				&pBitmap);

			if (S_OK == hr && pBitmap)
			{
				D2D1_RECT_F area = D2D1::RectF(
					static_cast<FLOAT>(offsetX),
					static_cast<FLOAT>(offsetY),
					static_cast<FLOAT>(offsetX + w),
					static_cast<FLOAT>(offsetY + h)
				);
				m_pD2DRenderTarget->DrawBitmap(pBitmap, &area);
			}
			ReleaseUnknown(pBitmap);
		}
	}

#define BITMAP_WIDTH	64
#define BITMAP_HEIGHT	64

	void DoPaint()
	{
		LPRECT lpRect;

		m_rectBtn.left = 0;
		m_rectBtn.top = 0;
		m_rectBtn.right = 0;
		m_rectBtn.bottom = 0;

		lpRect = &m_rcSplitter;
		HRESULT hr = CreateDeviceDependantResource(lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
		if (S_OK == hr && nullptr != m_pD2DRenderTarget)
		{
			int w = m_rcSplitter.right - m_rcSplitter.left;
			int h = m_rcSplitter.bottom - m_rcSplitter.top;

			m_pD2DRenderTarget->BeginDraw();

			if (AppIsDocIdReady())
			{
				if (m_bitmapSplit)
				{
					int T = m_rcSplitter.top + m_xySplitterPos;
					int B = T + m_cxySplitBar + m_cxyBarEdge;
					int L = m_rcSplitter.left;
					int R = m_rcSplitter.right;
					D2D1_RECT_F area = D2D1::RectF(
						static_cast<FLOAT>(L),
						static_cast<FLOAT>(T),
						static_cast<FLOAT>(R),
						static_cast<FLOAT>(B)
					);
					m_pD2DRenderTarget->DrawBitmap(m_bitmapSplit, &area);
				}

				if (m_statusBuff)
				{
					DrawPaneWindow();
				}
			}
			else if(w > BITMAP_WIDTH && h > BITMAP_HEIGHT)
			{
				ID2D1Bitmap* pBitmap = nullptr;
				int offsetX = (w - BITMAP_WIDTH) >> 1;
				int offsetY = (h - BITMAP_HEIGHT) >> 1;
				U32* src = const_cast<U32*>(xbmpLOpenFileN);
				if(m_lpRectPress == &m_rectBtn)
					src = const_cast<U32*>(xbmpLOpenFileP);

				m_pD2DRenderTarget->Clear(D2D1::ColorF(0xFFFFFF));

				hr = m_pD2DRenderTarget->CreateBitmap(D2D1::SizeU(BITMAP_WIDTH, BITMAP_HEIGHT),
					src, (BITMAP_HEIGHT << 2),
					D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
					&pBitmap);

				if (S_OK == hr && pBitmap)
				{
					m_rectBtn.left   = offsetX;
					m_rectBtn.top    = offsetY;
					m_rectBtn.right  = m_rectBtn.left + BITMAP_WIDTH;
					m_rectBtn.bottom = m_rectBtn.top + BITMAP_HEIGHT;

					D2D1_RECT_F area = D2D1::RectF(
						static_cast<FLOAT>(m_rectBtn.left),
						static_cast<FLOAT>(m_rectBtn.top),
						static_cast<FLOAT>(m_rectBtn.right),
						static_cast<FLOAT>(m_rectBtn.bottom)
					);
					m_pD2DRenderTarget->DrawBitmap(pBitmap, &area);
				}
				ReleaseUnknown(pBitmap);
			}

			hr = m_pD2DRenderTarget->EndDraw();
			//////////////////////////////////////////////////////////////////
			if (FAILED(hr) || D2DERR_RECREATE_TARGET == hr)
			{
				ReleaseUnknown(m_pD2DRenderTarget);
			}
		}
	}

	void DoSize(bool bUpdate = true)
	{
		ReleaseUnknown(m_pD2DRenderTarget);
		
		GetClientRect(&m_rcSplitter);

		if (m_rcSplitter.right > (m_rcSplitter.left + m_statusHeight)
			&& m_rcSplitter.bottom > (m_rcSplitter.top + FEEDBACK_WIN_HEIGHT))
		{
			U32 bytes;
			int w = m_rcSplitter.right - m_rcSplitter.left;

			if (m_nProportionalPos == 0)
				m_nProportionalPos = FEEDBACK_WIN_HEIGHT;
			
			UpdateRightAlignPos();

			bytes = ZT_ALIGN_PAGE64K(w * m_statusHeight * sizeof(U32));
			m_statusBuff = static_cast<U32*>(VirtualAlloc(NULL, bytes, MEM_COMMIT, PAGE_READWRITE));

			if (bUpdate)
				UpdateSplitterLayout();
		}

		Invalidate();
	}

	void Init()
	{
		m_hCursorNS = ::LoadCursor(NULL, IDC_SIZENS);
		m_hCursorHand = ::LoadCursor(NULL, IDC_HAND);

#if 0
		m_crBackGround = AppInDarkMode() ? BACKGROUND_COLOR_DARK : BACKGROUND_COLOR_LIGHT;

		m_nSinglePane = SPLIT_PANE_NONE;
		if (!AppIsAIWindowShowing())
		{
			m_nSinglePane = AppLeftAIMode() ? SPLIT_PANE_RIGHT : SPLIT_PANE_LEFT;
		}

		GetSystemSettings(false);
#endif 
	}

	void SetSplitterRect(bool bUpdate = true)
	{

		GetClientRect(&m_rcSplitter);

		UpdateRightAlignPos();

		if (bUpdate)
			UpdateSplitterLayout();
	}

	bool SetSplitterPos(int xyPos = -1, bool bUpdate = true)
	{
		if (xyPos == -1)   // -1 == default position
		{
			if (m_bProportionalDefPos)
			{
				ATLASSERT((m_xySplitterDefPos >= 0) && (m_xySplitterDefPos <= m_nPropMax));

				xyPos = ::MulDiv(m_xySplitterDefPos, m_rcSplitter.bottom - m_rcSplitter.top - m_cxySplitBar - m_cxyBarEdge, m_nPropMax);
			}
			else if (m_xySplitterDefPos != -1)
			{
				xyPos = m_xySplitterDefPos;
			}
			else   // not set, use middle position
			{
				xyPos = (m_rcSplitter.bottom - m_rcSplitter.top - m_cxySplitBar - m_cxyBarEdge) / 2;
			}
		}

		// Adjust if out of valid range
		int	cxyMax = m_rcSplitter.bottom - m_rcSplitter.top;

		if (xyPos < m_cxyMin + m_cxyBarEdge)
			xyPos = m_cxyMin;
		else if (xyPos > (cxyMax - m_cxySplitBar - m_cxyBarEdge - m_cxyMin))
			xyPos = cxyMax - m_cxySplitBar - m_cxyBarEdge - m_cxyMin;

		// Set new position and update if requested
		bool bRet = (m_xySplitterPos != xyPos);
		m_xySplitterPos = xyPos;

		if (m_bUpdateProportionalPos)
		{
			StoreRightAlignPos();
		}
		else
		{
			m_bUpdateProportionalPos = true;
		}

		if (bUpdate && bRet)
			UpdateSplitterLayout();

		return bRet;
	}

	void SetSplitterPanes(HWND hWndLeftTop, HWND hWndRightBottom, bool bUpdate = true)
	{
		m_hWndPane[SPLIT_PANE_LEFT] = hWndLeftTop;
		m_hWndPane[SPLIT_PANE_RIGHT] = hWndRightBottom;
		ATLASSERT((m_hWndPane[SPLIT_PANE_LEFT] == NULL) || (m_hWndPane[SPLIT_PANE_RIGHT] == NULL) || (m_hWndPane[SPLIT_PANE_LEFT] != m_hWndPane[SPLIT_PANE_RIGHT]));
		if (bUpdate)
			UpdateSplitterLayout();
	}

	bool SetActivePane(int nPane)
	{
		ATLASSERT((nPane == SPLIT_PANE_LEFT) || (nPane == SPLIT_PANE_RIGHT));
		if ((nPane != SPLIT_PANE_LEFT) && (nPane != SPLIT_PANE_RIGHT))
			return false;
		if ((m_nSinglePane != SPLIT_PANE_NONE) && (nPane != m_nSinglePane))
			return false;

		::SetFocus(m_hWndPane[nPane]);
		m_nDefActivePane = nPane;

		return true;
	}


	void UpdateSplitterLayout()
	{
		if (m_nSinglePane == SPLIT_PANE_NONE)
		{
			RECT rect = {};

			if (m_xySplitterPos == -1)
				return;

			if (GetSplitterBarRect(&rect))
				InvalidateRect(&rect);

			for (int nPane = 0; nPane < m_nPanesCount; nPane++)
			{
				if (GetSplitterPaneRect(nPane, &rect))
				{
					if (m_hWndPane[nPane] != NULL)
						::SetWindowPos(m_hWndPane[nPane], NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
					else
						InvalidateRect(&rect);
				}
			}
		}
	}

	bool GetSplitterBarRect(LPRECT lpRect) const
	{
		ATLASSERT(lpRect != NULL);
		if ((m_nSinglePane != SPLIT_PANE_NONE) || (m_xySplitterPos == -1))
			return false;

		lpRect->left = m_rcSplitter.left;
		lpRect->top = m_rcSplitter.top + m_xySplitterPos;
		lpRect->right = m_rcSplitter.right;
		lpRect->bottom = m_rcSplitter.top + m_xySplitterPos + m_cxySplitBar + m_cxyBarEdge;

		return true;
	}

	bool GetSplitterPaneRect(int nPane, LPRECT lpRect) const
	{
		ATLASSERT((nPane == SPLIT_PANE_LEFT) || (nPane == SPLIT_PANE_RIGHT));
		ATLASSERT(lpRect != NULL);
		bool bRet = true;
		if (m_nSinglePane != SPLIT_PANE_NONE)
		{
			if (nPane == m_nSinglePane)
				*lpRect = m_rcSplitter;
			else
				bRet = false;
		}
		else if (nPane == SPLIT_PANE_LEFT)
		{
			lpRect->left = m_rcSplitter.left;
			lpRect->top = m_rcSplitter.top;
			lpRect->right = m_rcSplitter.right;
			lpRect->bottom = m_rcSplitter.top + m_xySplitterPos;
		}
		else if (nPane == SPLIT_PANE_RIGHT)
		{
			lpRect->left = m_rcSplitter.left;
			lpRect->top = m_rcSplitter.top + m_xySplitterPos + m_cxySplitBar + m_cxyBarEdge;
			lpRect->right = m_rcSplitter.right;
			lpRect->bottom = m_rcSplitter.bottom - m_statusHeight;
		}
		else
		{
			bRet = false;
		}

		return bRet;
	}

	bool IsOverSplitterRect(int x, int y) const
	{
		// -1 == don't check
		return (((x == -1) || ((x >= m_rcSplitter.left) && (x <= m_rcSplitter.right))) &&
			((y == -1) || ((y >= m_rcSplitter.top) && (y <= m_rcSplitter.bottom))));
	}

	bool IsOverSplitterBar(int x, int y) const
	{
		if (m_nSinglePane != SPLIT_PANE_NONE)
			return false;
		if ((m_xySplitterPos == -1) || !IsOverSplitterRect(x, y))
			return false;
		int xy = y;
		int xyOff = m_rcSplitter.top;

		return ((xy >= (xyOff + m_xySplitterPos)) && (xy < (xyOff + m_xySplitterPos + m_cxySplitBar + m_cxyBarEdge)));
	}

	void DrawGhostBar()
	{
		RECT rect = {};
		if (GetSplitterBarRect(&rect))
		{
			// convert client to window coordinates

			RECT rcWnd = {};
			GetWindowRect(&rcWnd);
			::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rcWnd, 2);
			::OffsetRect(&rect, -rcWnd.left, -rcWnd.top);

			// invert the brush pattern (looks just like frame window sizing)
			CBrush brush(CDCHandle::GetHalftoneBrush());
			if (brush.m_hBrush != NULL)
			{
				CWindowDC dc(m_hWnd);
				CBrushHandle brushOld = dc.SelectBrush(brush);
				dc.PatBlt(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, PATINVERT);
				dc.SelectBrush(brushOld);
			}
		}
	}

	void StoreRightAlignPos()
	{
		int cxyTotal = (m_rcSplitter.bottom - m_rcSplitter.top - m_cxySplitBar - m_cxyBarEdge);
		if (cxyTotal > 0)
			m_nProportionalPos = cxyTotal - m_xySplitterPos;
		else
			m_nProportionalPos = 0;
#if 0
		ATLTRACE2(atlTraceUI, 0, _T("CSplitterImpl::StoreRightAlignPos - %i\n"), m_nProportionalPos);
#endif 
	}

	void UpdateRightAlignPos()
	{
		int cxyTotal = (m_rcSplitter.bottom - m_rcSplitter.top - m_cxySplitBar - m_cxyBarEdge);
		if (cxyTotal > 0)
		{
			m_bUpdateProportionalPos = false;

			SetSplitterPos(cxyTotal - m_nProportionalPos, false);
		}
	}

};
