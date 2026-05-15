/*
===========================================================================

SDL system bridge declarations for the Windows SDL3 target.

The engine-facing sys layer lives here, while OS-specific helpers stay under
sys/platform/windows/.

===========================================================================
*/

#ifndef RTCW_SYS_LOCAL_H
#define RTCW_SYS_LOCAL_H

#if defined( _MSC_VER ) && ( _MSC_VER >= 1200 )
#pragma warning(disable : 4201)
#pragma warning( push )
#endif
#include <windows.h>
#if defined( _MSC_VER ) && ( _MSC_VER >= 1200 )
#pragma warning( pop )
#endif

#include <winsock.h>
#include <wsipx.h>

#ifdef __cplusplus
extern "C" {
#endif

void IN_MouseEvent( int mstate );
void IN_RawMouseEvent( int dx, int dy );

void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );

void Sys_CreateConsole( void );
void Sys_DestroyConsole( void );

char *Sys_ConsoleInput( void );

qboolean Sys_GetPacket( netadr_t *net_from, msg_t *net_message );

void IN_Init( void );
void IN_Shutdown( void );
void IN_JoystickCommands( void );
void IN_Move( usercmd_t *cmd );

void IN_Activate( qboolean active );
void IN_DeactivateMouse( void );
void IN_Frame( void );

LONG WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

void Conbuf_AppendText( const char *msg );

void SNDDMA_Activate( void );

typedef struct {
	HINSTANCE reflib_library;
	qboolean reflib_active;

	HWND hWnd;
	HINSTANCE hInstance;
	qboolean activeApp;
	qboolean isMinimized;
	OSVERSIONINFO osversion;

	unsigned sysMsgTime;
} WinVars_t;

extern WinVars_t g_wv;

#ifdef __cplusplus
}
#endif

#endif
