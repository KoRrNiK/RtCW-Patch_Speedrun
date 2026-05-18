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

cvar_t      *scr_drawLRT;
cvar_t      *scr_lrtTime;
cvar_t      *scr_lrtLastRealTime;
cvar_t      *scr_lrtReset;
cvar_t      *scr_lrtX;
cvar_t      *scr_lrtY;
cvar_t      *scr_lrtScale;
cvar_t      *scr_drawLRTSegment;
cvar_t      *scr_lrtSegmentTime;
cvar_t      *scr_lrtSegmentAlign;

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
static void SCR_DrawChar( int x, int y, float size, int ch ) {
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
	color[3] = setColor[3];
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawChar( xx + 2, y + 2, size, *s );
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
=================
*/
void SCR_DrawDemoRecording( void ) {
	char string[1024];
	int pos;

	if ( !clc.demorecording ) {
		return;
	}

	pos = FS_FTell( clc.demofile );
	sprintf( string, "RECORDING %s: %ik", clc.demoName, pos / 1024 );

	SCR_DrawStringExt( 320 - strlen( string ) * 4, 20, 8, string, g_color_table[7], qtrue );
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

typedef struct {
	qboolean initialized;
	qboolean running;
	int accumulatedMs;
	int segmentMs;
	int lastRealMs;
	int lastPersistMs;
	char segmentMapname[MAX_QPATH];
} scrLRTTimer_t;

static scrLRTTimer_t scr_lrtTimer;

/*
=================
SCR_LRTPersist
=================
*/
static void SCR_LRTPersist( qboolean force ) {
	char value[32];
	int diff;

	diff = scr_lrtTimer.accumulatedMs - scr_lrtTimer.lastPersistMs;
	if ( diff < 0 ) {
		diff = -diff;
	}
	if ( !force && diff < 250 ) {
		return;
	}

	Com_sprintf( value, sizeof( value ), "%d", scr_lrtTimer.accumulatedMs );
	Cvar_Set( "cg_lrt_ms", value );
	Com_sprintf( value, sizeof( value ), "%d", scr_lrtTimer.segmentMs );
	Cvar_Set( "cg_lrt_segment_ms", value );
	Com_sprintf( value, sizeof( value ), "%d", scr_lrtTimer.lastRealMs );
	Cvar_Set( "cg_lrt_last_real_ms", value );
	scr_lrtTimer.lastPersistMs = scr_lrtTimer.accumulatedMs;
}

/*
=================
SCR_LRTShouldRun
=================
*/
static qboolean SCR_LRTShouldRun( void ) {
	int savegameLoading;
	int reloading;

	if ( !cl.mapname[0] ) {
		return qfalse;
	}
	if ( cls.state != CA_ACTIVE && cls.state != CA_CINEMATIC ) {
		return qfalse;
	}
	if ( cl_missionStats && strlen( cl_missionStats->string ) > 1 ) {
		return qfalse;
	}
	if ( Cvar_VariableIntegerValue( "cg_norender" ) ) {
		savegameLoading = Cvar_VariableIntegerValue( "savegame_loading" );
		reloading = Cvar_VariableIntegerValue( "g_reloading" );
		if ( savegameLoading != 2 &&
			 !( reloading == RELOAD_SAVEGAME && scr_lrtTimer.running ) ) {
			return qfalse;
		}
	}
	if ( com_sv_running && com_sv_running->integer &&
		 cl_paused && cl_paused->integer &&
		 sv_paused && sv_paused->integer ) {
		return qfalse;
	}
	return qtrue;
}

/*
=================
SCR_LRTUpdateSegment
=================
*/
static qboolean SCR_LRTUpdateSegment( qboolean reset ) {
	if ( !cl.mapname[0] ) {
		if ( reset ) {
			scr_lrtTimer.segmentMs = 0;
			scr_lrtTimer.segmentMapname[0] = '\0';
		}
		return reset;
	}

	if ( reset || !scr_lrtTimer.segmentMapname[0] ||
		 Q_stricmp( scr_lrtTimer.segmentMapname, cl.mapname ) ) {
		Q_strncpyz( scr_lrtTimer.segmentMapname, cl.mapname, sizeof( scr_lrtTimer.segmentMapname ) );
		scr_lrtTimer.segmentMs = 0;
		return qtrue;
	}

	return qfalse;
}

/*
=================
SCR_LRTInit
=================
*/
static void SCR_LRTInit( void ) {
	int now;

	scr_lrtTimer.accumulatedMs = scr_lrtTime ? scr_lrtTime->integer : 0;
	if ( scr_lrtTimer.accumulatedMs < 0 ) {
		scr_lrtTimer.accumulatedMs = 0;
	}

	now = Sys_Milliseconds();
	scr_lrtTimer.lastRealMs = now;
	scr_lrtTimer.lastPersistMs = scr_lrtTimer.accumulatedMs;
	scr_lrtTimer.running = qfalse;
	SCR_LRTUpdateSegment( qtrue );
	scr_lrtTimer.initialized = qtrue;
	SCR_LRTPersist( qtrue );
}

/*
=================
SCR_LRTReset
=================
*/
static void SCR_LRTReset( void ) {
	scr_lrtTimer.initialized = qtrue;
	scr_lrtTimer.accumulatedMs = 0;
	scr_lrtTimer.segmentMs = 0;
	scr_lrtTimer.lastRealMs = Sys_Milliseconds();
	scr_lrtTimer.lastPersistMs = -1;
	scr_lrtTimer.running = qfalse;
	SCR_LRTUpdateSegment( qtrue );
	SCR_LRTPersist( qtrue );
	Cvar_Set( "cg_lrt_reset", "0" );
	Com_Printf( "^2LRT reset\n" );
}

/*
=================
SCR_LRTReset_f
=================
*/
static void SCR_LRTReset_f( void ) {
	SCR_LRTReset();
}

/*
=================
SCR_LRTUpdate
=================
*/
static void SCR_LRTUpdate( void ) {
	int now;
	int delta;
	qboolean shouldRun;

	if ( !scr_lrtTimer.initialized ) {
		SCR_LRTInit();
	}

	if ( scr_lrtReset && scr_lrtReset->integer ) {
		SCR_LRTReset();
		return;
	}

	now = Sys_Milliseconds();
	if ( SCR_LRTUpdateSegment( qfalse ) ) {
		scr_lrtTimer.running = qfalse;
	}

	shouldRun = SCR_LRTShouldRun();
	if ( !shouldRun ) {
		scr_lrtTimer.lastRealMs = now;
		scr_lrtTimer.running = qfalse;
		SCR_LRTPersist( qfalse );
		return;
	}
	if ( !scr_lrtTimer.running ) {
		scr_lrtTimer.lastRealMs = now;
		scr_lrtTimer.running = qtrue;
		SCR_LRTPersist( qfalse );
		return;
	}

	delta = now - scr_lrtTimer.lastRealMs;
	scr_lrtTimer.lastRealMs = now;
	if ( delta <= 0 ) {
		return;
	}
	if ( delta > 600000 ) {
		return;
	}

	scr_lrtTimer.accumulatedMs += delta;
	scr_lrtTimer.segmentMs += delta;
	SCR_LRTPersist( qfalse );
}

int SCR_LRTGetTime( void ) {
	return scr_lrtTimer.accumulatedMs;
}

int SCR_LRTGetSegmentTime( void ) {
	return scr_lrtTimer.segmentMs;
}

qboolean SCR_LRTIsRunning( void ) {
	return scr_lrtTimer.running;
}

/*
=================
SCR_LRTFormatTime
=================
*/
static void SCR_LRTFormatTime( int msec, char *buffer, int bufferSize ) {
	int hours;
	int mins;
	int seconds;
	int hundredths;

	if ( msec < 0 ) {
		msec = 0;
	}

	hours = msec / 3600000;
	msec -= hours * 3600000;
	mins = msec / 60000;
	msec -= mins * 60000;
	seconds = msec / 1000;
	hundredths = ( msec % 1000 ) / 10;

	if ( hours > 0 ) {
		Com_sprintf( buffer, bufferSize, "%d:%02d:%02d.%02d", hours, mins, seconds, hundredths );
	} else {
		Com_sprintf( buffer, bufferSize, "%d:%02d.%02d", mins, seconds, hundredths );
	}
}

/*
=================
SCR_LRTScale
=================
*/
static float SCR_LRTScale( float multiplier ) {
	float scale;

	scale = scr_lrtScale ? scr_lrtScale->value : 1.0f;
	if ( scale < 0.25f ) {
		scale = 0.25f;
	} else if ( scale > 3.0f ) {
		scale = 3.0f;
	}

	return scale * multiplier;
}

static int SCR_LRTSegmentAlign( void ) {
	if ( !scr_lrtSegmentAlign ) {
		return 0;
	}
	if ( !Q_stricmp( scr_lrtSegmentAlign->string, "left" ) ||
		 scr_lrtSegmentAlign->integer == 1 ) {
		return 1;
	}
	if ( !Q_stricmp( scr_lrtSegmentAlign->string, "center" ) ||
		 scr_lrtSegmentAlign->integer == 2 ) {
		return 2;
	}
	return 0;
}

/*
=================
SCR_LRTDrawTime
=================
*/
static void SCR_LRTDrawTime( int x, int y, int msec, float scale ) {
	char timeText[32];
	float color[4];
	float size;
	float width;

	SCR_LRTFormatTime( msec, timeText, sizeof( timeText ) );

	size = BIGCHAR_WIDTH * scale;
	width = SCR_Strlen( timeText ) * size;
	color[0] = color[1] = color[2] = 1.0f;
	color[3] = 1.0f;

	SCR_DrawStringExt( (int)( x - width ), y, size, timeText, color, qfalse );
}

static void SCR_LRTDrawSegmentTime( int x, int y, int msec, float scale, float mainWidth ) {
	char timeText[32];
	float color[4];
	float size;
	float width;
	int drawX;

	SCR_LRTFormatTime( msec, timeText, sizeof( timeText ) );

	size = BIGCHAR_WIDTH * scale;
	width = SCR_Strlen( timeText ) * size;
	color[0] = color[1] = color[2] = 1.0f;
	color[3] = 1.0f;

	switch ( SCR_LRTSegmentAlign() ) {
	case 1:
		drawX = (int)( x - mainWidth );
		break;
	case 2:
		drawX = (int)( x - mainWidth * 0.5f - width * 0.5f );
		break;
	default:
		drawX = (int)( x - width );
		break;
	}

	SCR_DrawStringExt( drawX, y, size, timeText, color, qfalse );
}

/*
=================
SCR_DrawLRTOverlay
=================
*/
static void SCR_DrawLRTOverlay( void ) {
	int x;
	int y;
	float scale;
	float segmentScale;
	float mainWidth;
	char timeText[32];

	if ( !scr_drawLRT || !scr_drawLRT->integer ) {
		return;
	}

	x = scr_lrtX ? scr_lrtX->integer : 500;
	y = scr_lrtY ? scr_lrtY->integer : 0;
	scale = SCR_LRTScale( 1.0f );
	segmentScale = SCR_LRTScale( 0.65f );
	SCR_LRTFormatTime( scr_lrtTimer.accumulatedMs, timeText, sizeof( timeText ) );
	mainWidth = SCR_Strlen( timeText ) * BIGCHAR_WIDTH * scale;

	SCR_LRTDrawTime( x, y + 2, scr_lrtTimer.accumulatedMs, scale );
	if ( scr_drawLRTSegment && scr_drawLRTSegment->integer ) {
		SCR_LRTDrawSegmentTime( x, (int)( y + 4 + BIGCHAR_HEIGHT * scale ), scr_lrtTimer.segmentMs, segmentScale, mainWidth );
	}
}

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

	scr_drawLRT = Cvar_Get( "cg_drawLRT", "1", CVAR_ARCHIVE );
	scr_lrtTime = Cvar_Get( "cg_lrt_ms", "0", 0 );
	scr_lrtLastRealTime = Cvar_Get( "cg_lrt_last_real_ms", "0", 0 );
	scr_lrtReset = Cvar_Get( "cg_lrt_reset", "0", 0 );
	scr_lrtX = Cvar_Get( "cg_lrt_x", "500", CVAR_ARCHIVE );
	scr_lrtY = Cvar_Get( "cg_lrt_y", "0", CVAR_ARCHIVE );
	scr_lrtScale = Cvar_Get( "cg_lrt_scale", "1", CVAR_ARCHIVE );
	scr_drawLRTSegment = Cvar_Get( "cg_drawLRTSegment", "0", CVAR_ARCHIVE );
	scr_lrtSegmentTime = Cvar_Get( "cg_lrt_segment_ms", "0", 0 );
	scr_lrtSegmentAlign = Cvar_Get( "cg_lrt_segment_align", "right", CVAR_ARCHIVE );
	Cmd_AddCommand( "lrt_reset", SCR_LRTReset_f );

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

			// also draw the connection information, so it doesn't
			// flash away too briefly on local or lan games
			//if (!com_sv_running->value || Cvar_VariableIntegerValue("sv_cheats"))	// Ridah, don't draw useless text if not in dev mode
			// refresh to update the time
			VM_Call( uivm, UI_REFRESH, cls.realtime );
			VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qtrue );
			break;
		case CA_ACTIVE:
			CL_CGameRendering( stereoFrame );
			SCR_DrawDemoRecording();
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

	SCR_DrawLRTOverlay();
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

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = 1;

	SCR_LRTUpdate();
	CL_UpdateASLState();

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
