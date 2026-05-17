/*
===========================================================================

Renderer2 MD3 scene path.

This is intentionally small: it gives renderer2 enough model support for UI
scenes while the full world renderer is still being brought over.

===========================================================================
*/

#include "r2_gl.h"
#include "../qcommon/qfiles.h"

#define R2_MAX_MODELS 2048
#define R2_MAX_SCENE_ENTITIES 4096
#define R2_MDC_DIST_SCALE 0.05f
#define R2_MDC_MAX_OFS 127.0f

typedef enum {
	R2_MODEL_BAD = 0,
	R2_MODEL_EMPTY,
	R2_MODEL_INLINE,
	R2_MODEL_MD3,
	R2_MODEL_MDC,
	R2_MODEL_MDS
} r2ModelType_t;

typedef struct {
	char name[MAX_QPATH];
	byte *data;
	int length;
	r2ModelType_t type;
	md3Header_t *md3;
	mdcHeader_t *mdc;
	mdsHeader_t *mds;
	vec3_t mins;
	vec3_t maxs;
	int inlineModelIndex;
	qboolean skeletalPlaceholder;
	qboolean valid;
} r2Model_t;

static r2Model_t r2_models[R2_MAX_MODELS];
static int r2_numModels = 1;
static refEntity_t r2_sceneEntities[R2_MAX_SCENE_ENTITIES];
static int r2_numSceneEntities;
static qboolean r2_sceneEntityLimitWarned;
static cvar_t *r2_modelDraw;
static cvar_t *r2_modelDebugStatus;
static cvar_t *r2_modelLogMissing;
static r2ModelVertex_t *r2_modelScratch;
static int r2_modelScratchCapacity;
static int r2_modelFrameEntities;
static int r2_modelFrameSurfaces;
static int r2_modelFrameTriangles;
static int r2_modelFrameDraws;

static qboolean R2_ModelValidHandle( qhandle_t handle ) {
	return handle > 0 && handle < r2_numModels && r2_models[handle].valid;
}

static qboolean R2_ModelEnsureScratch( int vertexCount, r2ModelVertex_t **out ) {
	r2ModelVertex_t *newScratch;
	int newCapacity;

	if ( out ) {
		*out = NULL;
	}
	if ( vertexCount <= 0 || !out ) {
		return qfalse;
	}
	if ( vertexCount <= r2_modelScratchCapacity ) {
		*out = r2_modelScratch;
		return qtrue;
	}

	newCapacity = r2_modelScratchCapacity ? r2_modelScratchCapacity * 2 : 4096;
	while ( newCapacity < vertexCount ) {
		newCapacity *= 2;
	}

	newScratch = (r2ModelVertex_t *)realloc( r2_modelScratch, sizeof( *newScratch ) * newCapacity );
	if ( !newScratch ) {
		return qfalse;
	}

	r2_modelScratch = newScratch;
	r2_modelScratchCapacity = newCapacity;
	*out = r2_modelScratch;
	return qtrue;
}

static qboolean R2_ModelValidateMd3( const byte *data, int len ) {
	const md3Header_t *header;

	if ( !data || len < (int)sizeof( md3Header_t ) ) {
		return qfalse;
	}

	header = (const md3Header_t *)data;
	if ( header->ident != MD3_IDENT || header->version != MD3_VERSION ) {
		return qfalse;
	}
	if ( header->numFrames <= 0 || header->numFrames > MD3_MAX_FRAMES ||
		 header->numSurfaces < 0 || header->numSurfaces > MD3_MAX_SURFACES ||
		 header->numTags < 0 || header->numTags > MD3_MAX_TAGS ||
		 header->ofsFrames < (int)sizeof( md3Header_t ) ||
		 header->ofsSurfaces < header->ofsFrames ||
		 header->ofsEnd <= header->ofsSurfaces ||
		 header->ofsEnd > len ) {
		return qfalse;
	}

	return qtrue;
}

static qboolean R2_ModelValidateMdc( const byte *data, int len ) {
	const mdcHeader_t *header;

	if ( !data || len < (int)sizeof( mdcHeader_t ) ) {
		return qfalse;
	}

	header = (const mdcHeader_t *)data;
	if ( header->ident != MDC_IDENT || header->version != MDC_VERSION ) {
		return qfalse;
	}
	if ( header->numFrames <= 0 || header->numFrames > MD3_MAX_FRAMES ||
		 header->numSurfaces < 0 || header->numSurfaces > MD3_MAX_SURFACES ||
		 header->numTags < 0 || header->numTags > MD3_MAX_TAGS ||
		 header->ofsFrames < (int)sizeof( mdcHeader_t ) ||
		 header->ofsTagNames < header->ofsFrames ||
		 header->ofsTags < header->ofsTagNames ||
		 header->ofsSurfaces < header->ofsTags ||
		 header->ofsEnd <= header->ofsSurfaces ||
		 header->ofsEnd > len ) {
		return qfalse;
	}

	return qtrue;
}

static int R2_ModelMdsFrameSize( const mdsHeader_t *header ) {
	return (int)( sizeof( mdsFrame_t ) + ( header->numBones - 1 ) * sizeof( mdsBoneFrameCompressed_t ) );
}

static qboolean R2_ModelValidateMds( const byte *data, int len ) {
	const mdsHeader_t *header;
	const mdsSurface_t *surface;
	int frameSize;
	int i;

	if ( !data || len < (int)sizeof( mdsHeader_t ) ) {
		return qfalse;
	}

	header = (const mdsHeader_t *)data;
	if ( header->ident != MDS_IDENT || header->version != MDS_VERSION ) {
		return qfalse;
	}
	if ( header->numFrames <= 0 ||
		 header->numBones <= 0 || header->numBones > MDS_MAX_BONES ||
		 header->numSurfaces < 0 || header->numSurfaces > MDS_MAX_SURFACES ||
		 header->numTags < 0 || header->numTags > MDS_MAX_TAGS ||
		 header->ofsFrames < (int)sizeof( mdsHeader_t ) ||
		 header->ofsBones < header->ofsFrames ||
		 header->ofsSurfaces < header->ofsBones ||
		 header->ofsTags < header->ofsSurfaces ||
		 header->ofsEnd <= header->ofsTags ||
		 header->ofsEnd > len ) {
		return qfalse;
	}

	frameSize = R2_ModelMdsFrameSize( header );
	if ( frameSize <= 0 || header->ofsFrames + frameSize * header->numFrames > len ||
		 header->ofsBones + (int)sizeof( mdsBoneInfo_t ) * header->numBones > len ||
		 header->ofsTags + (int)sizeof( mdsTag_t ) * header->numTags > len ) {
		return qfalse;
	}

	surface = (const mdsSurface_t *)( data + header->ofsSurfaces );
	for ( i = 0; i < header->numSurfaces; ++i ) {
		if ( (const byte *)surface < data ||
			 (const byte *)surface + sizeof( *surface ) > data + len ||
			 surface->numVerts <= 0 || surface->numVerts > MDS_MAX_VERTS ||
			 surface->numTriangles < 0 || surface->numTriangles > MDS_MAX_TRIANGLES ||
			 surface->numBoneReferences <= 0 || surface->numBoneReferences > MDS_MAX_BONES ||
			 surface->ofsEnd <= 0 ||
			 (const byte *)surface + surface->ofsEnd > data + len ) {
			return qfalse;
		}
		surface = (const mdsSurface_t *)( (const byte *)surface + surface->ofsEnd );
	}

	return qtrue;
}

static qboolean R2_ModelTryReadFile( const char *candidate, byte **data, int *len,
									 char *resolvedName, int resolvedNameSize ) {
	if ( !candidate || !candidate[0] || !data || !len ) {
		return qfalse;
	}

	*len = r2_ri.FS_ReadFile( candidate, (void **)data );
	if ( *len > 0 && *data ) {
		if ( resolvedName ) {
			Q_strncpyz( resolvedName, candidate, resolvedNameSize );
		}
		return qtrue;
	}
	if ( *data ) {
		r2_ri.FS_FreeFile( *data );
		*data = NULL;
	}
	*len = 0;
	return qfalse;
}

static qboolean R2_ModelTryReadWithExtension( const char *baseName, const char *extension,
											 byte **data, int *len, char *resolvedName, int resolvedNameSize ) {
	char candidate[MAX_QPATH];

	if ( !baseName || !baseName[0] || !extension || !extension[0] ) {
		return qfalse;
	}

	Com_sprintf( candidate, sizeof( candidate ), "%s%s", baseName, extension );
	return R2_ModelTryReadFile( candidate, data, len, resolvedName, resolvedNameSize );
}

static qboolean R2_ModelReadFile( const char *name, byte **data, int *len, char *resolvedName, int resolvedNameSize ) {
	char baseName[MAX_QPATH];
	int nameLen;
	qboolean hasModelExtension;

	if ( data ) {
		*data = NULL;
	}
	if ( len ) {
		*len = 0;
	}
	if ( !name || !name[0] || !data || !len ) {
		return qfalse;
	}

	nameLen = (int)strlen( name );
	hasModelExtension = nameLen > 4 && name[nameLen - 4] == '.' &&
						( !Q_stricmp( name + nameLen - 4, ".md3" ) ||
						  !Q_stricmp( name + nameLen - 4, ".mdc" ) ||
						  !Q_stricmp( name + nameLen - 4, ".mds" ) );

	if ( hasModelExtension ) {
		COM_StripExtension( name, baseName );
	} else {
		Q_strncpyz( baseName, name, sizeof( baseName ) );
	}

	/* Match legacy registration for mesh fallbacks, but keep explicit skeletal
	   registrations on their MDS file now that renderer2 can draw them. */
	if ( hasModelExtension && !Q_stricmp( name + nameLen - 4, ".mds" ) &&
		 R2_ModelTryReadFile( name, data, len, resolvedName, resolvedNameSize ) ) {
		return qtrue;
	}
	if ( hasModelExtension && !Q_stricmp( name + nameLen - 4, ".mdc" ) &&
		 R2_ModelTryReadFile( name, data, len, resolvedName, resolvedNameSize ) ) {
		return qtrue;
	}
	if ( R2_ModelTryReadWithExtension( baseName, ".mdc", data, len, resolvedName, resolvedNameSize ) ) {
		return qtrue;
	}
	if ( hasModelExtension && !Q_stricmp( name + nameLen - 4, ".md3" ) &&
		 R2_ModelTryReadFile( name, data, len, resolvedName, resolvedNameSize ) ) {
		return qtrue;
	}
	if ( R2_ModelTryReadWithExtension( baseName, ".md3", data, len, resolvedName, resolvedNameSize ) ) {
		return qtrue;
	}
	if ( !hasModelExtension ) {
		return R2_ModelTryReadFile( name, data, len, resolvedName, resolvedNameSize );
	}
	return qfalse;
}

static void R2_ModelRegisterSurfaceShaders( md3Header_t *header, const byte *end ) {
	md3Surface_t *surface;
	int surfnum;

	surface = (md3Surface_t *)( (byte *)header + header->ofsSurfaces );
	for ( surfnum = 0; surfnum < header->numSurfaces; ++surfnum ) {
		md3Shader_t *shader;
		int i;

		if ( (byte *)surface < (byte *)header || (byte *)surface + sizeof( *surface ) > end ||
			 surface->ident != MD3_IDENT || surface->numVerts <= 0 ||
			 surface->numVerts > MD3_MAX_VERTS || surface->numTriangles < 0 ||
			 surface->numTriangles > MD3_MAX_TRIANGLES || surface->numShaders < 0 ||
			 surface->numShaders > MD3_MAX_SHADERS || surface->ofsEnd <= 0 ||
			 (byte *)surface + surface->ofsEnd > end ) {
			return;
		}

		shader = (md3Shader_t *)( (byte *)surface + surface->ofsShaders );
		for ( i = 0; i < surface->numShaders; ++i ) {
			shader[i].shaderIndex = R2_GL_RegisterShader( shader[i].name, qfalse );
		}

		surface = (md3Surface_t *)( (byte *)surface + surface->ofsEnd );
	}
}

static void R2_ModelRegisterMdcSurfaceShaders( mdcHeader_t *header, const byte *end ) {
	mdcSurface_t *surface;
	int surfnum;

	surface = (mdcSurface_t *)( (byte *)header + header->ofsSurfaces );
	for ( surfnum = 0; surfnum < header->numSurfaces; ++surfnum ) {
		md3Shader_t *shader;
		int i;

		if ( (byte *)surface < (byte *)header || (byte *)surface + sizeof( *surface ) > end ||
			 surface->numVerts <= 0 ||
			 surface->numVerts > MD3_MAX_VERTS || surface->numTriangles < 0 ||
			 surface->numTriangles > MD3_MAX_TRIANGLES || surface->numShaders < 0 ||
			 surface->numShaders > MD3_MAX_SHADERS || surface->ofsEnd <= 0 ||
			 (byte *)surface + surface->ofsEnd > end ) {
			return;
		}

		shader = (md3Shader_t *)( (byte *)surface + surface->ofsShaders );
		for ( i = 0; i < surface->numShaders; ++i ) {
			shader[i].shaderIndex = R2_GL_RegisterShader( shader[i].name, qfalse );
		}

		surface = (mdcSurface_t *)( (byte *)surface + surface->ofsEnd );
	}
}

static void R2_ModelRegisterMdsSurfaceShaders( mdsHeader_t *header, const byte *end ) {
	mdsSurface_t *surface;
	int surfnum;

	surface = (mdsSurface_t *)( (byte *)header + header->ofsSurfaces );
	for ( surfnum = 0; surfnum < header->numSurfaces; ++surfnum ) {
		if ( (byte *)surface < (byte *)header || (byte *)surface + sizeof( *surface ) > end ||
			 surface->numVerts <= 0 ||
			 surface->numVerts > MDS_MAX_VERTS || surface->numTriangles < 0 ||
			 surface->numTriangles > MDS_MAX_TRIANGLES || surface->ofsEnd <= 0 ||
			 (byte *)surface + surface->ofsEnd > end ) {
			return;
		}

		if ( surface->shader[0] ) {
			surface->shaderIndex = R2_GL_RegisterShader( surface->shader, qfalse );
		} else {
			surface->shaderIndex = 0;
		}

		surface = (mdsSurface_t *)( (byte *)surface + surface->ofsEnd );
	}
}

qhandle_t R2_ModelRegister( const char *name ) {
	byte *data = NULL;
	r2Model_t *model;
	md3Header_t *header;
	md3Frame_t *frame;
	int len;
	int nameLen;
	int i;
	qboolean skeletalPlaceholder = qfalse;

	if ( !name || !name[0] ) {
		return 0;
	}
	nameLen = (int)strlen( name );
	if ( !r2_modelLogMissing ) {
		r2_modelLogMissing = r2_ri.Cvar_Get( "r2_modelLogMissing", "0", CVAR_ARCHIVE );
	}

	for ( i = 1; i < r2_numModels; ++i ) {
		if ( !Q_stricmp( r2_models[i].name, name ) ) {
			return i;
		}
	}

	if ( r2_numModels >= R2_MAX_MODELS ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: model limit reached for '%s'\n", name );
		return 0;
	}

	if ( name[0] == '*' ) {
		int inlineIndex = atoi( name + 1 );

		model = &r2_models[r2_numModels];
		memset( model, 0, sizeof( *model ) );
		Q_strncpyz( model->name, name, sizeof( model->name ) );
		model->type = R2_MODEL_INLINE;
		model->inlineModelIndex = inlineIndex;
		model->valid = qtrue;
		if ( !R2_WorldInlineModelBounds( inlineIndex, model->mins, model->maxs ) ) {
			VectorClear( model->mins );
			VectorClear( model->maxs );
		}
		return r2_numModels++;
	}

	if ( !R2_ModelReadFile( name, &data, &len, NULL, 0 ) ) {
		if ( r2_modelLogMissing && r2_modelLogMissing->integer ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: missing model '%s'\n", name );
		}
		return 0;
	}

	if ( !R2_ModelValidateMd3( data, len ) && !R2_ModelValidateMdc( data, len ) &&
		 !R2_ModelValidateMds( data, len ) ) {
		if ( nameLen > 4 && !Q_stricmp( name + nameLen - 4, ".mds" ) ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: unsupported skeletal model '%s', using empty placeholder\n", name );
			skeletalPlaceholder = qtrue;
		} else {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: unsupported model '%s', using empty placeholder\n", name );
		}
		r2_ri.FS_FreeFile( data );
		data = NULL;
	}

	model = &r2_models[r2_numModels];
	memset( model, 0, sizeof( *model ) );
	Q_strncpyz( model->name, name, sizeof( model->name ) );
	model->data = data;
	model->length = len;
	model->skeletalPlaceholder = skeletalPlaceholder;
	model->valid = qtrue;

	if ( !data ) {
		model->type = R2_MODEL_EMPTY;
		VectorSet( model->mins, -16.0f, -16.0f, -16.0f );
		VectorSet( model->maxs, 16.0f, 16.0f, 16.0f );
	} else if ( R2_ModelValidateMd3( data, len ) ) {
		model->type = R2_MODEL_MD3;
		model->md3 = (md3Header_t *)data;
		header = model->md3;
		frame = (md3Frame_t *)( (byte *)header + header->ofsFrames );
		VectorCopy( frame->bounds[0], model->mins );
		VectorCopy( frame->bounds[1], model->maxs );
		R2_ModelRegisterSurfaceShaders( header, data + len );
	} else if ( R2_ModelValidateMdc( data, len ) ) {
		model->type = R2_MODEL_MDC;
		model->mdc = (mdcHeader_t *)data;
		frame = (md3Frame_t *)( (byte *)model->mdc + model->mdc->ofsFrames );
		VectorCopy( frame->bounds[0], model->mins );
		VectorCopy( frame->bounds[1], model->maxs );
		R2_ModelRegisterMdcSurfaceShaders( model->mdc, data + len );
	} else {
		mdsFrame_t *mdsFrame;

		model->type = R2_MODEL_MDS;
		model->mds = (mdsHeader_t *)data;
		mdsFrame = (mdsFrame_t *)( (byte *)model->mds + model->mds->ofsFrames );
		VectorCopy( mdsFrame->bounds[0], model->mins );
		VectorCopy( mdsFrame->bounds[1], model->maxs );
		R2_ModelRegisterMdsSurfaceShaders( model->mds, data + len );
	}

	return r2_numModels++;
}

void R2_ModelShutdown( void ) {
	int i;

	for ( i = 1; i < r2_numModels; ++i ) {
		if ( r2_models[i].data ) {
			r2_ri.FS_FreeFile( r2_models[i].data );
		}
	}
	memset( r2_models, 0, sizeof( r2_models ) );
	memset( r2_sceneEntities, 0, sizeof( r2_sceneEntities ) );
	free( r2_modelScratch );
	r2_modelScratch = NULL;
	r2_modelScratchCapacity = 0;
	r2_numModels = 1;
	r2_numSceneEntities = 0;
}

qhandle_t R2_ModelGetShader( qhandle_t modelid, int surfnum ) {
	r2Model_t *model;
	md3Surface_t *surface;
	mdcSurface_t *mdcSurface;
	mdsSurface_t *mdsSurface;
	int i;

	if ( !R2_ModelValidHandle( modelid ) ) {
		return 0;
	}

	model = &r2_models[modelid];
	if ( model->type == R2_MODEL_EMPTY || model->type == R2_MODEL_INLINE ) {
		return 0;
	}
	if ( model->type == R2_MODEL_MDC ) {
		mdcSurface = (mdcSurface_t *)( (byte *)model->mdc + model->mdc->ofsSurfaces );
		for ( i = 0; i < model->mdc->numSurfaces; ++i ) {
			if ( i == surfnum ) {
				if ( mdcSurface->numShaders > 0 ) {
					md3Shader_t *shader = (md3Shader_t *)( (byte *)mdcSurface + mdcSurface->ofsShaders );
					return shader->shaderIndex;
				}
				return 0;
			}
			mdcSurface = (mdcSurface_t *)( (byte *)mdcSurface + mdcSurface->ofsEnd );
		}
		return 0;
	}
	if ( model->type == R2_MODEL_MDS ) {
		mdsSurface = (mdsSurface_t *)( (byte *)model->mds + model->mds->ofsSurfaces );
		for ( i = 0; i < model->mds->numSurfaces; ++i ) {
			if ( i == surfnum ) {
				return mdsSurface->shaderIndex;
			}
			mdsSurface = (mdsSurface_t *)( (byte *)mdsSurface + mdsSurface->ofsEnd );
		}
		return 0;
	}

	surface = (md3Surface_t *)( (byte *)model->md3 + model->md3->ofsSurfaces );
	for ( i = 0; i < model->md3->numSurfaces; ++i ) {
		if ( i == surfnum ) {
			if ( surface->numShaders > 0 ) {
				md3Shader_t *shader = (md3Shader_t *)( (byte *)surface + surface->ofsShaders );
				return shader->shaderIndex;
			}
			return 0;
		}
		surface = (md3Surface_t *)( (byte *)surface + surface->ofsEnd );
	}
	return 0;
}

void R2_ModelClearScene( void ) {
	r2_numSceneEntities = 0;
}

void R2_ModelAddRefEntityToScene( const refEntity_t *refent ) {
	refEntity_t normalized;

	if ( !refent || refent->reType != RT_MODEL || !R2_ModelValidHandle( refent->hModel ) ) {
		return;
	}
	if ( r2_numSceneEntities >= R2_MAX_SCENE_ENTITIES ) {
		if ( !r2_sceneEntityLimitWarned ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: scene entity limit reached (%d), dropping additional model entities\n",
						  R2_MAX_SCENE_ENTITIES );
			r2_sceneEntityLimitWarned = qtrue;
		}
		return;
	}

	normalized = *refent;
	if ( !normalized.axis[0][0] && !normalized.axis[0][1] && !normalized.axis[0][2] &&
		 !normalized.axis[1][0] && !normalized.axis[1][1] && !normalized.axis[1][2] &&
		 !normalized.axis[2][0] && !normalized.axis[2][1] && !normalized.axis[2][2] ) {
		AxisClear( normalized.axis );
	}
	r2_sceneEntities[r2_numSceneEntities++] = normalized;
}

static void R2_ModelProjectionMatrix( float fovX, float fovY, float *m ) {
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

static void R2_ModelEntityColor( const refEntity_t *ent, float *color ) {
	if ( !ent->shaderRGBA[0] && !ent->shaderRGBA[1] && !ent->shaderRGBA[2] ) {
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
		color[3] = ent->shaderRGBA[3] ? ent->shaderRGBA[3] / 255.0f : 1.0f;
		return;
	}

	if ( ent->shaderRGBA[0] || ent->shaderRGBA[1] || ent->shaderRGBA[2] || ent->shaderRGBA[3] ) {
		color[0] = ent->shaderRGBA[0] / 255.0f;
		color[1] = ent->shaderRGBA[1] / 255.0f;
		color[2] = ent->shaderRGBA[2] / 255.0f;
		color[3] = ent->shaderRGBA[3] ? ent->shaderRGBA[3] / 255.0f : 1.0f;
	} else {
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
		color[3] = 1.0f;
	}
}

static qboolean R2_ModelShouldDrawEntity( const refdef_t *fd, const refEntity_t *ent ) {
	if ( !fd || !ent ) {
		return qfalse;
	}
	if ( ( ent->renderfx & RF_THIRD_PERSON ) && !( fd->rdflags & RDF_SKYBOXPORTAL ) ) {
		return qfalse;
	}
	if ( ( ent->renderfx & RF_FIRST_PERSON ) && ( fd->rdflags & RDF_SKYBOXPORTAL ) ) {
		return qfalse;
	}
	return qtrue;
}

static int R2_ModelFrameForEntity( const refEntity_t *ent, int frame, int numFrames, int fallbackFrame ) {
	if ( numFrames <= 0 ) {
		return 0;
	}
	if ( ent && ( ent->renderfx & RF_WRAP_FRAMES ) ) {
		frame %= numFrames;
		if ( frame < 0 ) {
			frame += numFrames;
		}
		return frame;
	}
	if ( frame < 0 || frame >= numFrames ) {
		if ( fallbackFrame >= 0 && fallbackFrame < numFrames ) {
			return fallbackFrame;
		}
		return 0;
	}
	return frame;
}

typedef struct {
	mdsHeader_t *header;
	const refEntity_t *ent;
	mdsBoneInfo_t *boneInfo;
	mdsFrame_t *frame;
	mdsFrame_t *oldFrame;
	mdsFrame_t *torsoFrame;
	mdsFrame_t *oldTorsoFrame;
	float frontlerp;
	float backlerp;
	float torsoFrontlerp;
	float torsoBacklerp;
	vec3_t torsoAxis[3];
	vec3_t torsoParentOffset;
	mdsBoneFrame_t bones[MDS_MAX_BONES];
	qboolean validBones[MDS_MAX_BONES];
	qboolean newBones[MDS_MAX_BONES];
} r2MdsBoneContext_t;

static mdsFrame_t *R2_MdsFrameAt( mdsHeader_t *header, int frameIndex ) {
	return (mdsFrame_t *)( (byte *)header + header->ofsFrames + frameIndex * R2_ModelMdsFrameSize( header ) );
}

static qboolean R2_ModelAxisIsZero( const vec3_t axis[3] ) {
	return !axis[0][0] && !axis[0][1] && !axis[0][2] &&
		   !axis[1][0] && !axis[1][1] && !axis[1][2] &&
		   !axis[2][0] && !axis[2][1] && !axis[2][2];
}

static void R2_MdsMatrix3Transpose( const vec3_t in[3], vec3_t out[3] ) {
	int i;
	int j;

	for ( i = 0; i < 3; ++i ) {
		for ( j = 0; j < 3; ++j ) {
			out[i][j] = in[j][i];
		}
	}
}

static void R2_MdsLocalAngleVector( const vec3_t angles, vec3_t forward ) {
	float sy;
	float cy;
	float sp;
	float cp;

	sy = sinf( DEG2RAD( angles[YAW] ) );
	cy = cosf( DEG2RAD( angles[YAW] ) );
	sp = sinf( DEG2RAD( angles[PITCH] ) );
	cp = cosf( DEG2RAD( angles[PITCH] ) );

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
}

static void R2_MdsSlerpNormal( const vec3_t from, const vec3_t to, float frac, vec3_t out ) {
	float invFrac = 1.0f - frac;

	out[0] = from[0] * invFrac + to[0] * frac;
	out[1] = from[1] * invFrac + to[1] * frac;
	out[2] = from[2] * invFrac + to[2] * frac;
	VectorNormalize( out );
}

static void R2_MdsScaledMatrixTransformVector( const vec3_t in, float scale,
											   const vec3_t matrix[3], vec3_t out ) {
	out[0] = ( 1.0f - scale ) * in[0] +
			 scale * ( in[0] * matrix[0][0] + in[1] * matrix[0][1] + in[2] * matrix[0][2] );
	out[1] = ( 1.0f - scale ) * in[1] +
			 scale * ( in[0] * matrix[1][0] + in[1] * matrix[1][1] + in[2] * matrix[1][2] );
	out[2] = ( 1.0f - scale ) * in[2] +
			 scale * ( in[0] * matrix[2][0] + in[1] * matrix[2][1] + in[2] * matrix[2][2] );
}

static void R2_MdsMatrix4FromAxisPlusTranslation( const vec3_t axis[3], const vec3_t t, vec4_t dst[4] ) {
	int i;
	int j;

	for ( i = 0; i < 3; ++i ) {
		for ( j = 0; j < 3; ++j ) {
			dst[i][j] = axis[i][j];
		}
		dst[3][i] = 0.0f;
		dst[i][3] = t[i];
	}
	dst[3][3] = 1.0f;
}

static void R2_MdsMatrix4FromScaledAxisPlusTranslation( const vec3_t axis[3], float scale,
														const vec3_t t, vec4_t dst[4] ) {
	int i;
	int j;

	for ( i = 0; i < 3; ++i ) {
		for ( j = 0; j < 3; ++j ) {
			dst[i][j] = scale * axis[i][j];
			if ( i == j ) {
				dst[i][j] += 1.0f - scale;
			}
		}
		dst[3][i] = 0.0f;
		dst[i][3] = t[i];
	}
	dst[3][3] = 1.0f;
}

static void R2_MdsMatrix4MultiplyInto3x3AndTranslation( const vec4_t a[4], const vec4_t b[4],
														vec3_t dst[3], vec3_t t ) {
	dst[0][0] = a[0][0] * b[0][0] + a[0][1] * b[1][0] + a[0][2] * b[2][0] + a[0][3] * b[3][0];
	dst[0][1] = a[0][0] * b[0][1] + a[0][1] * b[1][1] + a[0][2] * b[2][1] + a[0][3] * b[3][1];
	dst[0][2] = a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2] * b[2][2] + a[0][3] * b[3][2];
	t[0] = a[0][0] * b[0][3] + a[0][1] * b[1][3] + a[0][2] * b[2][3] + a[0][3] * b[3][3];

	dst[1][0] = a[1][0] * b[0][0] + a[1][1] * b[1][0] + a[1][2] * b[2][0] + a[1][3] * b[3][0];
	dst[1][1] = a[1][0] * b[0][1] + a[1][1] * b[1][1] + a[1][2] * b[2][1] + a[1][3] * b[3][1];
	dst[1][2] = a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2] * b[2][2] + a[1][3] * b[3][2];
	t[1] = a[1][0] * b[0][3] + a[1][1] * b[1][3] + a[1][2] * b[2][3] + a[1][3] * b[3][3];

	dst[2][0] = a[2][0] * b[0][0] + a[2][1] * b[1][0] + a[2][2] * b[2][0] + a[2][3] * b[3][0];
	dst[2][1] = a[2][0] * b[0][1] + a[2][1] * b[1][1] + a[2][2] * b[2][1] + a[2][3] * b[3][1];
	dst[2][2] = a[2][0] * b[0][2] + a[2][1] * b[1][2] + a[2][2] * b[2][2] + a[2][3] * b[3][2];
	t[2] = a[2][0] * b[0][3] + a[2][1] * b[1][3] + a[2][2] * b[2][3] + a[2][3] * b[3][3];
}

static void R2_MdsAddScaledMatrixTransformVectorTranslate( const vec3_t in, float scale,
														   const vec3_t matrix[3], const vec3_t translate,
														   vec3_t out ) {
	out[0] += scale * ( in[0] * matrix[0][0] + in[1] * matrix[0][1] + in[2] * matrix[0][2] + translate[0] );
	out[1] += scale * ( in[0] * matrix[1][0] + in[1] * matrix[1][1] + in[2] * matrix[1][2] + translate[1] );
	out[2] += scale * ( in[0] * matrix[2][0] + in[1] * matrix[2][1] + in[2] * matrix[2][2] + translate[2] );
}

static void R2_MdsLerpAngles( const short *newAngles, const short *oldAngles, float backlerp, vec3_t out ) {
	int i;

	for ( i = 0; i < 3; ++i ) {
		float a1 = SHORT2ANGLE( newAngles[i] );
		float a2 = SHORT2ANGLE( oldAngles[i] );
		float diff = AngleNormalize180( a1 - a2 );

		out[i] = a1 - backlerp * diff;
	}
}

static void R2_MdsLerpOffsetDirection( const short *newAngles, const short *oldAngles,
									   float frontlerp, vec3_t out ) {
	vec3_t angles;
	vec3_t newDir;
	vec3_t oldDir;

	angles[0] = SHORT2ANGLE( newAngles[0] );
	angles[1] = SHORT2ANGLE( newAngles[1] );
	angles[2] = 0.0f;
	R2_MdsLocalAngleVector( angles, newDir );

	angles[0] = SHORT2ANGLE( oldAngles[0] );
	angles[1] = SHORT2ANGLE( oldAngles[1] );
	angles[2] = 0.0f;
	R2_MdsLocalAngleVector( angles, oldDir );

	R2_MdsSlerpNormal( oldDir, newDir, frontlerp, out );
}

static void R2_MdsCalcBone( r2MdsBoneContext_t *ctx, int boneNum ) {
	mdsBoneInfo_t *info;
	mdsBoneFrame_t *bone;
	mdsBoneFrame_t *parentBone = NULL;
	mdsBoneFrameCompressed_t *newBone;
	mdsBoneFrameCompressed_t *oldBone;
	mdsBoneFrameCompressed_t *newTorsoBone;
	mdsBoneFrameCompressed_t *oldTorsoBone;
	qboolean isTorso;
	qboolean fullTorso;
	vec3_t angles;

	if ( !ctx || boneNum < 0 || boneNum >= ctx->header->numBones || ctx->validBones[boneNum] ) {
		return;
	}

	info = &ctx->boneInfo[boneNum];
	if ( info->parent >= 0 && info->parent < ctx->header->numBones ) {
		R2_MdsCalcBone( ctx, info->parent );
		parentBone = &ctx->bones[info->parent];
	}

	bone = &ctx->bones[boneNum];
	newBone = &ctx->frame->bones[boneNum];
	oldBone = &ctx->oldFrame->bones[boneNum];
	newTorsoBone = &ctx->torsoFrame->bones[boneNum];
	oldTorsoBone = &ctx->oldTorsoFrame->bones[boneNum];
	isTorso = info->torsoWeight != 0.0f;
	fullTorso = info->torsoWeight == 1.0f;

	if ( fullTorso ) {
		R2_MdsLerpAngles( newTorsoBone->angles, oldTorsoBone->angles, ctx->torsoBacklerp, angles );
	} else {
		R2_MdsLerpAngles( newBone->angles, oldBone->angles, ctx->backlerp, angles );
		if ( isTorso ) {
			vec3_t torsoAngles;
			int i;

			R2_MdsLerpAngles( newTorsoBone->angles, oldTorsoBone->angles, ctx->torsoBacklerp, torsoAngles );
			for ( i = 0; i < 3; ++i ) {
				float diff = torsoAngles[i] - angles[i];

				if ( fabs( diff ) > 180.0f ) {
					diff = AngleNormalize180( diff );
				}
				angles[i] += info->torsoWeight * diff;
			}
		}
	}
	AnglesToAxis( angles, bone->matrix );

	if ( parentBone ) {
		vec3_t dir;

		if ( fullTorso ) {
			R2_MdsLerpOffsetDirection( newTorsoBone->ofsAngles, oldTorsoBone->ofsAngles,
									   ctx->torsoFrontlerp, dir );
		} else {
			R2_MdsLerpOffsetDirection( newBone->ofsAngles, oldBone->ofsAngles, ctx->frontlerp, dir );
			if ( isTorso ) {
				vec3_t torsoDir;

				R2_MdsLerpOffsetDirection( newTorsoBone->ofsAngles, oldTorsoBone->ofsAngles,
										   ctx->torsoFrontlerp, torsoDir );
				R2_MdsSlerpNormal( dir, torsoDir, info->torsoWeight, dir );
			}
		}
		VectorMA( parentBone->translation, info->parentDist, dir, bone->translation );
	} else {
		bone->translation[0] = ctx->frontlerp * ctx->frame->parentOffset[0] +
							   ctx->backlerp * ctx->oldFrame->parentOffset[0];
		bone->translation[1] = ctx->frontlerp * ctx->frame->parentOffset[1] +
							   ctx->backlerp * ctx->oldFrame->parentOffset[1];
		bone->translation[2] = ctx->frontlerp * ctx->frame->parentOffset[2] +
							   ctx->backlerp * ctx->oldFrame->parentOffset[2];
	}

	if ( boneNum == ctx->header->torsoParent ) {
		VectorCopy( bone->translation, ctx->torsoParentOffset );
	}
	ctx->validBones[boneNum] = qtrue;
	ctx->newBones[boneNum] = qtrue;
}

static void R2_MdsApplyTorsoAxis( r2MdsBoneContext_t *ctx ) {
	int i;
	float torsoWeight = -1.0f;
	vec4_t torsoMatrix[4];

	if ( !ctx ) {
		return;
	}

	for ( i = 0; i < ctx->header->numBones; ++i ) {
		mdsBoneInfo_t *info;
		mdsBoneFrame_t *bone;
		vec3_t t;
		vec3_t tmpAxis[3];

		if ( !ctx->validBones[i] ) {
			continue;
		}

		info = &ctx->boneInfo[i];
		if ( info->torsoWeight <= 0.0f ) {
			continue;
		}

		bone = &ctx->bones[i];
		if ( !( info->flags & BONEFLAG_TAG ) ) {
			vec4_t boneMatrix[4];

			VectorSubtract( bone->translation, ctx->torsoParentOffset, t );
			R2_MdsMatrix4FromAxisPlusTranslation( bone->matrix, t, boneMatrix );
			if ( torsoWeight != info->torsoWeight ) {
				R2_MdsMatrix4FromScaledAxisPlusTranslation( ctx->torsoAxis, info->torsoWeight,
														   ctx->torsoParentOffset, torsoMatrix );
				torsoWeight = info->torsoWeight;
			}
			R2_MdsMatrix4MultiplyInto3x3AndTranslation( torsoMatrix, boneMatrix,
														bone->matrix, bone->translation );
		} else {
			R2_MdsScaledMatrixTransformVector( bone->matrix[0], info->torsoWeight, ctx->torsoAxis, tmpAxis[0] );
			R2_MdsScaledMatrixTransformVector( bone->matrix[1], info->torsoWeight, ctx->torsoAxis, tmpAxis[1] );
			R2_MdsScaledMatrixTransformVector( bone->matrix[2], info->torsoWeight, ctx->torsoAxis, tmpAxis[2] );
			VectorCopy( tmpAxis[0], bone->matrix[0] );
			VectorCopy( tmpAxis[1], bone->matrix[1] );
			VectorCopy( tmpAxis[2], bone->matrix[2] );

			VectorSubtract( bone->translation, ctx->torsoParentOffset, t );
			R2_MdsScaledMatrixTransformVector( t, info->torsoWeight, ctx->torsoAxis, bone->translation );
			VectorAdd( bone->translation, ctx->torsoParentOffset, bone->translation );
		}
	}
}

static void R2_MdsInitBoneContext( r2MdsBoneContext_t *ctx, mdsHeader_t *header, const refEntity_t *ent ) {
	int frame;
	int oldFrame;
	int torsoFrame;
	int oldTorsoFrame;

	memset( ctx, 0, sizeof( *ctx ) );
	ctx->header = header;
	ctx->ent = ent;
	ctx->boneInfo = (mdsBoneInfo_t *)( (byte *)header + header->ofsBones );

	frame = R2_ModelFrameForEntity( ent, ent->frame, header->numFrames, 0 );
	oldFrame = R2_ModelFrameForEntity( ent, ent->oldframe, header->numFrames, frame );
	torsoFrame = R2_ModelFrameForEntity( ent, ent->torsoFrame, header->numFrames, frame );
	oldTorsoFrame = R2_ModelFrameForEntity( ent, ent->oldTorsoFrame, header->numFrames, torsoFrame );

	ctx->frame = R2_MdsFrameAt( header, frame );
	ctx->oldFrame = R2_MdsFrameAt( header, oldFrame );
	ctx->torsoFrame = R2_MdsFrameAt( header, torsoFrame );
	ctx->oldTorsoFrame = R2_MdsFrameAt( header, oldTorsoFrame );
	ctx->backlerp = oldFrame == frame ? 0.0f : ent->backlerp;
	ctx->frontlerp = 1.0f - ctx->backlerp;
	ctx->torsoBacklerp = oldTorsoFrame == torsoFrame ? 0.0f : ent->torsoBacklerp;
	ctx->torsoFrontlerp = 1.0f - ctx->torsoBacklerp;

	if ( R2_ModelAxisIsZero( ent->torsoAxis ) ) {
		AxisClear( ctx->torsoAxis );
	} else {
		R2_MdsMatrix3Transpose( ent->torsoAxis, ctx->torsoAxis );
	}
}

static void R2_MdsCalcBones( r2MdsBoneContext_t *ctx, const int *boneRefs, int numBoneRefs ) {
	int i;

	if ( !ctx || !boneRefs ) {
		return;
	}

	for ( i = 0; i < numBoneRefs; ++i ) {
		R2_MdsCalcBone( ctx, boneRefs[i] );
	}
	R2_MdsApplyTorsoAxis( ctx );
}

static void R2_MdsTransformVertex( const r2MdsBoneContext_t *ctx, const mdsVertex_t *vertex, vec3_t out ) {
	int i;

	VectorClear( out );
	if ( !ctx || !vertex ) {
		return;
	}

	for ( i = 0; i < vertex->numWeights; ++i ) {
		const mdsWeight_t *weight = &vertex->weights[i];

		if ( weight->boneIndex < 0 || weight->boneIndex >= ctx->header->numBones ||
			 !ctx->validBones[weight->boneIndex] ) {
			continue;
		}
		R2_MdsAddScaledMatrixTransformVectorTranslate( weight->offset, weight->boneWeight,
													   ctx->bones[weight->boneIndex].matrix,
													   ctx->bones[weight->boneIndex].translation, out );
	}
}

static void R2_MdsRecursiveBoneListAdd( int boneIndex, int *boneList, int *numBones,
										const mdsBoneInfo_t *boneInfo, int maxBones ) {
	int i;

	if ( boneIndex < 0 || !boneList || !numBones || !boneInfo || *numBones >= maxBones ) {
		return;
	}
	for ( i = 0; i < *numBones; ++i ) {
		if ( boneList[i] == boneIndex ) {
			return;
		}
	}
	if ( boneInfo[boneIndex].parent >= 0 ) {
		R2_MdsRecursiveBoneListAdd( boneInfo[boneIndex].parent, boneList, numBones, boneInfo, maxBones );
	}
	if ( *numBones < maxBones ) {
		boneList[( *numBones )++] = boneIndex;
	}
}

static qhandle_t R2_ModelSurfaceShader( const refEntity_t *ent, const char *surfaceName,
										md3Shader_t *shaders, int numShaders ) {
	if ( ent && ent->customShader ) {
		return ent->customShader;
	}
	if ( ent && ent->customSkin > 0 ) {
		return R2_SkinGetSurfaceShader( ent->customSkin, surfaceName );
	}
	if ( shaders && numShaders > 0 ) {
		return shaders[abs( ent ? ent->skinNum : 0 ) % numShaders].shaderIndex;
	}
	return 0;
}

static qhandle_t R2_ModelMdsSurfaceShader( const refEntity_t *ent, const mdsSurface_t *surface ) {
	qhandle_t skinShader;

	if ( ent && ent->customShader ) {
		return ent->customShader;
	}
	if ( ent && ent->customSkin > 0 && surface ) {
		skinShader = R2_SkinGetSurfaceShader( ent->customSkin, surface->name );
		if ( skinShader ) {
			return skinShader;
		}
	}
	return surface ? surface->shaderIndex : 0;
}

static void R2_ModelTransformVertex( const refdef_t *fd, const refEntity_t *ent, const vec3_t in, vec3_t out ) {
	vec3_t world;
	vec3_t delta;
	int i;

	VectorCopy( ent->origin, world );
	for ( i = 0; i < 3; ++i ) {
		VectorMA( world, in[i], ent->axis[i], world );
	}

	VectorSubtract( world, fd->vieworg, delta );
	out[0] = -DotProduct( delta, fd->viewaxis[1] );
	out[1] = DotProduct( delta, fd->viewaxis[2] );
	out[2] = -DotProduct( delta, fd->viewaxis[0] );
}

static void R2_ModelClearTag( orientation_t *tag ) {
	if ( !tag ) {
		return;
	}
	VectorClear( tag->origin );
	AxisClear( tag->axis );
}

static void R2_ModelLerpMd3Tags( orientation_t *tag, const md3Tag_t *oldTag, const md3Tag_t *newTag,
								 float backlerp ) {
	float frontlerp;
	int i;

	if ( !tag || !oldTag || !newTag ) {
		return;
	}

	frontlerp = 1.0f - backlerp;
	for ( i = 0; i < 3; ++i ) {
		tag->origin[i] = oldTag->origin[i] * backlerp + newTag->origin[i] * frontlerp;
		tag->axis[0][i] = oldTag->axis[0][i] * backlerp + newTag->axis[0][i] * frontlerp;
		tag->axis[1][i] = oldTag->axis[1][i] * backlerp + newTag->axis[1][i] * frontlerp;
		tag->axis[2][i] = oldTag->axis[2][i] * backlerp + newTag->axis[2][i] * frontlerp;
	}
	VectorNormalize( tag->axis[0] );
	VectorNormalize( tag->axis[1] );
	VectorNormalize( tag->axis[2] );
}

static void R2_ModelMdcTagToMd3Tag( const mdcTag_t *in, md3Tag_t *out ) {
	vec3_t angles;

	if ( !in || !out ) {
		return;
	}

	out->origin[0] = in->xyz[0] * MD3_XYZ_SCALE;
	out->origin[1] = in->xyz[1] * MD3_XYZ_SCALE;
	out->origin[2] = in->xyz[2] * MD3_XYZ_SCALE;
	angles[0] = in->angles[0] * MDC_TAG_ANGLE_SCALE;
	angles[1] = in->angles[1] * MDC_TAG_ANGLE_SCALE;
	angles[2] = in->angles[2] * MDC_TAG_ANGLE_SCALE;
	AnglesToAxis( angles, out->axis );
}

static void R2_ModelRenderSurface( const refdef_t *fd, const refEntity_t *ent, md3Surface_t *surface, const float *mvp ) {
	md3Triangle_t *triangles;
	md3St_t *st;
	md3XyzNormal_t *newXyz;
	md3XyzNormal_t *oldXyz;
	md3Shader_t *shader = NULL;
	r2ModelVertex_t *vertices;
	float color[4];
	qhandle_t hShader = 0;
	int frame;
	int oldframe;
	int vertexCount;
	int i;
	int outIndex = 0;

	if ( surface->numTriangles <= 0 || surface->numVerts <= 0 ) {
		return;
	}

	vertexCount = surface->numTriangles * 3;
	if ( !R2_ModelEnsureScratch( vertexCount, &vertices ) ) {
		return;
	}

	frame = R2_ModelFrameForEntity( ent, ent->frame, surface->numFrames, 0 );
	oldframe = R2_ModelFrameForEntity( ent, ent->oldframe, surface->numFrames, frame );

	triangles = (md3Triangle_t *)( (byte *)surface + surface->ofsTriangles );
	st = (md3St_t *)( (byte *)surface + surface->ofsSt );
	newXyz = (md3XyzNormal_t *)( (byte *)surface + surface->ofsXyzNormals ) + frame * surface->numVerts;
	oldXyz = (md3XyzNormal_t *)( (byte *)surface + surface->ofsXyzNormals ) + oldframe * surface->numVerts;

	if ( surface->numShaders > 0 ) {
		shader = (md3Shader_t *)( (byte *)surface + surface->ofsShaders );
	}
	hShader = R2_ModelSurfaceShader( ent, surface->name, shader, surface->numShaders );

	R2_ModelEntityColor( ent, color );
	for ( i = 0; i < surface->numTriangles; ++i ) {
		int j;
		for ( j = 0; j < 3; ++j ) {
			int index = triangles[i].indexes[j];
			vec3_t pos;
			vec3_t lerped;
			float backlerp = ent->backlerp;
			float frontlerp = 1.0f - backlerp;

			if ( index < 0 || index >= surface->numVerts ) {
				continue;
			}

			lerped[0] = ( oldXyz[index].xyz[0] * backlerp + newXyz[index].xyz[0] * frontlerp ) * MD3_XYZ_SCALE;
			lerped[1] = ( oldXyz[index].xyz[1] * backlerp + newXyz[index].xyz[1] * frontlerp ) * MD3_XYZ_SCALE;
			lerped[2] = ( oldXyz[index].xyz[2] * backlerp + newXyz[index].xyz[2] * frontlerp ) * MD3_XYZ_SCALE;

			R2_ModelTransformVertex( fd, ent, lerped, pos );
			VectorCopy( pos, vertices[outIndex].xyz );
			vertices[outIndex].st[0] = st[index].st[0];
			vertices[outIndex].st[1] = st[index].st[1];
			vertices[outIndex].color[0] = color[0];
			vertices[outIndex].color[1] = color[1];
			vertices[outIndex].color[2] = color[2];
			vertices[outIndex].color[3] = color[3];
			++outIndex;
		}
	}

	if ( outIndex > 0 ) {
		++r2_modelFrameSurfaces;
		++r2_modelFrameDraws;
		r2_modelFrameTriangles += outIndex / 3;
		R2_GL_DrawModelTriangles( vertices, outIndex, hShader, mvp );
	}
}

static void R2_ModelDecodeMdcOffset( unsigned int ofsVec, vec3_t out ) {
	out[0] = ( (float)( ofsVec & 255 ) - R2_MDC_MAX_OFS ) * R2_MDC_DIST_SCALE;
	out[1] = ( (float)( ( ofsVec >> 8 ) & 255 ) - R2_MDC_MAX_OFS ) * R2_MDC_DIST_SCALE;
	out[2] = ( (float)( ( ofsVec >> 16 ) & 255 ) - R2_MDC_MAX_OFS ) * R2_MDC_DIST_SCALE;
}

static void R2_ModelMdcVertexPosition( mdcSurface_t *surface, int numFrames, int frame, int index, vec3_t out ) {
	short *baseFrames;
	short *compFrames;
	md3XyzNormal_t *baseXyz;
	mdcXyzCompressed_t *compXyz;
	int baseFrame;
	int compFrame;

	baseFrames = (short *)( (byte *)surface + surface->ofsFrameBaseFrames );
	compFrames = (short *)( (byte *)surface + surface->ofsFrameCompFrames );
	if ( frame < 0 || frame >= numFrames ) {
		frame = 0;
	}
	baseFrame = baseFrames[frame];
	compFrame = compFrames[frame];

	if ( baseFrame < 0 || baseFrame >= surface->numBaseFrames ) {
		baseFrame = 0;
	}

	baseXyz = (md3XyzNormal_t *)( (byte *)surface + surface->ofsXyzNormals ) + baseFrame * surface->numVerts + index;
	out[0] = baseXyz->xyz[0] * MD3_XYZ_SCALE;
	out[1] = baseXyz->xyz[1] * MD3_XYZ_SCALE;
	out[2] = baseXyz->xyz[2] * MD3_XYZ_SCALE;

	if ( compFrame >= 0 && compFrame < surface->numCompFrames ) {
		vec3_t offset;
		compXyz = (mdcXyzCompressed_t *)( (byte *)surface + surface->ofsXyzCompressed ) + compFrame * surface->numVerts + index;
		R2_ModelDecodeMdcOffset( compXyz->ofsVec, offset );
		VectorAdd( out, offset, out );
	}
}

static void R2_ModelRenderMdcSurface( const refdef_t *fd, const refEntity_t *ent, mdcHeader_t *header, mdcSurface_t *surface, const float *mvp ) {
	md3Triangle_t *triangles;
	md3St_t *st;
	md3Shader_t *shader = NULL;
	r2ModelVertex_t *vertices;
	float color[4];
	qhandle_t hShader = 0;
	int frame;
	int oldframe;
	int vertexCount;
	int i;
	int outIndex = 0;

	if ( surface->numTriangles <= 0 || surface->numVerts <= 0 ) {
		return;
	}

	vertexCount = surface->numTriangles * 3;
	if ( !R2_ModelEnsureScratch( vertexCount, &vertices ) ) {
		return;
	}

	frame = R2_ModelFrameForEntity( ent, ent->frame, header->numFrames, 0 );
	oldframe = R2_ModelFrameForEntity( ent, ent->oldframe, header->numFrames, frame );

	triangles = (md3Triangle_t *)( (byte *)surface + surface->ofsTriangles );
	st = (md3St_t *)( (byte *)surface + surface->ofsSt );

	if ( surface->numShaders > 0 ) {
		shader = (md3Shader_t *)( (byte *)surface + surface->ofsShaders );
	}
	hShader = R2_ModelSurfaceShader( ent, surface->name, shader, surface->numShaders );

	R2_ModelEntityColor( ent, color );
	for ( i = 0; i < surface->numTriangles; ++i ) {
		int j;
		for ( j = 0; j < 3; ++j ) {
			int index = triangles[i].indexes[j];
			vec3_t oldPos;
			vec3_t newPos;
			vec3_t lerped;
			vec3_t pos;
			float backlerp = ent->backlerp;
			float frontlerp = 1.0f - backlerp;

			if ( index < 0 || index >= surface->numVerts ) {
				continue;
			}

			R2_ModelMdcVertexPosition( surface, header->numFrames, frame, index, newPos );
			R2_ModelMdcVertexPosition( surface, header->numFrames, oldframe, index, oldPos );
			VectorScale( oldPos, backlerp, lerped );
			VectorMA( lerped, frontlerp, newPos, lerped );

			R2_ModelTransformVertex( fd, ent, lerped, pos );
			VectorCopy( pos, vertices[outIndex].xyz );
			vertices[outIndex].st[0] = st[index].st[0];
			vertices[outIndex].st[1] = st[index].st[1];
			vertices[outIndex].color[0] = color[0];
			vertices[outIndex].color[1] = color[1];
			vertices[outIndex].color[2] = color[2];
			vertices[outIndex].color[3] = color[3];
			++outIndex;
		}
	}

	if ( outIndex > 0 ) {
		++r2_modelFrameSurfaces;
		++r2_modelFrameDraws;
		r2_modelFrameTriangles += outIndex / 3;
		R2_GL_DrawModelTriangles( vertices, outIndex, hShader, mvp );
	}
}

static qboolean R2_ModelMdsBuildVertexTable( mdsSurface_t *surface, mdsVertex_t **table ) {
	mdsVertex_t *vertex;
	byte *end;
	int i;

	if ( !surface || !table || surface->numVerts <= 0 || surface->numVerts > MDS_MAX_VERTS ) {
		return qfalse;
	}

	vertex = (mdsVertex_t *)( (byte *)surface + surface->ofsVerts );
	end = (byte *)surface + surface->ofsEnd;
	for ( i = 0; i < surface->numVerts; ++i ) {
		if ( (byte *)vertex < (byte *)surface || (byte *)vertex + sizeof( *vertex ) > end ||
			 vertex->numWeights <= 0 || vertex->numWeights > MDS_MAX_BONES ||
			 (byte *)&vertex->weights[vertex->numWeights] > end ) {
			return qfalse;
		}
		table[i] = vertex;
		vertex = (mdsVertex_t *)&vertex->weights[vertex->numWeights];
	}
	return qtrue;
}

static void R2_ModelRenderMdsSurface( const refdef_t *fd, const refEntity_t *ent,
									  mdsHeader_t *header, mdsSurface_t *surface, const float *mvp ) {
	mdsTriangle_t *triangles;
	mdsVertex_t *vertexTable[MDS_MAX_VERTS];
	r2MdsBoneContext_t boneContext;
	r2ModelVertex_t *vertices;
	float color[4];
	qhandle_t hShader;
	int *boneRefs;
	int vertexCount;
	int i;
	int outIndex = 0;

	if ( !header || !surface || surface->numTriangles <= 0 || surface->numVerts <= 0 ||
		 surface->numVerts > MDS_MAX_VERTS ) {
		return;
	}

	vertexCount = surface->numTriangles * 3;
	if ( !R2_ModelEnsureScratch( vertexCount, &vertices ) ) {
		return;
	}
	if ( !R2_ModelMdsBuildVertexTable( surface, vertexTable ) ) {
		return;
	}

	boneRefs = (int *)( (byte *)surface + surface->ofsBoneReferences );
	R2_MdsInitBoneContext( &boneContext, header, ent );
	R2_MdsCalcBones( &boneContext, boneRefs, surface->numBoneReferences );

	triangles = (mdsTriangle_t *)( (byte *)surface + surface->ofsTriangles );
	hShader = R2_ModelMdsSurfaceShader( ent, surface );
	R2_ModelEntityColor( ent, color );

	for ( i = 0; i < surface->numTriangles; ++i ) {
		int j;

		for ( j = 0; j < 3; ++j ) {
			int index = triangles[i].indexes[j];
			mdsVertex_t *vertex;
			vec3_t skinned;
			vec3_t pos;

			if ( index < 0 || index >= surface->numVerts ) {
				continue;
			}

			vertex = vertexTable[index];
			R2_MdsTransformVertex( &boneContext, vertex, skinned );
			R2_ModelTransformVertex( fd, ent, skinned, pos );
			VectorCopy( pos, vertices[outIndex].xyz );
			vertices[outIndex].st[0] = vertex->texCoords[0];
			vertices[outIndex].st[1] = vertex->texCoords[1];
			vertices[outIndex].color[0] = color[0];
			vertices[outIndex].color[1] = color[1];
			vertices[outIndex].color[2] = color[2];
			vertices[outIndex].color[3] = color[3];
			++outIndex;
		}
	}

	if ( outIndex > 0 ) {
		++r2_modelFrameSurfaces;
		++r2_modelFrameDraws;
		r2_modelFrameTriangles += outIndex / 3;
		R2_GL_DrawModelTriangles( vertices, outIndex, hShader, mvp );
	}
}

static void R2_ModelRenderEntity( const refdef_t *fd, const refEntity_t *ent, const float *mvp ) {
	r2Model_t *model;
	md3Surface_t *surface;
	mdcSurface_t *mdcSurface;
	mdsSurface_t *mdsSurface;
	int i;

	if ( !R2_ModelValidHandle( ent->hModel ) ) {
		return;
	}

	model = &r2_models[ent->hModel];
	if ( model->type == R2_MODEL_EMPTY ) {
		return;
	}
	++r2_modelFrameEntities;
	if ( model->type == R2_MODEL_INLINE ) {
		R2_WorldDrawInlineModel( model->inlineModelIndex, fd, ent );
		return;
	}
	if ( model->type == R2_MODEL_MDC ) {
		mdcSurface = (mdcSurface_t *)( (byte *)model->mdc + model->mdc->ofsSurfaces );
		for ( i = 0; i < model->mdc->numSurfaces; ++i ) {
			R2_ModelRenderMdcSurface( fd, ent, model->mdc, mdcSurface, mvp );
			mdcSurface = (mdcSurface_t *)( (byte *)mdcSurface + mdcSurface->ofsEnd );
		}
		return;
	}
	if ( model->type == R2_MODEL_MDS ) {
		mdsSurface = (mdsSurface_t *)( (byte *)model->mds + model->mds->ofsSurfaces );
		for ( i = 0; i < model->mds->numSurfaces; ++i ) {
			R2_ModelRenderMdsSurface( fd, ent, model->mds, mdsSurface, mvp );
			mdsSurface = (mdsSurface_t *)( (byte *)mdsSurface + mdsSurface->ofsEnd );
		}
		return;
	}

	surface = (md3Surface_t *)( (byte *)model->md3 + model->md3->ofsSurfaces );
	for ( i = 0; i < model->md3->numSurfaces; ++i ) {
		R2_ModelRenderSurface( fd, ent, surface, mvp );
		surface = (md3Surface_t *)( (byte *)surface + surface->ofsEnd );
	}
}

void R2_ModelRenderScene( const refdef_t *fd ) {
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

	if ( !r2_modelDraw ) {
		r2_modelDraw = r2_ri.Cvar_Get( "r2_modelDraw", "1", CVAR_ARCHIVE );
	}
	if ( !r2_modelDebugStatus ) {
		r2_modelDebugStatus = r2_ri.Cvar_Get( "r2_modelDebugStatus", "0", CVAR_ARCHIVE );
	}
	if ( r2_modelDraw && !r2_modelDraw->integer ) {
		return;
	}
	if ( !fd || r2_numSceneEntities <= 0 || r2.glConfig.vidWidth <= 0 || r2.glConfig.vidHeight <= 0 ) {
		return;
	}

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

	R2_ModelProjectionMatrix( fd->fov_x, fd->fov_y, mvp );

	glViewport( viewportX, viewportY, viewportW, viewportH );
	glEnable( GL_SCISSOR_TEST );
	glScissor( scissorX, scissorY, scissorW, scissorH );
	if ( fd->rdflags & RDF_NOWORLDMODEL ) {
		glClear( GL_DEPTH_BUFFER_BIT );
	}
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	glDisable( GL_CULL_FACE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	r2_modelFrameEntities = 0;
	r2_modelFrameSurfaces = 0;
	r2_modelFrameTriangles = 0;
	r2_modelFrameDraws = 0;
	for ( i = 0; i < r2_numSceneEntities; ++i ) {
		const refEntity_t *ent = &r2_sceneEntities[i];
		qboolean depthHack;

		if ( !R2_ModelShouldDrawEntity( fd, ent ) ) {
			continue;
		}

		depthHack = ( ent->renderfx & RF_DEPTHHACK ) != 0;
		if ( depthHack ) {
			glDepthRange( 0.0, 0.3 );
		}
		R2_ModelRenderEntity( fd, ent, mvp );
		if ( depthHack ) {
			glDepthRange( 0.0, 1.0 );
		}
	}
	if ( r2_modelDebugStatus && r2_modelDebugStatus->integer ) {
		R2_GL_SetStatusTitle( va( "Return to Castle Wolfenstein - Renderer2 GL %d.%d | models ent %d surf %d tris %d draws %d",
								   r2.contextMajor, r2.contextMinor,
								   r2_modelFrameEntities, r2_modelFrameSurfaces,
								   r2_modelFrameTriangles, r2_modelFrameDraws ) );
	}

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glViewport( 0, 0, r2.glConfig.vidWidth, r2.glConfig.vidHeight );
}

int R2_ModelLerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagName, int startIndex ) {
	r2Model_t *model;
	md3Header_t *header;
	int frame;
	int oldframe;
	int i;

	if ( !tag || !refent || !tagName || !R2_ModelValidHandle( refent->hModel ) ) {
		R2_ModelClearTag( tag );
		return -1;
	}

	model = &r2_models[refent->hModel];
	if ( model->type == R2_MODEL_INLINE ) {
		R2_ModelClearTag( tag );
		return -1;
	}
	if ( model->type == R2_MODEL_EMPTY ) {
		if ( model->skeletalPlaceholder ) {
			if ( startIndex > 0 ) {
				R2_ModelClearTag( tag );
				return -1;
			}
			VectorClear( tag->origin );
			AxisClear( tag->axis );
			return 0;
		}
		R2_ModelClearTag( tag );
		return -1;
	}
	if ( model->type == R2_MODEL_MDS ) {
		mdsHeader_t *mds = model->mds;
		mdsTag_t *tags;
		mdsBoneInfo_t *boneInfo;
		int boneList[MDS_MAX_BONES];
		int numBones = 0;

		if ( startIndex < 0 ) {
			startIndex = 0;
		}
		if ( startIndex > mds->numTags ) {
			R2_ModelClearTag( tag );
			return -1;
		}

		tags = (mdsTag_t *)( (byte *)mds + mds->ofsTags );
		for ( i = startIndex; i < mds->numTags; ++i ) {
			if ( !Q_stricmp( tags[i].name, tagName ) ) {
				r2MdsBoneContext_t boneContext;

				if ( tags[i].boneIndex < 0 || tags[i].boneIndex >= mds->numBones ) {
					R2_ModelClearTag( tag );
					return -1;
				}

				boneInfo = (mdsBoneInfo_t *)( (byte *)mds + mds->ofsBones );
				R2_MdsRecursiveBoneListAdd( tags[i].boneIndex, boneList, &numBones,
											boneInfo, MDS_MAX_BONES );
				R2_MdsInitBoneContext( &boneContext, mds, refent );
				R2_MdsCalcBones( &boneContext, boneList, numBones );

				VectorCopy( boneContext.bones[tags[i].boneIndex].translation, tag->origin );
				VectorCopy( boneContext.bones[tags[i].boneIndex].matrix[0], tag->axis[0] );
				VectorCopy( boneContext.bones[tags[i].boneIndex].matrix[1], tag->axis[1] );
				VectorCopy( boneContext.bones[tags[i].boneIndex].matrix[2], tag->axis[2] );
				return i;
			}
		}
		R2_ModelClearTag( tag );
		return -1;
	}
	if ( model->type == R2_MODEL_MDC ) {
		mdcHeader_t *mdc = model->mdc;
		mdcTagName_t *tagNames;
		mdcTag_t *newTags;
		mdcTag_t *oldTags;

		frame = R2_ModelFrameForEntity( refent, refent->frame, mdc->numFrames, 0 );
		oldframe = R2_ModelFrameForEntity( refent, refent->oldframe, mdc->numFrames, frame );
		if ( startIndex < 0 ) {
			startIndex = 0;
		}

		tagNames = (mdcTagName_t *)( (byte *)mdc + mdc->ofsTagNames );
		newTags = (mdcTag_t *)( (byte *)mdc + mdc->ofsTags ) + frame * mdc->numTags;
		oldTags = (mdcTag_t *)( (byte *)mdc + mdc->ofsTags ) + oldframe * mdc->numTags;
		for ( i = startIndex; i < mdc->numTags; ++i ) {
			if ( !Q_stricmp( tagNames[i].name, tagName ) ) {
				md3Tag_t oldTag;
				md3Tag_t newTag;

				R2_ModelMdcTagToMd3Tag( &oldTags[i], &oldTag );
				R2_ModelMdcTagToMd3Tag( &newTags[i], &newTag );
				R2_ModelLerpMd3Tags( tag, &oldTag, &newTag, refent->backlerp );
				return i;
			}
		}
		R2_ModelClearTag( tag );
		return -1;
	}

	header = model->md3;
	frame = R2_ModelFrameForEntity( refent, refent->frame, header->numFrames, 0 );
	oldframe = R2_ModelFrameForEntity( refent, refent->oldframe, header->numFrames, frame );
	if ( startIndex < 0 ) {
		startIndex = 0;
	}

	for ( i = startIndex; i < header->numTags; ++i ) {
		md3Tag_t *newTag = (md3Tag_t *)( (byte *)header + header->ofsTags ) + frame * header->numTags + i;
		md3Tag_t *oldTag = (md3Tag_t *)( (byte *)header + header->ofsTags ) + oldframe * header->numTags + i;

		if ( !Q_stricmp( newTag->name, tagName ) ) {
			R2_ModelLerpMd3Tags( tag, oldTag, newTag, refent->backlerp );
			return i;
		}
	}

	R2_ModelClearTag( tag );
	return -1;
}

void R2_ModelBounds( qhandle_t model, vec3_t mins, vec3_t maxs ) {
	if ( R2_ModelValidHandle( model ) ) {
		VectorCopy( r2_models[model].mins, mins );
		VectorCopy( r2_models[model].maxs, maxs );
		return;
	}

	VectorClear( mins );
	VectorClear( maxs );
}
