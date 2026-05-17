/*
===========================================================================

Renderer2 BSP world first pass.

This intentionally starts small: load BSP draw surfaces, batch them by shader,
and draw textured geometry in the active refdef.  The world path now uses the
BSP PVS/leaf surface lists for visibility; fog, sky, brush models, and fuller
shader semantics will keep being layered in as the map renderer matures.

===========================================================================
*/

#include "r2_gl.h"
#include "../qcommon/qfiles.h"
#include "../game/surfaceflags.h"

typedef struct {
	char name[MAX_QPATH];
	qhandle_t shader;
	int surfaceFlags;
} r2WorldShader_t;

typedef struct {
	qhandle_t shader;
	int lightmapIndex;
	GLuint lightmapTexnum;
	r2ModelVertex_t *vertices;
	int vertexCount;
	int vertexCapacity;
	GLuint vbo;
	int firstVisibleSurface;
	int lastVisibleSurface;
} r2WorldBatch_t;

typedef struct {
	int batchIndex;
	int firstVertex;
	int vertexCount;
	int visFrame;
	int nextVisibleInBatch;
} r2WorldSurfaceRef_t;

typedef struct {
	float normal[3];
	float dist;
} r2WorldPlane_t;

typedef struct {
	int planeNum;
	int children[2];
	int mins[3];
	int maxs[3];
} r2WorldNode_t;

typedef struct {
	int cluster;
	int area;
	int mins[3];
	int maxs[3];
	int firstLeafSurface;
	int numLeafSurfaces;
} r2WorldLeaf_t;

typedef struct {
	vec3_t mins;
	vec3_t maxs;
	int firstSurface;
	int numSurfaces;
} r2WorldInlineModel_t;

typedef struct {
	char name[MAX_QPATH];
	char *entityString;
	char *entityParsePoint;
	r2WorldShader_t *shaders;
	int numShaders;
	GLuint *lightmaps;
	int numLightmaps;
	r2WorldSurfaceRef_t *surfaceRefs;
	r2WorldPlane_t *planes;
	int numPlanes;
	r2WorldNode_t *nodes;
	int numNodes;
	r2WorldLeaf_t *leafs;
	int numLeafs;
	r2WorldInlineModel_t *inlineModels;
	int numInlineModels;
	int *leafSurfaces;
	int numLeafSurfaces;
	byte *vis;
	int numClusters;
	int clusterBytes;
	r2WorldBatch_t *batches;
	int numBatches;
	int batchCapacity;
	r2ModelVertex_t *scratch;
	int scratchCapacity;
	int *multiFirsts;
	int *multiCounts;
	int multiCapacity;
	int visFrame;
	int numSurfaces;
	int numDrawSurfaces;
	int numPatchSurfaces;
	int numSkippedSurfaces;
	int visibleSurfaceCount;
	qboolean loaded;
} r2World_t;

static r2World_t r2_world;
static cvar_t *r2_worldDrawVisibleBatches;
static cvar_t *r2_worldMultiDraw;

#define R2_PATCH_SUBDIVISIONS 8

static void R2_WorldColorShiftLightingBytes( const byte in[4], byte out[4] ) {
	int r;
	int g;
	int b;
	int max;

	r = in[0] << 2;
	g = in[1] << 2;
	b = in[2] << 2;

	if ( ( r | g | b ) > 255 ) {
		max = r > g ? r : g;
		max = max > b ? max : b;
		r = r * 255 / max;
		g = g * 255 / max;
		b = b * 255 / max;
	}

	out[0] = (byte)r;
	out[1] = (byte)g;
	out[2] = (byte)b;
	out[3] = in[3];
}

static void R2_WorldFreeBatches( void ) {
	int i;

	for ( i = 0; i < r2_world.numBatches; ++i ) {
		if ( r2_world.batches[i].vbo ) {
			r2gl.DeleteBuffers( 1, &r2_world.batches[i].vbo );
			r2_world.batches[i].vbo = 0;
		}
		if ( r2_world.batches[i].vertices ) {
			free( r2_world.batches[i].vertices );
		}
	}
	free( r2_world.batches );
	r2_world.batches = NULL;
	r2_world.numBatches = 0;
	r2_world.batchCapacity = 0;
}

void R2_WorldShutdown( void ) {
	int i;

	R2_WorldFreeBatches();
	for ( i = 0; i < r2_world.numLightmaps; ++i ) {
		if ( r2_world.lightmaps[i] ) {
			glDeleteTextures( 1, &r2_world.lightmaps[i] );
		}
	}
	free( r2_world.lightmaps );
	free( r2_world.shaders );
	free( r2_world.surfaceRefs );
	free( r2_world.planes );
	free( r2_world.nodes );
	free( r2_world.leafs );
	free( r2_world.inlineModels );
	free( r2_world.leafSurfaces );
	free( r2_world.vis );
	free( r2_world.entityString );
	free( r2_world.scratch );
	free( r2_world.multiFirsts );
	free( r2_world.multiCounts );
	memset( &r2_world, 0, sizeof( r2_world ) );
}

static qboolean R2_WorldLumpData( const dheader_t *header, int lumpNum, int elementSize, int fileLen,
								  const char *label, const void **data, int *count ) {
	const lump_t *lump;

	if ( !header || lumpNum < 0 || lumpNum >= HEADER_LUMPS || elementSize <= 0 || !data || !count ) {
		return qfalse;
	}

	lump = &header->lumps[lumpNum];
	if ( lump->fileofs < 0 || lump->filelen < 0 || lump->fileofs > fileLen ||
		 lump->filelen > fileLen - lump->fileofs ) {
		r2_ri.Error( ERR_DROP, "Renderer2: bad %s lump range in %s", label, r2_world.name );
		return qfalse;
	}
	if ( lump->filelen % elementSize ) {
		r2_ri.Error( ERR_DROP, "Renderer2: funny %s lump size in %s", label, r2_world.name );
		return qfalse;
	}

	*data = (const byte *)header + lump->fileofs;
	*count = lump->filelen / elementSize;
	return qtrue;
}

static void R2_WorldLoadEntities( const dheader_t *header, int fileLen ) {
	const lump_t *lump;

	lump = &header->lumps[LUMP_ENTITIES];
	if ( lump->fileofs < 0 || lump->filelen < 0 || lump->fileofs > fileLen ||
		 lump->filelen > fileLen - lump->fileofs ) {
		r2_ri.Error( ERR_DROP, "Renderer2: bad entities lump range in %s", r2_world.name );
		return;
	}

	r2_world.entityString = (char *)malloc( lump->filelen + 1 );
	if ( !r2_world.entityString ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading entities in %s", r2_world.name );
		return;
	}

	memcpy( r2_world.entityString, (const byte *)header + lump->fileofs, lump->filelen );
	r2_world.entityString[lump->filelen] = '\0';
	r2_world.entityParsePoint = r2_world.entityString;
}

static void R2_WorldLoadShaders( const dheader_t *header, int fileLen ) {
	const dshader_t *in;
	const void *data;
	int count;
	int i;

	if ( !R2_WorldLumpData( header, LUMP_SHADERS, sizeof( dshader_t ), fileLen, "shaders", &data, &count ) ) {
		return;
	}

	in = (const dshader_t *)data;
	r2_world.shaders = (r2WorldShader_t *)calloc( count, sizeof( *r2_world.shaders ) );
	if ( !r2_world.shaders && count > 0 ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading shaders in %s", r2_world.name );
		return;
	}

	r2_world.numShaders = count;
	for ( i = 0; i < count; ++i ) {
		Q_strncpyz( r2_world.shaders[i].name, in[i].shader, sizeof( r2_world.shaders[i].name ) );
		r2_world.shaders[i].surfaceFlags = LittleLong( in[i].surfaceFlags );
		R2_GL_ApplyShaderNameSideEffects( r2_world.shaders[i].name );
		if ( ( r2_world.shaders[i].surfaceFlags & ( SURF_NODRAW | SURF_SKIP | SURF_SKY ) ) ||
			 R2_GL_ShaderNameIsNoDraw( r2_world.shaders[i].name ) ||
			 R2_GL_ShaderNameIsSky( r2_world.shaders[i].name ) ) {
			r2_world.shaders[i].shader = 0;
		} else {
			r2_world.shaders[i].shader = R2_GL_RegisterShader( r2_world.shaders[i].name, qfalse );
		}
	}
}

static qhandle_t R2_WorldShaderForSurface( int shaderNum, int *surfaceFlags ) {
	if ( surfaceFlags ) {
		*surfaceFlags = 0;
	}
	if ( shaderNum < 0 || shaderNum >= r2_world.numShaders ) {
		return 0;
	}
	if ( surfaceFlags ) {
		*surfaceFlags = r2_world.shaders[shaderNum].surfaceFlags;
	}
	return r2_world.shaders[shaderNum].shader;
}

static void R2_WorldLoadLightmaps( const dheader_t *header, int fileLen ) {
	const byte *in;
	const void *data;
	int count;
	int i;

	if ( !R2_WorldLumpData( header, LUMP_LIGHTMAPS, LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 3,
							fileLen, "lightmaps", &data, &count ) ) {
		return;
	}

	r2_world.lightmaps = (GLuint *)calloc( count, sizeof( *r2_world.lightmaps ) );
	if ( !r2_world.lightmaps && count > 0 ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading lightmaps in %s", r2_world.name );
		return;
	}

	r2_world.numLightmaps = count;
	in = (const byte *)data;
	for ( i = 0; i < count; ++i ) {
		byte rgba[LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 4];
		int j;

		for ( j = 0; j < LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT; ++j ) {
			byte raw[4];

			raw[0] = in[i * LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 3 + j * 3 + 0];
			raw[1] = in[i * LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 3 + j * 3 + 1];
			raw[2] = in[i * LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 3 + j * 3 + 2];
			raw[3] = 255;
			R2_WorldColorShiftLightingBytes( raw, &rgba[j * 4] );
		}

		glGenTextures( 1, &r2_world.lightmaps[i] );
		if ( !r2_world.lightmaps[i] ) {
			continue;
		}
		glBindTexture( GL_TEXTURE_2D, r2_world.lightmaps[i] );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, LIGHTMAP_WIDTH, LIGHTMAP_HEIGHT,
					  0, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
}

static void R2_WorldLoadPlanes( const dheader_t *header, int fileLen ) {
	const dplane_t *in;
	const void *data;
	int count;
	int i;

	if ( !R2_WorldLumpData( header, LUMP_PLANES, sizeof( dplane_t ), fileLen, "planes", &data, &count ) ) {
		return;
	}

	r2_world.planes = (r2WorldPlane_t *)calloc( count, sizeof( *r2_world.planes ) );
	if ( !r2_world.planes && count > 0 ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading planes in %s", r2_world.name );
		return;
	}

	in = (const dplane_t *)data;
	r2_world.numPlanes = count;
	for ( i = 0; i < count; ++i ) {
		r2_world.planes[i].normal[0] = LittleFloat( in[i].normal[0] );
		r2_world.planes[i].normal[1] = LittleFloat( in[i].normal[1] );
		r2_world.planes[i].normal[2] = LittleFloat( in[i].normal[2] );
		r2_world.planes[i].dist = LittleFloat( in[i].dist );
	}
}

static void R2_WorldLoadNodesAndLeafs( const dheader_t *header, int fileLen ) {
	const dnode_t *inNodes;
	const dleaf_t *inLeafs;
	const void *data;
	int count;
	int i;
	int j;

	if ( R2_WorldLumpData( header, LUMP_NODES, sizeof( dnode_t ), fileLen, "nodes", &data, &count ) ) {
		r2_world.nodes = (r2WorldNode_t *)calloc( count, sizeof( *r2_world.nodes ) );
		if ( !r2_world.nodes && count > 0 ) {
			r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading nodes in %s", r2_world.name );
			return;
		}
		inNodes = (const dnode_t *)data;
		r2_world.numNodes = count;
		for ( i = 0; i < count; ++i ) {
			r2_world.nodes[i].planeNum = LittleLong( inNodes[i].planeNum );
			for ( j = 0; j < 2; ++j ) {
				r2_world.nodes[i].children[j] = LittleLong( inNodes[i].children[j] );
			}
			for ( j = 0; j < 3; ++j ) {
				r2_world.nodes[i].mins[j] = LittleLong( inNodes[i].mins[j] );
				r2_world.nodes[i].maxs[j] = LittleLong( inNodes[i].maxs[j] );
			}
		}
	}

	if ( R2_WorldLumpData( header, LUMP_LEAFS, sizeof( dleaf_t ), fileLen, "leafs", &data, &count ) ) {
		r2_world.leafs = (r2WorldLeaf_t *)calloc( count, sizeof( *r2_world.leafs ) );
		if ( !r2_world.leafs && count > 0 ) {
			r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading leafs in %s", r2_world.name );
			return;
		}
		inLeafs = (const dleaf_t *)data;
		r2_world.numLeafs = count;
		for ( i = 0; i < count; ++i ) {
			r2_world.leafs[i].cluster = LittleLong( inLeafs[i].cluster );
			r2_world.leafs[i].area = LittleLong( inLeafs[i].area );
			r2_world.leafs[i].firstLeafSurface = LittleLong( inLeafs[i].firstLeafSurface );
			r2_world.leafs[i].numLeafSurfaces = LittleLong( inLeafs[i].numLeafSurfaces );
			for ( j = 0; j < 3; ++j ) {
				r2_world.leafs[i].mins[j] = LittleLong( inLeafs[i].mins[j] );
				r2_world.leafs[i].maxs[j] = LittleLong( inLeafs[i].maxs[j] );
			}
		}
	}
}

static void R2_WorldLoadInlineModels( const dheader_t *header, int fileLen ) {
	const dmodel_t *in;
	const void *data;
	int count;
	int i;
	int j;

	if ( !R2_WorldLumpData( header, LUMP_MODELS, sizeof( dmodel_t ), fileLen, "models", &data, &count ) ) {
		return;
	}

	r2_world.inlineModels = (r2WorldInlineModel_t *)calloc( count, sizeof( *r2_world.inlineModels ) );
	if ( !r2_world.inlineModels && count > 0 ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading inline models in %s", r2_world.name );
		return;
	}

	in = (const dmodel_t *)data;
	r2_world.numInlineModels = count;
	for ( i = 0; i < count; ++i ) {
		r2WorldInlineModel_t *out = &r2_world.inlineModels[i];

		for ( j = 0; j < 3; ++j ) {
			out->mins[j] = LittleFloat( in[i].mins[j] );
			out->maxs[j] = LittleFloat( in[i].maxs[j] );
		}
		out->firstSurface = LittleLong( in[i].firstSurface );
		out->numSurfaces = LittleLong( in[i].numSurfaces );
	}
}

static void R2_WorldLoadLeafSurfaces( const dheader_t *header, int fileLen ) {
	const int *in;
	const void *data;
	int count;
	int i;

	if ( !R2_WorldLumpData( header, LUMP_LEAFSURFACES, sizeof( int ), fileLen, "leafsurfaces", &data, &count ) ) {
		return;
	}

	r2_world.leafSurfaces = (int *)calloc( count, sizeof( *r2_world.leafSurfaces ) );
	if ( !r2_world.leafSurfaces && count > 0 ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading leafsurfaces in %s", r2_world.name );
		return;
	}

	in = (const int *)data;
	r2_world.numLeafSurfaces = count;
	for ( i = 0; i < count; ++i ) {
		r2_world.leafSurfaces[i] = LittleLong( in[i] );
	}
}

static void R2_WorldLoadVisibility( const dheader_t *header, int fileLen ) {
	const lump_t *lump;
	const byte *in;
	int dataBytes;

	if ( !header ) {
		return;
	}

	lump = &header->lumps[LUMP_VISIBILITY];
	if ( lump->fileofs < 0 || lump->filelen < 8 || lump->fileofs > fileLen ||
		 lump->filelen > fileLen - lump->fileofs ) {
		return;
	}

	in = (const byte *)header + lump->fileofs;
	r2_world.numClusters = LittleLong( ( (const int *)in )[0] );
	r2_world.clusterBytes = LittleLong( ( (const int *)in )[1] );
	dataBytes = lump->filelen - 8;
	if ( r2_world.numClusters <= 0 || r2_world.clusterBytes <= 0 ||
		 dataBytes < r2_world.numClusters * r2_world.clusterBytes ) {
		r2_world.numClusters = 0;
		r2_world.clusterBytes = 0;
		return;
	}

	r2_world.vis = (byte *)malloc( dataBytes );
	if ( !r2_world.vis ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading visibility in %s", r2_world.name );
		return;
	}
	memcpy( r2_world.vis, in + 8, dataBytes );
}

static r2WorldBatch_t *R2_WorldFindBatch( qhandle_t shader, int lightmapIndex ) {
	r2WorldBatch_t *newBatches;
	int newCapacity;
	int i;
	GLuint lightmapTexnum = 0;

	if ( lightmapIndex >= 0 && lightmapIndex < r2_world.numLightmaps ) {
		lightmapTexnum = r2_world.lightmaps[lightmapIndex];
	} else {
		lightmapIndex = -1;
	}

	for ( i = 0; i < r2_world.numBatches; ++i ) {
		if ( r2_world.batches[i].shader == shader &&
			 r2_world.batches[i].lightmapIndex == lightmapIndex ) {
			return &r2_world.batches[i];
		}
	}

	if ( r2_world.numBatches >= r2_world.batchCapacity ) {
		newCapacity = r2_world.batchCapacity ? r2_world.batchCapacity * 2 : 64;
		newBatches = (r2WorldBatch_t *)realloc( r2_world.batches, sizeof( *newBatches ) * newCapacity );
		if ( !newBatches ) {
			return NULL;
		}
		memset( newBatches + r2_world.batchCapacity, 0,
				sizeof( *newBatches ) * ( newCapacity - r2_world.batchCapacity ) );
		r2_world.batches = newBatches;
		r2_world.batchCapacity = newCapacity;
	}

	r2_world.batches[r2_world.numBatches].shader = shader;
	r2_world.batches[r2_world.numBatches].lightmapIndex = lightmapIndex;
	r2_world.batches[r2_world.numBatches].lightmapTexnum = lightmapTexnum;
	r2_world.batches[r2_world.numBatches].firstVisibleSurface = -1;
	r2_world.batches[r2_world.numBatches].lastVisibleSurface = -1;
	return &r2_world.batches[r2_world.numBatches++];
}

static qboolean R2_WorldEnsureBatchCapacity( r2WorldBatch_t *batch, int addCount ) {
	r2ModelVertex_t *newVertices;
	int newCapacity;
	int needed;

	if ( !batch || addCount <= 0 ) {
		return qfalse;
	}

	needed = batch->vertexCount + addCount;
	if ( needed <= batch->vertexCapacity ) {
		return qtrue;
	}

	newCapacity = batch->vertexCapacity ? batch->vertexCapacity * 2 : 1024;
	while ( newCapacity < needed ) {
		newCapacity *= 2;
	}

	newVertices = (r2ModelVertex_t *)realloc( batch->vertices, sizeof( *newVertices ) * newCapacity );
	if ( !newVertices ) {
		return qfalse;
	}

	batch->vertices = newVertices;
	batch->vertexCapacity = newCapacity;
	return qtrue;
}

static void R2_WorldConvertDrawVert( const drawVert_t *in, r2ModelVertex_t *out ) {
	byte shiftedColor[4];

	out->xyz[0] = LittleFloat( in->xyz[0] );
	out->xyz[1] = LittleFloat( in->xyz[1] );
	out->xyz[2] = LittleFloat( in->xyz[2] );
	out->st[0] = LittleFloat( in->st[0] );
	out->st[1] = LittleFloat( in->st[1] );
	out->lightmap[0] = LittleFloat( in->lightmap[0] );
	out->lightmap[1] = LittleFloat( in->lightmap[1] );
	R2_WorldColorShiftLightingBytes( in->color, shiftedColor );
	if ( in->color[0] || in->color[1] || in->color[2] ) {
		out->color[0] = (float)shiftedColor[0] / 255.0f;
		out->color[1] = (float)shiftedColor[1] / 255.0f;
		out->color[2] = (float)shiftedColor[2] / 255.0f;
	} else {
		out->color[0] = 1.0f;
		out->color[1] = 1.0f;
		out->color[2] = 1.0f;
	}
	out->color[3] = (float)in->color[3] / 255.0f;
}

static void R2_WorldAppendModelVertex( r2WorldBatch_t *batch, const r2ModelVertex_t *in ) {
	batch->vertices[batch->vertexCount++] = *in;
}

static void R2_WorldAppendDrawVert( r2WorldBatch_t *batch, const drawVert_t *in ) {
	r2ModelVertex_t out;

	R2_WorldConvertDrawVert( in, &out );
	R2_WorldAppendModelVertex( batch, &out );
}

static void R2_WorldLerpVertex( const r2ModelVertex_t *a, const r2ModelVertex_t *b, float t, r2ModelVertex_t *out ) {
	int i;

	for ( i = 0; i < 3; ++i ) {
		out->xyz[i] = a->xyz[i] + ( b->xyz[i] - a->xyz[i] ) * t;
	}
	for ( i = 0; i < 2; ++i ) {
		out->st[i] = a->st[i] + ( b->st[i] - a->st[i] ) * t;
		out->lightmap[i] = a->lightmap[i] + ( b->lightmap[i] - a->lightmap[i] ) * t;
	}
	for ( i = 0; i < 4; ++i ) {
		out->color[i] = a->color[i] + ( b->color[i] - a->color[i] ) * t;
	}
}

static void R2_WorldBezier3( const r2ModelVertex_t *a, const r2ModelVertex_t *b,
							 const r2ModelVertex_t *c, float t, r2ModelVertex_t *out ) {
	r2ModelVertex_t ab;
	r2ModelVertex_t bc;

	R2_WorldLerpVertex( a, b, t, &ab );
	R2_WorldLerpVertex( b, c, t, &bc );
	R2_WorldLerpVertex( &ab, &bc, t, out );
}

static void R2_WorldEvalPatchVertex( const r2ModelVertex_t control[3][3], float u, float v,
									 r2ModelVertex_t *out ) {
	r2ModelVertex_t row[3];
	int y;

	for ( y = 0; y < 3; ++y ) {
		R2_WorldBezier3( &control[y][0], &control[y][1], &control[y][2], u, &row[y] );
	}
	R2_WorldBezier3( &row[0], &row[1], &row[2], v, out );
}

static void R2_WorldEmitPatchBlock( r2WorldBatch_t *batch, const r2ModelVertex_t control[3][3] ) {
	r2ModelVertex_t grid[R2_PATCH_SUBDIVISIONS + 1][R2_PATCH_SUBDIVISIONS + 1];
	int x;
	int y;

	for ( y = 0; y <= R2_PATCH_SUBDIVISIONS; ++y ) {
		float v = (float)y / (float)R2_PATCH_SUBDIVISIONS;
		for ( x = 0; x <= R2_PATCH_SUBDIVISIONS; ++x ) {
			float u = (float)x / (float)R2_PATCH_SUBDIVISIONS;
			R2_WorldEvalPatchVertex( control, u, v, &grid[y][x] );
		}
	}

	for ( y = 0; y < R2_PATCH_SUBDIVISIONS; ++y ) {
		for ( x = 0; x < R2_PATCH_SUBDIVISIONS; ++x ) {
			R2_WorldAppendModelVertex( batch, &grid[y][x] );
			R2_WorldAppendModelVertex( batch, &grid[y][x + 1] );
			R2_WorldAppendModelVertex( batch, &grid[y + 1][x + 1] );
			R2_WorldAppendModelVertex( batch, &grid[y][x] );
			R2_WorldAppendModelVertex( batch, &grid[y + 1][x + 1] );
			R2_WorldAppendModelVertex( batch, &grid[y + 1][x] );
		}
	}
}

static void R2_WorldSetSurfaceRef( int surfaceIndex, const r2WorldBatch_t *batch, int firstVertex, int vertexCount ) {
	r2WorldSurfaceRef_t *ref;

	if ( surfaceIndex < 0 || surfaceIndex >= r2_world.numSurfaces || !r2_world.surfaceRefs ||
		 !batch || vertexCount <= 0 ) {
		return;
	}

	ref = &r2_world.surfaceRefs[surfaceIndex];
	ref->batchIndex = (int)( batch - r2_world.batches );
	ref->firstVertex = firstVertex;
	ref->vertexCount = vertexCount;
}

static void R2_WorldAppendIndexedSurface( int surfaceIndex, const dsurface_t *surface, const drawVert_t *drawVerts, int drawVertCount,
										  const int *indexes, int indexCount, qhandle_t shader, int lightmapIndex ) {
	r2WorldBatch_t *batch;
	int firstVert;
	int numVerts;
	int firstIndex;
	int numIndexes;
	int startVertex;
	int i;

	firstVert = LittleLong( surface->firstVert );
	numVerts = LittleLong( surface->numVerts );
	firstIndex = LittleLong( surface->firstIndex );
	numIndexes = LittleLong( surface->numIndexes );

	if ( firstVert < 0 || numVerts <= 0 || firstVert > drawVertCount || numVerts > drawVertCount - firstVert ||
		 firstIndex < 0 || numIndexes <= 0 || firstIndex > indexCount || numIndexes > indexCount - firstIndex ) {
		++r2_world.numSkippedSurfaces;
		return;
	}

	batch = R2_WorldFindBatch( shader, lightmapIndex );
	if ( !batch || !R2_WorldEnsureBatchCapacity( batch, numIndexes ) ) {
		++r2_world.numSkippedSurfaces;
		return;
	}

	startVertex = batch->vertexCount;
	for ( i = 0; i + 2 < numIndexes; i += 3 ) {
		int tri[3];
		int j;
		qboolean valid = qtrue;

		for ( j = 0; j < 3; ++j ) {
			tri[j] = LittleLong( indexes[firstIndex + i + j] );
			if ( tri[j] < 0 || tri[j] >= numVerts ) {
				valid = qfalse;
			}
		}
		if ( !valid ) {
			continue;
		}
		for ( j = 0; j < 3; ++j ) {
			R2_WorldAppendDrawVert( batch, &drawVerts[firstVert + tri[j]] );
		}
	}

	R2_WorldSetSurfaceRef( surfaceIndex, batch, startVertex, batch->vertexCount - startVertex );
	++r2_world.numDrawSurfaces;
}

static void R2_WorldAppendPatchSurface( int surfaceIndex, const dsurface_t *surface, const drawVert_t *drawVerts, int drawVertCount,
										qhandle_t shader, int lightmapIndex ) {
	r2WorldBatch_t *batch;
	r2ModelVertex_t *controlVerts;
	int firstVert;
	int width;
	int height;
	int addCount;
	int x;
	int y;
	int patchBlocksX;
	int patchBlocksY;
	int startVertex;

	firstVert = LittleLong( surface->firstVert );
	width = LittleLong( surface->patchWidth );
	height = LittleLong( surface->patchHeight );

	if ( width < 2 || height < 2 || firstVert < 0 || firstVert > drawVertCount ||
		 width * height > drawVertCount - firstVert ) {
		++r2_world.numSkippedSurfaces;
		return;
	}

	if ( width >= 3 && height >= 3 && ( width & 1 ) && ( height & 1 ) ) {
		patchBlocksX = ( width - 1 ) / 2;
		patchBlocksY = ( height - 1 ) / 2;
		addCount = patchBlocksX * patchBlocksY * R2_PATCH_SUBDIVISIONS * R2_PATCH_SUBDIVISIONS * 6;
	} else {
		patchBlocksX = 0;
		patchBlocksY = 0;
		addCount = ( width - 1 ) * ( height - 1 ) * 6;
	}

	batch = R2_WorldFindBatch( shader, lightmapIndex );
	if ( !batch || !R2_WorldEnsureBatchCapacity( batch, addCount ) ) {
		++r2_world.numSkippedSurfaces;
		return;
	}

	startVertex = batch->vertexCount;
	if ( patchBlocksX > 0 && patchBlocksY > 0 ) {
		controlVerts = (r2ModelVertex_t *)malloc( sizeof( *controlVerts ) * width * height );
		if ( !controlVerts ) {
			++r2_world.numSkippedSurfaces;
			return;
		}

		for ( y = 0; y < height; ++y ) {
			for ( x = 0; x < width; ++x ) {
				R2_WorldConvertDrawVert( &drawVerts[firstVert + y * width + x], &controlVerts[y * width + x] );
			}
		}

		for ( y = 0; y < height - 2; y += 2 ) {
			for ( x = 0; x < width - 2; x += 2 ) {
				r2ModelVertex_t control[3][3];
				int cx;
				int cy;

				for ( cy = 0; cy < 3; ++cy ) {
					for ( cx = 0; cx < 3; ++cx ) {
						control[cy][cx] = controlVerts[( y + cy ) * width + x + cx];
					}
				}
				R2_WorldEmitPatchBlock( batch, control );
			}
		}

		free( controlVerts );
		R2_WorldSetSurfaceRef( surfaceIndex, batch, startVertex, batch->vertexCount - startVertex );
		++r2_world.numPatchSurfaces;
		++r2_world.numDrawSurfaces;
		return;
	}

	for ( y = 0; y < height - 1; ++y ) {
		for ( x = 0; x < width - 1; ++x ) {
			int v0 = firstVert + y * width + x;
			int v1 = firstVert + y * width + x + 1;
			int v2 = firstVert + ( y + 1 ) * width + x + 1;
			int v3 = firstVert + ( y + 1 ) * width + x;

			R2_WorldAppendDrawVert( batch, &drawVerts[v0] );
			R2_WorldAppendDrawVert( batch, &drawVerts[v1] );
			R2_WorldAppendDrawVert( batch, &drawVerts[v2] );
			R2_WorldAppendDrawVert( batch, &drawVerts[v0] );
			R2_WorldAppendDrawVert( batch, &drawVerts[v2] );
			R2_WorldAppendDrawVert( batch, &drawVerts[v3] );
		}
	}

	R2_WorldSetSurfaceRef( surfaceIndex, batch, startVertex, batch->vertexCount - startVertex );
	++r2_world.numPatchSurfaces;
	++r2_world.numDrawSurfaces;
}

static void R2_WorldLoadSurfaces( const dheader_t *header, int fileLen ) {
	const dsurface_t *surfaces;
	const drawVert_t *drawVerts;
	const int *indexes;
	const void *data;
	int surfaceCount;
	int drawVertCount;
	int indexCount;
	int i;

	if ( !R2_WorldLumpData( header, LUMP_SURFACES, sizeof( dsurface_t ), fileLen, "surfaces", &data, &surfaceCount ) ) {
		return;
	}
	surfaces = (const dsurface_t *)data;
	if ( !R2_WorldLumpData( header, LUMP_DRAWVERTS, sizeof( drawVert_t ), fileLen, "drawverts", &data, &drawVertCount ) ) {
		return;
	}
	drawVerts = (const drawVert_t *)data;
	if ( !R2_WorldLumpData( header, LUMP_DRAWINDEXES, sizeof( int ), fileLen, "drawindexes", &data, &indexCount ) ) {
		return;
	}
	indexes = (const int *)data;

	r2_world.numSurfaces = surfaceCount;
	r2_world.surfaceRefs = (r2WorldSurfaceRef_t *)calloc( surfaceCount, sizeof( *r2_world.surfaceRefs ) );
	if ( !r2_world.surfaceRefs && surfaceCount > 0 ) {
		r2_ri.Error( ERR_DROP, "Renderer2: out of memory loading surface refs in %s", r2_world.name );
		return;
	}
	for ( i = 0; i < surfaceCount; ++i ) {
		r2_world.surfaceRefs[i].batchIndex = -1;
		r2_world.surfaceRefs[i].nextVisibleInBatch = -1;
	}

	for ( i = 0; i < surfaceCount; ++i ) {
		int surfaceType;
		int shaderNum;
		int surfaceFlags;
		int lightmapIndex;
		qhandle_t shader;

		surfaceType = LittleLong( surfaces[i].surfaceType );
		shaderNum = LittleLong( surfaces[i].shaderNum );
		lightmapIndex = LittleLong( surfaces[i].lightmapNum );
		shader = R2_WorldShaderForSurface( shaderNum, &surfaceFlags );
		if ( !shader ||
			 ( surfaceFlags & ( SURF_NODRAW | SURF_SKIP | SURF_SKY ) ) ||
			 R2_GL_ShaderIsSky( shader ) ||
			 R2_GL_ShaderIsNoDraw( shader ) ) {
			++r2_world.numSkippedSurfaces;
			continue;
		}
		if ( ( surfaceFlags & SURF_NOLIGHTMAP ) ||
			 R2_GL_ShaderNoLightmap( shader ) ||
			 surfaceType == MST_TRIANGLE_SOUP ||
			 lightmapIndex < 0 || lightmapIndex >= r2_world.numLightmaps ) {
			lightmapIndex = -1;
		}

		if ( surfaceType == MST_PLANAR || surfaceType == MST_TRIANGLE_SOUP ) {
			R2_WorldAppendIndexedSurface( i, &surfaces[i], drawVerts, drawVertCount, indexes, indexCount, shader, lightmapIndex );
		} else if ( surfaceType == MST_PATCH ) {
			R2_WorldAppendPatchSurface( i, &surfaces[i], drawVerts, drawVertCount, shader, lightmapIndex );
		} else {
			++r2_world.numSkippedSurfaces;
		}
	}
}

static void R2_WorldUploadBatches( void ) {
	int i;

	for ( i = 0; i < r2_world.numBatches; ++i ) {
		r2WorldBatch_t *batch = &r2_world.batches[i];

		if ( batch->vbo || batch->vertexCount <= 0 || !batch->vertices ) {
			continue;
		}

		r2gl.GenBuffers( 1, &batch->vbo );
		if ( !batch->vbo ) {
			continue;
		}
		r2gl.BindBuffer( GL_ARRAY_BUFFER, batch->vbo );
		r2gl.BufferData( GL_ARRAY_BUFFER,
						  sizeof( r2ModelVertex_t ) * batch->vertexCount,
						  batch->vertices,
						  GL_STATIC_DRAW );
		r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	}
}

void R2_WorldLoad( const char *name ) {
	dheader_t *header;
	byte *buffer = NULL;
	int fileLen;
	int version;
	int ident;
	int i;

	R2_WorldShutdown();

	if ( !name || !name[0] ) {
		return;
	}

	Q_strncpyz( r2_world.name, name, sizeof( r2_world.name ) );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | world read %s",
							  r2.contextMajor, r2.contextMinor, r2_world.name ) );
	fileLen = r2_ri.FS_ReadFile( name, (void **)&buffer );
	if ( !buffer || fileLen < (int)sizeof( dheader_t ) ) {
		r2_ri.Error( ERR_DROP, "Renderer2: %s not found or too small", name );
		return;
	}

	header = (dheader_t *)buffer;
	ident = LittleLong( header->ident );
	version = LittleLong( header->version );
	if ( ident != BSP_IDENT || version != BSP_VERSION ) {
		r2_ri.FS_FreeFile( buffer );
		r2_ri.Error( ERR_DROP, "Renderer2: %s has wrong BSP header (%i/%i)", name, ident, version );
		return;
	}

	for ( i = 0; i < (int)( sizeof( dheader_t ) / sizeof( int ) ); ++i ) {
		( (int *)header )[i] = LittleLong( ( (int *)header )[i] );
	}

	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | world entities %s",
							  r2.contextMajor, r2.contextMinor, r2_world.name ) );
	R2_WorldLoadEntities( header, fileLen );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | world shaders %s",
							  r2.contextMajor, r2.contextMinor, r2_world.name ) );
	R2_WorldLoadShaders( header, fileLen );
	R2_WorldLoadLightmaps( header, fileLen );
	R2_WorldLoadPlanes( header, fileLen );
	R2_WorldLoadNodesAndLeafs( header, fileLen );
	R2_WorldLoadInlineModels( header, fileLen );
	R2_WorldLoadLeafSurfaces( header, fileLen );
	R2_WorldLoadVisibility( header, fileLen );
	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | world surfaces %s",
							  r2.contextMajor, r2.contextMinor, r2_world.name ) );
	R2_WorldLoadSurfaces( header, fileLen );
	R2_WorldUploadBatches();
	r2_ri.FS_FreeFile( buffer );

	r2_world.loaded = qtrue;
	r2_ri.Printf( PRINT_ALL,
				  "Renderer2: loaded world %s: %i/%i surfaces, %i patch grids, %i batches, %i skipped\n",
				  r2_world.name, r2_world.numDrawSurfaces, r2_world.numSurfaces,
				  r2_world.numPatchSurfaces, r2_world.numBatches, r2_world.numSkippedSurfaces );
}

qboolean R2_WorldIsLoaded( void ) {
	return r2_world.loaded;
}

int R2_WorldBatchCount( void ) {
	return r2_world.numBatches;
}

const char *R2_WorldName( void ) {
	return r2_world.name[0] ? r2_world.name : "<none>";
}

qboolean R2_WorldGetEntityToken( char *buffer, int size ) {
	char *token;

	if ( buffer && size > 0 ) {
		buffer[0] = '\0';
	}
	if ( !r2_world.entityString || !r2_world.entityParsePoint ) {
		return qfalse;
	}

	token = COM_Parse( &r2_world.entityParsePoint );
	if ( buffer && size > 0 ) {
		Q_strncpyz( buffer, token, size );
	}
	if ( !r2_world.entityParsePoint || !token[0] ) {
		r2_world.entityParsePoint = r2_world.entityString;
		return qfalse;
	}

	return qtrue;
}

qboolean R2_WorldInlineModelBounds( int index, vec3_t mins, vec3_t maxs ) {
	if ( mins ) {
		VectorClear( mins );
	}
	if ( maxs ) {
		VectorClear( maxs );
	}
	if ( index < 0 || index >= r2_world.numInlineModels || !r2_world.inlineModels ) {
		return qfalse;
	}
	if ( mins ) {
		VectorCopy( r2_world.inlineModels[index].mins, mins );
	}
	if ( maxs ) {
		VectorCopy( r2_world.inlineModels[index].maxs, maxs );
	}
	return qtrue;
}

static qboolean R2_WorldEnsureScratch( int vertexCount ) {
	r2ModelVertex_t *newScratch;

	if ( vertexCount <= r2_world.scratchCapacity ) {
		return qtrue;
	}

	newScratch = (r2ModelVertex_t *)realloc( r2_world.scratch, sizeof( *newScratch ) * vertexCount );
	if ( !newScratch ) {
		return qfalse;
	}
	r2_world.scratch = newScratch;
	r2_world.scratchCapacity = vertexCount;
	return qtrue;
}

static qboolean R2_WorldEnsureMultiDrawCapacity( int drawCount ) {
	int newCapacity;
	int *newFirsts;
	int *newCounts;

	if ( drawCount <= r2_world.multiCapacity ) {
		return qtrue;
	}

	newCapacity = r2_world.multiCapacity ? r2_world.multiCapacity * 2 : 256;
	while ( newCapacity < drawCount ) {
		newCapacity *= 2;
	}

	newFirsts = (int *)realloc( r2_world.multiFirsts, sizeof( *newFirsts ) * newCapacity );
	if ( !newFirsts ) {
		return qfalse;
	}
	r2_world.multiFirsts = newFirsts;

	newCounts = (int *)realloc( r2_world.multiCounts, sizeof( *newCounts ) * newCapacity );
	if ( !newCounts ) {
		return qfalse;
	}
	r2_world.multiCounts = newCounts;
	r2_world.multiCapacity = newCapacity;
	return qtrue;
}

static void R2_WorldProjectionMatrix( float fovX, float fovY, float *m ) {
	const float zNear = 1.0f;
	const float zFar = 131072.0f;
	float xScale;
	float yScale;

	if ( fovX < 1.0f || fovX > 170.0f ) {
		fovX = 90.0f;
	}
	if ( fovY < 1.0f || fovY > 170.0f ) {
		fovY = 90.0f;
	}

	xScale = 1.0f / tanf( DEG2RAD( fovX ) * 0.5f );
	yScale = 1.0f / tanf( DEG2RAD( fovY ) * 0.5f );

	memset( m, 0, sizeof( float ) * 16 );
	m[0] = xScale;
	m[5] = yScale;
	m[10] = ( zFar + zNear ) / ( zNear - zFar );
	m[11] = -1.0f;
	m[14] = ( 2.0f * zFar * zNear ) / ( zNear - zFar );
}

static void R2_WorldMatrixMultiply( const float *a, const float *b, float *out ) {
	float result[16];
	int row;
	int col;
	int k;

	for ( col = 0; col < 4; ++col ) {
		for ( row = 0; row < 4; ++row ) {
			float v = 0.0f;
			for ( k = 0; k < 4; ++k ) {
				v += a[k * 4 + row] * b[col * 4 + k];
			}
			result[col * 4 + row] = v;
		}
	}
	memcpy( out, result, sizeof( result ) );
}

static void R2_WorldViewProjectionMatrix( const refdef_t *fd, float *mvp ) {
	float projection[16];
	float view[16];

	R2_WorldProjectionMatrix( fd->fov_x, fd->fov_y, projection );
	memset( view, 0, sizeof( view ) );

	view[0] = -fd->viewaxis[1][0];
	view[4] = -fd->viewaxis[1][1];
	view[8] = -fd->viewaxis[1][2];
	view[12] = DotProduct( fd->vieworg, fd->viewaxis[1] );

	view[1] = fd->viewaxis[2][0];
	view[5] = fd->viewaxis[2][1];
	view[9] = fd->viewaxis[2][2];
	view[13] = -DotProduct( fd->vieworg, fd->viewaxis[2] );

	view[2] = -fd->viewaxis[0][0];
	view[6] = -fd->viewaxis[0][1];
	view[10] = -fd->viewaxis[0][2];
	view[14] = DotProduct( fd->vieworg, fd->viewaxis[0] );

	view[15] = 1.0f;
	R2_WorldMatrixMultiply( projection, view, mvp );
}

static void R2_WorldEntityModelMatrix( const refEntity_t *ent, float *model ) {
	memset( model, 0, sizeof( float ) * 16 );
	model[0] = ent->axis[0][0];
	model[1] = ent->axis[0][1];
	model[2] = ent->axis[0][2];
	model[4] = ent->axis[1][0];
	model[5] = ent->axis[1][1];
	model[6] = ent->axis[1][2];
	model[8] = ent->axis[2][0];
	model[9] = ent->axis[2][1];
	model[10] = ent->axis[2][2];
	model[12] = ent->origin[0];
	model[13] = ent->origin[1];
	model[14] = ent->origin[2];
	model[15] = 1.0f;
}

static void R2_WorldInlineModelMvp( const refdef_t *fd, const refEntity_t *ent, float *mvp ) {
	float viewProjection[16];
	float model[16];

	R2_WorldViewProjectionMatrix( fd, viewProjection );
	R2_WorldEntityModelMatrix( ent, model );
	R2_WorldMatrixMultiply( viewProjection, model, mvp );
}

static void R2_WorldTransformVertex( const refdef_t *fd, const r2ModelVertex_t *in, r2ModelVertex_t *out ) {
	vec3_t delta;

	VectorSubtract( in->xyz, fd->vieworg, delta );
	out->xyz[0] = -DotProduct( delta, fd->viewaxis[1] );
	out->xyz[1] = DotProduct( delta, fd->viewaxis[2] );
	out->xyz[2] = -DotProduct( delta, fd->viewaxis[0] );
	out->st[0] = in->st[0];
	out->st[1] = in->st[1];
	out->color[0] = in->color[0];
	out->color[1] = in->color[1];
	out->color[2] = in->color[2];
	out->color[3] = in->color[3];
	out->lightmap[0] = in->lightmap[0];
	out->lightmap[1] = in->lightmap[1];
}

static int R2_WorldPointInLeaf( const vec3_t point ) {
	int nodeIndex = 0;

	if ( !r2_world.nodes || !r2_world.planes || r2_world.numNodes <= 0 || r2_world.numLeafs <= 0 ) {
		return -1;
	}

	while ( nodeIndex >= 0 ) {
		const r2WorldNode_t *node;
		const r2WorldPlane_t *plane;
		float d;

		if ( nodeIndex >= r2_world.numNodes ) {
			return -1;
		}
		node = &r2_world.nodes[nodeIndex];
		if ( node->planeNum < 0 || node->planeNum >= r2_world.numPlanes ) {
			return -1;
		}
		plane = &r2_world.planes[node->planeNum];
		d = DotProduct( point, plane->normal ) - plane->dist;
		nodeIndex = node->children[d > 0.0f ? 0 : 1];
	}

	nodeIndex = -1 - nodeIndex;
	if ( nodeIndex < 0 || nodeIndex >= r2_world.numLeafs ) {
		return -1;
	}
	return nodeIndex;
}

static qboolean R2_WorldAreaVisible( const refdef_t *fd, int area ) {
	if ( !fd || area < 0 || area >= MAX_MAP_AREAS ) {
		return qtrue;
	}
	return ( fd->areamask[area >> 3] & ( 1 << ( area & 7 ) ) ) ? qfalse : qtrue;
}

static qboolean R2_WorldClusterVisible( const byte *pvs, int cluster ) {
	if ( !pvs ) {
		return qtrue;
	}
	if ( cluster < 0 || cluster >= r2_world.numClusters ) {
		return qfalse;
	}
	return ( pvs[cluster >> 3] & ( 1 << ( cluster & 7 ) ) ) ? qtrue : qfalse;
}

static void R2_WorldResetVisibleBatches( void ) {
	int i;

	for ( i = 0; i < r2_world.numBatches; ++i ) {
		r2_world.batches[i].firstVisibleSurface = -1;
		r2_world.batches[i].lastVisibleSurface = -1;
	}
	r2_world.visibleSurfaceCount = 0;
}

static void R2_WorldMarkSurfaceVisible( int surfaceIndex ) {
	r2WorldSurfaceRef_t *ref;
	r2WorldBatch_t *batch;

	if ( surfaceIndex < 0 || surfaceIndex >= r2_world.numSurfaces || !r2_world.surfaceRefs ) {
		return;
	}

	ref = &r2_world.surfaceRefs[surfaceIndex];
	if ( ref->batchIndex < 0 || ref->batchIndex >= r2_world.numBatches || ref->vertexCount <= 0 ) {
		return;
	}
	if ( ref->visFrame == r2_world.visFrame ) {
		return;
	}

	ref->visFrame = r2_world.visFrame;
	ref->nextVisibleInBatch = -1;

	batch = &r2_world.batches[ref->batchIndex];
	if ( batch->lastVisibleSurface >= 0 &&
		 batch->lastVisibleSurface < r2_world.numSurfaces ) {
		r2_world.surfaceRefs[batch->lastVisibleSurface].nextVisibleInBatch = surfaceIndex;
	} else {
		batch->firstVisibleSurface = surfaceIndex;
	}
	batch->lastVisibleSurface = surfaceIndex;
	++r2_world.visibleSurfaceCount;
}

static void R2_WorldMarkLeafSurfacesVisible( const r2WorldLeaf_t *leaf ) {
	int i;

	if ( !leaf || !r2_world.leafSurfaces || leaf->firstLeafSurface < 0 || leaf->numLeafSurfaces <= 0 ||
		 leaf->firstLeafSurface > r2_world.numLeafSurfaces ||
		 leaf->numLeafSurfaces > r2_world.numLeafSurfaces - leaf->firstLeafSurface ) {
		return;
	}

	for ( i = 0; i < leaf->numLeafSurfaces; ++i ) {
		R2_WorldMarkSurfaceVisible( r2_world.leafSurfaces[leaf->firstLeafSurface + i] );
	}
}

static void R2_WorldMarkAllSurfacesVisible( void ) {
	int i;

	for ( i = 0; i < r2_world.numSurfaces; ++i ) {
		R2_WorldMarkSurfaceVisible( i );
	}
}

static void R2_WorldMarkVisibleSurfaces( const refdef_t *fd ) {
	const byte *pvs = NULL;
	int leafIndex;
	int viewCluster = -1;
	int i;

	++r2_world.visFrame;
	if ( r2_world.visFrame <= 0 ) {
		if ( r2_world.surfaceRefs ) {
			for ( i = 0; i < r2_world.numSurfaces; ++i ) {
				r2_world.surfaceRefs[i].visFrame = 0;
				r2_world.surfaceRefs[i].nextVisibleInBatch = -1;
			}
		}
		r2_world.visFrame = 1;
	}
	R2_WorldResetVisibleBatches();

	if ( !r2_world.surfaceRefs || !r2_world.leafs || !r2_world.leafSurfaces ||
		 r2_world.numLeafs <= 0 || r2_world.numLeafSurfaces <= 0 ) {
		R2_WorldMarkAllSurfacesVisible();
		return;
	}

	leafIndex = R2_WorldPointInLeaf( fd->vieworg );
	if ( leafIndex >= 0 ) {
		viewCluster = r2_world.leafs[leafIndex].cluster;
	}
	if ( r2_world.vis && viewCluster >= 0 && viewCluster < r2_world.numClusters ) {
		pvs = r2_world.vis + viewCluster * r2_world.clusterBytes;
	}

	for ( i = 0; i < r2_world.numLeafs; ++i ) {
		const r2WorldLeaf_t *leaf = &r2_world.leafs[i];

		if ( !R2_WorldClusterVisible( pvs, leaf->cluster ) ) {
			continue;
		}
		if ( !R2_WorldAreaVisible( fd, leaf->area ) ) {
			continue;
		}
		R2_WorldMarkLeafSurfacesVisible( leaf );
	}
}

void R2_WorldDrawInlineModel( int index, const refdef_t *fd, const refEntity_t *ent ) {
	const r2WorldInlineModel_t *model;
	float mvp[16];
	int surfaceEnd;
	int pass;
	int maxPassCount = 1;
	int surfaceIndex;

	if ( !fd || !ent || !r2_world.loaded || index < 0 ||
		 index >= r2_world.numInlineModels || !r2_world.inlineModels ||
		 !r2_world.surfaceRefs ) {
		return;
	}

	model = &r2_world.inlineModels[index];
	if ( model->numSurfaces <= 0 || model->firstSurface < 0 ||
		 model->firstSurface >= r2_world.numSurfaces ) {
		return;
	}

	surfaceEnd = model->firstSurface + model->numSurfaces;
	if ( surfaceEnd > r2_world.numSurfaces ) {
		surfaceEnd = r2_world.numSurfaces;
	}

	for ( surfaceIndex = model->firstSurface; surfaceIndex < surfaceEnd; ++surfaceIndex ) {
		const r2WorldSurfaceRef_t *ref = &r2_world.surfaceRefs[surfaceIndex];
		int passCount;

		if ( ref->batchIndex < 0 || ref->batchIndex >= r2_world.numBatches || ref->vertexCount <= 0 ) {
			continue;
		}
		passCount = R2_GL_WorldShaderPassCount( r2_world.batches[ref->batchIndex].shader );
		if ( passCount > maxPassCount ) {
			maxPassCount = passCount;
		}
	}

	R2_WorldInlineModelMvp( fd, ent, mvp );
	for ( pass = 0; pass < maxPassCount; ++pass ) {
		for ( surfaceIndex = model->firstSurface; surfaceIndex < surfaceEnd; ++surfaceIndex ) {
			const r2WorldSurfaceRef_t *ref = &r2_world.surfaceRefs[surfaceIndex];
			r2WorldBatch_t *batch;

			if ( ref->batchIndex < 0 || ref->batchIndex >= r2_world.numBatches || ref->vertexCount <= 0 ) {
				continue;
			}

			batch = &r2_world.batches[ref->batchIndex];
			if ( pass >= R2_GL_WorldShaderPassCount( batch->shader ) || !batch->vbo ) {
				continue;
			}

			R2_GL_DrawWorldTrianglesVboPass( batch->vbo, ref->firstVertex, ref->vertexCount,
											 batch->shader, batch->lightmapTexnum, mvp, pass );
		}
	}
}

void R2_WorldRender( const refdef_t *fd ) {
	float mvp[16];
	int viewportX;
	int viewportY;
	int viewportW;
	int viewportH;
	int scissorX;
	int scissorY;
	int scissorW;
	int scissorH;
	int i;
	int pass;
	int maxPassCount = 1;
	int visibleSurfaces = 0;
	int visibleBatches = 0;
	int drawCalls = 0;
	qboolean drawVisibleBatches;
	qboolean useMultiDraw;

	if ( !fd || !r2_world.loaded || ( fd->rdflags & RDF_NOWORLDMODEL ) ||
		 r2.glConfig.vidWidth <= 0 || r2.glConfig.vidHeight <= 0 ) {
		return;
	}
	if ( !r2_worldDrawVisibleBatches ) {
		r2_worldDrawVisibleBatches = r2_ri.Cvar_Get( "r2_worldDrawVisibleBatches", "0", CVAR_ARCHIVE );
	}
	if ( !r2_worldMultiDraw ) {
		r2_worldMultiDraw = r2_ri.Cvar_Get( "r2_worldMultiDraw", "1", CVAR_ARCHIVE );
	}
	drawVisibleBatches = r2_worldDrawVisibleBatches && r2_worldDrawVisibleBatches->integer;
	useMultiDraw = r2_worldMultiDraw && r2_worldMultiDraw->integer;

	viewportX = fd->x;
	viewportY = r2.glConfig.vidHeight - ( fd->y + fd->height );
	viewportW = fd->width;
	viewportH = fd->height;
	if ( viewportW <= 0 || viewportH <= 0 ) {
		return;
	}

	scissorX = viewportX;
	scissorY = viewportY;
	scissorW = viewportW;
	scissorH = viewportH;
	if ( scissorX < 0 ) {
		scissorW += scissorX;
		scissorX = 0;
	}
	if ( scissorY < 0 ) {
		scissorH += scissorY;
		scissorY = 0;
	}
	if ( scissorX + scissorW > r2.glConfig.vidWidth ) {
		scissorW = r2.glConfig.vidWidth - scissorX;
	}
	if ( scissorY + scissorH > r2.glConfig.vidHeight ) {
		scissorH = r2.glConfig.vidHeight - scissorY;
	}
	if ( scissorW <= 0 || scissorH <= 0 ) {
		return;
	}

	R2_WorldViewProjectionMatrix( fd, mvp );

	glViewport( viewportX, viewportY, viewportW, viewportH );
	glEnable( GL_SCISSOR_TEST );
	glScissor( scissorX, scissorY, scissorW, scissorH );
	glClear( GL_DEPTH_BUFFER_BIT );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	glDisable( GL_CULL_FACE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	R2_WorldMarkVisibleSurfaces( fd );

	visibleSurfaces = r2_world.visibleSurfaceCount;
	if ( useMultiDraw && visibleSurfaces > 0 &&
		 !R2_WorldEnsureMultiDrawCapacity( visibleSurfaces ) ) {
		useMultiDraw = qfalse;
	}

	for ( i = 0; i < r2_world.numBatches; ++i ) {
		int passCount = R2_GL_WorldShaderPassCount( r2_world.batches[i].shader );
		if ( passCount > maxPassCount ) {
			maxPassCount = passCount;
		}
		if ( r2_world.batches[i].firstVisibleSurface >= 0 ) {
			++visibleBatches;
		}
	}

	for ( pass = 0; pass < maxPassCount; ++pass ) {
		for ( i = 0; i < r2_world.numBatches; ++i ) {
			r2WorldBatch_t *batch = &r2_world.batches[i];
			int passCount = R2_GL_WorldShaderPassCount( batch->shader );
			int surfaceIndex;

			if ( pass >= passCount || !batch->vbo || batch->firstVisibleSurface < 0 ) {
				continue;
			}

			if ( drawVisibleBatches ) {
				R2_GL_DrawWorldTrianglesVboPass( batch->vbo, 0, batch->vertexCount,
												 batch->shader, batch->lightmapTexnum, mvp, pass );
				++drawCalls;
				continue;
			}

			if ( useMultiDraw ) {
				int multiDrawCount = 0;

				for ( surfaceIndex = batch->firstVisibleSurface;
					  surfaceIndex >= 0 && surfaceIndex < r2_world.numSurfaces; ) {
					r2WorldSurfaceRef_t *ref;

					ref = &r2_world.surfaceRefs[surfaceIndex];
					surfaceIndex = ref->nextVisibleInBatch;
					if ( ref->visFrame != r2_world.visFrame || ref->batchIndex != i || ref->vertexCount <= 0 ) {
						continue;
					}
					r2_world.multiFirsts[multiDrawCount] = ref->firstVertex;
					r2_world.multiCounts[multiDrawCount] = ref->vertexCount;
					++multiDrawCount;
				}
				if ( multiDrawCount > 0 ) {
					R2_GL_DrawWorldTrianglesVboPassMulti( batch->vbo, r2_world.multiFirsts, r2_world.multiCounts,
														  multiDrawCount, batch->shader, batch->lightmapTexnum, mvp, pass );
					++drawCalls;
				}
				continue;
			}

			for ( surfaceIndex = batch->firstVisibleSurface;
				  surfaceIndex >= 0 && surfaceIndex < r2_world.numSurfaces; ) {
				r2WorldSurfaceRef_t *ref;

				ref = &r2_world.surfaceRefs[surfaceIndex];
				surfaceIndex = ref->nextVisibleInBatch;
				if ( ref->visFrame != r2_world.visFrame || ref->batchIndex != i || ref->vertexCount <= 0 ) {
					continue;
				}
				R2_GL_DrawWorldTrianglesVboPass( batch->vbo, ref->firstVertex, ref->vertexCount,
												 batch->shader, batch->lightmapTexnum, mvp, pass );
				++drawCalls;
			}
		}
	}

	R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | world visible %d/%d batches %d draws %d",
							  r2.contextMajor, r2.contextMinor,
							  visibleSurfaces, r2_world.numSurfaces, visibleBatches, drawCalls ) );

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glViewport( 0, 0, r2.glConfig.vidWidth, r2.glConfig.vidHeight );
}
