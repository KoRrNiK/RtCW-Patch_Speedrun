/*
===========================================================================

Renderer2 material declarations.

Keep this file backend-neutral so the parsed shader stages can later feed
OpenGL, Vulkan, or another renderer without leaking API constants.

===========================================================================
*/

#ifndef R2_MATERIAL_H
#define R2_MATERIAL_H

#include "../game/q_shared.h"

#define R2_MAX_SHADER_DEFS 16384
#define R2_MAX_SHADER_STAGES 8

typedef enum {
	R2_BLEND_ZERO = 0,
	R2_BLEND_ONE,
	R2_BLEND_SRC_ALPHA,
	R2_BLEND_ONE_MINUS_SRC_ALPHA,
	R2_BLEND_DST_COLOR,
	R2_BLEND_ONE_MINUS_DST_COLOR,
	R2_BLEND_SRC_COLOR,
	R2_BLEND_ONE_MINUS_SRC_COLOR,
	R2_BLEND_DST_ALPHA,
	R2_BLEND_ONE_MINUS_DST_ALPHA
} r2BlendFactor_t;

typedef enum {
	R2_ALPHA_NONE = 0,
	R2_ALPHA_GT0,
	R2_ALPHA_LT128,
	R2_ALPHA_GE128
} r2AlphaTestMode_t;

typedef enum {
	R2_ALPHA_GEN_IDENTITY = 0,
	R2_ALPHA_GEN_VERTEX,
	R2_ALPHA_GEN_ONE_MINUS_VERTEX
} r2AlphaGenMode_t;

typedef enum {
	R2_TCGEN_TEXTURE = 0,
	R2_TCGEN_LIGHTMAP
} r2TcGenMode_t;

typedef enum {
	R2_CULL_FRONT = 0,
	R2_CULL_BACK,
	R2_CULL_TWO_SIDED
} r2CullMode_t;

typedef struct {
	char image[MAX_QPATH];
	qhandle_t imageHandle;
	qboolean isLightmap;
	qboolean blend;
	r2BlendFactor_t blendSrc;
	r2BlendFactor_t blendDst;
	qboolean rgbVertex;
	r2AlphaGenMode_t alphaGen;
	r2TcGenMode_t tcGen;
	r2AlphaTestMode_t alphaTest;
	qboolean depthWrite;
	qboolean depthFuncEqual;
	float scaleS;
	float scaleT;
	qboolean hasScale;
	float scrollS;
	float scrollT;
	qboolean hasScroll;
} r2ShaderStage_t;

typedef struct {
	char name[MAX_QPATH];
	r2CullMode_t cullMode;
	qboolean isSky;
	qboolean noDraw;
	qboolean noLightmap;
	qboolean hasMapFog;
	float mapFogColor[3];
	float mapFogDensity;
	int mapFogFar;
	qboolean noMip;
	int stageCount;
	r2ShaderStage_t stages[R2_MAX_SHADER_STAGES];
} r2ShaderDef_t;

#endif /* R2_MATERIAL_H */
