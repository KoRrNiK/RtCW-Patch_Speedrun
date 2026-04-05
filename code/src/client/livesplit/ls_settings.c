/*
===========================================================================
ls_settings.c  --  Color picker, font picker & settings dialog

Included as part of cl_livesplit_window.c unity build.
All engine headers, Win32 headers, shared state, ls_layout.c and
ls_render.c functions already available.

Uses native Windows theming via ComCtl32 v6 visual styles manifest.
===========================================================================
*/

/* forward-declared from ls_window.c (comes later in unity build) */
static void LSWND_ApplyWindowStyle( void );

/* =====================================================================
   Theme color helpers  (use system colors for native look)
   ===================================================================== */

#define LSDK_ACCENT    RGB(0, 102, 204)
#define LSDK_ACCENT2   RGB(40, 160, 100)
#define LSDK_SECTION   RGB(0, 102, 204)

/* =====================================================================
   Color Picker Dialog  -- Modern HSV wheel + gradient/inherit support
   ===================================================================== */

#define IDC_CP_INHERIT  100
#define IDC_CP_PLAIN    101
#define IDC_CP_VGRAD    102
#define IDC_CP_HGRAD    103
#define IDC_CP_CLR1     104
#define IDC_CP_CLR2     105
#define IDC_CP_ALPHA    106
#define IDC_CP_GRADPOS  107
#define IDC_CP_GRADLBL  108
#define IDC_CP_HEX1     109
#define IDC_CP_HEX2     110
#define IDC_CP_R1       111
#define IDC_CP_G1       112
#define IDC_CP_B1       113
#define IDC_CP_PRESET   200   /* base ID for preset buttons */

/* HSV helpers */
static void LSSD_HSVtoRGB( float h, float s, float v, int *r, int *g, int *b ) {
	float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
	float rf, gf, bf;
	if ( h < 60 )       { rf = c; gf = x; bf = 0; }
	else if ( h < 120 ) { rf = x; gf = c; bf = 0; }
	else if ( h < 180 ) { rf = 0; gf = c; bf = x; }
	else if ( h < 240 ) { rf = 0; gf = x; bf = c; }
	else if ( h < 300 ) { rf = x; gf = 0; bf = c; }
	else                { rf = c; gf = 0; bf = x; }
	*r = (int)((rf + m) * 255.0f + 0.5f);
	*g = (int)((gf + m) * 255.0f + 0.5f);
	*b = (int)((bf + m) * 255.0f + 0.5f);
}

static void LSSD_RGBtoHSV( int r, int g, int b, float *h, float *s, float *v ) {
	float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
	float cmax = rf > gf ? ( rf > bf ? rf : bf ) : ( gf > bf ? gf : bf );
	float cmin = rf < gf ? ( rf < bf ? rf : bf ) : ( gf < bf ? gf : bf );
	float d = cmax - cmin;
	*v = cmax;
	*s = ( cmax > 0.0001f ) ? d / cmax : 0.0f;
	if ( d < 0.0001f ) *h = 0.0f;
	else if ( cmax == rf ) *h = 60.0f * fmodf( (gf - bf) / d + 6.0f, 6.0f );
	else if ( cmax == gf ) *h = 60.0f * ( (bf - rf) / d + 2.0f );
	else                   *h = 60.0f * ( (rf - gf) / d + 4.0f );
}

static lsColor_t  lssd_cpClr;
static int         lssd_cpDone;
static int         lssd_cpResult;
static HFONT       lssd_cpFont;

/* HSV state for picker */
static float lssd_cpHue, lssd_cpSat, lssd_cpVal;
static int   lssd_cpDragSV, lssd_cpDragHue;
static int   lssd_cpActive;  /* 1 = editing c1, 2 = editing c2 */
static HBITMAP lssd_cpSvBmp;
static float   lssd_cpSvHue;
static int     lssd_cpSkipNotify;

/* recent colors */
#define CP_MAX_RECENT 8
static unsigned lssd_cpRecent[CP_MAX_RECENT];
static int      lssd_cpRecentCount;

/* preset colors for speedrun timers */
static const unsigned lssd_cpPresets[] = {
	0x00CC36,  /* ahead green */
	0xCC3600,  /* behind red */
	0xD8AF2C,  /* gold */
	0x2694E0,  /* accent blue */
	0xDCDCDC,  /* light text */
	0x808080,  /* dim text */
	0x171722,  /* dark bg */
	0x0D0D15,  /* darker bg */
	0xFFFFFF,  /* white */
	0x000000,  /* black */
	0x8B5CF6,  /* purple */
	0xEC4899,  /* pink */
};
#define CP_NUM_PRESETS (sizeof(lssd_cpPresets)/sizeof(lssd_cpPresets[0]))

/* SV area and Hue bar geometry (DPI-scaled) */
#define CP_SVX   LSSD_S(14)
#define CP_SVY   LSSD_S(14)
#define CP_SVSZ  LSSD_S(200)
#define CP_HUE_X LSSD_S(224)
#define CP_HUE_Y LSSD_S(14)
#define CP_HUE_W LSSD_S(24)
#define CP_HUE_H LSSD_S(200)

/* ---- DPI scaling -------------------------------------------------- */
static int lssd_dpi = 96;

static void LSSD_InitDpi( void ) {
	/* Query per-monitor DPI from the LiveSplit main window.
	   Falls back to screen DC (correct since thread is per-monitor aware). */
	typedef UINT (WINAPI *pfn_GetDpiForWindow)( HWND );
	HMODULE usr = GetModuleHandleA( "user32.dll" );
	if ( usr && lswnd_hwnd ) {
		pfn_GetDpiForWindow pFn =
			(pfn_GetDpiForWindow)GetProcAddress( usr, "GetDpiForWindow" );
		if ( pFn ) {
			UINT dpi = pFn( lswnd_hwnd );
			if ( dpi > 0 ) { lssd_dpi = (int)dpi; return; }
		}
	}
	{
		HDC dc = GetDC( NULL );
		lssd_dpi = GetDeviceCaps( dc, LOGPIXELSY );
		ReleaseDC( NULL, dc );
	}
	if ( lssd_dpi < 96 ) lssd_dpi = 96;
}

static int LSSD_S( int px ) {
	return MulDiv( px, lssd_dpi, 96 );
}

/* Build SV bitmap for a given hue */
static void LSSD_CPBuildSV( float hue ) {
	BITMAPINFO bmi = {0};
	unsigned char *bits;
	int x, y, sz, stride;

	sz = CP_SVSZ;
	if ( lssd_cpSvBmp && fabsf(lssd_cpSvHue - hue) < 0.5f ) return;
	if ( lssd_cpSvBmp ) DeleteObject( lssd_cpSvBmp );

	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = sz;
	bmi.bmiHeader.biHeight = -sz;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 24;
	bmi.bmiHeader.biCompression = BI_RGB;

	lssd_cpSvBmp = CreateDIBSection( NULL, &bmi, DIB_RGB_COLORS, (void**)&bits, NULL, 0 );
	if ( !lssd_cpSvBmp ) return;

	stride = ( ( sz * 3 + 3 ) & ~3 );
	for ( y = 0; y < sz; y++ ) {
		float val = 1.0f - (float)y / (sz - 1);
		for ( x = 0; x < sz; x++ ) {
			float sat = (float)x / (sz - 1);
			int r, g, b;
			int off = y * stride + x * 3;
			LSSD_HSVtoRGB( hue, sat, val, &r, &g, &b );
			bits[off + 0] = (unsigned char)b;
			bits[off + 1] = (unsigned char)g;
			bits[off + 2] = (unsigned char)r;
		}
	}
	lssd_cpSvHue = hue;
}

static void LSSD_CPSyncFromRGB( HWND hw, unsigned rgb ) {
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	LSSD_RGBtoHSV( r, g, b, &lssd_cpHue, &lssd_cpSat, &lssd_cpVal );
}

static unsigned LSSD_CPCurrentRGB( void ) {
	int r, g, b;
	LSSD_HSVtoRGB( lssd_cpHue, lssd_cpSat, lssd_cpVal, &r, &g, &b );
	return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}

static void LSSD_CPUpdateHex( HWND hw ) {
	char buf[16];
	unsigned rgb;
	int hexId;

	lssd_cpSkipNotify = 1;
	if ( lssd_cpActive == 2 ) {
		rgb = lssd_cpClr.c2;
		hexId = IDC_CP_HEX2;
	} else {
		rgb = ( lssd_cpClr.c1 != LSCLR_INHERIT ) ? lssd_cpClr.c1 : 0;
		hexId = IDC_CP_HEX1;
	}

	Com_sprintf( buf, sizeof(buf), "%06X", rgb & 0xFFFFFF );
	SetDlgItemTextA( hw, hexId, buf );

	SetDlgItemInt( hw, IDC_CP_R1, (rgb >> 16) & 0xFF, FALSE );
	SetDlgItemInt( hw, IDC_CP_G1, (rgb >> 8) & 0xFF, FALSE );
	SetDlgItemInt( hw, IDC_CP_B1, rgb & 0xFF, FALSE );
	lssd_cpSkipNotify = 0;
}

static void LSSD_CPSetActiveColor( unsigned rgb ) {
	if ( lssd_cpActive == 2 )
		lssd_cpClr.c2 = rgb;
	else {
		if ( lssd_cpClr.c1 != LSCLR_INHERIT )
			lssd_cpClr.c1 = rgb;
	}
}

static void LSSD_CPAddRecent( unsigned rgb ) {
	int i;
	/* check if already in recent list */
	for ( i = 0; i < lssd_cpRecentCount; i++ ) {
		if ( lssd_cpRecent[i] == rgb ) {
			/* move to front */
			for ( ; i > 0; i-- )
				lssd_cpRecent[i] = lssd_cpRecent[i - 1];
			lssd_cpRecent[0] = rgb;
			return;
		}
	}
	/* shift and add */
	if ( lssd_cpRecentCount < CP_MAX_RECENT ) lssd_cpRecentCount++;
	for ( i = lssd_cpRecentCount - 1; i > 0; i-- )
		lssd_cpRecent[i] = lssd_cpRecent[i - 1];
	lssd_cpRecent[0] = rgb;
}

static void LSSD_CPUpdateEnable( HWND hw ) {
	int inherit = IsDlgButtonChecked( hw, IDC_CP_INHERIT );
	int grad    = lssd_cpClr.mode != LSCLR_MODE_PLAIN;
	HWND hGradLbl = GetDlgItem( hw, IDC_CP_GRADLBL );
	HWND hGradPos = GetDlgItem( hw, IDC_CP_GRADPOS );
	EnableWindow( GetDlgItem( hw, IDC_CP_PLAIN ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_VGRAD ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_HGRAD ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_CLR1 ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_CLR2 ), !inherit && grad );
	EnableWindow( GetDlgItem( hw, IDC_CP_ALPHA ),  !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_HEX1 ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_R1 ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_G1 ), !inherit );
	EnableWindow( GetDlgItem( hw, IDC_CP_B1 ), !inherit );
	ShowWindow( GetDlgItem( hw, IDC_CP_CLR2 ), ( !inherit && grad ) ? SW_SHOW : SW_HIDE );
	ShowWindow( GetDlgItem( hw, IDC_CP_HEX2 ), ( !inherit && grad ) ? SW_SHOW : SW_HIDE );
	if ( hGradLbl ) ShowWindow( hGradLbl, ( !inherit && grad ) ? SW_SHOW : SW_HIDE );
	if ( hGradPos ) {
		ShowWindow( hGradPos, ( !inherit && grad ) ? SW_SHOW : SW_HIDE );
		EnableWindow( hGradPos, !inherit && grad );
	}
}

/* Draw a small color swatch */
static void LSSD_CPDrawSwatch( HDC dc, int x, int y, int w, int h, unsigned rgb, int selected ) {
	HBRUSH br = CreateSolidBrush( LSWND_CLR( rgb ) );
	RECT rc;
	SetRect( &rc, x, y, x + w, y + h );
	FillRect( dc, &rc, br );
	DeleteObject( br );
	if ( selected ) {
		HPEN pen = CreatePen( PS_SOLID, 2, RGB(255,255,255) );
		HPEN old = (HPEN)SelectObject( dc, pen );
		HBRUSH oldBr = (HBRUSH)SelectObject( dc, GetStockObject( NULL_BRUSH ) );
		Rectangle( dc, x, y, x + w, y + h );
		SelectObject( dc, oldBr );
		SelectObject( dc, old );
		DeleteObject( pen );
	} else {
		FrameRect( dc, &rc, (HBRUSH)GetStockObject( BLACK_BRUSH ) );
	}
}

/* Invalidate color picker + owner-draw color buttons */
static void LSSD_CPInvalidate( HWND hw ) {
	InvalidateRect( hw, NULL, FALSE );
	InvalidateRect( GetDlgItem( hw, IDC_CP_CLR1 ), NULL, TRUE );
	InvalidateRect( GetDlgItem( hw, IDC_CP_CLR2 ), NULL, TRUE );
}

static LRESULT CALLBACK LSSD_CPProc( HWND hw, UINT msg, WPARAM wp, LPARAM lp ) {
	switch ( msg ) {
	case WM_CREATE: {
		HWND h;
		int inh = ( lssd_cpClr.c1 == LSCLR_INHERIT ) ? 1 : 0;
		char buf[16];
		unsigned rgb0;
		int py, rx;

		lssd_cpFont = CreateFontA( LSSD_S(-13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, "Segoe UI" );
		lssd_cpActive = 1;
		lssd_cpDragSV = 0;
		lssd_cpDragHue = 0;
		lssd_cpSvBmp = NULL;

		rgb0 = ( lssd_cpClr.c1 != LSCLR_INHERIT ) ? lssd_cpClr.c1 : 0;
		LSSD_CPSyncFromRGB( hw, rgb0 );

		/* --- Right side controls --- */
		rx = LSSD_S(264);
		py = LSSD_S(14);

		/* Inherit checkbox */
		h = CreateWindowExA( 0, "BUTTON", "Use Default (inherit)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
			rx, py, LSSD_S(230), LSSD_S(20), hw, (HMENU)IDC_CP_INHERIT, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		if ( inh ) CheckDlgButton( hw, IDC_CP_INHERIT, BST_CHECKED );
		py += LSSD_S(28);

		/* Mode radios */
		h = CreateWindowExA( 0, "BUTTON", "Plain", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
			rx, py, LSSD_S(70), LSSD_S(20), hw, (HMENU)IDC_CP_PLAIN, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "BUTTON", "V-Grad", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
			rx + LSSD_S(74), py, LSSD_S(70), LSSD_S(20), hw, (HMENU)IDC_CP_VGRAD, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "BUTTON", "H-Grad", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
			rx + LSSD_S(148), py, LSSD_S(70), LSSD_S(20), hw, (HMENU)IDC_CP_HGRAD, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		CheckRadioButton( hw, IDC_CP_PLAIN, IDC_CP_HGRAD, IDC_CP_PLAIN + lssd_cpClr.mode );
		py += LSSD_S(28);

		/* Color 1/2 buttons */
		h = CreateWindowExA( 0, "STATIC", "Color 1:", WS_CHILD | WS_VISIBLE,
			rx, py + LSSD_S(2), LSSD_S(56), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "BUTTON", "", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			rx + LSSD_S(60), py, LSSD_S(60), LSSD_S(22), hw, (HMENU)IDC_CP_CLR1, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "STATIC", "Color 2:", WS_CHILD | WS_VISIBLE,
			rx + LSSD_S(130), py + LSSD_S(2), LSSD_S(56), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "BUTTON", "", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
			rx + LSSD_S(190), py, LSSD_S(60), LSSD_S(22), hw, (HMENU)IDC_CP_CLR2, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		py += LSSD_S(30);

		/* Hex input */
		h = CreateWindowExA( 0, "STATIC", "#", WS_CHILD | WS_VISIBLE,
			rx, py + LSSD_S(2), LSSD_S(14), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof(buf), "%06X", rgb0 & 0xFFFFFF );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE,
			rx + LSSD_S(16), py, LSSD_S(70), LSSD_S(22), hw, (HMENU)IDC_CP_HEX1, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof(buf), "%06X", lssd_cpClr.c2 & 0xFFFFFF );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE,
			rx + LSSD_S(130), py, LSSD_S(70), LSSD_S(22), hw, (HMENU)IDC_CP_HEX2, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		py += LSSD_S(28);

		/* RGB */
		h = CreateWindowExA( 0, "STATIC", "R:", WS_CHILD | WS_VISIBLE,
			rx, py + LSSD_S(2), LSSD_S(18), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof(buf), "%d", (rgb0 >> 16) & 0xFF );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
			rx + LSSD_S(20), py, LSSD_S(42), LSSD_S(22), hw, (HMENU)IDC_CP_R1, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "STATIC", "G:", WS_CHILD | WS_VISIBLE,
			rx + LSSD_S(70), py + LSSD_S(2), LSSD_S(18), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof(buf), "%d", (rgb0 >> 8) & 0xFF );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
			rx + LSSD_S(90), py, LSSD_S(42), LSSD_S(22), hw, (HMENU)IDC_CP_G1, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "STATIC", "B:", WS_CHILD | WS_VISIBLE,
			rx + LSSD_S(140), py + LSSD_S(2), LSSD_S(18), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof(buf), "%d", rgb0 & 0xFF );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
			rx + LSSD_S(160), py, LSSD_S(42), LSSD_S(22), hw, (HMENU)IDC_CP_B1, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		py += LSSD_S(30);

		/* Alpha + gradient midpoint */
		h = CreateWindowExA( 0, "STATIC", "Alpha:", WS_CHILD | WS_VISIBLE,
			rx, py + LSSD_S(2), LSSD_S(44), LSSD_S(16), hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof( buf ), "%d", lssd_cpClr.alpha );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_NUMBER,
			rx + LSSD_S(48), py, LSSD_S(42), LSSD_S(22), hw, (HMENU)IDC_CP_ALPHA, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "STATIC", "Mid%:", WS_CHILD | WS_VISIBLE,
			rx + LSSD_S(106), py + LSSD_S(2), LSSD_S(40), LSSD_S(16), hw, (HMENU)IDC_CP_GRADLBL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		Com_sprintf( buf, sizeof( buf ), "%d", lssd_cpClr.gradPos > 0 ? lssd_cpClr.gradPos : 50 );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_NUMBER,
			rx + LSSD_S(150), py, LSSD_S(42), LSSD_S(22), hw, (HMENU)IDC_CP_GRADPOS, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		py += LSSD_S(48);

		/* OK / Cancel */
		h = CreateWindowExA( 0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			rx + LSSD_S(40), py, LSSD_S(80), LSSD_S(28), hw, (HMENU)IDOK, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );
		h = CreateWindowExA( 0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
			rx + LSSD_S(130), py, LSSD_S(80), LSSD_S(28), hw, (HMENU)IDCANCEL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_cpFont, TRUE );

		LSSD_CPUpdateEnable( hw );
		return 0;
	}

	case WM_ERASEBKGND:
		return 1;  /* handled by double-buffered WM_PAINT */

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC paintDC = BeginPaint( hw, &ps );
		HDC dc;
		HBITMAP backBuf, oldBuf;
		HDC memDC;
		RECT cr;
		int y, i;
		int pvX, pvY, pvW, pvH, midPct;

		/* double-buffer: paint to off-screen bitmap to avoid flicker */
		GetClientRect( hw, &cr );
		dc = CreateCompatibleDC( paintDC );
		backBuf = CreateCompatibleBitmap( paintDC, cr.right, cr.bottom );
		oldBuf = (HBITMAP)SelectObject( dc, backBuf );

		/* background */
		FillRect( dc, &cr, GetSysColorBrush( COLOR_3DFACE ) );

		SelectObject( dc, lssd_cpFont );
		SetBkMode( dc, TRANSPARENT );
		SetTextColor( dc, GetSysColor( COLOR_WINDOWTEXT ) );

		/* --- draw SV square --- */
		LSSD_CPBuildSV( lssd_cpHue );
		if ( lssd_cpSvBmp ) {
			memDC = CreateCompatibleDC( dc );
			SelectObject( memDC, lssd_cpSvBmp );
			BitBlt( dc, CP_SVX, CP_SVY, CP_SVSZ, CP_SVSZ, memDC, 0, 0, SRCCOPY );
			DeleteDC( memDC );
		}
		/* crosshair on SV */
		{
			int cx = CP_SVX + (int)(lssd_cpSat * (CP_SVSZ - 1));
			int cy = CP_SVY + (int)((1.0f - lssd_cpVal) * (CP_SVSZ - 1));
			HPEN wpen = CreatePen( PS_SOLID, 2, RGB(255,255,255) );
			HPEN bpen = CreatePen( PS_SOLID, 1, RGB(0,0,0) );
			HPEN old = (HPEN)SelectObject( dc, wpen );
			HBRUSH oldBr = (HBRUSH)SelectObject( dc, GetStockObject( NULL_BRUSH ) );
			Ellipse( dc, cx - 6, cy - 6, cx + 6, cy + 6 );
			SelectObject( dc, bpen );
			Ellipse( dc, cx - 7, cy - 7, cx + 7, cy + 7 );
			SelectObject( dc, oldBr );
			SelectObject( dc, old );
			DeleteObject( wpen );
			DeleteObject( bpen );
		}

		/* --- draw Hue bar --- */
		for ( y = 0; y < CP_HUE_H; y++ ) {
			float h2 = (float)y / (CP_HUE_H - 1) * 360.0f;
			int r2, g2, b2;
			RECT rr;
			HBRUSH br;
			LSSD_HSVtoRGB( h2, 1.0f, 1.0f, &r2, &g2, &b2 );
			rr.left = CP_HUE_X; rr.right = CP_HUE_X + CP_HUE_W;
			rr.top = CP_HUE_Y + y; rr.bottom = CP_HUE_Y + y + 1;
			br = CreateSolidBrush( RGB(r2, g2, b2) );
			FillRect( dc, &rr, br );
			DeleteObject( br );
		}
		/* hue indicator */
		{
			int hy = CP_HUE_Y + (int)(lssd_cpHue / 360.0f * (CP_HUE_H - 1));
			HPEN p = CreatePen( PS_SOLID, 2, RGB(255,255,255) );
			HPEN olp = (HPEN)SelectObject( dc, p );
			MoveToEx( dc, CP_HUE_X - 2, hy, NULL );
			LineTo( dc, CP_HUE_X + CP_HUE_W + 2, hy );
			SelectObject( dc, olp );
			DeleteObject( p );
		}
		/* borders */
		{
			HPEN bp = CreatePen( PS_SOLID, 1, GetSysColor( COLOR_3DSHADOW ) );
			HPEN olp = (HPEN)SelectObject( dc, bp );
			HBRUSH oldBr = (HBRUSH)SelectObject( dc, GetStockObject( NULL_BRUSH ) );
			Rectangle( dc, CP_SVX-1, CP_SVY-1, CP_SVX+CP_SVSZ+1, CP_SVY+CP_SVSZ+1 );
			Rectangle( dc, CP_HUE_X-1, CP_HUE_Y-1, CP_HUE_X+CP_HUE_W+1, CP_HUE_Y+CP_HUE_H+1 );
			SelectObject( dc, oldBr );
			SelectObject( dc, olp );
			DeleteObject( bp );
		}

		/* --- preset colors row --- */
		SetTextColor( dc, GetSysColor( COLOR_GRAYTEXT ) );
		TextOutA( dc, CP_SVX, CP_SVY + CP_SVSZ + LSSD_S(6), "Presets:", 8 );
		for ( i = 0; i < (int)CP_NUM_PRESETS; i++ ) {
			int sx = CP_SVX + i * LSSD_S(21);
			int sy = CP_SVY + CP_SVSZ + LSSD_S(22);
			LSSD_CPDrawSwatch( dc, sx, sy, LSSD_S(18), LSSD_S(16), lssd_cpPresets[i],
				( lssd_cpClr.c1 == lssd_cpPresets[i] ) );
		}

		/* --- recent colors row --- */
		if ( lssd_cpRecentCount > 0 ) {
			SetTextColor( dc, GetSysColor( COLOR_GRAYTEXT ) );
			TextOutA( dc, CP_SVX, CP_SVY + CP_SVSZ + LSSD_S(44), "Recent:", 7 );
			for ( i = 0; i < lssd_cpRecentCount; i++ ) {
				int sx = CP_SVX + i * LSSD_S(21);
				int sy = CP_SVY + CP_SVSZ + LSSD_S(60);
				LSSD_CPDrawSwatch( dc, sx, sy, LSSD_S(18), LSSD_S(16), lssd_cpRecent[i], 0 );
			}
		}

		/* --- gradient preview with sample text --- */
		SetTextColor( dc, GetSysColor( COLOR_GRAYTEXT ) );
		TextOutA( dc, CP_SVX, CP_SVY + CP_SVSZ + LSSD_S(82), "Preview:", 8 );
		midPct = lssd_cpClr.gradPos > 0 ? lssd_cpClr.gradPos : 50;
		pvX = CP_SVX;
		pvY = CP_SVY + CP_SVSZ + LSSD_S(98);
		pvW = CP_SVSZ + CP_HUE_W + LSSD_S(10);
		pvH = LSSD_S(36);
		{
			unsigned rgb1 = ( lssd_cpClr.c1 != LSCLR_INHERIT ) ? lssd_cpClr.c1 : 0;
			COLORREF c1 = LSWND_CLR( rgb1 ), c2 = LSWND_CLR( lssd_cpClr.c2 );
			RECT pvrc;
			switch ( lssd_cpClr.mode ) {
			case LSCLR_MODE_VGRADIENT: LSRND_GradientV( dc, pvX, pvY, pvW, pvH, c1, c2, midPct ); break;
			case LSCLR_MODE_HGRADIENT: LSRND_GradientH( dc, pvX, pvY, pvW, pvH, c1, c2, midPct ); break;
			default:                   LSRND_FillRC( dc, pvX, pvY, pvW, pvH, c1 );                 break;
			}
			/* draw sample text on the preview */
			SetBkMode( dc, TRANSPARENT );
			SetTextColor( dc, RGB(255,255,255) );
			SetRect( &pvrc, pvX + 6, pvY + 2, pvX + pvW - 6, pvY + pvH - 2 );
			DrawTextA( dc, "Split Name  +1.234", -1, &pvrc, DT_SINGLELINE | DT_VCENTER | DT_CENTER );
			{
				HPEN bp = CreatePen( PS_SOLID, 1, GetSysColor( COLOR_3DSHADOW ) );
				HPEN olp = (HPEN)SelectObject( dc, bp );
				HBRUSH oldBr = (HBRUSH)SelectObject( dc, GetStockObject( NULL_BRUSH ) );
				Rectangle( dc, pvX, pvY, pvX+pvW, pvY+pvH );
				SelectObject( dc, oldBr );
				SelectObject( dc, olp );
				DeleteObject( bp );
			}
		}

		/* flush back-buffer to screen */
		BitBlt( paintDC, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY );
		SelectObject( dc, oldBuf );
		DeleteObject( backBuf );
		DeleteDC( dc );
		EndPaint( hw, &ps );
		return 0;
	}

	case WM_DRAWITEM: {
		DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
		unsigned rgb = 0;
		HBRUSH br;
		HPEN bp, olp;
		HBRUSH oldBr2;
		int active;
		if ( dis->CtlID == IDC_CP_CLR1 ) {
			rgb = ( lssd_cpClr.c1 != LSCLR_INHERIT ) ? lssd_cpClr.c1 : 0;
			active = ( lssd_cpActive == 1 );
		} else if ( dis->CtlID == IDC_CP_CLR2 ) {
			rgb = lssd_cpClr.c2;
			active = ( lssd_cpActive == 2 );
		} else {
			break;
		}
		br = CreateSolidBrush( LSWND_CLR( rgb ) );
		FillRect( dis->hDC, &dis->rcItem, br );
		DeleteObject( br );
		bp = CreatePen( PS_SOLID, active ? 2 : 1,
			active ? RGB(255,255,255) : GetSysColor( COLOR_3DSHADOW ) );
		olp = (HPEN)SelectObject( dis->hDC, bp );
		oldBr2 = (HBRUSH)SelectObject( dis->hDC, GetStockObject( NULL_BRUSH ) );
		Rectangle( dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom );
		SelectObject( dis->hDC, oldBr2 );
		SelectObject( dis->hDC, olp );
		DeleteObject( bp );
		return TRUE;
	}

	case WM_LBUTTONDOWN: {
		int mx = LOWORD(lp), my = HIWORD(lp);
		int i;

		/* SV square */
		if ( mx >= CP_SVX && mx < CP_SVX + CP_SVSZ && my >= CP_SVY && my < CP_SVY + CP_SVSZ ) {
			lssd_cpDragSV = 1;
			SetCapture( hw );
			lssd_cpSat = (float)(mx - CP_SVX) / (CP_SVSZ - 1);
			lssd_cpVal = 1.0f - (float)(my - CP_SVY) / (CP_SVSZ - 1);
			LSSD_CPSetActiveColor( LSSD_CPCurrentRGB() );
			LSSD_CPUpdateHex( hw );
			LSSD_CPInvalidate( hw );
		}
		/* Hue bar */
		else if ( mx >= CP_HUE_X && mx < CP_HUE_X + CP_HUE_W && my >= CP_HUE_Y && my < CP_HUE_Y + CP_HUE_H ) {
			lssd_cpDragHue = 1;
			SetCapture( hw );
			lssd_cpHue = (float)(my - CP_HUE_Y) / (CP_HUE_H - 1) * 360.0f;
			if (lssd_cpHue < 0) lssd_cpHue = 0; if (lssd_cpHue > 360) lssd_cpHue = 360;
			LSSD_CPSetActiveColor( LSSD_CPCurrentRGB() );
			LSSD_CPUpdateHex( hw );
			LSSD_CPInvalidate( hw );
		}
		/* Preset click */
		for ( i = 0; i < (int)CP_NUM_PRESETS; i++ ) {
			int sx = CP_SVX + i * LSSD_S(21);
			int sy = CP_SVY + CP_SVSZ + LSSD_S(22);
			if ( mx >= sx && mx < sx + LSSD_S(18) && my >= sy && my < sy + LSSD_S(16) ) {
				LSSD_CPSetActiveColor( lssd_cpPresets[i] );
				LSSD_CPSyncFromRGB( hw, lssd_cpPresets[i] );
				lssd_cpSvHue = -1;
				LSSD_CPUpdateHex( hw );
				LSSD_CPInvalidate( hw );
				break;
			}
		}
		/* Recent click */
		for ( i = 0; i < lssd_cpRecentCount; i++ ) {
			int sx = CP_SVX + i * LSSD_S(21);
			int sy = CP_SVY + CP_SVSZ + LSSD_S(60);
			if ( mx >= sx && mx < sx + LSSD_S(18) && my >= sy && my < sy + LSSD_S(16) ) {
				LSSD_CPSetActiveColor( lssd_cpRecent[i] );
				LSSD_CPSyncFromRGB( hw, lssd_cpRecent[i] );
				lssd_cpSvHue = -1;
				LSSD_CPUpdateHex( hw );
				LSSD_CPInvalidate( hw );
				break;
			}
		}
		return 0;
	}

	case WM_MOUSEMOVE:
		if ( lssd_cpDragSV ) {
			int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
			lssd_cpSat = (float)(mx - CP_SVX) / (CP_SVSZ - 1);
			lssd_cpVal = 1.0f - (float)(my - CP_SVY) / (CP_SVSZ - 1);
			if (lssd_cpSat < 0) lssd_cpSat = 0; if (lssd_cpSat > 1) lssd_cpSat = 1;
			if (lssd_cpVal < 0) lssd_cpVal = 0; if (lssd_cpVal > 1) lssd_cpVal = 1;
			LSSD_CPSetActiveColor( LSSD_CPCurrentRGB() );
			LSSD_CPUpdateHex( hw );
			LSSD_CPInvalidate( hw );
		} else if ( lssd_cpDragHue ) {
			int my = (short)HIWORD(lp);
			lssd_cpHue = (float)(my - CP_HUE_Y) / (CP_HUE_H - 1) * 360.0f;
			if (lssd_cpHue < 0) lssd_cpHue = 0; if (lssd_cpHue > 360) lssd_cpHue = 360;
			lssd_cpSvHue = -1;
			LSSD_CPSetActiveColor( LSSD_CPCurrentRGB() );
			LSSD_CPUpdateHex( hw );
			LSSD_CPInvalidate( hw );
		}
		return 0;

	case WM_LBUTTONUP:
		if ( lssd_cpDragSV || lssd_cpDragHue ) {
			lssd_cpDragSV = 0;
			lssd_cpDragHue = 0;
			ReleaseCapture();
		}
		return 0;

	case WM_COMMAND:
		switch ( LOWORD( wp ) ) {
		case IDC_CP_GRADPOS:
			if ( HIWORD( wp ) == EN_CHANGE && !lssd_cpSkipNotify ) {
				int gp = GetDlgItemInt( hw, IDC_CP_GRADPOS, NULL, FALSE );
				lssd_cpClr.gradPos = ( gp < 1 ) ? 1 : ( gp > 99 ) ? 99 : gp;
				LSSD_CPInvalidate( hw );
			}
			break;
		case IDC_CP_INHERIT:
			if ( IsDlgButtonChecked( hw, IDC_CP_INHERIT ) )
				lssd_cpClr.c1 = LSCLR_INHERIT;
			else if ( lssd_cpClr.c1 == LSCLR_INHERIT )
				lssd_cpClr.c1 = 0;
			LSSD_CPUpdateEnable( hw );
			LSSD_CPInvalidate( hw );
			break;
		case IDC_CP_PLAIN: case IDC_CP_VGRAD: case IDC_CP_HGRAD:
			lssd_cpClr.mode = LOWORD( wp ) - IDC_CP_PLAIN;
			LSSD_CPUpdateEnable( hw );
			LSSD_CPInvalidate( hw );
			break;
		case IDC_CP_CLR1:
			if ( HIWORD(wp) == BN_CLICKED ) {
				lssd_cpActive = 1;
				LSSD_CPSyncFromRGB( hw, (lssd_cpClr.c1 != LSCLR_INHERIT) ? lssd_cpClr.c1 : 0 );
				lssd_cpSvHue = -1;
				LSSD_CPUpdateHex( hw );
				LSSD_CPInvalidate( hw );
			}
			break;
		case IDC_CP_CLR2:
			if ( HIWORD(wp) == BN_CLICKED ) {
				lssd_cpActive = 2;
				LSSD_CPSyncFromRGB( hw, lssd_cpClr.c2 );
				lssd_cpSvHue = -1;
				LSSD_CPUpdateHex( hw );
				LSSD_CPInvalidate( hw );
			}
			break;
		case IDC_CP_HEX1:
		case IDC_CP_HEX2:
			if ( HIWORD(wp) == EN_CHANGE && !lssd_cpSkipNotify ) {
				char buf[16];
				unsigned rgb;
				GetDlgItemTextA( hw, LOWORD(wp), buf, sizeof(buf) );
				rgb = (unsigned)strtoul( buf, NULL, 16 );
				if ( LOWORD(wp) == IDC_CP_HEX1 ) {
					lssd_cpClr.c1 = rgb;
					if ( lssd_cpActive == 1 ) LSSD_CPSyncFromRGB( hw, rgb );
				} else {
					lssd_cpClr.c2 = rgb;
					if ( lssd_cpActive == 2 ) LSSD_CPSyncFromRGB( hw, rgb );
				}
				lssd_cpSvHue = -1;
				lssd_cpSkipNotify = 1;
				SetDlgItemInt( hw, IDC_CP_R1, (rgb >> 16) & 0xFF, FALSE );
				SetDlgItemInt( hw, IDC_CP_G1, (rgb >> 8) & 0xFF, FALSE );
				SetDlgItemInt( hw, IDC_CP_B1, rgb & 0xFF, FALSE );
				lssd_cpSkipNotify = 0;
				LSSD_CPInvalidate( hw );
			}
			break;
		case IDC_CP_R1: case IDC_CP_G1: case IDC_CP_B1:
			if ( HIWORD(wp) == EN_CHANGE && !lssd_cpSkipNotify ) {
				int r = GetDlgItemInt( hw, IDC_CP_R1, NULL, FALSE );
				int g = GetDlgItemInt( hw, IDC_CP_G1, NULL, FALSE );
				int b = GetDlgItemInt( hw, IDC_CP_B1, NULL, FALSE );
				unsigned rgb;
				if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
				rgb = ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
				LSSD_CPSetActiveColor( rgb );
				LSSD_CPSyncFromRGB( hw, rgb );
				lssd_cpSvHue = -1;
				lssd_cpSkipNotify = 1;
				{
					char hx[16];
					Com_sprintf( hx, sizeof(hx), "%06X", rgb & 0xFFFFFF );
					SetDlgItemTextA( hw, (lssd_cpActive == 2) ? IDC_CP_HEX2 : IDC_CP_HEX1, hx );
				}
				lssd_cpSkipNotify = 0;
				LSSD_CPInvalidate( hw );
			}
			break;
		case IDOK: {
			int a = GetDlgItemInt( hw, IDC_CP_ALPHA, NULL, FALSE );
			int gp = GetDlgItemInt( hw, IDC_CP_GRADPOS, NULL, FALSE );
			lssd_cpClr.alpha = ( a < 0 ) ? 0 : ( a > 255 ) ? 255 : a;
			lssd_cpClr.gradPos = ( gp < 1 ) ? 1 : ( gp > 99 ) ? 99 : gp;
			/* add to recent colors */
			if ( lssd_cpClr.c1 != LSCLR_INHERIT )
				LSSD_CPAddRecent( lssd_cpClr.c1 );
			lssd_cpResult = 1;
			DestroyWindow( hw );
			break;
		}
		case IDCANCEL:
			lssd_cpResult = 0;
			DestroyWindow( hw );
			break;
		}
		return 0;

	case WM_DESTROY:
		if ( lssd_cpFont ) { DeleteObject( lssd_cpFont ); lssd_cpFont = NULL; }
		if ( lssd_cpSvBmp ) { DeleteObject( lssd_cpSvBmp ); lssd_cpSvBmp = NULL; }
		lssd_cpDone = 1;
		return 0;

	case WM_CLOSE:
		lssd_cpResult = 0;
		DestroyWindow( hw );
		return 0;
	}
	return DefWindowProcA( hw, msg, wp, lp );
}

/* Open color picker; returns 1 if OK (clr modified), 0 if cancelled */
static int LSSD_EditColor( HWND parent, lsColor_t *clr ) {
	static int reg = 0;
	HWND hw;
	MSG msg;
	RECT pr;

	lssd_cpClr    = *clr;
	lssd_cpDone   = 0;
	lssd_cpResult = 0;


	if ( !reg ) {
		WNDCLASSEXA wc = {0};
		wc.cbSize        = sizeof( wc );
		wc.lpfnWndProc   = LSSD_CPProc;
		wc.hInstance      = GetModuleHandle( NULL );
		wc.lpszClassName  = "LSSD_ColorPicker";
		wc.hbrBackground  = (HBRUSH)( COLOR_3DFACE + 1 );
		wc.hCursor        = LoadCursor( NULL, IDC_ARROW );
		RegisterClassExA( &wc );
		reg = 1;
	}

	GetWindowRect( parent, &pr );
	hw = CreateWindowExA( WS_EX_TOOLWINDOW, "LSSD_ColorPicker", "Color Picker",
		WS_POPUP | WS_CAPTION | WS_SYSMENU,
		pr.left + LSSD_S(40), pr.top + LSSD_S(30),
		LSSD_S(540), LSSD_S(420),
		parent, NULL, GetModuleHandle( NULL ), NULL );

	EnableWindow( parent, FALSE );
	ShowWindow( hw, SW_SHOW );
	UpdateWindow( hw );

	while ( !lssd_cpDone && GetMessage( &msg, NULL, 0, 0 ) ) {
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	EnableWindow( parent, TRUE );
	SetForegroundWindow( parent );

	if ( lssd_cpResult ) {
		*clr = lssd_cpClr;
		return 1;
	}
	return 0;
}

/* =====================================================================
   Font Picker -- dark themed, cached fonts, preview
   ===================================================================== */

#define IDC_FP_LIST     120
#define IDC_FP_SEARCH   121
#define IDC_FP_SIZE     122
#define IDC_FP_PREVIEW  123
#define IDC_FP_BOLD     124
#define IDC_FP_CATEGORY 125

static char     lssd_fpFace[LF_FACESIZE];
static int      lssd_fpSize;
static int      lssd_fpBold;
static int      lssd_fpDone;
static int      lssd_fpResult;
static HFONT    lssd_fpFont;
static HWND     lssd_fpListHw;

/* Cached font list */
#define FP_MAX_FONTS 1024
static char lssd_fpCache[FP_MAX_FONTS][LF_FACESIZE];
static int  lssd_fpCacheCount = 0;
static int  lssd_fpCacheDone = 0;

static int CALLBACK LSSD_FPCacheEnum( const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM lParam ) {
	int i;
	(void)tm; (void)type; (void)lParam;
	if ( lf->lfFaceName[0] == '@' ) return 1;
	/* check duplicate */
	for ( i = 0; i < lssd_fpCacheCount; i++ )
		if ( !strcmp( lssd_fpCache[i], lf->lfFaceName ) ) return 1;
	if ( lssd_fpCacheCount < FP_MAX_FONTS ) {
		Q_strncpyz( lssd_fpCache[lssd_fpCacheCount], lf->lfFaceName, LF_FACESIZE );
		lssd_fpCacheCount++;
	}
	return 1;
}

static void LSSD_FPEnsureCache( void ) {
	if ( !lssd_fpCacheDone ) {
		HDC dc = GetDC( NULL );
		lssd_fpCacheCount = 0;
		EnumFontFamiliesA( dc, NULL, (FONTENUMPROCA)LSSD_FPCacheEnum, 0 );
		ReleaseDC( NULL, dc );
		lssd_fpCacheDone = 1;
	}
}

static void LSSD_FPPopulate( const char *filter ) {
	int i;
	char filterLow[LF_FACESIZE], itemLow[LF_FACESIZE];
	int j;

	SendMessageA( lssd_fpListHw, WM_SETREDRAW, FALSE, 0 );
	SendMessageA( lssd_fpListHw, LB_RESETCONTENT, 0, 0 );

	if ( filter && filter[0] ) {
		Q_strncpyz( filterLow, filter, sizeof(filterLow) );
		for ( j = 0; filterLow[j]; j++ ) filterLow[j] = (char)tolower((unsigned char)filterLow[j]);
	}

	for ( i = 0; i < lssd_fpCacheCount; i++ ) {
		if ( filter && filter[0] ) {
			Q_strncpyz( itemLow, lssd_fpCache[i], sizeof(itemLow) );
			for ( j = 0; itemLow[j]; j++ ) itemLow[j] = (char)tolower((unsigned char)itemLow[j]);
			if ( !strstr( itemLow, filterLow ) ) continue;
		}
		SendMessageA( lssd_fpListHw, LB_ADDSTRING, 0, (LPARAM)lssd_fpCache[i] );
	}

	SendMessageA( lssd_fpListHw, WM_SETREDRAW, TRUE, 0 );
	InvalidateRect( lssd_fpListHw, NULL, TRUE );
}

static void LSSD_FPUpdatePreview( HWND hw ) {
	HWND prev = GetDlgItem( hw, IDC_FP_PREVIEW );
	if ( prev ) InvalidateRect( prev, NULL, TRUE );
}

static LRESULT CALLBACK LSSD_FPProc( HWND hw, UINT msg, WPARAM wp, LPARAM lp ) {
	switch ( msg ) {
	case WM_CREATE: {
		HWND h;
		char buf[16];
		int sel, py;

		lssd_fpFont = CreateFontA( LSSD_S(-13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, "Segoe UI" );

		LSSD_FPEnsureCache();

		/* Search filter */
		h = CreateWindowExA( 0, "STATIC", "Filter:", WS_CHILD | WS_VISIBLE, 14, 12, 44, 16, hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
			60, 10, 230, 22, hw, (HMENU)IDC_FP_SEARCH, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );

		/* Font list */
		lssd_fpListHw = CreateWindowExA( WS_EX_CLIENTEDGE, "LISTBOX", "",
			WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_SORT,
			14, 38, 280, 260, hw, (HMENU)IDC_FP_LIST, NULL, NULL );
		SendMessage( lssd_fpListHw, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );

		LSSD_FPPopulate( NULL );

		sel = (int)SendMessageA( lssd_fpListHw, LB_FINDSTRINGEXACT, -1, (LPARAM)lssd_fpFace );
		if ( sel != LB_ERR ) {
			SendMessageA( lssd_fpListHw, LB_SETCURSEL, sel, 0 );
			SendMessageA( lssd_fpListHw, LB_SETTOPINDEX, sel > 3 ? sel - 3 : 0, 0 );
		}

		py = 304;
		h = CreateWindowExA( 0, "STATIC", "Size:", WS_CHILD | WS_VISIBLE, 14, py + 2, 40, 16, hw, NULL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );
		Com_sprintf( buf, sizeof(buf), "%d", lssd_fpSize );
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "EDIT", buf, WS_CHILD | WS_VISIBLE | ES_NUMBER,
			58, py, 50, 22, hw, (HMENU)IDC_FP_SIZE, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );

		/* Bold checkbox */
		h = CreateWindowExA( 0, "BUTTON", "Bold", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
			120, py + 2, 60, 18, hw, (HMENU)IDC_FP_BOLD, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );
		if ( lssd_fpBold ) CheckDlgButton( hw, IDC_FP_BOLD, BST_CHECKED );

		/* Preview (dark background) */
		h = CreateWindowExA( WS_EX_CLIENTEDGE, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
			14, py + 30, 280, 56, hw, (HMENU)IDC_FP_PREVIEW, NULL, NULL );

		/* Buttons */
		h = CreateWindowExA( 0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			80, py + 96, 80, 28, hw, (HMENU)IDOK, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );
		h = CreateWindowExA( 0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
			170, py + 96, 80, 28, hw, (HMENU)IDCANCEL, NULL, NULL );
		SendMessage( h, WM_SETFONT, (WPARAM)lssd_fpFont, TRUE );

		return 0;
	}

	case WM_DRAWITEM: {
		DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
		if ( dis->CtlID == IDC_FP_PREVIEW ) {
			int sz = lssd_fpSize > 0 ? lssd_fpSize : 16;
			int bold = lssd_fpBold;
			HFONT pf = CreateFontA( -sz, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
				OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
				DEFAULT_PITCH | FF_SWISS, lssd_fpFace );
			HFONT old = (HFONT)SelectObject( dis->hDC, pf );
			/* dark background matching LiveSplit */
			{
				unsigned bgRGB = LSCLR_Resolve( lswnd_layout.bgColor, 0x171722 );
				HBRUSH bgBr = CreateSolidBrush( LSWND_CLR( bgRGB ) );
				FillRect( dis->hDC, &dis->rcItem, bgBr );
				DeleteObject( bgBr );
			}
			SetBkMode( dis->hDC, TRANSPARENT );
			SetTextColor( dis->hDC, LSWND_CLR( LSCLR_Resolve( lswnd_layout.textColor, 0xDCDCDC ) ) );
			DrawTextA( dis->hDC, "AaBbCcDdEe 123:45.67", -1, &dis->rcItem,
				DT_SINGLELINE | DT_CENTER | DT_VCENTER );
			SelectObject( dis->hDC, old );
			DeleteObject( pf );
			return TRUE;
		}
		break;
	}

	case WM_COMMAND:
		switch ( LOWORD(wp) ) {
		case IDC_FP_SEARCH:
			if ( HIWORD(wp) == EN_CHANGE ) {
				char filter[LF_FACESIZE];
				GetDlgItemTextA( hw, IDC_FP_SEARCH, filter, sizeof(filter) );
				LSSD_FPPopulate( filter[0] ? filter : NULL );
				{
					int sel = (int)SendMessageA( lssd_fpListHw, LB_FINDSTRINGEXACT, -1, (LPARAM)lssd_fpFace );
					if ( sel != LB_ERR ) SendMessageA( lssd_fpListHw, LB_SETCURSEL, sel, 0 );
				}
			}
			break;
		case IDC_FP_LIST:
			if ( HIWORD(wp) == LBN_SELCHANGE ) {
				int sel = (int)SendMessageA( lssd_fpListHw, LB_GETCURSEL, 0, 0 );
				if ( sel != LB_ERR )
					SendMessageA( lssd_fpListHw, LB_GETTEXT, sel, (LPARAM)lssd_fpFace );
				LSSD_FPUpdatePreview( hw );
			}
			break;
		case IDC_FP_SIZE:
			if ( HIWORD(wp) == EN_CHANGE ) {
				lssd_fpSize = GetDlgItemInt( hw, IDC_FP_SIZE, NULL, FALSE );
				LSSD_FPUpdatePreview( hw );
			}
			break;
		case IDC_FP_BOLD:
			lssd_fpBold = IsDlgButtonChecked( hw, IDC_FP_BOLD ) == BST_CHECKED;
			LSSD_FPUpdatePreview( hw );
			break;
		case IDOK:
			{
				int sel = (int)SendMessageA( lssd_fpListHw, LB_GETCURSEL, 0, 0 );
				if ( sel != LB_ERR )
					SendMessageA( lssd_fpListHw, LB_GETTEXT, sel, (LPARAM)lssd_fpFace );
			}
			lssd_fpSize = GetDlgItemInt( hw, IDC_FP_SIZE, NULL, FALSE );
			lssd_fpBold = IsDlgButtonChecked( hw, IDC_FP_BOLD ) == BST_CHECKED;
			lssd_fpResult = 1;
			DestroyWindow( hw );
			break;
		case IDCANCEL:
			lssd_fpResult = 0;
			DestroyWindow( hw );
			break;
		}
		return 0;

	case WM_DESTROY:
		if ( lssd_fpFont ) { DeleteObject( lssd_fpFont ); lssd_fpFont = NULL; }
		lssd_fpDone = 1;
		return 0;

	case WM_CLOSE:
		lssd_fpResult = 0;
		DestroyWindow( hw );
		return 0;
	}
	return DefWindowProcA( hw, msg, wp, lp );
}

static int LSSD_EditFont( HWND parent, char *faceBuf, int faceBufSize, int *pSize, int *pBold ) {
	static int reg = 0;
	HWND hw;
	MSG msg;
	RECT pr;

	Q_strncpyz( lssd_fpFace, faceBuf, sizeof(lssd_fpFace) );
	lssd_fpSize   = pSize ? *pSize : 16;
	lssd_fpBold   = pBold ? *pBold : 0;
	lssd_fpDone   = 0;
	lssd_fpResult = 0;


	if ( !reg ) {
		WNDCLASSEXA wc = {0};
		wc.cbSize        = sizeof( wc );
		wc.lpfnWndProc   = LSSD_FPProc;
		wc.hInstance      = GetModuleHandle( NULL );
		wc.lpszClassName  = "LSSD_FontPicker";
		wc.hbrBackground  = (HBRUSH)( COLOR_3DFACE + 1 );
		wc.hCursor        = LoadCursor( NULL, IDC_ARROW );
		RegisterClassExA( &wc );
		reg = 1;
	}

	GetWindowRect( parent, &pr );
	hw = CreateWindowExA( WS_EX_TOOLWINDOW, "LSSD_FontPicker", "Font Picker",
		WS_POPUP | WS_CAPTION | WS_SYSMENU,
		pr.left + LSSD_S(60), pr.top + LSSD_S(30),
		LSSD_S(320), LSSD_S(470),
		parent, NULL, GetModuleHandle( NULL ), NULL );

	EnableWindow( parent, FALSE );
	ShowWindow( hw, SW_SHOW );
	UpdateWindow( hw );

	while ( !lssd_fpDone && GetMessage( &msg, NULL, 0, 0 ) ) {
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	EnableWindow( parent, TRUE );
	SetForegroundWindow( parent );

	if ( lssd_fpResult ) {
		Q_strncpyz( faceBuf, lssd_fpFace, faceBufSize );
		if ( pSize ) *pSize = lssd_fpSize;
		if ( pBold ) *pBold = lssd_fpBold;
		return 1;
	}
	return 0;
}

/* =====================================================================
   Settings Dialog  --  Dark themed, fixed scroll, new options
   ===================================================================== */

/* ---- Control IDs ---- */
#define IDC_LIST         2001
#define IDC_BTN_ADD      2002
#define IDC_BTN_REM      2003
#define IDC_BTN_UP       2004
#define IDC_BTN_DN       2005
#define IDC_BTN_OK       2010
#define IDC_BTN_CANCEL   2011
#define IDC_BTN_RESET    2012
#define IDC_BTN_RESETCOMP 2013
#define IDC_BTN_EXPORT   2014
#define IDC_BTN_IMPORT   2015

/* Common component settings */
#define IDC_S_ENABLED    3000
#define IDC_S_FONT       3001
#define IDC_S_FONTBTN    3002
#define IDC_S_FONTSIZE   3003
#define IDC_S_TEXTCLR    3004
#define IDC_S_BGCLR      3005
#define IDC_S_PADH       3006
#define IDC_S_PADV       3009
#define IDC_S_ALIGN      3007
#define IDC_S_BOLD       3008
#define IDC_S_OVERRIDEH  3010

/* Title */
#define IDC_T_GAMENAME   3100
#define IDC_T_CATEGORY   3101
#define IDC_T_ATTEMPTS   3102
#define IDC_T_ATTNEWLINE 3103
#define IDC_T_TOPACCENT  3104
#define IDC_T_BOTACCENT  3105

/* Splits */
#define IDC_SP_VISIBLE   3110
#define IDC_SP_THINSEPS  3111
#define IDC_SP_SHOWLAST  3112
#define IDC_SP_LOCKLAST  3113
#define IDC_SP_SEPLAST   3114
#define IDC_SP_DELTAACC  3115
#define IDC_SP_DELTADROP 3116
#define IDC_SP_SPLITACC  3117
#define IDC_SP_COLADD    3119
#define IDC_SP_HEADER    3123
#define IDC_SP_ALTROWS   3124
#define IDC_SP_SHORTNAMES 3125

/* Inline column IDs: base + col_idx * stride + field
   0=name 1=type 2=textClr 3=width 4=moveUp 5=moveDown 6=remove
   7=font 8=fontSize 9=bold 10=fontBtn 11=beforeClr 12=currentClr 13=afterClr */
#define IDC_COL_BASE    6100
#define IDC_COL_STRIDE  20

#define IDC_SP_CLR_CUR   3130
#define IDC_SP_CLR_BCUR  3131
#define IDC_SP_CLR_CSPL  3132
#define IDC_SP_CLR_ACUR  3133
#define IDC_SP_CLR_LDLT  3134
#define IDC_SP_CLR_STIM  3135
#define IDC_SP_CLR_BSPL  3136
#define IDC_SP_CLR_NAME  3137
#define IDC_SP_CLR_HDR   3138
#define IDC_SP_CLR_ALT   3139
#define IDC_SP_CLR_ROW   3140
#define IDC_SP_CLR_SEP   3141

/* Per-state name styling */
#define IDC_SPN_BFONT    3250
#define IDC_SPN_BFSIZE   3251
#define IDC_SPN_BBOLD    3252
#define IDC_SPN_BCLR     3253
#define IDC_SPN_CFONT    3254
#define IDC_SPN_CFSIZE   3255
#define IDC_SPN_CBOLD    3256
#define IDC_SPN_AFONT    3257
#define IDC_SPN_AFSIZE   3258
#define IDC_SPN_ABOLD    3259
#define IDC_SPN_ACLR     3260
#define IDC_SPN_BFONTBTN 3261
#define IDC_SPN_CFONTBTN 3262
#define IDC_SPN_AFONTBTN 3263
#define IDC_SP_DELTACNT  3264
#define IDC_SP_COMPARE   3265

/* Timer */
#define IDC_TM_DECIMALS  3150
#define IDC_TM_METHOD    3151
#define IDC_TM_AHEAD     3152
#define IDC_TM_BEHIND    3153
#define IDC_TM_GOLD      3154
#define IDC_TM_DELTACLR  3155
#define IDC_TM_COMPARE   3156

/* Detailed Timer */
#define IDC_DT_MDEC      3150
#define IDC_DT_CDEC      3151
#define IDC_DT_METHOD    3152
#define IDC_DT_SHOWPB    3153
#define IDC_DT_SHOWBEST  3154
#define IDC_DT_MFSIZE    3155
#define IDC_DT_CFSIZE    3156
#define IDC_DT_MAHEAD    3157
#define IDC_DT_MBEHIND   3158
#define IDC_DT_MGOLD     3159
#define IDC_DT_PBCLR     3160
#define IDC_DT_BESTCLR   3161
#define IDC_DT_LBLCLR    3162
#define IDC_DT_SEGCLR    3163
#define IDC_DT_SEGNAME   3164
#define IDC_DT_SFSIZE    3165
#define IDC_DT_SEGTIMERCLR 3166
#define IDC_DT_DELTACLR  3167
#define IDC_DT_COMPARE   3168

/* Segment Timer */
#define IDC_ST_DECIMALS  3170
#define IDC_ST_METHOD    3171
#define IDC_ST_TIMERCLR  3172
#define IDC_ST_DELTACLR  3173
#define IDC_ST_COMPARE   3174

/* Real Time */
#define IDC_RT_DECIMALS  3270
#define IDC_RT_TIMERCLR  3271

/* Info row */
#define IDC_IR_ACCURACY  3180
#define IDC_IR_DROP      3181
#define IDC_IR_LIVE      3182
#define IDC_IR_VALCLR    3183
#define IDC_IR_LBLCLR    3184
#define IDC_IR_LABEL     3185
#define IDC_IR_COMPARE   3186

/* Best Segments */
#define IDC_BG_SHOWSEGS  3188

/* 100% Tracker */
#define IDC_PCT_TOTAL    3200
#define IDC_PCT_SEGMENT  3201

/* Separator */
#define IDC_SEP_HEIGHT   3190
#define IDC_SEP_COLOR    3191

/* Text */
#define IDC_TX_TEXT      3195

/* Blank Space */
#define IDC_BS_HEIGHT    3196

/* Header */
#define IDC_HD_TEXT      3197
#define IDC_HD_SHOWLINE  3198

/* Global layout */
#define IDC_G_FONT       3200
#define IDC_G_FONTBTN    3201
#define IDC_G_FONTSIZE   3202
#define IDC_G_BGCLR      3210
#define IDC_G_HDRBG      3211
#define IDC_G_ACCENT     3212
#define IDC_G_TEXTCLR    3213
#define IDC_G_AHEAD      3214
#define IDC_G_BEHIND     3215
#define IDC_G_GOLD       3216
#define IDC_G_WIDTH      3217
#define IDC_G_SHADOW     3218
#define IDC_G_THINACCENT 3219
#define IDC_G_OPACITY    3220
#define IDC_G_BOLD       3221
#define IDC_G_LOCKRESIZE 3222
#define IDC_G_ONTOP      3223
#define IDC_G_TRANSP     3224
#define IDC_G_COMPARE    3225
#define IDC_G_FLIP       3226
#define IDC_G_SPACING    3227

#define IDM_ADD_BASE     4000

/* ---- Dialog state ---- */
#define LSSD_MAX_DYN  200
#define LSSD_MAX_CLR  40
#define LSSD_RX       LSSD_S(14)
#define LSSD_GBX      LSSD_S(4)
#define LSSD_GBW      LSSD_S(476)
#define LSSD_PANEL_H  LSSD_S(450)
#define LSSD_PANEL_W  LSSD_S(490)

static HWND      lssd_hw;
static HWND      lssd_list;
static HWND      lssd_panel;
static HWND      lssd_dynCtrls[LSSD_MAX_DYN];
static int       lssd_dynCount;
static HFONT     lssd_font;
static int       lssd_py;
static int       lssd_skipNotify;
static int       lssd_lastSel;
static int       lssd_scrollPos;
static lsLayout_t lssd_backup;

typedef struct {
	HWND       hw;
	lsColor_t *pClr;
} lssd_clrSlot_t;

static lssd_clrSlot_t lssd_clrSlots[LSSD_MAX_CLR];
static int             lssd_clrCount;

/* ---- Helpers ---- */

static HWND LSSD_AddCtrl( const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id ) {
	HWND hw = CreateWindowExA( 0, cls, text, WS_CHILD | WS_VISIBLE | style,
		x, y, w, h, lssd_panel, (HMENU)(INT_PTR)id, NULL, NULL );
	SendMessage( hw, WM_SETFONT, (WPARAM)lssd_font, TRUE );
	if ( lssd_dynCount < LSSD_MAX_DYN ) lssd_dynCtrls[lssd_dynCount++] = hw;
	return hw;
}

static HWND LSSD_AddCtrlEx( DWORD exStyle, const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id ) {
	HWND hw = CreateWindowExA( exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
		x, y, w, h, lssd_panel, (HMENU)(INT_PTR)id, NULL, NULL );
	SendMessage( hw, WM_SETFONT, (WPARAM)lssd_font, TRUE );
	if ( lssd_dynCount < LSSD_MAX_DYN ) lssd_dynCtrls[lssd_dynCount++] = hw;
	return hw;
}

static void LSSD_Label( const char *text, int x, int w ) {
	LSSD_AddCtrl( "STATIC", text, SS_LEFT, x, lssd_py + LSSD_S(2), w, LSSD_S(16), 0 );
}

/* Section header: accent-colored horizontal line with title text */
static void LSSD_SectionBegin( const char *text ) {
	/* draw accent line + text as a static control */
	LSSD_AddCtrl( "STATIC", text, SS_LEFT | SS_OWNERDRAW,
		LSSD_GBX, lssd_py, LSSD_GBW, LSSD_S(20), 0 );
	lssd_py += LSSD_S(24);
}

static void LSSD_SectionEnd( void ) {
	lssd_py += LSSD_S(8);
}

/* GroupBox fallback for visual sections */
static HWND lssd_curGroup;
static int  lssd_groupStartY;

static void LSSD_GroupBegin( const char *text ) {
	lssd_groupStartY = lssd_py;
	lssd_curGroup = LSSD_AddCtrl( "BUTTON", text, BS_GROUPBOX,
		LSSD_GBX, lssd_py, LSSD_GBW, LSSD_S(50), 0 );
	lssd_py += LSSD_S(18);
}

static void LSSD_GroupEnd( void ) {
	int h = lssd_py - lssd_groupStartY + LSSD_S(8);
	SetWindowPos( lssd_curGroup, NULL, 0, 0, LSSD_GBW, h,
		SWP_NOMOVE | SWP_NOZORDER );
	lssd_py += LSSD_S(10);
}

static HWND LSSD_Check( int id, const char *text, int checked ) {
	HWND h = LSSD_AddCtrl( "BUTTON", text, BS_AUTOCHECKBOX, LSSD_RX, lssd_py, LSSD_S(300), LSSD_S(18), id );
	if ( checked ) CheckDlgButton( lssd_panel, id, BST_CHECKED );
	lssd_py += LSSD_S(22);
	return h;
}

static HWND LSSD_EditNum( int id, int val, int x, int w ) {
	char buf[16];
	Com_sprintf( buf, sizeof( buf ), "%d", val );
	return LSSD_AddCtrlEx( WS_EX_CLIENTEDGE, "EDIT", buf, ES_AUTOHSCROLL | ES_NUMBER, x, lssd_py, w, LSSD_S(20), id );
}

/* Signed number edit (allows negative, e.g. -1 for default padding) */
static HWND LSSD_EditSigned( int id, int val, int x, int w ) {
	char buf[16];
	Com_sprintf( buf, sizeof( buf ), "%d", val );
	return LSSD_AddCtrlEx( WS_EX_CLIENTEDGE, "EDIT", buf, ES_AUTOHSCROLL, x, lssd_py, w, LSSD_S(20), id );
}

static HWND LSSD_EditStr( int id, const char *val, int x, int w ) {
	return LSSD_AddCtrlEx( WS_EX_CLIENTEDGE, "EDIT", val, ES_AUTOHSCROLL, x, lssd_py, w, LSSD_S(20), id );
}

static HWND LSSD_Combo( int id, const char **items, int count, int sel, int x, int w ) {
	HWND h;
	int i;
	h = LSSD_AddCtrl( "COMBOBOX", "", CBS_DROPDOWNLIST, x, lssd_py, w, LSSD_S(200), id );
	for ( i = 0; i < count; i++ )
		SendMessageA( h, CB_ADDSTRING, 0, (LPARAM)items[i] );
	SendMessageA( h, CB_SETCURSEL, sel, 0 );
	return h;
}

static HWND LSSD_ClrBtn( int id, lsColor_t *clr, int x, int w ) {
	HWND h = LSSD_AddCtrl( "BUTTON", "", BS_OWNERDRAW, x, lssd_py, w, LSSD_S(20), id );
	if ( lssd_clrCount < LSSD_MAX_CLR ) {
		lssd_clrSlots[lssd_clrCount].hw   = h;
		lssd_clrSlots[lssd_clrCount].pClr = clr;
		lssd_clrCount++;
	}
	return h;
}

static void LSSD_Row( const char *label, int id, lsColor_t *clr ) {
	LSSD_Label( label, LSSD_RX, LSSD_S(120) );
	LSSD_ClrBtn( id, clr, LSSD_RX + LSSD_S(125), LSSD_S(80) );
	lssd_py += LSSD_S(26);
}

static void LSSD_ClearPanel( void ) {
	int i;
	for ( i = 0; i < lssd_dynCount; i++ )
		if ( lssd_dynCtrls[i] ) DestroyWindow( lssd_dynCtrls[i] );
	lssd_dynCount = 0;
	lssd_clrCount = 0;
	lssd_py = LSSD_S(10);
	lssd_scrollPos = 0;
	if ( lssd_panel ) {
		SetScrollPos( lssd_panel, SB_VERT, 0, TRUE );
	}
}

static void LSSD_RefreshList( void ) {
	int sel, i;
	char buf[80];

	sel = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 );
	SendMessageA( lssd_list, LB_RESETCONTENT, 0, 0 );

	SendMessageA( lssd_list, LB_ADDSTRING, 0, (LPARAM)"Layout" );

	for ( i = 0; i < lswnd_layout.numComponents; i++ ) {
		if ( !lswnd_layout.components[i].s.enabled )
			Com_sprintf( buf, sizeof( buf ), "%s*", lsCompTypeLabels[lswnd_layout.components[i].type] );
		else
			Q_strncpyz( buf, lsCompTypeLabels[lswnd_layout.components[i].type], sizeof( buf ) );
		SendMessageA( lssd_list, LB_ADDSTRING, 0, (LPARAM)buf );
	}
	if ( sel >= 0 && sel <= lswnd_layout.numComponents )
		SendMessageA( lssd_list, LB_SETCURSEL, sel, 0 );
}

/* ---- Apply live preview ---- */
static void LSSD_ApplyLive( void );

/* ---- Save panel -> layout ---- */
static void LSSD_SavePanel( void ) {
	int sel, ci;
	lsCompSettings_t *s;
	lsInfoSettings_t *inf;
	HWND h;

	if ( lssd_skipNotify ) return;
	sel = lssd_lastSel;
	if ( sel < 0 ) return;

	/* Layout globals */
	if ( sel == 0 ) {
		h = GetDlgItem( lssd_panel, IDC_G_FONT );
		if ( h ) GetWindowTextA( h, lswnd_layout.globalFont, sizeof( lswnd_layout.globalFont ) );
		h = GetDlgItem( lssd_panel, IDC_G_FONTSIZE );
		if ( h ) lswnd_layout.globalFontSize = GetDlgItemInt( lssd_panel, IDC_G_FONTSIZE, NULL, FALSE );
		h = GetDlgItem( lssd_panel, IDC_G_WIDTH );
		if ( h ) {
			int w = GetDlgItemInt( lssd_panel, IDC_G_WIDTH, NULL, FALSE );
			lswnd_layout.windowWidth = ( w < 100 ) ? 100 : ( w > 800 ) ? 800 : w;
			/* apply width live */
			if ( lswnd_hwnd && IsWindow( lswnd_hwnd ) ) {
				RECT wr;
				GetWindowRect( lswnd_hwnd, &wr );
				if ( wr.right - wr.left != lswnd_layout.windowWidth ) {
					SetWindowPos( lswnd_hwnd, NULL, 0, 0,
						lswnd_layout.windowWidth, wr.bottom - wr.top,
						SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
				}
			}
		}
		h = GetDlgItem( lssd_panel, IDC_G_OPACITY );
		if ( h ) {
			int o = GetDlgItemInt( lssd_panel, IDC_G_OPACITY, NULL, FALSE );
			lswnd_layout.opacity = ( o < 0 ) ? 0 : ( o > 255 ) ? 255 : o;
		}
		lswnd_layout.textShadow = IsDlgButtonChecked( lssd_panel, IDC_G_SHADOW ) == BST_CHECKED;
		lswnd_layout.thinAccent = IsDlgButtonChecked( lssd_panel, IDC_G_THINACCENT ) == BST_CHECKED;
		lswnd_layout.globalBold = IsDlgButtonChecked( lssd_panel, IDC_G_BOLD ) == BST_CHECKED;
		lswnd_layout.lockResize = IsDlgButtonChecked( lssd_panel, IDC_G_LOCKRESIZE ) == BST_CHECKED;
		{
			int wasOnTop = lswnd_layout.alwaysOnTop;
			int wasTransp = lswnd_layout.transparent;
			lswnd_layout.alwaysOnTop = IsDlgButtonChecked( lssd_panel, IDC_G_ONTOP ) == BST_CHECKED;
			lswnd_layout.transparent = IsDlgButtonChecked( lssd_panel, IDC_G_TRANSP ) == BST_CHECKED;
			if ( lswnd_layout.alwaysOnTop != wasOnTop || lswnd_layout.transparent != wasTransp )
				LSWND_ApplyWindowStyle();
		}
		h = GetDlgItem( lssd_panel, IDC_G_COMPARE );
		if ( h ) {
			int ci2 = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
			if ( ci2 >= 0 && ci2 <= 2 ) lswnd_layout.compareAgainst = ci2;
		}
		lswnd_layout.flipLayout = IsDlgButtonChecked( lssd_panel, IDC_G_FLIP ) == BST_CHECKED;
		h = GetDlgItem( lssd_panel, IDC_G_SPACING );
		if ( h ) {
			int sp = GetDlgItemInt( lssd_panel, IDC_G_SPACING, NULL, FALSE );
			lswnd_layout.componentSpacing = ( sp < 0 ) ? 0 : ( sp > 20 ) ? 20 : sp;
		}
		LSSD_ApplyLive();
		return;
	}

	ci = sel - 1;
	if ( ci < 0 || ci >= lswnd_layout.numComponents ) return;
	s = &lswnd_layout.components[ci].s;

	/* common */
	{
		int wasEnabled = s->enabled;
		s->enabled = IsDlgButtonChecked( lssd_panel, IDC_S_ENABLED ) == BST_CHECKED;
		if ( s->enabled != wasEnabled ) LSSD_RefreshList();
	}
	h = GetDlgItem( lssd_panel, IDC_S_FONT );
	if ( h ) GetWindowTextA( h, s->font, sizeof( s->font ) );
	h = GetDlgItem( lssd_panel, IDC_S_FONTSIZE );
	if ( h ) s->fontSize = GetDlgItemInt( lssd_panel, IDC_S_FONTSIZE, NULL, FALSE );
	h = GetDlgItem( lssd_panel, IDC_S_PADH );
	if ( h ) s->padH = GetDlgItemInt( lssd_panel, IDC_S_PADH, NULL, TRUE );
	h = GetDlgItem( lssd_panel, IDC_S_PADV );
	if ( h ) s->padV = GetDlgItemInt( lssd_panel, IDC_S_PADV, NULL, TRUE );
	h = GetDlgItem( lssd_panel, IDC_S_OVERRIDEH );
	if ( h ) s->overrideHeight = GetDlgItemInt( lssd_panel, IDC_S_OVERRIDEH, NULL, FALSE );
	h = GetDlgItem( lssd_panel, IDC_S_ALIGN );
	if ( h ) s->alignment = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
	s->bold = IsDlgButtonChecked( lssd_panel, IDC_S_BOLD ) == BST_CHECKED;

	/* type-specific */
	switch ( lswnd_layout.components[ci].type ) {
	case LSCOMP_TITLE:
		s->u.title.showGameName = IsDlgButtonChecked( lssd_panel, IDC_T_GAMENAME ) == BST_CHECKED;
		s->u.title.showCategory = IsDlgButtonChecked( lssd_panel, IDC_T_CATEGORY ) == BST_CHECKED;
		s->u.title.showAttempts = IsDlgButtonChecked( lssd_panel, IDC_T_ATTEMPTS ) == BST_CHECKED;
		s->u.title.attemptsOnNewLine = IsDlgButtonChecked( lssd_panel, IDC_T_ATTNEWLINE ) == BST_CHECKED;
		s->u.title.showTopAccent = IsDlgButtonChecked( lssd_panel, IDC_T_TOPACCENT ) == BST_CHECKED;
		s->u.title.showBottomAccent = IsDlgButtonChecked( lssd_panel, IDC_T_BOTACCENT ) == BST_CHECKED;
		break;
	case LSCOMP_SPLITS:
		s->u.splits.visibleSplits = GetDlgItemInt( lssd_panel, IDC_SP_VISIBLE, NULL, FALSE );
		if ( s->u.splits.visibleSplits < 1 ) s->u.splits.visibleSplits = 1;
		s->u.splits.showThinSeps = IsDlgButtonChecked( lssd_panel, IDC_SP_THINSEPS ) == BST_CHECKED;
		s->u.splits.alwaysShowLast = IsDlgButtonChecked( lssd_panel, IDC_SP_SHOWLAST ) == BST_CHECKED;
		s->u.splits.lockLastToBottom = IsDlgButtonChecked( lssd_panel, IDC_SP_LOCKLAST ) == BST_CHECKED;
		s->u.splits.sepBeforeLastSplit = IsDlgButtonChecked( lssd_panel, IDC_SP_SEPLAST ) == BST_CHECKED;
		h = GetDlgItem( lssd_panel, IDC_SP_DELTAACC );
		if ( h ) s->u.splits.deltaAccuracy = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
		s->u.splits.deltaDropDecimals = IsDlgButtonChecked( lssd_panel, IDC_SP_DELTADROP ) == BST_CHECKED;
		h = GetDlgItem( lssd_panel, IDC_SP_SPLITACC );
		if ( h ) s->u.splits.splitAccuracy = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
		s->u.splits.deltaCountdownSec = GetDlgItemInt( lssd_panel, IDC_SP_DELTACNT, NULL, FALSE );
		h = GetDlgItem( lssd_panel, IDC_SP_COMPARE );
		if ( h ) s->u.splits.compareAgainst = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 ) - 1;
		s->u.splits.showHeader = IsDlgButtonChecked( lssd_panel, IDC_SP_HEADER ) == BST_CHECKED;
		s->u.splits.alternateRows = IsDlgButtonChecked( lssd_panel, IDC_SP_ALTROWS ) == BST_CHECKED;
		s->u.splits.shortNames = IsDlgButtonChecked( lssd_panel, IDC_SP_SHORTNAMES ) == BST_CHECKED;
		/* save inline column data */
		{
			int cidx;
			for ( cidx = 0; cidx < s->u.splits.numColumns; cidx++ ) {
				int nameId  = IDC_COL_BASE + cidx * IDC_COL_STRIDE;
				int typeId  = IDC_COL_BASE + cidx * IDC_COL_STRIDE + 1;
				int fontId  = IDC_COL_BASE + cidx * IDC_COL_STRIDE + 7;
				int fsizeId = IDC_COL_BASE + cidx * IDC_COL_STRIDE + 8;
				int boldId  = IDC_COL_BASE + cidx * IDC_COL_STRIDE + 9;
				int widthId = IDC_COL_BASE + cidx * IDC_COL_STRIDE + 3;
				HWND hc;
				hc = GetDlgItem( lssd_panel, nameId );
				if ( hc ) {
					char lb[32];
					GetWindowTextA( hc, lb, sizeof( lb ) );
					if ( !Q_stricmp( lb, lsColTypeLabels[s->u.splits.columns[cidx]] ) )
						s->u.splits.colSettings[cidx].label[0] = '\0';
					else
						Q_strncpyz( s->u.splits.colSettings[cidx].label, lb, sizeof( s->u.splits.colSettings[cidx].label ) );
				}
				hc = GetDlgItem( lssd_panel, typeId );
				if ( hc ) {
					int tsel = (int)SendMessageA( hc, CB_GETCURSEL, 0, 0 );
					if ( tsel >= 0 && tsel < LSCOL_TYPE_COUNT )
						s->u.splits.columns[cidx] = tsel;
				}
				/* per-column font/size/bold/width */
				hc = GetDlgItem( lssd_panel, fontId );
				if ( hc ) GetWindowTextA( hc, s->u.splits.colSettings[cidx].font, sizeof( s->u.splits.colSettings[cidx].font ) );
				hc = GetDlgItem( lssd_panel, fsizeId );
				if ( hc ) s->u.splits.colSettings[cidx].fontSize = GetDlgItemInt( lssd_panel, fsizeId, NULL, FALSE );
				if ( GetDlgItem( lssd_panel, boldId ) )
					s->u.splits.colSettings[cidx].bold = ( IsDlgButtonChecked( lssd_panel, boldId ) == BST_CHECKED ) ? 1 : 0;
				hc = GetDlgItem( lssd_panel, widthId );
				if ( hc ) s->u.splits.colSettings[cidx].width = GetDlgItemInt( lssd_panel, widthId, NULL, FALSE );
			}
		}
		/* save per-state name styling */
		{
			HWND hc;
			hc = GetDlgItem( lssd_panel, IDC_SPN_BFONT );
			if ( hc ) GetWindowTextA( hc, s->u.splits.beforeNameFont, sizeof( s->u.splits.beforeNameFont ) );
			s->u.splits.beforeNameSize = GetDlgItemInt( lssd_panel, IDC_SPN_BFSIZE, NULL, FALSE );
			s->u.splits.beforeNameBold = ( IsDlgButtonChecked( lssd_panel, IDC_SPN_BBOLD ) == BST_CHECKED ) ? 1 : 0;
			hc = GetDlgItem( lssd_panel, IDC_SPN_CFONT );
			if ( hc ) GetWindowTextA( hc, s->u.splits.currentNameFont, sizeof( s->u.splits.currentNameFont ) );
			s->u.splits.currentNameSize = GetDlgItemInt( lssd_panel, IDC_SPN_CFSIZE, NULL, FALSE );
			s->u.splits.currentNameBold = ( IsDlgButtonChecked( lssd_panel, IDC_SPN_CBOLD ) == BST_CHECKED ) ? 1 : 0;
			hc = GetDlgItem( lssd_panel, IDC_SPN_AFONT );
			if ( hc ) GetWindowTextA( hc, s->u.splits.afterNameFont, sizeof( s->u.splits.afterNameFont ) );
			s->u.splits.afterNameSize = GetDlgItemInt( lssd_panel, IDC_SPN_AFSIZE, NULL, FALSE );
			s->u.splits.afterNameBold = ( IsDlgButtonChecked( lssd_panel, IDC_SPN_ABOLD ) == BST_CHECKED ) ? 1 : 0;
		}
		break;
	case LSCOMP_TIMER:
		s->u.timer.decimals = GetDlgItemInt( lssd_panel, IDC_TM_DECIMALS, NULL, FALSE );
		h = GetDlgItem( lssd_panel, IDC_TM_METHOD );
		if ( h ) s->u.timer.timingMethod = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
		s->u.timer.colorOnDeltaPlus = IsDlgButtonChecked( lssd_panel, IDC_TM_DELTACLR ) == BST_CHECKED;
		h = GetDlgItem( lssd_panel, IDC_TM_COMPARE );
		if ( h ) s->u.timer.compareAgainst = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 ) - 1;
		break;
	case LSCOMP_DETAILED_TIMER:
		s->u.detailedTimer.mainDecimals = GetDlgItemInt( lssd_panel, IDC_DT_MDEC, NULL, FALSE );
		s->u.detailedTimer.compDecimals = GetDlgItemInt( lssd_panel, IDC_DT_CDEC, NULL, FALSE );
		h = GetDlgItem( lssd_panel, IDC_DT_METHOD );
		if ( h ) s->u.detailedTimer.timingMethod = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
		s->u.detailedTimer.showPB = IsDlgButtonChecked( lssd_panel, IDC_DT_SHOWPB ) == BST_CHECKED;
		s->u.detailedTimer.showBest = IsDlgButtonChecked( lssd_panel, IDC_DT_SHOWBEST ) == BST_CHECKED;
		s->u.detailedTimer.showSegTimer = IsDlgButtonChecked( lssd_panel, IDC_DT_SEGNAME ) == BST_CHECKED;
		s->u.detailedTimer.mainFontSize = GetDlgItemInt( lssd_panel, IDC_DT_MFSIZE, NULL, FALSE );
		s->u.detailedTimer.compFontSize = GetDlgItemInt( lssd_panel, IDC_DT_CFSIZE, NULL, FALSE );
		s->u.detailedTimer.segFontSize = GetDlgItemInt( lssd_panel, IDC_DT_SFSIZE, NULL, FALSE );
		s->u.detailedTimer.colorOnDeltaPlus = IsDlgButtonChecked( lssd_panel, IDC_DT_DELTACLR ) == BST_CHECKED;
		h = GetDlgItem( lssd_panel, IDC_DT_COMPARE );
		if ( h ) s->u.detailedTimer.compareAgainst = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 ) - 1;
		break;
	case LSCOMP_SEG_TIMER:
		s->u.segTimer.decimals = GetDlgItemInt( lssd_panel, IDC_ST_DECIMALS, NULL, FALSE );
		h = GetDlgItem( lssd_panel, IDC_ST_METHOD );
		if ( h ) s->u.segTimer.timingMethod = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
		s->u.segTimer.colorOnDeltaPlus = IsDlgButtonChecked( lssd_panel, IDC_ST_DELTACLR ) == BST_CHECKED;
		h = GetDlgItem( lssd_panel, IDC_ST_COMPARE );
		if ( h ) s->u.segTimer.compareAgainst = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 ) - 1;
		break;
	case LSCOMP_REAL_TIME:
		s->u.realTimer.decimals = GetDlgItemInt( lssd_panel, IDC_RT_DECIMALS, NULL, FALSE );
		break;
	case LSCOMP_GHOST_SEG_TIME:
		/* ghost seg timer has no editable decimals panel controls for now */
		break;
	case LSCOMP_100PCT:
		s->u.pct.showTotal   = IsDlgButtonChecked( lssd_panel, IDC_PCT_TOTAL ) == BST_CHECKED;
		s->u.pct.showSegment = IsDlgButtonChecked( lssd_panel, IDC_PCT_SEGMENT ) == BST_CHECKED;
		break;
	case LSCOMP_BEST_SEGMENTS:
		s->u.bestSegments.showSegments = IsDlgButtonChecked( lssd_panel, IDC_BG_SHOWSEGS ) == BST_CHECKED;
		break;
	case LSCOMP_PREV_SEGMENT:
	case LSCOMP_SUM_OF_BEST:
	case LSCOMP_BEST_POSSIBLE:
	case LSCOMP_POSSIBLE_SAVE:
	case LSCOMP_COMPARISON:
		inf = LSLAY_GetInfoSettings( s, lswnd_layout.components[ci].type );
		if ( inf ) {
			h = GetDlgItem( lssd_panel, IDC_IR_ACCURACY );
			if ( h ) inf->accuracy = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 );
			inf->dropDecimals = IsDlgButtonChecked( lssd_panel, IDC_IR_DROP ) == BST_CHECKED;
			inf->showLive = IsDlgButtonChecked( lssd_panel, IDC_IR_LIVE ) == BST_CHECKED;
			h = GetDlgItem( lssd_panel, IDC_IR_LABEL );
			if ( h ) GetWindowTextA( h, inf->label, sizeof( inf->label ) );
			h = GetDlgItem( lssd_panel, IDC_IR_COMPARE );
			if ( h ) inf->compareAgainst = (int)SendMessageA( h, CB_GETCURSEL, 0, 0 ) - 1;
		}
		break;
	case LSCOMP_SEPARATOR:
		s->u.separator.height = GetDlgItemInt( lssd_panel, IDC_SEP_HEIGHT, NULL, FALSE );
		break;
	case LSCOMP_TEXT:
		h = GetDlgItem( lssd_panel, IDC_TX_TEXT );
		if ( h ) GetWindowTextA( h, s->u.text.text, sizeof( s->u.text.text ) );
		break;
	case LSCOMP_BLANK_SPACE:
		h = GetDlgItem( lssd_panel, IDC_BS_HEIGHT );
		if ( h ) s->u.blankSpace.height = GetDlgItemInt( lssd_panel, IDC_BS_HEIGHT, NULL, FALSE );
		break;
	case LSCOMP_HEADER:
		h = GetDlgItem( lssd_panel, IDC_HD_TEXT );
		if ( h ) GetWindowTextA( h, s->u.header.text, sizeof( s->u.header.text ) );
		s->u.header.showLine = IsDlgButtonChecked( lssd_panel, IDC_HD_SHOWLINE ) == BST_CHECKED;
		break;
	default:
		break;
	}
	LSSD_ApplyLive();
}

/* ---- Build panel for selected item ---- */
static void LSSD_BuildPanel( void ) {
	static const char *fmtLabels[] = { "Seconds", "Tenths", "Hundredths", "Milliseconds", "Minutes" };
	static const char *alignLabels[] = { "Left", "Center", "Right" };
	static const char *methodLabels[] = { "Game Time", "In-Game Time" };
	int sel, ci;
	lsCompSettings_t *s;
	lsCompType_t type;
	lsInfoSettings_t *inf;

	if ( lssd_panel ) SendMessageA( lssd_panel, WM_SETREDRAW, FALSE, 0 );

	LSSD_ClearPanel();
	sel = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 );
	if ( sel < 0 ) {
		if ( lssd_panel ) {
			SendMessageA( lssd_panel, WM_SETREDRAW, TRUE, 0 );
			RedrawWindow( lssd_panel, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN );
		}
		return;
	}
	lssd_lastSel = sel;

	lssd_skipNotify = 1;

	/* ---- Layout globals ---- */
	if ( sel == 0 ) {
		LSSD_GroupBegin( "Font" );
		LSSD_Label( "Global Font:", LSSD_RX, LSSD_S(90) );
		LSSD_EditStr( IDC_G_FONT, lswnd_layout.globalFont, LSSD_RX + LSSD_S(95), LSSD_S(200) );
		LSSD_AddCtrl( "BUTTON", "Choose...", BS_PUSHBUTTON, LSSD_RX + LSSD_S(300), lssd_py, LSSD_S(60), LSSD_S(20), IDC_G_FONTBTN );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Font Size:", LSSD_RX, LSSD_S(90) );
		LSSD_EditNum( IDC_G_FONTSIZE, lswnd_layout.globalFontSize, LSSD_RX + LSSD_S(95), LSSD_S(50) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_G_BOLD, "Bold Font (Global)", lswnd_layout.globalBold );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Window" );
		LSSD_Check( IDC_G_ONTOP, "Always On Top", lswnd_layout.alwaysOnTop );
		LSSD_Check( IDC_G_TRANSP, "Transparent (Per-Pixel Alpha)", lswnd_layout.transparent );
		LSSD_Label( "Window Width:", LSSD_RX, LSSD_S(95) );
		LSSD_EditNum( IDC_G_WIDTH, lswnd_layout.windowWidth > 0 ? lswnd_layout.windowWidth : 250, LSSD_RX + LSSD_S(100), LSSD_S(50) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Opacity (0-255):", LSSD_RX, LSSD_S(105) );
		LSSD_EditNum( IDC_G_OPACITY, lswnd_layout.opacity > 0 ? lswnd_layout.opacity : 255, LSSD_RX + LSSD_S(110), LSSD_S(50) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_G_LOCKRESIZE, "Lock Window Size", lswnd_layout.lockResize );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Layout" );
		{
			static const char *gcmpLabels[] = { "Personal Best", "Best Segments", "Average" };
			LSSD_Label( "Compare:", LSSD_RX, LSSD_S(60) );
			LSSD_Combo( IDC_G_COMPARE, gcmpLabels, 3, lswnd_layout.compareAgainst, LSSD_RX + LSSD_S(65), LSSD_S(120) );
			lssd_py += LSSD_S(24);
		}
		LSSD_Check( IDC_G_FLIP, "Flip Layout (Bottom-to-Top)", lswnd_layout.flipLayout );
		LSSD_Label( "Component Spacing:", LSSD_RX, LSSD_S(120) );
		LSSD_EditNum( IDC_G_SPACING, lswnd_layout.componentSpacing, LSSD_RX + LSSD_S(125), LSSD_S(50) );
		lssd_py += LSSD_S(24);
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Visual Effects" );
		LSSD_Check( IDC_G_SHADOW, "Text Shadow", lswnd_layout.textShadow );
		LSSD_Check( IDC_G_THINACCENT, "Accent Lines Between Components", lswnd_layout.thinAccent );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Background:", IDC_G_BGCLR, &lswnd_layout.bgColor );
		LSSD_Row( "Header BG:", IDC_G_HDRBG, &lswnd_layout.headerBg );
		LSSD_Row( "Accent:", IDC_G_ACCENT, &lswnd_layout.accentColor );
		LSSD_Row( "Text:", IDC_G_TEXTCLR, &lswnd_layout.textColor );
		LSSD_Row( "Ahead:", IDC_G_AHEAD, &lswnd_layout.aheadColor );
		LSSD_Row( "Behind:", IDC_G_BEHIND, &lswnd_layout.behindColor );
		LSSD_Row( "Gold:", IDC_G_GOLD, &lswnd_layout.goldColor );
		LSSD_GroupEnd();
		lssd_skipNotify = 0;
		goto buildpanel_scroll;
	}

	/* ---- Component ---- */
	ci = sel - 1;
	if ( ci < 0 || ci >= lswnd_layout.numComponents ) { lssd_skipNotify = 0; goto buildpanel_scroll; }
	s    = &lswnd_layout.components[ci].s;
	type = lswnd_layout.components[ci].type;

	/* common: enabled */
	LSSD_GroupBegin( "General" );
	LSSD_Check( IDC_S_ENABLED, "Enabled", s->enabled );
	LSSD_GroupEnd();

	if ( type != LSCOMP_SEPARATOR && type != LSCOMP_BLANK_SPACE ) {
		int hasAlign = ( type == LSCOMP_TITLE || type == LSCOMP_TEXT
			|| type == LSCOMP_SEG_TIMER || type == LSCOMP_HEADER );
		LSSD_GroupBegin( "Font & Layout" );
		LSSD_Label( "Font:", LSSD_RX, LSSD_S(40) );
		LSSD_EditStr( IDC_S_FONT, s->font, LSSD_RX + LSSD_S(45), LSSD_S(200) );
		LSSD_AddCtrl( "BUTTON", "Choose...", BS_PUSHBUTTON, LSSD_RX + LSSD_S(250), lssd_py, LSSD_S(60), LSSD_S(20), IDC_S_FONTBTN );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Font Size:", LSSD_RX, LSSD_S(70) );
		LSSD_EditNum( IDC_S_FONTSIZE, s->fontSize, LSSD_RX + LSSD_S(75), LSSD_S(50) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_S_BOLD, "Bold", s->bold );
		LSSD_Label( "Pad H:", LSSD_RX, LSSD_S(50) );
		LSSD_EditSigned( IDC_S_PADH, s->padH, LSSD_RX + LSSD_S(55), LSSD_S(40) );
		LSSD_Label( "Pad V:", LSSD_RX + LSSD_S(110), LSSD_S(50) );
		LSSD_EditSigned( IDC_S_PADV, s->padV, LSSD_RX + LSSD_S(165), LSSD_S(40) );
		LSSD_Label( "(-1 = default)", LSSD_RX + LSSD_S(215), LSSD_S(100) );
		lssd_py += LSSD_S(24);
		if ( type != LSCOMP_SPLITS ) {
			LSSD_Label( "Height:", LSSD_RX, LSSD_S(50) );
			LSSD_EditNum( IDC_S_OVERRIDEH, s->overrideHeight, LSSD_RX + LSSD_S(55), LSSD_S(40) );
			lssd_py += LSSD_S(24);
		}
		if ( hasAlign ) {
			LSSD_Label( "Alignment:", LSSD_RX, LSSD_S(70) );
			LSSD_Combo( IDC_S_ALIGN, alignLabels, 3, s->alignment, LSSD_RX + LSSD_S(75), LSSD_S(90) );
			lssd_py += LSSD_S(24);
		}
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Base Colors" );
		LSSD_Row( "Text Color:", IDC_S_TEXTCLR, &s->textColor );
		LSSD_Row( "Background:", IDC_S_BGCLR, &s->bgColor );
		LSSD_GroupEnd();
	} else if ( type == LSCOMP_BLANK_SPACE ) {
		LSSD_GroupBegin( "Base Colors" );
		LSSD_Row( "Background:", IDC_S_BGCLR, &s->bgColor );
		LSSD_GroupEnd();
	}

	/* ---- type-specific ---- */
	switch ( type ) {
	case LSCOMP_TITLE:
		LSSD_GroupBegin( "Display" );
		LSSD_Check( IDC_T_GAMENAME, "Show Game Name", s->u.title.showGameName );
		LSSD_Check( IDC_T_CATEGORY, "Show Category", s->u.title.showCategory );
		LSSD_Check( IDC_T_ATTEMPTS, "Show Attempts", s->u.title.showAttempts );
		LSSD_Check( IDC_T_ATTNEWLINE, "Attempts On New Line", s->u.title.attemptsOnNewLine );
		LSSD_GroupEnd();
		LSSD_GroupBegin( "Separators" );
		LSSD_Check( IDC_T_TOPACCENT, "Show Top Accent Line", s->u.title.showTopAccent );
		LSSD_Check( IDC_T_BOTACCENT, "Show Bottom Accent Line", s->u.title.showBottomAccent );
		LSSD_GroupEnd();
		break;

	case LSCOMP_SPLITS:
		LSSD_GroupBegin( "Display" );
		LSSD_Label( "Visible Splits:", LSSD_RX, LSSD_S(100) );
		LSSD_EditNum( IDC_SP_VISIBLE, s->u.splits.visibleSplits, LSSD_RX + LSSD_S(105), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_SP_THINSEPS, "Thin Separators", s->u.splits.showThinSeps );
		LSSD_Check( IDC_SP_SHOWLAST, "Always Show Last Split", s->u.splits.alwaysShowLast );
		LSSD_Check( IDC_SP_LOCKLAST, "Lock Last to Bottom", s->u.splits.lockLastToBottom );
		LSSD_Check( IDC_SP_SEPLAST, "Separator Before Last", s->u.splits.sepBeforeLastSplit );
		LSSD_Check( IDC_SP_ALTROWS, "Alternate Row Shading", s->u.splits.alternateRows );
		LSSD_Check( IDC_SP_SHORTNAMES, "Short Names", s->u.splits.shortNames );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Format" );
		LSSD_Label( "Delta Format:", LSSD_RX, LSSD_S(100) );
		LSSD_Combo( IDC_SP_DELTAACC, fmtLabels, 5, s->u.splits.deltaAccuracy, LSSD_RX + LSSD_S(105), LSSD_S(110) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_SP_DELTADROP, "Drop Decimals > 1 min", s->u.splits.deltaDropDecimals );
		LSSD_Label( "Split Format:", LSSD_RX, LSSD_S(100) );
		LSSD_Combo( IDC_SP_SPLITACC, fmtLabels, 5, s->u.splits.splitAccuracy, LSSD_RX + LSSD_S(105), LSSD_S(110) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Delta Countdown:", LSSD_RX, LSSD_S(105) );
		LSSD_EditNum( IDC_SP_DELTACNT, s->u.splits.deltaCountdownSec, LSSD_RX + LSSD_S(110), LSSD_S(40) );
		LSSD_Label( "sec (0=off)", LSSD_RX + LSSD_S(155), LSSD_S(80) );
		lssd_py += LSSD_S(24);
		{
			static const char *cmpLabels[] = { "Global", "Personal Best", "Best Segments", "Average" };
			LSSD_Label( "Compare:", LSSD_RX, LSSD_S(65) );
			LSSD_Combo( IDC_SP_COMPARE, cmpLabels, 4, s->u.splits.compareAgainst + 1, LSSD_RX + LSSD_S(70), LSSD_S(120) );
			lssd_py += LSSD_S(24);
		}
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Columns" );
		LSSD_Check( IDC_SP_HEADER, "Show Column Labels", s->u.splits.showHeader );
		LSSD_Label( "Labels Color:", LSSD_RX, LSSD_S(85) );
		LSSD_ClrBtn( IDC_SP_CLR_HDR, &s->u.splits.headerTextColor, LSSD_RX + LSSD_S(90), LSSD_S(50) );
		LSSD_AddCtrl( "BUTTON", "Add Column", BS_PUSHBUTTON,
			LSSD_RX + LSSD_S(280), lssd_py, LSSD_S(90), LSSD_S(22), IDC_SP_COLADD );
		lssd_py += LSSD_S(28);
		{
			int ci2;
			for ( ci2 = 0; ci2 < s->u.splits.numColumns; ci2++ ) {
				int base = IDC_COL_BASE + ci2 * IDC_COL_STRIDE;
				const char *defName = lsColTypeLabels[s->u.splits.columns[ci2]];
				/* column sub-header */
				{
					char hdr[64];
					Com_sprintf( hdr, sizeof( hdr ), "Column: %s",
						s->u.splits.colSettings[ci2].label[0] ? s->u.splits.colSettings[ci2].label : defName );
					LSSD_SectionBegin( hdr );
				}
				/* Name */
				LSSD_Label( "Name:", LSSD_RX, LSSD_S(55) );
				LSSD_EditStr( base,
					s->u.splits.colSettings[ci2].label[0] ? s->u.splits.colSettings[ci2].label : defName,
					LSSD_RX + LSSD_S(90), LSSD_S(200) );
				lssd_py += LSSD_S(26);
				/* Column Type */
				LSSD_Label( "Column Type:", LSSD_RX, LSSD_S(85) );
				LSSD_Combo( base + 1, lsColTypeLabels, LSCOL_TYPE_COUNT,
					s->u.splits.columns[ci2], LSSD_RX + LSSD_S(90), LSSD_S(140) );
				lssd_py += LSSD_S(26);
				/* Text Color */
				LSSD_Label( "Text Color:", LSSD_RX, LSSD_S(85) );
				LSSD_ClrBtn( base + 2, &s->u.splits.colSettings[ci2].textColor,
					LSSD_RX + LSSD_S(90), LSSD_S(80) );
				lssd_py += LSSD_S(26);
				/* Font override */
				LSSD_Label( "Font:", LSSD_RX, LSSD_S(40) );
				LSSD_EditStr( base + 7, s->u.splits.colSettings[ci2].font, LSSD_RX + LSSD_S(45), LSSD_S(140) );
				LSSD_AddCtrl( "BUTTON", "Choose...", BS_PUSHBUTTON, LSSD_RX + LSSD_S(190), lssd_py, LSSD_S(60), LSSD_S(20), base + 10 );
				lssd_py += LSSD_S(26);
				LSSD_Label( "Size:", LSSD_RX, LSSD_S(40) );
				LSSD_EditNum( base + 8, s->u.splits.colSettings[ci2].fontSize, LSSD_RX + LSSD_S(45), LSSD_S(40) );
				{
					int boldVal = s->u.splits.colSettings[ci2].bold;
					LSSD_Check( base + 9, "Bold", boldVal > 0 ? 1 : 0 );
				}
				/* Per-column state colors */
				LSSD_Label( "Before Color:", LSSD_RX, LSSD_S(85) );
				LSSD_ClrBtn( base + 11, &s->u.splits.colSettings[ci2].beforeColor,
					LSSD_RX + LSSD_S(90), LSSD_S(80) );
				lssd_py += LSSD_S(26);
				LSSD_Label( "Current Color:", LSSD_RX, LSSD_S(85) );
				LSSD_ClrBtn( base + 12, &s->u.splits.colSettings[ci2].currentColor,
					LSSD_RX + LSSD_S(90), LSSD_S(80) );
				lssd_py += LSSD_S(26);
				LSSD_Label( "After Color:", LSSD_RX, LSSD_S(85) );
				LSSD_ClrBtn( base + 13, &s->u.splits.colSettings[ci2].afterColor,
					LSSD_RX + LSSD_S(90), LSSD_S(80) );
				lssd_py += LSSD_S(26);
				/* Width override */
				LSSD_Label( "Width:", LSSD_RX, LSSD_S(45) );
				LSSD_EditNum( base + 3, s->u.splits.colSettings[ci2].width, LSSD_RX + LSSD_S(50), LSSD_S(40) );
				lssd_py += LSSD_S(26);
				/* Buttons */
				LSSD_AddCtrl( "BUTTON", "Move Up", BS_PUSHBUTTON,
					LSSD_RX, lssd_py, LSSD_S(70), LSSD_S(22), base + 4 );
				LSSD_AddCtrl( "BUTTON", "Move Down", BS_PUSHBUTTON,
					LSSD_RX + LSSD_S(76), lssd_py, LSSD_S(78), LSSD_S(22), base + 5 );
				LSSD_AddCtrl( "BUTTON", "Remove Column", BS_PUSHBUTTON,
					LSSD_RX + LSSD_S(160), lssd_py, LSSD_S(105), LSSD_S(22), base + 6 );
				lssd_py += LSSD_S(28);
			}
		}
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Row Color:", IDC_SP_CLR_ROW, &s->u.splits.rowColor );
		LSSD_Row( "Alt Row Color:", IDC_SP_CLR_ALT, &s->u.splits.altRowColor );
		LSSD_Row( "Row Separator:", IDC_SP_CLR_SEP, &s->u.splits.rowSepColor );
		LSSD_Row( "Current BG:", IDC_SP_CLR_CUR, &s->u.splits.currentBg );
		LSSD_Row( "Before Current:", IDC_SP_CLR_BCUR, &s->u.splits.beforeCurrentBg );
		LSSD_Row( "Current Text:", IDC_SP_CLR_CSPL, &s->u.splits.currentSplitClr );
		LSSD_Row( "After Current:", IDC_SP_CLR_ACUR, &s->u.splits.afterCurrentBg );
		LSSD_Row( "Live Delta:", IDC_SP_CLR_LDLT, &s->u.splits.liveDeltaColor );
		LSSD_Row( "Split Time:", IDC_SP_CLR_STIM, &s->u.splits.splitTimeColor );
		LSSD_Row( "Before Split:", IDC_SP_CLR_BSPL, &s->u.splits.beforeSplitColor );
		LSSD_Row( "Split Name:", IDC_SP_CLR_NAME, &s->u.splits.nameColor );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Completed Splits Name Style" );
		LSSD_Label( "Font:", LSSD_RX, LSSD_S(40) );
		LSSD_EditStr( IDC_SPN_BFONT, s->u.splits.beforeNameFont, LSSD_RX + LSSD_S(45), LSSD_S(160) );
		LSSD_AddCtrl( "BUTTON", "Choose...", BS_PUSHBUTTON, LSSD_RX + LSSD_S(210), lssd_py, LSSD_S(60), LSSD_S(20), IDC_SPN_BFONTBTN );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Size:", LSSD_RX, LSSD_S(40) );
		LSSD_EditNum( IDC_SPN_BFSIZE, s->u.splits.beforeNameSize, LSSD_RX + LSSD_S(45), LSSD_S(40) );
		LSSD_Check( IDC_SPN_BBOLD, "Bold", s->u.splits.beforeNameBold > 0 ? 1 : 0 );
		LSSD_Row( "Name Color:", IDC_SPN_BCLR, &s->u.splits.beforeNameColor );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Current Split Name Style" );
		LSSD_Label( "Font:", LSSD_RX, LSSD_S(40) );
		LSSD_EditStr( IDC_SPN_CFONT, s->u.splits.currentNameFont, LSSD_RX + LSSD_S(45), LSSD_S(160) );
		LSSD_AddCtrl( "BUTTON", "Choose...", BS_PUSHBUTTON, LSSD_RX + LSSD_S(210), lssd_py, LSSD_S(60), LSSD_S(20), IDC_SPN_CFONTBTN );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Size:", LSSD_RX, LSSD_S(40) );
		LSSD_EditNum( IDC_SPN_CFSIZE, s->u.splits.currentNameSize, LSSD_RX + LSSD_S(45), LSSD_S(40) );
		LSSD_Check( IDC_SPN_CBOLD, "Bold", s->u.splits.currentNameBold > 0 ? 1 : 0 );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Upcoming Splits Name Style" );
		LSSD_Label( "Font:", LSSD_RX, LSSD_S(40) );
		LSSD_EditStr( IDC_SPN_AFONT, s->u.splits.afterNameFont, LSSD_RX + LSSD_S(45), LSSD_S(160) );
		LSSD_AddCtrl( "BUTTON", "Choose...", BS_PUSHBUTTON, LSSD_RX + LSSD_S(210), lssd_py, LSSD_S(60), LSSD_S(20), IDC_SPN_AFONTBTN );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Size:", LSSD_RX, LSSD_S(40) );
		LSSD_EditNum( IDC_SPN_AFSIZE, s->u.splits.afterNameSize, LSSD_RX + LSSD_S(45), LSSD_S(40) );
		LSSD_Check( IDC_SPN_ABOLD, "Bold", s->u.splits.afterNameBold > 0 ? 1 : 0 );
		LSSD_Row( "Name Color:", IDC_SPN_ACLR, &s->u.splits.afterNameColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_TIMER:
		LSSD_GroupBegin( "Timer" );
		LSSD_Label( "Decimals:", LSSD_RX, LSSD_S(70) );
		LSSD_EditNum( IDC_TM_DECIMALS, s->u.timer.decimals, LSSD_RX + LSSD_S(75), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Timing:", LSSD_RX, LSSD_S(60) );
		LSSD_Combo( IDC_TM_METHOD, methodLabels, 2, s->u.timer.timingMethod, LSSD_RX + LSSD_S(65), LSSD_S(120) );
		lssd_py += LSSD_S(28);
		LSSD_Check( IDC_TM_DELTACLR, "Color On Delta+", s->u.timer.colorOnDeltaPlus );
		{
			static const char *cmpLabels[] = { "Global", "Personal Best", "Best Segments", "Average" };
			LSSD_Label( "Compare:", LSSD_RX, LSSD_S(65) );
			LSSD_Combo( IDC_TM_COMPARE, cmpLabels, 4, s->u.timer.compareAgainst + 1, LSSD_RX + LSSD_S(70), LSSD_S(120) );
			lssd_py += LSSD_S(24);
		}
		LSSD_GroupEnd();
		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Ahead Color:", IDC_TM_AHEAD, &s->u.timer.aheadColor );
		LSSD_Row( "Behind Color:", IDC_TM_BEHIND, &s->u.timer.behindColor );
		LSSD_Row( "Gold Color:", IDC_TM_GOLD, &s->u.timer.goldColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_DETAILED_TIMER:
		LSSD_GroupBegin( "Timer" );
		LSSD_Label( "Main Decimals:", LSSD_RX, LSSD_S(100) );
		LSSD_EditNum( IDC_DT_MDEC, s->u.detailedTimer.mainDecimals, LSSD_RX + LSSD_S(105), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Comp Decimals:", LSSD_RX, LSSD_S(100) );
		LSSD_EditNum( IDC_DT_CDEC, s->u.detailedTimer.compDecimals, LSSD_RX + LSSD_S(105), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Timing:", LSSD_RX, LSSD_S(60) );
		LSSD_Combo( IDC_DT_METHOD, methodLabels, 2, s->u.detailedTimer.timingMethod, LSSD_RX + LSSD_S(65), LSSD_S(120) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Main Font Size:", LSSD_RX, LSSD_S(100) );
		LSSD_EditNum( IDC_DT_MFSIZE, s->u.detailedTimer.mainFontSize, LSSD_RX + LSSD_S(105), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Comp Font Size:", LSSD_RX, LSSD_S(100) );
		LSSD_EditNum( IDC_DT_CFSIZE, s->u.detailedTimer.compFontSize, LSSD_RX + LSSD_S(105), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Seg Font Size:", LSSD_RX, LSSD_S(100) );
		LSSD_EditNum( IDC_DT_SFSIZE, s->u.detailedTimer.segFontSize, LSSD_RX + LSSD_S(105), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_DT_DELTACLR, "Color On Delta+", s->u.detailedTimer.colorOnDeltaPlus );
		{
			static const char *cmpLabels[] = { "Global", "Personal Best", "Best Segments", "Average" };
			LSSD_Label( "Compare:", LSSD_RX, LSSD_S(65) );
			LSSD_Combo( IDC_DT_COMPARE, cmpLabels, 4, s->u.detailedTimer.compareAgainst + 1, LSSD_RX + LSSD_S(70), LSSD_S(120) );
			lssd_py += LSSD_S(24);
		}
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Info Rows" );
		LSSD_Check( IDC_DT_SHOWPB, "Show PB", s->u.detailedTimer.showPB );
		LSSD_Check( IDC_DT_SHOWBEST, "Show Best Segment", s->u.detailedTimer.showBest );
		LSSD_Check( IDC_DT_SEGNAME, "Show Segment Timer", s->u.detailedTimer.showSegTimer );
		LSSD_GroupEnd();

		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Main Ahead:", IDC_DT_MAHEAD, &s->u.detailedTimer.mainAheadColor );
		LSSD_Row( "Main Behind:", IDC_DT_MBEHIND, &s->u.detailedTimer.mainBehindColor );
		LSSD_Row( "Main Gold:", IDC_DT_MGOLD, &s->u.detailedTimer.mainGoldColor );
		LSSD_Row( "PB Color:", IDC_DT_PBCLR, &s->u.detailedTimer.pbColor );
		LSSD_Row( "Best Color:", IDC_DT_BESTCLR, &s->u.detailedTimer.bestColor );
		LSSD_Row( "Label Color:", IDC_DT_LBLCLR, &s->u.detailedTimer.labelColor );
		LSSD_Row( "Seg Name:", IDC_DT_SEGCLR, &s->u.detailedTimer.segNameColor );
		LSSD_Row( "Seg Timer:", IDC_DT_SEGTIMERCLR, &s->u.detailedTimer.segTimerColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_SEG_TIMER:
		LSSD_GroupBegin( "Timer" );
		LSSD_Label( "Decimals:", LSSD_RX, LSSD_S(70) );
		LSSD_EditNum( IDC_ST_DECIMALS, s->u.segTimer.decimals, LSSD_RX + LSSD_S(75), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Label( "Timing:", LSSD_RX, LSSD_S(60) );
		LSSD_Combo( IDC_ST_METHOD, methodLabels, 2, s->u.segTimer.timingMethod, LSSD_RX + LSSD_S(65), LSSD_S(120) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_ST_DELTACLR, "Color On Delta+", s->u.segTimer.colorOnDeltaPlus );
		{
			static const char *cmpLabels[] = { "Global", "Personal Best", "Best Segments", "Average" };
			LSSD_Label( "Compare:", LSSD_RX, LSSD_S(65) );
			LSSD_Combo( IDC_ST_COMPARE, cmpLabels, 4, s->u.segTimer.compareAgainst + 1, LSSD_RX + LSSD_S(70), LSSD_S(120) );
			lssd_py += LSSD_S(24);
		}
		LSSD_GroupEnd();
		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Timer Color:", IDC_ST_TIMERCLR, &s->u.segTimer.timerColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_REAL_TIME:
		LSSD_GroupBegin( "Timer" );
		LSSD_Label( "Decimals:", LSSD_RX, LSSD_S(70) );
		LSSD_EditNum( IDC_RT_DECIMALS, s->u.realTimer.decimals, LSSD_RX + LSSD_S(75), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_GroupEnd();
		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Timer Color:", IDC_RT_TIMERCLR, &s->u.realTimer.timerColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_GHOST_SEG_TIME:
		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Timer Color:", IDC_RT_TIMERCLR, &s->u.ghostSegTimer.timerColor );
		LSSD_Row( "Label Color:", IDC_IR_LBLCLR, &s->u.ghostSegTimer.labelColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_100PCT:
		LSSD_GroupBegin( "Display" );
		LSSD_Check( IDC_PCT_TOTAL, "Show Totals", s->u.pct.showTotal );
		LSSD_Check( IDC_PCT_SEGMENT, "Show Per-Segment", s->u.pct.showSegment );
		LSSD_GroupEnd();
		LSSD_GroupBegin( "Colors" );
		LSSD_Row( "Label Color:", IDC_IR_LBLCLR, &s->u.pct.labelColor );
		LSSD_Row( "Value Color:", IDC_IR_VALCLR, &s->u.pct.valueColor );
		LSSD_Row( "Complete Color:", IDC_RT_TIMERCLR, &s->u.pct.completeColor );
		LSSD_GroupEnd();
		break;

	case LSCOMP_BEST_SEGMENTS:
		LSSD_GroupBegin( "Display" );
		LSSD_Check( IDC_BG_SHOWSEGS, "Show Individual Segments", s->u.bestSegments.showSegments );
		LSSD_GroupEnd();
		break;

	case LSCOMP_PREV_SEGMENT:
	case LSCOMP_SUM_OF_BEST:
	case LSCOMP_BEST_POSSIBLE:
	case LSCOMP_POSSIBLE_SAVE:
	case LSCOMP_COMPARISON:
		inf = LSLAY_GetInfoSettings( s, type );
		if ( inf ) {
			LSSD_GroupBegin( "Display" );
			LSSD_Label( "Label:", LSSD_RX, LSSD_S(40) );
			LSSD_EditStr( IDC_IR_LABEL, inf->label, LSSD_RX + LSSD_S(45), LSSD_S(150) );
			lssd_py += LSSD_S(24);
			if ( type == LSCOMP_SUM_OF_BEST || type == LSCOMP_BEST_POSSIBLE ) {
				static const char *cmpLabels[] = { "Global", "Personal Best", "Best Segments", "Average" };
				LSSD_Label( "Compare:", LSSD_RX, LSSD_S(65) );
				LSSD_Combo( IDC_IR_COMPARE, cmpLabels, 4, inf->compareAgainst + 1, LSSD_RX + LSSD_S(70), LSSD_S(120) );
				lssd_py += LSSD_S(24);
			}
			LSSD_GroupEnd();

			LSSD_GroupBegin( "Format" );
			LSSD_Label( "Format:", LSSD_RX, LSSD_S(70) );
			LSSD_Combo( IDC_IR_ACCURACY, fmtLabels, 5, inf->accuracy, LSSD_RX + LSSD_S(75), LSSD_S(110) );
			lssd_py += LSSD_S(24);
			LSSD_Check( IDC_IR_DROP, "Drop Decimals > 1 min", inf->dropDecimals );
			if ( type == LSCOMP_PREV_SEGMENT )
				LSSD_Check( IDC_IR_LIVE, "Show Live Value", inf->showLive );
			LSSD_GroupEnd();
			LSSD_GroupBegin( "Colors" );
			LSSD_Row( "Value Color:", IDC_IR_VALCLR, &inf->valueColor );
			LSSD_Row( "Label Color:", IDC_IR_LBLCLR, &inf->labelColor );
			LSSD_GroupEnd();
		}
		break;

	case LSCOMP_SEPARATOR:
		LSSD_GroupBegin( "Separator" );
		LSSD_Label( "Height:", LSSD_RX, LSSD_S(50) );
		LSSD_EditNum( IDC_SEP_HEIGHT, s->u.separator.height, LSSD_RX + LSSD_S(55), LSSD_S(40) );
		lssd_py += LSSD_S(24);
		LSSD_Row( "Color:", IDC_SEP_COLOR, &s->u.separator.color );
		LSSD_GroupEnd();
		break;

	case LSCOMP_TEXT:
		LSSD_GroupBegin( "Text" );
		LSSD_Label( "Text:", LSSD_RX, LSSD_S(40) );
		LSSD_EditStr( IDC_TX_TEXT, s->u.text.text, LSSD_RX + LSSD_S(45), LSSD_S(300) );
		lssd_py += LSSD_S(24);
		LSSD_GroupEnd();
		break;

	case LSCOMP_BLANK_SPACE:
		LSSD_GroupBegin( "Blank Space" );
		LSSD_Label( "Height (px):", LSSD_RX, LSSD_S(80) );
		LSSD_EditNum( IDC_BS_HEIGHT, s->u.blankSpace.height, LSSD_RX + LSSD_S(85), LSSD_S(50) );
		lssd_py += LSSD_S(24);
		LSSD_GroupEnd();
		break;

	case LSCOMP_HEADER:
		LSSD_GroupBegin( "Header" );
		LSSD_Label( "Text:", LSSD_RX, LSSD_S(40) );
		LSSD_EditStr( IDC_HD_TEXT, s->u.header.text, LSSD_RX + LSSD_S(45), LSSD_S(300) );
		lssd_py += LSSD_S(24);
		LSSD_Check( IDC_HD_SHOWLINE, "Show Accent Line", s->u.header.showLine );
		LSSD_GroupEnd();
		break;

	default:
		break;
	}

	lssd_skipNotify = 0;

buildpanel_scroll:
	if ( lssd_panel ) {
		SCROLLINFO si;
		si.cbSize = sizeof( si );
		si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin   = 0;
		si.nMax   = lssd_py + LSSD_S(10);
		si.nPage  = LSSD_PANEL_H;
		si.nPos   = 0;
		SetScrollInfo( lssd_panel, SB_VERT, &si, TRUE );

		SendMessageA( lssd_panel, WM_SETREDRAW, TRUE, 0 );
		RedrawWindow( lssd_panel, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN );
	}
}

/* Repaint main window live */
static void LSSD_ApplyLive( void ) {
	if ( lswnd_hwnd && IsWindow( lswnd_hwnd ) ) {
		LSWND_ApplyWindowStyle();
		InvalidateRect( lswnd_hwnd, NULL, FALSE );
	}
}

static void LSSD_OnSelChange( void ) {
	LSSD_SavePanel();
	LSSD_BuildPanel();
}

/* ---- Scrollable panel WndProc ---- */

static LRESULT CALLBACK LSSD_PanelProc( HWND hw, UINT msg, WPARAM wp, LPARAM lp ) {
	switch ( msg ) {
	case WM_COMMAND:
	case WM_DRAWITEM:
		return SendMessageA( GetParent( hw ), msg, wp, lp );

	case WM_VSCROLL: {
		int oldPos = lssd_scrollPos;
		SCROLLINFO si;
		si.cbSize = sizeof( si );
		si.fMask  = SIF_ALL;
		GetScrollInfo( hw, SB_VERT, &si );
		switch ( LOWORD( wp ) ) {
		case SB_LINEUP:      lssd_scrollPos -= LSSD_S(20); break;
		case SB_LINEDOWN:    lssd_scrollPos += LSSD_S(20); break;
		case SB_PAGEUP:      lssd_scrollPos -= (int)si.nPage; break;
		case SB_PAGEDOWN:    lssd_scrollPos += (int)si.nPage; break;
		case SB_THUMBTRACK:  lssd_scrollPos = si.nTrackPos; break;
		}
		if ( lssd_scrollPos < 0 ) lssd_scrollPos = 0;
		if ( lssd_scrollPos > si.nMax - (int)si.nPage )
			lssd_scrollPos = si.nMax - (int)si.nPage;
		if ( lssd_scrollPos < 0 ) lssd_scrollPos = 0;
		if ( lssd_scrollPos != oldPos ) {
			ScrollWindowEx( hw, 0, oldPos - lssd_scrollPos, NULL, NULL, NULL, NULL,
				SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE );
			SetScrollPos( hw, SB_VERT, lssd_scrollPos, TRUE );
		}
		return 0;
	}

	case WM_MOUSEWHEEL: {
		short delta = (short)HIWORD( wp );
		int step = LSSD_S(40);
		int oldPos = lssd_scrollPos;
		SCROLLINFO si;
		si.cbSize = sizeof( si );
		si.fMask  = SIF_ALL;
		GetScrollInfo( hw, SB_VERT, &si );
		lssd_scrollPos += ( delta > 0 ) ? -step : step;
		if ( lssd_scrollPos < 0 ) lssd_scrollPos = 0;
		if ( lssd_scrollPos > si.nMax - (int)si.nPage )
			lssd_scrollPos = si.nMax - (int)si.nPage;
		if ( lssd_scrollPos < 0 ) lssd_scrollPos = 0;
		if ( lssd_scrollPos != oldPos ) {
			ScrollWindowEx( hw, 0, oldPos - lssd_scrollPos, NULL, NULL, NULL, NULL,
				SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE );
			SetScrollPos( hw, SB_VERT, lssd_scrollPos, TRUE );
		}
		return 0;
	}

	}
	return DefWindowProcA( hw, msg, wp, lp );
}

/* ---- Dialog WndProc ---- */
static LRESULT CALLBACK LSSD_WndProc( HWND hw, UINT msg, WPARAM wp, LPARAM lp ) {
	switch ( msg ) {
	case WM_COMMAND: {
		int id  = LOWORD( wp );
		int ntf = HIWORD( wp );

		/* component list selection change -- must check before generic CBN_SELCHANGE
		   because LBN_SELCHANGE == CBN_SELCHANGE == 1 */
		if ( id == IDC_LIST && ntf == LBN_SELCHANGE ) { LSSD_OnSelChange(); return 0; }

		if ( ntf == EN_CHANGE && !lssd_skipNotify ) { LSSD_SavePanel(); return 0; }
		if ( ntf == CBN_SELCHANGE && !lssd_skipNotify ) { LSSD_SavePanel(); return 0; }

		/* font chooser buttons */
		if ( ntf == BN_CLICKED && ( id == IDC_S_FONTBTN || id == IDC_G_FONTBTN
			|| id == IDC_SPN_BFONTBTN || id == IDC_SPN_CFONTBTN || id == IDC_SPN_AFONTBTN ) ) {
			int fontId, sizeId;
			char face[LF_FACESIZE];
			int sz, bold = 0;
			if ( id == IDC_G_FONTBTN )       { fontId = IDC_G_FONT;    sizeId = IDC_G_FONTSIZE; }
			else if ( id == IDC_SPN_BFONTBTN ) { fontId = IDC_SPN_BFONT; sizeId = IDC_SPN_BFSIZE; }
			else if ( id == IDC_SPN_CFONTBTN ) { fontId = IDC_SPN_CFONT; sizeId = IDC_SPN_CFSIZE; }
			else if ( id == IDC_SPN_AFONTBTN ) { fontId = IDC_SPN_AFONT; sizeId = IDC_SPN_AFSIZE; }
			else                               { fontId = IDC_S_FONT;    sizeId = IDC_S_FONTSIZE; }
			GetDlgItemTextA( lssd_panel, fontId, face, sizeof(face) );
			sz = GetDlgItemInt( lssd_panel, sizeId, NULL, FALSE );
			if ( LSSD_EditFont( hw, face, sizeof(face), &sz, &bold ) ) {
				char buf[16];
				SetDlgItemTextA( lssd_panel, fontId, face );
				Com_sprintf( buf, sizeof(buf), "%d", sz );
				SetDlgItemTextA( lssd_panel, sizeId, buf );
				LSSD_SavePanel();
			}
			return 0;
		}

		/* per-column font chooser buttons (offset 10 in column stride) */
		if ( ntf == BN_CLICKED && id >= IDC_COL_BASE + 10 &&
			 id < IDC_COL_BASE + LSWND_MAX_COLUMNS * IDC_COL_STRIDE + 10 &&
			 ( id - IDC_COL_BASE ) % IDC_COL_STRIDE == 10 ) {
			int colIdx = ( id - IDC_COL_BASE ) / IDC_COL_STRIDE;
			int fontId = IDC_COL_BASE + colIdx * IDC_COL_STRIDE + 7;
			int sizeId = IDC_COL_BASE + colIdx * IDC_COL_STRIDE + 8;
			char face[LF_FACESIZE];
			int sz, bold = 0;
			GetDlgItemTextA( lssd_panel, fontId, face, sizeof(face) );
			sz = GetDlgItemInt( lssd_panel, sizeId, NULL, FALSE );
			if ( LSSD_EditFont( hw, face, sizeof(face), &sz, &bold ) ) {
				char buf[16];
				SetDlgItemTextA( lssd_panel, fontId, face );
				Com_sprintf( buf, sizeof(buf), "%d", sz );
				SetDlgItemTextA( lssd_panel, sizeId, buf );
				LSSD_SavePanel();
			}
			return 0;
		}

		/* color buttons */
		if ( ntf == BN_CLICKED ) {
			int ci;
			for ( ci = 0; ci < lssd_clrCount; ci++ ) {
				if ( lssd_clrSlots[ci].hw == (HWND)lp ) {
					if ( LSSD_EditColor( hw, lssd_clrSlots[ci].pClr ) ) {
						InvalidateRect( lssd_clrSlots[ci].hw, NULL, TRUE );
						LSSD_SavePanel();
					}
					return 0;
				}
			}
		}

		/* generic checkbox auto-save */
		if ( ntf == BN_CLICKED && !lssd_skipNotify && id >= 3000 && id < 3300
			 && id != IDC_SP_COLADD ) { LSSD_SavePanel(); return 0; }

		/* Add component */
		if ( id == IDC_BTN_ADD ) {
			HMENU m = CreatePopupMenu();
			POINT pt;
			int t;
			for ( t = 0; t < LSCOMP_TYPE_COUNT; t++ ) {
				int cnt = 0, j;
				char label[64];
				for ( j = 0; j < lswnd_layout.numComponents; j++ ) {
					if ( lswnd_layout.components[j].type == t ) cnt++;
				}
				if ( cnt == 0 ) {
					Q_strncpyz( label, lsCompTypeLabels[t], sizeof( label ) );
				} else if ( cnt == 1 ) {
					Com_sprintf( label, sizeof( label ), "%s [used]", lsCompTypeLabels[t] );
				} else {
					Com_sprintf( label, sizeof( label ), "%s [x%d]", lsCompTypeLabels[t], cnt );
				}
				AppendMenuA( m, MF_STRING, IDM_ADD_BASE + t, label );
			}
			GetCursorPos( &pt );
			TrackPopupMenu( m, TPM_LEFTALIGN, pt.x, pt.y, 0, hw, NULL );
			DestroyMenu( m );
			return 0;
		}
		if ( id >= IDM_ADD_BASE && id < IDM_ADD_BASE + LSCOMP_TYPE_COUNT ) {
			int t = id - IDM_ADD_BASE;
			if ( lswnd_layout.numComponents < LSWND_MAX_COMPONENTS ) {
				lsComponent_t *c = &lswnd_layout.components[lswnd_layout.numComponents];
				c->type = (lsCompType_t)t;
				LSLAY_DefaultCompSettings( c->type, &c->s );
				lswnd_layout.numComponents++;
				LSSD_RefreshList();
				SendMessageA( lssd_list, LB_SETCURSEL, lswnd_layout.numComponents, 0 );
				LSSD_OnSelChange();
			}
			return 0;
		}

		/* Remove component */
		if ( id == IDC_BTN_REM ) {
			int sel = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 ) - 1;
			if ( sel >= 0 && sel < lswnd_layout.numComponents ) {
				int j;
				for ( j = sel; j < lswnd_layout.numComponents - 1; j++ )
					lswnd_layout.components[j] = lswnd_layout.components[j + 1];
				lswnd_layout.numComponents--;
				LSSD_RefreshList();
				LSSD_ClearPanel();
				if ( lssd_panel ) RedrawWindow( lssd_panel, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN );
				LSSD_ApplyLive();
			}
			return 0;
		}

		/* Move up */
		if ( id == IDC_BTN_UP ) {
			int sel = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 ) - 1;
			if ( sel > 0 && sel < lswnd_layout.numComponents ) {
				lsComponent_t tmp = lswnd_layout.components[sel];
				lswnd_layout.components[sel] = lswnd_layout.components[sel - 1];
				lswnd_layout.components[sel - 1] = tmp;
				LSSD_RefreshList();
				SendMessageA( lssd_list, LB_SETCURSEL, sel, 0 );
				LSSD_ApplyLive();
			}
			return 0;
		}

		/* Move down */
		if ( id == IDC_BTN_DN ) {
			int sel = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 ) - 1;
			if ( sel >= 0 && sel < lswnd_layout.numComponents - 1 ) {
				lsComponent_t tmp = lswnd_layout.components[sel];
				lswnd_layout.components[sel] = lswnd_layout.components[sel + 1];
				lswnd_layout.components[sel + 1] = tmp;
				LSSD_RefreshList();
				SendMessageA( lssd_list, LB_SETCURSEL, sel + 2, 0 );
				LSSD_ApplyLive();
			}
			return 0;
		}

		/* Column add (splits) */
		if ( id == IDC_SP_COLADD ) {
			HMENU m = CreatePopupMenu();
			POINT pt;
			int t;
			for ( t = 0; t < LSCOL_TYPE_COUNT; t++ )
				AppendMenuA( m, MF_STRING, 5000 + t, lsColTypeLabels[t] );
			GetCursorPos( &pt );
			TrackPopupMenu( m, TPM_LEFTALIGN, pt.x, pt.y, 0, hw, NULL );
			DestroyMenu( m );
			return 0;
		}
		if ( id >= 5000 && id < 5000 + LSCOL_TYPE_COUNT ) {
			int ci2 = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 ) - 1;
			if ( ci2 >= 0 && ci2 < lswnd_layout.numComponents ) {
				lsCompSettings_t *ss = &lswnd_layout.components[ci2].s;
				if ( ss->u.splits.numColumns < LSWND_MAX_COLUMNS ) {
					int nc = ss->u.splits.numColumns;
					LSSD_SavePanel();
					ss->u.splits.columns[nc] = id - 5000;
					memset( &ss->u.splits.colSettings[nc], 0, sizeof( lsColumnSettings_t ) );
					ss->u.splits.colSettings[nc].textColor = LSCLR_MakeInherit();
					ss->u.splits.numColumns++;
					LSSD_BuildPanel();
					LSSD_ApplyLive();
				}
			}
			return 0;
		}

		/* Inline column buttons (Move Up/Down/Remove) */
		if ( ntf == BN_CLICKED && id >= IDC_COL_BASE &&
			 id < IDC_COL_BASE + LSWND_MAX_COLUMNS * IDC_COL_STRIDE ) {
			int ci2 = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 ) - 1;
			if ( ci2 >= 0 && ci2 < lswnd_layout.numComponents &&
				 lswnd_layout.components[ci2].type == LSCOMP_SPLITS ) {
				lsCompSettings_t *ss = &lswnd_layout.components[ci2].s;
				int colIdx = ( id - IDC_COL_BASE ) / IDC_COL_STRIDE;
				int colSub = ( id - IDC_COL_BASE ) % IDC_COL_STRIDE;
				LSSD_SavePanel();
				if ( colSub == 4 && colIdx > 0 ) { /* Move Up */
					int tmpC = ss->u.splits.columns[colIdx];
					lsColumnSettings_t tmpS = ss->u.splits.colSettings[colIdx];
					ss->u.splits.columns[colIdx] = ss->u.splits.columns[colIdx - 1];
					ss->u.splits.colSettings[colIdx] = ss->u.splits.colSettings[colIdx - 1];
					ss->u.splits.columns[colIdx - 1] = tmpC;
					ss->u.splits.colSettings[colIdx - 1] = tmpS;
					LSSD_BuildPanel();
					LSSD_ApplyLive();
				} else if ( colSub == 5 && colIdx < ss->u.splits.numColumns - 1 ) { /* Move Down */
					int tmpC = ss->u.splits.columns[colIdx];
					lsColumnSettings_t tmpS = ss->u.splits.colSettings[colIdx];
					ss->u.splits.columns[colIdx] = ss->u.splits.columns[colIdx + 1];
					ss->u.splits.colSettings[colIdx] = ss->u.splits.colSettings[colIdx + 1];
					ss->u.splits.columns[colIdx + 1] = tmpC;
					ss->u.splits.colSettings[colIdx + 1] = tmpS;
					LSSD_BuildPanel();
					LSSD_ApplyLive();
				} else if ( colSub == 6 && colIdx >= 0 && colIdx < ss->u.splits.numColumns ) { /* Remove */
					int j;
					for ( j = colIdx; j < ss->u.splits.numColumns - 1; j++ ) {
						ss->u.splits.columns[j] = ss->u.splits.columns[j + 1];
						ss->u.splits.colSettings[j] = ss->u.splits.colSettings[j + 1];
					}
					ss->u.splits.numColumns--;
					LSSD_BuildPanel();
					LSSD_ApplyLive();
				}
			}
			return 0;
		}

		/* OK */
		if ( id == IDC_BTN_OK ) {
			LSSD_SavePanel();
			LSLAY_SaveLayout();
			DestroyWindow( hw );
			return 0;
		}

		/* Cancel */
		if ( id == IDC_BTN_CANCEL ) {
			lswnd_layout = lssd_backup;
			LSSD_ApplyLive();
			DestroyWindow( hw );
			return 0;
		}

		/* Reset all - with confirmation */
		if ( id == IDC_BTN_RESET ) {
			if ( MessageBoxA( hw, "Reset ALL settings to defaults?\nThis cannot be undone.",
					"Confirm Reset", MB_YESNO | MB_ICONQUESTION ) == IDYES ) {
				LS_LayoutSetDefault( &lswnd_layout );
				LSSD_RefreshList();
				SendMessageA( lssd_list, LB_SETCURSEL, 0, 0 );
				LSSD_BuildPanel();
				LSSD_ApplyLive();
			}
			return 0;
		}

		/* Reset current component */
		if ( id == IDC_BTN_RESETCOMP ) {
			int sel = (int)SendMessageA( lssd_list, LB_GETCURSEL, 0, 0 ) - 1;
			if ( sel >= 0 && sel < lswnd_layout.numComponents ) {
				if ( MessageBoxA( hw, "Reset this component to defaults?",
						"Confirm Reset", MB_YESNO | MB_ICONQUESTION ) == IDYES ) {
					LSLAY_DefaultCompSettings( lswnd_layout.components[sel].type,
						&lswnd_layout.components[sel].s );
					LSSD_BuildPanel();
					LSSD_ApplyLive();
				}
			}
			return 0;
		}

		/* Export layout */
		if ( id == IDC_BTN_EXPORT ) {
			OPENFILENAMEA ofn = {0};
			char path[MAX_PATH] = "livesplit_layout.json";
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner   = hw;
			ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
			ofn.lpstrFile   = path;
			ofn.nMaxFile    = sizeof(path);
			ofn.Flags       = OFN_OVERWRITEPROMPT;
			ofn.lpstrDefExt = "json";
			if ( GetSaveFileNameA( &ofn ) ) {
				LSSD_SavePanel();
				/* Use the existing save mechanism but to custom path */
				LSLAY_SaveLayout();
				/* Copy the layout file to the chosen path */
				{
					char srcPath[MAX_PATH];
					LSLAY_GetLayoutPath( srcPath, sizeof(srcPath) );
					CopyFileA( srcPath, path, FALSE );
				}
			}
			return 0;
		}

		/* Import layout */
		if ( id == IDC_BTN_IMPORT ) {
			OPENFILENAMEA ofn = {0};
			char path[MAX_PATH] = "";
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner   = hw;
			ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
			ofn.lpstrFile   = path;
			ofn.nMaxFile    = sizeof(path);
			ofn.Flags       = OFN_FILEMUSTEXIST;
			if ( GetOpenFileNameA( &ofn ) ) {
				/* Copy imported file to our layout path, then reload */
				char dstPath[MAX_PATH];
				LSLAY_GetLayoutPath( dstPath, sizeof(dstPath) );
				CopyFileA( path, dstPath, FALSE );
				LSLAY_LoadLayout();
				LSSD_RefreshList();
				SendMessageA( lssd_list, LB_SETCURSEL, 0, 0 );
				LSSD_BuildPanel();
				LSSD_ApplyLive();
			}
			return 0;
		}
		break;
	}

	case WM_DRAWITEM: {
		DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
		int ci;

		/* owner-draw section headers */
		for ( ci = 0; ci < lssd_dynCount; ci++ ) {
			if ( lssd_dynCtrls[ci] == dis->hwndItem && ( dis->CtlType == ODT_STATIC ) ) {
				char txt[64];
				int textLen;
				HPEN pen;
				HPEN oldPen;
				/* dark background */
				FillRect( dis->hDC, &dis->rcItem, GetSysColorBrush( COLOR_3DFACE ) );
				/* accent line */
				pen = CreatePen( PS_SOLID, 2, LSDK_ACCENT );
				oldPen = (HPEN)SelectObject( dis->hDC, pen );
				MoveToEx( dis->hDC, dis->rcItem.left, dis->rcItem.bottom - 1, NULL );
				LineTo( dis->hDC, dis->rcItem.right, dis->rcItem.bottom - 1 );
				SelectObject( dis->hDC, oldPen );
				DeleteObject( pen );
				/* text */
				textLen = GetWindowTextA( dis->hwndItem, txt, sizeof(txt) );
				SetBkMode( dis->hDC, TRANSPARENT );
				SetTextColor( dis->hDC, LSDK_ACCENT );
				SelectObject( dis->hDC, lssd_font );
				DrawTextA( dis->hDC, txt, textLen, &dis->rcItem, DT_SINGLELINE | DT_VCENTER | DT_LEFT );
				return TRUE;
			}
		}

		/* color buttons */
		for ( ci = 0; ci < lssd_clrCount; ci++ ) {
			if ( lssd_clrSlots[ci].hw == dis->hwndItem ) {
				lsColor_t *clr = lssd_clrSlots[ci].pClr;
				if ( clr->c1 == LSCLR_INHERIT ) {
					/* draw checkerboard pattern for inherit */
					FillRect( dis->hDC, &dis->rcItem, GetSysColorBrush( COLOR_3DFACE ) );
					SetBkMode( dis->hDC, TRANSPARENT );
					SetTextColor( dis->hDC, GetSysColor( COLOR_GRAYTEXT ) );
					DrawTextA( dis->hDC, "(default)", -1, &dis->rcItem,
						DT_SINGLELINE | DT_CENTER | DT_VCENTER );
				} else if ( clr->mode == LSCLR_MODE_VGRADIENT || clr->mode == LSCLR_MODE_HGRADIENT ) {
					/* draw gradient preview */
					COLORREF c1 = LSWND_CLR( clr->c1 ), c2 = LSWND_CLR( clr->c2 );
					int bw = dis->rcItem.right - dis->rcItem.left;
					int bh = dis->rcItem.bottom - dis->rcItem.top;
					int mp = clr->gradPos > 0 ? clr->gradPos : 50;
					if ( clr->mode == LSCLR_MODE_VGRADIENT )
						LSRND_GradientV( dis->hDC, dis->rcItem.left, dis->rcItem.top, bw, bh, c1, c2, mp );
					else
						LSRND_GradientH( dis->hDC, dis->rcItem.left, dis->rcItem.top, bw, bh, c1, c2, mp );
				} else {
					HBRUSH br = CreateSolidBrush( LSWND_CLR( clr->c1 ) );
					FillRect( dis->hDC, &dis->rcItem, br );
					DeleteObject( br );
				}
				{
					HPEN bp = CreatePen( PS_SOLID, 1, GetSysColor( COLOR_3DSHADOW ) );
					HPEN olp = (HPEN)SelectObject( dis->hDC, bp );
					HBRUSH oldBr = (HBRUSH)SelectObject( dis->hDC, GetStockObject( NULL_BRUSH ) );
					Rectangle( dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom );
					SelectObject( dis->hDC, oldBr );
					SelectObject( dis->hDC, olp );
					DeleteObject( bp );
				}
				return TRUE;
			}
		}
		break;
	}

	case WM_DPICHANGED: {
		RECT *suggested = (RECT *)lp;
		HWND child;
		int bx, by, btnY;
		lssd_dpi = HIWORD( wp );

		/* accept system-suggested window rect */
		SetWindowPos( hw, NULL,
			suggested->left, suggested->top,
			suggested->right - suggested->left,
			suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE );

		/* recreate font at new DPI */
		if ( lssd_font ) DeleteObject( lssd_font );
		lssd_font = CreateFontA( LSSD_S(-12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_SWISS, "Segoe UI" );

		/* reposition component list */
		MoveWindow( lssd_list, LSSD_S(6), LSSD_S(6), LSSD_S(150), LSSD_S(420), TRUE );
		SendMessage( lssd_list, WM_SETFONT, (WPARAM)lssd_font, TRUE );

		/* reposition management buttons */
		bx = LSSD_S(6); by = LSSD_S(430);
		child = GetDlgItem( hw, IDC_BTN_ADD );
		if ( child ) { MoveWindow( child, bx, by, LSSD_S(30), LSSD_S(24), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		bx += LSSD_S(32);
		child = GetDlgItem( hw, IDC_BTN_REM );
		if ( child ) { MoveWindow( child, bx, by, LSSD_S(30), LSSD_S(24), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		bx += LSSD_S(32);
		child = GetDlgItem( hw, IDC_BTN_UP );
		if ( child ) { MoveWindow( child, bx, by, LSSD_S(34), LSSD_S(24), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		bx += LSSD_S(36);
		child = GetDlgItem( hw, IDC_BTN_DN );
		if ( child ) { MoveWindow( child, bx, by, LSSD_S(34), LSSD_S(24), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }

		/* reposition scrollable panel */
		if ( lssd_panel ) MoveWindow( lssd_panel, LSSD_S(164), LSSD_S(6), LSSD_PANEL_W, LSSD_PANEL_H, TRUE );

		/* reposition bottom buttons */
		btnY = LSSD_S(464);
		child = GetDlgItem( hw, IDC_BTN_OK );
		if ( child ) { MoveWindow( child, LSSD_S(6), btnY, LSSD_S(70), LSSD_S(28), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		child = GetDlgItem( hw, IDC_BTN_CANCEL );
		if ( child ) { MoveWindow( child, LSSD_S(80), btnY, LSSD_S(70), LSSD_S(28), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		child = GetDlgItem( hw, IDC_BTN_RESETCOMP );
		if ( child ) { MoveWindow( child, LSSD_S(155), btnY, LSSD_S(85), LSSD_S(28), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		child = GetDlgItem( hw, IDC_BTN_RESET );
		if ( child ) { MoveWindow( child, LSSD_S(245), btnY, LSSD_S(70), LSSD_S(28), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		child = GetDlgItem( hw, IDC_BTN_IMPORT );
		if ( child ) { MoveWindow( child, LSSD_S(510), btnY, LSSD_S(70), LSSD_S(28), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }
		child = GetDlgItem( hw, IDC_BTN_EXPORT );
		if ( child ) { MoveWindow( child, LSSD_S(584), btnY, LSSD_S(70), LSSD_S(28), TRUE ); SendMessage( child, WM_SETFONT, (WPARAM)lssd_font, TRUE ); }

		/* rebuild panel contents at new DPI */
		LSSD_BuildPanel();
		return 0;
	}

	case WM_CLOSE:
		lswnd_layout = lssd_backup;
		LSSD_ApplyLive();
		DestroyWindow( hw );
		return 0;

	case WM_DESTROY:
		LSSD_ClearPanel();
		if ( lssd_font ) { DeleteObject( lssd_font ); lssd_font = NULL; }
			lssd_panel = NULL;
		lssd_list = NULL;
		lssd_hw = NULL;
		lssd_lastSel = -1;
		return 0;
	}
	return DefWindowProcA( hw, msg, wp, lp );
}

/* ---- Open settings dialog ---- */
static void LSSD_Open( void ) {
	static int reg = 0;
	HWND hw, btn;

	if ( lssd_hw ) {
		if ( IsWindow( lssd_hw ) ) { SetForegroundWindow( lssd_hw ); return; }
		lssd_hw = NULL;
	}

	LSSD_InitDpi();

	lssd_backup = lswnd_layout;

	if ( !reg ) {
		WNDCLASSEXA wc = {0};
		wc.cbSize        = sizeof( wc );
		wc.lpfnWndProc   = LSSD_WndProc;
		wc.hInstance      = GetModuleHandle( NULL );
		wc.lpszClassName  = "LSSD_Settings";
		wc.hbrBackground  = (HBRUSH)( COLOR_3DFACE + 1 );
		wc.hCursor        = LoadCursor( NULL, IDC_ARROW );
		wc.hIcon          = LoadIconA( GetModuleHandle( NULL ), MAKEINTRESOURCEA( 1 ) );
		wc.hIconSm        = wc.hIcon;
		RegisterClassExA( &wc );
		/* scrollable panel class */
		wc.lpfnWndProc   = LSSD_PanelProc;
		wc.lpszClassName  = "LSSD_Panel";
		RegisterClassExA( &wc );
		reg = 1;
	}

	lssd_font = CreateFontA( LSSD_S(-12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_SWISS, "Segoe UI" );

	/* Position settings dialog near the LiveSplit window (same monitor)
	   so the DPI matches what LSSD_InitDpi queried. */
	{
		int sx = CW_USEDEFAULT, sy = CW_USEDEFAULT;
		if ( lswnd_hwnd && IsWindow( lswnd_hwnd ) ) {
			RECT wr;
			GetWindowRect( lswnd_hwnd, &wr );
			sx = wr.right + 10;
			sy = wr.top;
		}
		hw = CreateWindowExA( 0, "LSSD_Settings", "LiveSplit Layout Editor",
			WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
			sx, sy, LSSD_S(680), LSSD_S(550),
			NULL, NULL, GetModuleHandle( NULL ), NULL );
	}
	lssd_hw = hw;

	/* ---- component list on the left ---- */
	{
		INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
		InitCommonControlsEx( &icc );
	}
	lssd_list = CreateWindowExA( WS_EX_CLIENTEDGE, "LISTBOX", "",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		LSSD_S(6), LSSD_S(6), LSSD_S(150), LSSD_S(420), hw, (HMENU)IDC_LIST, NULL, NULL );
	SendMessage( lssd_list, WM_SETFONT, (WPARAM)lssd_font, TRUE );

	/* management buttons below list */
	{
		int bx2 = LSSD_S(6), by2 = LSSD_S(430);
		btn = CreateWindowExA( 0, "BUTTON", "+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			bx2, by2, LSSD_S(30), LSSD_S(24), hw, (HMENU)IDC_BTN_ADD, NULL, NULL );
		SendMessage( btn, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		bx2 += LSSD_S(32);
		btn = CreateWindowExA( 0, "BUTTON", "-", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			bx2, by2, LSSD_S(30), LSSD_S(24), hw, (HMENU)IDC_BTN_REM, NULL, NULL );
		SendMessage( btn, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		bx2 += LSSD_S(32);
		btn = CreateWindowExA( 0, "BUTTON", "Up", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			bx2, by2, LSSD_S(34), LSSD_S(24), hw, (HMENU)IDC_BTN_UP, NULL, NULL );
		SendMessage( btn, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		bx2 += LSSD_S(36);
		btn = CreateWindowExA( 0, "BUTTON", "Dn", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			bx2, by2, LSSD_S(34), LSSD_S(24), hw, (HMENU)IDC_BTN_DN, NULL, NULL );
		SendMessage( btn, WM_SETFONT, (WPARAM)lssd_font, TRUE );
	}

	/* ---- scrollable panel on the right ---- */
	lssd_panel = CreateWindowExA( 0, "LSSD_Panel", "",
		WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		LSSD_S(164), LSSD_S(6), LSSD_PANEL_W, LSSD_PANEL_H, hw, NULL, NULL, NULL );
	lssd_scrollPos = 0;
	lssd_lastSel = -1;

	/* ---- bottom buttons ---- */
	{
		HWND b;
		int btnY = LSSD_S(464);

		b = CreateWindowExA( 0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			LSSD_S(6), btnY, LSSD_S(70), LSSD_S(28), hw, (HMENU)IDC_BTN_OK, NULL, NULL );
		SendMessage( b, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		b = CreateWindowExA( 0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
			LSSD_S(80), btnY, LSSD_S(70), LSSD_S(28), hw, (HMENU)IDC_BTN_CANCEL, NULL, NULL );
		SendMessage( b, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		b = CreateWindowExA( 0, "BUTTON", "Reset Comp", WS_CHILD | WS_VISIBLE,
			LSSD_S(155), btnY, LSSD_S(85), LSSD_S(28), hw, (HMENU)IDC_BTN_RESETCOMP, NULL, NULL );
		SendMessage( b, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		b = CreateWindowExA( 0, "BUTTON", "Reset All", WS_CHILD | WS_VISIBLE,
			LSSD_S(245), btnY, LSSD_S(70), LSSD_S(28), hw, (HMENU)IDC_BTN_RESET, NULL, NULL );
		SendMessage( b, WM_SETFONT, (WPARAM)lssd_font, TRUE );

		b = CreateWindowExA( 0, "BUTTON", "Import", WS_CHILD | WS_VISIBLE,
			LSSD_S(510), btnY, LSSD_S(70), LSSD_S(28), hw, (HMENU)IDC_BTN_IMPORT, NULL, NULL );
		SendMessage( b, WM_SETFONT, (WPARAM)lssd_font, TRUE );
		b = CreateWindowExA( 0, "BUTTON", "Export", WS_CHILD | WS_VISIBLE,
			LSSD_S(584), btnY, LSSD_S(70), LSSD_S(28), hw, (HMENU)IDC_BTN_EXPORT, NULL, NULL );
		SendMessage( b, WM_SETFONT, (WPARAM)lssd_font, TRUE );
	}

	LSSD_RefreshList();
	SendMessageA( lssd_list, LB_SETCURSEL, 0, 0 );
	LSSD_BuildPanel();

	ShowWindow( hw, SW_SHOW );
	UpdateWindow( hw );
}
