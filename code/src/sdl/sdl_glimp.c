/*
===========================================================================

SDL3 OpenGL backend for the test renderer target.

===========================================================================
*/

#include <stdio.h>
#include <string.h>

#include "sdl_local.h"
#include "../renderer/tr_local.h"
#include "../sys/platform/win32/sys_glw_win.h"
#include "../sys/core/sys_local.h"
#include "../client/cl_speedrun_imgui.h"

typedef enum {
	RSERR_OK,
	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE
} rserr_t;

void QGL_EnableLogging( qboolean enable );
qboolean QGL_Init( const char *dllname );
void QGL_Shutdown( void );

SDL_Window *sdl_window = NULL;
SDL_GLContext sdl_gl_context = NULL;

glwstate_t glw_state;

static cvar_t *r_sdlResizable;
static cvar_t *r_sdlDpiScale;

HANDLE renderCommandsEvent;
HANDLE renderCompletedEvent;
HANDLE renderActiveEvent;
HANDLE renderThreadHandle;
int renderThreadId;
static void ( *glimpRenderThread )( void );
static void *smpData;

static void SDLGL_UpdateNativeWindowHandle( void ) {
#ifdef _WIN32
	if ( sdl_window ) {
		SDL_PropertiesID props = SDL_GetWindowProperties( sdl_window );
		g_wv.hWnd = (HWND)SDL_GetPointerProperty( props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL );
		glw_state.hDC = (HDC)SDL_GetPointerProperty( props, SDL_PROP_WINDOW_WIN32_HDC_POINTER, NULL );
	} else {
		g_wv.hWnd = NULL;
		glw_state.hDC = NULL;
	}
#endif
}

static const char *SDLGL_StringOrEmpty( const GLubyte *value ) {
	return value ? (const char *)value : "";
}

static void SDLGL_CopyGLString( char *dest, int size, GLenum name ) {
	Q_strncpyz( dest, SDLGL_StringOrEmpty( qglGetString( name ) ), size );
}

static float SDLGL_GetDisplayScale( void ) {
	SDL_DisplayID display = 0;
	float scale = 1.0f;

	if ( sdl_window ) {
		display = SDL_GetDisplayForWindow( sdl_window );
	}
	if ( !display ) {
		display = SDL_GetPrimaryDisplay();
	}
	if ( display ) {
		scale = SDL_GetDisplayContentScale( display );
	}
	if ( scale < 1.0f ) {
		scale = 1.0f;
	}
	if ( scale > 3.0f ) {
		scale = 3.0f;
	}
	return scale;
}

static void SDLGL_InitExtensions( void ) {
	glConfig.textureCompression = TC_NONE;
	glConfig.textureEnvAddAvailable = qfalse;
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;
	qwglGetDeviceGammaRamp3DFX = NULL;
	qwglSetDeviceGammaRamp3DFX = NULL;
	qglPNTrianglesiATI = NULL;
	qglPNTrianglesfATI = NULL;
	glConfig.anisotropicAvailable = qfalse;
	glConfig.NVFogAvailable = qfalse;
	glConfig.NVFogMode = 0;

	if ( !r_allowExtensions->integer ) {
		ri.Printf( PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing SDL OpenGL extensions\n" );

	if ( SDL_GL_ExtensionSupported( "GL_EXT_texture_compression_s3tc" ) ) {
		if ( r_ext_compressed_textures->integer ) {
			glConfig.textureCompression = TC_EXT_COMP_S3TC;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
		}
	} else if ( SDL_GL_ExtensionSupported( "GL_S3_s3tc" ) ) {
		if ( r_ext_compressed_textures->integer ) {
			glConfig.textureCompression = TC_S3TC;
			ri.Printf( PRINT_ALL, "...using GL_S3_s3tc\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_S3_s3tc\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}

	if ( SDL_GL_ExtensionSupported( "GL_EXT_texture_env_add" ) ) {
		if ( r_ext_texture_env_add->integer ) {
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}

	if ( SDL_GL_ExtensionSupported( "WGL_EXT_swap_control" ) ) {
		r_swapInterval->modified = qtrue;
		ri.Printf( PRINT_ALL, "...using WGL_EXT_swap_control via SDL\n" );
	} else {
		ri.Printf( PRINT_ALL, "...WGL_EXT_swap_control not reported by SDL\n" );
	}

	if ( SDL_GL_ExtensionSupported( "GL_ARB_multitexture" ) ) {
		if ( r_ext_multitexture->integer ) {
			qglMultiTexCoord2fARB = (PFNGLMULTITEXCOORD2FARBPROC)SDL_GL_GetProcAddress( "glMultiTexCoord2fARB" );
			qglActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC)SDL_GL_GetProcAddress( "glActiveTextureARB" );
			qglClientActiveTextureARB = (PFNGLCLIENTACTIVETEXTUREARBPROC)SDL_GL_GetProcAddress( "glClientActiveTextureARB" );

			if ( qglActiveTextureARB ) {
				qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures );
				if ( glConfig.maxActiveTextures > 1 ) {
					ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
				} else {
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
	}

	if ( SDL_GL_ExtensionSupported( "GL_EXT_compiled_vertex_array" ) && glConfig.hardwareType != GLHW_RIVA128 ) {
		if ( r_ext_compiled_vertex_array->integer ) {
			qglLockArraysEXT = (void ( APIENTRY * )( int, int ))SDL_GL_GetProcAddress( "glLockArraysEXT" );
			qglUnlockArraysEXT = (void ( APIENTRY * )( void ))SDL_GL_GetProcAddress( "glUnlockArraysEXT" );
			if ( !qglLockArraysEXT || !qglUnlockArraysEXT ) {
				ri.Error( ERR_FATAL, "bad SDL_GL_GetProcAddress for GL_EXT_compiled_vertex_array" );
			}
			ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

	if ( SDL_GL_ExtensionSupported( "GL_ATI_pn_triangles" ) ) {
		if ( r_ext_ATI_pntriangles->integer ) {
			qglPNTrianglesiATI = (PFNGLPNTRIANGLESIATIPROC)SDL_GL_GetProcAddress( "glPNTrianglesiATI" );
			qglPNTrianglesfATI = (PFNGLPNTRIANGLESFATIPROC)SDL_GL_GetProcAddress( "glPNTrianglesfATI" );
			if ( !qglPNTrianglesiATI || !qglPNTrianglesfATI ) {
				ri.Error( ERR_FATAL, "bad SDL_GL_GetProcAddress for GL_ATI_pn_triangles" );
			}
			ri.Printf( PRINT_ALL, "...using GL_ATI_pn_triangles\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_ATI_pn_triangles\n" );
			ri.Cvar_Set( "r_ext_ATI_pntriangles", "0" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_ATI_pn_triangles not found\n" );
		ri.Cvar_Set( "r_ext_ATI_pntriangles", "0" );
	}

	if ( SDL_GL_ExtensionSupported( "GL_EXT_texture_filter_anisotropic" ) ) {
		glConfig.anisotropicAvailable = qtrue;
		qglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.maxAnisotropy );
		ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic\n" );
	} else {
		glConfig.anisotropicAvailable = qfalse;
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not found\n" );
		ri.Cvar_Set( "r_ext_texture_filter_anisotropic", "0" );
	}

	if ( SDL_GL_ExtensionSupported( "GL_NV_fog_distance" ) ) {
		if ( r_ext_NV_fog_dist->integer ) {
			glConfig.NVFogAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_NV_fog_distance\n" );
		} else {
			ri.Printf( PRINT_ALL, "...ignoring GL_NV_fog_distance\n" );
			ri.Cvar_Set( "r_ext_NV_fog_dist", "0" );
		}
	} else {
		ri.Printf( PRINT_ALL, "...GL_NV_fog_distance not found\n" );
		ri.Cvar_Set( "r_ext_NV_fog_dist", "0" );
	}
}

static void SDLGL_DestroyWindow( void ) {
	if ( sdl_gl_context ) {
		SDL_GL_MakeCurrent( sdl_window, NULL );
		SDL_GL_DestroyContext( sdl_gl_context );
		sdl_gl_context = NULL;
	}

	if ( sdl_window ) {
		SDL_DestroyWindow( sdl_window );
		sdl_window = NULL;
	}

	SDLGL_UpdateNativeWindowHandle();
}

static rserr_t SDLGL_SetMode( int mode, qboolean fullscreen ) {
	SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
	int windowWidth;
	int windowHeight;
	int colorBits;
	int depthBits;
	int stencilBits;
	int channelBits;
	int realRed = 0;
	int realGreen = 0;
	int realBlue = 0;

	if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode ) ) {
		return RSERR_INVALID_MODE;
	}

	windowWidth = glConfig.vidWidth;
	windowHeight = glConfig.vidHeight;

	ri.Printf( PRINT_ALL, "...setting SDL mode %d: %d %d %s\n",
			   mode, windowWidth, windowHeight, fullscreen ? "fullscreen" : "windowed" );

	SDLGL_DestroyWindow();
	SDL_GL_ResetAttributes();

	colorBits = r_colorbits->integer;
	if ( colorBits <= 0 || colorBits >= 32 ) {
		colorBits = 24;
	}
	depthBits = r_depthbits->integer ? r_depthbits->integer : 24;
	stencilBits = r_stencilbits->integer;
	channelBits = colorBits == 16 ? 5 : 8;

	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, channelBits );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, channelBits );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, channelBits );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, depthBits );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, stencilBits );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );

	if ( !fullscreen ) {
		flags |= SDL_WINDOW_RESIZABLE;
		if ( r_sdlDpiScale && r_sdlDpiScale->integer ) {
			flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
		}
	}

	if ( fullscreen ) {
		flags |= SDL_WINDOW_FULLSCREEN;
	}

	sdl_window = SDL_CreateWindow( "Return to Castle Wolfenstein", windowWidth, windowHeight, flags );
	if ( !sdl_window ) {
		ri.Printf( PRINT_ALL, "SDL_CreateWindow failed: %s\n", SDL_GetError() );
		return fullscreen ? RSERR_INVALID_FULLSCREEN : RSERR_INVALID_MODE;
	}

	if ( !fullscreen ) {
		SDL_SetWindowPosition( sdl_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED );
	}
	SDL_RaiseWindow( sdl_window );

	sdl_gl_context = SDL_GL_CreateContext( sdl_window );
	if ( !sdl_gl_context ) {
		ri.Printf( PRINT_ALL, "SDL_GL_CreateContext failed: %s\n", SDL_GetError() );
		SDLGL_DestroyWindow();
		return fullscreen ? RSERR_INVALID_FULLSCREEN : RSERR_INVALID_MODE;
	}

	if ( !SDL_GL_MakeCurrent( sdl_window, sdl_gl_context ) ) {
		ri.Printf( PRINT_ALL, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError() );
		SDLGL_DestroyWindow();
		return fullscreen ? RSERR_INVALID_FULLSCREEN : RSERR_INVALID_MODE;
	}

	{
		int actualWindowWidth = windowWidth;
		int actualWindowHeight = windowHeight;
		int pixelWidth = 0;
		int pixelHeight = 0;

		SDL_GetWindowSize( sdl_window, &actualWindowWidth, &actualWindowHeight );
		if ( SDL_GetWindowSizeInPixels( sdl_window, &pixelWidth, &pixelHeight ) &&
			 pixelWidth > 0 && pixelHeight > 0 ) {
			glConfig.vidWidth = pixelWidth;
			glConfig.vidHeight = pixelHeight;
			glConfig.windowAspect = (float)pixelWidth / (float)pixelHeight;
			ri.Printf( PRINT_ALL, "...SDL window %dx%d, drawable %dx%d, dpi scale %.2f/%.2f (display %.2f)\n",
				actualWindowWidth, actualWindowHeight, pixelWidth, pixelHeight,
				actualWindowWidth > 0 ? (float)pixelWidth / (float)actualWindowWidth : 1.0f,
				actualWindowHeight > 0 ? (float)pixelHeight / (float)actualWindowHeight : 1.0f,
				SDLGL_GetDisplayScale() );
		}
	}

	SDL_GL_SetSwapInterval( r_swapInterval->integer );

	SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &realRed );
	SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &realGreen );
	SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &realBlue );
	SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &glConfig.depthBits );
	SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &glConfig.stencilBits );

	glConfig.colorBits = realRed + realGreen + realBlue;
	glConfig.isFullscreen = fullscreen;
	glConfig.stereoEnabled = qfalse;
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	g_wv.activeApp = qtrue;
	g_wv.isMinimized = qfalse;
	SDLGL_UpdateNativeWindowHandle();
	glConfig.deviceSupportsGamma = SDLGL_InitGammaSupport();

	ri.Printf( PRINT_ALL, "Using %d color bits, %d depth, %d stencil display.\n",
			   glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );

	qglClearColor( 0, 0, 0, 1 );
	qglClear( GL_COLOR_BUFFER_BIT );
	SDL_GL_SwapWindow( sdl_window );

	return RSERR_OK;
}

static qboolean SDLGL_StartDriverAndSetMode( int mode, qboolean fullscreen ) {
	rserr_t err;

	SDL_SetHint( SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" );
	SDL_SetHint( SDL_HINT_MOUSE_AUTO_CAPTURE, "0" );
	SDL_SetHint( SDL_HINT_MOUSE_DPI_SCALE_CURSORS, "1" );
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1" );
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0" );
	SDL_SetHint( SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0" );
	SDL_SetHint( SDL_HINT_FORCE_RAISEWINDOW, "0" );

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) ) {
		if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
			ri.Printf( PRINT_ALL, "SDL_InitSubSystem( SDL_INIT_VIDEO ) failed: %s\n", SDL_GetError() );
			return qfalse;
		}
		ri.Printf( PRINT_ALL, "SDL video driver: %s\n", SDL_GetCurrentVideoDriver() );
	}

	err = SDLGL_SetMode( mode, fullscreen );
	switch ( err )
	{
	case RSERR_OK:
		return qtrue;
	case RSERR_INVALID_FULLSCREEN:
		ri.Printf( PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n" );
		return qfalse;
	case RSERR_INVALID_MODE:
	default:
		ri.Printf( PRINT_ALL, "...WARNING: could not set mode %d\n", mode );
		return qfalse;
	}
}

void GLimp_Init( void ) {
	char rendererBuffer[1024];
	cvar_t *lastValidRenderer;

	ri.Printf( PRINT_ALL, "Initializing SDL3 OpenGL subsystem\n" );

	r_sdlResizable = ri.Cvar_Get( "r_sdlResizable", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_sdlDpiScale = ri.Cvar_Get( "r_sdlDpiScale", "1", CVAR_ARCHIVE | CVAR_LATCH );
	lastValidRenderer = ri.Cvar_Get( "r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );

	if ( !QGL_Init( OPENGL_DRIVER_NAME ) ) {
		ri.Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL32 through QGL\n" );
	}

	if ( !SDLGL_StartDriverAndSetMode( r_mode->integer, r_fullscreen->integer ) ) {
		if ( r_fullscreen->integer && SDLGL_StartDriverAndSetMode( r_mode->integer, qfalse ) ) {
			ri.Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		} else if ( r_mode->integer != 3 && SDLGL_StartDriverAndSetMode( 3, qfalse ) ) {
			ri.Cvar_Set( "r_mode", "3" );
			r_mode->modified = qfalse;
		} else {
			QGL_Shutdown();
			ri.Error( ERR_FATAL, "GLimp_Init() - could not initialize SDL3 OpenGL subsystem\n" );
		}
	}

	SDLGL_CopyGLString( glConfig.vendor_string, sizeof( glConfig.vendor_string ), GL_VENDOR );
	SDLGL_CopyGLString( glConfig.renderer_string, sizeof( glConfig.renderer_string ), GL_RENDERER );
	SDLGL_CopyGLString( glConfig.version_string, sizeof( glConfig.version_string ), GL_VERSION );
	SDLGL_CopyGLString( glConfig.extensions_string, sizeof( glConfig.extensions_string ), GL_EXTENSIONS );

	Q_strncpyz( rendererBuffer, glConfig.renderer_string, sizeof( rendererBuffer ) );
	Q_strlwr( rendererBuffer );

	if ( Q_stricmp( lastValidRenderer->string, glConfig.renderer_string ) ) {
		ri.Cvar_Set( "r_textureMode", r_highQualityTextures->integer ? "GL_LINEAR_MIPMAP_LINEAR" : "GL_LINEAR_MIPMAP_NEAREST" );
		glConfig.hardwareType = GLHW_GENERIC;
	}

	if ( strstr( rendererBuffer, "rage pro" ) || strstr( rendererBuffer, "ragepro" ) ) {
		glConfig.hardwareType = GLHW_RAGEPRO;
	} else if ( strstr( rendererBuffer, "permedia2" ) ) {
		glConfig.hardwareType = GLHW_PERMEDIA2;
	} else if ( strstr( rendererBuffer, "riva 128" ) ) {
		glConfig.hardwareType = GLHW_RIVA128;
	}

	ri.Cvar_Set( "r_lastValidRenderer", glConfig.renderer_string );
	SDLGL_InitExtensions();
	IN_Shutdown();
	IN_Init();
}

void GLimp_Shutdown( void ) {
	if ( !sdl_window && !sdl_gl_context ) {
		return;
	}

	ri.Printf( PRINT_ALL, "Shutting down SDL3 OpenGL subsystem\n" );
	IN_Shutdown();
	SDLGL_RestoreGamma();

	if ( glw_state.log_fp ) {
		fclose( glw_state.log_fp );
		glw_state.log_fp = NULL;
	}

	SDLGL_DestroyWindow();
	QGL_Shutdown();
	SDL_QuitSubSystem( SDL_INIT_VIDEO );

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glState, 0, sizeof( glState ) );
}

void GLimp_EndFrame( void ) {
	if ( r_swapInterval->modified ) {
		r_swapInterval->modified = qfalse;
		SDL_GL_SetSwapInterval( r_swapInterval->integer );
	}

	CL_SpeedrunImGui_Draw();

	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 && sdl_window ) {
		SDL_GL_SwapWindow( sdl_window );
	}

	QGL_EnableLogging( r_logFile->integer );
}

void GLimp_LogComment( char *comment ) {
	if ( glw_state.log_fp ) {
		fprintf( glw_state.log_fp, "%s", comment );
	}
}

static DWORD WINAPI GLimp_RenderThreadWrapper( LPVOID data ) {
	(void)data;
	glimpRenderThread();
	SDL_GL_MakeCurrent( sdl_window, NULL );
	return 0;
}

qboolean GLimp_SpawnRenderThread( void ( *function )( void ) ) {
	renderCommandsEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	renderCompletedEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	renderActiveEvent = CreateEvent( NULL, TRUE, FALSE, NULL );

	glimpRenderThread = function;
	renderThreadHandle = CreateThread( NULL, 0, GLimp_RenderThreadWrapper, NULL, 0, (LPDWORD)&renderThreadId );
	return renderThreadHandle ? qtrue : qfalse;
}

void *GLimp_RendererSleep( void ) {
	void *data;

	SDL_GL_MakeCurrent( sdl_window, NULL );
	ResetEvent( renderActiveEvent );
	SetEvent( renderCompletedEvent );
	WaitForSingleObject( renderCommandsEvent, INFINITE );
	SDL_GL_MakeCurrent( sdl_window, sdl_gl_context );
	ResetEvent( renderCompletedEvent );
	ResetEvent( renderCommandsEvent );
	data = smpData;
	SetEvent( renderActiveEvent );
	return data;
}

void GLimp_FrontEndSleep( void ) {
	WaitForSingleObject( renderCompletedEvent, INFINITE );
	SDL_GL_MakeCurrent( sdl_window, sdl_gl_context );
}

void GLimp_WakeRenderer( void *data ) {
	smpData = data;
	SDL_GL_MakeCurrent( sdl_window, NULL );
	SetEvent( renderCommandsEvent );
	WaitForSingleObject( renderActiveEvent, INFINITE );
}
