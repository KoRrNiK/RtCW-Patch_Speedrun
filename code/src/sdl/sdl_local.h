/*
===========================================================================

SDL3 platform glue for the Win32 test target.

===========================================================================
*/

#ifndef RTCW_SDL_LOCAL_H
#define RTCW_SDL_LOCAL_H

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

#include <SDL3/SDL.h>

extern SDL_Window *sdl_window;
extern SDL_GLContext sdl_gl_context;

void IN_SuppressResizeEvents( int msec );

qboolean SDLGL_InitGammaSupport( void );
void SDLGL_RestoreGamma( void );

#endif
