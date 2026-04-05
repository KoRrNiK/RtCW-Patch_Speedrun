/*
===========================================================================
ls_window.c  --  Window thread, WndProc, context menu, public API

Included as part of cl_livesplit_window.c unity build.
All engine headers, Win32 headers, shared state, and all prior ls_*.c
functions already available.
===========================================================================
*/

/* ---- Context menu IDs ---- */
#define IDM_COMP_PB    1001
#define IDM_COMP_BEST  1002
#define IDM_COMP_AVG   1003
#define IDM_ONTOP      1010
#define IDM_TRANSP     1011
#define IDM_EDIT       1020
#define IDM_CLOSE      1030

/* ---- DPI query helper ---- */
static void LSWND_UpdateDpi( void ) {
	typedef UINT (WINAPI *pfn_GetDpiForWindow)( HWND );
	HMODULE usr = GetModuleHandleA( "user32.dll" );
	if ( usr && lswnd_hwnd ) {
		pfn_GetDpiForWindow pFn =
			(pfn_GetDpiForWindow)GetProcAddress( usr, "GetDpiForWindow" );
		if ( pFn ) {
			UINT dpi = pFn( lswnd_hwnd );
			if ( dpi > 0 ) { lswnd_dpi = (int)dpi; return; }
		}
	}
	{
		HDC sdc = GetDC( NULL );
		lswnd_dpi = GetDeviceCaps( sdc, LOGPIXELSY );
		ReleaseDC( NULL, sdc );
		if ( lswnd_dpi < 96 ) lswnd_dpi = 96;
	}
}

/* ---- Geometry persistence ---- */

static void LSWND_SaveGeometry( void ) {
	RECT r;
	if ( !lswnd_hwnd || !IsWindow( lswnd_hwnd ) ) return;
	GetWindowRect( lswnd_hwnd, &r );
	lswnd_layout.windowWidth = (int)( r.right - r.left );
	Cvar_Set( "ls_wnd_x", va( "%d", (int)r.left ) );
	Cvar_Set( "ls_wnd_y", va( "%d", (int)r.top ) );
	Cvar_Set( "ls_wnd_w", va( "%d", (int)( r.right - r.left ) ) );
	Cvar_Set( "ls_wnd_h", va( "%d", (int)( r.bottom - r.top ) ) );
}

/* ---- Window style (layered / transparency / always-on-top) ---- */

static void LSWND_ApplyWindowStyle( void ) {
	LONG exStyle;
	if ( !lswnd_hwnd ) return;
	exStyle = GetWindowLongA( lswnd_hwnd, GWL_EXSTYLE );

	/* always on top */
	SetWindowPos( lswnd_hwnd,
		lswnd_layout.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
		0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );

	/* transparency / per-pixel alpha */
	if ( lswnd_layout.transparent ) {
		LONG style = GetWindowLongA( lswnd_hwnd, GWL_STYLE );
		/* remove thick frame and border to prevent DWM drawing border */
		style &= ~( WS_THICKFRAME | WS_BORDER | WS_DLGFRAME );
		SetWindowLongA( lswnd_hwnd, GWL_STYLE, style );
		exStyle |= WS_EX_LAYERED;
		SetWindowLongA( lswnd_hwnd, GWL_EXSTYLE, exStyle );
		/* Do NOT call SetLayeredWindowAttributes - UpdateLayeredWindow
		   will manage per-pixel alpha from the paint loop. */
		/* try to disable DWM border on Win11+ (DWMWA_BORDER_COLOR = 34, DWMWA_COLOR_NONE = 0xFFFFFFFE) */
		{
			typedef HRESULT ( WINAPI *pfnDwmSetWindowAttribute )( HWND, DWORD, LPCVOID, DWORD );
			HMODULE dwm = GetModuleHandleA( "dwmapi.dll" );
			if ( !dwm ) dwm = LoadLibraryA( "dwmapi.dll" );
			if ( dwm ) {
				pfnDwmSetWindowAttribute pDwmSet = (pfnDwmSetWindowAttribute)
					GetProcAddress( dwm, "DwmSetWindowAttribute" );
				if ( pDwmSet ) {
					DWORD borderClr = 0xFFFFFFFE; /* DWMWA_COLOR_NONE */
					pDwmSet( lswnd_hwnd, 34, &borderClr, sizeof( borderClr ) );
				}
			}
		}
		/* force frame recalc */
		SetWindowPos( lswnd_hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
	} else if ( lswnd_layout.opacity < 255 ) {
		LONG style = GetWindowLongA( lswnd_hwnd, GWL_STYLE );
		style |= WS_THICKFRAME;
		SetWindowLongA( lswnd_hwnd, GWL_STYLE, style );
		exStyle |= WS_EX_LAYERED;
		SetWindowLongA( lswnd_hwnd, GWL_EXSTYLE, exStyle );
		SetLayeredWindowAttributes( lswnd_hwnd, 0,
			(BYTE)lswnd_layout.opacity, LWA_ALPHA );
		SetWindowPos( lswnd_hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
	} else {
		LONG style = GetWindowLongA( lswnd_hwnd, GWL_STYLE );
		style |= WS_THICKFRAME;
		SetWindowLongA( lswnd_hwnd, GWL_STYLE, style );
		exStyle &= ~WS_EX_LAYERED;
		SetWindowLongA( lswnd_hwnd, GWL_EXSTYLE, exStyle );
		SetWindowPos( lswnd_hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
	}
}

/* ---- Context menu ---- */

static void LSWND_ShowContextMenu( HWND hw, int x, int y ) {
	HMENU menu, compSub;

	menu   = CreatePopupMenu();
	compSub = CreatePopupMenu();
	AppendMenuA( compSub, MF_STRING | ( lswnd_layout.compareAgainst == 0 ? MF_CHECKED : 0 ), IDM_COMP_PB, "Personal Best" );
	AppendMenuA( compSub, MF_STRING | ( lswnd_layout.compareAgainst == 1 ? MF_CHECKED : 0 ), IDM_COMP_BEST, "Best Segments" );
	AppendMenuA( compSub, MF_STRING | ( lswnd_layout.compareAgainst == 2 ? MF_CHECKED : 0 ), IDM_COMP_AVG, "Average" );
	AppendMenuA( menu, MF_POPUP, (UINT_PTR)compSub, "Compare Against" );

	AppendMenuA( menu, MF_SEPARATOR, 0, NULL );
	AppendMenuA( menu, MF_STRING | ( lswnd_layout.alwaysOnTop ? MF_CHECKED : 0 ), IDM_ONTOP, "Always On Top" );
	AppendMenuA( menu, MF_STRING | ( lswnd_layout.transparent ? MF_CHECKED : 0 ), IDM_TRANSP, "Transparent" );
	AppendMenuA( menu, MF_SEPARATOR, 0, NULL );
	AppendMenuA( menu, MF_STRING, IDM_EDIT, "Edit Layout..." );
	AppendMenuA( menu, MF_SEPARATOR, 0, NULL );
	AppendMenuA( menu, MF_STRING, IDM_CLOSE, "Close" );

	TrackPopupMenu( menu, TPM_RIGHTBUTTON, x, y, 0, hw, NULL );
	DestroyMenu( menu );
}

/* ---- Main window procedure ---- */

static LRESULT CALLBACK LSWND_WndProc( HWND hw, UINT msg, WPARAM wp, LPARAM lp ) {
	switch ( msg ) {
	/* eliminate DWM non-client border (white bar at top) */
	case WM_NCCALCSIZE:
		if ( wp ) return 0;
		return DefWindowProcA( hw, msg, wp, lp );

	/* prevent system from painting non-client border in transparent mode */
	case WM_NCPAINT:
		if ( lswnd_layout.transparent ) return 0;
		return DefWindowProcA( hw, msg, wp, lp );

	case WM_NCACTIVATE:
		if ( lswnd_layout.transparent ) return TRUE;
		return DefWindowProcA( hw, msg, wp, lp );

	/* custom hit-test: resize edges since non-client area is removed */
	case WM_NCHITTEST: {
		POINT pt;
		RECT rc;
		int border = 5;
		if ( lswnd_layout.lockResize ) {
			/* no resize at all -- entire window is client (draggable) */
			return HTCLIENT;
		}
		pt.x = (short)LOWORD( lp );
		pt.y = (short)HIWORD( lp );
		GetWindowRect( hw, &rc );
		if ( pt.y < rc.top + border ) {
			if ( pt.x < rc.left + border ) return HTTOPLEFT;
			if ( pt.x >= rc.right - border ) return HTTOPRIGHT;
			return HTTOP;
		}
		if ( pt.y >= rc.bottom - border ) {
			if ( pt.x < rc.left + border ) return HTBOTTOMLEFT;
			if ( pt.x >= rc.right - border ) return HTBOTTOMRIGHT;
			return HTBOTTOM;
		}
		if ( pt.x < rc.left + border ) return HTLEFT;
		if ( pt.x >= rc.right - border ) return HTRIGHT;
		return HTCLIENT;
	}

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc;
		RECT cr;
		int cw, ch, paintH;

		/* Layered (transparent) mode: UpdateLayeredWindow handles painting,
		   just validate the region so Windows stops sending WM_PAINT. */
		if ( lswnd_layout.transparent ) {
			BeginPaint( hw, &ps );
			EndPaint( hw, &ps );
			return 0;
		}

		dc = BeginPaint( hw, &ps );
		GetClientRect( hw, &cr );
		cw = cr.right;
		ch = cr.bottom;
		/* paint at content height, then scale down if window is smaller */
		paintH = ( lswnd_contentHeight > 0 && lswnd_contentHeight > ch ) ? lswnd_contentHeight : ch;
		if ( paintH != ch ) {
			/* paint to off-screen at full content size, then StretchBlt */
			HDC tmpDC;
			HBITMAP tmpBmp, tmpOld;
			tmpDC  = CreateCompatibleDC( dc );
			tmpBmp = CreateCompatibleBitmap( dc, cw, paintH );
			tmpOld = (HBITMAP)SelectObject( tmpDC, tmpBmp );
			LSWND_Paint( tmpDC, cw, paintH );
			SetStretchBltMode( dc, HALFTONE );
			StretchBlt( dc, 0, 0, cw, ch, tmpDC, 0, 0, cw, paintH, SRCCOPY );
			SelectObject( tmpDC, tmpOld );
			DeleteObject( tmpBmp );
			DeleteDC( tmpDC );
		} else {
			LSWND_Paint( dc, cw, ch );
		}
		EndPaint( hw, &ps );
		return 0;
	}

	case WM_LBUTTONDOWN:
		lswnd_dragging  = TRUE;
		lswnd_dragStart.x = (short)LOWORD( lp );
		lswnd_dragStart.y = (short)HIWORD( lp );
		SetCapture( hw );
		return 0;

	case WM_MOUSEMOVE:
		if ( lswnd_dragging ) {
			POINT pt;
			RECT  wr;
			GetCursorPos( &pt );
			GetWindowRect( hw, &wr );
			MoveWindow( hw,
				pt.x - lswnd_dragStart.x,
				pt.y - lswnd_dragStart.y,
				wr.right - wr.left,
				wr.bottom - wr.top, TRUE );
		}
		return 0;

	case WM_LBUTTONUP:
		if ( lswnd_dragging ) {
			lswnd_dragging = FALSE;
			ReleaseCapture();
			LSWND_SaveGeometry();
		}
		return 0;

	case WM_RBUTTONUP: {
		POINT pt;
		GetCursorPos( &pt );
		LSWND_ShowContextMenu( hw, pt.x, pt.y );
		return 0;
	}

	case WM_MOUSEWHEEL: {
		short delta = (short)HIWORD( wp );
		if ( delta > 0 ) {
			lswnd_scrollOffset--;  /* scroll up = show earlier splits */
		} else if ( delta < 0 ) {
			lswnd_scrollOffset++;  /* scroll down = show later splits */
		}
		InvalidateRect( hw, NULL, FALSE );
		return 0;
	}

	case WM_COMMAND:
		switch ( LOWORD( wp ) ) {
		case IDM_COMP_PB:   lswnd_layout.compareAgainst = 0; LSLAY_SaveLayout(); break;
		case IDM_COMP_BEST: lswnd_layout.compareAgainst = 1; LSLAY_SaveLayout(); break;
		case IDM_COMP_AVG:  lswnd_layout.compareAgainst = 2; LSLAY_SaveLayout(); break;
		case IDM_ONTOP:
			lswnd_layout.alwaysOnTop = !lswnd_layout.alwaysOnTop;
			LSWND_ApplyWindowStyle();
			LSLAY_SaveLayout();
			break;
		case IDM_TRANSP:
			lswnd_layout.transparent = !lswnd_layout.transparent;
			LSWND_ApplyWindowStyle();
			LSLAY_SaveLayout();
			break;
		case IDM_EDIT:
			LSSD_Open();
			break;
		case IDM_CLOSE:
			PostMessage( hw, WM_CLOSE, 0, 0 );
			break;
		}
		return 0;

	case WM_SIZE: {
		RECT wr;
		GetWindowRect( hw, &wr );
		lswnd_layout.windowWidth = (int)( wr.right - wr.left );
		InvalidateRect( hw, NULL, FALSE );
		return 0;
	}

	case WM_EXITSIZEMOVE:
		LSWND_SaveGeometry();
		return 0;

	case WM_GETMINMAXINFO: {
		MINMAXINFO *mmi = (MINMAXINFO *)lp;
		mmi->ptMinTrackSize.x = MulDiv( 120, lswnd_dpi, 96 );
		mmi->ptMinTrackSize.y = MulDiv( 40, lswnd_dpi, 96 );
		return 0;
	}

	case WM_DPICHANGED: {
		RECT *suggested = (RECT *)lp;
		lswnd_dpi = HIWORD( wp );
		SetWindowPos( hw, NULL,
			suggested->left, suggested->top,
			suggested->right - suggested->left,
			suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE );
		return 0;
	}

	case WM_CLOSE:
		LSWND_SaveGeometry();
		DestroyWindow( hw );
		return 0;

	case WM_DESTROY:
		lswnd_hwnd = NULL;
		PostQuitMessage( 0 );
		return 0;
	}
	return DefWindowProcA( hw, msg, wp, lp );
}

/* ---- Per-pixel alpha paint for transparent / layered mode ---- */

static void LSWND_PaintLayered( void ) {
	BITMAPINFO bi;
	void *bits;
	HDC screenDC, memDC;
	HBITMAP dib, oldBmp;
	RECT cr;
	int w, h;
	BLENDFUNCTION bf;
	POINT ptSrc;
	SIZE sz;
	DWORD *px;
	int count, i;

	if ( !lswnd_hwnd || !IsWindow( lswnd_hwnd ) ) return;

	GetClientRect( lswnd_hwnd, &cr );
	w = cr.right;
	h = cr.bottom;
	if ( w <= 0 || h <= 0 ) return;

	memset( &bi, 0, sizeof( bi ) );
	bi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
	bi.bmiHeader.biWidth       = w;
	bi.bmiHeader.biHeight      = -h;   /* top-down */
	bi.bmiHeader.biPlanes      = 1;
	bi.bmiHeader.biBitCount    = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	screenDC = GetDC( NULL );
	memDC    = CreateCompatibleDC( screenDC );
	dib      = CreateDIBSection( screenDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0 );
	if ( !dib ) { DeleteDC( memDC ); ReleaseDC( NULL, screenDC ); return; }
	oldBmp   = (HBITMAP)SelectObject( memDC, dib );

	/* clear to fully transparent black */
	memset( bits, 0, w * h * 4 );

	/* render content directly into the 32-bit ARGB DIB */
	LSWND_Paint( memDC, w, h );

	/* post-process: set alpha=255 for any pixel with non-zero RGB.
	   GDI always writes alpha=0, so we fix it up here.
	   Pixels that remain (0,0,0,0) stay fully transparent. */
	px = (DWORD *)bits;
	count = w * h;
	for ( i = 0; i < count; i++ ) {
		if ( px[i] & 0x00FFFFFF ) {
			px[i] |= 0xFF000000;   /* alpha = 255, premultiply is identity */
		}
	}

	/* update the layered window with per-pixel alpha */
	bf.BlendOp             = AC_SRC_OVER;
	bf.BlendFlags          = 0;
	bf.SourceConstantAlpha = ( lswnd_layout.opacity < 255 ) ? (BYTE)lswnd_layout.opacity : 255;
	bf.AlphaFormat         = AC_SRC_ALPHA;
	ptSrc.x = 0;
	ptSrc.y = 0;
	sz.cx   = w;
	sz.cy   = h;
	UpdateLayeredWindow( lswnd_hwnd, screenDC, NULL, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA );

	SelectObject( memDC, oldBmp );
	DeleteObject( dib );
	DeleteDC( memDC );
	ReleaseDC( NULL, screenDC );
}

/* ---- Window thread ---- */

static DWORD WINAPI LSWND_ThreadFunc( LPVOID param ) {
	WNDCLASSEXA wc = {0};
	MSG msg;
	int x, y, w, h;

	(void)param;

	/* Set per-monitor DPI awareness for this thread so the LiveSplit
	   window renders at native resolution even when the host process
	   (WolfSP.exe) is not DPI-aware.  Dynamically loaded so it still
	   compiles and runs on older Windows versions. */
	{
		typedef HANDLE (WINAPI *pfn_SetThreadDpiAwarenessContext)( HANDLE );
		HMODULE usr = GetModuleHandleA( "user32.dll" );
		if ( usr ) {
			pfn_SetThreadDpiAwarenessContext pCtx =
				(pfn_SetThreadDpiAwarenessContext)GetProcAddress( usr, "SetThreadDpiAwarenessContext" );
			if ( pCtx ) {
				/* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4 */
				pCtx( (HANDLE)(long long)-4 );
			}
		}
	}

	/* load layout from file */
	LSLAY_LoadLayout();

	/* register window class */
	wc.cbSize        = sizeof( wc );
	wc.lpfnWndProc   = LSWND_WndProc;
	wc.hInstance      = GetModuleHandle( NULL );
	wc.lpszClassName  = "LSWND_Main";
	wc.hCursor        = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground  = NULL;
	wc.hIcon         = LoadIconA( GetModuleHandle( NULL ), MAKEINTRESOURCEA( 1 ) );
	wc.hIconSm       = wc.hIcon;
	RegisterClassExA( &wc );

	/* restore position from cvars */
	x = atoi( Cvar_VariableString( "ls_wnd_x" ) );
	y = atoi( Cvar_VariableString( "ls_wnd_y" ) );
	w = atoi( Cvar_VariableString( "ls_wnd_w" ) );
	h = atoi( Cvar_VariableString( "ls_wnd_h" ) );
	/* prefer layout width if set (settings take precedence over saved geometry) */
	if ( lswnd_layout.windowWidth > 0 ) w = lswnd_layout.windowWidth;
	if ( w < 180 ) w = 250;
	if ( h < 100 ) h = 500;

	/* sanity-check: ensure window is at least partially visible on screen */
	{
		int scrW = GetSystemMetrics( SM_CXSCREEN );
		int scrH = GetSystemMetrics( SM_CYSCREEN );
		if ( x != 0 || y != 0 ) {
			/* push back on-screen if completely off any edge */
			if ( x + w < 40 ) x = 0;
			if ( y + h < 40 ) y = 0;
			if ( x > scrW - 40 ) x = scrW - w;
			if ( y > scrH - 40 ) y = scrH - h;
		}
	}

	/* create window */
#ifdef LSWND_STANDALONE
	/* standalone: show in taskbar with icon */
	lswnd_hwnd = CreateWindowExA(
		WS_EX_APPWINDOW,
		"LSWND_Main", "RtCW LiveSplit",
		WS_POPUP | WS_THICKFRAME,
		x ? x : CW_USEDEFAULT,
		y ? y : CW_USEDEFAULT,
		w, h,
		NULL, NULL, GetModuleHandle( NULL ), NULL );
#else
	/* in-game: tool window, no taskbar entry */
	lswnd_hwnd = CreateWindowExA(
		WS_EX_TOOLWINDOW,
		"LSWND_Main", "LiveSplit",
		WS_POPUP | WS_THICKFRAME,
		x ? x : CW_USEDEFAULT,
		y ? y : CW_USEDEFAULT,
		w, h,
		NULL, NULL, GetModuleHandle( NULL ), NULL );
#endif

	LSWND_ApplyWindowStyle();
	LSWND_UpdateDpi();
	ShowWindow( lswnd_hwnd, SW_SHOW );
	UpdateWindow( lswnd_hwnd );

	/* 120 Hz refresh loop */
	while ( !lswnd_wantQuit ) {
		while ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ) {
			if ( msg.message == WM_QUIT ) {
				InterlockedExchange( &lswnd_wantQuit, 1 );
				break;
			}
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		if ( lswnd_wantQuit ) break;

		if ( lswnd_hwnd && IsWindow( lswnd_hwnd ) ) {
			RECT wr;
			if ( lswnd_layout.transparent ) {
				/* layered mode: direct per-pixel alpha update */
				LSWND_PaintLayered();
			} else {
				InvalidateRect( lswnd_hwnd, NULL, FALSE );
				UpdateWindow( lswnd_hwnd );
			}
			/* auto-height: grow or shrink window to match content */
			if ( lswnd_contentHeight > 0 ) {
				GetWindowRect( lswnd_hwnd, &wr );
				if ( wr.bottom - wr.top != lswnd_contentHeight ) {
					SetWindowPos( lswnd_hwnd, NULL, 0, 0,
						wr.right - wr.left, lswnd_contentHeight,
						SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
				}
			}
		}
		Sleep( 8 );
	}

	/* close settings dialog if still open */
	if ( lssd_hw && IsWindow( lssd_hw ) ) {
		DestroyWindow( lssd_hw );
		lssd_hw = NULL;
	}
	LSWND_SaveGeometry();
	if ( lswnd_hwnd ) { DestroyWindow( lswnd_hwnd ); lswnd_hwnd = NULL; }
	UnregisterClassA( "LSWND_Main", GetModuleHandle( NULL ) );
	return 0;
}

/* =====================================================================
   Public API  (called from game thread via cl_livesplit.c)
   ===================================================================== */

void LS_WindowCreate( void ) {
	if ( lswnd_thread ) return;
	InterlockedExchange( &lswnd_wantQuit, 0 );
	lswnd_thread = CreateThread( NULL, 0, LSWND_ThreadFunc, NULL, 0, NULL );
}

void LS_WindowDestroy( void ) {
	if ( !lswnd_thread ) return;
	InterlockedExchange( &lswnd_wantQuit, 1 );
	if ( lswnd_hwnd ) PostMessage( lswnd_hwnd, WM_CLOSE, 0, 0 );
	WaitForSingleObject( lswnd_thread, 3000 );
	CloseHandle( lswnd_thread );
	lswnd_thread = NULL;
}

int LS_WindowIsActive( void ) {
	return ( lswnd_hwnd != NULL && IsWindow( lswnd_hwnd ) ) ? 1 : 0;
}

void LS_WindowRepaint( void ) {
	if ( lswnd_hwnd && IsWindow( lswnd_hwnd ) )
		InvalidateRect( lswnd_hwnd, NULL, FALSE );
}
