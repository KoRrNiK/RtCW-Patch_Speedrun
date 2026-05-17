/*
===========================================================================

Renderer2 OpenGL 4 bootstrap.

===========================================================================
*/

#include "r2_gl.h"
#include "r2_material.h"

#define JPEG_INTERNALS
#include "../jpeg-6/jpeglib.h"
#include <setjmp.h>

r2State_t r2;
r2GLProcs_t r2gl;

typedef struct {
	int width;
	int height;
} r2VidMode_t;

static const r2VidMode_t r2_vidModes[] = {
	{ 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 480 },
	{ 800, 600 }, { 960, 720 }, { 1024, 768 }, { 1152, 864 },
	{ 1280, 960 }, { 1280, 1024 }, { 1400, 1050 }, { 1600, 1200 },
	{ 1920, 1440 }, { 2048, 1536 }, { 800, 480 }, { 856, 480 },
	{ 1024, 600 }, { 1280, 720 }, { 1280, 768 }, { 1280, 800 },
	{ 1360, 768 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 },
	{ 1600, 1024 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 },
	{ 2560, 1080 }, { 2560, 1440 }, { 2560, 1600 }, { 3200, 1800 },
	{ 3440, 1440 }, { 3840, 2160 }, { 3840, 2400 }, { 5120, 2880 }
};

static cvar_t *r2_mode;
static cvar_t *r2_customWidth;
static cvar_t *r2_customHeight;
static cvar_t *r2_customAspect;
static cvar_t *r2_fullscreen;
static cvar_t *r2_borderless;
static cvar_t *r2_resizableWindow;
static cvar_t *r2_sdlDpiScale;
static cvar_t *r2_monitor;
static cvar_t *r2_colorBits;
static cvar_t *r2_depthBits;
static cvar_t *r2_stencilBits;
static cvar_t *r2_swapInterval;
static cvar_t *r2_glMajor;
static cvar_t *r2_glMinor;
static cvar_t *r2_glDebug;
static cvar_t *r2_debugTriangle;
static cvar_t *r2_debugClear;
static cvar_t *r2_debugStatus;
static cvar_t *r2_shaderTcMod;
static cvar_t *r2_worldTcMod;
static cvar_t *r2_worldCloudStages;
static cvar_t *r2_worldCloudStrength;
static cvar_t *r2_worldTerrainCollapseStages;
static cvar_t *r2_worldTerrainInvertAlpha;
static cvar_t *r2_worldTerrainDebugBlend;
static cvar_t *r2_logMissingTextures;
static qboolean r2_debugTriangleLogged;
static char r2_lastStatusTitle[256];

typedef struct {
	float x;
	float y;
	float s;
	float t;
	float r;
	float g;
	float b;
	float a;
} r2ColorVertex_t;

#define R2_MAX_TEXTURES 8192
#define R2_MAX_SHADER_ALIASES 8192
#define R2_MAX_FONTS 6
#define R2_MAX_RAW_TEXTURES 32

typedef struct {
	char name[MAX_QPATH];
	GLuint texnum;
	int width;
	int height;
	qboolean valid;
	qboolean noMip;
	float scrollS;
	float scrollT;
	qboolean hasScroll;
	float scaleS;
	float scaleT;
	qboolean hasScale;
	r2CullMode_t cullMode;
	qboolean isSky;
	qboolean noDraw;
	qboolean noLightmap;
	qboolean hasAlpha;
	qboolean shaderDefApplied;
	qboolean applyingShaderDef;
	int stageCount;
	r2ShaderStage_t stages[R2_MAX_SHADER_STAGES];
} r2Texture_t;

typedef struct {
	char name[MAX_QPATH];
	char image[MAX_QPATH];
	float scrollS;
	float scrollT;
	qboolean hasScroll;
	float scaleS;
	float scaleT;
	qboolean hasScale;
} r2ShaderAlias_t;

typedef struct {
	GLuint texnum;
	int width;
	int height;
	qboolean valid;
} r2RawTexture_t;

static r2Texture_t r2_textures[R2_MAX_TEXTURES];
static int r2_numTextures;
static r2ShaderAlias_t r2_shaderAliases[R2_MAX_SHADER_ALIASES];
static int r2_numShaderAliases;
static r2ShaderDef_t r2_shaderDefs[R2_MAX_SHADER_DEFS];
static int r2_numShaderDefs;
static qboolean r2_shaderAliasesLoaded;
static r2RawTexture_t r2_rawTextures[R2_MAX_RAW_TEXTURES];
static fontInfo_t r2_registeredFonts[R2_MAX_FONTS];
static int r2_registeredFontCount;

typedef struct {
	struct jpeg_error_mgr pub;
	jmp_buf setjmpBuffer;
	char message[JMSG_LENGTH_MAX];
} r2JpegError_t;

typedef struct {
	struct jpeg_source_mgr pub;
	const JOCTET *data;
	size_t len;
	qboolean startOfFile;
} r2JpegMemorySource_t;

static void R2_GL_JpegErrorExit( j_common_ptr cinfo ) {
	r2JpegError_t *err = (r2JpegError_t *)cinfo->err;
	if ( err && err->pub.format_message ) {
		( *err->pub.format_message )( cinfo, err->message );
	}
	longjmp( err->setjmpBuffer, 1 );
}

static void R2_GL_JpegInitSource( j_decompress_ptr cinfo ) {
	r2JpegMemorySource_t *src = (r2JpegMemorySource_t *)cinfo->src;
	src->startOfFile = qtrue;
}

static boolean R2_GL_JpegFillInputBuffer( j_decompress_ptr cinfo ) {
	static const JOCTET eoiBuffer[2] = { 0xFF, JPEG_EOI };
	r2JpegMemorySource_t *src = (r2JpegMemorySource_t *)cinfo->src;

	src->pub.next_input_byte = eoiBuffer;
	src->pub.bytes_in_buffer = sizeof( eoiBuffer );
	src->startOfFile = qfalse;
	return TRUE;
}

static void R2_GL_JpegSkipInputData( j_decompress_ptr cinfo, long numBytes ) {
	r2JpegMemorySource_t *src = (r2JpegMemorySource_t *)cinfo->src;

	if ( numBytes <= 0 ) {
		return;
	}
	if ( (size_t)numBytes > src->pub.bytes_in_buffer ) {
		R2_GL_JpegFillInputBuffer( cinfo );
		return;
	}
	src->pub.next_input_byte += numBytes;
	src->pub.bytes_in_buffer -= numBytes;
}

static void R2_GL_JpegTermSource( j_decompress_ptr cinfo ) {
	(void)cinfo;
}

static void R2_GL_JpegMemorySrc( j_decompress_ptr cinfo, const byte *data, int len ) {
	r2JpegMemorySource_t *src;

	if ( !cinfo->src ) {
		cinfo->src = (struct jpeg_source_mgr *)( *cinfo->mem->alloc_small )
			( (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof( r2JpegMemorySource_t ) );
	}

	src = (r2JpegMemorySource_t *)cinfo->src;
	src->data = data;
	src->len = len > 0 ? (size_t)len : 0;
	src->startOfFile = qtrue;
	src->pub.init_source = R2_GL_JpegInitSource;
	src->pub.fill_input_buffer = R2_GL_JpegFillInputBuffer;
	src->pub.skip_input_data = R2_GL_JpegSkipInputData;
	src->pub.resync_to_restart = jpeg_resync_to_restart;
	src->pub.term_source = R2_GL_JpegTermSource;
	src->pub.bytes_in_buffer = src->len;
	src->pub.next_input_byte = src->data;
}

static void R2_GL_NormalizeAssetName( const char *in, char *out, int outSize ) {
	int i;

	if ( !out || outSize <= 0 ) {
		return;
	}
	out[0] = '\0';
	if ( !in ) {
		return;
	}

	Q_strncpyz( out, in, outSize );
	for ( i = 0; out[i]; ++i ) {
		if ( out[i] == '\\' ) {
			out[i] = '/';
		}
	}
}

static void R2_GL_StripImageExtension( const char *in, char *out, int outSize ) {
	int len;

	R2_GL_NormalizeAssetName( in, out, outSize );
	len = (int)strlen( out );
	if ( len > 4 && ( !Q_stricmp( out + len - 4, ".tga" ) || !Q_stricmp( out + len - 4, ".jpg" ) ) ) {
		out[len - 4] = '\0';
	}
}

static void R2_GL_UpdateNativeWindowHandle( void ) {
#ifdef _WIN32
	if ( sdl_window ) {
		SDL_PropertiesID props = SDL_GetWindowProperties( sdl_window );
		g_wv.hWnd = (HWND)SDL_GetPointerProperty( props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL );
	} else {
		g_wv.hWnd = NULL;
	}
#endif
}

static void *R2_GL_GetProc( const char *name ) {
	void *proc = SDL_GL_GetProcAddress( name );
	if ( !proc ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: missing OpenGL proc %s\n", name );
	}
	return proc;
}

#define R2_LOAD_GL_PROC( member, name ) \
	do { \
		r2gl.member = ( r2_gl##member##Proc )R2_GL_GetProc( name ); \
		if ( !r2gl.member ) { \
			return qfalse; \
		} \
	} while ( 0 )

qboolean R2_GL_LoadCoreProcs( void ) {
	memset( &r2gl, 0, sizeof( r2gl ) );

	R2_LOAD_GL_PROC( GenVertexArrays, "glGenVertexArrays" );
	R2_LOAD_GL_PROC( BindVertexArray, "glBindVertexArray" );
	R2_LOAD_GL_PROC( DeleteVertexArrays, "glDeleteVertexArrays" );
	R2_LOAD_GL_PROC( GenBuffers, "glGenBuffers" );
	R2_LOAD_GL_PROC( BindBuffer, "glBindBuffer" );
	R2_LOAD_GL_PROC( BufferData, "glBufferData" );
	R2_LOAD_GL_PROC( BufferSubData, "glBufferSubData" );
	R2_LOAD_GL_PROC( DeleteBuffers, "glDeleteBuffers" );
	R2_LOAD_GL_PROC( CreateShader, "glCreateShader" );
	R2_LOAD_GL_PROC( ShaderSource, "glShaderSource" );
	R2_LOAD_GL_PROC( CompileShader, "glCompileShader" );
	R2_LOAD_GL_PROC( GetShaderiv, "glGetShaderiv" );
	R2_LOAD_GL_PROC( GetShaderInfoLog, "glGetShaderInfoLog" );
	R2_LOAD_GL_PROC( DeleteShader, "glDeleteShader" );
	R2_LOAD_GL_PROC( CreateProgram, "glCreateProgram" );
	R2_LOAD_GL_PROC( AttachShader, "glAttachShader" );
	R2_LOAD_GL_PROC( LinkProgram, "glLinkProgram" );
	R2_LOAD_GL_PROC( GetProgramiv, "glGetProgramiv" );
	R2_LOAD_GL_PROC( GetProgramInfoLog, "glGetProgramInfoLog" );
	R2_LOAD_GL_PROC( UseProgram, "glUseProgram" );
	R2_LOAD_GL_PROC( DeleteProgram, "glDeleteProgram" );
	R2_LOAD_GL_PROC( GetUniformLocation, "glGetUniformLocation" );
	R2_LOAD_GL_PROC( Uniform1i, "glUniform1i" );
	R2_LOAD_GL_PROC( Uniform1f, "glUniform1f" );
	R2_LOAD_GL_PROC( Uniform2f, "glUniform2f" );
	R2_LOAD_GL_PROC( UniformMatrix4fv, "glUniformMatrix4fv" );
	R2_LOAD_GL_PROC( ActiveTexture, "glActiveTexture" );
	R2_LOAD_GL_PROC( GenerateMipmap, "glGenerateMipmap" );
	R2_LOAD_GL_PROC( VertexAttribPointer, "glVertexAttribPointer" );
	R2_LOAD_GL_PROC( EnableVertexAttribArray, "glEnableVertexAttribArray" );
	R2_LOAD_GL_PROC( DisableVertexAttribArray, "glDisableVertexAttribArray" );
	R2_LOAD_GL_PROC( MultiDrawArrays, "glMultiDrawArrays" );
	R2_LOAD_GL_PROC( GetStringi, "glGetStringi" );

	return qtrue;
}

static qboolean R2_GL_CompileShader( GLenum type, const char *source, GLuint *shaderOut ) {
	GLuint shader;
	GLint ok = 0;
	GLint logLength = 0;
	char log[2048];

	shader = r2gl.CreateShader( type );
	if ( !shader ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: glCreateShader failed\n" );
		return qfalse;
	}

	r2gl.ShaderSource( shader, 1, &source, NULL );
	r2gl.CompileShader( shader );
	r2gl.GetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if ( !ok ) {
		r2gl.GetShaderiv( shader, GL_INFO_LOG_LENGTH, &logLength );
		if ( logLength > (int)sizeof( log ) ) {
			logLength = sizeof( log );
		}
		log[0] = '\0';
		r2gl.GetShaderInfoLog( shader, logLength, NULL, log );
		r2_ri.Printf( PRINT_WARNING, "Renderer2: shader compile failed: %s\n", log );
		r2gl.DeleteShader( shader );
		return qfalse;
	}

	*shaderOut = shader;
	return qtrue;
}

static qboolean R2_GL_CreateColorPipeline( void ) {
	static const char *vertexShaderSource =
		"#version 330 core\n"
		"layout(location = 0) in vec2 a_pos;\n"
		"layout(location = 1) in vec2 a_tex;\n"
		"layout(location = 2) in vec4 a_color;\n"
		"out vec2 v_tex;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
		"    v_tex = a_tex;\n"
		"    v_color = a_color;\n"
		"}\n";
	static const char *fragmentShaderSource =
		"#version 330 core\n"
		"uniform sampler2D u_texture;\n"
		"in vec2 v_tex;\n"
		"in vec4 v_color;\n"
		"out vec4 out_color;\n"
		"void main() {\n"
		"    out_color = texture(u_texture, v_tex) * v_color;\n"
		"}\n";
	GLuint vs = 0;
	GLuint fs = 0;
	GLuint program = 0;
	GLint ok = 0;
	GLint logLength = 0;
	char log[2048];

	if ( !R2_GL_CompileShader( GL_VERTEX_SHADER, vertexShaderSource, &vs ) ) {
		return qfalse;
	}
	if ( !R2_GL_CompileShader( GL_FRAGMENT_SHADER, fragmentShaderSource, &fs ) ) {
		r2gl.DeleteShader( vs );
		return qfalse;
	}

	program = r2gl.CreateProgram();
	r2gl.AttachShader( program, vs );
	r2gl.AttachShader( program, fs );
	r2gl.LinkProgram( program );
	r2gl.GetProgramiv( program, GL_LINK_STATUS, &ok );

	r2gl.DeleteShader( vs );
	r2gl.DeleteShader( fs );

	if ( !ok ) {
		r2gl.GetProgramiv( program, GL_INFO_LOG_LENGTH, &logLength );
		if ( logLength > (int)sizeof( log ) ) {
			logLength = sizeof( log );
		}
		log[0] = '\0';
		r2gl.GetProgramInfoLog( program, logLength, NULL, log );
		r2_ri.Printf( PRINT_WARNING, "Renderer2: color program link failed: %s\n", log );
		r2gl.DeleteProgram( program );
		return qfalse;
	}

	r2gl.GenVertexArrays( 1, &r2.colorVao );
	r2gl.GenBuffers( 1, &r2.colorVbo );
	r2gl.BindVertexArray( r2.colorVao );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, r2.colorVbo );
	r2gl.BufferData( GL_ARRAY_BUFFER, sizeof( r2ColorVertex_t ) * 6, NULL, GL_DYNAMIC_DRAW );
	r2gl.VertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( r2ColorVertex_t ), (const void *)0 );
	r2gl.EnableVertexAttribArray( 0 );
	r2gl.VertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( r2ColorVertex_t ), (const void *)( sizeof( float ) * 2 ) );
	r2gl.EnableVertexAttribArray( 1 );
	r2gl.VertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( r2ColorVertex_t ), (const void *)( sizeof( float ) * 4 ) );
	r2gl.EnableVertexAttribArray( 2 );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	r2gl.BindVertexArray( 0 );

	r2.colorProgram = program;
	r2.colorSamplerLoc = r2gl.GetUniformLocation( r2.colorProgram, "u_texture" );
	if ( r2.colorSamplerLoc >= 0 ) {
		r2gl.UseProgram( r2.colorProgram );
		r2gl.Uniform1i( r2.colorSamplerLoc, 0 );
		r2gl.UseProgram( 0 );
	}
	r2.pipelineReady = qtrue;
	return qtrue;
}

static qboolean R2_GL_CreateModelPipeline( void ) {
	static const char *vertexShaderSource =
		"#version 330 core\n"
		"layout(location = 0) in vec3 a_pos;\n"
		"layout(location = 1) in vec2 a_tex;\n"
		"layout(location = 2) in vec4 a_color;\n"
		"uniform mat4 u_mvp;\n"
		"out vec2 v_tex;\n"
		"out vec4 v_color;\n"
		"void main() {\n"
		"    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
		"    v_tex = a_tex;\n"
		"    v_color = a_color;\n"
		"}\n";
	static const char *fragmentShaderSource =
		"#version 330 core\n"
		"uniform sampler2D u_texture;\n"
		"uniform int u_alphaTest;\n"
		"in vec2 v_tex;\n"
		"in vec4 v_color;\n"
		"out vec4 out_color;\n"
		"void main() {\n"
		"    vec4 color = texture(u_texture, v_tex) * v_color;\n"
		"    if (u_alphaTest == 1 && color.a <= 0.0) discard;\n"
		"    if (u_alphaTest == 2 && color.a >= 0.5) discard;\n"
		"    if (u_alphaTest == 3 && color.a < 0.5) discard;\n"
		"    out_color = color;\n"
		"}\n";
	GLuint vs = 0;
	GLuint fs = 0;
	GLuint program = 0;
	GLint ok = 0;
	GLint logLength = 0;
	char log[2048];

	if ( !R2_GL_CompileShader( GL_VERTEX_SHADER, vertexShaderSource, &vs ) ) {
		return qfalse;
	}
	if ( !R2_GL_CompileShader( GL_FRAGMENT_SHADER, fragmentShaderSource, &fs ) ) {
		r2gl.DeleteShader( vs );
		return qfalse;
	}

	program = r2gl.CreateProgram();
	r2gl.AttachShader( program, vs );
	r2gl.AttachShader( program, fs );
	r2gl.LinkProgram( program );
	r2gl.GetProgramiv( program, GL_LINK_STATUS, &ok );

	r2gl.DeleteShader( vs );
	r2gl.DeleteShader( fs );

	if ( !ok ) {
		r2gl.GetProgramiv( program, GL_INFO_LOG_LENGTH, &logLength );
		if ( logLength > (int)sizeof( log ) ) {
			logLength = sizeof( log );
		}
		log[0] = '\0';
		r2gl.GetProgramInfoLog( program, logLength, NULL, log );
		r2_ri.Printf( PRINT_WARNING, "Renderer2: model program link failed: %s\n", log );
		r2gl.DeleteProgram( program );
		return qfalse;
	}

	r2gl.GenVertexArrays( 1, &r2.modelVao );
	r2gl.GenBuffers( 1, &r2.modelVbo );
	r2gl.BindVertexArray( r2.modelVao );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, r2.modelVbo );
	r2gl.BufferData( GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW );
	r2gl.VertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)0 );
	r2gl.EnableVertexAttribArray( 0 );
	r2gl.VertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 3 ) );
	r2gl.EnableVertexAttribArray( 1 );
	r2gl.VertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 5 ) );
	r2gl.EnableVertexAttribArray( 2 );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	r2gl.BindVertexArray( 0 );

	r2.modelProgram = program;
	r2.modelSamplerLoc = r2gl.GetUniformLocation( r2.modelProgram, "u_texture" );
	r2.modelMvpLoc = r2gl.GetUniformLocation( r2.modelProgram, "u_mvp" );
	r2.modelAlphaTestLoc = r2gl.GetUniformLocation( r2.modelProgram, "u_alphaTest" );
	if ( r2.modelSamplerLoc >= 0 ) {
		r2gl.UseProgram( r2.modelProgram );
		r2gl.Uniform1i( r2.modelSamplerLoc, 0 );
		if ( r2.modelAlphaTestLoc >= 0 ) {
			r2gl.Uniform1i( r2.modelAlphaTestLoc, R2_ALPHA_NONE );
		}
		r2gl.UseProgram( 0 );
	}
	return qtrue;
}

static qboolean R2_GL_CreateWorldPipeline( void ) {
	static const char *vertexShaderSource =
		"#version 330 core\n"
		"layout(location = 0) in vec3 a_pos;\n"
		"layout(location = 1) in vec2 a_tex;\n"
		"layout(location = 2) in vec4 a_color;\n"
	"layout(location = 3) in vec2 a_lightmap;\n"
	"uniform mat4 u_mvp;\n"
	"uniform vec2 u_tcScale;\n"
	"uniform vec2 u_tcOffset;\n"
	"uniform vec2 u_blendTcScale;\n"
	"uniform vec2 u_blendTcOffset;\n"
	"uniform vec2 u_filterTcScale;\n"
	"uniform vec2 u_filterTcOffset;\n"
	"out vec2 v_tex;\n"
	"out vec2 v_blendTex;\n"
	"out vec2 v_filterTex;\n"
		"out vec2 v_lightmap;\n"
		"out vec4 v_color;\n"
	"void main() {\n"
	"    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
	"    v_tex = a_tex * u_tcScale + u_tcOffset;\n"
	"    v_blendTex = a_tex * u_blendTcScale + u_blendTcOffset;\n"
	"    v_filterTex = a_tex * u_filterTcScale + u_filterTcOffset;\n"
	"    v_lightmap = a_lightmap;\n"
		"    v_color = a_color;\n"
		"}\n";
	static const char *fragmentShaderSource =
		"#version 330 core\n"
		"uniform sampler2D u_texture;\n"
		"uniform sampler2D u_lightmap;\n"
		"uniform sampler2D u_blendTexture;\n"
		"uniform sampler2D u_filterTexture;\n"
		"uniform int u_hasLightmap;\n"
		"uniform int u_hasBlendStage;\n"
		"uniform int u_hasFilterStage;\n"
		"uniform int u_isFilterPass;\n"
		"uniform int u_tcSource;\n"
		"uniform int u_rgbVertex;\n"
	"uniform int u_blendRgbVertex;\n"
	"uniform int u_alphaGen;\n"
	"uniform int u_blendAlphaGen;\n"
	"uniform int u_alphaTest;\n"
	"uniform int u_terrainDebugBlend;\n"
	"uniform float u_filterStrength;\n"
		"in vec2 v_tex;\n"
		"in vec2 v_blendTex;\n"
		"in vec2 v_filterTex;\n"
		"in vec2 v_lightmap;\n"
		"in vec4 v_color;\n"
		"out vec4 out_color;\n"
	"void main() {\n"
	"    vec2 sampleTc = (u_tcSource == 1) ? v_lightmap : v_tex;\n"
	"    vec4 color = texture(u_texture, sampleTc);\n"
	"    if (u_isFilterPass != 0) color.rgb = mix(vec3(1.0), color.rgb, clamp(u_filterStrength, 0.0, 1.0));\n"
		"    if (u_hasLightmap != 0) {\n"
		"        color.rgb *= texture(u_lightmap, v_lightmap).rgb;\n"
		"    }\n"
		"    if (u_rgbVertex != 0) color.rgb *= v_color.rgb;\n"
		"    if (u_terrainDebugBlend == 4) {\n"
		"        if (u_hasBlendStage != 0) out_color = vec4(1.0, v_color.a, 0.0, 1.0);\n"
		"        else if (u_hasFilterStage != 0) out_color = vec4(0.0, 0.25, 1.0, 1.0);\n"
		"        else out_color = vec4(0.1, 0.1, 0.1, 1.0);\n"
		"        return;\n"
		"    }\n"
		"    if (u_hasBlendStage != 0) {\n"
		"        vec4 blendColor = texture(u_blendTexture, v_blendTex);\n"
		"        if (u_hasLightmap != 0) blendColor.rgb *= texture(u_lightmap, v_lightmap).rgb;\n"
		"        if (u_blendRgbVertex != 0) blendColor.rgb *= v_color.rgb;\n"
		"        float blendAlpha = blendColor.a;\n"
		"        if (u_blendAlphaGen == 1) blendAlpha *= v_color.a;\n"
		"        if (u_blendAlphaGen == 2) blendAlpha *= (1.0 - v_color.a);\n"
		"        blendAlpha = clamp(blendAlpha, 0.0, 1.0);\n"
		"        if (u_terrainDebugBlend == 1) { out_color = vec4(vec3(blendAlpha), 1.0); return; }\n"
		"        if (u_terrainDebugBlend == 2) blendAlpha = 0.5;\n"
		"        if (u_terrainDebugBlend == 3) { out_color = vec4(1.0, blendAlpha, 0.0, 1.0); return; }\n"
		"        color.rgb = blendColor.rgb * blendAlpha + color.rgb * (1.0 - blendAlpha);\n"
		"        color.a = 1.0;\n"
		"    }\n"
		"    if (u_hasFilterStage != 0) {\n"
		"        vec3 filterColor = texture(u_filterTexture, v_filterTex).rgb;\n"
		"        color.rgb *= mix(vec3(1.0), filterColor, clamp(u_filterStrength, 0.0, 1.0));\n"
		"    }\n"
		"    if (u_alphaGen == 1) color.a *= v_color.a;\n"
		"    if (u_alphaGen == 2) color.a *= (1.0 - v_color.a);\n"
		"    if (u_alphaTest == 1 && color.a <= 0.0) discard;\n"
		"    if (u_alphaTest == 2 && color.a >= 0.5) discard;\n"
		"    if (u_alphaTest == 3 && color.a < 0.5) discard;\n"
		"    out_color = color;\n"
		"}\n";
	GLuint vs = 0;
	GLuint fs = 0;
	GLuint program = 0;
	GLint ok = 0;
	GLint logLength = 0;
	char log[2048];

	if ( !R2_GL_CompileShader( GL_VERTEX_SHADER, vertexShaderSource, &vs ) ) {
		return qfalse;
	}
	if ( !R2_GL_CompileShader( GL_FRAGMENT_SHADER, fragmentShaderSource, &fs ) ) {
		r2gl.DeleteShader( vs );
		return qfalse;
	}

	program = r2gl.CreateProgram();
	r2gl.AttachShader( program, vs );
	r2gl.AttachShader( program, fs );
	r2gl.LinkProgram( program );
	r2gl.GetProgramiv( program, GL_LINK_STATUS, &ok );

	r2gl.DeleteShader( vs );
	r2gl.DeleteShader( fs );

	if ( !ok ) {
		r2gl.GetProgramiv( program, GL_INFO_LOG_LENGTH, &logLength );
		if ( logLength > (int)sizeof( log ) ) {
			logLength = sizeof( log );
		}
		log[0] = '\0';
		r2gl.GetProgramInfoLog( program, logLength, NULL, log );
		r2_ri.Printf( PRINT_WARNING, "Renderer2: world program link failed: %s\n", log );
		r2gl.DeleteProgram( program );
		return qfalse;
	}

	r2.worldProgram = program;
	r2.worldSamplerLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_texture" );
	r2.worldLightmapSamplerLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_lightmap" );
	r2.worldBlendSamplerLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_blendTexture" );
	r2.worldFilterSamplerLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_filterTexture" );
	r2.worldHasLightmapLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_hasLightmap" );
	r2.worldHasBlendStageLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_hasBlendStage" );
	r2.worldHasFilterStageLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_hasFilterStage" );
	r2.worldIsFilterPassLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_isFilterPass" );
	r2.worldTcSourceLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_tcSource" );
	r2.worldTcScaleLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_tcScale" );
	r2.worldTcOffsetLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_tcOffset" );
	r2.worldBlendTcScaleLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_blendTcScale" );
	r2.worldBlendTcOffsetLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_blendTcOffset" );
	r2.worldFilterTcScaleLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_filterTcScale" );
	r2.worldFilterTcOffsetLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_filterTcOffset" );
	r2.worldFilterStrengthLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_filterStrength" );
	r2.worldRgbVertexLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_rgbVertex" );
	r2.worldAlphaGenLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_alphaGen" );
	r2.worldBlendRgbVertexLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_blendRgbVertex" );
	r2.worldBlendAlphaGenLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_blendAlphaGen" );
	r2.worldAlphaTestLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_alphaTest" );
	r2.worldTerrainDebugBlendLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_terrainDebugBlend" );
	r2.worldMvpLoc = r2gl.GetUniformLocation( r2.worldProgram, "u_mvp" );
	r2gl.UseProgram( r2.worldProgram );
	if ( r2.worldSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldSamplerLoc, 0 );
	}
	if ( r2.worldLightmapSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldLightmapSamplerLoc, 1 );
	}
	if ( r2.worldBlendSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldBlendSamplerLoc, 2 );
	}
	if ( r2.worldFilterSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldFilterSamplerLoc, 3 );
	}
	if ( r2.worldTcScaleLoc >= 0 ) {
		r2gl.Uniform2f( r2.worldTcScaleLoc, 1.0f, 1.0f );
	}
	if ( r2.worldTcOffsetLoc >= 0 ) {
		r2gl.Uniform2f( r2.worldTcOffsetLoc, 0.0f, 0.0f );
	}
	if ( r2.worldBlendTcScaleLoc >= 0 ) {
		r2gl.Uniform2f( r2.worldBlendTcScaleLoc, 1.0f, 1.0f );
	}
	if ( r2.worldBlendTcOffsetLoc >= 0 ) {
		r2gl.Uniform2f( r2.worldBlendTcOffsetLoc, 0.0f, 0.0f );
	}
	if ( r2.worldFilterTcScaleLoc >= 0 ) {
		r2gl.Uniform2f( r2.worldFilterTcScaleLoc, 1.0f, 1.0f );
	}
	if ( r2.worldFilterTcOffsetLoc >= 0 ) {
		r2gl.Uniform2f( r2.worldFilterTcOffsetLoc, 0.0f, 0.0f );
	}
	if ( r2.worldHasBlendStageLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldHasBlendStageLoc, 0 );
	}
	if ( r2.worldHasFilterStageLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldHasFilterStageLoc, 0 );
	}
	if ( r2.worldIsFilterPassLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldIsFilterPassLoc, 0 );
	}
	if ( r2.worldFilterStrengthLoc >= 0 ) {
		r2gl.Uniform1f( r2.worldFilterStrengthLoc, 1.0f );
	}
	if ( r2.worldTerrainDebugBlendLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldTerrainDebugBlendLoc, 0 );
	}
	r2gl.UseProgram( 0 );
	return qtrue;
}

static void R2_GL_DestroyPipeline( void ) {
	if ( r2.worldProgram ) {
		r2gl.DeleteProgram( r2.worldProgram );
		r2.worldProgram = 0;
	}
	if ( r2.modelVbo ) {
		r2gl.DeleteBuffers( 1, &r2.modelVbo );
		r2.modelVbo = 0;
	}
	r2.modelVboCapacityBytes = 0;
	if ( r2.modelVao ) {
		r2gl.DeleteVertexArrays( 1, &r2.modelVao );
		r2.modelVao = 0;
	}
	if ( r2.modelProgram ) {
		r2gl.DeleteProgram( r2.modelProgram );
		r2.modelProgram = 0;
	}
	if ( r2.colorVbo ) {
		r2gl.DeleteBuffers( 1, &r2.colorVbo );
		r2.colorVbo = 0;
	}
	if ( r2.colorVao ) {
		r2gl.DeleteVertexArrays( 1, &r2.colorVao );
		r2.colorVao = 0;
	}
	if ( r2.colorProgram ) {
		r2gl.DeleteProgram( r2.colorProgram );
		r2.colorProgram = 0;
	}
	r2.pipelineReady = qfalse;
}

static void R2_GL_DeleteTextures( void ) {
	int i;

	for ( i = 1; i < r2_numTextures; ++i ) {
		if ( r2_textures[i].texnum && r2_textures[i].texnum != r2.whiteTexture ) {
			glDeleteTextures( 1, &r2_textures[i].texnum );
		}
	}
	if ( r2.whiteTexture ) {
		glDeleteTextures( 1, &r2.whiteTexture );
		r2.whiteTexture = 0;
	}
	for ( i = 0; i < R2_MAX_RAW_TEXTURES; ++i ) {
		if ( r2_rawTextures[i].texnum ) {
			glDeleteTextures( 1, &r2_rawTextures[i].texnum );
		}
	}

	memset( r2_textures, 0, sizeof( r2_textures ) );
	memset( r2_rawTextures, 0, sizeof( r2_rawTextures ) );
	r2_numTextures = 0;
}

static qboolean R2_GL_CreateTextureRGBA( const char *name, const byte *rgba, int width, int height, qboolean noMip, GLuint *texnumOut ) {
	GLuint texnum = 0;
	GLint wrapMode;

	if ( !rgba || width <= 0 || height <= 0 || !texnumOut ) {
		return qfalse;
	}

	glGenTextures( 1, &texnum );
	if ( !texnum ) {
		return qfalse;
	}

	glBindTexture( GL_TEXTURE_2D, texnum );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	wrapMode = noMip ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, noMip ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
	if ( !noMip ) {
		r2gl.GenerateMipmap( GL_TEXTURE_2D );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );

	*texnumOut = texnum;
	if ( r2_logMissingTextures && r2_logMissingTextures->integer > 1 ) {
		r2_ri.Printf( PRINT_ALL, "Renderer2: texture %s uploaded %dx%d\n", name ? name : "<memory>", width, height );
	}
	return qtrue;
}

static qboolean R2_GL_ImageHasAlpha( const byte *rgba, int width, int height ) {
	int i;
	int pixels;

	if ( !rgba || width <= 0 || height <= 0 ) {
		return qfalse;
	}

	pixels = width * height;
	for ( i = 0; i < pixels; ++i ) {
		if ( rgba[i * 4 + 3] < 255 ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void R2_GL_TextureSetNoMipFilter( GLuint texnum ) {
	if ( !texnum || texnum == r2.whiteTexture ) {
		return;
	}
	glBindTexture( GL_TEXTURE_2D, texnum );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

static qboolean R2_GL_CreateBuiltInTextures( void ) {
	static const byte whitePixel[4] = { 255, 255, 255, 255 };

	memset( r2_textures, 0, sizeof( r2_textures ) );
	r2_numTextures = 1;

	if ( !R2_GL_CreateTextureRGBA( "*white", whitePixel, 1, 1, qtrue, &r2.whiteTexture ) ) {
		return qfalse;
	}

	Q_strncpyz( r2_textures[0].name, "*white", sizeof( r2_textures[0].name ) );
	r2_textures[0].texnum = r2.whiteTexture;
	r2_textures[0].width = 1;
	r2_textures[0].height = 1;
	r2_textures[0].valid = qtrue;
	r2_textures[0].noMip = qtrue;
	return qtrue;
}

static qboolean R2_GL_LoadTGA( const char *name, byte **pic, int *width, int *height ) {
	byte *buffer = NULL;
	const byte *buf;
	byte *rgba;
	byte *pixbuf;
	int len;
	int columns;
	int rows;
	int row;
	int column;
	int pixelSize;
	int imageType;
	int idLength;
	int colorMapType;
	int numPixels;

	if ( pic ) {
		*pic = NULL;
	}
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	len = r2_ri.FS_ReadFile( name, (void **)&buffer );
	if ( len < 18 || !buffer ) {
		if ( buffer ) {
			r2_ri.FS_FreeFile( buffer );
		}
		return qfalse;
	}

	idLength = buffer[0];
	colorMapType = buffer[1];
	imageType = buffer[2];
	columns = buffer[12] | ( buffer[13] << 8 );
	rows = buffer[14] | ( buffer[15] << 8 );
	pixelSize = buffer[16];

	if ( colorMapType != 0 ||
		 ( imageType != 2 && imageType != 3 && imageType != 10 ) ||
		 ( pixelSize != 8 && pixelSize != 24 && pixelSize != 32 ) ||
		 columns <= 0 || rows <= 0 ) {
		r2_ri.FS_FreeFile( buffer );
		return qfalse;
	}

	numPixels = columns * rows;
	rgba = (byte *)malloc( numPixels * 4 );
	if ( !rgba ) {
		r2_ri.FS_FreeFile( buffer );
		return qfalse;
	}

	buf = buffer + 18 + idLength;
	if ( imageType == 2 || imageType == 3 ) {
		for ( row = rows - 1; row >= 0; --row ) {
			pixbuf = rgba + row * columns * 4;
			for ( column = 0; column < columns; ++column ) {
				byte red;
				byte green;
				byte blue;
				byte alpha = 255;

				if ( pixelSize == 8 ) {
					blue = *buf++;
					green = blue;
					red = blue;
				} else {
					blue = *buf++;
					green = *buf++;
					red = *buf++;
					if ( pixelSize == 32 ) {
						alpha = *buf++;
					}
				}

				*pixbuf++ = red;
				*pixbuf++ = green;
				*pixbuf++ = blue;
				*pixbuf++ = alpha;
			}
		}
	} else {
		byte red = 0;
		byte green = 0;
		byte blue = 0;
		byte alpha = 255;

		for ( row = rows - 1; row >= 0; --row ) {
			pixbuf = rgba + row * columns * 4;
			for ( column = 0; column < columns; ) {
				byte packetHeader = *buf++;
				int packetSize = 1 + ( packetHeader & 0x7f );
				int i;

				if ( packetHeader & 0x80 ) {
					blue = *buf++;
					green = *buf++;
					red = *buf++;
					alpha = pixelSize == 32 ? *buf++ : 255;

					for ( i = 0; i < packetSize; ++i ) {
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alpha;
						++column;
						if ( column == columns && i + 1 < packetSize ) {
							column = 0;
							if ( row > 0 ) {
								--row;
								pixbuf = rgba + row * columns * 4;
							}
						}
					}
				} else {
					for ( i = 0; i < packetSize; ++i ) {
						blue = *buf++;
						green = *buf++;
						red = *buf++;
						alpha = pixelSize == 32 ? *buf++ : 255;
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alpha;
						++column;
						if ( column == columns && i + 1 < packetSize ) {
							column = 0;
							if ( row > 0 ) {
								--row;
								pixbuf = rgba + row * columns * 4;
							}
						}
					}
				}
			}
		}
	}

	r2_ri.FS_FreeFile( buffer );
	*pic = rgba;
	*width = columns;
	*height = rows;
	return qtrue;
}

static qboolean R2_GL_LoadJPG( const char *name, byte **pic, int *width, int *height ) {
	struct jpeg_decompress_struct cinfo;
	r2JpegError_t jerr;
	JSAMPARRAY buffer;
	byte *fileBuffer = NULL;
	byte *rgba = NULL;
	byte *scanline = NULL;
	int rowStride;
	int len;
	qboolean jpegCreated = qfalse;

	if ( pic ) {
		*pic = NULL;
	}
	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	len = r2_ri.FS_ReadFile( name, (void **)&fileBuffer );
	if ( len <= 0 || !fileBuffer ) {
		return qfalse;
	}

	memset( &cinfo, 0, sizeof( cinfo ) );
	memset( &jerr, 0, sizeof( jerr ) );
	cinfo.err = jpeg_std_error( &jerr.pub );
	jerr.pub.error_exit = R2_GL_JpegErrorExit;
	if ( setjmp( jerr.setjmpBuffer ) ) {
		if ( scanline ) {
			free( scanline );
		}
		if ( rgba ) {
			free( rgba );
		}
		if ( jpegCreated ) {
			jpeg_destroy_decompress( &cinfo );
		}
		r2_ri.FS_FreeFile( fileBuffer );
		if ( r2_logMissingTextures && r2_logMissingTextures->integer ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: JPG load failed for '%s': %s\n",
						  name, jerr.message[0] ? jerr.message : "unknown jpeg error" );
		}
		return qfalse;
	}

	jpeg_create_decompress( &cinfo );
	jpegCreated = qtrue;
	R2_GL_JpegMemorySrc( &cinfo, fileBuffer, len );
	jpeg_read_header( &cinfo, TRUE );
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress( &cinfo );

	if ( cinfo.output_components < 3 ) {
		jpeg_destroy_decompress( &cinfo );
		r2_ri.FS_FreeFile( fileBuffer );
		return qfalse;
	}

	if ( cinfo.output_width <= 0 || cinfo.output_height <= 0 ||
		 cinfo.output_width > 8192 || cinfo.output_height > 8192 ) {
		jpeg_destroy_decompress( &cinfo );
		r2_ri.FS_FreeFile( fileBuffer );
		return qfalse;
	}

	*width = cinfo.output_width;
	*height = cinfo.output_height;
	rgba = (byte *)malloc( cinfo.output_width * cinfo.output_height * 4 );
	if ( !rgba ) {
		jpeg_destroy_decompress( &cinfo );
		r2_ri.FS_FreeFile( fileBuffer );
		return qfalse;
	}

	rowStride = cinfo.output_width * cinfo.output_components;
	scanline = (byte *)malloc( rowStride );
	if ( !scanline ) {
		free( rgba );
		jpeg_destroy_decompress( &cinfo );
		r2_ri.FS_FreeFile( fileBuffer );
		return qfalse;
	}

	while ( cinfo.output_scanline < cinfo.output_height ) {
		byte *dst;
		unsigned int x;
		unsigned int y = cinfo.output_scanline;

		buffer = &scanline;
		jpeg_read_scanlines( &cinfo, buffer, 1 );
		dst = rgba + y * cinfo.output_width * 4;
		for ( x = 0; x < cinfo.output_width; ++x ) {
			if ( cinfo.output_components == 1 ) {
				dst[x * 4 + 0] = scanline[x];
				dst[x * 4 + 1] = scanline[x];
				dst[x * 4 + 2] = scanline[x];
			} else {
				dst[x * 4 + 0] = scanline[x * cinfo.output_components + 0];
				dst[x * 4 + 1] = scanline[x * cinfo.output_components + 1];
				dst[x * 4 + 2] = scanline[x * cinfo.output_components + 2];
			}
			dst[x * 4 + 3] = 255;
		}
	}

	free( scanline );
	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );
	r2_ri.FS_FreeFile( fileBuffer );

	*pic = rgba;
	return qtrue;
}

static qboolean R2_GL_TokenLooksLikeImage( const char *token ) {
	if ( !token || !token[0] || token[0] == '$' || token[0] == '*' ) {
		return qfalse;
	}
	if ( !Q_stricmp( token, "clampmap" ) ||
		 !Q_stricmp( token, "map" ) ||
		 !Q_stricmp( token, "map16" ) ||
		 !Q_stricmp( token, "map32" ) ||
		 !Q_stricmp( token, "mapcomp" ) ||
		 !Q_stricmp( token, "mapnocomp" ) ||
		 !Q_stricmp( token, "animMap" ) ||
		 !Q_stricmp( token, "animmapcomp" ) ||
		 !Q_stricmp( token, "animmapnocomp" ) ) {
		return qfalse;
	}
	return qtrue;
}

static qboolean R2_GL_TokenIsMapKeyword( const char *token ) {
	return token &&
		   ( !Q_stricmp( token, "map" ) ||
			 !Q_stricmp( token, "clampmap" ) ||
			 !Q_stricmp( token, "map16" ) ||
			 !Q_stricmp( token, "map32" ) ||
			 !Q_stricmp( token, "mapcomp" ) ||
			 !Q_stricmp( token, "mapnocomp" ) );
}

static qboolean R2_GL_TokenIsAnimMapKeyword( const char *token ) {
	return token &&
		   ( !Q_stricmp( token, "animMap" ) ||
			 !Q_stricmp( token, "animmapcomp" ) ||
			 !Q_stricmp( token, "animmapnocomp" ) );
}

static qboolean R2_GL_ShouldUseColorDepthMap( const char *token ) {
	if ( !Q_stricmp( token, "map16" ) ) {
		return r2.glConfig.colorBits <= 16;
	}
	if ( !Q_stricmp( token, "map32" ) ) {
		return r2.glConfig.colorBits > 16;
	}
	return qtrue;
}

static qboolean R2_GL_ShouldReplaceStageImage( const char *token, const r2ShaderStage_t *stage ) {
	if ( !token || !stage ) {
		return qfalse;
	}
	if ( !Q_stricmp( token, "map16" ) || !Q_stricmp( token, "map32" ) ) {
		return R2_GL_ShouldUseColorDepthMap( token );
	}
	if ( !Q_stricmp( token, "mapcomp" ) || !Q_stricmp( token, "animmapcomp" ) ) {
		return !stage->image[0];
	}
	if ( !Q_stricmp( token, "mapnocomp" ) || !Q_stricmp( token, "animmapnocomp" ) ) {
		return qtrue;
	}
	return qtrue;
}

static void R2_GL_AddShaderAlias( const char *name, const char *image,
								  float scrollS, float scrollT, qboolean hasScroll,
								  float scaleS, float scaleT, qboolean hasScale ) {
	char normalizedName[MAX_QPATH];
	char normalizedImage[MAX_QPATH];
	int i;

	if ( !R2_GL_TokenLooksLikeImage( name ) || !R2_GL_TokenLooksLikeImage( image ) ) {
		return;
	}
	R2_GL_StripImageExtension( name, normalizedName, sizeof( normalizedName ) );
	R2_GL_NormalizeAssetName( image, normalizedImage, sizeof( normalizedImage ) );

	for ( i = 0; i < r2_numShaderAliases; ++i ) {
		if ( !Q_stricmp( r2_shaderAliases[i].name, normalizedName ) ) {
			Q_strncpyz( r2_shaderAliases[i].image, normalizedImage, sizeof( r2_shaderAliases[i].image ) );
			r2_shaderAliases[i].scrollS = scrollS;
			r2_shaderAliases[i].scrollT = scrollT;
			r2_shaderAliases[i].hasScroll = hasScroll;
			r2_shaderAliases[i].scaleS = scaleS;
			r2_shaderAliases[i].scaleT = scaleT;
			r2_shaderAliases[i].hasScale = hasScale;
			return;
		}
	}

	if ( r2_numShaderAliases >= R2_MAX_SHADER_ALIASES ) {
		return;
	}

	Q_strncpyz( r2_shaderAliases[r2_numShaderAliases].name, normalizedName, sizeof( r2_shaderAliases[r2_numShaderAliases].name ) );
	Q_strncpyz( r2_shaderAliases[r2_numShaderAliases].image, normalizedImage, sizeof( r2_shaderAliases[r2_numShaderAliases].image ) );
	r2_shaderAliases[r2_numShaderAliases].scrollS = scrollS;
	r2_shaderAliases[r2_numShaderAliases].scrollT = scrollT;
	r2_shaderAliases[r2_numShaderAliases].hasScroll = hasScroll;
	r2_shaderAliases[r2_numShaderAliases].scaleS = scaleS;
	r2_shaderAliases[r2_numShaderAliases].scaleT = scaleT;
	r2_shaderAliases[r2_numShaderAliases].hasScale = hasScale;
	++r2_numShaderAliases;
}

static r2ShaderDef_t *R2_GL_FindShaderDef( const char *name ) {
	char normalizedName[MAX_QPATH];
	int i;

	if ( !name || !name[0] ) {
		return NULL;
	}

	R2_GL_StripImageExtension( name, normalizedName, sizeof( normalizedName ) );
	for ( i = 0; i < r2_numShaderDefs; ++i ) {
		if ( !Q_stricmp( r2_shaderDefs[i].name, normalizedName ) ) {
			return &r2_shaderDefs[i];
		}
	}
	return NULL;
}

static r2ShaderDef_t *R2_GL_AddShaderDef( const char *name ) {
	char normalizedName[MAX_QPATH];
	r2ShaderDef_t *def;

	if ( !name || !name[0] ) {
		return NULL;
	}

	R2_GL_StripImageExtension( name, normalizedName, sizeof( normalizedName ) );
	def = R2_GL_FindShaderDef( normalizedName );
	if ( def ) {
		memset( def, 0, sizeof( *def ) );
		Q_strncpyz( def->name, normalizedName, sizeof( def->name ) );
		return def;
	}

	if ( r2_numShaderDefs >= R2_MAX_SHADER_DEFS ) {
		return NULL;
	}

	def = &r2_shaderDefs[r2_numShaderDefs++];
	memset( def, 0, sizeof( *def ) );
	Q_strncpyz( def->name, normalizedName, sizeof( def->name ) );
	return def;
}

static const r2ShaderAlias_t *R2_GL_FindShaderAlias( const char *name ) {
	char normalizedName[MAX_QPATH];
	int i;

	if ( !name || !name[0] ) {
		return NULL;
	}
	R2_GL_StripImageExtension( name, normalizedName, sizeof( normalizedName ) );
	for ( i = 0; i < r2_numShaderAliases; ++i ) {
		if ( !Q_stricmp( r2_shaderAliases[i].name, normalizedName ) ) {
			return &r2_shaderAliases[i];
		}
	}
	return NULL;
}

static GLenum R2_GL_BlendFactorToGL( r2BlendFactor_t factor ) {
	switch ( factor ) {
	case R2_BLEND_ZERO:
		return GL_ZERO;
	case R2_BLEND_ONE:
		return GL_ONE;
	case R2_BLEND_SRC_ALPHA:
		return GL_SRC_ALPHA;
	case R2_BLEND_ONE_MINUS_SRC_ALPHA:
		return GL_ONE_MINUS_SRC_ALPHA;
	case R2_BLEND_DST_COLOR:
		return GL_DST_COLOR;
	case R2_BLEND_ONE_MINUS_DST_COLOR:
		return GL_ONE_MINUS_DST_COLOR;
	case R2_BLEND_SRC_COLOR:
		return GL_SRC_COLOR;
	case R2_BLEND_ONE_MINUS_SRC_COLOR:
		return GL_ONE_MINUS_SRC_COLOR;
	case R2_BLEND_DST_ALPHA:
		return GL_DST_ALPHA;
	case R2_BLEND_ONE_MINUS_DST_ALPHA:
		return GL_ONE_MINUS_DST_ALPHA;
	default:
		return GL_ONE;
	}
}

static r2BlendFactor_t R2_GL_ParseBlendToken( const char *token ) {
	if ( !token || !token[0] ) {
		return R2_BLEND_ONE;
	}
	if ( !Q_stricmp( token, "GL_ZERO" ) || !Q_stricmp( token, "zero" ) ) {
		return R2_BLEND_ZERO;
	}
	if ( !Q_stricmp( token, "GL_ONE" ) || !Q_stricmp( token, "one" ) ) {
		return R2_BLEND_ONE;
	}
	if ( !Q_stricmp( token, "GL_SRC_ALPHA" ) ) {
		return R2_BLEND_SRC_ALPHA;
	}
	if ( !Q_stricmp( token, "GL_ONE_MINUS_SRC_ALPHA" ) ) {
		return R2_BLEND_ONE_MINUS_SRC_ALPHA;
	}
	if ( !Q_stricmp( token, "GL_DST_COLOR" ) ) {
		return R2_BLEND_DST_COLOR;
	}
	if ( !Q_stricmp( token, "GL_ONE_MINUS_DST_COLOR" ) ) {
		return R2_BLEND_ONE_MINUS_DST_COLOR;
	}
	if ( !Q_stricmp( token, "GL_SRC_COLOR" ) ) {
		return R2_BLEND_SRC_COLOR;
	}
	if ( !Q_stricmp( token, "GL_ONE_MINUS_SRC_COLOR" ) ) {
		return R2_BLEND_ONE_MINUS_SRC_COLOR;
	}
	if ( !Q_stricmp( token, "GL_DST_ALPHA" ) ) {
		return R2_BLEND_DST_ALPHA;
	}
	if ( !Q_stricmp( token, "GL_ONE_MINUS_DST_ALPHA" ) ) {
		return R2_BLEND_ONE_MINUS_DST_ALPHA;
	}
	return R2_BLEND_ONE;
}

static void R2_GL_SetStageBlend( r2ShaderStage_t *stage, const char *first, const char *second ) {
	if ( !stage || !first || !first[0] ) {
		return;
	}
	stage->blend = qtrue;
	if ( !Q_stricmp( first, "filter" ) ) {
		stage->blendSrc = R2_BLEND_DST_COLOR;
		stage->blendDst = R2_BLEND_ZERO;
		return;
	}
	if ( !Q_stricmp( first, "blend" ) ) {
		stage->blendSrc = R2_BLEND_SRC_ALPHA;
		stage->blendDst = R2_BLEND_ONE_MINUS_SRC_ALPHA;
		return;
	}
	if ( !Q_stricmp( first, "add" ) ) {
		stage->blendSrc = R2_BLEND_ONE;
		stage->blendDst = R2_BLEND_ONE;
		return;
	}
	stage->blendSrc = R2_GL_ParseBlendToken( first );
	stage->blendDst = R2_GL_ParseBlendToken( second );
}

static qboolean R2_GL_IsOpaqueReplaceBlend( const r2ShaderStage_t *stage ) {
	return stage && stage->blend &&
		   stage->blendSrc == R2_BLEND_ONE &&
		   stage->blendDst == R2_BLEND_ZERO;
}

static qboolean R2_GL_StageUsesBlend( const r2ShaderStage_t *stage ) {
	return stage && stage->blend && !R2_GL_IsOpaqueReplaceBlend( stage );
}

static void R2_GL_SetStageDepthBlendState( const r2ShaderStage_t *stage,
										   qboolean forceOpaque ) {
	qboolean blend = !forceOpaque && R2_GL_StageUsesBlend( stage );

	if ( blend ) {
		glEnable( GL_BLEND );
		glBlendFunc( R2_GL_BlendFactorToGL( stage->blendSrc ), R2_GL_BlendFactorToGL( stage->blendDst ) );
	} else {
		glDisable( GL_BLEND );
	}

	glDepthMask( ( !blend || ( stage && stage->depthWrite ) ) ? GL_TRUE : GL_FALSE );
	glDepthFunc( ( stage && stage->depthFuncEqual ) ? GL_EQUAL : GL_LEQUAL );
}

static void R2_GL_SetCullMode( r2CullMode_t cullMode ) {
	if ( cullMode == R2_CULL_TWO_SIDED ) {
		glDisable( GL_CULL_FACE );
		return;
	}

	glEnable( GL_CULL_FACE );
	glCullFace( cullMode == R2_CULL_BACK ? GL_BACK : GL_FRONT );
}

static qboolean R2_GL_IsCloudFilterStage( const r2ShaderStage_t *stage ) {
	if ( !stage || !stage->blend || !stage->image[0] ) {
		return qfalse;
	}
	if ( stage->blendSrc != R2_BLEND_DST_COLOR || stage->blendDst != R2_BLEND_ZERO ) {
		return qfalse;
	}
	return strstr( stage->image, "cloud" ) != NULL || strstr( stage->image, "skies" ) != NULL;
}

static qboolean R2_GL_IsTerrainShader( const r2Texture_t *texture ) {
	if ( !texture || !texture->name[0] ) {
		return qfalse;
	}
	return !Q_stricmpn( texture->name, "textures/terrain/", 17 );
}

static qboolean R2_GL_IsDigitChar( char c ) {
	return c >= '0' && c <= '9';
}

static qboolean R2_GL_TerrainNameLooksLikeTransition( const char *name ) {
	int i;

	if ( !name ) {
		return qfalse;
	}

	for ( i = 1; name[i] && name[i + 2]; ++i ) {
		if ( R2_GL_IsDigitChar( name[i - 1] ) &&
			 ( name[i] == 't' || name[i] == 'T' ) &&
			 ( name[i + 1] == 'o' || name[i + 1] == 'O' ) &&
			 R2_GL_IsDigitChar( name[i + 2] ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean R2_GL_IsTerrainAlphaBlendStage( const r2ShaderStage_t *stage ) {
	if ( !stage || stage->isLightmap || !stage->image[0] || !stage->blend ) {
		return qfalse;
	}

	return stage->blendSrc == R2_BLEND_SRC_ALPHA &&
		   stage->blendDst == R2_BLEND_ONE_MINUS_SRC_ALPHA;
}

static int R2_GL_TerrainBlendStageIndex( const r2Texture_t *texture ) {
	int i;

	if ( !R2_GL_IsTerrainShader( texture ) || texture->stageCount < 2 ) {
		return -1;
	}

	if ( texture->stages[0].isLightmap || texture->stages[0].blend ) {
		return -1;
	}

	for ( i = 1; i < texture->stageCount; ++i ) {
		if ( R2_GL_IsTerrainAlphaBlendStage( &texture->stages[i] ) ) {
			return i;
		}
	}

	if ( R2_GL_TerrainNameLooksLikeTransition( texture->name ) ) {
		for ( i = 1; i < texture->stageCount; ++i ) {
			const r2ShaderStage_t *stage = &texture->stages[i];

			if ( stage->isLightmap || !stage->image[0] || R2_GL_IsCloudFilterStage( stage ) ) {
				continue;
			}
			return i;
		}
	}

	return -1;
}

static qboolean R2_GL_HasTerrainBlendStage( const r2Texture_t *texture ) {
	return R2_GL_TerrainBlendStageIndex( texture ) >= 0;
}

static int R2_GL_TerrainFilterStageIndex( const r2Texture_t *texture ) {
	int i;

	if ( !R2_GL_IsTerrainShader( texture ) ) {
		return -1;
	}
	for ( i = 1; i < texture->stageCount; ++i ) {
		if ( R2_GL_IsCloudFilterStage( &texture->stages[i] ) ) {
			return i;
		}
	}
	return -1;
}

static qboolean R2_GL_ShouldCollapseTerrainStages( const r2Texture_t *texture ) {
	if ( r2_worldTerrainCollapseStages && !r2_worldTerrainCollapseStages->integer ) {
		return qfalse;
	}
	return R2_GL_IsTerrainShader( texture ) &&
		   ( R2_GL_HasTerrainBlendStage( texture ) ||
			 R2_GL_TerrainFilterStageIndex( texture ) >= 0 );
}

static qboolean R2_GL_IsLightmapMultiplyBlend( const r2ShaderStage_t *stage ) {
	if ( !stage || !stage->blend ) {
		return qfalse;
	}
	return ( stage->blendSrc == R2_BLEND_DST_COLOR && stage->blendDst == R2_BLEND_ZERO ) ||
		   ( stage->blendSrc == R2_BLEND_ZERO && stage->blendDst == R2_BLEND_DST_COLOR );
}

static int R2_GL_LightmapMultiplyStageIndex( const r2Texture_t *texture ) {
	if ( !texture || texture->stageCount != 2 ) {
		return -1;
	}
	if ( texture->stages[0].isLightmap &&
		 !texture->stages[1].isLightmap &&
		 R2_GL_IsLightmapMultiplyBlend( &texture->stages[1] ) ) {
		return 1;
	}
	if ( !texture->stages[0].isLightmap &&
		 texture->stages[1].isLightmap &&
		 R2_GL_IsLightmapMultiplyBlend( &texture->stages[1] ) ) {
		return 0;
	}
	return -1;
}

static qboolean R2_GL_ShouldCollapseLightmapMultiplyStages( const r2Texture_t *texture ) {
	return R2_GL_LightmapMultiplyStageIndex( texture ) >= 0;
}

static r2AlphaGenMode_t R2_GL_InvertAlphaGen( r2AlphaGenMode_t alphaGen ) {
	if ( alphaGen == R2_ALPHA_GEN_VERTEX ) {
		return R2_ALPHA_GEN_ONE_MINUS_VERTEX;
	}
	if ( alphaGen == R2_ALPHA_GEN_ONE_MINUS_VERTEX ) {
		return R2_ALPHA_GEN_VERTEX;
	}
	return alphaGen;
}

static r2AlphaGenMode_t R2_GL_EffectiveTerrainBlendAlphaGen( const r2ShaderStage_t *stage ) {
	if ( !stage ) {
		return R2_ALPHA_GEN_IDENTITY;
	}
	if ( stage->alphaGen == R2_ALPHA_GEN_IDENTITY && stage->rgbVertex ) {
		return R2_ALPHA_GEN_VERTEX;
	}
	return stage->alphaGen;
}

static qboolean R2_GL_ShouldInvertTerrainBlendAlpha( const r2Texture_t *texture, int pass ) {
	return r2_worldTerrainInvertAlpha &&
		   r2_worldTerrainInvertAlpha->integer &&
		   pass == R2_GL_TerrainBlendStageIndex( texture );
}

static qboolean R2_GL_ParseShaderStage( char **p, r2ShaderStage_t *stage ) {
	char *token;

	if ( !p || !stage ) {
		return qfalse;
	}

	memset( stage, 0, sizeof( *stage ) );
	stage->blendSrc = R2_BLEND_ONE;
	stage->blendDst = R2_BLEND_ZERO;
	stage->tcGen = R2_TCGEN_TEXTURE;
	stage->scaleS = 1.0f;
	stage->scaleT = 1.0f;

	while ( 1 ) {
		token = COM_ParseExt( p, qtrue );
		if ( !token || !token[0] ) {
			break;
		}
		if ( token[0] == '}' ) {
			break;
		}

		if ( R2_GL_TokenIsMapKeyword( token ) ) {
			char mapKeyword[32];

			Q_strncpyz( mapKeyword, token, sizeof( mapKeyword ) );

			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "$lightmap" ) ) {
				stage->isLightmap = qtrue;
			} else if ( token && ( !Q_stricmp( token, "$whiteimage" ) ||
								   !Q_stricmp( token, "*white" ) ) ) {
				Q_strncpyz( stage->image, "*white", sizeof( stage->image ) );
			} else if ( R2_GL_TokenLooksLikeImage( token ) &&
						R2_GL_ShouldReplaceStageImage( mapKeyword, stage ) ) {
				R2_GL_NormalizeAssetName( token, stage->image, sizeof( stage->image ) );
			}
			continue;
		}
		if ( R2_GL_TokenIsAnimMapKeyword( token ) ) {
			char mapKeyword[32];

			Q_strncpyz( mapKeyword, token, sizeof( mapKeyword ) );

			COM_ParseExt( p, qfalse );
			token = COM_ParseExt( p, qfalse );
			if ( R2_GL_TokenLooksLikeImage( token ) &&
				 R2_GL_ShouldReplaceStageImage( mapKeyword, stage ) ) {
				R2_GL_NormalizeAssetName( token, stage->image, sizeof( stage->image ) );
			}
			continue;
		}
		if ( !Q_stricmp( token, "blendFunc" ) ) {
			char *first = COM_ParseExt( p, qfalse );
			char *second = NULL;
			if ( first && Q_stricmp( first, "filter" ) && Q_stricmp( first, "blend" ) && Q_stricmp( first, "add" ) ) {
				second = COM_ParseExt( p, qfalse );
			}
			R2_GL_SetStageBlend( stage, first, second );
			continue;
		}
		if ( !Q_stricmp( token, "rgbGen" ) ) {
			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "vertex" ) ) {
				stage->rgbVertex = qtrue;
			} else if ( token && !Q_stricmp( token, "exactVertex" ) ) {
				stage->rgbVertex = qtrue;
			}
			continue;
		}
		if ( !Q_stricmp( token, "alphaGen" ) ) {
			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "vertex" ) ) {
				stage->alphaGen = R2_ALPHA_GEN_VERTEX;
			} else if ( token && !Q_stricmp( token, "oneMinusVertex" ) ) {
				stage->alphaGen = R2_ALPHA_GEN_ONE_MINUS_VERTEX;
			} else if ( token && !Q_stricmp( token, "identity" ) ) {
				stage->alphaGen = R2_ALPHA_GEN_IDENTITY;
			} else if ( token && !Q_stricmp( token, "const" ) ) {
				COM_ParseExt( p, qfalse );
			}
			continue;
		}
		if ( !Q_stricmp( token, "tcGen" ) || !Q_stricmp( token, "texgen" ) ) {
			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "lightmap" ) ) {
				stage->tcGen = R2_TCGEN_LIGHTMAP;
			} else {
				stage->tcGen = R2_TCGEN_TEXTURE;
			}
			continue;
		}
		if ( !Q_stricmp( token, "alphaFunc" ) ) {
			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "GT0" ) ) {
				stage->alphaTest = R2_ALPHA_GT0;
			} else if ( token && !Q_stricmp( token, "LT128" ) ) {
				stage->alphaTest = R2_ALPHA_LT128;
			} else if ( token && !Q_stricmp( token, "GE128" ) ) {
				stage->alphaTest = R2_ALPHA_GE128;
			}
			continue;
		}
		if ( !Q_stricmp( token, "depthWrite" ) ) {
			stage->depthWrite = qtrue;
			continue;
		}
		if ( !Q_stricmp( token, "depthFunc" ) ) {
			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "equal" ) ) {
				stage->depthFuncEqual = qtrue;
			} else {
				stage->depthFuncEqual = qfalse;
			}
			continue;
		}
		if ( !Q_stricmp( token, "tcMod" ) ) {
			token = COM_ParseExt( p, qfalse );
			if ( token && !Q_stricmp( token, "scale" ) ) {
				char *sToken = COM_ParseExt( p, qfalse );
				char *tToken = COM_ParseExt( p, qfalse );
				if ( sToken && sToken[0] && tToken && tToken[0] ) {
					stage->scaleS = (float)atof( sToken );
					stage->scaleT = (float)atof( tToken );
					stage->hasScale = qtrue;
				}
			} else if ( token && !Q_stricmp( token, "scroll" ) ) {
				char *sToken = COM_ParseExt( p, qfalse );
				char *tToken = COM_ParseExt( p, qfalse );
				if ( sToken && sToken[0] && tToken && tToken[0] ) {
					stage->scrollS = (float)atof( sToken );
					stage->scrollT = (float)atof( tToken );
					stage->hasScroll = qtrue;
				}
			}
			continue;
		}
	}

	return stage->isLightmap || stage->image[0];
}

static qboolean R2_GL_ParseShaderVector3( char **p, float out[3] ) {
	char *token;
	qboolean hadParen = qfalse;
	int i;

	if ( !p || !out ) {
		return qfalse;
	}

	token = COM_ParseExt( p, qfalse );
	if ( !token || !token[0] ) {
		return qfalse;
	}
	if ( token[0] == '(' ) {
		hadParen = qtrue;
		token = COM_ParseExt( p, qfalse );
	}

	for ( i = 0; i < 3; ++i ) {
		if ( !token || !token[0] ) {
			return qfalse;
		}
		out[i] = (float)atof( token );
		if ( i < 2 ) {
			token = COM_ParseExt( p, qfalse );
		}
	}

	if ( hadParen ) {
		token = COM_ParseExt( p, qfalse );
	}
	return qtrue;
}

static void R2_GL_ParseMapFogVars( char **p, r2ShaderDef_t *def ) {
	float fogColor[3];
	char *token;
	float fogDensity;

	if ( !R2_GL_ParseShaderVector3( p, fogColor ) ) {
		return;
	}

	token = COM_ParseExt( p, qfalse );
	if ( !token || !token[0] ) {
		return;
	}

	fogDensity = (float)atof( token );
	if ( def ) {
		def->hasMapFog = qtrue;
		def->mapFogColor[0] = fogColor[0];
		def->mapFogColor[1] = fogColor[1];
		def->mapFogColor[2] = fogColor[2];
		def->mapFogDensity = fogDensity;
		def->mapFogFar = fogDensity >= 1.0f ? (int)fogDensity : 5;
	}
}

static void R2_GL_ParseSurfaceParm( char **p, r2ShaderDef_t *def ) {
	char *token;

	token = COM_ParseExt( p, qfalse );
	if ( !token || !token[0] || !def ) {
		return;
	}

	if ( !Q_stricmp( token, "sky" ) ) {
		def->isSky = qtrue;
	} else if ( !Q_stricmp( token, "nodraw" ) ||
				!Q_stricmp( token, "skip" ) ) {
		def->noDraw = qtrue;
	} else if ( !Q_stricmp( token, "nolightmap" ) ) {
		def->noLightmap = qtrue;
	}
}

static void R2_GL_ParseShaderFile( const char *filename ) {
	char *text = NULL;
	char *p;
	char shaderName[MAX_QPATH];
	char imageName[MAX_QPATH];
	char editorImageName[MAX_QPATH];
	char *token;

	if ( r2_ri.FS_ReadFile( filename, (void **)&text ) <= 0 || !text ) {
		return;
	}

	p = text;
	while ( 1 ) {
		r2ShaderDef_t *def;

		token = COM_ParseExt( &p, qtrue );
		if ( !token || !token[0] ) {
			break;
		}
		Q_strncpyz( shaderName, token, sizeof( shaderName ) );

		token = COM_ParseExt( &p, qtrue );
		if ( !token || token[0] != '{' ) {
			SkipRestOfLine( &p );
			continue;
		}

		imageName[0] = '\0';
		editorImageName[0] = '\0';
		def = R2_GL_AddShaderDef( shaderName );
		while ( 1 ) {
			token = COM_ParseExt( &p, qtrue );
			if ( !token || !token[0] ) {
				break;
			}
			if ( token[0] == '{' ) {
				r2ShaderStage_t stage;
				if ( R2_GL_ParseShaderStage( &p, &stage ) && def && def->stageCount < R2_MAX_SHADER_STAGES ) {
					def->stages[def->stageCount++] = stage;
					if ( !imageName[0] && !stage.isLightmap && stage.image[0] ) {
						Q_strncpyz( imageName, stage.image, sizeof( imageName ) );
					}
				}
				continue;
			}
			if ( token[0] == '}' ) {
				break;
			}
			if ( !editorImageName[0] && !Q_stricmp( token, "qer_editorimage" ) ) {
				token = COM_ParseExt( &p, qfalse );
				if ( R2_GL_TokenLooksLikeImage( token ) ) {
					Q_strncpyz( editorImageName, token, sizeof( editorImageName ) );
				}
				continue;
			}
			if ( !Q_stricmp( token, "cull" ) ) {
				token = COM_ParseExt( &p, qfalse );
				if ( def && token && token[0] ) {
					if ( !Q_stricmp( token, "none" ) ||
						 !Q_stricmp( token, "twosided" ) ||
						 !Q_stricmp( token, "disable" ) ) {
						def->cullMode = R2_CULL_TWO_SIDED;
					} else if ( !Q_stricmp( token, "back" ) ||
								!Q_stricmp( token, "backside" ) ||
								!Q_stricmp( token, "backsided" ) ) {
						def->cullMode = R2_CULL_BACK;
					}
				}
				continue;
			}
			if ( !Q_stricmp( token, "fogvars" ) ) {
				R2_GL_ParseMapFogVars( &p, def );
				continue;
			}
			if ( !Q_stricmp( token, "nomipmaps" ) ||
				 !Q_stricmp( token, "nomipmap" ) ) {
				if ( def ) {
					def->noMip = qtrue;
				}
				continue;
			}
			if ( !Q_stricmp( token, "surfaceparm" ) ) {
				R2_GL_ParseSurfaceParm( &p, def );
				continue;
			}
			if ( !Q_stricmp( token, "skyparms" ) ) {
				if ( def ) {
					def->isSky = qtrue;
				}
				SkipRestOfLine( &p );
				continue;
			}
		}

		if ( !imageName[0] && editorImageName[0] ) {
			Q_strncpyz( imageName, editorImageName, sizeof( imageName ) );
		}
		if ( imageName[0] ) {
			float scrollS = 0.0f;
			float scrollT = 0.0f;
			qboolean hasScroll = qfalse;
			float scaleS = 1.0f;
			float scaleT = 1.0f;
			qboolean hasScale = qfalse;

			if ( def && def->stageCount > 0 ) {
				scrollS = def->stages[0].scrollS;
				scrollT = def->stages[0].scrollT;
				hasScroll = def->stages[0].hasScroll;
				scaleS = def->stages[0].scaleS;
				scaleT = def->stages[0].scaleT;
				hasScale = def->stages[0].hasScale;
			}
			R2_GL_AddShaderAlias( shaderName, imageName, scrollS, scrollT, hasScroll, scaleS, scaleT, hasScale );
		}
	}

	r2_ri.FS_FreeFile( text );
}

static void R2_GL_LoadShaderAliases( void ) {
	char **shaderFiles;
	int numShaderFiles = 0;
	int i;

	if ( r2_shaderAliasesLoaded ) {
		return;
	}

	r2_shaderAliasesLoaded = qtrue;
	r2_numShaderAliases = 0;
	r2_numShaderDefs = 0;

	shaderFiles = r2_ri.FS_ListFiles( "scripts", ".shader", &numShaderFiles );
	for ( i = 0; shaderFiles && i < numShaderFiles; ++i ) {
		char filename[MAX_QPATH];

		Com_sprintf( filename, sizeof( filename ), "scripts/%s", shaderFiles[i] );
		R2_GL_ParseShaderFile( filename );
	}
	if ( shaderFiles ) {
		r2_ri.FS_FreeFileList( shaderFiles );
	}

	r2_ri.Printf( PRINT_ALL, "Renderer2: loaded %d lightweight shader aliases, %d shader defs\n",
				  r2_numShaderAliases, r2_numShaderDefs );
}

static qboolean R2_GL_LoadImage( const char *name, byte **pic, int *width, int *height, char *resolvedName, int resolvedNameSize ) {
	char normalizedName[MAX_QPATH];
	char candidate[MAX_QPATH];
	int len;

	if ( !name || !name[0] ) {
		return qfalse;
	}

	R2_GL_NormalizeAssetName( name, normalizedName, sizeof( normalizedName ) );
	len = (int)strlen( normalizedName );
	if ( len > 4 && !Q_stricmp( normalizedName + len - 4, ".tga" ) ) {
		if ( R2_GL_LoadTGA( normalizedName, pic, width, height ) ) {
			if ( resolvedName ) {
				Q_strncpyz( resolvedName, normalizedName, resolvedNameSize );
			}
			return qtrue;
		}
		Com_sprintf( candidate, sizeof( candidate ), "%.*s.jpg", len - 4, normalizedName );
		if ( R2_GL_LoadJPG( candidate, pic, width, height ) ) {
			if ( resolvedName ) {
				Q_strncpyz( resolvedName, candidate, resolvedNameSize );
			}
			return qtrue;
		}
		return qfalse;
	}
	if ( len > 4 && !Q_stricmp( normalizedName + len - 4, ".jpg" ) ) {
		if ( R2_GL_LoadJPG( normalizedName, pic, width, height ) ) {
			if ( resolvedName ) {
				Q_strncpyz( resolvedName, normalizedName, resolvedNameSize );
			}
			return qtrue;
		}
		Com_sprintf( candidate, sizeof( candidate ), "%.*s.tga", len - 4, normalizedName );
		if ( R2_GL_LoadTGA( candidate, pic, width, height ) ) {
			if ( resolvedName ) {
				Q_strncpyz( resolvedName, candidate, resolvedNameSize );
			}
			return qtrue;
		}
		return qfalse;
	}

	Com_sprintf( candidate, sizeof( candidate ), "%s.tga", normalizedName );
	if ( R2_GL_LoadTGA( candidate, pic, width, height ) ) {
		if ( resolvedName ) {
			Q_strncpyz( resolvedName, candidate, resolvedNameSize );
		}
		return qtrue;
	}
	Com_sprintf( candidate, sizeof( candidate ), "%s.jpg", normalizedName );
	if ( R2_GL_LoadJPG( candidate, pic, width, height ) ) {
		if ( resolvedName ) {
			Q_strncpyz( resolvedName, candidate, resolvedNameSize );
		}
		return qtrue;
	}

	return qfalse;
}

static int R2_GL_FindTextureHandle( const char *name ) {
	char normalizedName[MAX_QPATH];
	int i;

	if ( !name || !name[0] ) {
		return 0;
	}
	R2_GL_NormalizeAssetName( name, normalizedName, sizeof( normalizedName ) );
	for ( i = 0; i < r2_numTextures; ++i ) {
		if ( !Q_stricmp( r2_textures[i].name, normalizedName ) ) {
			return i;
		}
	}
	return -1;
}

static qhandle_t R2_GL_StoreTexture( const char *name, GLuint texnum, int width, int height,
									 qboolean noMip, qboolean valid, qboolean hasAlpha,
									 const r2ShaderAlias_t *alias ) {
	char normalizedName[MAX_QPATH];
	int handle;

	if ( r2_numTextures >= R2_MAX_TEXTURES ) {
		return 0;
	}

	handle = r2_numTextures++;
	R2_GL_NormalizeAssetName( name, normalizedName, sizeof( normalizedName ) );
	Q_strncpyz( r2_textures[handle].name, normalizedName, sizeof( r2_textures[handle].name ) );
	r2_textures[handle].texnum = texnum;
	r2_textures[handle].width = width;
	r2_textures[handle].height = height;
	r2_textures[handle].noMip = noMip;
	r2_textures[handle].valid = valid;
	r2_textures[handle].hasAlpha = hasAlpha;
	r2_textures[handle].cullMode = R2_CULL_FRONT;
	if ( alias && alias->hasScroll ) {
		r2_textures[handle].scrollS = alias->scrollS;
		r2_textures[handle].scrollT = alias->scrollT;
		r2_textures[handle].hasScroll = qtrue;
	}
	if ( alias && alias->hasScale ) {
		r2_textures[handle].scaleS = alias->scaleS;
		r2_textures[handle].scaleT = alias->scaleT;
		r2_textures[handle].hasScale = qtrue;
	}
	if ( alias && ( alias->hasScroll || alias->hasScale ) ) {
		if ( texnum && texnum != r2.whiteTexture ) {
			glBindTexture( GL_TEXTURE_2D, texnum );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
			glBindTexture( GL_TEXTURE_2D, 0 );
		}
	}
	return handle;
}

static void R2_GL_ApplyShaderDefSideEffects( const r2ShaderDef_t *def ) {
	if ( !def ) {
		return;
	}

	if ( def->hasMapFog ) {
		r2_ri.Cvar_Set( "r_mapFogColor",
						va( "0 %d %f %f %f %f 0",
							def->mapFogFar,
							def->mapFogDensity,
							def->mapFogColor[0],
							def->mapFogColor[1],
							def->mapFogColor[2] ) );
	}
}

static void R2_GL_ApplyShaderDefToTexture( qhandle_t handle, const r2ShaderDef_t *def ) {
	int i;

	if ( !def ) {
		return;
	}

	R2_GL_ApplyShaderDefSideEffects( def );

	if ( handle <= 0 || handle >= r2_numTextures ||
		 r2_textures[handle].shaderDefApplied || r2_textures[handle].applyingShaderDef ) {
		return;
	}

	r2_textures[handle].isSky = def->isSky;
	r2_textures[handle].noDraw = def->noDraw;
	r2_textures[handle].noLightmap = def->noLightmap;
	if ( def->noMip ) {
		r2_textures[handle].noMip = qtrue;
		R2_GL_TextureSetNoMipFilter( r2_textures[handle].texnum );
	}
	if ( def->stageCount <= 0 ) {
		r2_textures[handle].shaderDefApplied = qtrue;
		return;
	}

	r2_textures[handle].applyingShaderDef = qtrue;
	r2_textures[handle].stageCount = 0;
	r2_textures[handle].cullMode = def->cullMode;
	for ( i = 0; i < def->stageCount && r2_textures[handle].stageCount < R2_MAX_SHADER_STAGES; ++i ) {
		r2ShaderStage_t *dst;

		if ( !def->stages[i].isLightmap && !def->stages[i].image[0] ) {
			continue;
		}

		dst = &r2_textures[handle].stages[r2_textures[handle].stageCount++];
		*dst = def->stages[i];
		if ( dst->isLightmap ) {
			dst->imageHandle = 0;
		} else if ( !Q_stricmp( dst->image, "*white" ) ) {
			dst->imageHandle = 0;
		} else if ( !Q_stricmp( dst->image, r2_textures[handle].name ) ) {
			dst->imageHandle = handle;
		} else {
			dst->imageHandle = R2_GL_RegisterShader( dst->image, r2_textures[handle].noMip );
		}
	}
	r2_textures[handle].applyingShaderDef = qfalse;
	r2_textures[handle].shaderDefApplied = qtrue;
}

static qboolean R2_GL_NameHasPrefix( const char *name, const char *prefix ) {
	return name && prefix && !Q_stricmpn( name, prefix, (int)strlen( prefix ) );
}

static qboolean R2_GL_TextureShouldForceNoMip( const char *name, const r2ShaderDef_t *def, qboolean hasAlpha ) {
	if ( def && def->noMip ) {
		return qtrue;
	}
	if ( !hasAlpha || !name ) {
		return qfalse;
	}
	return R2_GL_NameHasPrefix( name, "textures/tree/" ) ||
		   R2_GL_NameHasPrefix( name, "models/mapobjects/tree/" );
}

static qboolean R2_GL_ShaderNameLooksCommonNoDraw( const char *name ) {
	char normalizedName[MAX_QPATH];
	const char *local;

	if ( !name || !name[0] ) {
		return qfalse;
	}

	R2_GL_StripImageExtension( name, normalizedName, sizeof( normalizedName ) );
	if ( Q_stricmpn( normalizedName, "textures/common/", 16 ) ) {
		return qfalse;
	}

	local = normalizedName + 16;
	if ( R2_GL_NameHasPrefix( local, "portal" ) ||
		 R2_GL_NameHasPrefix( local, "mirror" ) ||
		 R2_GL_NameHasPrefix( local, "dirtymirror" ) ) {
		return qfalse;
	}

	return R2_GL_NameHasPrefix( local, "caulk" ) ||
		   R2_GL_NameHasPrefix( local, "clip" ) ||
		   R2_GL_NameHasPrefix( local, "trigger" ) ||
		   R2_GL_NameHasPrefix( local, "nodraw" ) ||
		   R2_GL_NameHasPrefix( local, "nodrop" ) ||
		   R2_GL_NameHasPrefix( local, "hint" ) ||
		   R2_GL_NameHasPrefix( local, "skip" ) ||
		   R2_GL_NameHasPrefix( local, "ladder" ) ||
		   R2_GL_NameHasPrefix( local, "terrain" ) ||
		   R2_GL_NameHasPrefix( local, "slip" ) ||
		   R2_GL_NameHasPrefix( local, "clusterportal" ) ||
		   R2_GL_NameHasPrefix( local, "areaportal" ) ||
		   R2_GL_NameHasPrefix( local, "lightgrid" ) ||
		   R2_GL_NameHasPrefix( local, "origin" ) ||
		   R2_GL_NameHasPrefix( local, "ai_nosight" );
}

qhandle_t R2_GL_RegisterShader( const char *name, qboolean noMip ) {
	byte *pic = NULL;
	const r2ShaderAlias_t *alias;
	const r2ShaderDef_t *def;
	char normalizedName[MAX_QPATH];
	char resolvedName[MAX_QPATH];
	GLuint texnum = 0;
	int width = 0;
	int height = 0;
	int existing;
	qhandle_t handle;
	qboolean hasAlpha = qfalse;
	qboolean uploadNoMip;

	if ( !name || !name[0] || !Q_stricmp( name, "white" ) || !Q_stricmp( name, "*white" ) ) {
		return 0;
	}

	R2_GL_NormalizeAssetName( name, normalizedName, sizeof( normalizedName ) );
	R2_GL_LoadShaderAliases();
	def = R2_GL_FindShaderDef( normalizedName );
	if ( def ) {
		R2_GL_ApplyShaderDefSideEffects( def );
		if ( def->noDraw || def->isSky ) {
			return 0;
		}
	}
	if ( R2_GL_ShaderNameLooksCommonNoDraw( normalizedName ) ) {
		return 0;
	}
	existing = R2_GL_FindTextureHandle( normalizedName );
	if ( existing >= 0 ) {
		if ( def && !r2_textures[existing].shaderDefApplied && !r2_textures[existing].applyingShaderDef ) {
			R2_GL_ApplyShaderDefToTexture( existing, def );
		}
		return existing;
	}

	if ( R2_GL_LoadImage( normalizedName, &pic, &width, &height, resolvedName, sizeof( resolvedName ) ) ) {
		hasAlpha = R2_GL_ImageHasAlpha( pic, width, height );
		uploadNoMip = noMip || R2_GL_TextureShouldForceNoMip( normalizedName, def, hasAlpha );
		if ( R2_GL_CreateTextureRGBA( resolvedName, pic, width, height, uploadNoMip, &texnum ) ) {
			handle = R2_GL_StoreTexture( normalizedName, texnum, width, height, uploadNoMip, qtrue,
										 hasAlpha, NULL );
			free( pic );
			R2_GL_ApplyShaderDefToTexture( handle, def );
			return handle;
		}
	}

	if ( pic ) {
		free( pic );
		pic = NULL;
	}

	alias = R2_GL_FindShaderAlias( normalizedName );
	if ( alias && Q_stricmp( alias->image, normalizedName ) &&
		 R2_GL_LoadImage( alias->image, &pic, &width, &height, resolvedName, sizeof( resolvedName ) ) ) {
		hasAlpha = R2_GL_ImageHasAlpha( pic, width, height );
		uploadNoMip = noMip || R2_GL_TextureShouldForceNoMip( normalizedName, def, hasAlpha ) ||
					  R2_GL_TextureShouldForceNoMip( alias->image, def, hasAlpha );
		if ( R2_GL_CreateTextureRGBA( resolvedName, pic, width, height, uploadNoMip, &texnum ) ) {
			handle = R2_GL_StoreTexture( normalizedName, texnum, width, height, uploadNoMip, qtrue,
										 hasAlpha, alias );
			free( pic );
			R2_GL_ApplyShaderDefToTexture( handle, def );
			return handle;
		}
	}

	if ( pic ) {
		free( pic );
	}
	if ( r2_logMissingTextures && r2_logMissingTextures->integer ) {
		if ( alias ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: missing texture/shader '%s' -> '%s', using white fallback\n", normalizedName, alias->image );
		} else {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: missing texture/shader '%s', using white fallback\n", normalizedName );
		}
	}
	handle = R2_GL_StoreTexture( normalizedName, r2.whiteTexture, 1, 1, noMip, qfalse, qfalse, alias );
	R2_GL_ApplyShaderDefToTexture( handle, def );
	return handle;
}

qboolean R2_GL_ShaderIsSky( qhandle_t hShader ) {
	if ( hShader <= 0 || hShader >= r2_numTextures ) {
		return qfalse;
	}
	return r2_textures[hShader].isSky;
}

qboolean R2_GL_ShaderIsNoDraw( qhandle_t hShader ) {
	if ( hShader <= 0 || hShader >= r2_numTextures ) {
		return qfalse;
	}
	return r2_textures[hShader].noDraw;
}

qboolean R2_GL_ShaderNoLightmap( qhandle_t hShader ) {
	if ( hShader <= 0 || hShader >= r2_numTextures ) {
		return qfalse;
	}
	return r2_textures[hShader].noLightmap;
}

qboolean R2_GL_ShaderNameIsNoDraw( const char *name ) {
	const r2ShaderDef_t *def;

	if ( !name || !name[0] ) {
		return qfalse;
	}

	R2_GL_LoadShaderAliases();
	def = R2_GL_FindShaderDef( name );
	return ( def && def->noDraw ) || R2_GL_ShaderNameLooksCommonNoDraw( name );
}

qboolean R2_GL_ShaderNameIsSky( const char *name ) {
	const r2ShaderDef_t *def;

	if ( !name || !name[0] ) {
		return qfalse;
	}

	R2_GL_LoadShaderAliases();
	def = R2_GL_FindShaderDef( name );
	return def && def->isSky;
}

void R2_GL_ApplyShaderNameSideEffects( const char *name ) {
	const r2ShaderDef_t *def;

	if ( !name || !name[0] ) {
		return;
	}

	R2_GL_LoadShaderAliases();
	def = R2_GL_FindShaderDef( name );
	R2_GL_ApplyShaderDefSideEffects( def );
}

static int R2_GL_FontReadInt( const byte *data, int *offset ) {
	int value = data[*offset] |
				( data[*offset + 1] << 8 ) |
				( data[*offset + 2] << 16 ) |
				( data[*offset + 3] << 24 );
	*offset += 4;
	return value;
}

static float R2_GL_FontReadFloat( const byte *data, int *offset ) {
	union {
		byte b[4];
		float f;
	} value;

	value.b[0] = data[*offset + 0];
	value.b[1] = data[*offset + 1];
	value.b[2] = data[*offset + 2];
	value.b[3] = data[*offset + 3];
	*offset += 4;
	return value.f;
}

static void R2_GL_RefreshFontGlyphHandles( fontInfo_t *font ) {
	int i;

	if ( !font ) {
		return;
	}
	for ( i = GLYPH_START; i < GLYPH_END; ++i ) {
		if ( font->glyphs[i].shaderName[0] ) {
			font->glyphs[i].glyph = R2_GL_RegisterShader( font->glyphs[i].shaderName, qtrue );
		}
	}
}

void R2_GL_RegisterFont( const char *fontName, int pointSize, fontInfo_t *font ) {
	byte *fontData = NULL;
	char name[MAX_QPATH];
	int len;
	int offset = 0;
	int i;

	(void)fontName;

	if ( !font ) {
		return;
	}
	memset( font, 0, sizeof( *font ) );

	if ( pointSize <= 0 ) {
		pointSize = 12;
	}
	Com_sprintf( name, sizeof( name ), "fonts/fontImage_%i.dat", pointSize );

	for ( i = 0; i < r2_registeredFontCount; ++i ) {
		if ( !Q_stricmp( r2_registeredFonts[i].name, name ) ) {
			memcpy( font, &r2_registeredFonts[i], sizeof( *font ) );
			R2_GL_RefreshFontGlyphHandles( font );
			memcpy( &r2_registeredFonts[i], font, sizeof( *font ) );
			return;
		}
	}

	len = r2_ri.FS_ReadFile( name, (void **)&fontData );
	if ( len != sizeof( fontInfo_t ) || !fontData ) {
		if ( fontData ) {
			r2_ri.FS_FreeFile( fontData );
		}
		r2_ri.Printf( PRINT_WARNING,
					  "Renderer2: font data '%s' missing or incompatible, menu text will be limited\n",
					  name );
		return;
	}

	for ( i = 0; i < GLYPHS_PER_FONT; ++i ) {
		font->glyphs[i].height = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].top = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].bottom = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].pitch = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].xSkip = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].imageWidth = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].imageHeight = R2_GL_FontReadInt( fontData, &offset );
		font->glyphs[i].s = R2_GL_FontReadFloat( fontData, &offset );
		font->glyphs[i].t = R2_GL_FontReadFloat( fontData, &offset );
		font->glyphs[i].s2 = R2_GL_FontReadFloat( fontData, &offset );
		font->glyphs[i].t2 = R2_GL_FontReadFloat( fontData, &offset );
		font->glyphs[i].glyph = R2_GL_FontReadInt( fontData, &offset );
		memcpy( font->glyphs[i].shaderName, &fontData[offset], sizeof( font->glyphs[i].shaderName ) );
		font->glyphs[i].shaderName[sizeof( font->glyphs[i].shaderName ) - 1] = '\0';
		offset += sizeof( font->glyphs[i].shaderName );
	}

	font->glyphScale = R2_GL_FontReadFloat( fontData, &offset );
	memcpy( font->name, &fontData[offset], sizeof( font->name ) );
	font->name[sizeof( font->name ) - 1] = '\0';
	r2_ri.FS_FreeFile( fontData );

	Q_strncpyz( font->name, name, sizeof( font->name ) );
	R2_GL_RefreshFontGlyphHandles( font );

	if ( r2_registeredFontCount < R2_MAX_FONTS ) {
		memcpy( &r2_registeredFonts[r2_registeredFontCount++], font, sizeof( *font ) );
	}
}

static GLuint R2_GL_TextureForHandle( qhandle_t hShader ) {
	if ( hShader <= 0 || hShader >= r2_numTextures ) {
		return r2.whiteTexture;
	}
	if ( !r2_textures[hShader].texnum ) {
		return r2.whiteTexture;
	}
	return r2_textures[hShader].texnum;
}

static const r2ShaderStage_t *R2_GL_ModelStageForHandle( qhandle_t hShader ) {
	static r2ShaderStage_t implicitAlphaStage;
	const r2Texture_t *texture;
	int i;

	if ( hShader <= 0 || hShader >= r2_numTextures ) {
		return NULL;
	}

	texture = &r2_textures[hShader];
	for ( i = 0; i < texture->stageCount; ++i ) {
		const r2ShaderStage_t *stage = &texture->stages[i];

		if ( stage->isLightmap || stage->imageHandle <= 0 ) {
			continue;
		}
		return stage;
	}

	if ( texture->valid && texture->hasAlpha ) {
		memset( &implicitAlphaStage, 0, sizeof( implicitAlphaStage ) );
		implicitAlphaStage.imageHandle = hShader;
		implicitAlphaStage.blendSrc = R2_BLEND_ONE;
		implicitAlphaStage.blendDst = R2_BLEND_ZERO;
		implicitAlphaStage.alphaGen = R2_ALPHA_GEN_IDENTITY;
		implicitAlphaStage.tcGen = R2_TCGEN_TEXTURE;
		implicitAlphaStage.alphaTest = R2_ALPHA_GT0;
		implicitAlphaStage.depthWrite = qtrue;
		implicitAlphaStage.scaleS = 1.0f;
		implicitAlphaStage.scaleT = 1.0f;
		return &implicitAlphaStage;
	}

	return NULL;
}

static GLuint R2_GL_ModelTextureForHandle( qhandle_t hShader ) {
	const r2ShaderStage_t *stage = R2_GL_ModelStageForHandle( hShader );

	if ( stage ) {
		return R2_GL_TextureForHandle( stage->imageHandle );
	}
	return R2_GL_TextureForHandle( hShader );
}

static float R2_GL_ScreenToNdcX( float x );
static float R2_GL_ScreenToNdcY( float y );
static void R2_GL_SetVertexColor( r2ColorVertex_t *vertex, const float *color );
static void R2_GL_SetVertexTexCoord( r2ColorVertex_t *vertex, float s, float t );

static void R2_GL_DrawVerticesWithTexture( const r2ColorVertex_t *vertices, int vertexCount, GLuint texnum ) {

	if ( !r2.initialized || !r2.pipelineReady || !vertices || vertexCount <= 0 || !texnum ) {
		return;
	}

	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glBindTexture( GL_TEXTURE_2D, texnum );
	r2gl.UseProgram( r2.colorProgram );
	r2gl.BindVertexArray( r2.colorVao );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, r2.colorVbo );
	r2gl.BufferData( GL_ARRAY_BUFFER, sizeof( r2ColorVertex_t ) * vertexCount, vertices, GL_DYNAMIC_DRAW );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	r2gl.BindVertexArray( 0 );
	r2gl.UseProgram( 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDepthMask( GL_TRUE );
}

static void R2_GL_DrawVertices( const r2ColorVertex_t *vertices, int vertexCount, qhandle_t hShader ) {
	GLuint texnum;
	const r2ColorVertex_t *drawVertices = vertices;
	r2ColorVertex_t *animatedVertices = NULL;

	if ( !r2.initialized || !r2.pipelineReady || !vertices || vertexCount <= 0 ) {
		return;
	}

	texnum = R2_GL_TextureForHandle( hShader );
	if ( r2_shaderTcMod && r2_shaderTcMod->integer &&
		 hShader > 0 && hShader < r2_numTextures &&
		 ( r2_textures[hShader].hasScroll || r2_textures[hShader].hasScale ) ) {
		int i;
		float time = (float)r2_ri.Milliseconds() * 0.001f;
		float scaleS = r2_textures[hShader].hasScale ? r2_textures[hShader].scaleS : 1.0f;
		float scaleT = r2_textures[hShader].hasScale ? r2_textures[hShader].scaleT : 1.0f;
		float scrollS = r2_textures[hShader].hasScroll ? r2_textures[hShader].scrollS * time : 0.0f;
		float scrollT = r2_textures[hShader].hasScroll ? r2_textures[hShader].scrollT * time : 0.0f;

		animatedVertices = (r2ColorVertex_t *)malloc( sizeof( *animatedVertices ) * vertexCount );
		if ( animatedVertices ) {
			memcpy( animatedVertices, vertices, sizeof( *animatedVertices ) * vertexCount );
			for ( i = 0; i < vertexCount; ++i ) {
				animatedVertices[i].s = animatedVertices[i].s * scaleS + scrollS;
				animatedVertices[i].t = animatedVertices[i].t * scaleT + scrollT;
			}
			drawVertices = animatedVertices;
		}
	}

	R2_GL_DrawVerticesWithTexture( drawVertices, vertexCount, texnum );
	if ( animatedVertices ) {
		free( animatedVertices );
	}
}

static qboolean R2_GL_ValidRawClient( int client ) {
	return client >= 0 && client < R2_MAX_RAW_TEXTURES;
}

static qboolean R2_GL_UploadRawTexture( int cols, int rows, const byte *data, int client, qboolean dirty ) {
	r2RawTexture_t *raw;

	if ( !r2.initialized || !R2_GL_ValidRawClient( client ) || cols <= 0 || rows <= 0 || !data ) {
		return qfalse;
	}

	raw = &r2_rawTextures[client];
	if ( !raw->texnum ) {
		glGenTextures( 1, &raw->texnum );
		if ( !raw->texnum ) {
			return qfalse;
		}
		raw->width = 0;
		raw->height = 0;
		raw->valid = qfalse;
	}

	glBindTexture( GL_TEXTURE_2D, raw->texnum );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	if ( !raw->valid || raw->width != cols || raw->height != rows ) {
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
		raw->width = cols;
		raw->height = rows;
		raw->valid = qtrue;
	} else if ( dirty ) {
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	return qtrue;
}

void R2_GL_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
						   const byte *data, int client, qboolean dirty ) {
	float x0;
	float x1;
	float y0;
	float y1;
	float s0;
	float s1;
	float t0;
	float t1;
	const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	r2ColorVertex_t vertices[6];

	if ( w <= 0 || h <= 0 || cols <= 0 || rows <= 0 ) {
		return;
	}
	if ( !R2_GL_UploadRawTexture( cols, rows, data, client, dirty ) ) {
		return;
	}

	x0 = R2_GL_ScreenToNdcX( (float)x );
	x1 = R2_GL_ScreenToNdcX( (float)( x + w ) );
	y0 = R2_GL_ScreenToNdcY( (float)y );
	y1 = R2_GL_ScreenToNdcY( (float)( y + h ) );
	s0 = 0.5f / (float)cols;
	t0 = 0.5f / (float)rows;
	s1 = ( (float)cols - 0.5f ) / (float)cols;
	t1 = ( (float)rows - 0.5f ) / (float)rows;

	vertices[0].x = x0; vertices[0].y = y0;
	vertices[1].x = x1; vertices[1].y = y0;
	vertices[2].x = x1; vertices[2].y = y1;
	vertices[3].x = x0; vertices[3].y = y0;
	vertices[4].x = x1; vertices[4].y = y1;
	vertices[5].x = x0; vertices[5].y = y1;

	R2_GL_SetVertexTexCoord( &vertices[0], s0, t0 );
	R2_GL_SetVertexTexCoord( &vertices[1], s1, t0 );
	R2_GL_SetVertexTexCoord( &vertices[2], s1, t1 );
	R2_GL_SetVertexTexCoord( &vertices[3], s0, t0 );
	R2_GL_SetVertexTexCoord( &vertices[4], s1, t1 );
	R2_GL_SetVertexTexCoord( &vertices[5], s0, t1 );

	R2_GL_SetVertexColor( &vertices[0], white );
	R2_GL_SetVertexColor( &vertices[1], white );
	R2_GL_SetVertexColor( &vertices[2], white );
	R2_GL_SetVertexColor( &vertices[3], white );
	R2_GL_SetVertexColor( &vertices[4], white );
	R2_GL_SetVertexColor( &vertices[5], white );

	R2_GL_DrawVerticesWithTexture( vertices, 6, r2_rawTextures[client].texnum );
}

void R2_GL_UploadCinematic( int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {
	(void)w;
	(void)h;
	R2_GL_UploadRawTexture( cols, rows, data, client, dirty );
}

void R2_GL_DrawModelTriangles( const r2ModelVertex_t *vertices, int vertexCount, qhandle_t hShader, const float *mvp ) {
	GLuint texnum;
	const r2ShaderStage_t *stage;
	const r2ModelVertex_t *drawVertices = vertices;
	r2ModelVertex_t *animatedVertices = NULL;
	int vertexBytes;

	if ( !r2.initialized || !r2.modelProgram || !vertices || vertexCount <= 0 || !mvp ) {
		return;
	}
	vertexBytes = sizeof( r2ModelVertex_t ) * vertexCount;

	texnum = R2_GL_ModelTextureForHandle( hShader );
	stage = R2_GL_ModelStageForHandle( hShader );
	if ( r2_shaderTcMod && r2_shaderTcMod->integer &&
		 hShader > 0 && hShader < r2_numTextures &&
		 ( r2_textures[hShader].hasScroll || r2_textures[hShader].hasScale ) ) {
		int i;
		float time = (float)r2_ri.Milliseconds() * 0.001f;
		float scaleS = r2_textures[hShader].hasScale ? r2_textures[hShader].scaleS : 1.0f;
		float scaleT = r2_textures[hShader].hasScale ? r2_textures[hShader].scaleT : 1.0f;
		float scrollS = r2_textures[hShader].hasScroll ? r2_textures[hShader].scrollS * time : 0.0f;
		float scrollT = r2_textures[hShader].hasScroll ? r2_textures[hShader].scrollT * time : 0.0f;

		animatedVertices = (r2ModelVertex_t *)malloc( sizeof( *animatedVertices ) * vertexCount );
		if ( animatedVertices ) {
			memcpy( animatedVertices, vertices, sizeof( *animatedVertices ) * vertexCount );
			for ( i = 0; i < vertexCount; ++i ) {
				animatedVertices[i].st[0] = animatedVertices[i].st[0] * scaleS + scrollS;
				animatedVertices[i].st[1] = animatedVertices[i].st[1] * scaleT + scrollT;
			}
			drawVertices = animatedVertices;
		}
	}

	R2_GL_SetStageDepthBlendState( stage, qfalse );
	glBindTexture( GL_TEXTURE_2D, texnum );
	r2gl.UseProgram( r2.modelProgram );
	if ( r2.modelSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.modelSamplerLoc, 0 );
	}
	if ( r2.modelMvpLoc >= 0 ) {
		r2gl.UniformMatrix4fv( r2.modelMvpLoc, 1, GL_FALSE, mvp );
	}
	if ( r2.modelAlphaTestLoc >= 0 ) {
		r2gl.Uniform1i( r2.modelAlphaTestLoc, stage ? stage->alphaTest : R2_ALPHA_NONE );
	}
	r2gl.BindVertexArray( r2.modelVao );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, r2.modelVbo );
	r2gl.VertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)0 );
	r2gl.EnableVertexAttribArray( 0 );
	r2gl.VertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 3 ) );
	r2gl.EnableVertexAttribArray( 1 );
	r2gl.VertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 5 ) );
	r2gl.EnableVertexAttribArray( 2 );
	if ( vertexBytes > r2.modelVboCapacityBytes ) {
		int newCapacity = r2.modelVboCapacityBytes ? r2.modelVboCapacityBytes : 65536;
		while ( newCapacity < vertexBytes ) {
			newCapacity *= 2;
		}
		r2gl.BufferData( GL_ARRAY_BUFFER, newCapacity, NULL, GL_DYNAMIC_DRAW );
		r2.modelVboCapacityBytes = newCapacity;
	}
	r2gl.BufferSubData( GL_ARRAY_BUFFER, 0, vertexBytes, drawVertices );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	r2gl.BindVertexArray( 0 );
	r2gl.UseProgram( 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	if ( animatedVertices ) {
		free( animatedVertices );
	}
}

void R2_GL_DrawModelTrianglesVbo( unsigned int vbo, int vertexCount, qhandle_t hShader, const float *mvp ) {
	GLuint texnum;
	const r2ShaderStage_t *stage;

	if ( !r2.initialized || !r2.modelProgram || !vbo || vertexCount <= 0 || !mvp ) {
		return;
	}

	texnum = R2_GL_ModelTextureForHandle( hShader );
	stage = R2_GL_ModelStageForHandle( hShader );
	R2_GL_SetStageDepthBlendState( stage, qfalse );
	glBindTexture( GL_TEXTURE_2D, texnum );
	r2gl.UseProgram( r2.modelProgram );
	if ( r2.modelSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.modelSamplerLoc, 0 );
	}
	if ( r2.modelMvpLoc >= 0 ) {
		r2gl.UniformMatrix4fv( r2.modelMvpLoc, 1, GL_FALSE, mvp );
	}
	if ( r2.modelAlphaTestLoc >= 0 ) {
		r2gl.Uniform1i( r2.modelAlphaTestLoc, stage ? stage->alphaTest : R2_ALPHA_NONE );
	}
	r2gl.BindVertexArray( r2.modelVao );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, vbo );
	r2gl.VertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)0 );
	r2gl.EnableVertexAttribArray( 0 );
	r2gl.VertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 3 ) );
	r2gl.EnableVertexAttribArray( 1 );
	r2gl.VertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 5 ) );
	r2gl.EnableVertexAttribArray( 2 );
	glDrawArrays( GL_TRIANGLES, 0, vertexCount );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	r2gl.BindVertexArray( 0 );
	r2gl.UseProgram( 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
}

int R2_GL_WorldShaderPassCount( qhandle_t hShader ) {
	const r2Texture_t *texture = NULL;

	if ( hShader > 0 && hShader < r2_numTextures ) {
		texture = &r2_textures[hShader];
	}
	if ( texture && R2_GL_ShouldCollapseLightmapMultiplyStages( texture ) ) {
		return 1;
	}
	if ( texture && R2_GL_ShouldCollapseTerrainStages( texture ) ) {
		return 1;
	}
	return texture && texture->stageCount > 0 ? texture->stageCount : 1;
}

static r2AlphaTestMode_t R2_GL_DefaultWorldAlphaTestForTexture( const r2Texture_t *texture ) {
	if ( texture && texture->valid && texture->hasAlpha && texture->stageCount <= 0 ) {
		return R2_ALPHA_GT0;
	}
	return R2_ALPHA_NONE;
}

void R2_GL_DrawWorldTrianglesVboPassMulti( unsigned int vbo, const int *firstVertices,
										   const int *vertexCounts, int drawCount,
										   qhandle_t hShader, unsigned int lightmapTexnum,
										   const float *mvp, int pass ) {
	const r2Texture_t *texture = NULL;
	const r2ShaderStage_t *stage;
	const r2ShaderStage_t *blendStage = NULL;
	const r2ShaderStage_t *filterStage = NULL;
	qboolean explicitLightmap;
	qboolean fallbackLightmap;
	qboolean cloudFilter;
	qboolean collapsedTerrainStages;
	qboolean collapsedLightmapMultiplyStages;
	qboolean shaderLightmap;
	GLuint passTexture = r2.whiteTexture;
	GLuint blendTexture = r2.whiteTexture;
	GLuint filterTexture = r2.whiteTexture;
	int passCount;
	int actualPass;
	int blendStageIndex;
	int filterStageIndex;
	int lightmapMultiplyStageIndex;
	int terrainDebugBlend = 0;
	int drawIndex;

	if ( !r2.initialized || !r2.worldProgram || !vbo || !firstVertices || !vertexCounts ||
		 drawCount <= 0 || !mvp ) {
		return;
	}
	for ( drawIndex = 0; drawIndex < drawCount; ++drawIndex ) {
		if ( firstVertices[drawIndex] < 0 || vertexCounts[drawIndex] <= 0 ) {
			return;
		}
	}

	if ( hShader > 0 && hShader < r2_numTextures ) {
		texture = &r2_textures[hShader];
	}
	passCount = R2_GL_WorldShaderPassCount( hShader );
	if ( pass < 0 || pass >= passCount ) {
		return;
	}

	collapsedLightmapMultiplyStages = texture && R2_GL_ShouldCollapseLightmapMultiplyStages( texture );
	collapsedTerrainStages = !collapsedLightmapMultiplyStages && texture && R2_GL_ShouldCollapseTerrainStages( texture );
	actualPass = pass;
	stage = texture && texture->stageCount > 0 ? &texture->stages[actualPass] : NULL;
	lightmapMultiplyStageIndex = texture ? R2_GL_LightmapMultiplyStageIndex( texture ) : -1;
	if ( collapsedLightmapMultiplyStages && lightmapMultiplyStageIndex >= 0 ) {
		actualPass = lightmapMultiplyStageIndex;
		stage = &texture->stages[lightmapMultiplyStageIndex];
	} else if ( collapsedTerrainStages ) {
		stage = texture && texture->stageCount > 0 ? &texture->stages[0] : NULL;
	}
	blendStageIndex = texture ? R2_GL_TerrainBlendStageIndex( texture ) : -1;
	if ( collapsedTerrainStages && blendStageIndex >= 0 ) {
		blendStage = &texture->stages[blendStageIndex];
	}
	filterStageIndex = collapsedTerrainStages ? R2_GL_TerrainFilterStageIndex( texture ) : -1;
	if ( filterStageIndex >= 0 && r2_worldCloudStages &&
		 r2_worldCloudStages->integer ) {
		filterStage = &texture->stages[filterStageIndex];
	}
	if ( collapsedTerrainStages && r2_worldTerrainDebugBlend ) {
		terrainDebugBlend = r2_worldTerrainDebugBlend->integer;
	}
	explicitLightmap = stage && stage->isLightmap;
	fallbackLightmap = !stage && lightmapTexnum;
	shaderLightmap = ( explicitLightmap || fallbackLightmap || collapsedLightmapMultiplyStages ) && lightmapTexnum;
	cloudFilter = !collapsedTerrainStages && R2_GL_IsCloudFilterStage( stage );

	if ( cloudFilter && r2_worldCloudStages && !r2_worldCloudStages->integer ) {
		return;
	}

	if ( stage ) {
		if ( explicitLightmap ) {
			passTexture = r2.whiteTexture;
		} else {
			passTexture = R2_GL_TextureForHandle( stage->imageHandle );
		}
	} else {
		passTexture = R2_GL_TextureForHandle( hShader );
	}
	if ( blendStage ) {
		blendTexture = R2_GL_TextureForHandle( blendStage->imageHandle );
	}
	if ( filterStage ) {
		filterTexture = R2_GL_TextureForHandle( filterStage->imageHandle );
	}

	r2gl.UseProgram( r2.worldProgram );
	if ( r2.worldSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldSamplerLoc, 0 );
	}
	if ( r2.worldLightmapSamplerLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldLightmapSamplerLoc, 1 );
	}
	if ( r2.worldMvpLoc >= 0 ) {
		r2gl.UniformMatrix4fv( r2.worldMvpLoc, 1, GL_FALSE, mvp );
	}

	r2gl.BindVertexArray( r2.modelVao );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, vbo );
	r2gl.VertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)0 );
	r2gl.EnableVertexAttribArray( 0 );
	r2gl.VertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 3 ) );
	r2gl.EnableVertexAttribArray( 1 );
	r2gl.VertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 5 ) );
	r2gl.EnableVertexAttribArray( 2 );
	r2gl.VertexAttribPointer( 3, 2, GL_FLOAT, GL_FALSE, sizeof( r2ModelVertex_t ), (const void *)( sizeof( float ) * 9 ) );
	r2gl.EnableVertexAttribArray( 3 );

	R2_GL_SetCullMode( texture ? texture->cullMode : R2_CULL_FRONT );

	R2_GL_SetStageDepthBlendState( stage, collapsedLightmapMultiplyStages );

	r2gl.ActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, passTexture );
	r2gl.ActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, shaderLightmap ? lightmapTexnum : r2.whiteTexture );
	r2gl.ActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, blendTexture );
	r2gl.ActiveTexture( GL_TEXTURE3 );
	glBindTexture( GL_TEXTURE_2D, filterTexture );
	r2gl.ActiveTexture( GL_TEXTURE0 );

	if ( r2.worldHasLightmapLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldHasLightmapLoc, shaderLightmap ? 1 : 0 );
	}
	if ( r2.worldHasBlendStageLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldHasBlendStageLoc, blendStage ? 1 : 0 );
	}
	if ( r2.worldHasFilterStageLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldHasFilterStageLoc, filterStage ? 1 : 0 );
	}
	if ( r2.worldIsFilterPassLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldIsFilterPassLoc, cloudFilter ? 1 : 0 );
	}
	if ( r2.worldTcSourceLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldTcSourceLoc, stage ? stage->tcGen : R2_TCGEN_TEXTURE );
	}
	if ( r2.worldTcScaleLoc >= 0 ) {
		float scaleS = stage && stage->hasScale && !explicitLightmap ? stage->scaleS : 1.0f;
		float scaleT = stage && stage->hasScale && !explicitLightmap ? stage->scaleT : 1.0f;
		r2gl.Uniform2f( r2.worldTcScaleLoc, scaleS, scaleT );
	}
	if ( r2.worldTcOffsetLoc >= 0 ) {
		float offsetS = 0.0f;
		float offsetT = 0.0f;
		if ( r2_worldTcMod && r2_worldTcMod->integer && stage && stage->hasScroll && !explicitLightmap ) {
			float time = (float)r2_ri.Milliseconds() * 0.001f;
			offsetS = stage->scrollS * time;
			offsetT = stage->scrollT * time;
		}
		r2gl.Uniform2f( r2.worldTcOffsetLoc, offsetS, offsetT );
	}
	if ( r2.worldBlendTcScaleLoc >= 0 ) {
		float scaleS = blendStage && blendStage->hasScale ? blendStage->scaleS : 1.0f;
		float scaleT = blendStage && blendStage->hasScale ? blendStage->scaleT : 1.0f;
		r2gl.Uniform2f( r2.worldBlendTcScaleLoc, scaleS, scaleT );
	}
	if ( r2.worldBlendTcOffsetLoc >= 0 ) {
		float offsetS = 0.0f;
		float offsetT = 0.0f;
		if ( r2_worldTcMod && r2_worldTcMod->integer && blendStage && blendStage->hasScroll ) {
			float time = (float)r2_ri.Milliseconds() * 0.001f;
			offsetS = blendStage->scrollS * time;
			offsetT = blendStage->scrollT * time;
		}
		r2gl.Uniform2f( r2.worldBlendTcOffsetLoc, offsetS, offsetT );
	}
	if ( r2.worldFilterTcScaleLoc >= 0 ) {
		float scaleS = filterStage && filterStage->hasScale ? filterStage->scaleS : 1.0f;
		float scaleT = filterStage && filterStage->hasScale ? filterStage->scaleT : 1.0f;
		r2gl.Uniform2f( r2.worldFilterTcScaleLoc, scaleS, scaleT );
	}
	if ( r2.worldFilterTcOffsetLoc >= 0 ) {
		float offsetS = 0.0f;
		float offsetT = 0.0f;
		if ( r2_worldTcMod && r2_worldTcMod->integer && filterStage && filterStage->hasScroll ) {
			float time = (float)r2_ri.Milliseconds() * 0.001f;
			offsetS = filterStage->scrollS * time;
			offsetT = filterStage->scrollT * time;
		}
		r2gl.Uniform2f( r2.worldFilterTcOffsetLoc, offsetS, offsetT );
	}
	if ( r2.worldFilterStrengthLoc >= 0 ) {
		float strength = 1.0f;
		if ( cloudFilter && r2_worldCloudStrength ) {
			strength = r2_worldCloudStrength->value;
			if ( strength < 0.0f ) {
				strength = 0.0f;
			} else if ( strength > 1.0f ) {
				strength = 1.0f;
			}
		}
		r2gl.Uniform1f( r2.worldFilterStrengthLoc, strength );
	}
	if ( r2.worldRgbVertexLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldRgbVertexLoc, stage ? ( stage->rgbVertex ? 1 : 0 ) : ( fallbackLightmap ? 0 : 1 ) );
	}
	if ( r2.worldAlphaGenLoc >= 0 ) {
		r2AlphaGenMode_t alphaGen = stage ? stage->alphaGen : R2_ALPHA_GEN_IDENTITY;
		if ( texture && actualPass == blendStageIndex ) {
			alphaGen = R2_GL_EffectiveTerrainBlendAlphaGen( stage );
		}
		if ( R2_GL_ShouldInvertTerrainBlendAlpha( texture, actualPass ) ) {
			alphaGen = R2_GL_InvertAlphaGen( alphaGen );
		}
		r2gl.Uniform1i( r2.worldAlphaGenLoc, alphaGen );
	}
	if ( r2.worldBlendRgbVertexLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldBlendRgbVertexLoc, blendStage && blendStage->rgbVertex ? 1 : 0 );
	}
	if ( r2.worldBlendAlphaGenLoc >= 0 ) {
		r2AlphaGenMode_t blendAlphaGen = R2_GL_EffectiveTerrainBlendAlphaGen( blendStage );
		if ( blendStage && r2_worldTerrainInvertAlpha && r2_worldTerrainInvertAlpha->integer ) {
			blendAlphaGen = R2_GL_InvertAlphaGen( blendAlphaGen );
		}
		r2gl.Uniform1i( r2.worldBlendAlphaGenLoc, blendAlphaGen );
	}
	if ( r2.worldAlphaTestLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldAlphaTestLoc,
						 stage ? stage->alphaTest : R2_GL_DefaultWorldAlphaTestForTexture( texture ) );
	}
	if ( r2.worldTerrainDebugBlendLoc >= 0 ) {
		r2gl.Uniform1i( r2.worldTerrainDebugBlendLoc, terrainDebugBlend );
	}

	if ( drawCount == 1 ) {
		glDrawArrays( GL_TRIANGLES, firstVertices[0], vertexCounts[0] );
	} else {
		r2gl.MultiDrawArrays( GL_TRIANGLES, (const GLint *)firstVertices, (const GLsizei *)vertexCounts, drawCount );
	}

	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	r2gl.BindBuffer( GL_ARRAY_BUFFER, 0 );
	r2gl.BindVertexArray( 0 );
	r2gl.UseProgram( 0 );
	r2gl.ActiveTexture( GL_TEXTURE3 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	r2gl.ActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	r2gl.ActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	r2gl.ActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void R2_GL_DrawWorldTrianglesVboPass( unsigned int vbo, int firstVertex, int vertexCount,
									  qhandle_t hShader, unsigned int lightmapTexnum,
									  const float *mvp, int pass ) {
	int first = firstVertex;
	int count = vertexCount;

	if ( firstVertex < 0 || vertexCount <= 0 ) {
		return;
	}
	R2_GL_DrawWorldTrianglesVboPassMulti( vbo, &first, &count, 1, hShader, lightmapTexnum, mvp, pass );
}

void R2_GL_DrawWorldTrianglesVbo( unsigned int vbo, int firstVertex, int vertexCount,
								  qhandle_t hShader, unsigned int lightmapTexnum, const float *mvp ) {
	int passCount;
	int pass;

	passCount = R2_GL_WorldShaderPassCount( hShader );
	for ( pass = 0; pass < passCount; ++pass ) {
		R2_GL_DrawWorldTrianglesVboPass( vbo, firstVertex, vertexCount, hShader, lightmapTexnum, mvp, pass );
	}
}

static void R2_GL_PolyProjectionMatrix( float fovX, float fovY, float *m ) {
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

static void R2_GL_TransformPolyVert( const refdef_t *fd, const polyVert_t *in, r2ModelVertex_t *out ) {
	vec3_t delta;

	VectorSubtract( in->xyz, fd->vieworg, delta );
	out->xyz[0] = -DotProduct( delta, fd->viewaxis[1] );
	out->xyz[1] = DotProduct( delta, fd->viewaxis[2] );
	out->xyz[2] = -DotProduct( delta, fd->viewaxis[0] );
	out->st[0] = in->st[0];
	out->st[1] = in->st[1];
	out->color[0] = (float)in->modulate[0] / 255.0f;
	out->color[1] = (float)in->modulate[1] / 255.0f;
	out->color[2] = (float)in->modulate[2] / 255.0f;
	out->color[3] = (float)in->modulate[3] / 255.0f;
}

void R2_GL_DrawScenePolys( const refdef_t *fd, const poly_t *polys, int numPolys ) {
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

	if ( !fd || !polys || numPolys <= 0 || r2.glConfig.vidWidth <= 0 || r2.glConfig.vidHeight <= 0 ) {
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

	R2_GL_PolyProjectionMatrix( fd->fov_x, fd->fov_y, mvp );

	glViewport( viewportX, viewportY, viewportW, viewportH );
	glEnable( GL_SCISSOR_TEST );
	glScissor( scissorX, scissorY, scissorW, scissorH );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_FALSE );
	glDisable( GL_CULL_FACE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	for ( i = 0; i < numPolys; ++i ) {
		const poly_t *poly = &polys[i];
		r2ModelVertex_t *triVerts;
		int triCount;
		int j;

		if ( !poly->verts || poly->numVerts < 3 ) {
			continue;
		}

		triCount = ( poly->numVerts - 2 ) * 3;
		triVerts = (r2ModelVertex_t *)malloc( sizeof( *triVerts ) * triCount );
		if ( !triVerts ) {
			continue;
		}

		for ( j = 0; j < poly->numVerts - 2; ++j ) {
			R2_GL_TransformPolyVert( fd, &poly->verts[0], &triVerts[j * 3 + 0] );
			R2_GL_TransformPolyVert( fd, &poly->verts[j + 1], &triVerts[j * 3 + 1] );
			R2_GL_TransformPolyVert( fd, &poly->verts[j + 2], &triVerts[j * 3 + 2] );
		}
		R2_GL_DrawModelTriangles( triVerts, triCount, poly->hShader, mvp );
		free( triVerts );
	}

	glDepthMask( GL_TRUE );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glViewport( 0, 0, r2.glConfig.vidWidth, r2.glConfig.vidHeight );
}

static void R2_GL_DrawDiagnosticTriangle( void ) {
	const r2ColorVertex_t vertices[3] = {
		{ -0.65f, -0.55f, 0.0f, 1.0f, 0.10f, 0.45f, 1.00f, 0.92f },
		{  0.65f, -0.55f, 1.0f, 1.0f, 0.20f, 0.90f, 0.35f, 0.92f },
		{  0.00f,  0.55f, 0.5f, 0.0f, 1.00f, 0.80f, 0.20f, 0.92f }
	};
	R2_GL_DrawVertices( vertices, 3, 0 );
}

void R2_GL_PresentInitFrame( void ) {
	if ( !r2.initialized || !sdl_window ) {
		return;
	}

	SDL_SetWindowTitle( sdl_window, "Return to Castle Wolfenstein - Renderer2 init OK" );
	glViewport( 0, 0, r2.glConfig.vidWidth, r2.glConfig.vidHeight );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_DEPTH_TEST );
	glClearColor( 0.85f, 0.10f, 0.18f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	SDL_GL_SwapWindow( sdl_window );

	glClearColor( 0.08f, 0.18f, 0.85f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	SDL_GL_SwapWindow( sdl_window );
}

void R2_GL_SetStatusTitle( const char *title ) {
	int now;

	if ( !r2_debugStatus || !r2_debugStatus->integer ) {
		return;
	}
	if ( !title || !title[0] || !Q_stricmp( r2_lastStatusTitle, title ) ) {
		return;
	}
	now = r2_ri.Milliseconds();
	if ( r2.lastTitleUpdateMs && now - r2.lastTitleUpdateMs < 500 ) {
		return;
	}
	if ( sdl_window && title && title[0] ) {
		Q_strncpyz( r2_lastStatusTitle, title, sizeof( r2_lastStatusTitle ) );
		SDL_SetWindowTitle( sdl_window, title );
		r2.lastTitleUpdateMs = now;
	}
}

static SDL_DisplayID R2_GL_GetDisplayByIndex( int index, int *displayCount ) {
	SDL_DisplayID *displays;
	SDL_DisplayID display = 0;
	int count = 0;

	displays = SDL_GetDisplays( &count );
	if ( displayCount ) {
		*displayCount = count;
	}
	if ( displays && count > 0 ) {
		if ( index < 0 || index >= count ) {
			index = 0;
		}
		display = displays[index];
	}
	if ( displays ) {
		SDL_free( displays );
	}
	if ( !display ) {
		display = SDL_GetPrimaryDisplay();
	}
	return display;
}

static qboolean R2_GL_GetModeInfo( int *width, int *height, float *aspect ) {
	int mode = r2_mode->integer;

	if ( mode == -1 ) {
		int customWidth = r2_customWidth->integer;
		int customHeight = r2_customHeight->integer;
		if ( customWidth < 320 ) customWidth = 320;
		if ( customHeight < 240 ) customHeight = 240;
		if ( customWidth > 8192 ) customWidth = 8192;
		if ( customHeight > 8192 ) customHeight = 8192;

		*width = customWidth;
		*height = customHeight;
		*aspect = r2_customAspect->value > 0.01f ? r2_customAspect->value : (float)customWidth / (float)customHeight;
		return qtrue;
	}

	if ( mode < 0 || mode >= (int)( sizeof( r2_vidModes ) / sizeof( r2_vidModes[0] ) ) ) {
		return qfalse;
	}

	*width = r2_vidModes[mode].width;
	*height = r2_vidModes[mode].height;
	*aspect = (float)*width / (float)*height;
	return qtrue;
}

static void R2_GL_RegisterCvars( void ) {
	r2_mode = r2_ri.Cvar_Get( "r_mode", "3", CVAR_ARCHIVE | CVAR_LATCH );
	r2_customWidth = r2_ri.Cvar_Get( "r_customwidth", "1600", CVAR_ARCHIVE | CVAR_LATCH );
	r2_customHeight = r2_ri.Cvar_Get( "r_customheight", "1024", CVAR_ARCHIVE | CVAR_LATCH );
	r2_customAspect = r2_ri.Cvar_Get( "r_customaspect", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r2_fullscreen = r2_ri.Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r2_borderless = r2_ri.Cvar_Get( "r_borderless", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r2_resizableWindow = r2_ri.Cvar_Get( "r_resizableWindow", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r2_sdlDpiScale = r2_ri.Cvar_Get( "r_sdlDpiScale", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r2_monitor = r2_ri.Cvar_Get( "r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r2_colorBits = r2_ri.Cvar_Get( "r_colorbits", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r2_depthBits = r2_ri.Cvar_Get( "r_depthbits", "24", CVAR_ARCHIVE | CVAR_LATCH );
	r2_stencilBits = r2_ri.Cvar_Get( "r_stencilbits", "8", CVAR_ARCHIVE | CVAR_LATCH );
	r2_swapInterval = r2_ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE );
	r2_glMajor = r2_ri.Cvar_Get( "r2_glMajor", "4", CVAR_ARCHIVE | CVAR_LATCH );
	r2_glMinor = r2_ri.Cvar_Get( "r2_glMinor", "6", CVAR_ARCHIVE | CVAR_LATCH );
	r2_glDebug = r2_ri.Cvar_Get( "r2_glDebugContext", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r2_debugTriangle = r2_ri.Cvar_Get( "r2_debugTriangle", "0", CVAR_ARCHIVE );
	r2_debugClear = r2_ri.Cvar_Get( "r2_debugClear", "0", CVAR_ARCHIVE );
	r2_debugStatus = r2_ri.Cvar_Get( "r2_debugStatus", "0", CVAR_ARCHIVE );
	r2_shaderTcMod = r2_ri.Cvar_Get( "r2_shaderTcMod", "0", CVAR_ARCHIVE );
	r2_worldTcMod = r2_ri.Cvar_Get( "r2_worldTcMod", "1", CVAR_ARCHIVE );
	r2_worldCloudStages = r2_ri.Cvar_Get( "r2_worldCloudStages", "1", CVAR_ARCHIVE );
	r2_worldCloudStrength = r2_ri.Cvar_Get( "r2_worldCloudStrength", "1.0", CVAR_ARCHIVE );
	r2_worldTerrainCollapseStages = r2_ri.Cvar_Get( "r2_worldTerrainCollapseStages", "1", CVAR_ARCHIVE );
	r2_worldTerrainInvertAlpha = r2_ri.Cvar_Get( "r2_worldTerrainInvertAlpha", "0", CVAR_ARCHIVE );
	r2_worldTerrainDebugBlend = r2_ri.Cvar_Get( "r2_worldTerrainDebugBlend", "0", CVAR_ARCHIVE );
	r2_logMissingTextures = r2_ri.Cvar_Get( "r2_logMissingTextures", "0", CVAR_ARCHIVE );
}

static void R2_GL_DestroyContext( void ) {
	if ( sdl_gl_context ) {
		SDL_GL_MakeCurrent( sdl_window, NULL );
		SDL_GL_DestroyContext( sdl_gl_context );
		sdl_gl_context = NULL;
	}
	if ( sdl_window ) {
		SDL_DestroyWindow( sdl_window );
		sdl_window = NULL;
	}
	R2_GL_UpdateNativeWindowHandle();
}

static qboolean R2_GL_CreateContextVersion( int major, int minor, int width, int height, SDL_DisplayID display ) {
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
	int colorBits = r2_colorBits->integer;
	int channelBits;

	if ( colorBits <= 0 || colorBits >= 32 ) {
		colorBits = 24;
	}
	channelBits = colorBits == 16 ? 5 : 8;

	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, major );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, minor );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, r2_glDebug->integer ? SDL_GL_CONTEXT_DEBUG_FLAG : 0 );
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, channelBits );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, channelBits );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, channelBits );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, r2_depthBits->integer );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, r2_stencilBits->integer );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );

	if ( !r2_fullscreen->integer ) {
		if ( r2_resizableWindow->integer ) {
			flags |= SDL_WINDOW_RESIZABLE;
		}
		if ( r2_borderless->integer ) {
			flags |= SDL_WINDOW_BORDERLESS;
		}
		if ( r2_sdlDpiScale->integer ) {
			flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
		}
	}

	sdl_window = SDL_CreateWindow( "Return to Castle Wolfenstein - Renderer2", width, height, flags );
	if ( !sdl_window ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: SDL_CreateWindow failed for OpenGL %d.%d: %s\n",
					  major, minor, SDL_GetError() );
		return qfalse;
	}

	SDL_SetWindowPosition( sdl_window, SDL_WINDOWPOS_CENTERED_DISPLAY( display ), SDL_WINDOWPOS_CENTERED_DISPLAY( display ) );
	if ( r2_fullscreen->integer ) {
		SDL_DisplayMode closestMode;
		if ( SDL_GetClosestFullscreenDisplayMode( display, width, height, 0.0f, true, &closestMode ) ) {
			SDL_SetWindowFullscreenMode( sdl_window, &closestMode );
		}
		if ( !SDL_SetWindowFullscreen( sdl_window, true ) ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: fullscreen failed: %s\n", SDL_GetError() );
			R2_GL_DestroyContext();
			return qfalse;
		}
	}

	SDL_ShowWindow( sdl_window );
	SDL_RaiseWindow( sdl_window );

	sdl_gl_context = SDL_GL_CreateContext( sdl_window );
	if ( !sdl_gl_context ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: SDL_GL_CreateContext failed for OpenGL %d.%d: %s\n",
					  major, minor, SDL_GetError() );
		R2_GL_DestroyContext();
		return qfalse;
	}

	if ( !SDL_GL_MakeCurrent( sdl_window, sdl_gl_context ) ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError() );
		R2_GL_DestroyContext();
		return qfalse;
	}

	R2_GL_UpdateNativeWindowHandle();
	return qtrue;
}

static qboolean R2_GL_CreateBestContext( int width, int height, SDL_DisplayID display ) {
	int requestedMajor = r2_glMajor->integer;
	int requestedMinor = r2_glMinor->integer;
	static const int fallbackVersions[][2] = {
		{ 4, 6 },
		{ 4, 5 },
		{ 4, 4 },
		{ 4, 3 },
		{ 4, 2 },
		{ 4, 1 },
		{ 4, 0 }
	};
	int i;

	if ( requestedMajor < 4 ) {
		requestedMajor = 4;
		requestedMinor = 0;
	}

	r2.requestedMajor = requestedMajor;
	r2.requestedMinor = requestedMinor;

	if ( R2_GL_CreateContextVersion( requestedMajor, requestedMinor, width, height, display ) ) {
		return qtrue;
	}

	for ( i = 0; i < (int)( sizeof( fallbackVersions ) / sizeof( fallbackVersions[0] ) ); ++i ) {
		if ( fallbackVersions[i][0] > requestedMajor ||
			 ( fallbackVersions[i][0] == requestedMajor && fallbackVersions[i][1] >= requestedMinor ) ) {
			continue;
		}
		if ( R2_GL_CreateContextVersion( fallbackVersions[i][0], fallbackVersions[i][1], width, height, display ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

qboolean R2_GL_Init( glconfig_t *config ) {
	SDL_DisplayID display;
	const GLubyte *vendor;
	const GLubyte *renderer;
	const GLubyte *version;
	int displayCount = 0;
	int displayIndex;
	int width;
	int height;
	int actualWindowWidth = 0;
	int actualWindowHeight = 0;
	int drawableWidth = 0;
	int drawableHeight = 0;
	int colorRed = 0;
	int colorGreen = 0;
	int colorBlue = 0;
	GLint glMajor = 0;
	GLint glMinor = 0;
	GLint maxTextureSize = 0;
	GLint profileMask = 0;
	float aspect;

	if ( r2.initialized ) {
		if ( config ) {
			*config = r2.glConfig;
		}
		return qtrue;
	}

	R2_GL_RegisterCvars();

	if ( !R2_GL_GetModeInfo( &width, &height, &aspect ) ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: invalid r_mode %d\n", r2_mode->integer );
		return qfalse;
	}

	SDL_SetHint( SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" );
	SDL_SetHint( SDL_HINT_MOUSE_AUTO_CAPTURE, "0" );
	SDL_SetHint( SDL_HINT_MOUSE_DPI_SCALE_CURSORS, "1" );
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1" );
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0" );

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) ) {
		if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
			r2_ri.Printf( PRINT_WARNING, "Renderer2: SDL video init failed: %s\n", SDL_GetError() );
			return qfalse;
		}
	}

	displayIndex = r2_monitor->integer;
	display = R2_GL_GetDisplayByIndex( displayIndex, &displayCount );
	if ( displayCount > 0 && ( displayIndex < 0 || displayIndex >= displayCount ) ) {
		displayIndex = 0;
		r2_ri.Cvar_Set( "r_monitor", "0" );
	}

	r2_ri.Printf( PRINT_ALL, "Renderer2: creating OpenGL %d.%d core context (%dx%d)\n",
				  r2_glMajor->integer, r2_glMinor->integer, width, height );

	if ( !R2_GL_CreateBestContext( width, height, display ) ) {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		return qfalse;
	}

	SDL_GL_SetSwapInterval( r2_swapInterval->integer );

	glGetIntegerv( GL_MAJOR_VERSION, &glMajor );
	glGetIntegerv( GL_MINOR_VERSION, &glMinor );
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &maxTextureSize );
	glGetIntegerv( GL_CONTEXT_PROFILE_MASK, &profileMask );

	if ( glMajor < 4 ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: OpenGL 4 required, got %d.%d\n", glMajor, glMinor );
		R2_GL_Shutdown();
		return qfalse;
	}
	if ( !( profileMask & GL_CONTEXT_CORE_PROFILE_BIT ) ) {
		r2_ri.Printf( PRINT_WARNING, "Renderer2: core profile required\n" );
		R2_GL_Shutdown();
		return qfalse;
	}
	if ( !R2_GL_LoadCoreProcs() ) {
		R2_GL_Shutdown();
		return qfalse;
	}
	if ( !R2_GL_CreateColorPipeline() ) {
		R2_GL_Shutdown();
		return qfalse;
	}
	if ( !R2_GL_CreateModelPipeline() ) {
		R2_GL_Shutdown();
		return qfalse;
	}
	if ( !R2_GL_CreateWorldPipeline() ) {
		R2_GL_Shutdown();
		return qfalse;
	}
	if ( !R2_GL_CreateBuiltInTextures() ) {
		R2_GL_Shutdown();
		return qfalse;
	}

	SDL_GetWindowSize( sdl_window, &actualWindowWidth, &actualWindowHeight );
	if ( SDL_GetWindowSizeInPixels( sdl_window, &drawableWidth, &drawableHeight ) &&
		 drawableWidth > 0 && drawableHeight > 0 ) {
		width = drawableWidth;
		height = drawableHeight;
		aspect = (float)drawableWidth / (float)drawableHeight;
	}

	memset( &r2.glConfig, 0, sizeof( r2.glConfig ) );
	vendor = glGetString( GL_VENDOR );
	renderer = glGetString( GL_RENDERER );
	version = glGetString( GL_VERSION );
	Q_strncpyz( r2.glConfig.vendor_string, vendor ? (const char *)vendor : "unknown", sizeof( r2.glConfig.vendor_string ) );
	Q_strncpyz( r2.glConfig.renderer_string, renderer ? (const char *)renderer : "unknown", sizeof( r2.glConfig.renderer_string ) );
	Q_strncpyz( r2.glConfig.version_string, version ? (const char *)version : "unknown", sizeof( r2.glConfig.version_string ) );
	Q_strncpyz( r2.glConfig.extensions_string, "renderer2 uses glGetStringi for core-profile extensions", sizeof( r2.glConfig.extensions_string ) );

	SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &colorRed );
	SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &colorGreen );
	SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &colorBlue );
	SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &r2.glConfig.depthBits );
	SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &r2.glConfig.stencilBits );

	r2.glConfig.maxTextureSize = maxTextureSize;
	r2.glConfig.maxActiveTextures = 1;
	r2.glConfig.colorBits = colorRed + colorGreen + colorBlue;
	r2.glConfig.driverType = GLDRV_ICD;
	r2.glConfig.hardwareType = GLHW_GENERIC;
	r2.glConfig.textureCompression = TC_NONE;
	r2.glConfig.vidWidth = width;
	r2.glConfig.vidHeight = height;
	r2.glConfig.windowAspect = aspect;
	r2.glConfig.isFullscreen = r2_fullscreen->integer ? qtrue : qfalse;
	r2.glConfig.stereoEnabled = qfalse;
	r2.glConfig.smpActive = qfalse;

	r2.contextMajor = glMajor;
	r2.contextMinor = glMinor;
	r2.initialized = qtrue;
	R2_GL_SetColor( NULL );

	r2_ri.Printf( PRINT_ALL, "Renderer2: GL_VENDOR: %s\n", r2.glConfig.vendor_string );
	r2_ri.Printf( PRINT_ALL, "Renderer2: GL_RENDERER: %s\n", r2.glConfig.renderer_string );
	r2_ri.Printf( PRINT_ALL, "Renderer2: GL_VERSION: %s\n", r2.glConfig.version_string );
	r2_ri.Printf( PRINT_ALL, "Renderer2: drawable %dx%d, window %dx%d, color/depth/stencil %d/%d/%d\n",
				  r2.glConfig.vidWidth, r2.glConfig.vidHeight,
				  actualWindowWidth, actualWindowHeight,
				  r2.glConfig.colorBits, r2.glConfig.depthBits, r2.glConfig.stencilBits );

	IN_Shutdown();
	IN_Init();

	if ( config ) {
		*config = r2.glConfig;
	}

	R2_GL_PresentInitFrame();
	return qtrue;
}

void R2_GL_Shutdown( void ) {
	if ( !r2.initialized && !sdl_window && !sdl_gl_context ) {
		return;
	}

	r2_ri.Printf( PRINT_ALL, "Renderer2: shutting down OpenGL context\n" );
	IN_Shutdown();
	SDLGL_RestoreGamma();
	R2_GL_DeleteTextures();
	R2_GL_DestroyPipeline();
	R2_GL_DestroyContext();
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	memset( &r2, 0, sizeof( r2 ) );
	memset( &r2gl, 0, sizeof( r2gl ) );
}

void R2_GL_BeginFrame( stereoFrame_t stereoFrame ) {
	int now;
	float r = 0.025f;
	float g = 0.035f;
	float b = 0.045f;

	(void)stereoFrame;
	if ( !r2.initialized ) {
		return;
	}

	if ( r2.useFogClearColor ) {
		r = r2.fogClearColor[0];
		g = r2.fogClearColor[1];
		b = r2.fogClearColor[2];
	}

	if ( r2_debugClear && r2_debugClear->integer ) {
		float t;

		now = r2_ri.Milliseconds();
		t = (float)( now % 3000 ) / 3000.0f;

		if ( t < 0.25f ) {
			r = 0.85f;
			g = 0.12f + t * 2.0f;
			b = 0.12f;
		} else if ( t < 0.50f ) {
			r = 0.12f;
			g = 0.75f;
			b = 0.18f + ( t - 0.25f ) * 2.0f;
		} else if ( t < 0.75f ) {
			r = 0.10f + ( t - 0.50f ) * 2.2f;
			g = 0.20f;
			b = 0.90f;
		} else {
			r = 0.90f;
			g = 0.75f;
			b = 0.10f;
		}
	}

	glViewport( 0, 0, r2.glConfig.vidWidth, r2.glConfig.vidHeight );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_DEPTH_TEST );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glClearColor( r, g, b, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}

void R2_GL_EndFrame( int *frontEndMsec, int *backEndMsec ) {
	if ( frontEndMsec ) {
		*frontEndMsec = 0;
	}
	if ( backEndMsec ) {
		*backEndMsec = 0;
	}

	if ( !r2.initialized ) {
		return;
	}

	if ( r2_swapInterval && r2_swapInterval->modified ) {
		r2_swapInterval->modified = qfalse;
		SDL_GL_SetSwapInterval( r2_swapInterval->integer );
	}

	r2.frameCount++;

	if ( r2_debugTriangle && r2_debugTriangle->integer ) {
		if ( !r2_debugTriangleLogged ) {
			r2_debugTriangleLogged = qtrue;
			r2_ri.Printf( PRINT_ALL, "Renderer2: drawing debug triangle at EndFrame\n" );
		}
		R2_GL_DrawDiagnosticTriangle();
	}

	if ( sdl_window ) {
		SDL_GL_SwapWindow( sdl_window );
	}
}

void R2_GL_SetColor( const float *rgba ) {
	if ( rgba ) {
		r2.currentColor[0] = rgba[0];
		r2.currentColor[1] = rgba[1];
		r2.currentColor[2] = rgba[2];
		r2.currentColor[3] = rgba[3];
	} else {
		r2.currentColor[0] = 1.0f;
		r2.currentColor[1] = 1.0f;
		r2.currentColor[2] = 1.0f;
		r2.currentColor[3] = 1.0f;
	}
}

static float R2_GL_ScreenToNdcX( float x ) {
	if ( r2.glConfig.vidWidth <= 0 ) {
		return -1.0f;
	}
	return ( x / (float)r2.glConfig.vidWidth ) * 2.0f - 1.0f;
}

static float R2_GL_ScreenToNdcY( float y ) {
	if ( r2.glConfig.vidHeight <= 0 ) {
		return 1.0f;
	}
	return 1.0f - ( y / (float)r2.glConfig.vidHeight ) * 2.0f;
}

static void R2_GL_SetVertexColor( r2ColorVertex_t *vertex, const float *color ) {
	vertex->r = color[0];
	vertex->g = color[1];
	vertex->b = color[2];
	vertex->a = color[3];
}

static void R2_GL_SetVertexTexCoord( r2ColorVertex_t *vertex, float s, float t ) {
	vertex->s = s;
	vertex->t = t;
}

void R2_GL_DrawStretchPic( float x, float y, float w, float h,
						   float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	float x0;
	float x1;
	float y0;
	float y1;
	r2ColorVertex_t vertices[6];

	if ( w == 0.0f || h == 0.0f ) {
		return;
	}

	x0 = R2_GL_ScreenToNdcX( x );
	x1 = R2_GL_ScreenToNdcX( x + w );
	y0 = R2_GL_ScreenToNdcY( y );
	y1 = R2_GL_ScreenToNdcY( y + h );

	vertices[0].x = x0; vertices[0].y = y0;
	vertices[1].x = x1; vertices[1].y = y0;
	vertices[2].x = x1; vertices[2].y = y1;
	vertices[3].x = x0; vertices[3].y = y0;
	vertices[4].x = x1; vertices[4].y = y1;
	vertices[5].x = x0; vertices[5].y = y1;

	R2_GL_SetVertexTexCoord( &vertices[0], s1, t1 );
	R2_GL_SetVertexTexCoord( &vertices[1], s2, t1 );
	R2_GL_SetVertexTexCoord( &vertices[2], s2, t2 );
	R2_GL_SetVertexTexCoord( &vertices[3], s1, t1 );
	R2_GL_SetVertexTexCoord( &vertices[4], s2, t2 );
	R2_GL_SetVertexTexCoord( &vertices[5], s1, t2 );

	R2_GL_SetVertexColor( &vertices[0], r2.currentColor );
	R2_GL_SetVertexColor( &vertices[1], r2.currentColor );
	R2_GL_SetVertexColor( &vertices[2], r2.currentColor );
	R2_GL_SetVertexColor( &vertices[3], r2.currentColor );
	R2_GL_SetVertexColor( &vertices[4], r2.currentColor );
	R2_GL_SetVertexColor( &vertices[5], r2.currentColor );

	R2_GL_DrawVertices( vertices, 6, hShader );
}

void R2_GL_DrawStretchPicGradient( float x, float y, float w, float h,
								   float s1, float t1, float s2, float t2,
								   qhandle_t hShader, const float *gradientColor, int gradientType ) {
	float x0;
	float x1;
	float y0;
	float y1;
	const float *c0 = r2.currentColor;
	const float *c1 = gradientColor ? gradientColor : r2.currentColor;
	r2ColorVertex_t vertices[6];

	if ( w == 0.0f || h == 0.0f ) {
		return;
	}

	x0 = R2_GL_ScreenToNdcX( x );
	x1 = R2_GL_ScreenToNdcX( x + w );
	y0 = R2_GL_ScreenToNdcY( y );
	y1 = R2_GL_ScreenToNdcY( y + h );

	vertices[0].x = x0; vertices[0].y = y0;
	vertices[1].x = x1; vertices[1].y = y0;
	vertices[2].x = x1; vertices[2].y = y1;
	vertices[3].x = x0; vertices[3].y = y0;
	vertices[4].x = x1; vertices[4].y = y1;
	vertices[5].x = x0; vertices[5].y = y1;

	R2_GL_SetVertexTexCoord( &vertices[0], s1, t1 );
	R2_GL_SetVertexTexCoord( &vertices[1], s2, t1 );
	R2_GL_SetVertexTexCoord( &vertices[2], s2, t2 );
	R2_GL_SetVertexTexCoord( &vertices[3], s1, t1 );
	R2_GL_SetVertexTexCoord( &vertices[4], s2, t2 );
	R2_GL_SetVertexTexCoord( &vertices[5], s1, t2 );

	if ( gradientType == 1 ) {
		R2_GL_SetVertexColor( &vertices[0], c0 );
		R2_GL_SetVertexColor( &vertices[1], c0 );
		R2_GL_SetVertexColor( &vertices[2], c1 );
		R2_GL_SetVertexColor( &vertices[3], c0 );
		R2_GL_SetVertexColor( &vertices[4], c1 );
		R2_GL_SetVertexColor( &vertices[5], c1 );
	} else {
		R2_GL_SetVertexColor( &vertices[0], c0 );
		R2_GL_SetVertexColor( &vertices[1], c1 );
		R2_GL_SetVertexColor( &vertices[2], c1 );
		R2_GL_SetVertexColor( &vertices[3], c0 );
		R2_GL_SetVertexColor( &vertices[4], c1 );
		R2_GL_SetVertexColor( &vertices[5], c0 );
	}

	R2_GL_DrawVertices( vertices, 6, hShader );
}
