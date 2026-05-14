/*
===========================================================================

Small SDL target console bridge.

The SDL build should not create the legacy Win32 system-console window.  That
window can steal focus from the SDL window during startup, which makes mouse
capture testing noisy.  Keep the engine-facing functions, but route output to
debug/stdout and keep input passive.

===========================================================================
*/

#include "../../game/q_shared.h"
#include "../../qcommon/qcommon.h"

#include <stdio.h>
#include <windows.h>

static char sys_errorText[4096];

void Sys_CreateConsole( void ) {
}

void Sys_DestroyConsole( void ) {
}

void Sys_ShowConsole( int visLevel, qboolean quitOnClose ) {
	(void)visLevel;
	(void)quitOnClose;
}

void Sys_DisplaySystemConsole( qboolean show ) {
	(void)show;
}

void Sys_SetErrorText( const char *text ) {
	Q_strncpyz( sys_errorText, text ? text : "", sizeof( sys_errorText ) );
}

void Conbuf_AppendText( const char *msg ) {
	if ( !msg ) {
		return;
	}

	OutputDebugStringA( msg );
	fputs( msg, stdout );
}

char *Sys_ConsoleInput( void ) {
	return NULL;
}
