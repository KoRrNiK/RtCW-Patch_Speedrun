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

// Copyright (C) 1999-2000 Id Software, Inc.
//
/**********************************************************************
	UI_ATOMS.C

	User interface building blocks and support functions.
**********************************************************************/
#include "ui_local.h"

uiStatic_t uis;
qboolean m_entersound;              // after a frame, so caching won't disrupt the sound

// these are here so the functions in q_shared.c can link
#if !defined( UI_HARD_LINKED ) || defined( __MACOS__ )

void QDECL Com_Error( int level, const char *error, ... ) {
	va_list argptr;
	char text[1024];

	va_start( argptr, error );
//	vsprintf( text, error, argptr );
	Q_vsnprintf( text, sizeof(text), error, argptr );	// Knightmare- buffer overflow fix
	va_end( argptr );

	trap_Error( va( "%s", text ) );
}

void QDECL Com_Printf( const char *msg, ... ) {
	va_list argptr;
	char text[1024];

	va_start( argptr, msg );
//	vsprintf( text, msg, argptr );
	Q_vsnprintf( text, sizeof(text), msg, argptr );	// Knightmare- buffer overflow fix
	va_end( argptr );

	trap_Print( va( "%s", text ) );
}

#endif

/*
=================
UI_ClampCvar
=================
*/
float UI_ClampCvar( float min, float max, float value ) {
	if ( value < min ) {
		return min;
	}
	if ( value > max ) {
		return max;
	}
	return value;
}

/*
=================
UI_StartDemoLoop
=================
*/
void UI_StartDemoLoop( void ) {
	trap_Cmd_ExecuteText( EXEC_APPEND, "d1\n" );
}

// TTimo: unused
/*
static void NeedCDAction( qboolean result ) {
	if ( !result ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "quit\n" );
	}
}

static void NeedCDKeyAction( qboolean result ) {
	if ( !result ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, "quit\n" );
	}
}
*/

char *UI_Argv( int arg ) {
	static char buffer[MAX_STRING_CHARS];

	trap_Argv( arg, buffer, sizeof( buffer ) );

	return buffer;
}


char *UI_Cvar_VariableString( const char *var_name ) {
	static char buffer[MAX_STRING_CHARS];

	trap_Cvar_VariableStringBuffer( var_name, buffer, sizeof( buffer ) );

	return buffer;
}


#ifdef MISSIONPACK
void UI_SetBestScores( postGameInfo_t *newInfo, qboolean postGame ) {
	trap_Cvar_Set( "ui_scoreAccuracy",     va( "%i%%", newInfo->accuracy ) );
	trap_Cvar_Set( "ui_scoreImpressives", va( "%i", newInfo->impressives ) );
	trap_Cvar_Set( "ui_scoreExcellents",  va( "%i", newInfo->excellents ) );
	trap_Cvar_Set( "ui_scoreDefends",             va( "%i", newInfo->defends ) );
	trap_Cvar_Set( "ui_scoreAssists",             va( "%i", newInfo->assists ) );
	trap_Cvar_Set( "ui_scoreGauntlets",       va( "%i", newInfo->gauntlets ) );
	trap_Cvar_Set( "ui_scoreScore",               va( "%i", newInfo->score ) );
	trap_Cvar_Set( "ui_scorePerfect",         va( "%i", newInfo->perfects ) );
	trap_Cvar_Set( "ui_scoreTeam",                    va( "%i to %i", newInfo->redScore, newInfo->blueScore ) );
	trap_Cvar_Set( "ui_scoreBase",                    va( "%i", newInfo->baseScore ) );
	trap_Cvar_Set( "ui_scoreTimeBonus",       va( "%i", newInfo->timeBonus ) );
	trap_Cvar_Set( "ui_scoreSkillBonus",      va( "%i", newInfo->skillBonus ) );
	trap_Cvar_Set( "ui_scoreShutoutBonus",    va( "%i", newInfo->shutoutBonus ) );
	trap_Cvar_Set( "ui_scoreTime",                    va( "%02i:%02i", newInfo->time / 60, newInfo->time % 60 ) );
	trap_Cvar_Set( "ui_scoreCaptures",        va( "%i", newInfo->captures ) );
	if ( postGame ) {
		trap_Cvar_Set( "ui_scoreAccuracy2",     va( "%i%%", newInfo->accuracy ) );
		trap_Cvar_Set( "ui_scoreImpressives2",    va( "%i", newInfo->impressives ) );
		trap_Cvar_Set( "ui_scoreExcellents2",     va( "%i", newInfo->excellents ) );
		trap_Cvar_Set( "ui_scoreDefends2",            va( "%i", newInfo->defends ) );
		trap_Cvar_Set( "ui_scoreAssists2",            va( "%i", newInfo->assists ) );
		trap_Cvar_Set( "ui_scoreGauntlets2",      va( "%i", newInfo->gauntlets ) );
		trap_Cvar_Set( "ui_scoreScore2",              va( "%i", newInfo->score ) );
		trap_Cvar_Set( "ui_scorePerfect2",            va( "%i", newInfo->perfects ) );
		trap_Cvar_Set( "ui_scoreTeam2",                   va( "%i to %i", newInfo->redScore, newInfo->blueScore ) );
		trap_Cvar_Set( "ui_scoreBase2",                   va( "%i", newInfo->baseScore ) );
		trap_Cvar_Set( "ui_scoreTimeBonus2",      va( "%i", newInfo->timeBonus ) );
		trap_Cvar_Set( "ui_scoreSkillBonus2",     va( "%i", newInfo->skillBonus ) );
		trap_Cvar_Set( "ui_scoreShutoutBonus2",   va( "%i", newInfo->shutoutBonus ) );
		trap_Cvar_Set( "ui_scoreTime2",                   va( "%02i:%02i", newInfo->time / 60, newInfo->time % 60 ) );
		trap_Cvar_Set( "ui_scoreCaptures2",       va( "%i", newInfo->captures ) );
	}
}
#endif  // #ifdef MISSIONPACK

void UI_LoadBestScores( const char *map, int game ) {
#ifdef MISSIONPACK
	char fileName[MAX_QPATH];
	fileHandle_t f;
	postGameInfo_t newInfo;
	memset( &newInfo, 0, sizeof( postGameInfo_t ) );
	Com_sprintf( fileName, MAX_QPATH, "games/%s_%i.game", map, game );
	if ( trap_FS_FOpenFile( fileName, &f, FS_READ ) >= 0 ) {
		int size = 0;
		trap_FS_Read( &size, sizeof( int ), f );
		if ( size == sizeof( postGameInfo_t ) ) {
			trap_FS_Read( &newInfo, sizeof( postGameInfo_t ), f );
		}
		trap_FS_FCloseFile( f );
	}
	UI_SetBestScores( &newInfo, qfalse );

	Com_sprintf( fileName, MAX_QPATH, "demos/%s_%d.dm_%d", map, game, (int)trap_Cvar_VariableValue( "protocol" ) );
	uiInfo.demoAvailable = qfalse;
	if ( trap_FS_FOpenFile( fileName, &f, FS_READ ) >= 0 ) {
		uiInfo.demoAvailable = qtrue;
		trap_FS_FCloseFile( f );
	}
#endif  // #ifdef MISSIONPACK
}

/*
===============
UI_ClearScores
===============
*/
void UI_ClearScores() {
#ifdef MISSIONPACK
	char gameList[4096];
	char *gameFile;
	int i, len, count, size;
	fileHandle_t f;
	postGameInfo_t newInfo;

	count = trap_FS_GetFileList( "games", "game", gameList, sizeof( gameList ) );

	size = sizeof( postGameInfo_t );
	memset( &newInfo, 0, size );

	if ( count > 0 ) {
		gameFile = gameList;
		for ( i = 0; i < count; i++ ) {
			len = strlen( gameFile );
			if ( trap_FS_FOpenFile( va( "games/%s",gameFile ), &f, FS_WRITE ) >= 0 ) {
				trap_FS_Write( &size, sizeof( int ), f );
				trap_FS_Write( &newInfo, size, f );
				trap_FS_FCloseFile( f );
			}
			gameFile += len + 1;
		}
	}

	UI_SetBestScores( &newInfo, qfalse );
#endif  // #ifdef MISSIONPACK

}



static void UI_Cache_f() {
	Display_CacheAll();
}

/*
=======================
UI_CalcPostGameStats
=======================
*/
static void UI_CalcPostGameStats() {
#ifdef MISSIONPACK
	char map[MAX_QPATH];
	char fileName[MAX_QPATH];
	char info[MAX_INFO_STRING];
	fileHandle_t f;
	int size, game, time, adjustedTime;
	postGameInfo_t oldInfo;
	postGameInfo_t newInfo;
	qboolean newHigh = qfalse;

	trap_GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	Q_strncpyz( map, Info_ValueForKey( info, "mapname" ), sizeof( map ) );
	game = atoi( Info_ValueForKey( info, "g_gametype" ) );

	// compose file name
	Com_sprintf( fileName, MAX_QPATH, "games/%s_%i.game", map, game );
	// see if we have one already
	memset( &oldInfo, 0, sizeof( postGameInfo_t ) );
	if ( trap_FS_FOpenFile( fileName, &f, FS_READ ) >= 0 ) {
		// if so load it
		size = 0;
		trap_FS_Read( &size, sizeof( int ), f );
		if ( size == sizeof( postGameInfo_t ) ) {
			trap_FS_Read( &oldInfo, sizeof( postGameInfo_t ), f );
		}
		trap_FS_FCloseFile( f );
	}

	newInfo.accuracy = atoi( UI_Argv( 3 ) );
	newInfo.impressives = atoi( UI_Argv( 4 ) );
	newInfo.excellents = atoi( UI_Argv( 5 ) );
	newInfo.defends = atoi( UI_Argv( 6 ) );
	newInfo.assists = atoi( UI_Argv( 7 ) );
	newInfo.gauntlets = atoi( UI_Argv( 8 ) );
	newInfo.baseScore = atoi( UI_Argv( 9 ) );
	newInfo.perfects = atoi( UI_Argv( 10 ) );
	newInfo.redScore = atoi( UI_Argv( 11 ) );
	newInfo.blueScore = atoi( UI_Argv( 12 ) );
	time = atoi( UI_Argv( 13 ) );
	newInfo.captures = atoi( UI_Argv( 14 ) );

	newInfo.time = ( time - trap_Cvar_VariableValue( "ui_matchStartTime" ) ) / 1000;
	adjustedTime = uiInfo.mapList[ui_currentMap.integer].timeToBeat[game];
	if ( newInfo.time < adjustedTime ) {
		newInfo.timeBonus = ( adjustedTime - newInfo.time ) * 10;
	} else {
		newInfo.timeBonus = 0;
	}

	if ( newInfo.redScore > newInfo.blueScore && newInfo.blueScore <= 0 ) {
		newInfo.shutoutBonus = 100;
	} else {
		newInfo.shutoutBonus = 0;
	}

	newInfo.skillBonus = trap_Cvar_VariableValue( "g_spSkill" );
	if ( newInfo.skillBonus <= 0 ) {
		newInfo.skillBonus = 1;
	}
	newInfo.score = newInfo.baseScore + newInfo.shutoutBonus + newInfo.timeBonus;
	newInfo.score *= newInfo.skillBonus;

	// see if the score is higher for this one
	newHigh = ( newInfo.redScore > newInfo.blueScore && newInfo.score > oldInfo.score );

	if  ( newHigh ) {
		// if so write out the new one
		uiInfo.newHighScoreTime = uiInfo.uiDC.realTime + 20000;
		if ( trap_FS_FOpenFile( fileName, &f, FS_WRITE ) >= 0 ) {
			size = sizeof( postGameInfo_t );
			trap_FS_Write( &size, sizeof( int ), f );
			trap_FS_Write( &newInfo, sizeof( postGameInfo_t ), f );
			trap_FS_FCloseFile( f );
		}
	}

	if ( newInfo.time < oldInfo.time ) {
		uiInfo.newBestTime = uiInfo.uiDC.realTime + 20000;
	}

	// put back all the ui overrides
	trap_Cvar_Set( "capturelimit", UI_Cvar_VariableString( "ui_saveCaptureLimit" ) );
	trap_Cvar_Set( "fraglimit", UI_Cvar_VariableString( "ui_saveFragLimit" ) );
	trap_Cvar_Set( "cg_drawTimer", UI_Cvar_VariableString( "ui_drawTimer" ) );
	trap_Cvar_Set( "g_doWarmup", UI_Cvar_VariableString( "ui_doWarmup" ) );
	trap_Cvar_Set( "g_Warmup", UI_Cvar_VariableString( "ui_Warmup" ) );
	trap_Cvar_Set( "sv_pure", UI_Cvar_VariableString( "ui_pure" ) );
	trap_Cvar_Set( "g_friendlyFire", UI_Cvar_VariableString( "ui_friendlyFire" ) );

	UI_SetBestScores( &newInfo, qtrue );
	UI_ShowPostGame( newHigh );

#endif  // #ifdef MISSIONPACK

}


/*
=================
UI_ConsoleCommand
=================
*/
qboolean UI_ConsoleCommand( int realTime ) {
	char    *cmd;

	uiInfo.uiDC.frameTime = realTime - uiInfo.uiDC.realTime;
	uiInfo.uiDC.realTime = realTime;

	cmd = UI_Argv( 0 );

	// ensure minimum menu data is available
	//Menu_Cache();

	if ( Q_stricmp( cmd, "ui_test" ) == 0 ) {
		UI_ShowPostGame( qtrue );
	}

	if ( Q_stricmp( cmd, "ui_report" ) == 0 ) {
		UI_Report();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "ui_load" ) == 0 ) {
		UI_Load();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "remapShader" ) == 0 ) {
		if ( trap_Argc() == 4 ) {
			char shader1[MAX_QPATH];
			char shader2[MAX_QPATH];
			Q_strncpyz( shader1, UI_Argv( 1 ), sizeof( shader1 ) );
			Q_strncpyz( shader2, UI_Argv( 2 ), sizeof( shader2 ) );
			trap_R_RemapShader( shader1, shader2, UI_Argv( 3 ) );
			return qtrue;
		}
	}

	if ( Q_stricmp( cmd, "postgame" ) == 0 ) {
		UI_CalcPostGameStats();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "ui_cache" ) == 0 ) {
		UI_Cache_f();
		return qtrue;
	}

	if ( Q_stricmp( cmd, "ui_teamOrders" ) == 0 ) {
		//UI_TeamOrdersMenu_f();
		return qtrue;
	}


	if ( Q_stricmp( cmd, "ui_cdkey" ) == 0 ) {
		//UI_CDKeyMenu_f();
		return qtrue;
	}

	return qfalse;
}

/*
=================
UI_Shutdown
=================
*/
void UI_Shutdown( void ) {
}

static qboolean UI_GetBlackbarSafeRange( float *safeLeft, float *safeRight ) {
	int left, right, total, maxTotal;

	if ( !trap_Cvar_VariableValue( "cg_blackbars" ) ) return qfalse;
	left = (int)trap_Cvar_VariableValue( "cg_blackbarLeft" );
	right = (int)trap_Cvar_VariableValue( "cg_blackbarRight" );
	if ( left < 0 ) left = 0;
	if ( right < 0 ) right = 0;
	if ( left <= 0 && right <= 0 ) return qfalse;

	maxTotal = uiInfo.uiDC.glconfig.vidWidth - 320;
	if ( maxTotal < 0 ) maxTotal = 0;
	total = left + right;
	if ( total > maxTotal && total > 0 ) {
		left = (int)( (float)left * (float)maxTotal / (float)total );
		right = maxTotal - left;
	}

	if ( safeLeft ) *safeLeft = (float)left;
	if ( safeRight ) *safeRight = (float)( uiInfo.uiDC.glconfig.vidWidth - right );
	return qtrue;
}

/*
================
UI_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void UI_AdjustFrom640( float *x, float *y, float *w, float *h, scralign_t align ) {
	float	xscale, lb_xscale, yscale, minscale, vertscale;	// Knightmare added
	float	tmp_x, tmp_y, tmp_w, tmp_h, tmp_left, tmp_right;	// Knightmare added
	float	xleft, xright, safeLeft, safeRight;
	qboolean blackbarSafe;

	int scr_surroundlayout = trap_Cvar_VariableValue( "scr_surroundlayout" );
	float scr_surroundleft = trap_Cvar_VariableValue( "scr_surroundleft" );
	float scr_surroundright = trap_Cvar_VariableValue( "scr_surroundright" );

	// expect valid pointers
#if 0
	*x = *x * uiInfo.uiDC.scale + uiInfo.uiDC.bias;
	*y *= uiInfo.uiDC.scale;
	*w *= uiInfo.uiDC.scale;
	*h *= uiInfo.uiDC.scale;
#endif

	// for eyefinity/surround setups, keep everything on the center monitor
	if (scr_surroundlayout != 0 && uiInfo.uiDC.screenAspect >= 3.6f)
	{
		if (scr_surroundleft > 0.0f && scr_surroundleft < 1.0f)
			xleft = (float)uiInfo.uiDC.glconfig.vidWidth * scr_surroundleft;
		else
			xleft = (float)uiInfo.uiDC.glconfig.vidWidth / 3.0f;
		if (scr_surroundright > 0.0f && scr_surroundright < 1.0f)
			xright = (float)uiInfo.uiDC.glconfig.vidWidth * scr_surroundright;
		else
			xright = (float)uiInfo.uiDC.glconfig.vidWidth * (2.0f / 3.0f);
		xscale = (xright - xleft) / SCREEN_WIDTH;
	}
	else
	{
		xleft = 0.0f;
		xright = (float)uiInfo.uiDC.glconfig.vidWidth;
		xscale = (float)uiInfo.uiDC.glconfig.vidWidth / SCREEN_WIDTH;
	}
	blackbarSafe = UI_GetBlackbarSafeRange( &safeLeft, &safeRight );
	if ( blackbarSafe ) {
		if ( safeLeft > xleft ) xleft = safeLeft;
		if ( safeRight < xright ) xright = safeRight;
		if ( xright <= xleft ) {
			xleft = safeLeft;
			xright = safeRight;
		}
		xscale = ( xright - xleft ) / SCREEN_WIDTH;
	}
	lb_xscale = blackbarSafe ? xscale : (float)uiInfo.uiDC.glconfig.vidWidth / SCREEN_WIDTH;
	yscale = (float)uiInfo.uiDC.glconfig.vidHeight / SCREEN_HEIGHT;
	minscale = min(xscale, yscale);

	// hack for 5:4 modes
	if ( !(xscale > yscale) && align != ALIGN_LETTERBOX)
		align = ALIGN_STRETCH;

	// scale for screen sizes
	// Knightmare- added anamorphic code
	switch (align)
	{
	case ALIGN_CENTER:
		if (x) {
			tmp_x = *x;
			*x = (tmp_x - (0.5 * SCREEN_WIDTH)) * minscale + (0.5 * ( xleft + xright ));
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - (0.5 * SCREEN_HEIGHT)) * minscale + (0.5 * uiInfo.uiDC.glconfig.vidHeight);
		}
		if (w) 
			*w *= minscale;
		if (h)
			*h *= minscale;
		break;
	case ALIGN_LETTERBOX:
		// special case: video mode (eyefinity?) is wider than object
		if ( w != NULL && h != NULL && ((float)uiInfo.uiDC.glconfig.vidWidth / (float)uiInfo.uiDC.glconfig.vidHeight > *w / *h) ) {
			tmp_h = *h;
			vertscale = uiInfo.uiDC.glconfig.vidHeight / tmp_h;
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
			if (y != NULL && h != NULL) {
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
			*x = (tmp_x - (0.5 * SCREEN_WIDTH)) * minscale + (0.5 * ( xleft + xright ));
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
			*x = (tmp_x - (0.5 * SCREEN_WIDTH)) * minscale + (0.5 * ( xleft + xright ));
		}
		if (y) {
			tmp_y = *y;
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + uiInfo.uiDC.glconfig.vidHeight;
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
			*y = (tmp_y - (0.5 * SCREEN_HEIGHT)) * minscale + (0.5 * uiInfo.uiDC.glconfig.vidHeight);
		}
		break;
	case ALIGN_LEFT:
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
			*y = (tmp_y - (0.5 * SCREEN_HEIGHT)) * minscale + (0.5 * uiInfo.uiDC.glconfig.vidHeight);
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
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + uiInfo.uiDC.glconfig.vidHeight;
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
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + uiInfo.uiDC.glconfig.vidHeight;
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
			*y = (tmp_y - SCREEN_HEIGHT) * minscale + uiInfo.uiDC.glconfig.vidHeight;
		}
		break;
	case ALIGN_STRETCH_ALL:
		if (x)
			*x = *x * lb_xscale + ( blackbarSafe ? xleft : 0.0f );
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
			tmp_right = (tmp_x + tmp_w - (0.5*SCREEN_WIDTH)) * minscale + (0.5*(xleft + xright));
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
			tmp_left = (tmp_x - (0.5*SCREEN_WIDTH)) * minscale + (0.5*(xleft + xright));
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
/*
	*x *= uiInfo.uiDC.xscale;
	*y *= uiInfo.uiDC.yscale;
	*w *= uiInfo.uiDC.xscale;
	*h *= uiInfo.uiDC.yscale;
*/
}

void UI_DrawNamedPic( float x, float y, float width, float height, const char *picname, scralign_t align ) {
	qhandle_t hShader;

	hShader = trap_R_RegisterShaderNoMip( picname );
	UI_AdjustFrom640( &x, &y, &width, &height, align );
	trap_R_DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}

void UI_DrawHandlePic( float x, float y, float w, float h, qhandle_t hShader, scralign_t align ) {
	float s0;
	float s1;
	float t0;
	float t1;

	if ( w < 0 ) {   // flip about vertical
		w  = -w;
		s0 = 1;
		s1 = 0;
	} else {
		s0 = 0;
		s1 = 1;
	}

	if ( h < 0 ) {   // flip about horizontal
		h  = -h;
		t0 = 1;
		t1 = 0;
	} else {
		t0 = 0;
		t1 = 1;
	}

	UI_AdjustFrom640( &x, &y, &w, &h, align );
	trap_R_DrawStretchPic( x, y, w, h, s0, t0, s1, t1, hShader );
}

/*
================
UI_FillRect

Coordinates are 640*480 virtual values
=================
*/
void UI_FillRect( float x, float y, float width, float height, const float *color, scralign_t align ) {
	trap_R_SetColor( color );

	UI_AdjustFrom640( &x, &y, &width, &height, align );
	trap_R_DrawStretchPic( x, y, width, height, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );

	trap_R_SetColor( NULL );
}

void UI_DrawSides( float x, float y, float w, float h, scralign_t align ) {
	UI_AdjustFrom640( &x, &y, &w, &h, align );
	trap_R_DrawStretchPic( x, y, 1, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap_R_DrawStretchPic( x + w - 1, y, 1, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
}

void UI_DrawTopBottom( float x, float y, float w, float h, scralign_t align ) {
	UI_AdjustFrom640( &x, &y, &w, &h,align );
	trap_R_DrawStretchPic( x, y, w, 1, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
	trap_R_DrawStretchPic( x, y + h - 1, w, 1, 0, 0, 0, 0, uiInfo.uiDC.whiteShader );
}
/*
================
UI_DrawRect

Coordinates are 640*480 virtual values
=================
*/
void UI_DrawRect( float x, float y, float width, float height, const float *color, scralign_t align ) {
	trap_R_SetColor( color );

	UI_DrawTopBottom( x, y, width, height, align );
	UI_DrawSides( x, y, width, height, align );

	trap_R_SetColor( NULL );
}

void UI_SetColor( const float *rgba ) {
	trap_R_SetColor( rgba );
}

void UI_UpdateScreen( void ) {
	trap_UpdateScreen();
}


void UI_DrawTextBox( int x, int y, int width, int lines, scralign_t align ) {
	UI_FillRect( x + BIGCHAR_WIDTH / 2, y + BIGCHAR_HEIGHT / 2, ( width + 1 ) * BIGCHAR_WIDTH, ( lines + 1 ) * BIGCHAR_HEIGHT, colorBlack, align );
	UI_DrawRect( x + BIGCHAR_WIDTH / 2, y + BIGCHAR_HEIGHT / 2, ( width + 1 ) * BIGCHAR_WIDTH, ( lines + 1 ) * BIGCHAR_HEIGHT, colorWhite, align );
}

qboolean UI_CursorInRect( int x, int y, int width, int height ) {
	if ( uiInfo.uiDC.cursorx < x ||
		 uiInfo.uiDC.cursory < y ||
		 uiInfo.uiDC.cursorx > x + width ||
		 uiInfo.uiDC.cursory > y + height ) {
		return qfalse;
	}
	return qtrue;
}
