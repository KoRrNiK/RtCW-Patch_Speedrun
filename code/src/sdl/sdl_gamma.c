#include "sdl_local.h"
#include "../renderer/tr_local.h"

/*
=================
SDLGL_InitGammaSupport

The SDL3 renderer intentionally keeps hardware gamma disabled. RtCW's
software gamma is baked into world textures on upload, which preserves
blackbars and 2D UI colors. Changing r_gamma therefore needs vid_restart.
=================
*/
qboolean SDLGL_InitGammaSupport( void ) {
	return qfalse;
}

void SDLGL_RestoreGamma( void ) {
}

void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
	(void)red;
	(void)green;
	(void)blue;
}
