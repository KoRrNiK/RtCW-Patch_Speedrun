/*
===========================================================================
ls_render.c  --  Component painters, gradient fill, drawing helpers

Included as part of cl_livesplit_window.c unity build.
All engine headers, Win32 headers, shared state, and ls_layout.c
functions already available.
===========================================================================
*/

/* ---- DPI scaling --------------------------------------------------- */
static int lswnd_dpi = 96;

/* ---- Drawing helpers ----------------------------------------------- */

/* ---- Local formatting helpers (renderer cannot call cl_livesplit.c) - */
static void LSRND_FormatTime( int ms, char *out, int outSize ) {
	int mins, secs, hundredths;
	int neg = 0;
	if ( ms < 0 ) { neg = 1; ms = -ms; }
	hundredths = ( ms % 1000 ) / 10;
	secs       = ( ms / 1000 ) % 60;
	mins       = ms / 60000;
	if ( neg )
		Com_sprintf( out, outSize, "-%d:%02d.%02d", mins, secs, hundredths );
	else
		Com_sprintf( out, outSize, "%d:%02d.%02d", mins, secs, hundredths );
}

static void LSRND_FormatDelta( int deltaMs, char *out, int outSize ) {
	int ad, secs, hundredths;
	if ( deltaMs == 0 ) { Com_sprintf( out, outSize, "-" ); return; }
	ad         = deltaMs < 0 ? -deltaMs : deltaMs;
	secs       = ad / 1000;
	hundredths = ( ad % 1000 ) / 10;
	if ( secs >= 60 )
		Com_sprintf( out, outSize, "%c%d:%02d", deltaMs < 0 ? '-' : '+', secs / 60, secs % 60 );
	else
		Com_sprintf( out, outSize, "%c%d.%02d", deltaMs < 0 ? '-' : '+', secs, hundredths );
}

/* Return comparison cumulative ms for a row based on given compareAgainst value */
static int LSRND_CompareCum( int pbCumMs, int bestCumMs, int avgCumMs, int compareAgainst ) {
	switch ( compareAgainst ) {
	case 1:  return bestCumMs;   /* Best Segments */
	case 2:  return avgCumMs;    /* Average */
	default: return pbCumMs;     /* PB */
	}
}

/* ---- Drawing helpers ----------------------------------------------- */

static void LSRND_FillRC( HDC dc, int x, int y, int w, int h, COLORREF c ) {
	RECT rc;
	HBRUSH br;
	if ( w <= 0 || h <= 0 ) return;
	SetRect( &rc, x, y, x + w, y + h );
	br = CreateSolidBrush( c );
	FillRect( dc, &rc, br );
	DeleteObject( br );
}

static COLORREF LSRND_AlphaBlend( COLORREF fg, COLORREF bg, int alpha ) {
	int r = ( GetRValue( fg ) * alpha + GetRValue( bg ) * ( 255 - alpha ) ) / 255;
	int g = ( GetGValue( fg ) * alpha + GetGValue( bg ) * ( 255 - alpha ) ) / 255;
	int b = ( GetBValue( fg ) * alpha + GetBValue( bg ) * ( 255 - alpha ) ) / 255;
	return RGB( r, g, b );
}

static void LSRND_GradientV( HDC dc, int x, int y, int w, int h, COLORREF c1, COLORREF c2, int midPct ) {
	int i, r1, g1, b1, r2, g2, b2, mr, mg, mb, midY;
	RECT rc;
	if ( h <= 0 || w <= 0 ) return;
	if ( midPct < 1 ) midPct = 1;
	if ( midPct > 99 ) midPct = 99;
	r1 = GetRValue( c1 ); g1 = GetGValue( c1 ); b1 = GetBValue( c1 );
	r2 = GetRValue( c2 ); g2 = GetGValue( c2 ); b2 = GetBValue( c2 );
	mr = ( r1 + r2 ) / 2; mg = ( g1 + g2 ) / 2; mb = ( b1 + b2 ) / 2;
	midY = h * midPct / 100;
	if ( midY < 1 ) midY = 1;
	if ( midY >= h ) midY = h - 1;
	for ( i = 0; i < h; i++ ) {
		int r, g, b;
		HBRUSH br;
		if ( i < midY ) {
			r = r1 + ( mr - r1 ) * i / midY;
			g = g1 + ( mg - g1 ) * i / midY;
			b = b1 + ( mb - b1 ) * i / midY;
		} else {
			int t = i - midY, len = h - midY;
			if ( len < 1 ) len = 1;
			r = mr + ( r2 - mr ) * t / len;
			g = mg + ( g2 - mg ) * t / len;
			b = mb + ( b2 - mb ) * t / len;
		}
		SetRect( &rc, x, y + i, x + w, y + i + 1 );
		br = CreateSolidBrush( RGB( r & 0xFF, g & 0xFF, b & 0xFF ) );
		FillRect( dc, &rc, br );
		DeleteObject( br );
	}
}

static void LSRND_GradientH( HDC dc, int x, int y, int w, int h, COLORREF c1, COLORREF c2, int midPct ) {
	int i, r1, g1, b1, r2, g2, b2, mr, mg, mb, midX;
	RECT rc;
	if ( h <= 0 || w <= 0 ) return;
	if ( midPct < 1 ) midPct = 1;
	if ( midPct > 99 ) midPct = 99;
	r1 = GetRValue( c1 ); g1 = GetGValue( c1 ); b1 = GetBValue( c1 );
	r2 = GetRValue( c2 ); g2 = GetGValue( c2 ); b2 = GetBValue( c2 );
	mr = ( r1 + r2 ) / 2; mg = ( g1 + g2 ) / 2; mb = ( b1 + b2 ) / 2;
	midX = w * midPct / 100;
	if ( midX < 1 ) midX = 1;
	if ( midX >= w ) midX = w - 1;
	for ( i = 0; i < w; i++ ) {
		int r, g, b;
		HBRUSH br;
		if ( i < midX ) {
			r = r1 + ( mr - r1 ) * i / midX;
			g = g1 + ( mg - g1 ) * i / midX;
			b = b1 + ( mb - b1 ) * i / midX;
		} else {
			int t = i - midX, len = w - midX;
			if ( len < 1 ) len = 1;
			r = mr + ( r2 - mr ) * t / len;
			g = mg + ( g2 - mg ) * t / len;
			b = mb + ( b2 - mb ) * t / len;
		}
		SetRect( &rc, x + i, y, x + i + 1, y + h );
		br = CreateSolidBrush( RGB( r & 0xFF, g & 0xFF, b & 0xFF ) );
		FillRect( dc, &rc, br );
		DeleteObject( br );
	}
}

/* Fill a rectangle using lsColor_t (plain / vgradient / hgradient + alpha) */
static void LSRND_FillBG( HDC dc, int x, int y, int w, int h, lsColor_t clr, unsigned fallbackRGB ) {
	unsigned rgb1, rgb2;
	COLORREF c1, c2;
	int mode;

	/* In transparent mode, skip background fills (per-pixel alpha handles it) */
	if ( lswnd_layout.transparent ) {
		return;
	}

	rgb1 = ( clr.c1 == LSCLR_INHERIT ) ? fallbackRGB : clr.c1;
	mode = ( clr.c1 == LSCLR_INHERIT ) ? LSCLR_MODE_PLAIN : clr.mode;
	if ( rgb1 == LSCLR_INHERIT ) rgb1 = 0x111111;
	c1 = LSWND_CLR( rgb1 );

	if ( clr.alpha < 255 && clr.c1 != LSCLR_INHERIT )
		c1 = LSRND_AlphaBlend( c1, LSWND_CLR( fallbackRGB ), clr.alpha );

	switch ( mode ) {
	case LSCLR_MODE_VGRADIENT:
		rgb2 = clr.c2;
		c2 = LSWND_CLR( rgb2 );
		if ( clr.alpha < 255 ) c2 = LSRND_AlphaBlend( c2, LSWND_CLR( fallbackRGB ), clr.alpha );
		LSRND_GradientV( dc, x, y, w, h, c1, c2, clr.gradPos > 0 ? clr.gradPos : 50 );
		break;
	case LSCLR_MODE_HGRADIENT:
		rgb2 = clr.c2;
		c2 = LSWND_CLR( rgb2 );
		if ( clr.alpha < 255 ) c2 = LSRND_AlphaBlend( c2, LSWND_CLR( fallbackRGB ), clr.alpha );
		LSRND_GradientH( dc, x, y, w, h, c1, c2, clr.gradPos > 0 ? clr.gradPos : 50 );
		break;
	default:
		LSRND_FillRC( dc, x, y, w, h, c1 );
		break;
	}
}

static void LSRND_Sep( HDC dc, int x, int y, int w, COLORREF c ) {
	LSRND_FillRC( dc, x, y, w, 1, c );
}

/* ---- Font helpers -------------------------------------------------- */

static HFONT LSRND_MakeFont( const char *face, int size, int bold ) {
	return CreateFontA( -size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face );
}

static HFONT LSRND_CompFont( const lsCompSettings_t *s, int sizeMod ) {
	const char *face = s->font[0] ? s->font : lswnd_layout.globalFont;
	int sz = s->fontSize > 0 ? s->fontSize : lswnd_layout.globalFontSize;
	int bold = s->bold || lswnd_layout.globalBold;
	return LSRND_MakeFont( face, sz + sizeMod, bold );
}

/* ---- Color resolution ---------------------------------------------- */

static COLORREF LSRND_TextClr( lsColor_t clr, unsigned fallbackRGB ) {
	unsigned rgb = ( clr.c1 == LSCLR_INHERIT ) ? fallbackRGB : clr.c1;
	if ( rgb == LSCLR_INHERIT ) rgb = 0xDCDCDC;
	return LSWND_CLR( rgb );
}

static unsigned LSRND_AltBG( unsigned base, int lighten ) {
	int r = ( base >> 16 ) & 0xFF;
	int g = ( base >> 8 )  & 0xFF;
	int b =   base         & 0xFF;
	int d = lighten ? 12 : -8;
	r = min( 255, max( 0, r + d ) );
	g = min( 255, max( 0, g + d ) );
	b = min( 255, max( 0, b + d ) );
	return ( (unsigned)r << 16 ) | ( (unsigned)g << 8 ) | (unsigned)b;
}

static COLORREF LSRND_SepClr( unsigned baseBG ) {
	int r = min( 255, (int)( ( baseBG >> 16 ) & 0xFF ) + 30 );
	int g = min( 255, (int)( ( baseBG >> 8  ) & 0xFF ) + 30 );
	int b = min( 255, (int)(   baseBG         & 0xFF ) + 30 );
	return RGB( r, g, b );
}

/* Truncate a time string to the given accuracy */
static void LSRND_TruncTime( char *out, int sz, const char *src, int accuracy ) {
	const char *dot;
	int dotIdx, maxLen;
	Q_strncpyz( out, src, sz );
	/* minutes only: strip seconds by removing last :SS... portion */
	if ( accuracy == LSACC_MINUTES ) {
		char *last = strrchr( out, ':' );
		if ( last ) *last = '\0';
		return;
	}
	if ( accuracy <= LSACC_SECONDS ) {
		dot = strchr( out, '.' );
		if ( dot ) out[(int)( dot - out )] = '\0';
		return;
	}
	dot = strchr( out, '.' );
	if ( dot ) {
		dotIdx = (int)( dot - out );
		maxLen = dotIdx + 1 + accuracy;   /* 1=T, 2=H, 3=ms */
		if ( maxLen < sz ) out[maxLen] = '\0';
	}
}

/* =====================================================================
   Component painters  (each returns height consumed)
   ===================================================================== */

/* ---- Text shadow helper ------------------------------------------- */
static void LSRND_DrawTextShadow( HDC dc, const char *text, RECT *rc, UINT fmt ) {
	if ( lswnd_layout.textShadow ) {
		RECT sr = *rc;
		COLORREF oldC = SetTextColor( dc, RGB(0, 0, 0) );
		OffsetRect( &sr, 1, 1 );
		DrawTextA( dc, text, -1, &sr, fmt );
		SetTextColor( dc, oldC );
	}
	DrawTextA( dc, text, -1, rc, fmt );
}

static UINT LSRND_AlignFlag( int align ) {
	switch ( align ) {
	case LSALIGN_CENTER: return DT_CENTER;
	case LSALIGN_RIGHT:  return DT_RIGHT;
	default:             return DT_LEFT;
	}
}

/* Draw text with gradient color support (color banding approach) */
static void LSRND_DrawTextClr( HDC dc, const char *text, RECT *rc, UINT fmt,
	lsColor_t clr, lsColor_t fallback ) {
	lsColor_t eff;
	unsigned rgb1;

	/* resolve inheritance: component color > global fallback > hardcoded default */
	if ( clr.c1 != LSCLR_INHERIT ) {
		eff = clr;
	} else if ( fallback.c1 != LSCLR_INHERIT ) {
		eff = fallback;
	} else {
		eff = LSCLR_MakePlain( 0xDCDCDC );
	}
	rgb1 = eff.c1;

	if ( eff.mode == LSCLR_MODE_VGRADIENT || eff.mode == LSCLR_MODE_HGRADIENT ) {
		COLORREF c1 = LSWND_CLR( rgb1 ), c2 = LSWND_CLR( eff.c2 );
		int isVert = ( eff.mode == LSCLR_MODE_VGRADIENT );
		int size = isVert ? ( rc->bottom - rc->top ) : ( rc->right - rc->left );
		int midPct = eff.gradPos > 0 ? eff.gradPos : 50;
		int midPx, bands, i;
		int r1 = GetRValue( c1 ), g1 = GetGValue( c1 ), b1 = GetBValue( c1 );
		int r2 = GetRValue( c2 ), g2 = GetGValue( c2 ), b2 = GetBValue( c2 );
		int mr = ( r1 + r2 ) / 2, mg = ( g1 + g2 ) / 2, mb = ( b1 + b2 ) / 2;

		if ( size < 2 ) { SetTextColor( dc, c1 ); DrawTextA( dc, text, -1, rc, fmt ); return; }
		midPx = size * midPct / 100;
		if ( midPx < 1 ) midPx = 1;
		if ( midPx >= size ) midPx = size - 1;
		bands = size < 16 ? size : 16;

		if ( lswnd_layout.textShadow ) {
			RECT sr = *rc;
			SetTextColor( dc, RGB( 0, 0, 0 ) );
			OffsetRect( &sr, 1, 1 );
			DrawTextA( dc, text, -1, &sr, fmt );
		}

		for ( i = 0; i < bands; i++ ) {
			int start = i * size / bands, end = ( i + 1 ) * size / bands;
			int ctr = ( start + end ) / 2, r, g, b;
			HRGN rgn;
			if ( ctr < midPx ) {
				r = r1 + ( mr - r1 ) * ctr / midPx;
				g = g1 + ( mg - g1 ) * ctr / midPx;
				b = b1 + ( mb - b1 ) * ctr / midPx;
			} else {
				int t = ctr - midPx, len = size - midPx;
				if ( len < 1 ) len = 1;
				r = mr + ( r2 - mr ) * t / len;
				g = mg + ( g2 - mg ) * t / len;
				b = mb + ( b2 - mb ) * t / len;
			}
			if ( isVert )
				rgn = CreateRectRgn( rc->left, rc->top + start, rc->right, rc->top + end );
			else
				rgn = CreateRectRgn( rc->left + start, rc->top, rc->left + end, rc->bottom );
			SelectClipRgn( dc, rgn );
			SetTextColor( dc, RGB( r & 0xFF, g & 0xFF, b & 0xFF ) );
			DrawTextA( dc, text, -1, rc, fmt );
			DeleteObject( rgn );
		}
		SelectClipRgn( dc, NULL );
	} else {
		SetTextColor( dc, LSWND_CLR( rgb1 ) );
		LSRND_DrawTextShadow( dc, text, rc, fmt );
	}
}

/* ---- Title --------------------------------------------------------- */
static int LSRND_PaintTitle( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 4;
	int pv = ( s->padV >= 0 ) ? s->padV : 4;
	int h = 0, ty, lastLineTop, lastLineH;
	HFONT fnt, sfnt, old;
	RECT rc;
	UINT alignFmt = LSRND_AlignFlag( s->alignment );
	int hasLine;

	/* --- compute height first (no drawing) --- */
	h = pv;   /* top padding */
	if ( s->u.title.showGameName && st->gameName[0] ) h += 19;
	if ( s->u.title.showCategory && st->categoryText[0] ) h += 15;
	if ( s->u.title.showAttempts ) {
		hasLine = ( s->u.title.showGameName && st->gameName[0] )
		       || ( s->u.title.showCategory && st->categoryText[0] );
		if ( s->u.title.attemptsOnNewLine || !hasLine ) h += 14;
	}
	h += pv;  /* bottom padding */
	if ( s->overrideHeight > 0 ) h = s->overrideHeight;
	if ( h < 10 ) h = 10;

	/* --- background --- */
	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.headerBg.c1 );

	/* --- accent bottom border --- */
	if ( s->u.title.showBottomAccent )
		LSRND_FillRC( dc, 0, y + h - 2, w, 2, LSWND_CLR( LSCLR_Resolve( lswnd_layout.accentColor, 0x2694E0 ) ) );

	/* --- draw text --- */
	fnt = LSRND_CompFont( s, 2 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );

	ty = y + pv;
	lastLineTop = ty;
	lastLineH = 0;

	if ( s->u.title.showGameName && st->gameName[0] ) {
		SetRect( &rc, ph, ty, w - ph, ty + 19 );
		LSRND_DrawTextClr( dc, st->gameName, &rc, DT_SINGLELINE | DT_VCENTER | alignFmt,
			s->textColor, lswnd_layout.textColor );
		lastLineTop = ty;
		lastLineH = 19;
		ty += 19;
	}
	if ( s->u.title.showCategory && st->categoryText[0] ) {
		SetRect( &rc, ph, ty, w - ph, ty + 15 );
		LSRND_DrawTextClr( dc, st->categoryText, &rc, DT_SINGLELINE | DT_VCENTER | alignFmt,
			s->textColor, lswnd_layout.textColor );
		lastLineTop = ty;
		lastLineH = 15;
		ty += 15;
	}
	if ( s->u.title.showAttempts ) {
		char buf[64];
		sfnt = LSRND_CompFont( s, -2 );
		SelectObject( dc, sfnt );
		Com_sprintf( buf, sizeof( buf ), "%d / %d", st->completions, st->attempts );
		if ( s->u.title.attemptsOnNewLine || lastLineH == 0 ) {
			/* draw on its own line */
			SetRect( &rc, ph, ty + 2, w - ph, ty + 14 );
			LSRND_DrawTextClr( dc, buf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
				s->textColor, lswnd_layout.textColor );
		} else {
			/* inline on the last visible line - put on the opposite side of the title text */
			UINT attemptAlign = ( alignFmt == DT_RIGHT ) ? DT_LEFT : DT_RIGHT;
			SetRect( &rc, ph, lastLineTop, w - ph, lastLineTop + lastLineH );
			LSRND_DrawTextClr( dc, buf, &rc, DT_SINGLELINE | DT_VCENTER | attemptAlign,
				s->textColor, lswnd_layout.textColor );
		}
		DeleteObject( sfnt );
	}

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

/* ---- Splits -------------------------------------------------------- */
static int LSRND_PaintSplits( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	const int colW = 78;
	int ph = ( s->padH >= 0 ) ? s->padH : 3;
	int pv = ( s->padV >= 0 ) ? s->padV : 3;
	int vis  = s->u.splits.visibleSplits;
	int numR = st->numRows;
	int curR = st->curRow;
	int rowH, headerH, totalH, totalColW;
	int mainStart, mainCount, drawLastSep, lastIdx;
	unsigned baseBG = LSCLR_Resolve( s->bgColor, lswnd_layout.bgColor.c1 );
	COLORREF textC  = LSRND_TextClr( s->textColor, lswnd_layout.textColor.c1 );
	COLORREF sepC   = LSRND_SepClr( baseBG );
	HFONT fnt, old;
	int i, c;

	if ( numR <= 0 ) return 0;

	fnt  = LSRND_CompFont( s, -1 );
	old  = (HFONT)SelectObject( dc, fnt );
	rowH = ( s->fontSize > 0 ? s->fontSize : lswnd_layout.globalFontSize ) + 2 * pv;
	headerH = rowH;
	totalColW = 0;
	for ( c = 0; c < s->u.splits.numColumns; c++ )
		totalColW += ( s->u.splits.colSettings[c].width > 0 ) ? s->u.splits.colSettings[c].width : colW;

	/* ---- auto-reset scroll on split change ---- */
	if ( curR != lswnd_prevCurRow ) {
		lswnd_scrollOffset = 0;
		lswnd_prevCurRow = curR;
	}

	/* ---- scroll logic ---- */
	drawLastSep = 0;
	lastIdx     = numR - 1;
	if ( numR <= vis ) {
		mainStart = 0;
		mainCount = numR;
	} else if ( s->u.splits.lockLastToBottom && numR > 1 ) {
		int mVis  = vis - 1;
		int mRows = numR - 1;
		mainStart = curR - mVis / 2;
		if ( mainStart < 0 ) mainStart = 0;
		if ( mainStart + mVis > mRows ) mainStart = mRows - mVis;
		mainCount = min( mVis, mRows );
		drawLastSep = s->u.splits.sepBeforeLastSplit;
	} else {
		mainStart = curR - vis / 2;
		if ( mainStart < 0 ) mainStart = 0;
		if ( mainStart + vis > numR ) mainStart = numR - vis;
		mainCount = min( vis, numR );
		if ( s->u.splits.alwaysShowLast && mainStart + mainCount <= lastIdx ) {
			mainCount--;
			drawLastSep = s->u.splits.sepBeforeLastSplit;
		} else {
			lastIdx = -1; /* no separate last */
		}
	}
	if ( numR <= vis ) lastIdx = -1; /* fits, no separate last */

	/* ---- apply user scroll offset ---- */
	if ( lswnd_scrollOffset != 0 && numR > vis ) {
		int maxVisRows = ( lastIdx >= 0 ) ? mainCount : mainCount;
		int maxStart;
		mainStart += lswnd_scrollOffset;
		maxStart = ( lastIdx >= 0 ) ? ( numR - 1 - maxVisRows ) : ( numR - maxVisRows );
		if ( mainStart < 0 ) { mainStart = 0; lswnd_scrollOffset = mainStart - ( curR - vis / 2 ); }
		if ( mainStart > maxStart ) { mainStart = maxStart; }
		/* clamp scroll offset so it stays bounded */
		{
			int naturalStart;
			if ( s->u.splits.lockLastToBottom && numR > 1 ) {
				int mVis = vis - 1;
				naturalStart = curR - mVis / 2;
				if ( naturalStart < 0 ) naturalStart = 0;
				if ( naturalStart + mVis > numR - 1 ) naturalStart = numR - 1 - mVis;
			} else {
				naturalStart = curR - vis / 2;
				if ( naturalStart < 0 ) naturalStart = 0;
				if ( naturalStart + vis > numR ) naturalStart = numR - vis;
			}
			lswnd_scrollOffset = mainStart - naturalStart;
		}
	}

	/* ---- pre-fill entire splits area with component BG gradient ---- */
	{
		int estH = 0;
		if ( s->u.splits.showHeader ) estH += headerH + 1;
		estH += mainCount * rowH;
		if ( drawLastSep && lastIdx >= 0 ) estH++;
		if ( lastIdx >= 0 && ( s->u.splits.lockLastToBottom || s->u.splits.alwaysShowLast ) && numR > vis )
			estH += rowH;
		if ( estH > 0 && s->bgColor.c1 != LSCLR_INHERIT )
			LSRND_FillBG( dc, 0, y, w, estH, s->bgColor, lswnd_layout.bgColor.c1 );
	}

	/* ---- header row ---- */
	if ( s->u.splits.showHeader ) {
		int cx = w - ph;
		lsColor_t hdrClr = ( s->u.splits.headerTextColor.c1 != LSCLR_INHERIT )
			? s->u.splits.headerTextColor : LSCLR_MakePlain( sepC );
		/* header BG already covered by full-area fill */
		SetBkMode( dc, TRANSPARENT );
		for ( c = s->u.splits.numColumns - 1; c >= 0; c-- ) {
			RECT hrc;
			const char *hdrText;
			int cw = s->u.splits.colSettings[c].width > 0 ? s->u.splits.colSettings[c].width : colW;
			/* use custom label if set, else default */
			if ( s->u.splits.colSettings[c].label[0] )
				hdrText = s->u.splits.colSettings[c].label;
			else
				hdrText = lsColTypeLabels[s->u.splits.columns[c]];
			SetRect( &hrc, cx - cw, y, cx, y + headerH );
			LSRND_DrawTextClr( dc, hdrText, &hrc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, hdrClr, lswnd_layout.textColor );
			cx -= cw;
		}
	}
	totalH = s->u.splits.showHeader ? headerH : 0;
	if ( s->u.splits.showHeader ) {
		LSRND_Sep( dc, 0, y + totalH, w, sepC );
		totalH++;
	}

	/* ---- main rows ---- */
	for ( i = 0; i < mainCount; i++ ) {
		int ri   = mainStart + i;
		int ry   = y + totalH;
		int state = st->rows[ri].state;
		lsColor_t rowBg;

		/* row background */
		if ( state == 1 )      rowBg = s->u.splits.currentBg;
		else if ( state == 2 ) rowBg = s->u.splits.beforeCurrentBg;
		else                   rowBg = s->u.splits.afterCurrentBg;

		/* alternating row shading */
		if ( state != 1 && s->u.splits.alternateRows && ( ri & 1 ) ) {
			if ( s->u.splits.altRowColor.c1 != LSCLR_INHERIT ) {
				rowBg = s->u.splits.altRowColor;
			} else {
				unsigned altRGB = LSRND_AltBG( baseBG, 1 );
				if ( rowBg.c1 == LSCLR_INHERIT )
					rowBg = LSCLR_MakePlain( altRGB );
			}
		} else if ( state != 1 && s->u.splits.rowColor.c1 != LSCLR_INHERIT ) {
			if ( rowBg.c1 == LSCLR_INHERIT )
				rowBg = s->u.splits.rowColor;
		}
		/* only draw per-row BG if it's not inherited (so full-area gradient shows through) */
		if ( rowBg.c1 != LSCLR_INHERIT )
			LSRND_FillBG( dc, 0, ry, w, rowH, rowBg, baseBG );

		/* split name */
		{
			RECT nr;
			lsColor_t nameClr;
			HFONT nameFnt = NULL;
			/* per-state name color */
			if ( state == 1 )
				nameClr = s->u.splits.currentSplitClr;
			else if ( state == 2 && s->u.splits.beforeNameColor.c1 != LSCLR_INHERIT )
				nameClr = s->u.splits.beforeNameColor;
			else if ( state == 0 && s->u.splits.afterNameColor.c1 != LSCLR_INHERIT )
				nameClr = s->u.splits.afterNameColor;
			else if ( s->u.splits.nameColor.c1 != LSCLR_INHERIT )
				nameClr = s->u.splits.nameColor;
			else
				nameClr = s->textColor;
			/* per-state name font */
			{
				const char *nf = NULL;
				int nsz = 0, nb = -1;
				if ( state == 2 ) {
					if ( s->u.splits.beforeNameFont[0] ) nf = s->u.splits.beforeNameFont;
					if ( s->u.splits.beforeNameSize )    nsz = s->u.splits.beforeNameSize;
					nb = s->u.splits.beforeNameBold;
				} else if ( state == 1 ) {
					if ( s->u.splits.currentNameFont[0] ) nf = s->u.splits.currentNameFont;
					if ( s->u.splits.currentNameSize )    nsz = s->u.splits.currentNameSize;
					nb = s->u.splits.currentNameBold;
				} else {
					if ( s->u.splits.afterNameFont[0] ) nf = s->u.splits.afterNameFont;
					if ( s->u.splits.afterNameSize )    nsz = s->u.splits.afterNameSize;
					nb = s->u.splits.afterNameBold;
				}
				if ( nf || nsz || nb != -1 ) {
					const char *face = nf ? nf : ( s->font[0] ? s->font : lswnd_layout.globalFont );
					int sz = nsz ? nsz : ( s->fontSize > 0 ? s->fontSize : lswnd_layout.globalFontSize );
					int b = ( nb != -1 ) ? nb : ( s->bold || lswnd_layout.globalBold );
					nameFnt = LSRND_MakeFont( face, sz - 1, b );
					SelectObject( dc, nameFnt );
				}
			}
			SetBkMode( dc, TRANSPARENT );
			SetRect( &nr, ph, ry, w - totalColW - ph, ry + rowH );
			{
				const char *dispName = ( s->u.splits.shortNames && st->rows[ri].shortName[0] )
					? st->rows[ri].shortName : st->rows[ri].name;
				LSRND_DrawTextClr( dc, dispName, &nr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS,
					nameClr, lswnd_layout.textColor );
			}
			if ( nameFnt ) { SelectObject( dc, fnt ); DeleteObject( nameFnt ); }
		}

		/* columns */
		{
			int cx = w - ph;
			/* pre-compute unified countdown flag: both Delta and Delta Best
			   start showing at the same moment (whichever triggers first) */
			int countdownActive = 0;
			if ( state == 1 && s->u.splits.deltaCountdownSec > 0 ) {
				int thresh = s->u.splits.deltaCountdownSec * 1000;
				if ( st->rows[ri].liveDeltaMs != (int)0x80000000
					 && st->rows[ri].liveDeltaMs >= -thresh )
					countdownActive = 1;
				if ( st->rows[ri].bestSegMs > 0 && st->rows[ri].segTimeMs > 0
					 && ( st->rows[ri].segTimeMs - st->rows[ri].bestSegMs ) >= -thresh )
					countdownActive = 1;
			}
			for ( c = s->u.splits.numColumns - 1; c >= 0; c-- ) {
				RECT cr;
				const char *txt = "";
				lsColor_t colClr;
				char tbuf[24];
				int colType = s->u.splits.columns[c];
				HFONT colFnt = NULL;
				int cw = s->u.splits.colSettings[c].width > 0 ? s->u.splits.colSettings[c].width : colW;

				/* per-column state color resolution */
				if ( state == 2 && s->u.splits.colSettings[c].beforeColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].beforeColor;
				else if ( state == 1 && s->u.splits.colSettings[c].currentColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].currentColor;
				else if ( state == 0 && s->u.splits.colSettings[c].afterColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].afterColor;
				else if ( s->u.splits.colSettings[c].textColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].textColor;
				else
					colClr = s->textColor;

				/* per-column font override */
				if ( s->u.splits.colSettings[c].font[0] || s->u.splits.colSettings[c].fontSize || s->u.splits.colSettings[c].bold != -1 ) {
					const char *cf = s->u.splits.colSettings[c].font[0] ? s->u.splits.colSettings[c].font : ( s->font[0] ? s->font : lswnd_layout.globalFont );
					int csz = s->u.splits.colSettings[c].fontSize ? s->u.splits.colSettings[c].fontSize : ( s->fontSize > 0 ? s->fontSize : lswnd_layout.globalFontSize );
					int cb = ( s->u.splits.colSettings[c].bold != -1 ) ? s->u.splits.colSettings[c].bold : ( s->bold || lswnd_layout.globalBold );
					colFnt = LSRND_MakeFont( cf, csz - 1, cb );
					SelectObject( dc, colFnt );
				}

				switch ( colType ) {
				case LSCOL_DELTA:
					txt = st->rows[ri].delta;
					if ( txt[0] == '-' && txt[1] == '\0' ) break; /* empty-split placeholder */
					/* delta countdown: for current split show live delta when within threshold */
					if ( state == 1 && s->u.splits.deltaCountdownSec > 0
						 && st->rows[ri].liveDeltaMs != (int)0x80000000 ) {
						if ( countdownActive ) {
							int dms = st->rows[ri].liveDeltaMs;
							int ad = dms < 0 ? -dms : dms;
							int se = ad / 1000;
							int hu = ( ad % 1000 ) / 10;
							if ( se >= 60 )
								Com_sprintf( tbuf, sizeof(tbuf), "%c%d:%02d", dms < 0 ? '-' : '+', se / 60, se % 60 );
							else
								Com_sprintf( tbuf, sizeof(tbuf), "%c%d.%02d", dms < 0 ? '-' : '+', se, hu );
							txt = tbuf;
						} else {
							txt = "";
						}
					} else if ( txt[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), txt, s->u.splits.deltaAccuracy );
						txt = tbuf;
					}
					if ( state == 1 && s->u.splits.deltaCountdownSec > 0
						 && st->rows[ri].liveDeltaMs != (int)0x80000000 && txt[0] ) {
						/* countdown colors: ahead/behind based on live delta sign */
						if ( s->u.splits.liveDeltaColor.c1 != LSCLR_INHERIT )
							colClr = s->u.splits.liveDeltaColor;
						else if ( st->rows[ri].liveDeltaMs > 0 )
							colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
						else
							colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
					} else if ( state == 1 && s->u.splits.liveDeltaColor.c1 != LSCLR_INHERIT )
						colClr = s->u.splits.liveDeltaColor;
					else if ( st->rows[ri].isBehind )
						colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
					else if ( st->rows[ri].isGold )
						colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
					else if ( txt[0] )
						colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
					break;
				case LSCOL_SPLIT_TIME:
					txt = st->rows[ri].splitTime;
					if ( txt[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), txt, s->u.splits.splitAccuracy );
						txt = tbuf;
					} else if ( st->rows[ri].pbSplitTime[0] ) {
						/* show PB cumulative for future splits (like LiveSplit) */
						LSRND_TruncTime( tbuf, sizeof( tbuf ), st->rows[ri].pbSplitTime, s->u.splits.splitAccuracy );
						txt = tbuf;
					}
					if ( s->u.splits.splitTimeColor.c1 != LSCLR_INHERIT )
						colClr = s->u.splits.splitTimeColor;
					break;
				case LSCOL_SEG_TIME:
					txt = st->rows[ri].segTime;
					break;
				case LSCOL_BEST_SEG:
					txt = st->rows[ri].bestSeg;
					break;
				case LSCOL_PB_SPLIT:
					/* show actual run time for done/current, PB for future */
					if ( st->rows[ri].splitTime[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), st->rows[ri].splitTime, s->u.splits.splitAccuracy );
						txt = tbuf;
					} else if ( st->rows[ri].pbSplitTime[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), st->rows[ri].pbSplitTime, s->u.splits.splitAccuracy );
						txt = tbuf;
					}
					if ( s->u.splits.splitTimeColor.c1 != LSCLR_INHERIT )
						colClr = s->u.splits.splitTimeColor;
					break;
				case LSCOL_DELTA_BEST:
					txt = st->rows[ri].deltaBest;
					if ( txt[0] == '-' && txt[1] == '\0' ) break; /* empty-split placeholder */
					if ( txt[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), txt, s->u.splits.deltaAccuracy );
						txt = tbuf;
						if ( st->rows[ri].isGold )
							colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
						else
							colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
					} else if ( state == 1
						&& st->rows[ri].bestSegMs > 0 && st->rows[ri].segTimeMs > 0 ) {
						if ( s->u.splits.deltaCountdownSec <= 0 || countdownActive ) {
							int liveBestDt = st->rows[ri].segTimeMs - st->rows[ri].bestSegMs;
							char dtmp[32];
							LSRND_FormatDelta( liveBestDt, dtmp, sizeof( dtmp ) );
							LSRND_TruncTime( tbuf, sizeof( tbuf ), dtmp, s->u.splits.deltaAccuracy );
							txt = tbuf;
							if ( liveBestDt <= 0 )
								colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
							else
								colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
						}
					}
					break;
				}
				SetRect( &cr, cx - cw, ry, cx, ry + rowH );
				LSRND_DrawTextClr( dc, txt, &cr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, colClr, lswnd_layout.textColor );
				if ( colFnt ) { SelectObject( dc, fnt ); DeleteObject( colFnt ); }
				cx -= cw;
			}
		}

		totalH += rowH;

		/* thin separator between rows */
		if ( s->u.splits.showThinSeps && i < mainCount - 1 ) {
			COLORREF sc = ( s->u.splits.rowSepColor.c1 != LSCLR_INHERIT )
				? LSWND_CLR( s->u.splits.rowSepColor.c1 ) : sepC;
			LSRND_Sep( dc, ph, y + totalH, w - 2 * ph, sc );
		}
	}

	/* ---- separator before last ---- */
	if ( drawLastSep && lastIdx >= 0 ) {
		LSRND_Sep( dc, 0, y + totalH, w, sepC );
		totalH++;
	}

	/* ---- pinned last row ---- */
	if ( lastIdx >= 0 && ( s->u.splits.lockLastToBottom || s->u.splits.alwaysShowLast ) && numR > vis ) {
		int ry = y + totalH;
		int state = st->rows[lastIdx].state;
		lsColor_t rowBg;

		if ( state == 1 )      rowBg = s->u.splits.currentBg;
		else if ( state == 2 ) rowBg = s->u.splits.beforeCurrentBg;
		else                   rowBg = s->u.splits.afterCurrentBg;
		if ( rowBg.c1 != LSCLR_INHERIT )
			LSRND_FillBG( dc, 0, ry, w, rowH, rowBg, baseBG );

		/* name */
		{
			RECT nr;
			lsColor_t nameClr;
			HFONT nameFnt = NULL;
			/* per-state name color */
			if ( state == 1 )
				nameClr = s->u.splits.currentSplitClr;
			else if ( state == 2 && s->u.splits.beforeNameColor.c1 != LSCLR_INHERIT )
				nameClr = s->u.splits.beforeNameColor;
			else if ( state == 0 && s->u.splits.afterNameColor.c1 != LSCLR_INHERIT )
				nameClr = s->u.splits.afterNameColor;
			else if ( s->u.splits.nameColor.c1 != LSCLR_INHERIT )
				nameClr = s->u.splits.nameColor;
			else
				nameClr = s->textColor;
			/* per-state name font */
			{
				const char *nf = NULL;
				int nsz = 0, nb = -1;
				if ( state == 2 ) {
					if ( s->u.splits.beforeNameFont[0] ) nf = s->u.splits.beforeNameFont;
					if ( s->u.splits.beforeNameSize )    nsz = s->u.splits.beforeNameSize;
					nb = s->u.splits.beforeNameBold;
				} else if ( state == 1 ) {
					if ( s->u.splits.currentNameFont[0] ) nf = s->u.splits.currentNameFont;
					if ( s->u.splits.currentNameSize )    nsz = s->u.splits.currentNameSize;
					nb = s->u.splits.currentNameBold;
				} else {
					if ( s->u.splits.afterNameFont[0] ) nf = s->u.splits.afterNameFont;
					if ( s->u.splits.afterNameSize )    nsz = s->u.splits.afterNameSize;
					nb = s->u.splits.afterNameBold;
				}
				if ( nf || nsz || nb != -1 ) {
					const char *face = nf ? nf : ( s->font[0] ? s->font : lswnd_layout.globalFont );
					int sz = nsz ? nsz : ( s->fontSize > 0 ? s->fontSize : lswnd_layout.globalFontSize );
					int b = ( nb != -1 ) ? nb : ( s->bold || lswnd_layout.globalBold );
					nameFnt = LSRND_MakeFont( face, sz - 1, b );
					SelectObject( dc, nameFnt );
				}
			}
			SetBkMode( dc, TRANSPARENT );
			SetRect( &nr, ph, ry, w - totalColW - ph, ry + rowH );
			{
				const char *dispName = ( s->u.splits.shortNames && st->rows[lastIdx].shortName[0] )
					? st->rows[lastIdx].shortName : st->rows[lastIdx].name;
				LSRND_DrawTextClr( dc, dispName, &nr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS, nameClr, lswnd_layout.textColor );
			}
			if ( nameFnt ) { SelectObject( dc, fnt ); DeleteObject( nameFnt ); }
		}
		/* columns */
		{
			int cx = w - ph;
			/* unified countdown flag for pinned last row */
			int lastState = st->rows[lastIdx].state;
			int countdownActive = 0;
			if ( lastState == 1 && s->u.splits.deltaCountdownSec > 0 ) {
				int thresh = s->u.splits.deltaCountdownSec * 1000;
				if ( st->rows[lastIdx].liveDeltaMs != (int)0x80000000
					 && st->rows[lastIdx].liveDeltaMs >= -thresh )
					countdownActive = 1;
				if ( st->rows[lastIdx].bestSegMs > 0 && st->rows[lastIdx].segTimeMs > 0
					 && ( st->rows[lastIdx].segTimeMs - st->rows[lastIdx].bestSegMs ) >= -thresh )
					countdownActive = 1;
			}
			for ( c = s->u.splits.numColumns - 1; c >= 0; c-- ) {
				RECT crr;
				const char *txt = "";
				lsColor_t colClr;
				char tbuf[24];
				int colType = s->u.splits.columns[c];
				HFONT colFnt = NULL;
				int cw = s->u.splits.colSettings[c].width > 0 ? s->u.splits.colSettings[c].width : colW;

				/* per-column state color resolution */
				if ( state == 2 && s->u.splits.colSettings[c].beforeColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].beforeColor;
				else if ( state == 1 && s->u.splits.colSettings[c].currentColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].currentColor;
				else if ( state == 0 && s->u.splits.colSettings[c].afterColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].afterColor;
				else if ( s->u.splits.colSettings[c].textColor.c1 != LSCLR_INHERIT )
					colClr = s->u.splits.colSettings[c].textColor;
				else
					colClr = s->textColor;

				/* per-column font override */
				if ( s->u.splits.colSettings[c].font[0] || s->u.splits.colSettings[c].fontSize || s->u.splits.colSettings[c].bold != -1 ) {
					const char *cf = s->u.splits.colSettings[c].font[0] ? s->u.splits.colSettings[c].font : ( s->font[0] ? s->font : lswnd_layout.globalFont );
					int csz = s->u.splits.colSettings[c].fontSize ? s->u.splits.colSettings[c].fontSize : ( s->fontSize > 0 ? s->fontSize : lswnd_layout.globalFontSize );
					int cb = ( s->u.splits.colSettings[c].bold != -1 ) ? s->u.splits.colSettings[c].bold : ( s->bold || lswnd_layout.globalBold );
					colFnt = LSRND_MakeFont( cf, csz - 1, cb );
					SelectObject( dc, colFnt );
				}

				switch ( colType ) {
				case LSCOL_DELTA:     txt = st->rows[lastIdx].delta;       break;
				case LSCOL_SPLIT_TIME:
					txt = st->rows[lastIdx].splitTime;
					if ( !txt[0] && st->rows[lastIdx].pbSplitTime[0] )
						txt = st->rows[lastIdx].pbSplitTime;
					break;
				case LSCOL_SEG_TIME:   txt = st->rows[lastIdx].segTime;    break;
				case LSCOL_BEST_SEG:   txt = st->rows[lastIdx].bestSeg;    break;
				case LSCOL_PB_SPLIT:
					txt = st->rows[lastIdx].splitTime;
					if ( !txt[0] ) txt = st->rows[lastIdx].pbSplitTime;
					break;
				case LSCOL_DELTA_BEST: txt = st->rows[lastIdx].deltaBest;  break;
				}
				if ( colType == LSCOL_DELTA && txt[0] == '-' && txt[1] == '\0' ) {
					/* empty-split placeholder – keep default text color */
				} else if ( colType == LSCOL_DELTA ) {
					/* delta countdown for pinned last row */
					if ( lastState == 1 && s->u.splits.deltaCountdownSec > 0
						 && st->rows[lastIdx].liveDeltaMs != (int)0x80000000 ) {
						if ( countdownActive ) {
							int dms = st->rows[lastIdx].liveDeltaMs;
							int ad = dms < 0 ? -dms : dms;
							int se = ad / 1000;
							int hu = ( ad % 1000 ) / 10;
							if ( se >= 60 )
								Com_sprintf( tbuf, sizeof(tbuf), "%c%d:%02d", dms < 0 ? '-' : '+', se / 60, se % 60 );
							else
								Com_sprintf( tbuf, sizeof(tbuf), "%c%d.%02d", dms < 0 ? '-' : '+', se, hu );
							txt = tbuf;
							if ( s->u.splits.liveDeltaColor.c1 != LSCLR_INHERIT )
								colClr = s->u.splits.liveDeltaColor;
							else if ( dms > 0 )
								colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
							else
								colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
						} else {
							txt = "";
						}
					} else if ( txt[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), txt, s->u.splits.deltaAccuracy );
						txt = tbuf;
						if ( st->rows[lastIdx].isBehind )      colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
						else if ( st->rows[lastIdx].isGold )   colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
						else                                   colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
					}
				}
				if ( colType == LSCOL_DELTA_BEST && txt[0] == '-' && txt[1] == '\0' ) {
					/* empty-split placeholder */
				} else if ( colType == LSCOL_DELTA_BEST ) {
					if ( txt[0] ) {
						LSRND_TruncTime( tbuf, sizeof( tbuf ), txt, s->u.splits.deltaAccuracy );
						txt = tbuf;
						if ( st->rows[lastIdx].isGold )
							colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
						else
							colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
					} else if ( lastState == 1
						&& st->rows[lastIdx].bestSegMs > 0 && st->rows[lastIdx].segTimeMs > 0 ) {
						if ( s->u.splits.deltaCountdownSec <= 0 || countdownActive ) {
							int liveBestDt = st->rows[lastIdx].segTimeMs - st->rows[lastIdx].bestSegMs;
							char dtmp[32];
							LSRND_FormatDelta( liveBestDt, dtmp, sizeof( dtmp ) );
							LSRND_TruncTime( tbuf, sizeof( tbuf ), dtmp, s->u.splits.deltaAccuracy );
							txt = tbuf;
							if ( liveBestDt <= 0 )
								colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
							else
								colClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
						}
					}
				}
				SetRect( &crr, cx - cw, ry, cx, ry + rowH );
				LSRND_DrawTextClr( dc, txt, &crr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, colClr, lswnd_layout.textColor );
				if ( colFnt ) { SelectObject( dc, fnt ); DeleteObject( colFnt ); }
				cx -= cw;
			}
		}
		totalH += rowH;
	}

	SelectObject( dc, old );
	DeleteObject( fnt );
	return totalH;
}

/* ---- Timer --------------------------------------------------------- */
static int LSRND_PaintTimer( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : 44;
	int ph = ( s->padH >= 0 ) ? s->padH : 4;
	lsColor_t timerClr;
	HFONT bigF, smlF, old;
	RECT rc;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	/* pick colour based on state */
	if ( st->finished && st->curRow >= 0 && st->rows[st->curRow].isGold )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.timer.goldColor, LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) ) );
	else if ( st->curRow >= 0 && st->rows[st->curRow].isBehind )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.timer.behindColor, LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) ) );
	else if ( s->u.timer.colorOnDeltaPlus && st->active && st->curRow >= 0
		&& st->rows[st->curRow].liveDeltaMs != (int)0x80000000
		&& st->rows[st->curRow].liveDeltaMs > 0 )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.timer.behindColor, LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) ) );
	else if ( st->active )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.timer.aheadColor, LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) ) );
	else
		timerClr = s->textColor;

	bigF = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, 32, 1 );
	smlF = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, 18, 1 );

	SetBkMode( dc, TRANSPARENT );

	/* fraction right-aligned */
	old = (HFONT)SelectObject( dc, smlF );
	SetRect( &rc, 0, y + 14, w - ph, y + h - 2 );
	LSRND_DrawTextClr( dc, st->timerFrac, &rc, DT_SINGLELINE | DT_BOTTOM | DT_RIGHT, timerClr, lswnd_layout.textColor );

	/* main time */
	{
		SIZE fracSz;
		int mainRight;
		GetTextExtentPoint32A( dc, st->timerFrac, (int)strlen( st->timerFrac ), &fracSz );
		mainRight = w - ph - fracSz.cx;
		SelectObject( dc, bigF );
		SetRect( &rc, ph, y + 2, mainRight, y + h - 2 );
		LSRND_DrawTextClr( dc, st->timerText, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, timerClr, lswnd_layout.textColor );
	}

	SelectObject( dc, old );
	DeleteObject( bigF );
	DeleteObject( smlF );
	return h;
}

/* ---- Detailed Timer ------------------------------------------------ */
static int LSRND_PaintDetailedTimer( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 4;
	lsColor_t timerClr, segClr, labelClr;
	HFONT bigF, bigFracF, segF, segFracF, cmpF, old;
	RECT rc;
	int cr = st->curRow;
	int mainSz, segSz, compSz;
	int h, segY, infoW;

	mainSz = s->u.detailedTimer.mainFontSize > 0 ? s->u.detailedTimer.mainFontSize : 28;
	segSz  = s->u.detailedTimer.segFontSize > 0 ? s->u.detailedTimer.segFontSize : 18;
	compSz = s->u.detailedTimer.compFontSize > 0 ? s->u.detailedTimer.compFontSize : 12;

	/* calculate height: main timer + segment timer row (if enabled) */
	if ( s->u.detailedTimer.showSegTimer )
		h = mainSz + 8 + segSz + 8 + 4;
	else
		h = mainSz + 12;
	if ( s->overrideHeight > 0 ) h = s->overrideHeight;
	if ( h < 28 ) h = 28;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	/* main timer colour */
	if ( st->finished && cr >= 0 && st->rows[cr].isGold )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.mainGoldColor, LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) ) );
	else if ( cr >= 0 && st->rows[cr].isBehind )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.mainBehindColor, LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) ) );
	else if ( s->u.detailedTimer.colorOnDeltaPlus && st->active && cr >= 0
		&& st->rows[cr].liveDeltaMs != (int)0x80000000
		&& st->rows[cr].liveDeltaMs > 0 )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.mainBehindColor, LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) ) );
	else if ( st->active )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.mainAheadColor, LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) ) );
	else
		timerClr = s->textColor;

	/* segment timer colour */
	segClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.segTimerColor, lswnd_layout.textColor.c1 ) );
	labelClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.labelColor, 0x808080 ) );

	bigF     = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, mainSz, 1 );
	bigFracF = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, mainSz / 2, 1 );
	segF     = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, segSz, 1 );
	segFracF = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, segSz / 2, 1 );
	cmpF     = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, compSz, 0 );

	SetBkMode( dc, TRANSPARENT );

	/* --- split timer text into main + fractional parts locally --- */
	{
		char mainText[32], mainFrac[8];
		char segText[32], segFrac[8];
		char *dot;

		Q_strncpyz( mainText, st->timerText, sizeof( mainText ) );
		mainFrac[0] = '\0';
		dot = strchr( mainText, '.' );
		if ( dot ) {
			Com_sprintf( mainFrac, sizeof( mainFrac ), ".%s", dot + 1 );
			*dot = '\0';
		}

		Q_strncpyz( segText, st->segTimerText, sizeof( segText ) );
		segFrac[0] = '\0';
		dot = strchr( segText, '.' );
		if ( dot ) {
			Com_sprintf( segFrac, sizeof( segFrac ), ".%s", dot + 1 );
			*dot = '\0';
		}

	/* --- main IGT timer: right-aligned, top portion --- */
	old = (HFONT)SelectObject( dc, bigFracF );
	SetRect( &rc, 0, y + 4, w - ph, y + mainSz + 6 );
	LSRND_DrawTextClr( dc, mainFrac, &rc, DT_SINGLELINE | DT_BOTTOM | DT_RIGHT, timerClr, lswnd_layout.textColor );
	{
		SIZE fracSz;
		int mainRight;
		GetTextExtentPoint32A( dc, mainFrac, (int)strlen( mainFrac ), &fracSz );
		mainRight = w - ph - fracSz.cx;
		SelectObject( dc, bigF );
		SetRect( &rc, ph, y + 2, mainRight, y + mainSz + 6 );
		LSRND_DrawTextClr( dc, mainText, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, timerClr, lswnd_layout.textColor );
	}

	if ( s->u.detailedTimer.showSegTimer ) {
	/* --- bottom row: PB/Best labels on left, segment timer on right --- */
	segY = y + mainSz + 8;

	/* info labels on the left (PB and Best stacked) */
	SelectObject( dc, cmpF );
	infoW = 0;
	if ( ( s->u.detailedTimer.showPB || s->u.detailedTimer.showBest ) && st->active ) {
		int iy = segY;
		int lineH = compSz + 2;
		SIZE bestLblSz;
		int labelW;
		/* measure the wider label ("Best:") to align both values */
		GetTextExtentPoint32A( dc, "Best: ", 6, &bestLblSz );
		labelW = bestLblSz.cx;

		if ( s->u.detailedTimer.showPB && cr >= 0 && cr < st->numRows ) {
			lsColor_t pbClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.pbColor, lswnd_layout.textColor.c1 ) );
			SIZE valSz;

			SetRect( &rc, ph, iy, ph + labelW, iy + lineH );
			LSRND_DrawTextClr( dc, "PB:", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT, labelClr, lswnd_layout.textColor );

			{
				const char *pbVal = st->rows[cr].pbSegTime[0] ? st->rows[cr].pbSegTime : "-";
				GetTextExtentPoint32A( dc, pbVal, (int)strlen( pbVal ), &valSz );
				SetRect( &rc, ph + labelW, iy, ph + labelW + valSz.cx + 4, iy + lineH );
				LSRND_DrawTextClr( dc, pbVal, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT, pbClr, lswnd_layout.textColor );
				if ( labelW + valSz.cx + 4 > infoW ) infoW = labelW + valSz.cx + 4;
			}
			iy += lineH;
		}
		if ( s->u.detailedTimer.showBest && cr >= 0 && cr < st->numRows ) {
			lsColor_t bestClr = LSCLR_MakePlain( LSCLR_Resolve( s->u.detailedTimer.bestColor, LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) ) );
			SIZE valSz;

			SetRect( &rc, ph, iy, ph + labelW, iy + lineH );
			LSRND_DrawTextClr( dc, "Best:", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT, labelClr, lswnd_layout.textColor );

			{
				const char *bestVal = st->rows[cr].bestSeg[0] ? st->rows[cr].bestSeg : "-";
				GetTextExtentPoint32A( dc, bestVal, (int)strlen( bestVal ), &valSz );
				SetRect( &rc, ph + labelW, iy, ph + labelW + valSz.cx + 4, iy + lineH );
				LSRND_DrawTextClr( dc, bestVal, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT, bestClr, lswnd_layout.textColor );
				if ( labelW + valSz.cx + 4 > infoW ) infoW = labelW + valSz.cx + 4;
			}
		}
	}

	/* segment timer right-aligned on the bottom row */
	SelectObject( dc, segFracF );
	SetRect( &rc, ph + infoW, segY, w - ph, segY + segSz + 4 );
	LSRND_DrawTextClr( dc, segFrac, &rc, DT_SINGLELINE | DT_BOTTOM | DT_RIGHT, segClr, lswnd_layout.textColor );
	{
		SIZE fracSz;
		int segRight;
		GetTextExtentPoint32A( dc, segFrac, (int)strlen( segFrac ), &fracSz );
		segRight = w - ph - fracSz.cx;
		SelectObject( dc, segF );
		SetRect( &rc, ph + infoW, segY, segRight, segY + segSz + 4 );
		LSRND_DrawTextClr( dc, segText, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, segClr, lswnd_layout.textColor );
	}

	}  /* end showSegTimer */
	}  /* end of local timer text split block */

	SelectObject( dc, old );
	DeleteObject( bigF );
	DeleteObject( bigFracF );
	DeleteObject( segF );
	DeleteObject( segFracF );
	DeleteObject( cmpF );
	return h;
}

/* ---- Real Time timer ----------------------------------------------- */
static int LSRND_PaintRealTime( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : 44;
	int ph = ( s->padH >= 0 ) ? s->padH : 4;
	lsColor_t timerClr;
	HFONT bigF, smlF, old;
	RECT rc;
	char mainText[32], mainFrac[8];
	char *dot;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	timerClr = ( s->u.realTimer.timerColor.c1 != LSCLR_INHERIT )
		? s->u.realTimer.timerColor : s->textColor;

	bigF = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, 32, 1 );
	smlF = LSRND_MakeFont( s->font[0] ? s->font : lswnd_layout.globalFont, 18, 1 );

	Q_strncpyz( mainText, st->rtTimerText, sizeof( mainText ) );
	mainFrac[0] = '\0';
	dot = strchr( mainText, '.' );
	if ( dot ) {
		Com_sprintf( mainFrac, sizeof( mainFrac ), ".%s", dot + 1 );
		*dot = '\0';
	}

	/* limit decimals */
	{
		int dec = s->u.realTimer.decimals;
		if ( dec < 0 ) dec = 0;
		if ( dec > 2 ) dec = 2;
		if ( mainFrac[0] == '.' && dec < 2 ) {
			if ( dec == 0 ) mainFrac[0] = '\0';
			else           mainFrac[dec + 1] = '\0';
		}
	}

	SetBkMode( dc, TRANSPARENT );

	old = (HFONT)SelectObject( dc, smlF );
	SetRect( &rc, 0, y + 14, w - ph, y + h - 2 );
	LSRND_DrawTextClr( dc, mainFrac, &rc, DT_SINGLELINE | DT_BOTTOM | DT_RIGHT,
		timerClr, lswnd_layout.textColor );

	{
		SIZE fracSz;
		int mainRight;
		GetTextExtentPoint32A( dc, mainFrac, (int)strlen( mainFrac ), &fracSz );
		mainRight = w - ph - fracSz.cx;
		SelectObject( dc, bigF );
		SetRect( &rc, ph, y + 2, mainRight, y + h - 2 );
		LSRND_DrawTextClr( dc, mainText, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
			timerClr, lswnd_layout.textColor );
	}

	SelectObject( dc, old );
	DeleteObject( bigF );
	DeleteObject( smlF );
	return h;
}

/* ---- 100% Tracker (Secrets + Treasures) ---- */
static int LSRND_Paint100Pct( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 6;
	int pv = ( s->padV >= 0 ) ? s->padV : 2;
	int fntSz = ( s->fontSize > 0 ) ? s->fontSize : lswnd_layout.globalFontSize;
	int rowH = fntSz + 2 * pv + 4;
	int totalRows = 0;
	int segRows = 0;
	int h, ri, curY;
	lsColor_t labelClr, valClr, completeClr;
	HFONT fnt, old;
	RECT rc;
	char valBuf[64];
	int secF = st->pctSecretsFound, secT = st->pctSecretsTotal;
	int trsF = st->pctTreasureFound, trsT = st->pctTreasureTotal;
	int showTotal   = s->u.pct.showTotal;
	int showSegment = s->u.pct.showSegment;

	/* If neither is set, default to showing totals */
	if ( !showTotal && !showSegment ) showTotal = 1;

	if ( showTotal ) totalRows = 2;
	if ( showSegment ) segRows = 2; /* two rows: Secrets + Treasures for current segment */
	h = rowH * ( totalRows + segRows );

	if ( s->overrideHeight > 0 ) h = s->overrideHeight;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	labelClr = ( s->u.pct.labelColor.c1 != LSCLR_INHERIT )
		? s->u.pct.labelColor : s->textColor;

	valClr = ( s->u.pct.valueColor.c1 != LSCLR_INHERIT )
		? s->u.pct.valueColor : s->textColor;

	completeClr = ( s->u.pct.completeColor.c1 != LSCLR_INHERIT )
		? s->u.pct.completeColor
		: LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xFFD700 ) );

	fnt = LSRND_CompFont( s, 0 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );
	curY = y;

	/* Per-segment rows: show only the current segment (state==1) */
	if ( showSegment ) {
		int sF = 0, sT = 0, tF = 0, tT = 0;
		const char *segName = "";

		/* Find the current segment row */
		for ( ri = 0; ri < st->numRows; ri++ ) {
			if ( st->rows[ri].state == 1 ) {
				sF = st->rows[ri].segSecretsFound;
				sT = st->rows[ri].segSecretsTotal;
				tF = st->rows[ri].segTreasureFound;
				tT = st->rows[ri].segTreasureTotal;
				segName = st->rows[ri].name;
				break;
			}
		}

		/* Row 1: Secrets (segment) */
		SetRect( &rc, ph, curY + pv, w / 2, curY + rowH - pv );
		LSRND_DrawTextClr( dc, "Secrets", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			labelClr, lswnd_layout.textColor );

		wsprintfA( valBuf, "%d / %d", sF, sT );
		SetRect( &rc, w / 2, curY + pv, w - ph, curY + rowH - pv );
		LSRND_DrawTextClr( dc, valBuf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
			( sT > 0 && sF >= sT ) ? completeClr : valClr, lswnd_layout.textColor );
		curY += rowH;

		/* Row 2: Treasures (segment) */
		SetRect( &rc, ph, curY + pv, w / 2, curY + rowH - pv );
		LSRND_DrawTextClr( dc, "Treasures", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			labelClr, lswnd_layout.textColor );

		wsprintfA( valBuf, "%d / %d", tF, tT );
		SetRect( &rc, w / 2, curY + pv, w - ph, curY + rowH - pv );
		LSRND_DrawTextClr( dc, valBuf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
			( tT > 0 && tF >= tT ) ? completeClr : valClr, lswnd_layout.textColor );
		curY += rowH;
	}

	/* Total rows */
	if ( showTotal ) {
		/* Row: Secrets */
		SetRect( &rc, ph, curY + pv, w / 2, curY + rowH - pv );
		LSRND_DrawTextClr( dc, "Secrets", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			labelClr, lswnd_layout.textColor );

		wsprintfA( valBuf, "%d / %d", secF, secT );
		SetRect( &rc, w / 2, curY + pv, w - ph, curY + rowH - pv );
		LSRND_DrawTextClr( dc, valBuf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
			( secT > 0 && secF >= secT ) ? completeClr : valClr, lswnd_layout.textColor );
		curY += rowH;

		/* Row: Treasures */
		SetRect( &rc, ph, curY + pv, w / 2, curY + rowH - pv );
		LSRND_DrawTextClr( dc, "Treasures", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			labelClr, lswnd_layout.textColor );

		wsprintfA( valBuf, "%d / %d", trsF, trsT );
		SetRect( &rc, w / 2, curY + pv, w - ph, curY + rowH - pv );
		LSRND_DrawTextClr( dc, valBuf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
			( trsT > 0 && trsF >= trsT ) ? completeClr : valClr, lswnd_layout.textColor );
		curY += rowH;
	}

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

/* ---- Ghost Segment Time ---- */
static int LSRND_PaintGhostSegTime( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 6;
	int pv = ( s->padV >= 0 ) ? s->padV : 2;
	int fntSz = ( s->fontSize > 0 ) ? s->fontSize : lswnd_layout.globalFontSize;
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : ( fntSz + 2 * pv + 4 );
	lsColor_t labelClr, valClr;
	HFONT fnt, old;
	RECT rc;
	const char *label = "Ghost Seg";
	const char *value;
	char timeBuf[32];

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	labelClr = ( s->u.ghostSegTimer.labelColor.c1 != LSCLR_INHERIT )
		? s->u.ghostSegTimer.labelColor : s->textColor;

	valClr = ( s->u.ghostSegTimer.timerColor.c1 != LSCLR_INHERIT )
		? s->u.ghostSegTimer.timerColor : s->textColor;

	if ( st->ghostSegMs >= 0 ) {
		LSRND_FormatTime( st->ghostSegMs, timeBuf, sizeof( timeBuf ) );
		value = timeBuf;
	} else {
		value = "-";
	}

	fnt = LSRND_CompFont( s, 0 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );

	SetRect( &rc, ph, y + pv, w / 2, y + h - pv );
	LSRND_DrawTextClr( dc, label, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
		labelClr, lswnd_layout.textColor );

	SetRect( &rc, w / 2, y + pv, w - ph, y + h - pv );
	LSRND_DrawTextClr( dc, value, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
		valClr, lswnd_layout.textColor );

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

static int LSRND_PaintSegTimer( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 4;
	int pv = ( s->padV >= 0 ) ? s->padV : 2;
	int fntSz = ( s->fontSize > 0 ) ? s->fontSize : lswnd_layout.globalFontSize;
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : ( fntSz + 4 + 2 * pv );
	lsColor_t timerClr;
	HFONT fnt, old;
	RECT rc;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	/* pick colour: delta+ override or normal */
	if ( s->u.segTimer.colorOnDeltaPlus && st->active && st->curRow >= 0
		&& st->rows[st->curRow].liveDeltaMs != (int)0x80000000
		&& st->rows[st->curRow].liveDeltaMs > 0 )
		timerClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
	else
		timerClr = ( s->u.segTimer.timerColor.c1 != LSCLR_INHERIT )
			? s->u.segTimer.timerColor : s->textColor;

	fnt = LSRND_CompFont( s, 4 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );
	SetRect( &rc, ph, y + pv, w - ph, y + h - pv );
	LSRND_DrawTextClr( dc, st->segTimerText, &rc,
		DT_SINGLELINE | DT_VCENTER | LSRND_AlignFlag( s->alignment ),
		timerClr, lswnd_layout.textColor );

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

/* ---- Generic info row (PrevSeg, SoB, BPT, PTS, Comparison) -------- */
static int LSRND_PaintInfoRow( HDC dc, int y, int w, const lsCompSettings_t *s,
	lsCompType_t type, const char *label, const char *value,
	int isBehind, int isGold ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 6;
	int pv = ( s->padV >= 0 ) ? s->padV : 2;
	int fntSz = ( s->fontSize > 0 ) ? s->fontSize : lswnd_layout.globalFontSize;
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : ( fntSz + 2 * pv + 4 );
	const lsInfoSettings_t *inf = LSLAY_GetInfoConst( s, type );
	lsColor_t labelClr, valClr;
	HFONT fnt, old;
	RECT rc;
	char tbuf[24];

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	/* use custom label if set */
	if ( inf && inf->label[0] )
		label = inf->label;

	fnt = LSRND_CompFont( s, -1 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );

	labelClr = inf ? inf->labelColor : s->textColor;
	if ( inf && inf->valueColor.c1 != LSCLR_INHERIT )
		valClr = inf->valueColor;
	else if ( isGold )
		valClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
	else if ( isBehind )
		valClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
	else
		valClr = s->textColor;

	/* truncate value by accuracy; show "-" when empty */
	if ( !value[0] ) value = "-";
	if ( inf && value[0] && value[0] != '-' ) {
		LSRND_TruncTime( tbuf, sizeof( tbuf ), value, inf->accuracy );
		value = tbuf;
	}

	/* label left */
	SetRect( &rc, ph, y + pv, w / 2, y + h - pv );
	LSRND_DrawTextClr( dc, label, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
		labelClr, lswnd_layout.textColor );

	/* value right */
	SetRect( &rc, w / 2, y + pv, w - ph, y + h - pv );
	LSRND_DrawTextClr( dc, value, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
		valClr, lswnd_layout.textColor );

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

/* ---- Separator ----------------------------------------------------- */
static int LSRND_PaintSeparator( HDC dc, int y, int w, const lsCompSettings_t *s ) {
	int h = s->u.separator.height > 0 ? s->u.separator.height : 2;
	COLORREF c;
	unsigned bgRGB = LSCLR_Resolve( lswnd_layout.bgColor, 0x111111 );
	if ( s->u.separator.color.c1 != LSCLR_INHERIT )
		c = LSWND_CLR( s->u.separator.color.c1 );
	else
		c = LSRND_SepClr( bgRGB );
	LSRND_FillRC( dc, 0, y, w, h, c );
	return h;
}

/* ---- Custom Text --------------------------------------------------- */
static int LSRND_PaintText( HDC dc, int y, int w, const lsCompSettings_t *s ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 6;
	int pv = ( s->padV >= 0 ) ? s->padV : 0;
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : 22;
	HFONT fnt, old;
	RECT rc;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

	fnt = LSRND_CompFont( s, 0 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );
	SetRect( &rc, ph, y + pv, w - ph, y + h - pv );
	LSRND_DrawTextClr( dc, s->u.text.text, &rc,
		DT_SINGLELINE | DT_VCENTER | LSRND_AlignFlag( s->alignment ),
		s->textColor, lswnd_layout.textColor );

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

/* ---- Blank Space --------------------------------------------------- */
static int LSRND_PaintBlankSpace( HDC dc, int y, int w, const lsCompSettings_t *s ) {
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : s->u.blankSpace.height;
	if ( h < 1 ) h = 1;
	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );
	return h;
}

/* ---- Header -------------------------------------------------------- */
static int LSRND_PaintHeader( HDC dc, int y, int w, const lsCompSettings_t *s ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 6;
	int pv = ( s->padV >= 0 ) ? s->padV : 3;
	int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : 24;
	HFONT fnt, old;
	RECT rc;

	if ( s->bgColor.c1 != LSCLR_INHERIT )
		LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.headerBg.c1 );

	fnt = LSRND_CompFont( s, 0 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );
	SetRect( &rc, ph, y + pv, w - ph, y + h - pv );
	LSRND_DrawTextClr( dc, s->u.header.text, &rc,
		DT_SINGLELINE | DT_VCENTER | LSRND_AlignFlag( s->alignment ),
		s->textColor, lswnd_layout.textColor );

	if ( s->u.header.showLine ) {
		LSRND_FillRC( dc, 0, y + h - 2, w, 2,
			LSWND_CLR( LSCLR_Resolve( lswnd_layout.accentColor, 0x2694E0 ) ) );
	}

	SelectObject( dc, old );
	DeleteObject( fnt );
	return h;
}

/* ---- Best Segments ------------------------------------------------- */
static int LSRND_PaintBestSegments( HDC dc, int y, int w, const lsCompSettings_t *s, const lsWndState_t *st ) {
	int ph = ( s->padH >= 0 ) ? s->padH : 6;
	int pv = ( s->padV >= 0 ) ? s->padV : 2;
	int fntSz = ( s->fontSize > 0 ) ? s->fontSize : lswnd_layout.globalFontSize;
	int rowH = fntSz + 2 * pv + 4;
	int totalH = 0;
	int i;
	int cumDeltaMs = 0;        /* running sum of segment deltas */
	int hasCumDelta = 0;       /* at least one valid delta seen */
	HFONT fnt, old;
	RECT rc;
	lsColor_t textClr;
	unsigned bgRGB = LSCLR_Resolve( lswnd_layout.bgColor, 0x111111 );
	COLORREF sepC = LSRND_SepClr( bgRGB );

	if ( st->numBestSegs <= 0 ) return 0;

	textClr = ( s->textColor.c1 != LSCLR_INHERIT ) ? s->textColor : lswnd_layout.textColor;

	fnt = LSRND_CompFont( s, -1 );
	old = (HFONT)SelectObject( dc, fnt );
	SetBkMode( dc, TRANSPARENT );

	/* always accumulate cumulative delta */
	for ( i = 0; i < st->numBestSegs && i < LSWND_MAX_ROWS; i++ ) {
		if ( st->bestSegs[i].state == 2 && st->bestSegs[i].deltaMs != (int)0x80000000 ) {
			cumDeltaMs += st->bestSegs[i].deltaMs;
			hasCumDelta = 1;
		}
	}

	/* compact mode: show only "Best Segments" label + total delta */
	if ( !s->u.bestSegments.showSegments ) {
		int h = ( s->overrideHeight > 0 ) ? s->overrideHeight : rowH;

		if ( s->bgColor.c1 != LSCLR_INHERIT )
			LSRND_FillBG( dc, 0, y, w, h, s->bgColor, lswnd_layout.bgColor.c1 );

		/* label left */
		SetRect( &rc, ph, y + pv, w / 2, y + h - pv );
		LSRND_DrawTextClr( dc, "Best Segments", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			textClr, lswnd_layout.textColor );

		/* total delta right */
		if ( hasCumDelta ) {
			char cumBuf[32];
			lsColor_t cumClr;
			LSRND_FormatDelta( cumDeltaMs, cumBuf, sizeof( cumBuf ) );
			if ( cumDeltaMs > 0 )
				cumClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
			else if ( cumDeltaMs < 0 )
				cumClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
			else
				cumClr = textClr;
			SetRect( &rc, w / 2, y + pv, w - ph, y + h - pv );
			LSRND_DrawTextClr( dc, cumBuf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
				cumClr, lswnd_layout.textColor );
		} else {
			SetRect( &rc, w / 2, y + pv, w - ph, y + h - pv );
			LSRND_DrawTextClr( dc, "-", &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
				textClr, lswnd_layout.textColor );
		}

		SelectObject( dc, old );
		DeleteObject( fnt );
		return h;
	}

	/* expanded mode: show all individual segments */
	for ( i = 0; i < st->numBestSegs && i < LSWND_MAX_ROWS; i++ ) {
		int ry = y + totalH;

		if ( s->bgColor.c1 != LSCLR_INHERIT )
			LSRND_FillBG( dc, 0, ry, w, rowH, s->bgColor, lswnd_layout.bgColor.c1 );

		/* segment name left */
		SetRect( &rc, ph, ry + pv, w * 45 / 100, ry + rowH - pv );
		LSRND_DrawTextClr( dc, st->bestSegs[i].name, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			textClr, lswnd_layout.textColor );

		/* gold time center-right */
		{
			const char *valText = st->bestSegs[i].time[0] ? st->bestSegs[i].time : "-";
			SetRect( &rc, w * 45 / 100, ry + pv, w * 72 / 100, ry + rowH - pv );
			LSRND_DrawTextClr( dc, valText, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
				textClr, lswnd_layout.textColor );
		}

		/* delta vs gold (far right, colored) - only for completed splits */
		if ( st->bestSegs[i].state == 2 && st->bestSegs[i].deltaMs != (int)0x80000000 ) {
			lsColor_t dc2;
			if ( st->bestSegs[i].isGold ) {
				dc2 = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.goldColor, 0xD8AF2C ) );
			} else if ( st->bestSegs[i].deltaMs > 0 ) {
				dc2 = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
			} else {
				dc2 = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
			}
			SetRect( &rc, w * 72 / 100, ry + pv, w - ph, ry + rowH - pv );
			LSRND_DrawTextClr( dc, st->bestSegs[i].delta, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
				dc2, lswnd_layout.textColor );
		} else if ( st->bestSegs[i].state == 2 ) {
			/* completed but no gold to compare */
			SetRect( &rc, w * 72 / 100, ry + pv, w - ph, ry + rowH - pv );
			LSRND_DrawTextClr( dc, "-", &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
				textClr, lswnd_layout.textColor );
		}

		totalH += rowH;

		/* thin separator */
		if ( i < st->numBestSegs - 1 ) {
			LSRND_Sep( dc, ph, y + totalH, w - 2 * ph, sepC );
		}
	}

	/* cumulative delta summary row */
	if ( hasCumDelta ) {
		char cumBuf[32];
		lsColor_t cumClr;
		int ry = y + totalH;

		LSRND_Sep( dc, 0, ry, w, sepC );
		totalH += 2;
		ry += 2;

		if ( s->bgColor.c1 != LSCLR_INHERIT )
			LSRND_FillBG( dc, 0, ry, w, rowH, s->bgColor, lswnd_layout.bgColor.c1 );

		LSRND_FormatDelta( cumDeltaMs, cumBuf, sizeof( cumBuf ) );
		if ( cumDeltaMs > 0 ) {
			cumClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.behindColor, 0xCC3600 ) );
		} else if ( cumDeltaMs < 0 ) {
			cumClr = LSCLR_MakePlain( LSCLR_Resolve( lswnd_layout.aheadColor, 0x00CC36 ) );
		} else {
			cumClr = textClr;
		}

		SetRect( &rc, ph, ry + pv, w * 45 / 100, ry + rowH - pv );
		LSRND_DrawTextClr( dc, "Total", &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT,
			textClr, lswnd_layout.textColor );

		SetRect( &rc, w * 72 / 100, ry + pv, w - ph, ry + rowH - pv );
		LSRND_DrawTextClr( dc, cumBuf, &rc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT,
			cumClr, lswnd_layout.textColor );

		totalH += rowH;
	}

	SelectObject( dc, old );
	DeleteObject( fnt );
	return totalH;
}

/* =====================================================================
   Main paint  (called per WM_PAINT from window thread)
   ===================================================================== */

static void LSWND_Paint( HDC dc, int w, int h ) {
	HDC memDC;
	HBITMAP memBmp, oldBmp;
	lsWndState_t st;
	unsigned bgRGB;
	COLORREF bgCR;
	int y, i;
	int layered = lswnd_layout.transparent;
	int physW = w, physH = h;

	if ( layered ) {
		/* paint directly to the 32-bit DIB DC (caller manages the buffer) */
		memDC = dc;
		memBmp = NULL;
		oldBmp = NULL;
	} else {
		memDC  = CreateCompatibleDC( dc );
		memBmp = CreateCompatibleBitmap( dc, w, h );
		oldBmp = (HBITMAP)SelectObject( memDC, memBmp );
	}

	/* Apply DPI scaling via world transform so all drawing
	   (fills, text, fonts) is automatically scaled to native
	   resolution.  Painters work in 96-DPI logical coords. */
	if ( lswnd_dpi != 96 ) {
		XFORM xf;
		float s = (float)lswnd_dpi / 96.0f;
		SetGraphicsMode( memDC, GM_ADVANCED );
		xf.eM11 = s;  xf.eM12 = 0;
		xf.eM21 = 0;  xf.eM22 = s;
		xf.eDx  = 0;  xf.eDy  = 0;
		SetWorldTransform( memDC, &xf );
		w = MulDiv( physW, 96, lswnd_dpi );
		h = MulDiv( physH, 96, lswnd_dpi );
	}

	/* snapshot volatile state */
	st = lswnd_state;

	/* ---- recompute delta / isBehind / liveDeltaMs / splitTime for window
	   compareAgainst (may differ from in-game ls_compare cvar) ---- */
	{
		/* determine effective compareAgainst: splits override > global */
		int effectiveCompare = lswnd_layout.compareAgainst;
		for ( i = 0; i < lswnd_layout.numComponents; i++ ) {
			if ( lswnd_layout.components[i].type == LSCOMP_SPLITS
				&& lswnd_layout.components[i].s.u.splits.compareAgainst != -1 ) {
				effectiveCompare = lswnd_layout.components[i].s.u.splits.compareAgainst;
				break;
			}
		}

		for ( i = 0; i < st.numRows; i++ ) {
			int cmpCum = LSRND_CompareCum( st.rows[i].pbCumMs, st.rows[i].bestCumMs, st.rows[i].avgCumMs, effectiveCompare );

			/* completed rows: always show actual cumulative time in splitTime;
			   recompute delta & isBehind for the window's comparison mode.
			   Re-format splitTime from the authoritative cumTimeMs to guard
			   against partial volatile-copy races between threads. */
			if ( st.rows[i].cumTimeMs > 0 && st.rows[i].state != 1 ) {
				LSRND_FormatTime( st.rows[i].cumTimeMs, (char *)st.rows[i].splitTime,
					sizeof( st.rows[i].splitTime ) );
				if ( cmpCum > 0 ) {
					int dt = st.rows[i].cumTimeMs - cmpCum;
					LSRND_FormatDelta( dt, (char *)st.rows[i].delta, sizeof( st.rows[i].delta ) );
					st.rows[i].isBehind = ( dt > 0 ) ? 1 : 0;
				} else {
					st.rows[i].delta[0] = '\0';
					st.rows[i].isBehind = 0;
				}
			}

			/* current row: show comparison cumulative (or best-segments
			   fallback) but NOT live time - time only appears once
			   the split is completed (matches in-game HUD behaviour) */
			if ( st.rows[i].state == 1 ) {
				if ( cmpCum > 0 ) {
					LSRND_FormatTime( cmpCum, (char *)st.rows[i].splitTime,
						sizeof( st.rows[i].splitTime ) );
				} else if ( st.rows[i].bestCumMs > 0 ) {
					LSRND_FormatTime( st.rows[i].bestCumMs, (char *)st.rows[i].splitTime,
						sizeof( st.rows[i].splitTime ) );
				} else {
					st.rows[i].splitTime[0] = '\0';
				}
				if ( st.rows[i].cumTimeMs > 0 && cmpCum > 0 ) {
					st.rows[i].liveDeltaMs = st.rows[i].cumTimeMs - cmpCum;
					st.rows[i].isBehind = ( st.rows[i].liveDeltaMs > 0 ) ? 1 : 0;
				}
			}

			/* future rows: show comparison cumulative, fall back to
			   best-segments cumulative (matches in-game HUD behaviour) */
			if ( st.rows[i].state == 0 && st.rows[i].cumTimeMs <= 0 ) {
				if ( cmpCum > 0 ) {
					LSRND_FormatTime( cmpCum, (char *)st.rows[i].splitTime,
						sizeof( st.rows[i].splitTime ) );
				} else if ( st.rows[i].bestCumMs > 0 ) {
					LSRND_FormatTime( st.rows[i].bestCumMs, (char *)st.rows[i].splitTime,
						sizeof( st.rows[i].splitTime ) );
				}
			}
		}
	}

	bgRGB = LSCLR_Resolve( lswnd_layout.bgColor, 0x111111 );
	bgCR  = LSWND_CLR( bgRGB );

	/* fill background (skip when layered - DIB already transparent) */
	if ( !layered ) {
		LSRND_FillBG( memDC, 0, 0, w, h, lswnd_layout.bgColor, 0x111111 );
	}

	y = 0;
	for ( i = 0; i < lswnd_layout.numComponents; i++ ) {
		int ci = lswnd_layout.flipLayout
			? ( lswnd_layout.numComponents - 1 - i )
			: i;
		const lsComponent_t *comp = &lswnd_layout.components[ci];
		int prevY;
		if ( !comp->s.enabled ) continue;

		/* component spacing (extra gap between components) */
		if ( lswnd_layout.componentSpacing > 0 && y > 0 ) {
			if ( !layered )
				LSRND_FillRC( memDC, 0, y, w, lswnd_layout.componentSpacing, bgCR );
			y += lswnd_layout.componentSpacing;
		}

		/* thin accent line between components */
		if ( lswnd_layout.thinAccent && y > 0 && comp->type != LSCOMP_SEPARATOR ) {
			LSRND_FillRC( memDC, 0, y, w, 1,
				LSWND_CLR( LSCLR_Resolve( lswnd_layout.accentColor, 0x2694E0 ) ) );
			y += 1;
		}

		prevY = y;
		switch ( comp->type ) {
		case LSCOMP_TITLE:
			y += LSRND_PaintTitle( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_SPLITS:
			y += LSRND_PaintSplits( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_TIMER:
			y += LSRND_PaintTimer( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_DETAILED_TIMER:
			y += LSRND_PaintDetailedTimer( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_SEG_TIMER:
			y += LSRND_PaintSegTimer( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_PREV_SEGMENT:
			y += LSRND_PaintInfoRow( memDC, y, w, &comp->s, LSCOMP_PREV_SEGMENT,
				st.prevSegLabel, st.prevSegValue, st.prevSegBehind, st.prevSegGold );
			break;
		case LSCOMP_SUM_OF_BEST:
			{
				const lsInfoSettings_t *sobInf = LSLAY_GetInfoConst( &comp->s, LSCOMP_SUM_OF_BEST );
				int sobCmp = sobInf ? sobInf->compareAgainst : -1;
				int sobMs;
				char sobBuf[20];
				const char *sobVal = st.sobText;
				if ( sobCmp == -1 ) sobCmp = lswnd_layout.compareAgainst;
				switch ( sobCmp ) {
				case 0:  sobMs = st.sobPbMs;   break;  /* PB */
				case 2:  sobMs = st.sobAvgMs;  break;  /* Average */
				default: sobMs = st.sobBestMs;  break;  /* Best (default) */
				}
				if ( sobMs >= 0 ) {
					LSRND_FormatTime( sobMs, sobBuf, sizeof( sobBuf ) );
					sobVal = sobBuf;
				} else {
					sobVal = "-";
				}
				y += LSRND_PaintInfoRow( memDC, y, w, &comp->s, LSCOMP_SUM_OF_BEST,
					"Sum of Best", sobVal, 0, 1 );
			}
			break;
		case LSCOMP_BEST_POSSIBLE:
			{
				const lsInfoSettings_t *bptInf = LSLAY_GetInfoConst( &comp->s, LSCOMP_BEST_POSSIBLE );
				int bptCmp = bptInf ? bptInf->compareAgainst : -1;
				int bptMs;
				char bptBuf[20];
				const char *bptVal = st.bptText;
				if ( bptCmp == -1 ) bptCmp = lswnd_layout.compareAgainst;
				switch ( bptCmp ) {
				case 0:  bptMs = st.bptPbMs;   break;  /* PB */
				case 2:  bptMs = st.bptAvgMs;  break;  /* Average */
				default: bptMs = st.bptBestMs;  break;  /* Best (default) */
				}
				if ( bptMs >= 0 ) {
					LSRND_FormatTime( bptMs, bptBuf, sizeof( bptBuf ) );
					bptVal = bptBuf;
				} else {
					bptVal = "-";
				}
				y += LSRND_PaintInfoRow( memDC, y, w, &comp->s, LSCOMP_BEST_POSSIBLE,
					"Best Possible", bptVal, 0, 0 );
			}
			break;
		case LSCOMP_POSSIBLE_SAVE:
			y += LSRND_PaintInfoRow( memDC, y, w, &comp->s, LSCOMP_POSSIBLE_SAVE,
				"Possible Time Save", st.ptsText, 0, 0 );
			break;
		case LSCOMP_COMPARISON:
			y += LSRND_PaintInfoRow( memDC, y, w, &comp->s, LSCOMP_COMPARISON,
				st.compareLabel, st.pbText, 0, 0 );
			break;
		case LSCOMP_SEPARATOR:
			y += LSRND_PaintSeparator( memDC, y, w, &comp->s );
			break;
		case LSCOMP_TEXT:
			y += LSRND_PaintText( memDC, y, w, &comp->s );
			break;
		case LSCOMP_BLANK_SPACE:
			y += LSRND_PaintBlankSpace( memDC, y, w, &comp->s );
			break;
		case LSCOMP_HEADER:
			y += LSRND_PaintHeader( memDC, y, w, &comp->s );
			break;
		case LSCOMP_BEST_SEGMENTS:
			y += LSRND_PaintBestSegments( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_REAL_TIME:
			y += LSRND_PaintRealTime( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_GHOST_SEG_TIME:
			y += LSRND_PaintGhostSegTime( memDC, y, w, &comp->s, &st );
			break;
		case LSCOMP_100PCT:
			y += LSRND_Paint100Pct( memDC, y, w, &comp->s, &st );
			break;
		default:
			break;
		}
	}

	/* update content height for auto-height (convert logical -> physical) */
	lswnd_contentHeight = MulDiv( y, lswnd_dpi, 96 );

	/* fill remaining space (skip when layered) */
	if ( y < h && !layered ) {
		LSRND_FillRC( memDC, 0, y, w, h - y, bgCR );
	}

	/* accent border at the very top (skip if first component is Title with showTopAccent=0) */
	{
		int drawTopAccent = 1;
		for ( i = 0; i < lswnd_layout.numComponents; i++ ) {
			if ( !lswnd_layout.components[i].s.enabled ) continue;
			if ( lswnd_layout.components[i].type == LSCOMP_TITLE
				&& !lswnd_layout.components[i].s.u.title.showTopAccent )
				drawTopAccent = 0;
			break;
		}
		if ( drawTopAccent )
			LSRND_FillRC( memDC, 0, 0, w, 2, LSWND_CLR( LSCLR_Resolve( lswnd_layout.accentColor, 0x2694E0 ) ) );
	}

	/* (Ready overlay removed - standalone exe doesn't need it) */

	/* blit to real DC (skip when layered - caller handles it) */
	if ( !layered ) {
		/* reset world transform before BitBlt so source coords
		   are in device (physical) pixels */
		if ( lswnd_dpi != 96 ) {
			XFORM id = { 1.0f, 0, 0, 1.0f, 0, 0 };
			SetWorldTransform( memDC, &id );
		}
		BitBlt( dc, 0, 0, physW, physH, memDC, 0, 0, SRCCOPY );
		SelectObject( memDC, oldBmp );
		DeleteObject( memBmp );
		DeleteDC( memDC );
	} else if ( lswnd_dpi != 96 ) {
		/* reset world transform on the layered DC too */
		XFORM id = { 1.0f, 0, 0, 1.0f, 0, 0 };
		SetWorldTransform( memDC, &id );
	}
}
