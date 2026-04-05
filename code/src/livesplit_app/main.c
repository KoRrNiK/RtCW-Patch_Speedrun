/*
===========================================================================
RtCW_LiveSplit  --  Standalone LiveSplit window for Return to Castle Wolfenstein

Runs independently of the game.  When WolfSP.exe is running, the game
writes timing data to a named shared-memory block; this exe reads it
and renders the LiveSplit window.  When the game is not running, the
window stays alive showing the last known state.

Build:  cl /O2 /W3 /Fe:RtCW_LiveSplit.exe main.c /link user32.lib gdi32.lib
        comdlg32.lib comctl32.lib uxtheme.lib shell32.lib
===========================================================================
*/

#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <uxtheme.h>
#endif

/* ====================================================================
   Engine shims -- replace Quake 3 engine functions with stdlib
   ==================================================================== */

/* Q_strncpyz: safe string copy (like strlcpy) */
static void Q_strncpyz( char *dest, const char *src, int destsize ) {
	if ( !dest || destsize < 1 ) return;
	if ( !src ) { dest[0] = '\0'; return; }
	strncpy( dest, src, destsize - 1 );
	dest[destsize - 1] = '\0';
}

/* Q_stricmp: case-insensitive compare */
static int Q_stricmp( const char *a, const char *b ) {
	return _stricmp( a, b );
}

/* Com_sprintf: safe formatted print */
static void Com_sprintf( char *dest, int size, const char *fmt, ... ) {
	va_list ap;
	va_start( ap, fmt );
	_vsnprintf( dest, size, fmt, ap );
	va_end( ap );
	dest[size - 1] = '\0';
}

/* va: varargs temp string (rotating buffers) */
#define VA_BUFCOUNT 4
#define VA_BUFSIZE  256
static char va_bufs[VA_BUFCOUNT][VA_BUFSIZE];
static int  va_idx = 0;
static const char *va( const char *fmt, ... ) {
	char *buf = va_bufs[va_idx & (VA_BUFCOUNT-1)];
	va_list ap;
	va_idx++;
	va_start( ap, fmt );
	_vsnprintf( buf, VA_BUFSIZE, fmt, ap );
	va_end( ap );
	buf[VA_BUFSIZE - 1] = '\0';
	return buf;
}

/* ====================================================================
   Cvar shim -- simple key-value store for window geometry & settings
   ==================================================================== */

#define CVAR_MAX  64
#define CVAR_VLEN 256

typedef struct {
	char name[64];
	char value[CVAR_VLEN];
} shim_cvar_t;

static shim_cvar_t shim_cvars[CVAR_MAX];
static int         shim_numCvars = 0;

static const char *Cvar_VariableString( const char *name ) {
	int i;
	for ( i = 0; i < shim_numCvars; i++ ) {
		if ( _stricmp( shim_cvars[i].name, name ) == 0 )
			return shim_cvars[i].value;
	}
	return "";
}

static void Cvar_Set( const char *name, const char *value ) {
	int i;
	for ( i = 0; i < shim_numCvars; i++ ) {
		if ( _stricmp( shim_cvars[i].name, name ) == 0 ) {
			Q_strncpyz( shim_cvars[i].value, value, CVAR_VLEN );
			return;
		}
	}
	if ( shim_numCvars < CVAR_MAX ) {
		Q_strncpyz( shim_cvars[shim_numCvars].name, name, 64 );
		Q_strncpyz( shim_cvars[shim_numCvars].value, value, CVAR_VLEN );
		shim_numCvars++;
	}
}

/* fs_homepath: default to exe directory */
static char shim_homepath[512];

static void Shim_InitHomePath( void ) {
	char buf[512];
	char *last;
	GetModuleFileNameA( NULL, buf, sizeof( buf ) );
	last = strrchr( buf, '\\' );
	if ( !last ) last = strrchr( buf, '/' );
	if ( last ) *last = '\0';
	Q_strncpyz( shim_homepath, buf, sizeof( shim_homepath ) );
	Cvar_Set( "fs_homepath", shim_homepath );
}

/* ====================================================================
   Settings persistence (simple INI-like file)
   ==================================================================== */

static void Shim_GetCfgPath( char *buf, int sz ) {
	Com_sprintf( buf, sz, "%s\\livesplit_standalone.cfg", shim_homepath );
}

static void Shim_LoadCvars( void ) {
	char path[512];
	char line[512];
	FILE *f;
	Shim_GetCfgPath( path, sizeof( path ) );
	f = fopen( path, "r" );
	if ( !f ) return;
	while ( fgets( line, sizeof( line ), f ) ) {
		char name[64], val[CVAR_VLEN];
		/* format: name "value" */
		if ( sscanf( line, "%63s \"%255[^\"]\"", name, val ) == 2 ) {
			Cvar_Set( name, val );
		}
	}
	fclose( f );
}

static void Shim_SaveCvars( void ) {
	char path[512];
	FILE *f;
	int i;
	Shim_GetCfgPath( path, sizeof( path ) );
	f = fopen( path, "w" );
	if ( !f ) return;
	for ( i = 0; i < shim_numCvars; i++ ) {
		if ( _stricmp( shim_cvars[i].name, "fs_homepath" ) == 0 ) continue;
		fprintf( f, "%s \"%s\"\n", shim_cvars[i].name, shim_cvars[i].value );
	}
	fclose( f );
}

/* ====================================================================
   Include the LiveSplit window modules (same code as in-game)
   ==================================================================== */

/* ls_types.h is engine-free */
#include "../client/livesplit/ls_types.h"

/* ---- shared volatile state ---- */
volatile lsWndState_t lswnd_state;

/* ---- macros shared by all modules ---- */
#define LSWND_CLR(rgb)  RGB(((rgb)>>16)&0xFF, ((rgb)>>8)&0xFF, (rgb)&0xFF)
#define LSWND_REV(cr)   ((unsigned)(((GetRValue(cr))<<16)|((GetGValue(cr))<<8)|(GetBValue(cr))))

/* ---- module-level state ---- */
static lsLayout_t    lswnd_layout;
static HWND           lswnd_hwnd      = NULL;
static HANDLE         lswnd_thread    = NULL;
static volatile LONG  lswnd_wantQuit  = 0;
static BOOL           lswnd_dragging  = FALSE;
static POINT          lswnd_dragStart;
static int            lswnd_contentHeight = 0;
static int            lswnd_scrollOffset  = 0;
static int            lswnd_prevCurRow    = -1;

/* standalone exe flag (controls taskbar visibility) */
#define LSWND_STANDALONE 1

/* ---- include modules (order matters) ---- */
#include "../client/livesplit/ls_layout.c"
#include "../client/livesplit/ls_render.c"
#include "../client/livesplit/ls_settings.c"
#include "../client/livesplit/ls_window.c"

/* ====================================================================
   Shared memory IPC
   ==================================================================== */

#include "../client/livesplit/ls_shared.h"

static HANDLE         shm_handle   = NULL;
static lsSharedMem_t *shm_ptr      = NULL;
static long           shm_lastSeq  = -1;

static int SHM_Open( void ) {
	shm_handle = OpenFileMappingA( FILE_MAP_READ, FALSE, LS_SHM_NAME );
	if ( !shm_handle ) return 0;
	shm_ptr = (lsSharedMem_t *)MapViewOfFile( shm_handle, FILE_MAP_READ, 0, 0, sizeof( lsSharedMem_t ) );
	if ( !shm_ptr ) {
		CloseHandle( shm_handle );
		shm_handle = NULL;
		return 0;
	}
	return 1;
}

static void SHM_Close( void ) {
	if ( shm_ptr )    { UnmapViewOfFile( (void *)shm_ptr ); shm_ptr = NULL; }
	if ( shm_handle ) { CloseHandle( shm_handle ); shm_handle = NULL; }
}

static int SHM_Poll( void ) {
	/* If not connected, try to connect */
	if ( !shm_ptr ) {
		if ( !SHM_Open() ) return 0;
	}

	/* Check if the game is still alive */
	if ( shm_ptr->version != LS_SHM_VERSION ) {
		/* data layout mismatch, disconnect */
		SHM_Close();
		return 0;
	}

	/* Check for new data */
	if ( shm_ptr->sequence != shm_lastSeq ) {
		/* copy new state */
		lswnd_state = shm_ptr->state;
		shm_lastSeq = shm_ptr->sequence;
		return 1;
	}

	return 0; /* no new data */
}

/* ====================================================================
   Main entry point
   ==================================================================== */

int WINAPI WinMain( HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow ) {
	int reconnectTimer = 0;
	int wasConnected   = 0;

	(void)hInst; (void)hPrev; (void)cmdLine; (void)nShow;

	/* Make the process per-monitor DPI aware so the LiveSplit window
	   renders at native resolution on high-DPI displays (e.g. 4K 150%).
	   Try the newest API first, fall back for older Windows versions. */
	{
		typedef BOOL  (WINAPI *pfn_SetProcessDpiAwarenessContext)( HANDLE );
		typedef HRESULT (WINAPI *pfn_SetProcessDpiAwareness)( int );
		HMODULE usr = GetModuleHandleA( "user32.dll" );
		HMODULE shc = LoadLibraryA( "shcore.dll" );
		int done = 0;
		if ( usr ) {
			pfn_SetProcessDpiAwarenessContext pCtx =
				(pfn_SetProcessDpiAwarenessContext)GetProcAddress( usr, "SetProcessDpiAwarenessContext" );
			if ( pCtx ) {
				/* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (HANDLE)-4 */
				done = pCtx( (HANDLE)(long long)-4 );
			}
		}
		if ( !done && shc ) {
			pfn_SetProcessDpiAwareness pAw =
				(pfn_SetProcessDpiAwareness)GetProcAddress( shc, "SetProcessDpiAwareness" );
			if ( pAw ) {
				/* PROCESS_PER_MONITOR_DPI_AWARE = 2 */
				done = SUCCEEDED( pAw( 2 ) );
			}
		}
		if ( !done && usr ) {
			typedef BOOL (WINAPI *pfn_SetProcessDPIAware)( void );
			pfn_SetProcessDPIAware pOld =
				(pfn_SetProcessDPIAware)GetProcAddress( usr, "SetProcessDPIAware" );
			if ( pOld ) pOld();
		}
		if ( shc ) FreeLibrary( shc );
	}

	/* Init common controls (needed for settings dialog) */
	{
		INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
		InitCommonControlsEx( &icc );
	}

	/* Init home path and load saved settings */
	Shim_InitHomePath();
	Shim_LoadCvars();

	/* Set defaults for ls_wnd_* if not in saved config */
	if ( !Cvar_VariableString("ls_wnd_w")[0] )  Cvar_Set( "ls_wnd_w", "280" );
	if ( !Cvar_VariableString("ls_wnd_h")[0] )  Cvar_Set( "ls_wnd_h", "500" );
	if ( !Cvar_VariableString("ls_wnd_x")[0] )  Cvar_Set( "ls_wnd_x", "0" );
	if ( !Cvar_VariableString("ls_wnd_y")[0] )  Cvar_Set( "ls_wnd_y", "0" );

	/* Clear shared state */
	memset( (void *)&lswnd_state, 0, sizeof( lswnd_state ) );

	/* Set a "waiting for game" message */
	Q_strncpyz( (char *)lswnd_state.gameName, "Return to Castle Wolfenstein", sizeof( lswnd_state.gameName ) );
	Q_strncpyz( (char *)lswnd_state.categoryText, "Waiting for game...", sizeof( lswnd_state.categoryText ) );
	lswnd_state.attempts = 0;
	lswnd_state.completions = 0;

	/* Create the window (on its own thread with its own message loop) */
	LS_WindowCreate();

	/* Main loop:
	   - poll shared memory for game data
	   - copy new state into the volatile lswnd_state
	   - the window thread repaints at ~120 Hz independently
	   - exit when the user closes the window */
	while ( !lswnd_wantQuit ) {
		/* Try to connect / read new data */
		if ( SHM_Poll() ) {
			/* lswnd_state was updated by SHM_Poll */
			wasConnected   = 1;
			reconnectTimer = 0;
		} else {
			/* No connection or no new data */
			reconnectTimer++;
			if ( reconnectTimer > 60 ) {   /* ~500ms at 8ms sleep */
				if ( wasConnected ) {
					/* game disconnected, show waiting message */
					Q_strncpyz( (char *)lswnd_state.categoryText, "Waiting for game...", sizeof( lswnd_state.categoryText ) );
					wasConnected = 0;
				}
				SHM_Close();
				reconnectTimer = 0;
			}
		}

		Sleep( 8 );
	}

	/* Wait for window thread to finish */
	if ( lswnd_thread ) {
		WaitForSingleObject( lswnd_thread, 3000 );
		CloseHandle( lswnd_thread );
		lswnd_thread = NULL;
	}

	/* Cleanup */
	SHM_Close();
	Shim_SaveCvars();

	return 0;
}
