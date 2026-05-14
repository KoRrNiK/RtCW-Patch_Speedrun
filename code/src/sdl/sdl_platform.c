/*
===========================================================================

SDL3 compatibility glue for the legacy WinMain/sys layer.

===========================================================================
*/

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../sys/core/sys_local.h"

WinVars_t g_wv;

LONG WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}
