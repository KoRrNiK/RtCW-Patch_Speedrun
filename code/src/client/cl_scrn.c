/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"

qboolean scr_initialized;           // ready to draw

cvar_t      *cl_timegraph;
cvar_t      *cl_debuggraph;
cvar_t      *cl_graphheight;
cvar_t      *cl_graphscale;
cvar_t      *cl_graphshift;

// Knightmare added
cvar_t		*scr_surroundlayout;	// whether to keep HUD/menu elements on center screen in triple-wide video modes
cvar_t		*scr_surroundleft;		// left placement of HUD/menu elements on center screen in triple-wide video modes
cvar_t		*scr_surroundright;		// right placement of HUD/menu elements on center screen in triple-wide video modes
// end Knightmare

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	SCR_AdjustFrom640( &x, &y, &width, &height, ALIGN_STRETCH );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h, scralign_t align ) {
	float	screenAspect;
	float	xscale, lb_xscale, yscale, minscale, vertscale;	// Knightmare added
	float	tmp_x, tmp_y, tmp_w, tmp_h, tmp_left, tmp_right;	// Knightmare added
	float	xleft, xright;

#if 0
	// adjust for wide screens
	if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
		*x += 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * 640 / 480 ) );
	}
#endif

	// scale for screen sizes
//	xscale = cls.glconfig.vidWidth / 640.0;
//	yscale = cls.glconfig.vidHeight / 480.0;
//	minscale = min (xscale, yscale);
	screenAspect = (float)cls.glconfig.vidWidth / (float)cls.glconfig.vidHeight;	// Knightmare added

	// for eyefinity/surround setups, keep everything on the center monitor
	if (scr_surroundlayout && scr_surroundlayout->integer && screenAspect >= 3.6f)
	{
		if (scr_surroundleft && scr_surroundleft->value > 0.0f && scr_surroundleft->value < 1.0f)
			xleft = (float)cls.glconfig.vidWidth * scr_surroundleft->value;
		else
			xleft = (float)cls.glconfig.vidWidth / 3.0f;
		if (scr_surroundright && scr_surroundright->value > 0.0f && scr_surroundright->value < 1.0f)
			xright = (float)cls.glconfig.vidWidth * scr_surroundright->value;
		else
			xright = (float)cls.glconfig.vidWidth * (2.0f / 3.0f);
		xscale = (xright - xleft) / SCREEN_WIDTH;
	}
	else {
		xleft = 0.0f;
		xright = (float)cls.glconfig.vidWidth;
		xscale = (float)cls.glconfig.vidWidth / SCREEN_WIDTH;
	}

	lb_xscale = (float)cls.glconfig.vidWidth / SCREEN_WIDTH;
	yscale = (float)cls.glconfig.vidHeight / SCREEN_HEIGHT;
	minscale = min(xscale, yscale);

	// hack for 5:4 modes
	if ( !(xscale > yscale) && align != ALIGN_LETTERBOX)
		align = ALIGN_STRETCH;

	// Knightmare- added anamorphic code
	switch (align)
	{
	case ALIGN_CENTER:
		if (x) {
		tmp_x = *x;
			*x = (tmp_x - (0.5 * SCREEN_WIDTH)) * minscale + (0.5 * cls.glconfig.vidWidth);
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - (0.5 * SCREEN_HEIGHT)) * minscale + (0.5 * cls.glconfig.vidHeight);
		}
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		break;
	case ALIGN_LETTERBOX:
		// special case: video mode (eyefinity?) is wider than object
		if ( w != NULL && h != NULL && ((float)cls.glconfig.vidWidth / (float)cls.glconfig.vidHeight > *w / *h) ) {
			tmp_h = *h;
			vertscale = cls.glconfig.vidHeight / tmp_h;
			if (x != NULL && w != NULL) {
				tmp_x = *x;
				tmp_w = *w;
				*x = tmp_x * lb_xscale - (0.5 * (tmp_w * vertscale - tmp_w * lb_xscale));
			}
			if (y)
				*y = 0;
			if (w) 
				*w *= vertscale;
			if (h)
				*h *= vertscale;
		}
		else {
			if (x)
				*x *= xscale;
			if (y != NULL && h != NULL)  {
				tmp_y = *y;
				tmp_h = *h;
				*y = tmp_y * yscale - (0.5 * (tmp_h * xscale - tmp_h * yscale));
			}
			if (w) 
				*w *= xscale;
			if (h)
				*h *= xscale;
		}
		break;
	case ALIGN_TOP:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = (tmp_x - (0.5 * SCREEN_WIDTH)) * minscale + (0.5 * cls.glconfig.vidWidth);
		}
		if (y)
			*y *= minscale;
		break;
	case ALIGN_BOTTOM:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = (tmp_x - (0.5 * SCREEN_WIDTH)) * minscale + (0.5 * cls.glconfig.vidWidth);
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + cls.glconfig.vidHeight;
		}
		break;
	case ALIGN_RIGHT:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = (tmp_x - SCREEN_WIDTH) * minscale + xright;
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - (0.5 * SCREEN_HEIGHT)) * minscale + (0.5 * cls.glconfig.vidHeight);
		}
		break;
	case ALIGN_LEFT:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x)
			*x *= minscale + xleft;
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - (0.5 * SCREEN_HEIGHT)) * minscale + (0.5 * cls.glconfig.vidHeight);
		}
		break;
	case ALIGN_TOPRIGHT:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = (tmp_x - SCREEN_WIDTH) * minscale + xright;
		}
		if (y)
			*y *= minscale;
		break;
	case ALIGN_TOPLEFT:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = tmp_x * minscale + xleft;
		}
		if (y)
			*y *= minscale;
		break;
	case ALIGN_BOTTOMRIGHT:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = (tmp_x - SCREEN_WIDTH) * minscale + xright;
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + cls.glconfig.vidHeight;
		}
		break;
	case ALIGN_BOTTOMLEFT:
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = tmp_x * minscale + xleft;
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + cls.glconfig.vidHeight;
		}
		break;
	case ALIGN_TOP_STRETCH:
		if (w) 
			*w *= xscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = tmp_x * xscale + xleft;
		}
		if (y)
			*y *= minscale;
		break;
	case ALIGN_BOTTOM_STRETCH:
		if (w) 
			*w *= xscale;
		if (h)
			*h *= minscale;
		if (x) {
			tmp_x = *x;
			*x = tmp_x * xscale + xleft;
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + cls.glconfig.vidHeight;
		}
		break;
	case ALIGN_STRETCH_ALL:
		if (x)
			*x *= lb_xscale;
		if (y) 
			*y *= yscale;
		if (w) 
			*w *= lb_xscale;
		if (h)
			*h *= yscale;
		break;
	case ALIGN_STRETCH_LEFT_CENTER:
		if (x && w) {
			tmp_x = *x;
			tmp_w = *w;
			tmp_left = tmp_x * xscale + xleft;
			tmp_right = (tmp_x + tmp_w - (0.5*SCREEN_WIDTH)) * minscale + (0.5*(cls.glconfig.vidWidth));
			*x = tmp_left;
			*w = tmp_right - tmp_left;
		}
		if (y) 
			*y *= minscale;
		if (h)
			*h *= minscale;
		break;
	case ALIGN_STRETCH_RIGHT_CENTER:
		if (x && w) {
			tmp_x = *x;
			tmp_w = *w;
			tmp_left = (tmp_x - (0.5*SCREEN_WIDTH)) * minscale + (0.5*(cls.glconfig.vidWidth));
			tmp_right = (tmp_x + tmp_w - SCREEN_WIDTH) * xscale + xright;
			*x = tmp_left;
			*w = tmp_right - tmp_left;
		}
		if (y) 
			*y *= minscale;
		if (h)
			*h *= minscale;
		break;
	case ALIGN_STRETCH:
	default:
		if (x) {
			tmp_x = *x;
			*x = tmp_x * xscale + xleft;
		}
		if (y) 
			*y *= yscale;
		if (w) 
			*w *= xscale;
		if (h)
			*h *= yscale;
		break;
	}
/*	if ( x ) {
		*x *= xscale;
	}
	if ( y ) {
		*y *= yscale;
	}
	if ( w ) {
		*w *= xscale;
	}
	if ( h ) {
		*h *= yscale;
	}*/
}

/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	re.SetColor( color );

	SCR_AdjustFrom640( &x, &y, &width, &height, ALIGN_STRETCH );
	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

	re.SetColor( NULL );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	SCR_AdjustFrom640( &x, &y, &width, &height, ALIGN_STRETCH );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}



/*
** SCR_DrawChar
** chars are drawn at 640*480 virtual screen size
*/
void SCR_DrawChar( int x, int y, float size, int ch ) {
	int row, col;
	float frow, fcol;
	float ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -size ) {
		return;
	}

	ax = x;
	ay = y;
	aw = size;
	ah = size;
	SCR_AdjustFrom640( &ax, &ay, &aw, &ah, ALIGN_STRETCH );

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow,
					   fcol + size, frow + size,
					   cls.charSetShader );
}

/*
** SCR_DrawSmallChar
** small chars are drawn at native screen resolution
*/
void SCR_DrawSmallChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	re.DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
					   fcol, frow,
					   fcol + size, frow + size,
					   cls.charSetShader );
}


/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawStringExt( int x, int y, float size, const char *string, float *setColor, qboolean forceColor ) {
	vec4_t color;
	const char  *s;
	int xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0;
	color[3] = setColor[3] * 0.6f;
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawChar( xx + 1, y + 1, size, *s );
		xx += size;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex( *( s + 1 ) )], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			s += 2;
			continue;
		}
		SCR_DrawChar( xx, y, size, *s );
		xx += size;
		s++;
	}
	re.SetColor( NULL );
}


void SCR_DrawBigString( int x, int y, const char *s, float alpha ) {
	float color[4];

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qfalse );
}

void SCR_DrawBigStringColor( int x, int y, const char *s, vec4_t color ) {
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qtrue );
}


/*
==================
SCR_DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawSmallStringExt( int x, int y, const char *string, float *setColor, qboolean forceColor ) {
	vec4_t color;
	const char  *s;
	int xx;

	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex( *( s + 1 ) )], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			s += 2;
			continue;
		}
		SCR_DrawSmallChar( xx, y, *s );
		xx += SMALLCHAR_WIDTH;
		s++;
	}
	re.SetColor( NULL );
}



/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

/*
** SCR_GetBigStringWidth
*/
int SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * 16;
}


//===============================================================================

/*
=================
SCR_DrawDemoRecording

Minimal recording indicator: pulsing red dot + "REC" + demo name.
No background panel - just the elements floating on screen.
=================
*/
void SCR_DrawDemoRecording( void ) {
	char shortName[40];
	int nameLen;
	float pulse, dotAlpha;
	int x, y, dotSz;
	vec4_t recDot;
	vec4_t recLabel = { 0.92f, 0.22f, 0.18f, 0.90f };
	vec4_t nameDim  = { 0.70f, 0.72f, 0.68f, 0.55f };
	vec4_t shadow   = { 0.0f,  0.0f,  0.0f,  0.35f };

	if ( !clc.demorecording ) {
		return;
	}

	/* Pulsing dot alpha: 0.35 – 1.0 */
	pulse = (float)sin( (double)cls.realtime * 0.004 );
	dotAlpha = 0.68f + 0.32f * pulse;

	recDot[0] = 0.92f;
	recDot[1] = 0.12f;
	recDot[2] = 0.08f;
	recDot[3] = dotAlpha;

	/* Shorten demo name */
	nameLen = strlen( clc.demoName );
	if ( nameLen > 24 ) {
		Com_sprintf( shortName, sizeof( shortName ), "..%s",
					 clc.demoName + nameLen - 22 );
	} else {
		Q_strncpyz( shortName, clc.demoName, sizeof( shortName ) );
	}

	x = 6;
	y = 4;
	dotSz = 5;

	/* Shadow under dot */
	SCR_FillRect( x + 1, y + 1, dotSz, dotSz, shadow );
	/* Pulsing red dot */
	SCR_FillRect( x, y, dotSz, dotSz, recDot );

	/* "REC" - small, with shadow */
	SCR_DrawStringExt( x + dotSz + 4 + 1, y + 1, 3, "REC", shadow, qtrue );
	SCR_DrawStringExt( x + dotSz + 4, y, 3, "REC", recLabel, qtrue );

	/* Demo name - even smaller, dimmed */
	{
		int nameX = x + dotSz + 4 + 3 * 3 + 4;
		SCR_DrawStringExt( nameX + 1, y + 1 + 1, 3, shortName, shadow, qtrue );
		SCR_DrawStringExt( nameX, y + 1, 3, shortName, nameDim, qtrue );
	}
}


/*
=================
SCR_DrawDemoPlayback

Layout:
  Top bar:    demo name | map X/Y | status | keybinds
  Left side:  three timers (Total, Map, Map Total)
  Bottom:     progress bar with map boundary markers
Uses LiveSplit/keystroke color scheme.
=================
*/
extern float CL_DemoTimescale( void );
extern qboolean CL_DemoPaused( void );
extern int  CL_DemoGetTotalMaps( void );
extern int  CL_DemoGetCurrentMapIndex( void );
extern int  CL_DemoGetMapDuration( int mapIdx );
extern const char *CL_DemoGetMapName( int mapIdx );
extern int  CL_DemoGetFullDuration( void );
extern int  CL_DemoGetCumulativeTime( void );

void SCR_DrawDemoPlayback( void ) {
	char line[128];
	float speed;
	float progress;
	int elapsed, elapsedMin, elapsedSec, elapsedMs;
	int stageElapsed, stageMin, stageSec, stageMs;
	int mapTotal, mapTotalMin, mapTotalSec, mapTotalMs;
	int demoFull, demoFullMin, demoFullSec, demoFullMs;
	int pos, fileLen;
	int totalMaps, currentMapIdx;

	/* ---- LiveSplit / Keystroke palette ---- */
	static vec4_t panelBg     = { 0.06f, 0.06f, 0.08f, 0.80f };
	static vec4_t panelBgLt   = { 0.08f, 0.09f, 0.10f, 0.60f }; /* lighter stripe */
	static vec4_t accentGrn   = { 0.35f, 0.75f, 0.20f, 1.00f };
	static vec4_t accentLine  = { 0.35f, 0.75f, 0.20f, 0.45f };
	static vec4_t barBg       = { 0.08f, 0.08f, 0.10f, 0.75f };
	static vec4_t barFill     = { 0.35f, 0.75f, 0.20f, 0.85f };
	static vec4_t barHead     = { 0.90f, 0.97f, 0.85f, 0.95f };
	static vec4_t barMarker   = { 0.55f, 0.55f, 0.55f, 0.40f };
	static vec4_t textBright  = { 0.92f, 0.95f, 0.90f, 0.95f };
	static vec4_t textMain    = { 0.78f, 0.82f, 0.76f, 0.85f };
	static vec4_t textDim     = { 0.45f, 0.52f, 0.40f, 0.60f };
	static vec4_t textLabel   = { 0.50f, 0.58f, 0.45f, 0.70f };
	static vec4_t pauseCol    = { 0.95f, 0.22f, 0.22f, 0.95f };
	static vec4_t speedCol    = { 1.00f, 0.82f, 0.15f, 0.95f };
	static vec4_t timerGrn    = { 0.65f, 0.92f, 0.42f, 1.00f };
	static vec4_t timerWhite  = { 0.90f, 0.93f, 0.88f, 0.95f };
	static vec4_t timerSub    = { 0.55f, 0.70f, 0.45f, 0.75f };
	static vec4_t sepLine     = { 0.30f, 0.45f, 0.22f, 0.25f };

	if ( !clc.demoplaying ) {
		return;
	}

	/* H key toggle: hide ALL demo overlay */
	if ( clc.demoHideHUD ) {
		return;
	}

	speed = CL_DemoTimescale();
	totalMaps = CL_DemoGetTotalMaps();
	currentMapIdx = CL_DemoGetCurrentMapIndex();

	/* ---- Progress ---- */
	progress = 0.0f;
	fileLen = clc.demoFileLen;
	if ( fileLen > 0 && clc.demofile ) {
		pos = FS_FTell( clc.demofile );
		if ( pos > 0 ) {
			progress = (float)pos / (float)fileLen;
			if ( progress > 1.0f ) progress = 1.0f;
		}
	}

	/* ---- Full demo elapsed time (cumulative across all maps) ---- */
	elapsed = CL_DemoGetCumulativeTime();
	elapsedMin = elapsed / 60000;
	elapsedSec = ( elapsed / 1000 ) % 60;
	elapsedMs  = elapsed % 1000;

	/* ---- Stage / map elapsed time ---- */
	stageElapsed = 0;
	if ( clc.demoMapStartServerTime > 0 && clc.demoCurrentServerTime > clc.demoMapStartServerTime ) {
		stageElapsed = clc.demoCurrentServerTime - clc.demoMapStartServerTime;
	}
	stageMin = stageElapsed / 60000;
	stageSec = ( stageElapsed / 1000 ) % 60;
	stageMs  = stageElapsed % 1000;

	/* ---- Map total duration ---- */
	mapTotal = CL_DemoGetMapDuration( currentMapIdx );
	mapTotalMin = mapTotal / 60000;
	mapTotalSec = ( mapTotal / 1000 ) % 60;
	mapTotalMs  = mapTotal % 1000;

	/* ---- Full demo duration ---- */
	demoFull = CL_DemoGetFullDuration();
	demoFullMin = demoFull / 60000;
	demoFullSec = ( demoFull / 1000 ) % 60;
	demoFullMs  = demoFull % 1000;

	/* ============================================================
	   TIMER PANEL - compact LiveSplit-style widget, upper-left
	   ============================================================ */
	{
		int bx = 4;
		int by = 20;
		int boxW = 158;
		int headerH = 16;
		int rowH = 14;
		int nRows = 4;
		int footerH = 14;
		int boxH = headerH + rowH * nRows + footerH + 2; /* +2 for separator lines */
		int pad = 6;
		int tx, ty;
		int valRight = bx + boxW - pad;   /* right-align timer values here */
		int i;

		/* --- Panel background --- */
		SCR_FillRect( bx, by, boxW, boxH, panelBg );

		/* --- Green accent line at top (2px) --- */
		SCR_FillRect( bx, by, boxW, 2, accentGrn );

		/* --- Header: demo name + map --- */
		tx = bx + pad;
		ty = by + 4;
		{
			char header[80];
			int nameLen = strlen( clc.demoName );
			int maxC = 14;
			char shortName[32];
			if ( nameLen > maxC ) {
				Com_sprintf( shortName, sizeof( shortName ), "..%s", clc.demoName + nameLen - maxC + 2 );
			} else {
				Q_strncpyz( shortName, clc.demoName, sizeof( shortName ) );
			}
			if ( totalMaps > 0 ) {
				Com_sprintf( header, sizeof( header ), "%s  %d/%d",
							 shortName, currentMapIdx + 1, totalMaps );
			} else {
				Q_strncpyz( header, shortName, sizeof( header ) );
			}
			SCR_DrawStringExt( tx, ty, 4, header, textMain, qtrue );
		}

		/* Status badges - show ALL active states combined on the right */
		{
			char badge[48];
			int bpos = 0;
			vec4_t *badgeColor = &textMain;

			badge[0] = '\0';

			if ( CL_DemoPaused() ) {
				Q_strncpyz( badge + bpos, "PAUSE", sizeof(badge) - bpos );
				bpos += 5;
				badgeColor = &pauseCol;
			}
			if ( clc.demoFreecam ) {
				if ( bpos > 0 ) badge[bpos++] = '|';
				Q_strncpyz( badge + bpos, "CAM", sizeof(badge) - bpos );
				bpos += 3;
				if ( !CL_DemoPaused() ) badgeColor = &accentGrn;
			}
			if ( !CL_DemoPaused() && speed != 1.0f ) {
				char spd[16];
				if ( bpos > 0 ) badge[bpos++] = '|';
				Com_sprintf( spd, sizeof(spd), "%.1fx", speed );
				Q_strncpyz( badge + bpos, spd, sizeof(badge) - bpos );
				bpos = strlen( badge );
				if ( !CL_DemoPaused() && !clc.demoFreecam ) badgeColor = &speedCol;
			}

			if ( bpos > 0 ) {
				int sw = bpos * 4;
				SCR_DrawStringExt( valRight - sw, ty, 4, badge, *badgeColor, qtrue );
			}
		}

		/* --- Separator line under header --- */
		ty = by + headerH;
		SCR_FillRect( bx + pad, ty, boxW - pad * 2, 1, sepLine );
		ty += 2;

		/* --- Timer rows --- */
		{
			const char *labels[4] = { "Total",  "Map",     "Map Len", "Demo" };
			vec4_t *colors[4]     = { &timerWhite, &timerGrn, &timerSub, &timerSub };
			char values[4][24];

			Com_sprintf( values[0], sizeof( values[0] ), "%d:%02d.%03d",
						 elapsedMin, elapsedSec, elapsedMs );
			Com_sprintf( values[1], sizeof( values[1] ), "%d:%02d.%03d",
						 stageMin, stageSec, stageMs );
			if ( mapTotal > 0 ) {
				Com_sprintf( values[2], sizeof( values[2] ), "%d:%02d.%03d",
							 mapTotalMin, mapTotalSec, mapTotalMs );
			} else {
				Q_strncpyz( values[2], "--:--.---", sizeof( values[2] ) );
			}
			if ( demoFull > 0 ) {
				Com_sprintf( values[3], sizeof( values[3] ), "%d:%02d.%03d",
							 demoFullMin, demoFullSec, demoFullMs );
			} else {
				Q_strncpyz( values[3], "--:--.---", sizeof( values[3] ) );
			}

			for ( i = 0; i < nRows; i++ ) {
				int ry = ty + i * rowH;
				int vw;

				/* Alternating subtle row highlight */
				if ( i & 1 ) {
					SCR_FillRect( bx + 1, ry, boxW - 2, rowH, panelBgLt );
				}

				/* Label (left) */
				SCR_DrawStringExt( bx + pad, ry + 3, 4, labels[i], textLabel, qtrue );

				/* Value (right-aligned) */
				vw = strlen( values[i] ) * 5;  /* timerSz=5 */
				SCR_DrawStringExt( valRight - vw, ry + 2, 5, values[i],
								   *colors[i], qtrue );
			}
		}

		/* --- Footer: map name only --- */
		ty = ty + nRows * rowH;
		SCR_FillRect( bx + pad, ty, boxW - pad * 2, 1, sepLine );
		ty += 3;
		{
			char cleanMap[64];
			const char *mapn = clc.demoCurrentMapname;
			const char *p;
			int len;

			/* Strip "maps/" prefix */
			p = strstr( mapn, "maps/" );
			if ( p ) mapn = p + 5;
			p = strstr( mapn, "maps\\" );
			if ( p ) mapn = p + 5;
			Q_strncpyz( cleanMap, mapn[0] ? mapn : "---", sizeof( cleanMap ) );
			/* Strip ".bsp" suffix */
			len = strlen( cleanMap );
			if ( len > 4 && !Q_stricmp( cleanMap + len - 4, ".bsp" ) ) {
				cleanMap[len - 4] = '\0';
			}
			SCR_DrawStringExt( bx + pad, ty, 4, cleanMap, textDim, qtrue );
		}

		/* --- Thin green line at bottom --- */
		SCR_FillRect( bx, by + boxH - 1, boxW, 1, accentLine );
	}

	/* ============================================================
	   KEYBINDS BOX - separate panel below the main timer panel
	   ============================================================ */
	{
		extern qboolean CL_DemoBindsHidden( void );
		int bx = 4;
		int by = 20;
		int boxW = 158;
		int headerH = 16;
		int rowH = 14;
		int nRows = 4;
		int footerH = 14;
		int mainBoxH = headerH + rowH * nRows + footerH + 2;
		int pad = 6;
		int kbY = by + mainBoxH + 2;  /* 2px gap below main panel */

		if ( CL_DemoBindsHidden() ) {
			/* When hidden, show a small hint to reveal binds */
			SCR_DrawStringExt( bx + pad, kbY + 2, 3, "B: Show keybinds", textDim, qtrue );
		} else {
			/* Full keybinds box */
			int kbH = 12 * 10 + 8;  /* 10 lines * 12px + padding */
			int ky;

			SCR_FillRect( bx, kbY, boxW, kbH, panelBg );
			SCR_FillRect( bx, kbY, boxW, 1, accentLine );

			ky = kbY + 4;

			SCR_DrawStringExt( bx + pad, ky, 4, "Keybinds", textLabel, qtrue );
			ky += 14;

			SCR_DrawStringExt( bx + pad, ky, 3, "Space / P      Pause", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "Left / Right   Skip 5s", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, " + Ctrl        Skip 1s", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, " + Shift       Skip 30s", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "LR (paused)    Step frame", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "Up / Down      Speed +/-", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "F  Freecam   H  Hide HUD", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "PgUp/PgDn      Next/Prev map", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "0-9  Seek 0-90%%  Wheel:Scrub", textMain, qtrue );
			ky += 11;
			SCR_DrawStringExt( bx + pad, ky, 3, "B  Toggle this box", textDim, qtrue );

			SCR_FillRect( bx, kbY + kbH - 1, boxW, 1, accentLine );
		}
	}

	/* ============================================================
	   BOTTOM BAR - progress bar with time labels (6px)
	   ============================================================ */
	{
		float bH = 4;
		float bY = 480 - bH;
		float labelY = bY - 5;
		int i;

		/* Bar background */
		SCR_FillRect( 0, bY, 640, bH, barBg );

		/* Filled portion */
		if ( progress > 0.0f ) {
			SCR_FillRect( 0, bY, 640 * progress, bH, barFill );
		}

		/* Map boundary markers on the progress bar */
		if ( fileLen > 0 ) {
			for ( i = 1; i < totalMaps && i < 32; i++ ) {
				extern int CL_DemoGetMapFileOffset( int idx );
				int moff = CL_DemoGetMapFileOffset( i );
				if ( moff > 0 ) {
					float mx = ( (float)moff / (float)fileLen ) * 640.0f;
					SCR_FillRect( mx, bY, 1, bH, barMarker );
				}
			}
		}

		/* Playhead */
		{
			float hx = 640 * progress;
			if ( hx < 1 ) hx = 1;
			if ( hx > 638 ) hx = 638;
			SCR_FillRect( hx - 1, bY, 3, bH, barHead );
		}

		/* Elapsed / Total time labels above bar */
		{
			Com_sprintf( line, sizeof( line ), "%d:%02d", elapsedMin, elapsedSec );
			SCR_DrawStringExt( 4, labelY, 4, line, textMain, qtrue );
		}
		if ( demoFull > 0 ) {
			Com_sprintf( line, sizeof( line ), "%d:%02d", demoFullMin, demoFullSec );
			{
				int tw = strlen( line ) * 4;
				SCR_DrawStringExt( 636 - tw, labelY, 4, line, textDim, qtrue );
			}
		}
	}
}


/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/

typedef struct
{
	float value;
	int color;
} graphsamp_t;

static int current;
static graphsamp_t values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph( float value, int color ) {
	values[current & 1023].value = value;
	values[current & 1023].color = color;
	current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph( void ) {
	int a, x, y, w, i, h;
	float v;
	int color;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic( x, y - cl_graphheight->integer,
					   w, cl_graphheight->integer, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );

	for ( a = 0 ; a < w ; a++ )
	{
		i = ( current - 1 - a + 1024 ) & 1023;
		v = values[i].value;
		color = values[i].color;
		v = v * cl_graphscale->integer + cl_graphshift->integer;

		if ( v < 0 ) {
			v += cl_graphheight->integer * ( 1 + (int)( -v / cl_graphheight->integer ) );
		}
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x + w - 1 - a, y - h, 1, h, 0, 0, 0, 0, cls.whiteShader );
	}
}

//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void )
{
	cl_timegraph = Cvar_Get( "timegraph", "0", CVAR_CHEAT );
	cl_debuggraph = Cvar_Get( "debuggraph", "0", CVAR_CHEAT );
	cl_graphheight = Cvar_Get( "graphheight", "32", CVAR_CHEAT );
	cl_graphscale = Cvar_Get( "graphscale", "1", CVAR_CHEAT );
	cl_graphshift = Cvar_Get( "graphshift", "0", CVAR_CHEAT );

	// Knightmare added
	scr_surroundlayout = Cvar_Get ("scr_surroundlayout", "1", CVAR_ARCHIVE);	// whether to keep HUD/menu elements on center screen in triple-wide video modes
	scr_surroundleft = Cvar_Get ("scr_surroundleft", "0.333333333333", CVAR_ARCHIVE);		// left placement of HUD/menu elements on center screen in triple-wide video modes
	scr_surroundright = Cvar_Get ("scr_surroundright", "0.666666666667", CVAR_ARCHIVE);		// right placement of HUD/menu elements on center screen in triple-wide video modes
	// end Knightmare

	scr_initialized = qtrue;
}


//=======================================================

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {
	re.BeginFrame( stereoFrame );

	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
	// Knightmare- removed because it causes a bad pointer crash in RE_SetColor at dam -> village2 map change
//	if ( cls.state != CA_ACTIVE ) {
/*	if ( cls.state != CA_ACTIVE && cls.state != CA_CINEMATIC ) {	// Knightmare- fix cinematics in widescreen
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			re.SetColor( g_color_table[0] );
			re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
			re.SetColor( NULL );
		}
	}*/

	// Knightmare removed
/*	if ( !uivm ) {
		Com_DPrintf( "draw screen without UI loaded\n" );
		return;
	}*/

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
//	if ( !VM_Call( uivm, UI_IS_FULLSCREEN ) ) {
	if ( uivm &&  !VM_Call( uivm, UI_IS_FULLSCREEN ) ) {	// Knightmare- fix cinematics in widescreen
		switch ( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			S_StopAllSounds();
			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			// refresh to update the time
			VM_Call( uivm, UI_REFRESH, cls.realtime );
			VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qfalse );
			break;
//			// Ridah, if the cgame is valid, fall through to there
//			if (!cls.cgameStarted || !com_sv_running->integer) {
//				// connecting clients will only show the connection dialog
//				VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qfalse );
//				break;
//			}
		case CA_LOADING:
		case CA_PRIMED:
			// draw the game information screen and loading progress
			CL_CGameRendering( stereoFrame );

			// During demo playback, suppress the connect/loading screen
			// which shows wrong map images and briefing data.
			if ( !clc.demoplaying ) {
				// also draw the connection information, so it doesn't
				// flash away too briefly on local or lan games
				//if (!com_sv_running->value || Cvar_VariableIntegerValue("sv_cheats"))	// Ridah, don't draw useless text if not in dev mode
				// refresh to update the time
				VM_Call( uivm, UI_REFRESH, cls.realtime );
				VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qtrue );
			}
			break;
		case CA_ACTIVE:
			CL_CGameRendering( stereoFrame );
			SCR_DrawDemoRecording();
			SCR_DrawDemoPlayback();
			break;
		}
	}

	// the menu draws next
//	if ( cls.keyCatchers & KEYCATCH_UI && uivm ) {
	if ( Key_GetCatcher( ) & KEYCATCH_UI && uivm ) {	// Knightmare- fix cinematics in widescreen
		VM_Call( uivm, UI_REFRESH, cls.realtime );
	}

	// console draws next
	Con_DrawConsole();

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer || cl_debugMove->integer ) {
		SCR_DrawDebugGraph();
	}

	// LiveSplit overlay - always on top, visible in all states
	SCR_LiveSplitDraw();
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static int recursive;

	if ( !scr_initialized ) {
		return;             // not initialized yet
	}

	/* During demo backward seek, show a minimal "SEEKING..." overlay.
	   We still call BeginFrame/EndFrame so the user gets visual feedback
	   instead of a frozen screen.  We skip the full scene rendering
	   (SCR_DrawScreenField) because cgame state is mid-seek. */
	if ( clc.demoSeekInProgress ) {
		static vec4_t seekBg   = { 0.02f, 0.02f, 0.03f, 0.92f };
		static vec4_t seekText = { 0.70f, 0.90f, 0.50f, 0.95f };
		static vec4_t seekDim  = { 0.40f, 0.50f, 0.35f, 0.60f };

		if ( ++recursive > 2 ) {
			Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
		}

		re.BeginFrame( STEREO_CENTER );
		/* Dark overlay */
		SCR_FillRect( 0, 0, 640, 480, seekBg );
		/* "SEEKING..." centered */
		{
			const char *msg = "SEEKING...";
			int len = strlen( msg );
			int charW = 8;
			int sx = ( 640 - len * charW ) / 2;
			SCR_DrawStringExt( sx, 228, charW, msg, seekText, qtrue );
		}
		/* Show seek progress: current vs target time */
		if ( clc.demoSeekTargetTime > 0 ) {
			int cur = clc.demoCurrentServerTime;
			int tgt = clc.demoSeekTargetTime;
			int curMin = cur / 60000, curSec = ( cur / 1000 ) % 60;
			int tgtMin = tgt / 60000, tgtSec = ( tgt / 1000 ) % 60;
			char buf[64];
			int blen, bsx;
			Com_sprintf( buf, sizeof( buf ), "%d:%02d / %d:%02d",
						curMin, curSec, tgtMin, tgtSec );
			blen = strlen( buf );
			bsx = ( 640 - blen * 6 ) / 2;
			SCR_DrawStringExt( bsx, 246, 6, buf, seekDim, qtrue );
		}
		if ( com_speeds->integer ) {
			re.EndFrame( &time_frontend, &time_backend );
		} else {
			int fe2, be2;
			re.EndFrame( &fe2, &be2 );
		}
		recursive = 0;
		return;
	}

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = 1;

	// if running in stereo, we need to draw the frame twice
	if ( cls.glconfig.stereoEnabled ) {
		SCR_DrawScreenField( STEREO_LEFT );
		SCR_DrawScreenField( STEREO_RIGHT );
	} else {
		SCR_DrawScreenField( STEREO_CENTER );
	}

	if ( com_speeds->integer ) {
		re.EndFrame( &time_frontend, &time_backend );
	} else {
		re.EndFrame( NULL, NULL );
	}

	recursive = 0;
}
