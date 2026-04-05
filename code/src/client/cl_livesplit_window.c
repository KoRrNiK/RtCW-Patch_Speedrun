/*
===========================================================================
cl_livesplit_window.c  --  LiveSplit external window

All code is organised in the livesplit/ folder:
  ls_types.h     - enums, structs, constants, API declarations
  ls_layout.c    - layout defaults, JSON parser / writer, file I/O
  ls_render.c    - gradient fill, component painters, main paint
  ls_settings.c  - colour picker dialog, settings dialog (live preview)
  ls_window.c    - window thread, WndProc, context menu, public API

This file is a unity build wrapper for ls_window.c, which contains the main window thread and WndProc.  It includes all other ls_*.c files in the correct order, so they can share internal static functions and state without exposing them in the public API.	
===========================================================================
*/

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <uxtheme.h>
#endif

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "livesplit/ls_types.h"

/* ---- shared volatile state (game thread -> window thread) --------- */
volatile lsWndState_t lswnd_state;

#ifdef _WIN32

/* ---- macros shared by all modules --------------------------------- */
#define LSWND_CLR(rgb)  RGB(((rgb)>>16)&0xFF, ((rgb)>>8)&0xFF, (rgb)&0xFF)
#define LSWND_REV(cr)   ((unsigned)(((GetRValue(cr))<<16)|((GetGValue(cr))<<8)|(GetBValue(cr))))

/* ---- module-level state ------------------------------------------- */
static lsLayout_t    lswnd_layout;
static HWND           lswnd_hwnd      = NULL;
static HANDLE         lswnd_thread    = NULL;
static volatile LONG  lswnd_wantQuit  = 0;
static BOOL           lswnd_dragging  = FALSE;
static POINT          lswnd_dragStart;
static int            lswnd_contentHeight = 0;
static int            lswnd_scrollOffset  = 0;   /* user scroll offset for splits */
static int            lswnd_prevCurRow    = -1;  /* detect split change for auto-reset */

/* ---- include modules (order matters) ------------------------------ */
#include "livesplit/ls_layout.c"
#include "livesplit/ls_render.c"
#include "livesplit/ls_settings.c"
#include "livesplit/ls_window.c"

#else /* ---- non-Win32 stubs ---------------------------------------- */

void LS_WindowCreate( void )  {}
void LS_WindowDestroy( void ) {}
int  LS_WindowIsActive( void ) { return 0; }
void LS_WindowRepaint( void ) {}
void LS_LayoutSetDefault( lsLayout_t *l ) { if ( l ) memset( l, 0, sizeof( *l ) ); }

#endif
