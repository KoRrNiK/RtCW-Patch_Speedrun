/*
===========================================================================
ls_layout.c  --  Layout defaults, JSON I/O, file persistence

Included as part of cl_livesplit_window.c unity build.
All engine headers, Win32 headers, and shared state already available.
===========================================================================
*/

/* ---- lsColor_t helpers -------------------------------------------- */

static lsColor_t LSCLR_MakePlain( unsigned rgb ) {
	lsColor_t c;
	c.mode    = LSCLR_MODE_PLAIN;
	c.c1      = rgb;
	c.c2      = 0;
	c.alpha   = 255;
	c.gradPos = 50;
	return c;
}

static lsColor_t LSCLR_MakeInherit( void ) {
	lsColor_t c;
	c.mode    = LSCLR_MODE_PLAIN;
	c.c1      = LSCLR_INHERIT;
	c.c2      = 0;
	c.alpha   = 255;
	c.gradPos = 50;
	return c;
}

static lsColor_t LSCLR_MakeGradient( int mode, unsigned rgb1, unsigned rgb2, int pos ) {
	lsColor_t c;
	c.mode    = mode;
	c.c1      = rgb1;
	c.c2      = rgb2;
	c.alpha   = 255;
	c.gradPos = ( pos >= 0 && pos <= 100 ) ? pos : 50;
	return c;
}

static unsigned LSCLR_Resolve( lsColor_t clr, unsigned fallback ) {
	return ( clr.c1 == LSCLR_INHERIT ) ? fallback : clr.c1;
}

static unsigned LSWND_HexToRGB( const char *s ) {
	unsigned v = 0;
	int i;
	for ( i = 0; i < 6 && s[i]; i++ ) {
		unsigned d = 0;
		char ch = s[i];
		if      ( ch >= '0' && ch <= '9' ) d = (unsigned)( ch - '0' );
		else if ( ch >= 'A' && ch <= 'F' ) d = (unsigned)( ch - 'A' ) + 10;
		else if ( ch >= 'a' && ch <= 'f' ) d = (unsigned)( ch - 'a' ) + 10;
		v = ( v << 4 ) | d;
	}
	return v;
}

/* ---- Component type tables ---------------------------------------- */

static const char *lsCompTypeNames[LSCOMP_TYPE_COUNT] = {
	"title", "splits", "timer", "detailedTimer", "segTimer",
	"prevSegment", "sumOfBest", "bestPossible", "possibleSave",
	"comparison", "separator", "text", "blankSpace", "header",
	"bestSegments", "realTime", "ghostSegTimer", "100pct"
};

static const char *lsCompTypeLabels[LSCOMP_TYPE_COUNT] = {
	"Title", "Splits", "Timer", "Detailed Timer", "Segment Timer",
	"Previous Segment", "Sum of Best", "Best Possible Time",
	"Possible Time Save", "Comparison", "Separator", "Text",
	"Blank Space", "Header", "Best Segments", "Real Time",
	"Ghost Segment Time", "100% Tracker"
};

static const char *lsColTypeNames[LSCOL_TYPE_COUNT] = {
	"delta", "splitTime", "segTime", "bestSeg", "pbSplit", "deltaBest"
};

static const char *lsColTypeLabels[LSCOL_TYPE_COUNT] = {
	"Delta", "Split Time", "Segment Time", "Best Segment", "Time", "Delta Best"
};

static lsCompType_t LSLAY_TypeFromName( const char *name ) {
	int i;
	for ( i = 0; i < LSCOMP_TYPE_COUNT; i++ )
		if ( !strcmp( name, lsCompTypeNames[i] ) ) return (lsCompType_t)i;
	return LSCOMP_TITLE;
}

/* ---- Info settings helpers ---------------------------------------- */

static lsInfoSettings_t *LSLAY_GetInfoSettings( lsCompSettings_t *s, lsCompType_t type ) {
	switch ( type ) {
	case LSCOMP_PREV_SEGMENT:  return &s->u.prevSeg;
	case LSCOMP_SUM_OF_BEST:   return &s->u.sumOfBest;
	case LSCOMP_BEST_POSSIBLE: return &s->u.bestPossible;
	case LSCOMP_POSSIBLE_SAVE: return &s->u.possibleSave;
	case LSCOMP_COMPARISON:    return &s->u.comparison;
	default: return NULL;
	}
}

static const lsInfoSettings_t *LSLAY_GetInfoConst( const lsCompSettings_t *s, lsCompType_t type ) {
	switch ( type ) {
	case LSCOMP_PREV_SEGMENT:  return &s->u.prevSeg;
	case LSCOMP_SUM_OF_BEST:   return &s->u.sumOfBest;
	case LSCOMP_BEST_POSSIBLE: return &s->u.bestPossible;
	case LSCOMP_POSSIBLE_SAVE: return &s->u.possibleSave;
	case LSCOMP_COMPARISON:    return &s->u.comparison;
	default: return NULL;
	}
}

static int LSLAY_IsInfoType( lsCompType_t type ) {
	return type == LSCOMP_PREV_SEGMENT || type == LSCOMP_SUM_OF_BEST ||
	       type == LSCOMP_BEST_POSSIBLE || type == LSCOMP_POSSIBLE_SAVE ||
	       type == LSCOMP_COMPARISON;
}

/* ---- Default component settings ----------------------------------- */

static void LSLAY_DefaultCompSettings( lsCompType_t type, lsCompSettings_t *s ) {
	lsInfoSettings_t *inf;
	memset( s, 0, sizeof( *s ) );
	s->enabled   = 1;
	s->font[0]   = '\0';
	s->fontSize  = 0;
	s->textColor = LSCLR_MakeInherit();
	s->bgColor   = LSCLR_MakeInherit();
	s->padH      = -1;
	s->padV      = -1;
	s->overrideHeight = 0;
	s->alignment = LSALIGN_LEFT;

	switch ( type ) {
	case LSCOMP_TITLE:
		s->alignment = LSALIGN_CENTER;
		s->u.title.showGameName = 1;
		s->u.title.showCategory = 1;
		s->u.title.showAttempts = 1;
		s->u.title.attemptsOnNewLine = 0;
		s->u.title.showTopAccent = 1;
		s->u.title.showBottomAccent = 1;
		break;
	case LSCOMP_SPLITS:
		s->u.splits.visibleSplits     = 7;
		s->u.splits.columns[0]        = LSCOL_DELTA_BEST;
		s->u.splits.columns[1]        = LSCOL_DELTA;
		s->u.splits.columns[2]        = LSCOL_SPLIT_TIME;
		memset( s->u.splits.colSettings, 0, sizeof( s->u.splits.colSettings ) );
		{ int ci; for ( ci = 0; ci < LSWND_MAX_COLUMNS; ci++ ) {
			s->u.splits.colSettings[ci].textColor = LSCLR_MakeInherit();
			s->u.splits.colSettings[ci].bold = -1;
			s->u.splits.colSettings[ci].beforeColor = LSCLR_MakeInherit();
			s->u.splits.colSettings[ci].currentColor = LSCLR_MakeInherit();
			s->u.splits.colSettings[ci].afterColor = LSCLR_MakeInherit();
		} }
		s->u.splits.numColumns        = 3;
		s->u.splits.showThinSeps      = 1;
		s->u.splits.alwaysShowLast    = 0;
		s->u.splits.lockLastToBottom  = 0;
		s->u.splits.sepBeforeLastSplit = 0;
		s->u.splits.deltaAccuracy     = LSACC_TENTHS;
		s->u.splits.deltaDropDecimals = 1;
		s->u.splits.splitAccuracy     = LSACC_SECONDS;
		s->u.splits.deltaCountdownSec = 10;
		s->u.splits.showHeader        = 1;
		s->u.splits.alternateRows     = 1;
		s->u.splits.shortNames        = 0;
		s->u.splits.currentBg         = LSCLR_MakeGradient( LSCLR_MODE_HGRADIENT, 0x2A3A50, 0x1E2C3C, 50 );
		s->u.splits.beforeCurrentBg   = LSCLR_MakeInherit();
		s->u.splits.currentSplitClr   = LSCLR_MakePlain( 0xFFFFFF );
		s->u.splits.afterCurrentBg    = LSCLR_MakeInherit();
		s->u.splits.liveDeltaColor    = LSCLR_MakeInherit();
		s->u.splits.splitTimeColor    = LSCLR_MakePlain( 0xA0A0A0 );
		s->u.splits.beforeSplitColor  = LSCLR_MakeInherit();
		s->u.splits.headerTextColor   = LSCLR_MakeInherit();
		s->u.splits.nameColor         = LSCLR_MakeInherit();
		s->u.splits.altRowColor       = LSCLR_MakeInherit();
		s->u.splits.rowColor          = LSCLR_MakeInherit();
		s->u.splits.rowSepColor       = LSCLR_MakeInherit();
		/* per-state name styling defaults */
		s->u.splits.beforeNameFont[0] = '\0';
		s->u.splits.beforeNameSize    = 0;
		s->u.splits.beforeNameBold    = -1;
		s->u.splits.beforeNameColor   = LSCLR_MakeInherit();
		s->u.splits.currentNameFont[0] = '\0';
		s->u.splits.currentNameSize   = 0;
		s->u.splits.currentNameBold   = -1;
		s->u.splits.afterNameFont[0]  = '\0';
		s->u.splits.afterNameSize     = 0;
		s->u.splits.afterNameBold     = -1;
		s->u.splits.afterNameColor    = LSCLR_MakeInherit();
		s->u.splits.compareAgainst    = -1;
		break;
	case LSCOMP_TIMER:
		s->u.timer.decimals     = 2;
		s->u.timer.timingMethod = LSTIME_GAME_TIME;
		s->u.timer.aheadColor   = LSCLR_MakeInherit();
		s->u.timer.behindColor  = LSCLR_MakeInherit();
		s->u.timer.goldColor    = LSCLR_MakeInherit();
		s->u.timer.colorOnDeltaPlus = 0;
		s->u.timer.compareAgainst   = -1;
		break;
	case LSCOMP_DETAILED_TIMER:
		s->u.detailedTimer.mainDecimals    = 2;
		s->u.detailedTimer.compDecimals    = 1;
		s->u.detailedTimer.timingMethod    = LSTIME_GAME_TIME;
		s->u.detailedTimer.showPB          = 1;
		s->u.detailedTimer.showBest        = 1;
		s->u.detailedTimer.showSegTimer    = 1;
		s->u.detailedTimer.mainFontSize    = 0;
		s->u.detailedTimer.compFontSize    = 0;
		s->u.detailedTimer.mainAheadColor  = LSCLR_MakeInherit();
		s->u.detailedTimer.mainBehindColor = LSCLR_MakeInherit();
		s->u.detailedTimer.mainGoldColor   = LSCLR_MakeInherit();
		s->u.detailedTimer.pbColor         = LSCLR_MakeInherit();
		s->u.detailedTimer.bestColor       = LSCLR_MakeInherit();
		s->u.detailedTimer.labelColor      = LSCLR_MakePlain( 0x808080 );
		s->u.detailedTimer.segNameColor    = LSCLR_MakeInherit();
		s->u.detailedTimer.segTimerColor   = LSCLR_MakeInherit();
		s->u.detailedTimer.segFontSize     = 0;
		s->u.detailedTimer.colorOnDeltaPlus = 0;
		s->u.detailedTimer.compareAgainst   = -1;
		break;
	case LSCOMP_SEG_TIMER:
		s->u.segTimer.decimals     = 2;
		s->u.segTimer.timingMethod = LSTIME_GAME_TIME;
		s->u.segTimer.timerColor   = LSCLR_MakeInherit();
		s->u.segTimer.colorOnDeltaPlus = 0;
		s->u.segTimer.compareAgainst   = -1;
		break;
	case LSCOMP_BEST_SEGMENTS:
		s->u.bestSegments.showSegments = 0;
		break;
	case LSCOMP_REAL_TIME:
		s->u.realTimer.decimals   = 2;
		s->u.realTimer.timerColor = LSCLR_MakeInherit();
		break;
	case LSCOMP_GHOST_SEG_TIME:
		s->u.ghostSegTimer.decimals   = 2;
		s->u.ghostSegTimer.timerColor = LSCLR_MakePlain( 0x808080 );
		s->u.ghostSegTimer.labelColor = LSCLR_MakeInherit();
		break;
	case LSCOMP_100PCT:
		s->u.pct.labelColor    = LSCLR_MakeInherit();
		s->u.pct.valueColor    = LSCLR_MakeInherit();
		s->u.pct.completeColor = LSCLR_MakeInherit();
		s->u.pct.showTotal     = 1;
		s->u.pct.showSegment   = 0;
		break;
	case LSCOMP_PREV_SEGMENT:
	case LSCOMP_SUM_OF_BEST:
	case LSCOMP_BEST_POSSIBLE:
	case LSCOMP_POSSIBLE_SAVE:
	case LSCOMP_COMPARISON:
		inf = LSLAY_GetInfoSettings( s, type );
		if ( inf ) {
			inf->accuracy     = LSACC_TENTHS;
			inf->dropDecimals = 1;
			inf->showLive     = ( type == LSCOMP_PREV_SEGMENT ) ? 1 : 0;
			inf->compareAgainst = -1;
			inf->valueColor   = LSCLR_MakeInherit();
			inf->labelColor   = LSCLR_MakeInherit();
			inf->label[0]     = '\0';
		}
		break;
	case LSCOMP_SEPARATOR:
		s->u.separator.color  = LSCLR_MakeInherit();
		s->u.separator.height = 2;
		break;
	case LSCOMP_TEXT:
		Q_strncpyz( s->u.text.text, "Custom Text", sizeof( s->u.text.text ) );
		break;
	case LSCOMP_BLANK_SPACE:
		s->u.blankSpace.height = 24;
		break;
	case LSCOMP_HEADER:
		Q_strncpyz( s->u.header.text, "Header", sizeof( s->u.header.text ) );
		s->u.header.showLine = 1;
		break;
	default:
		break;
	}
}

/* ---- LS_LayoutSetDefault ------------------------------------------ */

void LS_LayoutSetDefault( lsLayout_t *l ) {
	static const lsCompType_t defComps[] = {
		LSCOMP_TITLE, LSCOMP_SPLITS, LSCOMP_DETAILED_TIMER,
		LSCOMP_PREV_SEGMENT,
		LSCOMP_SUM_OF_BEST, LSCOMP_BEST_POSSIBLE,
		LSCOMP_POSSIBLE_SAVE, LSCOMP_COMPARISON,
		LSCOMP_REAL_TIME
	};
	int i, n;
	if ( !l ) return;
	memset( l, 0, sizeof( *l ) );
	l->version       = 2;
	l->alwaysOnTop   = 1;
	l->transparent   = 0;
	l->opacity       = 255;
	l->compareAgainst = 0;
	l->windowWidth   = 400;
	Q_strncpyz( l->globalFont, "Segoe UI", sizeof( l->globalFont ) );
	l->globalFontSize = 13;
	l->textShadow    = 0;
	l->thinAccent    = 0;
	l->globalBold    = 0;
	l->lockResize    = 0;
	l->flipLayout    = 0;
	l->componentSpacing = 0;
	l->bgColor     = LSCLR_MakeGradient( LSCLR_MODE_VGRADIENT, 0x141414, 0x0A0A0A, 50 );
	l->headerBg    = LSCLR_MakeGradient( LSCLR_MODE_VGRADIENT, 0x1A1A1A, 0x111111, 50 );
	l->accentColor = LSCLR_MakePlain( 0x00AAFF );
	l->textColor   = LSCLR_MakePlain( 0xDCDCDC );
	l->aheadColor  = LSCLR_MakePlain( 0x30D158 );
	l->behindColor = LSCLR_MakePlain( 0xFF4444 );
	l->goldColor   = LSCLR_MakePlain( 0xFFD700 );
	n = sizeof( defComps ) / sizeof( defComps[0] );
	l->numComponents = n;
	for ( i = 0; i < n; i++ ) {
		l->components[i].type = defComps[i];
		LSLAY_DefaultCompSettings( defComps[i], &l->components[i].s );
	}
}

/* =====================================================================
   JSON Parser
   ===================================================================== */

static void JP_SkipWS( const char **pp ) {
	while ( **pp == ' ' || **pp == '\t' || **pp == '\r' || **pp == '\n' ) ( *pp )++;
}

static int JP_Match( const char **pp, char ch ) {
	JP_SkipWS( pp );
	if ( **pp == ch ) { ( *pp )++; return 1; }
	return 0;
}

static int JP_ReadStr( const char **pp, char *buf, int sz ) {
	int i = 0;
	JP_SkipWS( pp );
	if ( **pp != '"' ) return 0;
	( *pp )++;
	while ( **pp && **pp != '"' ) {
		if ( **pp == '\\' ) { ( *pp )++; if ( !**pp ) break; }
		if ( i < sz - 1 ) buf[i++] = **pp;
		( *pp )++;
	}
	buf[i] = '\0';
	if ( **pp == '"' ) ( *pp )++;
	return 1;
}

static int JP_ReadInt( const char **pp, int *out ) {
	int neg = 0, v = 0;
	JP_SkipWS( pp );
	if ( **pp == '-' ) { neg = 1; ( *pp )++; }
	if ( **pp < '0' || **pp > '9' ) return 0;
	while ( **pp >= '0' && **pp <= '9' ) { v = v * 10 + ( **pp - '0' ); ( *pp )++; }
	*out = neg ? -v : v;
	return 1;
}

static int JP_ReadUInt( const char **pp, unsigned *out ) {
	unsigned v = 0;
	JP_SkipWS( pp );
	if ( **pp < '0' || **pp > '9' ) return 0;
	while ( **pp >= '0' && **pp <= '9' ) { v = v * 10 + (unsigned)( **pp - '0' ); ( *pp )++; }
	*out = v;
	return 1;
}

static void JP_SkipValue( const char **pp ) {
	int depth = 0;
	JP_SkipWS( pp );
	if ( **pp == '"' ) {
		( *pp )++;
		while ( **pp && **pp != '"' ) { if ( **pp == '\\' ) ( *pp )++; ( *pp )++; }
		if ( **pp == '"' ) ( *pp )++;
		return;
	}
	if ( **pp == '{' || **pp == '[' ) {
		char open = **pp, close = ( open == '{' ) ? '}' : ']';
		( *pp )++; depth = 1;
		while ( **pp && depth > 0 ) {
			if ( **pp == open ) depth++;
			else if ( **pp == close ) depth--;
			else if ( **pp == '"' ) {
				( *pp )++;
				while ( **pp && **pp != '"' ) { if ( **pp == '\\' ) ( *pp )++; ( *pp )++; }
			}
			( *pp )++;
		}
		return;
	}
	/* number / boolean / null */
	while ( **pp && **pp != ',' && **pp != '}' && **pp != ']' ) ( *pp )++;
}

/* Read lsColor_t: "RRGGBB" string | {"mode":N,"c1":"...","c2":"...","alpha":N} */
static int JP_ReadColor( const char **pp, lsColor_t *clr ) {
	char hex[16];
	JP_SkipWS( pp );
	if ( **pp == '"' ) {
		JP_ReadStr( pp, hex, sizeof( hex ) );
		clr->mode    = LSCLR_MODE_PLAIN;
		clr->c2      = 0;
		clr->alpha   = 255;
		clr->gradPos = 50;
		clr->c1      = hex[0] ? LSWND_HexToRGB( hex ) : LSCLR_INHERIT;
		return 1;
	}
	if ( **pp == '{' ) {
		char key[32], sv[16];
		clr->mode    = LSCLR_MODE_PLAIN;
		clr->c1      = LSCLR_INHERIT;
		clr->c2      = 0;
		clr->alpha   = 255;
		clr->gradPos = 50;
		JP_Match( pp, '{' );
		while ( **pp && **pp != '}' ) {
			if ( !JP_ReadStr( pp, key, sizeof( key ) ) ) break;
			JP_Match( pp, ':' );
			if      ( !strcmp( key, "mode" ) )    JP_ReadInt( pp, &clr->mode );
			else if ( !strcmp( key, "c1" ) )      { JP_ReadStr( pp, sv, sizeof( sv ) ); clr->c1 = sv[0] ? LSWND_HexToRGB( sv ) : LSCLR_INHERIT; }
			else if ( !strcmp( key, "c2" ) )      { JP_ReadStr( pp, sv, sizeof( sv ) ); clr->c2 = sv[0] ? LSWND_HexToRGB( sv ) : 0; }
			else if ( !strcmp( key, "alpha" ) )   JP_ReadInt( pp, &clr->alpha );
			else if ( !strcmp( key, "gradPos" ) ) JP_ReadInt( pp, &clr->gradPos );
			else JP_SkipValue( pp );
			JP_Match( pp, ',' );
		}
		JP_Match( pp, '}' );
		return 1;
	}
	/* fallback: numeric literal -> plain black */
	{ unsigned v = 0; JP_ReadUInt( pp, &v ); *clr = LSCLR_MakePlain( v ); }
	return 1;
}

/* Parse one component object */
static int JP_ParseComponent( const char **pp, lsComponent_t *comp ) {
	char key[64], sv[128];
	const char *start;
	lsCompSettings_t *s;
	lsInfoSettings_t *inf;
	int iv;

	JP_SkipWS( pp );
	if ( **pp != '{' ) return 0;

	/* --- first pass: find "type" --- */
	start = *pp;
	JP_Match( pp, '{' );
	comp->type = LSCOMP_TITLE;
	while ( **pp && **pp != '}' ) {
		if ( !JP_ReadStr( pp, key, sizeof( key ) ) ) break;
		JP_Match( pp, ':' );
		if ( !strcmp( key, "type" ) ) {
			JP_ReadStr( pp, sv, sizeof( sv ) );
			comp->type = LSLAY_TypeFromName( sv );
		} else {
			JP_SkipValue( pp );
		}
		JP_Match( pp, ',' );
	}

	/* --- init defaults for this type --- */
	LSLAY_DefaultCompSettings( comp->type, &comp->s );
	s = &comp->s;

	/* --- second pass: all fields --- */
	*pp = start;
	JP_Match( pp, '{' );
	while ( **pp && **pp != '}' ) {
		if ( !JP_ReadStr( pp, key, sizeof( key ) ) ) break;
		JP_Match( pp, ':' );

		/* -- common fields -- */
		if      ( !strcmp( key, "type" ) )      { JP_SkipValue( pp ); }
		else if ( !strcmp( key, "enabled" ) )   JP_ReadInt( pp, &s->enabled );
		else if ( !strcmp( key, "font" ) )      JP_ReadStr( pp, s->font, sizeof( s->font ) );
		else if ( !strcmp( key, "fontSize" ) )  JP_ReadInt( pp, &s->fontSize );
		else if ( !strcmp( key, "bold" ) )      JP_ReadInt( pp, &s->bold );
		else if ( !strcmp( key, "textColor" ) ) JP_ReadColor( pp, &s->textColor );
		else if ( !strcmp( key, "bgColor" ) )   JP_ReadColor( pp, &s->bgColor );
		else if ( !strcmp( key, "padding" ) )   { int pv; JP_ReadInt( pp, &pv ); s->padH = pv; s->padV = pv; }
		else if ( !strcmp( key, "padH" ) )      JP_ReadInt( pp, &s->padH );
		else if ( !strcmp( key, "padV" ) )      JP_ReadInt( pp, &s->padV );
		else if ( !strcmp( key, "overrideHeight" ) ) JP_ReadInt( pp, &s->overrideHeight );
		else if ( !strcmp( key, "alignment" ) ) JP_ReadInt( pp, &s->alignment );

		/* -- Title -- */
		else if ( comp->type == LSCOMP_TITLE ) {
			if      ( !strcmp( key, "showGameName" ) )  JP_ReadInt( pp, &s->u.title.showGameName );
			else if ( !strcmp( key, "showCategory" ) )  JP_ReadInt( pp, &s->u.title.showCategory );
			else if ( !strcmp( key, "showAttempts" ) )  JP_ReadInt( pp, &s->u.title.showAttempts );
			else if ( !strcmp( key, "attemptsOnNewLine" ) ) JP_ReadInt( pp, &s->u.title.attemptsOnNewLine );
			else if ( !strcmp( key, "showTopAccent" ) ) JP_ReadInt( pp, &s->u.title.showTopAccent );
			else if ( !strcmp( key, "showBottomAccent" ) ) JP_ReadInt( pp, &s->u.title.showBottomAccent );
			else JP_SkipValue( pp );
		}

		/* -- Splits -- */
		else if ( comp->type == LSCOMP_SPLITS ) {
			if      ( !strcmp( key, "visibleSplits" ) )      JP_ReadInt( pp, &s->u.splits.visibleSplits );
			else if ( !strcmp( key, "showThinSeps" ) )       JP_ReadInt( pp, &s->u.splits.showThinSeps );
			else if ( !strcmp( key, "alwaysShowLast" ) )     JP_ReadInt( pp, &s->u.splits.alwaysShowLast );
			else if ( !strcmp( key, "lockLastToBottom" ) )   JP_ReadInt( pp, &s->u.splits.lockLastToBottom );
			else if ( !strcmp( key, "sepBeforeLastSplit" ) ) JP_ReadInt( pp, &s->u.splits.sepBeforeLastSplit );
			else if ( !strcmp( key, "deltaAccuracy" ) )      JP_ReadInt( pp, &s->u.splits.deltaAccuracy );
			else if ( !strcmp( key, "deltaDropDecimals" ) )  JP_ReadInt( pp, &s->u.splits.deltaDropDecimals );
			else if ( !strcmp( key, "splitAccuracy" ) )      JP_ReadInt( pp, &s->u.splits.splitAccuracy );
			else if ( !strcmp( key, "deltaCountdownSec" ) )  JP_ReadInt( pp, &s->u.splits.deltaCountdownSec );
			else if ( !strcmp( key, "showHeader" ) )          JP_ReadInt( pp, &s->u.splits.showHeader );
			else if ( !strcmp( key, "alternateRows" ) )        JP_ReadInt( pp, &s->u.splits.alternateRows );
			else if ( !strcmp( key, "shortNames" ) )            JP_ReadInt( pp, &s->u.splits.shortNames );
			else if ( !strcmp( key, "currentBg" ) )          JP_ReadColor( pp, &s->u.splits.currentBg );
			else if ( !strcmp( key, "beforeCurrentBg" ) )    JP_ReadColor( pp, &s->u.splits.beforeCurrentBg );
			else if ( !strcmp( key, "currentSplitClr" ) )    JP_ReadColor( pp, &s->u.splits.currentSplitClr );
			else if ( !strcmp( key, "afterCurrentBg" ) )     JP_ReadColor( pp, &s->u.splits.afterCurrentBg );
			else if ( !strcmp( key, "liveDeltaColor" ) )     JP_ReadColor( pp, &s->u.splits.liveDeltaColor );
			else if ( !strcmp( key, "splitTimeColor" ) )     JP_ReadColor( pp, &s->u.splits.splitTimeColor );
			else if ( !strcmp( key, "beforeSplitColor" ) )   JP_ReadColor( pp, &s->u.splits.beforeSplitColor );
			else if ( !strcmp( key, "headerTextColor" ) )    JP_ReadColor( pp, &s->u.splits.headerTextColor );
			else if ( !strcmp( key, "nameColor" ) )           JP_ReadColor( pp, &s->u.splits.nameColor );
			else if ( !strcmp( key, "altRowColor" ) )         JP_ReadColor( pp, &s->u.splits.altRowColor );
			else if ( !strcmp( key, "rowColor" ) )            JP_ReadColor( pp, &s->u.splits.rowColor );
			else if ( !strcmp( key, "rowSepColor" ) )         JP_ReadColor( pp, &s->u.splits.rowSepColor );
			else if ( !strcmp( key, "beforeNameFont" ) )     JP_ReadStr( pp, s->u.splits.beforeNameFont, sizeof( s->u.splits.beforeNameFont ) );
			else if ( !strcmp( key, "beforeNameSize" ) )     JP_ReadInt( pp, &s->u.splits.beforeNameSize );
			else if ( !strcmp( key, "beforeNameBold" ) )     JP_ReadInt( pp, &s->u.splits.beforeNameBold );
			else if ( !strcmp( key, "beforeNameColor" ) )    JP_ReadColor( pp, &s->u.splits.beforeNameColor );
			else if ( !strcmp( key, "currentNameFont" ) )    JP_ReadStr( pp, s->u.splits.currentNameFont, sizeof( s->u.splits.currentNameFont ) );
			else if ( !strcmp( key, "currentNameSize" ) )    JP_ReadInt( pp, &s->u.splits.currentNameSize );
			else if ( !strcmp( key, "currentNameBold" ) )    JP_ReadInt( pp, &s->u.splits.currentNameBold );
			else if ( !strcmp( key, "afterNameFont" ) )      JP_ReadStr( pp, s->u.splits.afterNameFont, sizeof( s->u.splits.afterNameFont ) );
			else if ( !strcmp( key, "afterNameSize" ) )      JP_ReadInt( pp, &s->u.splits.afterNameSize );
			else if ( !strcmp( key, "afterNameBold" ) )      JP_ReadInt( pp, &s->u.splits.afterNameBold );
			else if ( !strcmp( key, "afterNameColor" ) )     JP_ReadColor( pp, &s->u.splits.afterNameColor );
			else if ( !strcmp( key, "compareAgainst" ) )      JP_ReadInt( pp, &s->u.splits.compareAgainst );
			else if ( !strcmp( key, "columns" ) ) {
				int n = 0;
				JP_Match( pp, '[' );
				while ( **pp && **pp != ']' ) {
					if ( n < LSWND_MAX_COLUMNS && JP_ReadInt( pp, &iv ) )
						s->u.splits.columns[n++] = iv;
					else
						JP_SkipValue( pp );
					JP_Match( pp, ',' );
				}
				JP_Match( pp, ']' );
				s->u.splits.numColumns = n;
			}
			else if ( !strcmp( key, "colSettings" ) ) {
				int ci = 0;
				JP_Match( pp, '[' );
				while ( **pp && **pp != ']' ) {
					JP_Match( pp, '{' );
					while ( **pp && **pp != '}' ) {
						char ck[32];
						if ( !JP_ReadStr( pp, ck, sizeof( ck ) ) ) break;
						JP_Match( pp, ':' );
						if ( ci < LSWND_MAX_COLUMNS ) {
							if      ( !strcmp( ck, "label" ) )     JP_ReadStr( pp, s->u.splits.colSettings[ci].label, sizeof( s->u.splits.colSettings[ci].label ) );
							else if ( !strcmp( ck, "textColor" ) ) JP_ReadColor( pp, &s->u.splits.colSettings[ci].textColor );
							else if ( !strcmp( ck, "width" ) )     JP_ReadInt( pp, &s->u.splits.colSettings[ci].width );
							else if ( !strcmp( ck, "font" ) )      JP_ReadStr( pp, s->u.splits.colSettings[ci].font, sizeof( s->u.splits.colSettings[ci].font ) );
							else if ( !strcmp( ck, "fontSize" ) )  JP_ReadInt( pp, &s->u.splits.colSettings[ci].fontSize );
							else if ( !strcmp( ck, "bold" ) )      JP_ReadInt( pp, &s->u.splits.colSettings[ci].bold );
							else if ( !strcmp( ck, "beforeColor" ) ) JP_ReadColor( pp, &s->u.splits.colSettings[ci].beforeColor );
							else if ( !strcmp( ck, "currentColor" ) ) JP_ReadColor( pp, &s->u.splits.colSettings[ci].currentColor );
							else if ( !strcmp( ck, "afterColor" ) ) JP_ReadColor( pp, &s->u.splits.colSettings[ci].afterColor );
							else JP_SkipValue( pp );
						} else {
							JP_SkipValue( pp );
						}
						JP_Match( pp, ',' );
					}
					JP_Match( pp, '}' );
					ci++;
					JP_Match( pp, ',' );
				}
				JP_Match( pp, ']' );
			}
			else JP_SkipValue( pp );
		}

		/* -- Timer -- */
		else if ( comp->type == LSCOMP_TIMER ) {
			if      ( !strcmp( key, "decimals" ) )     JP_ReadInt( pp, &s->u.timer.decimals );
			else if ( !strcmp( key, "timingMethod" ) ) JP_ReadInt( pp, &s->u.timer.timingMethod );
			else if ( !strcmp( key, "aheadColor" ) )   JP_ReadColor( pp, &s->u.timer.aheadColor );
			else if ( !strcmp( key, "behindColor" ) )  JP_ReadColor( pp, &s->u.timer.behindColor );
			else if ( !strcmp( key, "goldColor" ) )    JP_ReadColor( pp, &s->u.timer.goldColor );
			else if ( !strcmp( key, "colorOnDeltaPlus" ) ) JP_ReadInt( pp, &s->u.timer.colorOnDeltaPlus );
			else if ( !strcmp( key, "compareAgainst" ) ) JP_ReadInt( pp, &s->u.timer.compareAgainst );
			else JP_SkipValue( pp );
		}

		/* -- Detailed Timer -- */
		else if ( comp->type == LSCOMP_DETAILED_TIMER ) {
			if      ( !strcmp( key, "mainDecimals" ) )    JP_ReadInt( pp, &s->u.detailedTimer.mainDecimals );
			else if ( !strcmp( key, "compDecimals" ) )    JP_ReadInt( pp, &s->u.detailedTimer.compDecimals );
			else if ( !strcmp( key, "timingMethod" ) )    JP_ReadInt( pp, &s->u.detailedTimer.timingMethod );
			else if ( !strcmp( key, "showPB" ) )          JP_ReadInt( pp, &s->u.detailedTimer.showPB );
			else if ( !strcmp( key, "showBest" ) )        JP_ReadInt( pp, &s->u.detailedTimer.showBest );
			else if ( !strcmp( key, "mainFontSize" ) )    JP_ReadInt( pp, &s->u.detailedTimer.mainFontSize );
			else if ( !strcmp( key, "compFontSize" ) )    JP_ReadInt( pp, &s->u.detailedTimer.compFontSize );
			else if ( !strcmp( key, "segFontSize" ) )     JP_ReadInt( pp, &s->u.detailedTimer.segFontSize );
			else if ( !strcmp( key, "mainAheadColor" ) )  JP_ReadColor( pp, &s->u.detailedTimer.mainAheadColor );
			else if ( !strcmp( key, "mainBehindColor" ) ) JP_ReadColor( pp, &s->u.detailedTimer.mainBehindColor );
			else if ( !strcmp( key, "mainGoldColor" ) )   JP_ReadColor( pp, &s->u.detailedTimer.mainGoldColor );
			else if ( !strcmp( key, "pbColor" ) )         JP_ReadColor( pp, &s->u.detailedTimer.pbColor );
			else if ( !strcmp( key, "bestColor" ) )       JP_ReadColor( pp, &s->u.detailedTimer.bestColor );
			else if ( !strcmp( key, "labelColor" ) )      JP_ReadColor( pp, &s->u.detailedTimer.labelColor );
			else if ( !strcmp( key, "segNameColor" ) )    JP_ReadColor( pp, &s->u.detailedTimer.segNameColor );
			else if ( !strcmp( key, "segTimerColor" ) )   JP_ReadColor( pp, &s->u.detailedTimer.segTimerColor );
			else if ( !strcmp( key, "showSegName" ) )     JP_ReadInt( pp, &s->u.detailedTimer.showSegTimer );
			else if ( !strcmp( key, "showSegTimer" ) )    JP_ReadInt( pp, &s->u.detailedTimer.showSegTimer );
			else if ( !strcmp( key, "colorOnDeltaPlus" ) ) JP_ReadInt( pp, &s->u.detailedTimer.colorOnDeltaPlus );
			else if ( !strcmp( key, "compareAgainst" ) )  JP_ReadInt( pp, &s->u.detailedTimer.compareAgainst );
			else JP_SkipValue( pp );
		}

		/* -- Segment Timer -- */
		else if ( comp->type == LSCOMP_SEG_TIMER ) {
			if      ( !strcmp( key, "decimals" ) )     JP_ReadInt( pp, &s->u.segTimer.decimals );
			else if ( !strcmp( key, "timingMethod" ) ) JP_ReadInt( pp, &s->u.segTimer.timingMethod );
			else if ( !strcmp( key, "timerColor" ) )   JP_ReadColor( pp, &s->u.segTimer.timerColor );
			else if ( !strcmp( key, "colorOnDeltaPlus" ) ) JP_ReadInt( pp, &s->u.segTimer.colorOnDeltaPlus );
			else if ( !strcmp( key, "compareAgainst" ) ) JP_ReadInt( pp, &s->u.segTimer.compareAgainst );
			else JP_SkipValue( pp );
		}

		/* -- Real Time -- */
		else if ( comp->type == LSCOMP_REAL_TIME ) {
			if      ( !strcmp( key, "decimals" ) )   JP_ReadInt( pp, &s->u.realTimer.decimals );
			else if ( !strcmp( key, "timerColor" ) ) JP_ReadColor( pp, &s->u.realTimer.timerColor );
			else JP_SkipValue( pp );
		}

		/* -- Ghost Segment Time -- */
		else if ( comp->type == LSCOMP_GHOST_SEG_TIME ) {
			if      ( !strcmp( key, "decimals" ) )   JP_ReadInt( pp, &s->u.ghostSegTimer.decimals );
			else if ( !strcmp( key, "timerColor" ) ) JP_ReadColor( pp, &s->u.ghostSegTimer.timerColor );
			else if ( !strcmp( key, "labelColor" ) ) JP_ReadColor( pp, &s->u.ghostSegTimer.labelColor );
			else JP_SkipValue( pp );
		}

		/* -- 100% Tracker -- */
		else if ( comp->type == LSCOMP_100PCT ) {
			if      ( !strcmp( key, "labelColor" ) )    JP_ReadColor( pp, &s->u.pct.labelColor );
			else if ( !strcmp( key, "valueColor" ) )    JP_ReadColor( pp, &s->u.pct.valueColor );
			else if ( !strcmp( key, "completeColor" ) ) JP_ReadColor( pp, &s->u.pct.completeColor );
			else if ( !strcmp( key, "showTotal" ) )     JP_ReadInt( pp, &s->u.pct.showTotal );
			else if ( !strcmp( key, "showSegment" ) )   JP_ReadInt( pp, &s->u.pct.showSegment );
			else JP_SkipValue( pp );
		}

		/* -- Best Segments -- */
		else if ( comp->type == LSCOMP_BEST_SEGMENTS ) {
			if ( !strcmp( key, "showSegments" ) ) JP_ReadInt( pp, &s->u.bestSegments.showSegments );
			else JP_SkipValue( pp );
		}

		/* -- Info rows (PrevSeg, SoB, BPT, PTS, Comparison) -- */
		else if ( ( inf = LSLAY_GetInfoSettings( s, comp->type ) ) != NULL ) {
			if      ( !strcmp( key, "accuracy" ) )     JP_ReadInt( pp, &inf->accuracy );
			else if ( !strcmp( key, "dropDecimals" ) ) JP_ReadInt( pp, &inf->dropDecimals );
			else if ( !strcmp( key, "showLive" ) )     JP_ReadInt( pp, &inf->showLive );
			else if ( !strcmp( key, "compareAgainst" ) ) JP_ReadInt( pp, &inf->compareAgainst );
			else if ( !strcmp( key, "valueColor" ) )   JP_ReadColor( pp, &inf->valueColor );
			else if ( !strcmp( key, "labelColor" ) )   JP_ReadColor( pp, &inf->labelColor );
			else if ( !strcmp( key, "label" ) )        JP_ReadStr( pp, inf->label, sizeof( inf->label ) );
			else JP_SkipValue( pp );
		}

		/* -- Separator -- */
		else if ( comp->type == LSCOMP_SEPARATOR ) {
			if      ( !strcmp( key, "color" ) )  JP_ReadColor( pp, &s->u.separator.color );
			else if ( !strcmp( key, "height" ) ) JP_ReadInt( pp, &s->u.separator.height );
			else JP_SkipValue( pp );
		}

		/* -- Text -- */
		else if ( comp->type == LSCOMP_TEXT ) {
			if ( !strcmp( key, "text" ) ) JP_ReadStr( pp, s->u.text.text, sizeof( s->u.text.text ) );
			else JP_SkipValue( pp );
		}

		/* -- Blank Space -- */
		else if ( comp->type == LSCOMP_BLANK_SPACE ) {
			if ( !strcmp( key, "height" ) ) JP_ReadInt( pp, &s->u.blankSpace.height );
			else JP_SkipValue( pp );
		}

		/* -- Header -- */
		else if ( comp->type == LSCOMP_HEADER ) {
			if      ( !strcmp( key, "text" ) )     JP_ReadStr( pp, s->u.header.text, sizeof( s->u.header.text ) );
			else if ( !strcmp( key, "showLine" ) ) JP_ReadInt( pp, &s->u.header.showLine );
			else JP_SkipValue( pp );
		}

		else {
			JP_SkipValue( pp );
		}

		JP_Match( pp, ',' );
	}
	JP_Match( pp, '}' );
	return 1;
}

/* Parse entire layout JSON */
static int JP_ParseLayout( const char **pp, lsLayout_t *l ) {
	char key[64];
	JP_SkipWS( pp );
	if ( **pp != '{' ) return 0;
	LS_LayoutSetDefault( l );
	JP_Match( pp, '{' );
	while ( **pp && **pp != '}' ) {
		if ( !JP_ReadStr( pp, key, sizeof( key ) ) ) break;
		JP_Match( pp, ':' );
		if      ( !strcmp( key, "version" ) )       JP_ReadInt( pp, &l->version );
		else if ( !strcmp( key, "alwaysOnTop" ) )   JP_ReadInt( pp, &l->alwaysOnTop );
		else if ( !strcmp( key, "transparent" ) )   JP_ReadInt( pp, &l->transparent );
		else if ( !strcmp( key, "opacity" ) )       JP_ReadInt( pp, &l->opacity );
		else if ( !strcmp( key, "compareAgainst" ) ) JP_ReadInt( pp, &l->compareAgainst );
		else if ( !strcmp( key, "globalFont" ) )    JP_ReadStr( pp, l->globalFont, sizeof( l->globalFont ) );
		else if ( !strcmp( key, "globalFontSize" ) ) JP_ReadInt( pp, &l->globalFontSize );
		else if ( !strcmp( key, "textShadow" ) )    JP_ReadInt( pp, &l->textShadow );
		else if ( !strcmp( key, "thinAccent" ) )    JP_ReadInt( pp, &l->thinAccent );
		else if ( !strcmp( key, "globalBold" ) )    JP_ReadInt( pp, &l->globalBold );
		else if ( !strcmp( key, "lockResize" ) )    JP_ReadInt( pp, &l->lockResize );
		else if ( !strcmp( key, "flipLayout" ) )    JP_ReadInt( pp, &l->flipLayout );
		else if ( !strcmp( key, "componentSpacing" ) ) JP_ReadInt( pp, &l->componentSpacing );
		else if ( !strcmp( key, "windowWidth" ) )    JP_ReadInt( pp, &l->windowWidth );
		else if ( !strcmp( key, "bgColor" ) )       JP_ReadColor( pp, &l->bgColor );
		else if ( !strcmp( key, "headerBg" ) )      JP_ReadColor( pp, &l->headerBg );
		else if ( !strcmp( key, "accentColor" ) )   JP_ReadColor( pp, &l->accentColor );
		else if ( !strcmp( key, "textColor" ) )     JP_ReadColor( pp, &l->textColor );
		else if ( !strcmp( key, "aheadColor" ) )    JP_ReadColor( pp, &l->aheadColor );
		else if ( !strcmp( key, "behindColor" ) )   JP_ReadColor( pp, &l->behindColor );
		else if ( !strcmp( key, "goldColor" ) )     JP_ReadColor( pp, &l->goldColor );
		else if ( !strcmp( key, "components" ) ) {
			l->numComponents = 0;
			JP_Match( pp, '[' );
			while ( **pp && **pp != ']' ) {
				if ( l->numComponents < LSWND_MAX_COMPONENTS ) {
					JP_ParseComponent( pp, &l->components[l->numComponents] );
					l->numComponents++;
				} else {
					JP_SkipValue( pp );
				}
				JP_Match( pp, ',' );
			}
			JP_Match( pp, ']' );
		}
		else JP_SkipValue( pp );
		JP_Match( pp, ',' );
	}
	JP_Match( pp, '}' );
	return 1;
}

/* =====================================================================
   JSON Writer
   ===================================================================== */

static void JW_Color( FILE *f, lsColor_t clr ) {
	if ( clr.mode == LSCLR_MODE_PLAIN && clr.alpha == 255 ) {
		if ( clr.c1 == LSCLR_INHERIT ) fprintf( f, "\"\"" );
		else fprintf( f, "\"%06X\"", clr.c1 );
	} else {
		fprintf( f, "{\"mode\":%d,\"c1\":", clr.mode );
		if ( clr.c1 == LSCLR_INHERIT ) fprintf( f, "\"\"" );
		else fprintf( f, "\"%06X\"", clr.c1 );
		fprintf( f, ",\"c2\":" );
		if ( clr.c2 ) fprintf( f, "\"%06X\"", clr.c2 );
		else fprintf( f, "\"000000\"" );
		fprintf( f, ",\"alpha\":%d,\"gradPos\":%d}", clr.alpha, clr.gradPos );
	}
}

static void JW_WriteComponent( FILE *f, const lsComponent_t *comp, int isLast ) {
	const lsCompSettings_t *s = &comp->s;
	const lsInfoSettings_t *inf;
	int i;

	fprintf( f, "\t\t{\n" );
	fprintf( f, "\t\t\t\"type\": \"%s\",\n", lsCompTypeNames[comp->type] );
	fprintf( f, "\t\t\t\"enabled\": %d,\n", s->enabled );
	fprintf( f, "\t\t\t\"font\": \"%s\",\n", s->font );
	fprintf( f, "\t\t\t\"fontSize\": %d,\n", s->fontSize );
	fprintf( f, "\t\t\t\"bold\": %d,\n", s->bold );
	fprintf( f, "\t\t\t\"textColor\": " ); JW_Color( f, s->textColor ); fprintf( f, ",\n" );
	fprintf( f, "\t\t\t\"bgColor\": " ); JW_Color( f, s->bgColor ); fprintf( f, ",\n" );
	fprintf( f, "\t\t\t\"padH\": %d,\n", s->padH );
	fprintf( f, "\t\t\t\"padV\": %d,\n", s->padV );
	fprintf( f, "\t\t\t\"overrideHeight\": %d,\n", s->overrideHeight );
	fprintf( f, "\t\t\t\"alignment\": %d", s->alignment );

	switch ( comp->type ) {
	case LSCOMP_TITLE:
		fprintf( f, ",\n\t\t\t\"showGameName\": %d", s->u.title.showGameName );
		fprintf( f, ",\n\t\t\t\"showCategory\": %d", s->u.title.showCategory );
		fprintf( f, ",\n\t\t\t\"showAttempts\": %d", s->u.title.showAttempts );
		fprintf( f, ",\n\t\t\t\"attemptsOnNewLine\": %d", s->u.title.attemptsOnNewLine );
		fprintf( f, ",\n\t\t\t\"showTopAccent\": %d", s->u.title.showTopAccent );
		fprintf( f, ",\n\t\t\t\"showBottomAccent\": %d", s->u.title.showBottomAccent );
		break;
	case LSCOMP_SPLITS:
		fprintf( f, ",\n\t\t\t\"visibleSplits\": %d", s->u.splits.visibleSplits );
		fprintf( f, ",\n\t\t\t\"showThinSeps\": %d", s->u.splits.showThinSeps );
		fprintf( f, ",\n\t\t\t\"alwaysShowLast\": %d", s->u.splits.alwaysShowLast );
		fprintf( f, ",\n\t\t\t\"lockLastToBottom\": %d", s->u.splits.lockLastToBottom );
		fprintf( f, ",\n\t\t\t\"sepBeforeLastSplit\": %d", s->u.splits.sepBeforeLastSplit );
		fprintf( f, ",\n\t\t\t\"deltaAccuracy\": %d", s->u.splits.deltaAccuracy );
		fprintf( f, ",\n\t\t\t\"deltaDropDecimals\": %d", s->u.splits.deltaDropDecimals );
		fprintf( f, ",\n\t\t\t\"splitAccuracy\": %d", s->u.splits.splitAccuracy );
		if ( s->u.splits.deltaCountdownSec )
			fprintf( f, ",\n\t\t\t\"deltaCountdownSec\": %d", s->u.splits.deltaCountdownSec );
		fprintf( f, ",\n\t\t\t\"showHeader\": %d", s->u.splits.showHeader );
		fprintf( f, ",\n\t\t\t\"alternateRows\": %d", s->u.splits.alternateRows );
		fprintf( f, ",\n\t\t\t\"shortNames\": %d", s->u.splits.shortNames );
		fprintf( f, ",\n\t\t\t\"currentBg\": " );        JW_Color( f, s->u.splits.currentBg );
		fprintf( f, ",\n\t\t\t\"beforeCurrentBg\": " );  JW_Color( f, s->u.splits.beforeCurrentBg );
		fprintf( f, ",\n\t\t\t\"currentSplitClr\": " );  JW_Color( f, s->u.splits.currentSplitClr );
		fprintf( f, ",\n\t\t\t\"afterCurrentBg\": " );   JW_Color( f, s->u.splits.afterCurrentBg );
		fprintf( f, ",\n\t\t\t\"liveDeltaColor\": " );   JW_Color( f, s->u.splits.liveDeltaColor );
		fprintf( f, ",\n\t\t\t\"splitTimeColor\": " );   JW_Color( f, s->u.splits.splitTimeColor );
		fprintf( f, ",\n\t\t\t\"beforeSplitColor\": " ); JW_Color( f, s->u.splits.beforeSplitColor );
		fprintf( f, ",\n\t\t\t\"headerTextColor\": " ); JW_Color( f, s->u.splits.headerTextColor );
		fprintf( f, ",\n\t\t\t\"nameColor\": " );         JW_Color( f, s->u.splits.nameColor );
		fprintf( f, ",\n\t\t\t\"altRowColor\": " );       JW_Color( f, s->u.splits.altRowColor );
		fprintf( f, ",\n\t\t\t\"rowColor\": " );          JW_Color( f, s->u.splits.rowColor );
		fprintf( f, ",\n\t\t\t\"rowSepColor\": " );       JW_Color( f, s->u.splits.rowSepColor );
		/* per-state name styling */
		if ( s->u.splits.beforeNameFont[0] )
			fprintf( f, ",\n\t\t\t\"beforeNameFont\": \"%s\"", s->u.splits.beforeNameFont );
		if ( s->u.splits.beforeNameSize )
			fprintf( f, ",\n\t\t\t\"beforeNameSize\": %d", s->u.splits.beforeNameSize );
		if ( s->u.splits.beforeNameBold != -1 )
			fprintf( f, ",\n\t\t\t\"beforeNameBold\": %d", s->u.splits.beforeNameBold );
		fprintf( f, ",\n\t\t\t\"beforeNameColor\": " ); JW_Color( f, s->u.splits.beforeNameColor );
		if ( s->u.splits.currentNameFont[0] )
			fprintf( f, ",\n\t\t\t\"currentNameFont\": \"%s\"", s->u.splits.currentNameFont );
		if ( s->u.splits.currentNameSize )
			fprintf( f, ",\n\t\t\t\"currentNameSize\": %d", s->u.splits.currentNameSize );
		if ( s->u.splits.currentNameBold != -1 )
			fprintf( f, ",\n\t\t\t\"currentNameBold\": %d", s->u.splits.currentNameBold );
		if ( s->u.splits.afterNameFont[0] )
			fprintf( f, ",\n\t\t\t\"afterNameFont\": \"%s\"", s->u.splits.afterNameFont );
		if ( s->u.splits.afterNameSize )
			fprintf( f, ",\n\t\t\t\"afterNameSize\": %d", s->u.splits.afterNameSize );
		if ( s->u.splits.afterNameBold != -1 )
			fprintf( f, ",\n\t\t\t\"afterNameBold\": %d", s->u.splits.afterNameBold );
		fprintf( f, ",\n\t\t\t\"afterNameColor\": " ); JW_Color( f, s->u.splits.afterNameColor );
		fprintf( f, ",\n\t\t\t\"compareAgainst\": %d", s->u.splits.compareAgainst );
		fprintf( f, ",\n\t\t\t\"columns\": [" );
		for ( i = 0; i < s->u.splits.numColumns; i++ ) {
			if ( i ) fprintf( f, ", " );
			fprintf( f, "%d", s->u.splits.columns[i] );
		}
		fprintf( f, "]" );
		/* per-column settings */
		fprintf( f, ",\n\t\t\t\"colSettings\": [" );
		for ( i = 0; i < s->u.splits.numColumns; i++ ) {
			const lsColumnSettings_t *cs = &s->u.splits.colSettings[i];
			if ( i ) fprintf( f, "," );
			fprintf( f, "\n\t\t\t\t{" );
			fprintf( f, " \"label\": \"%s\"", cs->label );
			fprintf( f, ", \"textColor\": " ); JW_Color( f, cs->textColor );
			fprintf( f, ", \"width\": %d", cs->width );
			if ( cs->font[0] )  fprintf( f, ", \"font\": \"%s\"", cs->font );
			if ( cs->fontSize ) fprintf( f, ", \"fontSize\": %d", cs->fontSize );
			if ( cs->bold != -1 ) fprintf( f, ", \"bold\": %d", cs->bold );
			fprintf( f, ", \"beforeColor\": " ); JW_Color( f, cs->beforeColor );
			fprintf( f, ", \"currentColor\": " ); JW_Color( f, cs->currentColor );
			fprintf( f, ", \"afterColor\": " ); JW_Color( f, cs->afterColor );
			fprintf( f, " }" );
		}
		fprintf( f, "\n\t\t\t]" );
		break;
	case LSCOMP_TIMER:
		fprintf( f, ",\n\t\t\t\"decimals\": %d", s->u.timer.decimals );
		fprintf( f, ",\n\t\t\t\"timingMethod\": %d", s->u.timer.timingMethod );
		fprintf( f, ",\n\t\t\t\"aheadColor\": " );  JW_Color( f, s->u.timer.aheadColor );
		fprintf( f, ",\n\t\t\t\"behindColor\": " ); JW_Color( f, s->u.timer.behindColor );
		fprintf( f, ",\n\t\t\t\"goldColor\": " );   JW_Color( f, s->u.timer.goldColor );
		fprintf( f, ",\n\t\t\t\"colorOnDeltaPlus\": %d", s->u.timer.colorOnDeltaPlus );
		fprintf( f, ",\n\t\t\t\"compareAgainst\": %d", s->u.timer.compareAgainst );
		break;
	case LSCOMP_DETAILED_TIMER:
		fprintf( f, ",\n\t\t\t\"mainDecimals\": %d", s->u.detailedTimer.mainDecimals );
		fprintf( f, ",\n\t\t\t\"compDecimals\": %d", s->u.detailedTimer.compDecimals );
		fprintf( f, ",\n\t\t\t\"timingMethod\": %d", s->u.detailedTimer.timingMethod );
		fprintf( f, ",\n\t\t\t\"showPB\": %d", s->u.detailedTimer.showPB );
		fprintf( f, ",\n\t\t\t\"showBest\": %d", s->u.detailedTimer.showBest );
		fprintf( f, ",\n\t\t\t\"mainFontSize\": %d", s->u.detailedTimer.mainFontSize );
		fprintf( f, ",\n\t\t\t\"compFontSize\": %d", s->u.detailedTimer.compFontSize );
		fprintf( f, ",\n\t\t\t\"mainAheadColor\": " );  JW_Color( f, s->u.detailedTimer.mainAheadColor );
		fprintf( f, ",\n\t\t\t\"mainBehindColor\": " ); JW_Color( f, s->u.detailedTimer.mainBehindColor );
		fprintf( f, ",\n\t\t\t\"mainGoldColor\": " );   JW_Color( f, s->u.detailedTimer.mainGoldColor );
		fprintf( f, ",\n\t\t\t\"pbColor\": " );          JW_Color( f, s->u.detailedTimer.pbColor );
		fprintf( f, ",\n\t\t\t\"bestColor\": " );        JW_Color( f, s->u.detailedTimer.bestColor );
		fprintf( f, ",\n\t\t\t\"labelColor\": " );       JW_Color( f, s->u.detailedTimer.labelColor );
		fprintf( f, ",\n\t\t\t\"segNameColor\": " );     JW_Color( f, s->u.detailedTimer.segNameColor );
		fprintf( f, ",\n\t\t\t\"segTimerColor\": " );    JW_Color( f, s->u.detailedTimer.segTimerColor );
		fprintf( f, ",\n\t\t\t\"showSegTimer\": %d", s->u.detailedTimer.showSegTimer );
		fprintf( f, ",\n\t\t\t\"segFontSize\": %d", s->u.detailedTimer.segFontSize );
		fprintf( f, ",\n\t\t\t\"colorOnDeltaPlus\": %d", s->u.detailedTimer.colorOnDeltaPlus );
		fprintf( f, ",\n\t\t\t\"compareAgainst\": %d", s->u.detailedTimer.compareAgainst );
		break;
	case LSCOMP_SEG_TIMER:
		fprintf( f, ",\n\t\t\t\"decimals\": %d", s->u.segTimer.decimals );
		fprintf( f, ",\n\t\t\t\"timingMethod\": %d", s->u.segTimer.timingMethod );
		fprintf( f, ",\n\t\t\t\"timerColor\": " ); JW_Color( f, s->u.segTimer.timerColor );
		fprintf( f, ",\n\t\t\t\"colorOnDeltaPlus\": %d", s->u.segTimer.colorOnDeltaPlus );
		fprintf( f, ",\n\t\t\t\"compareAgainst\": %d", s->u.segTimer.compareAgainst );
		break;
	case LSCOMP_REAL_TIME:
		fprintf( f, ",\n\t\t\t\"decimals\": %d", s->u.realTimer.decimals );
		fprintf( f, ",\n\t\t\t\"timerColor\": " ); JW_Color( f, s->u.realTimer.timerColor );
		break;
	case LSCOMP_GHOST_SEG_TIME:
		fprintf( f, ",\n\t\t\t\"decimals\": %d", s->u.ghostSegTimer.decimals );
		fprintf( f, ",\n\t\t\t\"timerColor\": " ); JW_Color( f, s->u.ghostSegTimer.timerColor );
		fprintf( f, ",\n\t\t\t\"labelColor\": " ); JW_Color( f, s->u.ghostSegTimer.labelColor );
		break;
	case LSCOMP_100PCT:
		fprintf( f, ",\n\t\t\t\"labelColor\": " ); JW_Color( f, s->u.pct.labelColor );
		fprintf( f, ",\n\t\t\t\"valueColor\": " ); JW_Color( f, s->u.pct.valueColor );
		fprintf( f, ",\n\t\t\t\"completeColor\": " ); JW_Color( f, s->u.pct.completeColor );
		fprintf( f, ",\n\t\t\t\"showTotal\": %d", s->u.pct.showTotal );
		fprintf( f, ",\n\t\t\t\"showSegment\": %d", s->u.pct.showSegment );
		break;
	case LSCOMP_BEST_SEGMENTS:
		fprintf( f, ",\n\t\t\t\"showSegments\": %d", s->u.bestSegments.showSegments );
		break;
	case LSCOMP_PREV_SEGMENT:
	case LSCOMP_SUM_OF_BEST:
	case LSCOMP_BEST_POSSIBLE:
	case LSCOMP_POSSIBLE_SAVE:
	case LSCOMP_COMPARISON:
		inf = LSLAY_GetInfoConst( s, comp->type );
		if ( inf ) {
			fprintf( f, ",\n\t\t\t\"accuracy\": %d", inf->accuracy );
			fprintf( f, ",\n\t\t\t\"dropDecimals\": %d", inf->dropDecimals );
			fprintf( f, ",\n\t\t\t\"showLive\": %d", inf->showLive );
			fprintf( f, ",\n\t\t\t\"compareAgainst\": %d", inf->compareAgainst );
			fprintf( f, ",\n\t\t\t\"valueColor\": " ); JW_Color( f, inf->valueColor );
			fprintf( f, ",\n\t\t\t\"labelColor\": " ); JW_Color( f, inf->labelColor );
			if ( inf->label[0] )
				fprintf( f, ",\n\t\t\t\"label\": \"%s\"", inf->label );
		}
		break;
	case LSCOMP_SEPARATOR:
		fprintf( f, ",\n\t\t\t\"color\": " ); JW_Color( f, s->u.separator.color );
		fprintf( f, ",\n\t\t\t\"height\": %d", s->u.separator.height );
		break;
	case LSCOMP_TEXT:
		fprintf( f, ",\n\t\t\t\"text\": \"%s\"", s->u.text.text );
		break;
	case LSCOMP_BLANK_SPACE:
		fprintf( f, ",\n\t\t\t\"height\": %d", s->u.blankSpace.height );
		break;
	case LSCOMP_HEADER:
		fprintf( f, ",\n\t\t\t\"text\": \"%s\"", s->u.header.text );
		fprintf( f, ",\n\t\t\t\"showLine\": %d", s->u.header.showLine );
		break;
	default:
		break;
	}

	fprintf( f, "\n\t\t}%s\n", isLast ? "" : "," );
}

static void JW_WriteLayout( FILE *f, const lsLayout_t *l ) {
	int i;
	fprintf( f, "{\n" );
	fprintf( f, "\t\"version\": %d,\n", l->version );
	fprintf( f, "\t\"alwaysOnTop\": %d,\n", l->alwaysOnTop );
	fprintf( f, "\t\"transparent\": %d,\n", l->transparent );
	fprintf( f, "\t\"opacity\": %d,\n", l->opacity );
	fprintf( f, "\t\"compareAgainst\": %d,\n", l->compareAgainst );
	fprintf( f, "\t\"globalFont\": \"%s\",\n", l->globalFont );
	fprintf( f, "\t\"globalFontSize\": %d,\n", l->globalFontSize );
	fprintf( f, "\t\"textShadow\": %d,\n", l->textShadow );
	fprintf( f, "\t\"thinAccent\": %d,\n", l->thinAccent );
	fprintf( f, "\t\"globalBold\": %d,\n", l->globalBold );
	fprintf( f, "\t\"lockResize\": %d,\n", l->lockResize );
	fprintf( f, "\t\"flipLayout\": %d,\n", l->flipLayout );
	fprintf( f, "\t\"componentSpacing\": %d,\n", l->componentSpacing );
	fprintf( f, "\t\"windowWidth\": %d,\n", l->windowWidth );
	fprintf( f, "\t\"bgColor\": " );     JW_Color( f, l->bgColor );     fprintf( f, ",\n" );
	fprintf( f, "\t\"headerBg\": " );    JW_Color( f, l->headerBg );    fprintf( f, ",\n" );
	fprintf( f, "\t\"accentColor\": " ); JW_Color( f, l->accentColor ); fprintf( f, ",\n" );
	fprintf( f, "\t\"textColor\": " );   JW_Color( f, l->textColor );   fprintf( f, ",\n" );
	fprintf( f, "\t\"aheadColor\": " );  JW_Color( f, l->aheadColor );  fprintf( f, ",\n" );
	fprintf( f, "\t\"behindColor\": " ); JW_Color( f, l->behindColor ); fprintf( f, ",\n" );
	fprintf( f, "\t\"goldColor\": " );   JW_Color( f, l->goldColor );   fprintf( f, ",\n" );
	fprintf( f, "\t\"components\": [\n" );
	for ( i = 0; i < l->numComponents; i++ )
		JW_WriteComponent( f, &l->components[i], ( i == l->numComponents - 1 ) );
	fprintf( f, "\t]\n" );
	fprintf( f, "}\n" );
}

/* =====================================================================
   File I/O
   ===================================================================== */

static void LSLAY_GetLayoutPath( char *buf, int sz ) {
	const char *home = Cvar_VariableString( "fs_homepath" );
	if ( home[0] )
		Com_sprintf( buf, sz, "%s/livesplit_layout.json", home );
	else
		Q_strncpyz( buf, "livesplit_layout.json", sz );
}

static void LSLAY_LoadLayout( void ) {
	char path[512];
	FILE *f;
	long len;
	char *data;
	const char *p;

	LSLAY_GetLayoutPath( path, sizeof( path ) );
	f = fopen( path, "rb" );
	if ( !f ) {
		LS_LayoutSetDefault( &lswnd_layout );
		return;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( len <= 0 || len > 1024 * 1024 ) {
		fclose( f );
		LS_LayoutSetDefault( &lswnd_layout );
		return;
	}
	data = (char *)malloc( len + 1 );
	if ( !data ) { fclose( f ); LS_LayoutSetDefault( &lswnd_layout ); return; }
	fread( data, 1, len, f );
	fclose( f );
	data[len] = '\0';
	p = data;
	JP_ParseLayout( &p, &lswnd_layout );
	free( data );
}

static void LSLAY_SaveLayout( void ) {
	char path[512];
	FILE *f;
	LSLAY_GetLayoutPath( path, sizeof( path ) );
	f = fopen( path, "w" );
	if ( !f ) return;
	JW_WriteLayout( f, &lswnd_layout );
	fclose( f );
}
