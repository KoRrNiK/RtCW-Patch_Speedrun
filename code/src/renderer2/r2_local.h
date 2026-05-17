/*
===========================================================================

Renderer2 private declarations.

===========================================================================
*/

#ifndef R2_LOCAL_H
#define R2_LOCAL_H

#include "../game/q_shared.h"
#include "r2_public.h"

extern refimport_t r2_ri;

typedef enum {
	R2_INIT_NOT_READY = 0,
	R2_INIT_READY
} r2InitStatus_t;

typedef struct {
	glconfig_t glConfig;
	qboolean initialized;
	int requestedMajor;
	int requestedMinor;
	int contextMajor;
	int contextMinor;
	unsigned int colorProgram;
	unsigned int colorVao;
	unsigned int colorVbo;
	int colorSamplerLoc;
	unsigned int modelProgram;
	unsigned int modelVao;
	unsigned int modelVbo;
	int modelVboCapacityBytes;
	int modelSamplerLoc;
	int modelMvpLoc;
	int modelAlphaTestLoc;
	unsigned int worldProgram;
	int worldSamplerLoc;
	int worldLightmapSamplerLoc;
	int worldBlendSamplerLoc;
	int worldFilterSamplerLoc;
	int worldHasLightmapLoc;
	int worldHasBlendStageLoc;
	int worldHasFilterStageLoc;
	int worldIsFilterPassLoc;
	int worldTcSourceLoc;
	int worldTcScaleLoc;
	int worldTcOffsetLoc;
	int worldBlendTcScaleLoc;
	int worldBlendTcOffsetLoc;
	int worldFilterTcScaleLoc;
	int worldFilterTcOffsetLoc;
	int worldFilterStrengthLoc;
	int worldRgbVertexLoc;
	int worldAlphaGenLoc;
	int worldBlendRgbVertexLoc;
	int worldBlendAlphaGenLoc;
	int worldAlphaTestLoc;
	int worldTerrainDebugBlendLoc;
	int worldMvpLoc;
	unsigned int whiteTexture;
	float currentColor[4];
	qboolean fogRegistered[NUM_FOGS];
	float fogColor[NUM_FOGS][4];
	int activeFog;
	qboolean useFogClearColor;
	float fogClearColor[3];
	int frameCount;
	int lastTitleUpdateMs;
	qboolean pipelineReady;
} r2State_t;

extern r2State_t r2;

typedef struct {
	int worldVisibleSurfaces;
	int worldTotalSurfaces;
	int worldVisibleBatches;
	int worldDrawCalls;
	int worldMaxPasses;
	int modelSubmittedEntities;
	int modelRenderedEntities;
	int modelSurfaces;
	int modelTriangles;
	int modelDrawCalls;
	int modelDrawEnabled;
	int scenePolys;
} r2DebugStats_t;

extern r2DebugStats_t r2_debugStats;

typedef struct {
	vec3_t xyz;
	float st[2];
	float color[4];
	float lightmap[2];
} r2ModelVertex_t;

qboolean R2_GL_Init( glconfig_t *config );
void R2_GL_Shutdown( void );
void R2_GL_BeginFrame( stereoFrame_t stereoFrame );
void R2_GL_EndFrame( int *frontEndMsec, int *backEndMsec );
void R2_GL_PresentInitFrame( void );
void R2_GL_SetStatusTitle( const char *title );
void R2_GL_DrawDebugText( float x, float y, float scale, const char *text );
void R2_GL_RegisterFont( const char *fontName, int pointSize, fontInfo_t *font );
qhandle_t R2_GL_RegisterShader( const char *name, qboolean noMip );
qboolean R2_GL_ShaderIsSky( qhandle_t hShader );
qboolean R2_GL_ShaderIsNoDraw( qhandle_t hShader );
qboolean R2_GL_ShaderNoLightmap( qhandle_t hShader );
qboolean R2_GL_ShaderNameIsNoDraw( const char *name );
qboolean R2_GL_ShaderNameIsSky( const char *name );
void R2_GL_ApplyShaderNameSideEffects( const char *name );
void R2_GL_SetColor( const float *rgba );
void R2_GL_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader );
void R2_GL_DrawStretchPicGradient( float x, float y, float w, float h,
								   float s1, float t1, float s2, float t2,
								   qhandle_t hShader, const float *gradientColor, int gradientType );
void R2_GL_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty );
void R2_GL_UploadCinematic( int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty );
void R2_GL_DrawModelTriangles( const r2ModelVertex_t *vertices, int vertexCount, qhandle_t hShader, const float *mvp );
void R2_GL_DrawModelTrianglesVbo( unsigned int vbo, int vertexCount, qhandle_t hShader, const float *mvp );
int R2_GL_WorldShaderPassCount( qhandle_t hShader );
void R2_GL_DrawWorldTrianglesVboPassMulti( unsigned int vbo, const int *firstVertices,
										   const int *vertexCounts, int drawCount,
										   qhandle_t hShader, unsigned int lightmapTexnum,
										   const float *mvp, int pass );
void R2_GL_DrawWorldTrianglesVboPass( unsigned int vbo, int firstVertex, int vertexCount,
									  qhandle_t hShader, unsigned int lightmapTexnum,
									  const float *mvp, int pass );
void R2_GL_DrawWorldTrianglesVbo( unsigned int vbo, int firstVertex, int vertexCount,
								  qhandle_t hShader, unsigned int lightmapTexnum, const float *mvp );
void R2_GL_DrawScenePolys( const refdef_t *fd, const poly_t *polys, int numPolys );

void R2_ModelShutdown( void );
qhandle_t R2_ModelRegister( const char *name );
qhandle_t R2_ModelGetShader( qhandle_t modelid, int surfnum );
void R2_ModelClearScene( void );
void R2_ModelAddRefEntityToScene( const refEntity_t *refent );
void R2_ModelRenderScene( const refdef_t *fd );
int R2_ModelLerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagName, int startIndex );
void R2_ModelBounds( qhandle_t model, vec3_t mins, vec3_t maxs );

void R2_SkinShutdown( void );
qhandle_t R2_SkinRegister( const char *name );
qboolean R2_SkinGetModel( qhandle_t skinid, const char *type, char *name );
qhandle_t R2_SkinGetSurfaceShader( qhandle_t skinid, const char *surfaceName );

void R2_WorldShutdown( void );
void R2_WorldLoad( const char *name );
void R2_WorldRender( const refdef_t *fd );
qboolean R2_WorldIsLoaded( void );
int R2_WorldBatchCount( void );
const char *R2_WorldName( void );
qboolean R2_WorldGetEntityToken( char *buffer, int size );
qboolean R2_WorldInlineModelBounds( int index, vec3_t mins, vec3_t maxs );
void R2_WorldDrawInlineModel( int index, const refdef_t *fd, const refEntity_t *ent );

#endif /* R2_LOCAL_H */
