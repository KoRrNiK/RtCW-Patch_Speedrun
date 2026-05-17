/*
===========================================================================

Renderer2 skin registry.

The first world-rendering pass only needs skins to be valid handles so cgame
can finish map initialization.  Surface remapping and skin model metadata will
be expanded when skeletal/world entity rendering is brought over.

===========================================================================
*/

#include "r2_local.h"

#define R2_MAX_SKINS 1024
#define R2_MAX_SKIN_MODELS 16
#define R2_MAX_SKIN_SURFACES 256

typedef struct {
	char type[MAX_QPATH];
	char model[MAX_QPATH];
} r2SkinModel_t;

typedef struct {
	char name[MAX_QPATH];
	qhandle_t shader;
} r2SkinSurface_t;

typedef struct {
	char name[MAX_QPATH];
	char *text;
	r2SkinModel_t models[R2_MAX_SKIN_MODELS];
	r2SkinSurface_t surfaces[R2_MAX_SKIN_SURFACES];
	int numModels;
	int numSurfaces;
	qhandle_t defaultShader;
	vec3_t scale;
	qboolean hasScale;
	qboolean valid;
} r2Skin_t;

static r2Skin_t r2_skins[R2_MAX_SKINS];
static int r2_numSkins = 1;

static char *R2_SkinParseToken( char **data ) {
	static char token[MAX_TOKEN_CHARS];
	char *p;
	int len = 0;

	token[0] = '\0';
	if ( !data || !*data ) {
		return token;
	}

	p = *data;
	while ( *p && ( *p <= ' ' || *p == ',' ) ) {
		++p;
	}

	if ( *p == '"' ) {
		++p;
		while ( *p && *p != '"' ) {
			if ( len < (int)sizeof( token ) - 1 ) {
				token[len++] = *p;
			}
			++p;
		}
		if ( *p == '"' ) {
			++p;
		}
	} else {
		while ( *p && *p != ',' && *p != '\n' && *p != '\r' ) {
			if ( len < (int)sizeof( token ) - 1 ) {
				token[len++] = *p;
			}
			++p;
		}
		while ( len > 0 && token[len - 1] <= ' ' ) {
			--len;
		}
	}

	token[len] = '\0';
	*data = p;
	return token;
}

static void R2_SkinNormalizeSurfaceName( const char *in, char *out, int outSize ) {
	int len;

	if ( !out || outSize <= 0 ) {
		return;
	}
	out[0] = '\0';
	if ( !in ) {
		return;
	}

	Q_strncpyz( out, in, outSize );
	Q_strlwr( out );

	len = (int)strlen( out );
	if ( len > 2 && out[len - 2] == '_' ) {
		out[len - 2] = '\0';
	}
}

static void R2_SkinParseText( r2Skin_t *skin ) {
	char *text;

	if ( !skin || !skin->text ) {
		return;
	}

	text = skin->text;
	while ( text && *text ) {
		char line[MAX_TOKEN_CHARS];
		char *lineText;
		char *key;
		char keyCopy[MAX_QPATH];
		char *value;
		int lineLen = 0;

		while ( text[lineLen] && text[lineLen] != '\n' && text[lineLen] != '\r' &&
				lineLen < (int)sizeof( line ) - 1 ) {
			++lineLen;
		}
		memcpy( line, text, lineLen );
		line[lineLen] = '\0';
		text += lineLen;
		while ( *text == '\n' || *text == '\r' ) {
			++text;
		}

		lineText = line;
		key = R2_SkinParseToken( &lineText );
		if ( !key[0] ) {
			continue;
		}
		Q_strncpyz( keyCopy, key, sizeof( keyCopy ) );
		value = R2_SkinParseToken( &lineText );

		if ( strstr( keyCopy, "md3_" ) && value[0] && skin->numModels < R2_MAX_SKIN_MODELS ) {
			Q_strncpyz( skin->models[skin->numModels].type, keyCopy, sizeof( skin->models[skin->numModels].type ) );
			Q_strncpyz( skin->models[skin->numModels].model, value, sizeof( skin->models[skin->numModels].model ) );
			++skin->numModels;
		} else if ( strstr( keyCopy, "playerscale" ) && value[0] ) {
			float scale = (float)atof( value );
			VectorSet( skin->scale, scale, scale, scale );
			skin->hasScale = qtrue;
		} else if ( !strstr( keyCopy, "tag_" ) && value[0] && skin->numSurfaces < R2_MAX_SKIN_SURFACES ) {
			R2_SkinNormalizeSurfaceName( keyCopy,
										 skin->surfaces[skin->numSurfaces].name,
										 sizeof( skin->surfaces[skin->numSurfaces].name ) );
			skin->surfaces[skin->numSurfaces].shader = R2_GL_RegisterShader( value, qfalse );
			++skin->numSurfaces;
		}
	}
}

void R2_SkinShutdown( void ) {
	int i;

	for ( i = 1; i < r2_numSkins; ++i ) {
		free( r2_skins[i].text );
	}
	memset( r2_skins, 0, sizeof( r2_skins ) );
	r2_numSkins = 1;
}

qhandle_t R2_SkinRegister( const char *name ) {
	byte *data = NULL;
	int len;
	int i;

	if ( !name || !name[0] ) {
		return 0;
	}

	for ( i = 1; i < r2_numSkins; ++i ) {
		if ( r2_skins[i].valid && !Q_stricmp( r2_skins[i].name, name ) ) {
			return i;
		}
	}

	if ( r2_numSkins >= R2_MAX_SKINS ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: skin limit reached for '%s'\n", name );
		return 0;
	}

	if ( strlen( name ) < 5 || Q_stricmp( name + strlen( name ) - 5, ".skin" ) ) {
		Q_strncpyz( r2_skins[r2_numSkins].name, name, sizeof( r2_skins[r2_numSkins].name ) );
		r2_skins[r2_numSkins].defaultShader = R2_GL_RegisterShader( name, qfalse );
		r2_skins[r2_numSkins].valid = qtrue;
		return r2_numSkins++;
	}

	len = r2_ri.FS_ReadFile( name, (void **)&data );
	if ( len <= 0 || !data ) {
		if ( data ) {
			r2_ri.FS_FreeFile( data );
		}
		r2_ri.Printf( PRINT_WARNING, "Renderer2: missing skin '%s'\n", name );
		return 0;
	}

	Q_strncpyz( r2_skins[r2_numSkins].name, name, sizeof( r2_skins[r2_numSkins].name ) );
	r2_skins[r2_numSkins].text = (char *)malloc( len + 1 );
	if ( r2_skins[r2_numSkins].text ) {
		memcpy( r2_skins[r2_numSkins].text, data, len );
		r2_skins[r2_numSkins].text[len] = '\0';
	}
	r2_skins[r2_numSkins].valid = qtrue;
	R2_SkinParseText( &r2_skins[r2_numSkins] );
	r2_ri.FS_FreeFile( data );
	return r2_numSkins++;
}

qboolean R2_SkinGetModel( qhandle_t skinid, const char *type, char *name ) {
	r2Skin_t *skin;
	int i;

	if ( name ) {
		name[0] = '\0';
	}
	if ( skinid <= 0 || skinid >= r2_numSkins || !r2_skins[skinid].valid || !type || !name ) {
		return qfalse;
	}

	skin = &r2_skins[skinid];
	if ( !Q_stricmp( type, "playerscale" ) ) {
		if ( !skin->hasScale ) {
			return qfalse;
		}
		Com_sprintf( name, MAX_QPATH, "%.2f %.2f %.2f", skin->scale[0], skin->scale[1], skin->scale[2] );
		return qtrue;
	}

	for ( i = 0; i < skin->numModels; ++i ) {
		if ( !Q_stricmp( skin->models[i].type, type ) ) {
			Q_strncpyz( name, skin->models[i].model, MAX_QPATH );
			return qtrue;
		}
	}

	return qfalse;
}

qhandle_t R2_SkinGetSurfaceShader( qhandle_t skinid, const char *surfaceName ) {
	r2Skin_t *skin;
	char normalizedSurface[MAX_QPATH];
	int i;

	if ( skinid <= 0 || skinid >= r2_numSkins || !r2_skins[skinid].valid ) {
		return 0;
	}

	skin = &r2_skins[skinid];
	if ( skin->defaultShader ) {
		return skin->defaultShader;
	}
	if ( !surfaceName || !surfaceName[0] ) {
		return 0;
	}

	R2_SkinNormalizeSurfaceName( surfaceName, normalizedSurface, sizeof( normalizedSurface ) );
	for ( i = 0; i < skin->numSurfaces; ++i ) {
		if ( !strcmp( skin->surfaces[i].name, normalizedSurface ) ) {
			return skin->surfaces[i].shader;
		}
	}

	return 0;
}
