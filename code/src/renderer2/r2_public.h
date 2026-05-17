/*
===========================================================================

Renderer2 public entry points.

This backend is intentionally separate from the legacy renderer so the client
can switch between implementations without changing the refexport_t contract.

===========================================================================
*/

#ifndef R2_PUBLIC_H
#define R2_PUBLIC_H

#include "../renderer/tr_public.h"

#define R2_BACKEND_CVAR      "r_rendererBackend"
#define R2_BACKEND_LEGACY    "legacy"
#define R2_BACKEND_RENDERER2 "renderer2"

refexport_t *R2_GetRefAPI( int apiVersion, refimport_t *rimp );
const char *R2_GetBackendName( void );
void R2_DebugSetStatusTitle( const char *title );

#endif /* R2_PUBLIC_H */
