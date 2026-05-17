/*
===========================================================================

Renderer2 bootstrap.

The OpenGL 4 renderer will be grown behind this entry point.  Returning NULL
keeps the client on the legacy renderer until renderer2 has a complete,
validated refexport_t implementation.

===========================================================================
*/

#include "r2_local.h"

refimport_t r2_ri;

#define R2_MAX_SCENE_POLYS 4096
#define R2_MAX_SCENE_POLY_VERTS 32768

static poly_t r2_scenePolys[R2_MAX_SCENE_POLYS];
static polyVert_t r2_scenePolyVerts[R2_MAX_SCENE_POLY_VERTS];
static int r2_numScenePolys;
static int r2_numScenePolyVerts;
static cvar_t *r2_statsOverlay;

r2DebugStats_t r2_debugStats;

static float R2_ClampUnit( float value ) {
	if ( value < 0.0f ) {
		return 0.0f;
	}
	if ( value > 1.0f ) {
		return 1.0f;
	}
	return value;
}

static void R2_UpdateFogClearColor( void ) {
	if ( r2.activeFog > FOG_NONE && r2.activeFog < NUM_FOGS && r2.fogRegistered[r2.activeFog] ) {
		r2.fogClearColor[0] = r2.fogColor[r2.activeFog][0];
		r2.fogClearColor[1] = r2.fogColor[r2.activeFog][1];
		r2.fogClearColor[2] = r2.fogColor[r2.activeFog][2];
		r2.useFogClearColor = qtrue;
		return;
	}

	r2.useFogClearColor = qfalse;
}

static void R2_ClearFogState( void ) {
	memset( r2.fogRegistered, 0, sizeof( r2.fogRegistered ) );
	memset( r2.fogColor, 0, sizeof( r2.fogColor ) );
	VectorClear( r2.fogClearColor );
	r2.activeFog = FOG_NONE;
	r2.useFogClearColor = qfalse;
}

static void R2_Shutdown( qboolean destroyWindow ) {
	(void)destroyWindow;
	R2_WorldShutdown();
	R2_SkinShutdown();
	R2_ModelShutdown();
	R2_GL_Shutdown();
}

static void R2_BeginRegistration( glconfig_t *config ) {
	if ( !R2_GL_Init( config ) ) {
		r2_ri.Error( ERR_FATAL, "Renderer2: could not initialize OpenGL 4 backend" );
	}
	R2_ClearFogState();
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | begin registration",
							  r2.contextMajor, r2.contextMinor ) );
	r2_ri.Printf( PRINT_ALL, "Renderer2: BeginRegistration completed\n" );
}

static qhandle_t R2_RegisterModel( const char *name ) {
	return R2_ModelRegister( name );
}

static qhandle_t R2_RegisterSkin( const char *name ) {
	return R2_SkinRegister( name );
}

static qhandle_t R2_RegisterShader( const char *name ) {
	return R2_GL_RegisterShader( name, qfalse );
}

static qhandle_t R2_RegisterShaderNoMip( const char *name ) {
	return R2_GL_RegisterShader( name, qtrue );
}

static void R2_LoadWorld( const char *name ) {
	R2_ClearFogState();
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | load world start %s",
							  r2.contextMajor, r2.contextMinor, name ? name : "<null>" ) );
	R2_WorldLoad( name );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | load world done %s b%d",
							  r2.contextMajor, r2.contextMinor,
							  R2_WorldIsLoaded() ? R2_WorldName() : "<not loaded>",
							  R2_WorldBatchCount() ) );
}

static qboolean R2_GetSkinModel( qhandle_t skinid, const char *type, char *name ) {
	return R2_SkinGetModel( skinid, type, name );
}

static qhandle_t R2_GetShaderFromModel( qhandle_t modelid, int surfnum, int withlightmap ) {
	(void)withlightmap;
	return R2_ModelGetShader( modelid, surfnum );
}

static void R2_SetWorldVisData( const byte *vis ) {
	(void)vis;
}

static void R2_EndRegistration( void ) {
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | end registration | world %s b%d",
							  r2.contextMajor, r2.contextMinor,
							  R2_WorldIsLoaded() ? R2_WorldName() : "<not loaded>",
							  R2_WorldBatchCount() ) );
}

static void R2_ClearScene( void ) {
	R2_ModelClearScene();
	r2_numScenePolys = 0;
	r2_numScenePolyVerts = 0;
}

static void R2_AddRefEntityToScene( const refEntity_t *refent ) {
	R2_ModelAddRefEntityToScene( refent );
}

static int R2_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir ) {
	(void)point;
	VectorClear( ambientLight );
	VectorClear( directedLight );
	VectorClear( lightDir );
	return qfalse;
}

static void R2_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts ) {
	poly_t *poly;

	if ( !verts || numVerts < 3 || r2_numScenePolys >= R2_MAX_SCENE_POLYS ||
		 r2_numScenePolyVerts + numVerts > R2_MAX_SCENE_POLY_VERTS ) {
		return;
	}

	poly = &r2_scenePolys[r2_numScenePolys++];
	poly->hShader = hShader;
	poly->numVerts = numVerts;
	poly->verts = &r2_scenePolyVerts[r2_numScenePolyVerts];
	memcpy( poly->verts, verts, sizeof( *verts ) * numVerts );
	r2_numScenePolyVerts += numVerts;
}

static void R2_AddPolysToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys ) {
	int i;

	if ( !verts || numVerts < 3 || numPolys <= 0 ) {
		return;
	}
	for ( i = 0; i < numPolys; ++i ) {
		R2_AddPolyToScene( hShader, numVerts, verts + i * numVerts );
	}
}

static void R2_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b, int overdraw ) {
	(void)org;
	(void)intensity;
	(void)r;
	(void)g;
	(void)b;
	(void)overdraw;
}

static void R2_AddCoronaToScene( const vec3_t org, float r, float g, float b, float scale, int id, int flags ) {
	(void)org;
	(void)r;
	(void)g;
	(void)b;
	(void)scale;
	(void)id;
	(void)flags;
}

static void R2_SetFog( int fogvar, int var1, int var2, float r, float g, float b, float density ) {
	(void)density;

	if ( fogvar == FOG_CMD_SWITCHFOG ) {
		if ( var1 > FOG_NONE && var1 < NUM_FOGS && r2.fogRegistered[var1] ) {
			r2.activeFog = var1;
		} else {
			r2.activeFog = FOG_NONE;
		}
		R2_UpdateFogClearColor();
		return;
	}

	if ( fogvar <= FOG_NONE || fogvar >= NUM_FOGS ) {
		return;
	}

	if ( var1 == 0 && var2 == 0 ) {
		r2.fogRegistered[fogvar] = qfalse;
		if ( r2.activeFog == fogvar ) {
			R2_UpdateFogClearColor();
		}
		return;
	}

	r2.fogRegistered[fogvar] = qtrue;
	r2.fogColor[fogvar][0] = R2_ClampUnit( r );
	r2.fogColor[fogvar][1] = R2_ClampUnit( g );
	r2.fogColor[fogvar][2] = R2_ClampUnit( b );
	r2.fogColor[fogvar][3] = 1.0f;
	if ( r2.activeFog == fogvar ) {
		R2_UpdateFogClearColor();
	}
}

static void R2_RenderScene( const refdef_t *fd ) {
	r2_debugStats.scenePolys = r2_numScenePolys;

	if ( fd && ( fd->rdflags & RDF_SKYBOXPORTAL ) ) {
		R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | frame %d | portal skipped rd %x | polys %d",
								  r2.contextMajor, r2.contextMinor, r2.frameCount,
								  fd->rdflags, r2_numScenePolys ) );
		return;
	}

	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | frame %d | scene start rd %x | %dx%d | world %s b%d",
							  r2.contextMajor, r2.contextMinor, r2.frameCount,
							  fd ? fd->rdflags : 0, fd ? fd->width : 0, fd ? fd->height : 0,
							  R2_WorldIsLoaded() ? R2_WorldName() : "<not loaded>", R2_WorldBatchCount() ) );
	R2_WorldRender( fd );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | frame %d | models start rd %x | world %s b%d",
							  r2.contextMajor, r2.contextMinor, r2.frameCount,
							  fd ? fd->rdflags : 0,
							  R2_WorldIsLoaded() ? R2_WorldName() : "<not loaded>", R2_WorldBatchCount() ) );
	R2_ModelRenderScene( fd );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | frame %d | polys start %d",
							  r2.contextMajor, r2.contextMinor, r2.frameCount, r2_numScenePolys ) );
	R2_GL_DrawScenePolys( fd, r2_scenePolys, r2_numScenePolys );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | frame %d | rd %x | polys %d | world %s b%d",
							  r2.contextMajor, r2.contextMinor, r2.frameCount,
							  fd ? fd->rdflags : 0, r2_numScenePolys,
							  R2_WorldIsLoaded() ? R2_WorldName() : "<not loaded>", R2_WorldBatchCount() ) );
}

static void R2_SetColor( const float *rgba ) {
	R2_GL_SetColor( rgba );
}

static void R2_DrawStretchPic( float x, float y, float w, float h,
							   float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	R2_GL_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
}

static void R2_DrawStretchPicGradient( float x, float y, float w, float h,
									   float s1, float t1, float s2, float t2, qhandle_t hShader,
									   const float *gradientColor, int gradientType ) {
	R2_GL_DrawStretchPicGradient( x, y, w, h, s1, t1, s2, t2, hShader, gradientColor, gradientType );
}

static void R2_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
							   const byte *data, int client, qboolean dirty ) {
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | draw raw c%d %dx%d dirty %d",
							  r2.contextMajor, r2.contextMinor, client, cols, rows, dirty ) );
	R2_GL_DrawStretchRaw( x, y, w, h, cols, rows, data, client, dirty );
}

static void R2_UploadCinematic( int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | upload cinematic c%d %dx%d dirty %d",
							  r2.contextMajor, r2.contextMinor, client, cols, rows, dirty ) );
	R2_GL_UploadCinematic( w, h, cols, rows, data, client, dirty );
}

static void R2_BeginFrame( stereoFrame_t stereoFrame ) {
	R2_GL_BeginFrame( stereoFrame );
}

static void R2_EndFrame( int *frontEndMsec, int *backEndMsec ) {
	R2_GL_EndFrame( frontEndMsec, backEndMsec );
}

static int R2_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
							 int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer ) {
	(void)numPoints;
	(void)points;
	(void)projection;
	(void)maxPoints;
	(void)pointBuffer;
	(void)maxFragments;
	(void)fragmentBuffer;
	return 0;
}

static int R2_LerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagName, int startIndex ) {
	return R2_ModelLerpTag( tag, refent, tagName, startIndex );
}

static void R2_ModelBoundsRef( qhandle_t model, vec3_t mins, vec3_t maxs ) {
	R2_ModelBounds( model, mins, maxs );
}

static void R2_RegisterFont( const char *fontName, int pointSize, fontInfo_t *font ) {
	R2_GL_RegisterFont( fontName, pointSize, font );
}

static void R2_RemapShader( const char *oldShader, const char *newShader, const char *offsetTime ) {
	(void)oldShader;
	(void)newShader;
	(void)offsetTime;
}

static void R2_ZombieFXAddNewHit( int entityNum, const vec3_t hitPos, const vec3_t hitDir ) {
	(void)entityNum;
	(void)hitPos;
	(void)hitDir;
}

static qboolean R2_GetEntityToken( char *buffer, int size ) {
	return R2_WorldGetEntityToken( buffer, size );
}

const char *R2_GetBackendName( void ) {
	return "renderer2-opengl4";
}

void R2_DebugSetStatusTitle( const char *title ) {
	if ( r2.initialized && title && title[0] ) {
		R2_GL_SetStatusTitle( title );
	}
}

refexport_t *R2_GetRefAPI( int apiVersion, refimport_t *rimp ) {
	static refexport_t re;

	if ( !rimp ) {
		return NULL;
	}

	r2_ri = *rimp;
	memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		r2_ri.Printf( PRINT_WARNING,
					  "Renderer2: mismatched REF_API_VERSION: expected %i, got %i\n",
					  REF_API_VERSION, apiVersion );
		return NULL;
	}

	r2_ri.Printf( PRINT_ALL,
				  "Renderer2: %s diagnostic backend selected.\n",
				  R2_GetBackendName() );

	re.Shutdown = R2_Shutdown;
	re.BeginRegistration = R2_BeginRegistration;
	re.RegisterModel = R2_RegisterModel;
	re.RegisterSkin = R2_RegisterSkin;
	re.RegisterShader = R2_RegisterShader;
	re.RegisterShaderNoMip = R2_RegisterShaderNoMip;
	re.LoadWorld = R2_LoadWorld;
	re.GetSkinModel = R2_GetSkinModel;
	re.GetShaderFromModel = R2_GetShaderFromModel;
	re.SetWorldVisData = R2_SetWorldVisData;
	re.EndRegistration = R2_EndRegistration;
	re.ClearScene = R2_ClearScene;
	re.AddRefEntityToScene = R2_AddRefEntityToScene;
	re.LightForPoint = R2_LightForPoint;
	re.AddPolyToScene = R2_AddPolyToScene;
	re.AddPolysToScene = R2_AddPolysToScene;
	re.AddLightToScene = R2_AddLightToScene;
	re.AddCoronaToScene = R2_AddCoronaToScene;
	re.SetFog = R2_SetFog;
	re.RenderScene = R2_RenderScene;
	re.SetColor = R2_SetColor;
	re.DrawStretchPic = R2_DrawStretchPic;
	re.DrawStretchPicGradient = R2_DrawStretchPicGradient;
	re.DrawStretchRaw = R2_DrawStretchRaw;
	re.UploadCinematic = R2_UploadCinematic;
	re.BeginFrame = R2_BeginFrame;
	re.EndFrame = R2_EndFrame;
	re.MarkFragments = R2_MarkFragments;
	re.LerpTag = R2_LerpTag;
	re.ModelBounds = R2_ModelBoundsRef;
	re.RegisterFont = R2_RegisterFont;
	re.RemapShader = R2_RemapShader;
	re.ZombieFXAddNewHit = R2_ZombieFXAddNewHit;
	re.GetEntityToken = R2_GetEntityToken;

	return &re;
}
