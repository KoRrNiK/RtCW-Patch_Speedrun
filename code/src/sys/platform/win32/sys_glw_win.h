/*
===========================================================================

Windows OpenGL state used by the SDL renderer bridge.

===========================================================================
*/

#ifndef RTCW_SYS_GLW_WIN_H
#define RTCW_SYS_GLW_WIN_H

#include <stdio.h>
#if defined( _MSC_VER ) && ( _MSC_VER >= 1200 )
#pragma warning(disable : 4201)
#pragma warning( push )
#endif
#include <windows.h>
#if defined( _MSC_VER ) && ( _MSC_VER >= 1200 )
#pragma warning( pop )
#endif

typedef struct {
	WNDPROC wndproc;

	HDC hDC;
	HGLRC hGLRC;

	HINSTANCE hinstOpenGL;

	qboolean allowdisplaydepthchange;
	qboolean pixelFormatSet;

	int desktopBitsPixel;
	int desktopWidth;
	int desktopHeight;

	qboolean cdsFullscreen;

	DEVMODE dm;

	FILE *log_fp;
} glwstate_t;

extern glwstate_t glw_state;

#endif
