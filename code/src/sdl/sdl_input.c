/*
===========================================================================

SDL3 input backend.

This is intentionally structured after RealRTCW's SDL input layer instead of
the legacy Win32 path.  A few key/gamepad pieces are adapted to this older SP
codebase, which still uses Sys_QueEvent and the original <256 key table.

===========================================================================
*/

#include <stdlib.h>
#include <string.h>

#include "sdl_local.h"
#include "../client/client.h"
#include "../client/cl_speedrun_imgui.h"
#include "../sys/core/sys_local.h"

static cvar_t *in_keyboardDebug;
static cvar_t *in_mouse;
static cvar_t *in_nograb;
static cvar_t *in_sdlForceGrab;
static cvar_t *in_sdlRequireFocus;
static cvar_t *in_sdlMouseDebug;
static cvar_t *in_joystick;
static cvar_t *in_joystickThreshold;
static cvar_t *in_joystickNo;
static cvar_t *in_joystickUseAnalog;

static SDL_Joystick *stick = NULL;

static qboolean mouseAvailable = qfalse;
static qboolean mouseActive = qfalse;
static int vidRestartTime = 0;
static int ignoreResizeUntil = 0;
static int in_eventTime = 0;
static int mouseDebugEvents = 0;
static int mouseDebugState = -1;
static int systemShortcutUntil = 0;

#define CTRL( a ) ( (a) - 'a' + 1 )

static void IN_DeactivateMouseInternal( qboolean isFullscreen );

qboolean IN_IsPhysicalSpaceDown( void ) {
	const Uint8 *keyboardState;
	qboolean down = qfalse;

	keyboardState = (const Uint8 *)SDL_GetKeyboardState( NULL );
	if ( keyboardState && keyboardState[SDL_SCANCODE_SPACE] ) {
		down = qtrue;
	}
#ifdef _WIN32
	if ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) {
		down = qtrue;
	}
#endif
	return down;
}

static void IN_UpdatePhysicalSpaceState( void ) {
	if ( IN_IsPhysicalSpaceDown() ) {
		CL_SetPhysicalSpaceState( qtrue );
	} else {
		CL_ClearPhysicalSpaceHold();
	}
}

static void IN_PrintKey( const SDL_KeyboardEvent *event, keyNum_t key, qboolean down ) {
	Com_Printf( "%s Scancode: 0x%02x(%s) Sym: 0x%02x(%s) Q:%s\n",
		down ? "+" : " ",
		event->scancode, SDL_GetScancodeName( event->scancode ),
		event->key, SDL_GetKeyName( event->key ),
		Key_KeynumToString( key, qtrue ) );
}

static keyNum_t IN_TranslateSDLToQ3Key( const SDL_KeyboardEvent *event, qboolean down ) {
	keyNum_t key = 0;

	if ( event->scancode == SDL_SCANCODE_GRAVE ) {
		/* Console key should be physical-layout stable.  Some keyboards/layouts
		   report this key as section/degree/etc instead of ` or ~. */
		key = '`';
	} else if ( event->scancode >= SDL_SCANCODE_1 && event->scancode <= SDL_SCANCODE_0 ) {
		if ( event->scancode == SDL_SCANCODE_0 ) {
			key = '0';
		} else {
			key = (keyNum_t)( '1' + event->scancode - SDL_SCANCODE_1 );
		}
	} else if ( event->key >= SDLK_SPACE && event->key < SDLK_DELETE ) {
		key = (keyNum_t)event->key;
		if ( key >= 'A' && key <= 'Z' ) {
			key += 'a' - 'A';
		}
	} else {
		switch ( event->key )
		{
		case SDLK_PAGEUP:       key = K_PGUP; break;
		case SDLK_KP_9:         key = K_KP_PGUP; break;
		case SDLK_PAGEDOWN:     key = K_PGDN; break;
		case SDLK_KP_3:         key = K_KP_PGDN; break;
		case SDLK_KP_7:         key = K_KP_HOME; break;
		case SDLK_HOME:         key = K_HOME; break;
		case SDLK_KP_1:         key = K_KP_END; break;
		case SDLK_END:          key = K_END; break;
		case SDLK_KP_4:         key = K_KP_LEFTARROW; break;
		case SDLK_LEFT:         key = K_LEFTARROW; break;
		case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
		case SDLK_RIGHT:        key = K_RIGHTARROW; break;
		case SDLK_KP_2:         key = K_KP_DOWNARROW; break;
		case SDLK_DOWN:         key = K_DOWNARROW; break;
		case SDLK_KP_8:         key = K_KP_UPARROW; break;
		case SDLK_UP:           key = K_UPARROW; break;
		case SDLK_ESCAPE:       key = K_ESCAPE; break;
		case SDLK_KP_ENTER:     key = K_KP_ENTER; break;
		case SDLK_RETURN:       key = K_ENTER; break;
		case SDLK_TAB:          key = K_TAB; break;
		case SDLK_F1:           key = K_F1; break;
		case SDLK_F2:           key = K_F2; break;
		case SDLK_F3:           key = K_F3; break;
		case SDLK_F4:           key = K_F4; break;
		case SDLK_F5:           key = K_F5; break;
		case SDLK_F6:           key = K_F6; break;
		case SDLK_F7:           key = K_F7; break;
		case SDLK_F8:           key = K_F8; break;
		case SDLK_F9:           key = K_F9; break;
		case SDLK_F10:          key = K_F10; break;
		case SDLK_F11:          key = K_F11; break;
		case SDLK_F12:          key = K_F12; break;
		case SDLK_F13:          key = K_F13; break;
		case SDLK_F14:          key = K_F14; break;
		case SDLK_F15:          key = K_F15; break;
		case SDLK_BACKSPACE:    key = K_BACKSPACE; break;
		case SDLK_KP_PERIOD:    key = K_KP_DEL; break;
		case SDLK_DELETE:       key = K_DEL; break;
		case SDLK_PAUSE:        key = K_PAUSE; break;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:       key = K_SHIFT; break;
		case SDLK_LCTRL:
		case SDLK_RCTRL:        key = K_CTRL; break;
		case SDLK_LGUI:
		case SDLK_RGUI:         key = K_COMMAND; break;
		case SDLK_LALT:
		case SDLK_RALT:         key = K_ALT; break;
		case SDLK_KP_5:         key = K_KP_5; break;
		case SDLK_INSERT:       key = K_INS; break;
		case SDLK_KP_0:         key = K_KP_INS; break;
		case SDLK_KP_MULTIPLY:  key = K_KP_STAR; break;
		case SDLK_KP_PLUS:      key = K_KP_PLUS; break;
		case SDLK_KP_MINUS:     key = K_KP_MINUS; break;
		case SDLK_KP_DIVIDE:    key = K_KP_SLASH; break;
		case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK; break;
		case SDLK_CAPSLOCK:     key = K_CAPSLOCK; break;
		case SDLK_POWER:        key = K_POWER; break;
		default:                key = 0; break;
		}
	}

	if ( in_keyboardDebug && in_keyboardDebug->integer ) {
		IN_PrintKey( event, key, down );
	}

	return key;
}

static void IN_GobbleMotionEvents( void ) {
	SDL_Event dummy[1];
	int val;

	SDL_PumpEvents();
	while ( ( val = SDL_PeepEvents( dummy, 1, SDL_GETEVENT,
		SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_MOTION ) ) > 0 ) {
	}

	if ( val < 0 ) {
		Com_Printf( "IN_GobbleMotionEvents failed: %s\n", SDL_GetError() );
	}
}

static void IN_DebugMouseState( int state, const char *reason, qboolean isFullscreen, Uint64 flags ) {
	if ( !in_sdlMouseDebug || !in_sdlMouseDebug->integer || !sdl_window ) {
		return;
	}
	if ( mouseDebugState == state ) {
		return;
	}
	mouseDebugState = state;
	Com_Printf( "SDL mouse %s: %s fullscreen:%d cls:%d catcher:%d flags:%llu rel:%d grab:%d\n",
		state == 0 ? "active" : "inactive",
		reason,
		isFullscreen ? 1 : 0,
		cls.state,
		Key_GetCatcher(),
		(unsigned long long)flags,
		SDL_GetWindowRelativeMouseMode( sdl_window ) ? 1 : 0,
		SDL_GetWindowMouseGrab( sdl_window ) ? 1 : 0 );
}

static qboolean IN_ImGuiOwnsInput( void ) {
	return ( CL_SpeedrunImGui_IsOpen() || CL_SpeedrunImGui_IsRaceChatOpen() ) ? qtrue : qfalse;
}

static void IN_SuspendMouseForSystemShortcut( int msec ) {
	int until = Sys_Milliseconds() + msec;

	if ( until > systemShortcutUntil ) {
		systemShortcutUntil = until;
	}
	IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
}

static qboolean IN_IsSystemShortcutKey( const SDL_KeyboardEvent *event ) {
	if ( event->key == SDLK_LGUI || event->key == SDLK_RGUI || event->key == SDLK_PRINTSCREEN ) {
		return qtrue;
	}

	if ( event->mod & SDL_KMOD_GUI ) {
		return qtrue;
	}

	return qfalse;
}

static void IN_GetWindowSize( int *width, int *height ) {
	int w = cls.glconfig.vidWidth;
	int h = cls.glconfig.vidHeight;

	if ( sdl_window ) {
		SDL_GetWindowSize( sdl_window, &w, &h );
	}
	if ( width ) {
		*width = w;
	}
	if ( height ) {
		*height = h;
	}
}

static void IN_WindowToRenderCoords( float *x, float *y ) {
	int windowWidth;
	int windowHeight;

	if ( !x || !y || !sdl_window || cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 ) {
		return;
	}

	IN_GetWindowSize( &windowWidth, &windowHeight );
	if ( windowWidth > 0 && windowHeight > 0 ) {
		*x = *x * (float)cls.glconfig.vidWidth / (float)windowWidth;
		*y = *y * (float)cls.glconfig.vidHeight / (float)windowHeight;
	}
}

static void IN_ActivateMouse( qboolean isFullscreen ) {
	if ( !mouseAvailable || !SDL_WasInit( SDL_INIT_VIDEO ) || !sdl_window ) {
		return;
	}

	if ( !mouseActive ) {
		if ( !SDL_SetWindowMouseGrab( sdl_window, true ) && in_sdlMouseDebug && in_sdlMouseDebug->integer ) {
			Com_Printf( "SDL mouse grab failed: %s\n", SDL_GetError() );
		}
		if ( !SDL_SetWindowRelativeMouseMode( sdl_window, true ) && in_sdlMouseDebug && in_sdlMouseDebug->integer ) {
			Com_Printf( "SDL relative mouse failed: %s\n", SDL_GetError() );
		}
		SDL_HideCursor();
		IN_GobbleMotionEvents();
	}

	if ( !isFullscreen && in_sdlForceGrab && in_sdlForceGrab->integer && in_nograb && in_nograb->modified ) {
		in_nograb->modified = qfalse;
	}

	if ( !isFullscreen && in_nograb && !in_sdlForceGrab->integer && ( in_nograb->modified || !mouseActive ) ) {
		if ( in_nograb->integer ) {
			SDL_SetWindowRelativeMouseMode( sdl_window, false );
			SDL_SetWindowMouseGrab( sdl_window, false );
			SDL_ShowCursor();
		} else {
			SDL_SetWindowMouseGrab( sdl_window, true );
			SDL_SetWindowRelativeMouseMode( sdl_window, true );
			SDL_HideCursor();
		}
		in_nograb->modified = qfalse;
	}

	mouseActive = qtrue;
}

static void IN_DeactivateMouseInternal( qboolean isFullscreen ) {
	if ( !SDL_WasInit( SDL_INIT_VIDEO ) || !sdl_window ) {
		return;
	}

	SDL_ShowCursor();

	if ( !mouseAvailable ) {
		return;
	}

	if ( mouseActive ) {
		IN_GobbleMotionEvents();
		SDL_SetWindowRelativeMouseMode( sdl_window, false );
		SDL_SetWindowMouseGrab( sdl_window, false );

		if ( SDL_GetWindowFlags( sdl_window ) & SDL_WINDOW_MOUSE_FOCUS ) {
			int width;
			int height;
			IN_GetWindowSize( &width, &height );
			SDL_WarpMouseInWindow( sdl_window, (float)width * 0.5f, (float)height * 0.5f );
		}

		mouseActive = qfalse;
	}
}

static void IN_ProcessText( const char *text ) {
	const unsigned char *c = (const unsigned char *)text;

	while ( *c ) {
		int utf32 = 0;

		if ( ( *c & 0x80 ) == 0 ) {
			utf32 = *c++;
		} else if ( ( *c & 0xE0 ) == 0xC0 ) {
			utf32 |= ( *c++ & 0x1F ) << 6;
			utf32 |= ( *c++ & 0x3F );
		} else if ( ( *c & 0xF0 ) == 0xE0 ) {
			utf32 |= ( *c++ & 0x0F ) << 12;
			utf32 |= ( *c++ & 0x3F ) << 6;
			utf32 |= ( *c++ & 0x3F );
		} else if ( ( *c & 0xF8 ) == 0xF0 ) {
			utf32 |= ( *c++ & 0x07 ) << 18;
			utf32 |= ( *c++ & 0x3F ) << 12;
			utf32 |= ( *c++ & 0x3F ) << 6;
			utf32 |= ( *c++ & 0x3F );
		} else {
			c++;
		}

		if ( utf32 > 0 ) {
			Sys_QueEvent( in_eventTime, SE_CHAR, utf32, 0, 0, NULL );
		}
	}
}

typedef struct {
	qboolean buttons[32];
	int oldaxes[MAX_JOYSTICK_AXIS];
	unsigned int oldhats;
} stickState_t;

static stickState_t stick_state;

static int hat_keys[16] = {
	K_JOY29, K_JOY30, K_JOY31, K_JOY32,
	K_JOY25, K_JOY26, K_JOY27, K_JOY28,
	K_JOY21, K_JOY22, K_JOY23, K_JOY24,
	K_JOY17, K_JOY18, K_JOY19, K_JOY20
};

static void IN_InitJoystick( void ) {
	SDL_JoystickID *sticks;
	int total = 0;

	if ( stick ) {
		SDL_CloseJoystick( stick );
		stick = NULL;
	}
	memset( &stick_state, 0, sizeof( stick_state ) );

	if ( !in_joystick || !in_joystick->integer ) {
		return;
	}

	if ( !SDL_WasInit( SDL_INIT_JOYSTICK ) && !SDL_Init( SDL_INIT_JOYSTICK ) ) {
		Com_DPrintf( "SDL_Init(SDL_INIT_JOYSTICK) failed: %s\n", SDL_GetError() );
		return;
	}

	sticks = SDL_GetJoysticks( &total );
	if ( !sticks || total <= 0 ) {
		SDL_free( sticks );
		return;
	}

	in_joystickNo = Cvar_Get( "in_joystickNo", "0", CVAR_ARCHIVE );
	if ( in_joystickNo->integer < 0 || in_joystickNo->integer >= total ) {
		Cvar_Set( "in_joystickNo", "0" );
	}

	stick = SDL_OpenJoystick( sticks[in_joystickNo->integer] );
	if ( stick ) {
		Com_DPrintf( "SDL joystick opened: %s\n", SDL_GetJoystickName( stick ) );
	} else {
		Com_DPrintf( "SDL joystick open failed: %s\n", SDL_GetError() );
	}

	SDL_free( sticks );
}

static void IN_ShutdownJoystick( void ) {
	if ( stick ) {
		SDL_CloseJoystick( stick );
		stick = NULL;
	}

	if ( SDL_WasInit( SDL_INIT_JOYSTICK ) ) {
		SDL_QuitSubSystem( SDL_INIT_JOYSTICK );
	}
}

static void IN_JoyMove( void ) {
	int i, total;
	unsigned int hats = 0;

	if ( !stick ) {
		return;
	}

	SDL_UpdateJoysticks();

	total = SDL_GetNumJoystickButtons( stick );
	if ( total > 32 ) {
		total = 32;
	}
	for ( i = 0; i < total; i++ ) {
		qboolean pressed = SDL_GetJoystickButton( stick, i ) != 0;
		if ( pressed != stick_state.buttons[i] ) {
			Sys_QueEvent( in_eventTime, SE_KEY, K_JOY1 + i, pressed, 0, NULL );
			stick_state.buttons[i] = pressed;
		}
	}

	total = SDL_GetNumJoystickHats( stick );
	if ( total > 4 ) {
		total = 4;
	}
	for ( i = 0; i < total; i++ ) {
		( (Uint8 *)&hats )[i] = SDL_GetJoystickHat( stick, i );
	}
	if ( hats != stick_state.oldhats ) {
		for ( i = 0; i < 4; i++ ) {
			Uint8 oldHat = ( (Uint8 *)&stick_state.oldhats )[i];
			Uint8 newHat = ( (Uint8 *)&hats )[i];
			if ( oldHat == newHat ) {
				continue;
			}
			if ( oldHat & SDL_HAT_UP )    Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 0], qfalse, 0, NULL );
			if ( oldHat & SDL_HAT_RIGHT ) Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 1], qfalse, 0, NULL );
			if ( oldHat & SDL_HAT_DOWN )  Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 2], qfalse, 0, NULL );
			if ( oldHat & SDL_HAT_LEFT )  Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 3], qfalse, 0, NULL );
			if ( newHat & SDL_HAT_UP )    Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 0], qtrue, 0, NULL );
			if ( newHat & SDL_HAT_RIGHT ) Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 1], qtrue, 0, NULL );
			if ( newHat & SDL_HAT_DOWN )  Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 2], qtrue, 0, NULL );
			if ( newHat & SDL_HAT_LEFT )  Sys_QueEvent( in_eventTime, SE_KEY, hat_keys[4 * i + 3], qtrue, 0, NULL );
		}
		stick_state.oldhats = hats;
	}

	if ( in_joystickUseAnalog && in_joystickUseAnalog->integer ) {
		total = SDL_GetNumJoystickAxes( stick );
		if ( total > MAX_JOYSTICK_AXIS ) {
			total = MAX_JOYSTICK_AXIS;
		}
		for ( i = 0; i < total; i++ ) {
			Sint16 axis = SDL_GetJoystickAxis( stick, i );
			float f = (float)abs( axis ) / 32767.0f;
			if ( in_joystickThreshold && f < in_joystickThreshold->value ) {
				axis = 0;
			}
			if ( axis != stick_state.oldaxes[i] ) {
				Sys_QueEvent( in_eventTime, SE_JOYSTICK_AXIS, i, axis, 0, NULL );
				stick_state.oldaxes[i] = axis;
			}
		}
	}
}

static void IN_ProcessEvents( void ) {
	SDL_Event e;
	keyNum_t key;
	static keyNum_t lastKeyDown = 0;

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) ) {
		return;
	}

	SDL_PumpEvents();
	IN_UpdatePhysicalSpaceState();

	while ( SDL_PollEvent( &e ) ) {
		switch ( e.type )
		{
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			Cbuf_ExecuteText( EXEC_NOW, "quit Closed window\n" );
			break;

		case SDL_EVENT_KEY_DOWN:
			g_wv.activeApp = qtrue;
			Cvar_SetValue( "com_unfocused", 0 );
			if ( IN_IsSystemShortcutKey( &e.key ) ) {
				IN_SuspendMouseForSystemShortcut( 1500 );
			}
			key = IN_TranslateSDLToQ3Key( &e.key, qtrue );
			if ( e.key.repeat && Key_GetCatcher() == 0 && cls.state != CA_CINEMATIC &&
				!( cl.cameraMode && ( key == K_ESCAPE || key == K_SPACE || key == K_ENTER || key == K_KP_ENTER ) ) ) {
				break;
			}
			if ( key == K_SPACE ) {
				CL_SetPhysicalSpaceState( qtrue );
			}
			if ( key && CL_SpeedrunImGui_SDLKeyEvent( key, qtrue ) ) {
				break;
			}
			if ( key ) {
				Sys_QueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );
			}
			if ( key == K_BACKSPACE ) {
				Sys_QueEvent( in_eventTime, SE_CHAR, CTRL( 'h' ), 0, 0, NULL );
			} else if ( keys[K_CTRL].down && key >= 'a' && key <= 'z' ) {
				Sys_QueEvent( in_eventTime, SE_CHAR, CTRL( key ), 0, 0, NULL );
			}
			lastKeyDown = key;
			break;

		case SDL_EVENT_KEY_UP:
			if ( IN_IsSystemShortcutKey( &e.key ) ) {
				IN_SuspendMouseForSystemShortcut( 750 );
			}
			key = IN_TranslateSDLToQ3Key( &e.key, qfalse );
			if ( key == K_SPACE ) {
				CL_ClearPhysicalSpaceHold();
			}
			if ( key && CL_SpeedrunImGui_SDLKeyEvent( key, qfalse ) ) {
				break;
			}
			if ( key ) {
				Sys_QueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );
			}
			lastKeyDown = 0;
			break;

		case SDL_EVENT_TEXT_INPUT:
			if ( CL_SpeedrunImGui_SDLTextInput( e.text.text ) ) {
				break;
			}
			if ( lastKeyDown != '`' && lastKeyDown != '~' ) {
				IN_ProcessText( e.text.text );
			}
			break;

		case SDL_EVENT_MOUSE_MOTION:
			{
				float mx = e.motion.x;
				float my = e.motion.y;
				IN_WindowToRenderCoords( &mx, &my );
				if ( CL_SpeedrunImGui_SDLMouseMotion( mx, my ) ) {
					break;
				}
				if ( mouseActive && ( e.motion.xrel || e.motion.yrel ) ) {
					if ( in_sdlMouseDebug && in_sdlMouseDebug->integer && mouseDebugEvents < 32 ) {
						Com_Printf( "SDL mouse motion: %d %d flags:%llu\n",
							(int)e.motion.xrel, (int)e.motion.yrel,
							(unsigned long long)SDL_GetWindowFlags( sdl_window ) );
						mouseDebugEvents++;
					}
					Sys_QueEvent( in_eventTime, SE_MOUSE, (int)e.motion.xrel, (int)e.motion.yrel, 0, NULL );
				}
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				int b;
				int imguiButton;
				float mx = e.button.x;
				float my = e.button.y;
				g_wv.activeApp = qtrue;
				Cvar_SetValue( "com_unfocused", 0 );
				IN_WindowToRenderCoords( &mx, &my );
				CL_SpeedrunImGui_SDLMouseMotion( mx, my );
				switch ( e.button.button )
				{
				case SDL_BUTTON_LEFT:   b = K_MOUSE1; imguiButton = 0; break;
				case SDL_BUTTON_MIDDLE: b = K_MOUSE3; imguiButton = 2; break;
				case SDL_BUTTON_RIGHT:  b = K_MOUSE2; imguiButton = 1; break;
				case SDL_BUTTON_X1:     b = K_MOUSE4; imguiButton = 3; break;
				case SDL_BUTTON_X2:     b = K_MOUSE5; imguiButton = 4; break;
				default:                b = K_AUX1 + ( ( e.button.button - SDL_BUTTON_X2 + 1 ) & 15 ); imguiButton = -1; break;
				}
				if ( CL_SpeedrunImGui_SDLMouseButton( b, imguiButton, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) ) {
					break;
				}
				if ( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
					 !( Key_GetCatcher() & KEYCATCH_CONSOLE ) && !IN_ImGuiOwnsInput() ) {
					IN_ActivateMouse( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
				}
				Sys_QueEvent( in_eventTime, SE_KEY, b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, 0, NULL );
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if ( e.wheel.y != 0.0f && CL_SpeedrunImGui_SDLMouseWheel( e.wheel.y > 0.0f ? 1.0f : -1.0f ) ) {
				break;
			}
			if ( e.wheel.y > 0 ) {
				Sys_QueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
			} else if ( e.wheel.y < 0 ) {
				Sys_QueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
			}
			break;

		case SDL_EVENT_JOYSTICK_ADDED:
		case SDL_EVENT_JOYSTICK_REMOVED:
			IN_InitJoystick();
			break;

		case SDL_EVENT_WINDOW_RESIZED:
			if ( Sys_Milliseconds() < ignoreResizeUntil ) {
				break;
			}
			if ( !cls.glconfig.isFullscreen ) {
				int width;
				int height;
				IN_GetWindowSize( &width, &height );
				if ( width > 0 && height > 0 &&
					 ( Cvar_VariableIntegerValue( "r_mode" ) != -1 ||
					   Cvar_VariableIntegerValue( "r_customwidth" ) != width ||
					   Cvar_VariableIntegerValue( "r_customheight" ) != height ) ) {
					Cvar_SetValue( "r_customwidth", width );
					Cvar_SetValue( "r_customheight", height );
					Cvar_Set( "r_mode", "-1" );
					vidRestartTime = Sys_Milliseconds() + 1000;
				}
			}
			break;

		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			if ( Sys_Milliseconds() < ignoreResizeUntil ) {
				break;
			}
			if ( !cls.glconfig.isFullscreen ) {
				int width = 0;
				int height = 0;
				if ( SDL_GetWindowSizeInPixels( sdl_window, &width, &height ) &&
					 width > 0 && height > 0 &&
					 ( cls.glconfig.vidWidth != width || cls.glconfig.vidHeight != height ) ) {
					vidRestartTime = Sys_Milliseconds() + 1000;
				}
			}
			break;

		case SDL_EVENT_WINDOW_MINIMIZED:
			g_wv.isMinimized = qtrue;
			g_wv.activeApp = qfalse;
			CL_ClearPhysicalSpaceHold();
			Cvar_SetValue( "com_minimized", 1 );
			IN_SuspendMouseForSystemShortcut( 1000 );
			IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
			break;

		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_MAXIMIZED:
			g_wv.isMinimized = qfalse;
			Cvar_SetValue( "com_minimized", 0 );
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			g_wv.activeApp = qfalse;
			CL_ClearPhysicalSpaceHold();
			Cvar_SetValue( "com_unfocused", 1 );
			IN_SuspendMouseForSystemShortcut( 1000 );
			IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			g_wv.activeApp = qtrue;
			Cvar_SetValue( "com_unfocused", 0 );
			if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) && !IN_ImGuiOwnsInput() ) {
				IN_ActivateMouse( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
			}
			break;

		default:
			break;
		}
	}

	IN_UpdatePhysicalSpaceState();
}

void IN_Init( void ) {
	Uint64 appState;

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) || !sdl_window ) {
		return;
	}

	Com_DPrintf( "\n------- SDL Input Initialization -------\n" );

	in_keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );
	in_sdlForceGrab = Cvar_Get( "in_sdlForceGrab", "1", CVAR_ARCHIVE );
	in_sdlRequireFocus = Cvar_Get( "in_sdlRequireFocus", "0", CVAR_ARCHIVE );
	in_sdlMouseDebug = Cvar_Get( "in_sdlMouseDebug", "0", CVAR_TEMP );
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE );
	in_joystickThreshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );
	in_joystickUseAnalog = Cvar_Get( "in_joystickUseAnalog", "1", CVAR_ARCHIVE );

	SDL_StartTextInput( sdl_window );
	mouseAvailable = in_mouse->integer != 0;
	IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );

	appState = SDL_GetWindowFlags( sdl_window );
	g_wv.activeApp = ( appState & SDL_WINDOW_INPUT_FOCUS ) ? qtrue : qfalse;
	g_wv.isMinimized = ( appState & SDL_WINDOW_MINIMIZED ) ? qtrue : qfalse;
	Cvar_SetValue( "com_unfocused", !( appState & SDL_WINDOW_INPUT_FOCUS ) );
	Cvar_SetValue( "com_minimized", ( appState & SDL_WINDOW_MINIMIZED ) ? 1 : 0 );

	IN_InitJoystick();
	in_eventTime = Sys_Milliseconds();
	Com_DPrintf( "----------------------------------------\n" );
}

void IN_SuppressResizeEvents( int msec ) {
	int until;

	if ( msec <= 0 ) {
		return;
	}
	until = Sys_Milliseconds() + msec;
	if ( until > ignoreResizeUntil ) {
		ignoreResizeUntil = until;
	}
}

void IN_Shutdown( void ) {
	if ( SDL_WasInit( SDL_INIT_VIDEO ) && sdl_window ) {
		SDL_StopTextInput( sdl_window );
		IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
	}

	mouseAvailable = qfalse;
	IN_ShutdownJoystick();
}

void IN_Activate( qboolean active ) {
	g_wv.activeApp = active;
	if ( !active ) {
		IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
	}
}

void IN_DeactivateMouse( void ) {
	IN_DeactivateMouseInternal( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0 );
}

void IN_Frame( void ) {
	qboolean loading;
	qboolean isFullscreen;
	qboolean requireFocus;
	Uint64 flags = 0;
	int now;

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) || !sdl_window ) {
		return;
	}

	in_eventTime = Sys_Milliseconds();
	IN_ProcessEvents();

	IN_JoyMove();

	loading = ( cls.state != CA_DISCONNECTED && cls.state != CA_ACTIVE );
	isFullscreen = Cvar_VariableIntegerValue( "r_fullscreen" ) != 0;
	cls.glconfig.isFullscreen = isFullscreen;
	flags = SDL_GetWindowFlags( sdl_window );
	g_wv.isMinimized = ( flags & SDL_WINDOW_MINIMIZED ) ? qtrue : qfalse;
	requireFocus = qtrue;
	now = Sys_Milliseconds();

	if ( IN_ImGuiOwnsInput() ) {
		IN_DebugMouseState( 4, "imgui", isFullscreen, flags );
		IN_DeactivateMouseInternal( isFullscreen );
	} else if ( systemShortcutUntil > now ) {
		IN_DebugMouseState( 5, "system-shortcut", isFullscreen, flags );
		IN_DeactivateMouseInternal( isFullscreen );
	} else if ( !isFullscreen && ( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		IN_DebugMouseState( 1, "console", isFullscreen, flags );
		IN_DeactivateMouseInternal( isFullscreen );
	} else if ( !isFullscreen && loading ) {
		IN_DebugMouseState( 2, "loading", isFullscreen, flags );
		IN_DeactivateMouseInternal( isFullscreen );
	} else if ( requireFocus && ( !( flags & SDL_WINDOW_INPUT_FOCUS ) || ( flags & SDL_WINDOW_MINIMIZED ) ) ) {
		g_wv.activeApp = qfalse;
		IN_DebugMouseState( 3, "no-input-focus", isFullscreen, flags );
		IN_DeactivateMouseInternal( isFullscreen );
	} else {
		g_wv.activeApp = qtrue;
		Cvar_SetValue( "com_unfocused", 0 );
		IN_ActivateMouse( isFullscreen );
		IN_DebugMouseState( 0, "capture", isFullscreen, flags );
	}

	if ( vidRestartTime != 0 && vidRestartTime < Sys_Milliseconds() ) {
		vidRestartTime = 0;
		Cbuf_AddText( "vid_restart\n" );
	}

	SNDDMA_Activate();
}

void IN_ClearStates( void ) {
	Key_ClearStates();
}

void IN_JoystickCommands( void ) {
}

void IN_Move( usercmd_t *cmd ) {
	(void)cmd;
}
