// Dear ImGui speedrun settings UI.

#include <windows.h>
#include <GL/gl.h>
#include <float.h>
#include <math.h>

extern "C" {
#include "client.h"
char *Key_KeynumToString( int keynum, qboolean bTranslate );
void Key_ClearStates( void );
}

#include "../thirdparty/imgui/imgui.h"
#include "../thirdparty/imgui/backends/imgui_impl_opengl2.h"

static bool     s_imguiInitialized = false;
static bool     s_imguiOpen = false;
static int      s_lastEnabledCvar = 0;
static cvar_t  *s_imguiEnabled = NULL;
static cvar_t  *s_imguiAlpha = NULL;
static cvar_t  *s_imguiCardAlpha = NULL;
static cvar_t  *s_imguiRounding = NULL;
static cvar_t  *s_imguiAccent = NULL;
static cvar_t  *s_imguiAccentAlt = NULL;
static cvar_t  *s_imguiAnimations = NULL;
static cvar_t  *s_imguiRestorePause = NULL;
static int      s_lastFrameMs = 0;
static float    s_imguiAnim = 1.0f;
static bool     s_mouseDown[5] = { false, false, false, false, false };
static float    s_mouseWheel = 0.0f;
static const char *s_pendingBindCommand = NULL;

#define SRGUI_MAX_DEMOS 256
#define SRGUI_DEMOCAT_FULLGAME 0
#define SRGUI_DEMOCAT_MISSION  1
#define SRGUI_DEMOCAT_IL       2
#define SRGUI_DEMOCAT_OTHER    3

static char s_demoList[SRGUI_MAX_DEMOS][MAX_QPATH];
static int  s_demoCategory[SRGUI_MAX_DEMOS];
static int  s_demoCount = 0;
static int  s_demoSelected = -1;
static int  s_demoFilter = -1;
static char s_demoSearch[64] = "";

static cvar_t *s_cg_livesplit = NULL;
static cvar_t *s_ls_type = NULL;
static cvar_t *s_ls_mode = NULL;
static cvar_t *s_ls_mission = NULL;
static cvar_t *s_ls_100pct = NULL;
static cvar_t *s_ls_compare = NULL;
static cvar_t *s_ls_timing = NULL;
static cvar_t *s_sp_autorecord = NULL;
static cvar_t *s_ls_showtimer = NULL;
static cvar_t *s_ls_showsegtimer = NULL;
static cvar_t *s_ls_showheaders = NULL;
static cvar_t *s_ls_showstats = NULL;

static cvar_t *CL_ImGuiCvar( const char *name, const char *defaultValue ) {
	return Cvar_Get( name, defaultValue ? defaultValue : "0", CVAR_ARCHIVE );
}

static void CL_SpeedrunImGui_ClearGameplayInput( void ) {
	Key_ClearStates();
	CL_ClearKeys();
	cl.mouseDx[0] = cl.mouseDx[1] = 0;
	cl.mouseDy[0] = cl.mouseDy[1] = 0;
	cl.joystickAxis[AXIS_FORWARD] = 0;
	cl.joystickAxis[AXIS_SIDE] = 0;
	cl.joystickAxis[AXIS_UP] = 0;
	cl.joystickAxis[AXIS_YAW] = 0;
	cl.joystickAxis[AXIS_PITCH] = 0;
}

static void CL_SpeedrunImGui_Open( void ) {
	bool openedInGame = cls.state == CA_ACTIVE;
	s_imguiOpen = true;
	s_lastEnabledCvar = 1;
	s_imguiAnim = s_imguiAnimations && s_imguiAnimations->integer == 0 ? 1.0f : 0.0f;
	SetCursor( NULL );
	if ( openedInGame && s_imguiRestorePause ) {
		Cvar_Set( s_imguiRestorePause->name, "1" );
	}
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "1" );
	}
	CL_SpeedrunImGui_ClearGameplayInput();

	/* If launched from the console, close the console immediately.  Otherwise
	   the user sees a cursor but the half-console overlay still owns the view,
	   which looks like ImGui opened without a usable window.  Keep KEYCATCH_UI
	   active while ImGui is open so the client does not generate gameplay cmds
	   under the panel. */
	Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_CONSOLE ) | KEYCATCH_UI );
	if ( openedInGame ) {
		Cvar_Set( "cl_paused", "1" );
	}
}

static void CL_SpeedrunImGui_Close( void ) {
	bool restorePause = s_imguiRestorePause && s_imguiRestorePause->integer != 0;
	s_imguiOpen = false;
	s_pendingBindCommand = NULL;
	s_mouseDown[0] = s_mouseDown[1] = s_mouseDown[2] = false;
	s_lastEnabledCvar = 0;
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "0" );
	}
	if ( restorePause ) {
		CL_SpeedrunImGui_ClearGameplayInput();
		if ( uivm ) {
			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_INGAME );
		} else {
			Key_SetCatcher( Key_GetCatcher() | KEYCATCH_UI );
			Cvar_Set( "cl_paused", "1" );
		}
		if ( s_imguiRestorePause ) {
			Cvar_Set( s_imguiRestorePause->name, "0" );
		}
	} else {
		if ( uivm ) {
			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE );
		}
		Key_SetCatcher( Key_GetCatcher() & ~( KEYCATCH_UI | KEYCATCH_CONSOLE ) );
		CL_SpeedrunImGui_ClearGameplayInput();
		Cvar_Set( "cl_paused", "0" );
	}
}

static ImGuiKey CL_ImGuiMapVK( WPARAM vk ) {
	switch ( vk ) {
	case VK_TAB: return ImGuiKey_Tab;
	case VK_LEFT: return ImGuiKey_LeftArrow;
	case VK_RIGHT: return ImGuiKey_RightArrow;
	case VK_UP: return ImGuiKey_UpArrow;
	case VK_DOWN: return ImGuiKey_DownArrow;
	case VK_PRIOR: return ImGuiKey_PageUp;
	case VK_NEXT: return ImGuiKey_PageDown;
	case VK_HOME: return ImGuiKey_Home;
	case VK_END: return ImGuiKey_End;
	case VK_INSERT: return ImGuiKey_Insert;
	case VK_DELETE: return ImGuiKey_Delete;
	case VK_BACK: return ImGuiKey_Backspace;
	case VK_SPACE: return ImGuiKey_Space;
	case VK_RETURN: return ImGuiKey_Enter;
	case VK_ESCAPE: return ImGuiKey_Escape;
	case VK_OEM_7: return ImGuiKey_Apostrophe;
	case VK_OEM_COMMA: return ImGuiKey_Comma;
	case VK_OEM_MINUS: return ImGuiKey_Minus;
	case VK_OEM_PERIOD: return ImGuiKey_Period;
	case VK_OEM_2: return ImGuiKey_Slash;
	case VK_OEM_1: return ImGuiKey_Semicolon;
	case VK_OEM_PLUS: return ImGuiKey_Equal;
	case VK_OEM_4: return ImGuiKey_LeftBracket;
	case VK_OEM_5: return ImGuiKey_Backslash;
	case VK_OEM_6: return ImGuiKey_RightBracket;
	case VK_OEM_3: return ImGuiKey_GraveAccent;
	case VK_CAPITAL: return ImGuiKey_CapsLock;
	case VK_SCROLL: return ImGuiKey_ScrollLock;
	case VK_NUMLOCK: return ImGuiKey_NumLock;
	case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
	case VK_PAUSE: return ImGuiKey_Pause;
	case VK_NUMPAD0: return ImGuiKey_Keypad0;
	case VK_NUMPAD1: return ImGuiKey_Keypad1;
	case VK_NUMPAD2: return ImGuiKey_Keypad2;
	case VK_NUMPAD3: return ImGuiKey_Keypad3;
	case VK_NUMPAD4: return ImGuiKey_Keypad4;
	case VK_NUMPAD5: return ImGuiKey_Keypad5;
	case VK_NUMPAD6: return ImGuiKey_Keypad6;
	case VK_NUMPAD7: return ImGuiKey_Keypad7;
	case VK_NUMPAD8: return ImGuiKey_Keypad8;
	case VK_NUMPAD9: return ImGuiKey_Keypad9;
	case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
	case VK_DIVIDE: return ImGuiKey_KeypadDivide;
	case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
	case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
	case VK_ADD: return ImGuiKey_KeypadAdd;
	case VK_SHIFT: return ImGuiKey_LeftShift;
	case VK_CONTROL: return ImGuiKey_LeftCtrl;
	case VK_MENU: return ImGuiKey_LeftAlt;
	}
	if ( vk >= '0' && vk <= '9' ) return (ImGuiKey)( ImGuiKey_0 + ( vk - '0' ) );
	if ( vk >= 'A' && vk <= 'Z' ) return (ImGuiKey)( ImGuiKey_A + ( vk - 'A' ) );
	if ( vk >= VK_F1 && vk <= VK_F12 ) return (ImGuiKey)( ImGuiKey_F1 + ( vk - VK_F1 ) );
	return ImGuiKey_None;
}

static void CL_ImGuiSetDarkSpeedrunStyle( void ) {
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowPadding = ImVec2( 18, 16 );
	style.FramePadding = ImVec2( 10, 6 );
	style.CellPadding = ImVec2( 8, 5 );
	style.ItemSpacing = ImVec2( 10, 9 );
	style.ItemInnerSpacing = ImVec2( 8, 6 );
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 12.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.WindowRounding = 10.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 7.0f;
	style.PopupRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding = 8.0f;
	style.TabRounding = 8.0f;

	ImVec4 *c = style.Colors;
	c[ImGuiCol_Text] = ImVec4( 0.88f, 0.91f, 0.86f, 1.00f );
	c[ImGuiCol_TextDisabled] = ImVec4( 0.45f, 0.50f, 0.46f, 1.00f );
	c[ImGuiCol_WindowBg] = ImVec4( 0.020f, 0.026f, 0.024f, 0.96f );
	c[ImGuiCol_ChildBg] = ImVec4( 0.035f, 0.044f, 0.040f, 0.92f );
	c[ImGuiCol_PopupBg] = ImVec4( 0.035f, 0.044f, 0.040f, 0.98f );
	c[ImGuiCol_Border] = ImVec4( 0.28f, 0.42f, 0.22f, 0.40f );
	c[ImGuiCol_FrameBg] = ImVec4( 0.065f, 0.082f, 0.074f, 1.00f );
	c[ImGuiCol_FrameBgHovered] = ImVec4( 0.12f, 0.20f, 0.10f, 1.00f );
	c[ImGuiCol_FrameBgActive] = ImVec4( 0.18f, 0.32f, 0.13f, 1.00f );
	c[ImGuiCol_TitleBg] = ImVec4( 0.030f, 0.045f, 0.035f, 1.00f );
	c[ImGuiCol_TitleBgActive] = ImVec4( 0.055f, 0.085f, 0.050f, 1.00f );
	c[ImGuiCol_CheckMark] = ImVec4( 0.58f, 0.92f, 0.34f, 1.00f );
	c[ImGuiCol_SliderGrab] = ImVec4( 0.50f, 0.78f, 0.28f, 1.00f );
	c[ImGuiCol_SliderGrabActive] = ImVec4( 0.68f, 0.96f, 0.42f, 1.00f );
	c[ImGuiCol_ScrollbarBg] = ImVec4( 0.025f, 0.035f, 0.030f, 0.70f );
	c[ImGuiCol_ScrollbarGrab] = ImVec4( 0.22f, 0.36f, 0.16f, 0.90f );
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.34f, 0.56f, 0.23f, 1.00f );
	c[ImGuiCol_ScrollbarGrabActive] = ImVec4( 0.48f, 0.76f, 0.30f, 1.00f );
	c[ImGuiCol_PlotLines] = ImVec4( 0.58f, 0.92f, 0.34f, 0.90f );
	c[ImGuiCol_PlotHistogram] = ImVec4( 0.78f, 0.58f, 0.20f, 0.90f );
	c[ImGuiCol_Button] = ImVec4( 0.09f, 0.14f, 0.08f, 1.00f );
	c[ImGuiCol_ButtonHovered] = ImVec4( 0.16f, 0.28f, 0.12f, 1.00f );
	c[ImGuiCol_ButtonActive] = ImVec4( 0.23f, 0.42f, 0.16f, 1.00f );
	c[ImGuiCol_Header] = ImVec4( 0.10f, 0.17f, 0.09f, 1.00f );
	c[ImGuiCol_HeaderHovered] = ImVec4( 0.15f, 0.28f, 0.12f, 1.00f );
	c[ImGuiCol_HeaderActive] = ImVec4( 0.22f, 0.40f, 0.15f, 1.00f );
	c[ImGuiCol_Separator] = ImVec4( 0.25f, 0.42f, 0.18f, 0.55f );
	c[ImGuiCol_SeparatorHovered] = ImVec4( 0.48f, 0.78f, 0.30f, 0.86f );
	c[ImGuiCol_SeparatorActive] = ImVec4( 0.64f, 0.96f, 0.38f, 1.00f );
	c[ImGuiCol_ResizeGrip] = ImVec4( 0.30f, 0.55f, 0.20f, 0.38f );
	c[ImGuiCol_ResizeGripHovered] = ImVec4( 0.48f, 0.78f, 0.30f, 0.82f );
	c[ImGuiCol_ResizeGripActive] = ImVec4( 0.64f, 0.96f, 0.38f, 1.00f );
	c[ImGuiCol_NavHighlight] = ImVec4( 0.58f, 0.92f, 0.34f, 0.80f );
}

static void CL_ImGuiReadColorCvar( cvar_t *cv, const float fallback[4], float out[4] ) {
	int r, g, b;
	float a;
	out[0] = fallback[0];
	out[1] = fallback[1];
	out[2] = fallback[2];
	out[3] = fallback[3];
	if ( cv && cv->string && sscanf( cv->string, "%d %d %d %f", &r, &g, &b, &a ) == 4 ) {
		out[0] = Com_Clamp( 0.0f, 1.0f, r / 255.0f );
		out[1] = Com_Clamp( 0.0f, 1.0f, g / 255.0f );
		out[2] = Com_Clamp( 0.0f, 1.0f, b / 255.0f );
		out[3] = Com_Clamp( 0.0f, 1.0f, a );
	}
}

static void CL_ImGuiApplyRuntimeStyle( void ) {
	static const float accentFallback[4] = { 0.36f, 0.82f, 0.21f, 1.00f };
	static const float accentAltFallback[4] = { 0.96f, 0.74f, 0.24f, 1.00f };
	float accent[4];
	float accentAlt[4];
	bool rounded = !s_imguiRounding || s_imguiRounding->integer != 0;
	float cardAlpha = s_imguiCardAlpha ? Com_Clamp( 0.35f, 1.0f, s_imguiCardAlpha->value ) : 0.92f;
	ImGuiStyle &style = ImGui::GetStyle();
	ImVec4 *c = style.Colors;

	CL_ImGuiReadColorCvar( s_imguiAccent, accentFallback, accent );
	CL_ImGuiReadColorCvar( s_imguiAccentAlt, accentAltFallback, accentAlt );

	style.WindowRounding = rounded ? 10.0f : 0.0f;
	style.ChildRounding = rounded ? 10.0f : 0.0f;
	style.FrameRounding = rounded ? 7.0f : 0.0f;
	style.PopupRounding = rounded ? 8.0f : 0.0f;
	style.ScrollbarRounding = rounded ? 8.0f : 0.0f;
	style.GrabRounding = rounded ? 8.0f : 0.0f;
	style.TabRounding = rounded ? 8.0f : 0.0f;

	c[ImGuiCol_ChildBg].w = cardAlpha;
	c[ImGuiCol_CheckMark] = ImVec4( accent[0], accent[1], accent[2], 1.00f );
	c[ImGuiCol_SliderGrab] = ImVec4( accent[0] * 0.86f, accent[1] * 0.86f, accent[2] * 0.86f, 1.00f );
	c[ImGuiCol_SliderGrabActive] = ImVec4( accent[0], accent[1], accent[2], 1.00f );
	c[ImGuiCol_ResizeGripHovered] = ImVec4( accent[0], accent[1], accent[2], 0.82f );
	c[ImGuiCol_ResizeGripActive] = ImVec4( accent[0], accent[1], accent[2], 1.00f );
	c[ImGuiCol_SeparatorHovered] = ImVec4( accent[0], accent[1], accent[2], 0.86f );
	c[ImGuiCol_SeparatorActive] = ImVec4( accent[0], accent[1], accent[2], 1.00f );
	c[ImGuiCol_PlotHistogram] = ImVec4( accentAlt[0], accentAlt[1], accentAlt[2], 0.90f );
	c[ImGuiCol_NavHighlight] = ImVec4( accent[0], accent[1], accent[2], 0.80f );
}

static void CL_ImGuiLazyInit( void ) {
	if ( s_imguiInitialized ) {
		return;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	/* RTCW hides/captures the Win32 cursor while the game window is active, so
	   ImGui must draw its own cursor.  This is now rendered at GL end-frame, so
	   it no longer leaves trails behind the game's command buffer. */
	io.MouseDrawCursor = true;
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendPlatformName = "rtcw_win32_minimal";
	CL_ImGuiSetDarkSpeedrunStyle();
	ImGui_ImplOpenGL2_Init();
	s_imguiInitialized = true;
}

static void CL_ImGuiBoolCvar( const char *label, cvar_t *cv, const char *hint = NULL ) {
	bool v = cv && cv->integer != 0;
	if ( ImGui::Checkbox( label, &v ) && cv ) {
		Cvar_Set( cv->name, v ? "1" : "0" );
	}
	if ( cv ) {
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", cv->name );
	}
	if ( hint && hint[0] ) {
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", hint );
	}
}

static void CL_ImGuiBoolCvarName( const char *label, const char *name, const char *defaultValue = "0", const char *hint = NULL ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	ImGui::PushID( name );
	CL_ImGuiBoolCvar( label, cv, hint );
	ImGui::PopID();
}

static void CL_ImGuiSliderCvarName( const char *label, const char *name, const char *defaultValue, float minValue, float maxValue, const char *format = "%.2f" ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	float v = cv ? cv->value : 0.0f;
	ImGui::PushID( name );
	if ( ImGui::SliderFloat( label, &v, minValue, maxValue, format ) && cv ) {
		Cvar_SetValue( cv->name, v );
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "%s", name );
	ImGui::PopID();
}

static void CL_ImGuiIntSliderCvarName( const char *label, const char *name, const char *defaultValue, int minValue, int maxValue ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	int v = cv ? cv->integer : 0;
	ImGui::PushID( name );
	if ( ImGui::SliderInt( label, &v, minValue, maxValue ) && cv ) {
		Cvar_SetValue( cv->name, v );
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "%s", name );
	ImGui::PopID();
}

static void CL_ImGuiComboCvar( const char *label, cvar_t *cv, const char *const *labels, const int *values, int count ) {
	int current = 0;
	int i;
	if ( cv ) {
		for ( i = 0; i < count; ++i ) {
			if ( cv->integer == values[i] ) {
				current = i;
				break;
			}
		}
	}
	if ( ImGui::BeginCombo( label, labels[current] ) ) {
		for ( i = 0; i < count; ++i ) {
			bool selected = ( i == current );
			if ( ImGui::Selectable( labels[i], selected ) && cv ) {
				Cvar_SetValue( cv->name, values[i] );
			}
			if ( selected ) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if ( cv ) {
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", cv->name );
	}
}

static void CL_ImGuiComboCvarName( const char *label, const char *name, const char *defaultValue, const char *const *labels, const int *values, int count ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	ImGui::PushID( name );
	CL_ImGuiComboCvar( label, cv, labels, values, count );
	ImGui::PopID();
}

static void CL_ImGuiCommandComboCvarName( const char *label, const char *name, const char *defaultValue, const char *const *labels, const int *values, int count, const char *command ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	int current = 0;
	int i;
	char cmd[128];
	if ( cv ) {
		for ( i = 0; i < count; ++i ) {
			if ( cv->integer == values[i] ) {
				current = i;
				break;
			}
		}
	}
	ImGui::PushID( command );
	if ( ImGui::BeginCombo( label, labels[current] ) ) {
		for ( i = 0; i < count; ++i ) {
			bool selected = ( i == current );
			if ( ImGui::Selectable( labels[i], selected ) ) {
				if ( cv ) {
					Cvar_SetValue( cv->name, values[i] );
				}
				Com_sprintf( cmd, sizeof( cmd ), "%s %d\n", command, values[i] );
				Cbuf_AddText( cmd );
			}
			if ( selected ) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if ( cv ) {
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", cv->name );
	}
	ImGui::PopID();
}

static void CL_ImGuiStringComboCvarName( const char *label, const char *name, const char *defaultValue, const char *const *labels, const char *const *values, int count ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	int current = 0;
	int i;
	bool matched = false;
	if ( cv && cv->string && cv->string[0] ) {
		for ( i = 0; i < count; ++i ) {
			if ( !Q_stricmp( cv->string, values[i] ) ) {
				current = i;
				matched = true;
				break;
			}
		}
		if ( !matched && cv->integer >= 1 && cv->integer <= count ) {
			current = cv->integer - 1;
		}
	}
	ImGui::PushID( name );
	if ( ImGui::BeginCombo( label, labels[current] ) ) {
		for ( i = 0; i < count; ++i ) {
			bool selected = ( i == current );
			if ( ImGui::Selectable( labels[i], selected ) && cv ) {
				Cvar_Set( cv->name, values[i] );
			}
			if ( selected ) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if ( cv ) {
		ImGui::SameLine();
		ImGui::TextDisabled( "%s = %s", cv->name, cv->string && cv->string[0] ? cv->string : values[current] );
	}
	ImGui::PopID();
}

static void CL_ImGuiColorCvarName( const char *label, const char *name, const float fallback[4] ) {
	cvar_t *cv = CL_ImGuiCvar( name, "" );
	float rgba[4] = { fallback[0], fallback[1], fallback[2], fallback[3] };
	int r, g, b;
	float a;
	char buf[64];

	if ( cv && cv->string && sscanf( cv->string, "%d %d %d %f", &r, &g, &b, &a ) == 4 ) {
		rgba[0] = Com_Clamp( 0.0f, 1.0f, r / 255.0f );
		rgba[1] = Com_Clamp( 0.0f, 1.0f, g / 255.0f );
		rgba[2] = Com_Clamp( 0.0f, 1.0f, b / 255.0f );
		rgba[3] = Com_Clamp( 0.0f, 1.0f, a );
	}

	ImGui::PushID( name );
	if ( ImGui::ColorEdit4( label, rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB ) && cv ) {
		Com_sprintf( buf, sizeof( buf ), "%d %d %d %.2f", (int)( rgba[0] * 255.0f + 0.5f ), (int)( rgba[1] * 255.0f + 0.5f ), (int)( rgba[2] * 255.0f + 0.5f ), rgba[3] );
		Cvar_Set( cv->name, buf );
	}
	ImGui::SameLine();
	if ( ImGui::SmallButton( "Reset" ) && cv ) {
		Cvar_Set( cv->name, "" );
	}
	ImGui::PopID();
}

static void CL_ImGuiCommandButton( const char *label, const char *command, const char *hint = NULL ) {
	if ( ImGui::Button( label, ImVec2( 160, 0 ) ) ) {
		Cbuf_AddText( command );
		Cbuf_AddText( "\n" );
	}
	if ( hint && hint[0] ) {
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", hint );
	}
}

static void CL_ImGuiActionCard( const char *title, const char *desc, const char *button, const char *command, const ImVec4 &accent, bool highlighted = false ) {
	ImGuiStyle &style = ImGui::GetStyle();
	float avail = ImGui::GetContentRegionAvail().x;
	float wrapWidth = avail > 48.0f ? avail - 48.0f : 260.0f;
	ImVec2 descSize = ImGui::CalcTextSize( desc, NULL, false, wrapWidth );
	float wantedHeight = style.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + descSize.y + style.ItemSpacing.y * 3.0f + ImGui::GetFrameHeight();
	float cardHeight = wantedHeight > 92.0f ? wantedHeight : 92.0f;
	float rounding = style.ChildRounding > 0.0f ? style.ChildRounding : style.FrameRounding;
	float pulse = s_imguiAnimations && s_imguiAnimations->integer != 0 ? ( 0.82f + 0.18f * sinf( (float)ImGui::GetTime() * 2.8f ) ) : 1.0f;
	ImVec4 animatedAccent = ImVec4( accent.x, accent.y, accent.z, accent.w * pulse );

	ImGui::BeginChild( title, ImVec2( 0, cardHeight ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetWindowPos();
	ImVec2 p2 = ImVec2( p.x + ImGui::GetWindowWidth(), p.y + ImGui::GetWindowHeight() );
	bool hovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
	if ( hovered && s_imguiAnimations && s_imguiAnimations->integer != 0 ) {
		float glow = 0.22f + 0.10f * sinf( (float)ImGui::GetTime() * 5.2f );
		draw->AddRectFilled( ImVec2( p.x + 1.0f, p.y + 1.0f ), ImVec2( p2.x - 1.0f, p2.y - 1.0f ), ImGui::ColorConvertFloat4ToU32( ImVec4( accent.x, accent.y, accent.z, glow ) ), rounding );
	}
	if ( highlighted ) {
		draw->AddRectFilled( ImVec2( p.x + 1.0f, p.y + 1.0f ), ImVec2( p2.x - 1.0f, p2.y - 1.0f ), ImGui::ColorConvertFloat4ToU32( ImVec4( accent.x, accent.y, accent.z, 0.10f ) ), rounding );
		draw->AddRect( ImVec2( p.x + 1.0f, p.y + 1.0f ), ImVec2( p2.x - 1.0f, p2.y - 1.0f ), ImGui::ColorConvertFloat4ToU32( ImVec4( accent.x, accent.y, accent.z, 0.85f ) ), rounding, 0, 1.5f );
	}
	ImVec2 stripeMin = ImVec2( p.x + 1.0f, p.y + 1.0f );
	ImVec2 stripeMax = ImVec2( p.x + 5.0f, p2.y - 1.0f );
	draw->AddRectFilled( stripeMin, stripeMax, ImGui::ColorConvertFloat4ToU32( animatedAccent ), rounding > 1.0f ? rounding - 1.0f : 0.0f, ImDrawFlags_RoundCornersLeft );
	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 && ImGui::GetWindowWidth() > 120.0f ) {
		float scan = fmodf( (float)ImGui::GetTime() * 84.0f, ImGui::GetWindowWidth() + 60.0f ) - 60.0f;
		float scanMin = ( p.x + 12.0f ) > ( p.x + scan ) ? ( p.x + 12.0f ) : ( p.x + scan );
		float scanMax = ( p2.x - 10.0f ) < ( p.x + scan + 58.0f ) ? ( p2.x - 10.0f ) : ( p.x + scan + 58.0f );
		if ( scanMax > scanMin ) {
			draw->AddRectFilled( ImVec2( scanMin, p.y + 4.0f ), ImVec2( scanMax, p.y + 5.5f ), ImGui::ColorConvertFloat4ToU32( ImVec4( accent.x, accent.y, accent.z, 0.32f ) ), 1.5f );
		}
	}
	ImGui::Indent( 10.0f );
	ImGui::TextColored( accent, "%s", title );
	ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + wrapWidth );
	ImGui::TextDisabled( "%s", desc );
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
	if ( ImGui::Button( button, ImVec2( 150, 0 ) ) ) {
		Cbuf_AddText( command );
		Cbuf_AddText( "\n" );
	}
	ImGui::Unindent( 10.0f );
	ImGui::EndChild();
}

static int CL_ImGuiDemoCategory( const char *name ) {
	if ( !Q_stricmpn( name, "speedrun_fullgame_", 18 ) ) return SRGUI_DEMOCAT_FULLGAME;
	if ( !Q_stricmpn( name, "speedrun_mission_", 17 ) ) return SRGUI_DEMOCAT_MISSION;
	if ( !Q_stricmpn( name, "speedrun_il_", 12 ) ) return SRGUI_DEMOCAT_IL;
	return SRGUI_DEMOCAT_OTHER;
}

static const char *CL_ImGuiDemoCategoryName( int category ) {
	switch ( category ) {
	case SRGUI_DEMOCAT_FULLGAME: return "Full Game";
	case SRGUI_DEMOCAT_MISSION: return "Mission";
	case SRGUI_DEMOCAT_IL: return "IL";
	default: return "Other";
	}
}

static void CL_ImGuiLoadDemos( void ) {
	char listBuf[131072];
	char ext[32];
	char *name;
	int count;
	int i;
	int len;

	Com_sprintf( ext, sizeof( ext ), "dm_%d", PROTOCOL_VERSION );
	count = FS_GetFileList( "demos", ext, listBuf, sizeof( listBuf ) );
	if ( count > SRGUI_MAX_DEMOS ) {
		count = SRGUI_MAX_DEMOS;
	}
	s_demoCount = 0;
	name = listBuf;
	Com_sprintf( ext, sizeof( ext ), ".dm_%d", PROTOCOL_VERSION );
	for ( i = 0; i < count; ++i ) {
		len = strlen( name );
		if ( len > 0 ) {
			Q_strncpyz( s_demoList[s_demoCount], name, sizeof( s_demoList[s_demoCount] ) );
			if ( len > (int)strlen( ext ) && !Q_stricmp( s_demoList[s_demoCount] + len - strlen( ext ), ext ) ) {
				s_demoList[s_demoCount][len - strlen( ext )] = '\0';
			}
			s_demoCategory[s_demoCount] = CL_ImGuiDemoCategory( s_demoList[s_demoCount] );
			s_demoCount++;
		}
		name += len + 1;
	}
	if ( s_demoSelected >= s_demoCount ) {
		s_demoSelected = s_demoCount > 0 ? 0 : -1;
	}
	if ( s_demoSelected < 0 && s_demoCount > 0 ) {
		s_demoSelected = 0;
	}
}

static bool CL_ImGuiDemoMatchesFilter( int index ) {
	char lowerName[MAX_QPATH];
	char lowerSearch[64];
	if ( index < 0 || index >= s_demoCount ) {
		return false;
	}
	if ( s_demoFilter != -1 && s_demoCategory[index] != s_demoFilter ) {
		return false;
	}
	if ( s_demoSearch[0] ) {
		Q_strncpyz( lowerName, s_demoList[index], sizeof( lowerName ) );
		Q_strncpyz( lowerSearch, s_demoSearch, sizeof( lowerSearch ) );
		Q_strlwr( lowerName );
		Q_strlwr( lowerSearch );
		return strstr( lowerName, lowerSearch ) != NULL;
	}
	return true;
}

static void CL_ImGuiPlaySelectedDemo( void ) {
	char cmd[MAX_QPATH + 16];
	if ( s_demoSelected < 0 || s_demoSelected >= s_demoCount ) {
		return;
	}
	Com_sprintf( cmd, sizeof( cmd ), "demo %s\n", s_demoList[s_demoSelected] );
	Cbuf_AddText( cmd );
}

static void CL_ImGuiDrawDemoStats( void ) {
	float counts[4] = { 0, 0, 0, 0 };
	int i;
	for ( i = 0; i < s_demoCount; ++i ) {
		if ( s_demoCategory[i] >= 0 && s_demoCategory[i] < 4 ) {
			counts[s_demoCategory[i]] += 1.0f;
		}
	}
	ImGui::BeginChild( "demo_stats", ImVec2( 0, 88 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Demo Statistics" );
	ImGui::PlotHistogram( "##demo_hist", counts, 4, 0, "FG / Mission / IL / Other", 0.0f, counts[0] + counts[1] + counts[2] + counts[3] > 0.0f ? FLT_MAX : 1.0f, ImVec2( 0, 42 ) );
	ImGui::TextDisabled( "FG %.0f   Mission %.0f   IL %.0f   Other %.0f", counts[0], counts[1], counts[2], counts[3] );
	ImGui::EndChild();
}

static bool CL_ImGuiParseRecordAttempts( const char *text, int *attempts, int *completions ) {
	const char *slash = strrchr( text, '/' );
	const char *start;
	const char *end;
	if ( !slash ) {
		return false;
	}
	end = slash;
	while ( end > text && ( end[-1] == ' ' || end[-1] == '\t' ) ) {
		end--;
	}
	start = end;
	while ( start > text && start[-1] >= '0' && start[-1] <= '9' ) {
		start--;
	}
	if ( start == end ) {
		return false;
	}
	*attempts = atoi( start );
	*completions = atoi( slash + 1 );
	return true;
}

static bool CL_ImGuiParseRecordRow( const char *text, char *map, int mapSize, char *gold, int goldSize, char *pb, int pbSize, int *attempts, int *completions ) {
	char nameBuf[64];
	char goldBuf[32];
	char pbBuf[32];
	int att, comp;
	if ( sscanf( text, "%63s %31s %31s %d/%d", nameBuf, goldBuf, pbBuf, &att, &comp ) != 5 ) {
		return false;
	}
	Q_strncpyz( map, nameBuf, mapSize );
	Q_strncpyz( gold, goldBuf, goldSize );
	Q_strncpyz( pb, pbBuf, pbSize );
	*attempts = att;
	*completions = comp;
	return true;
}

static bool CL_ImGuiParseRecordSummary( const char *text, int *attempts, int *completions ) {
	const char *att = strstr( text, "Att:" );
	const char *comp = strstr( text, "Comp:" );
	if ( !att || !comp ) {
		return false;
	}
	*attempts = atoi( att + 4 );
	*completions = atoi( comp + 5 );
	return true;
}

static void CL_ImGuiDrawRecordStats( const float *attempts, const float *completions, int count ) {
	float totalAttempts = 0.0f;
	float totalCompletions = 0.0f;
	int i;
	for ( i = 0; i < count; ++i ) {
		totalAttempts += attempts[i];
		totalCompletions += completions[i];
	}
	ImGui::BeginChild( "record_stats", ImVec2( 0, 156 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Records Statistics" );
	ImGui::TextDisabled( "Visible rows: %d    Attempts: %.0f    Completions: %.0f", count, totalAttempts, totalCompletions );
	if ( count > 0 && totalAttempts > 0.0f ) {
		ImGui::PlotHistogram( "Attempts", attempts, count, 0, NULL, 0.0f, FLT_MAX, ImVec2( 0, 46 ) );
		ImGui::PlotHistogram( "Completions", completions, count, 0, NULL, 0.0f, totalCompletions > 0.0f ? FLT_MAX : 1.0f, ImVec2( 0, 46 ) );
	} else if ( count > 0 ) {
		ImGui::TextWrapped( "Records were loaded, but these rows have no attempts yet. Start/refresh a category with saved split history to fill the charts." );
	} else {
		ImGui::TextDisabled( "Refresh records to populate charts." );
	}
	ImGui::EndChild();
}

static void CL_ImGuiSectionHeader( const char *title, const char *hint ) {
	ImGui::Spacing();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", title );
	if ( hint && hint[0] ) {
		ImGui::TextDisabled( "%s", hint );
	}
	ImGui::Separator();
	ImGui::Spacing();
}

static void CL_ImGuiMiniStat( const char *label, const char *value ) {
	ImGui::BeginChild( label, ImVec2( 0, 58 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImGuiStyle &style = ImGui::GetStyle();
	ImVec2 p0 = ImGui::GetWindowPos();
	ImVec2 p1 = ImVec2( p0.x + 5.0f, p0.y + ImGui::GetWindowHeight() - 1.0f );
	draw->AddRectFilled( ImVec2( p0.x + 1.0f, p0.y + 1.0f ), p1, IM_COL32( 110, 220, 62, 180 ), style.ChildRounding > 1.0f ? style.ChildRounding - 1.0f : 0.0f, ImDrawFlags_RoundCornersLeft );
	float textBlock = ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y;
	float y = ( ImGui::GetWindowHeight() - textBlock ) * 0.5f;
	if ( y < 6.0f ) y = 6.0f;
	ImGui::SetCursorPos( ImVec2( 18.0f, y ) );
	ImGui::TextDisabled( "%s", label );
	ImGui::SetCursorPosX( 18.0f );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", value && value[0] ? value : "---" );
	ImGui::EndChild();
}

static const char *CL_ImGuiMapLabel( const char *mapValue ) {
	static const char *mapLabels[] = {
		"Escape!", "Castle Keep", "Tram Ride", "Village", "Catacombs", "Crypt", "Church", "Tomb", "Forest Compound", "Rocket Base", "Radar Installation", "Air Base Assault", "Kugelstadt", "The Bombed Factory", "The Trainyards", "Secret Weapons Facility", "Ice Station Norway", "X-Labs", "Super Soldier", "Bramburg Dam", "Paderborn Village", "Chateau Schufstaffel", "Unhallowed Ground", "The Dig", "Return to Castle Wolfenstein", "Heinrich"
	};
	static const char *mapValues[] = {
		"escape1", "escape2", "tram", "village1", "crypt1", "crypt2", "church", "boss1", "forest", "rocket", "baseout", "assault", "sfm", "factory", "trainyard", "swf", "norway", "xlabs", "boss2", "dam", "village2", "chateau", "dark", "dig", "castle", "end"
	};
	int i;
	int legacyIndex = atoi( mapValue ? mapValue : "" );
	if ( legacyIndex >= 1 && legacyIndex <= IM_ARRAYSIZE( mapLabels ) ) {
		return mapLabels[legacyIndex - 1];
	}
	for ( i = 0; i < IM_ARRAYSIZE( mapValues ); ++i ) {
		if ( mapValue && !Q_stricmp( mapValue, mapValues[i] ) ) {
			return mapLabels[i];
		}
	}
	return mapValue && mapValue[0] ? mapValue : "Escape!";
}

static void CL_ImGuiDrawAccentBanner( void ) {
	ImVec2 p = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	float shift = s_imguiAnimations && s_imguiAnimations->integer != 0 ? ( 0.04f * sinf( (float)ImGui::GetTime() * 1.8f ) ) : 0.0f;
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( p, ImVec2( p.x + w, p.y + 4.0f ), IM_COL32( 92, 210, 54, 220 ), 2.0f );
	draw->AddRectFilled( ImVec2( p.x + w * ( 0.34f + shift ), p.y ), ImVec2( p.x + w * ( 0.52f + shift ), p.y + 4.0f ), IM_COL32( 244, 188, 62, 220 ), 2.0f );
	draw->AddRectFilled( ImVec2( p.x + w * ( 0.58f - shift ), p.y ), ImVec2( p.x + w * ( 0.72f - shift ), p.y + 4.0f ), IM_COL32( 158, 224, 72, 170 ), 2.0f );
	ImGui::Dummy( ImVec2( w, 10.0f ) );
}

static void CL_ImGuiDrawSoftGlow( ImDrawList *draw, ImVec2 center, float radius, int r, int g, int b, int maxAlpha ) {
	int i;
	for ( i = 0; i < 14; ++i ) {
		float layer = (float)( 14 - i ) / 14.0f;
		float rr = radius * ( 0.55f + layer * 0.65f );
		int alpha = (int)( maxAlpha * ( 1.0f - layer ) * ( 1.0f - layer ) );
		if ( alpha > 0 ) {
			draw->AddCircleFilled( center, rr, IM_COL32( r, g, b, alpha ), 72 );
		}
	}
}

static void CL_ImGuiDrawBackgroundMist( void ) {
	if ( !s_imguiAnimations || s_imguiAnimations->integer == 0 ) {
		return;
	}
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 wp = ImGui::GetWindowPos();
	ImVec2 ws = ImGui::GetWindowSize();
	float t = (float)ImGui::GetTime();
	float x1 = wp.x + ws.x * ( 0.20f + 0.04f * sinf( t * 0.45f ) );
	float y1 = wp.y + ws.y * ( 0.22f + 0.03f * cosf( t * 0.38f ) );
	float x2 = wp.x + ws.x * ( 0.72f + 0.05f * sinf( t * 0.31f + 1.4f ) );
	float y2 = wp.y + ws.y * ( 0.70f + 0.04f * cosf( t * 0.29f + 0.7f ) );
	draw->AddRectFilledMultiColor( ImVec2( wp.x, wp.y + ws.y * 0.10f ), ImVec2( wp.x + ws.x, wp.y + ws.y * 0.58f ), IM_COL32( 72, 150, 48, 0 ), IM_COL32( 72, 150, 48, 12 ), IM_COL32( 72, 150, 48, 2 ), IM_COL32( 72, 150, 48, 0 ) );
	draw->AddRectFilledMultiColor( ImVec2( wp.x, wp.y + ws.y * 0.42f ), ImVec2( wp.x + ws.x, wp.y + ws.y ), IM_COL32( 236, 186, 58, 0 ), IM_COL32( 236, 186, 58, 3 ), IM_COL32( 236, 186, 58, 10 ), IM_COL32( 236, 186, 58, 0 ) );
	CL_ImGuiDrawSoftGlow( draw, ImVec2( x1, y1 ), ws.x * 0.30f, 74, 170, 50, 13 );
	CL_ImGuiDrawSoftGlow( draw, ImVec2( x2, y2 ), ws.x * 0.24f, 236, 186, 58, 8 );
	draw->AddLine( ImVec2( wp.x + 18.0f, wp.y + ws.y - 42.0f + sinf( t * 0.7f ) * 4.0f ), ImVec2( wp.x + ws.x - 20.0f, wp.y + ws.y - 54.0f + cosf( t * 0.55f ) * 4.0f ), IM_COL32( 110, 220, 62, 24 ), 1.0f );
}

static int CL_ImGuiWinKeyToQuake( unsigned int vk, long lParam ) {
	bool extended = ( lParam & ( 1 << 24 ) ) != 0;
	if ( vk >= 'A' && vk <= 'Z' ) return (int)( vk + 32 );
	if ( vk >= '0' && vk <= '9' ) return (int)vk;
	if ( vk >= VK_F1 && vk <= VK_F12 ) return K_F1 + (int)( vk - VK_F1 );
	switch ( vk ) {
	case VK_TAB: return K_TAB;
	case VK_RETURN: return extended ? K_KP_ENTER : K_ENTER;
	case VK_ESCAPE: return K_ESCAPE;
	case VK_SPACE: return K_SPACE;
	case VK_BACK: return K_BACKSPACE;
	case VK_UP: return extended ? K_UPARROW : K_KP_UPARROW;
	case VK_DOWN: return extended ? K_DOWNARROW : K_KP_DOWNARROW;
	case VK_LEFT: return extended ? K_LEFTARROW : K_KP_LEFTARROW;
	case VK_RIGHT: return extended ? K_RIGHTARROW : K_KP_RIGHTARROW;
	case VK_HOME: return extended ? K_HOME : K_KP_HOME;
	case VK_END: return extended ? K_END : K_KP_END;
	case VK_PRIOR: return extended ? K_PGUP : K_KP_PGUP;
	case VK_NEXT: return extended ? K_PGDN : K_KP_PGDN;
	case VK_INSERT: return extended ? K_INS : K_KP_INS;
	case VK_DELETE: return extended ? K_DEL : K_KP_DEL;
	case VK_SHIFT: return K_SHIFT;
	case VK_CONTROL: return K_CTRL;
	case VK_MENU: return K_ALT;
	case VK_OEM_MINUS: return '-';
	case VK_OEM_PLUS: return '=';
	case VK_OEM_COMMA: return ',';
	case VK_OEM_PERIOD: return '.';
	case VK_OEM_1: return ';';
	case VK_OEM_2: return '/';
	case VK_OEM_3: return '`';
	case VK_OEM_4: return '[';
	case VK_OEM_5: return '\\';
	case VK_OEM_6: return ']';
	case VK_OEM_7: return '\'';
	}
	return 0;
}

static void CL_ImGuiAssignPendingBind( int keynum ) {
	if ( !s_pendingBindCommand ) {
		return;
	}
	if ( keynum > 0 && keynum != K_ESCAPE ) {
		Key_SetBinding( keynum, s_pendingBindCommand );
	}
	s_pendingBindCommand = NULL;
}

static void CL_ImGuiBindingRow( const char *label, const char *command ) {
	int keynum = Key_GetKey( command );
	const char *keyName = keynum > 0 ? Key_KeynumToString( keynum, qtrue ) : "Unbound";
	ImGui::PushID( command );
	ImGui::BeginChild( label, ImVec2( 0, 48 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetWindowPos();
	ImVec2 p2 = ImVec2( p.x + ImGui::GetWindowWidth(), p.y + ImGui::GetWindowHeight() );
	bool waiting = s_pendingBindCommand == command;
	bool hovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
	if ( hovered || waiting ) {
		float a = waiting ? 0.24f : 0.10f;
		if ( s_imguiAnimations && s_imguiAnimations->integer != 0 ) {
			a += 0.05f * sinf( (float)ImGui::GetTime() * 5.0f );
		}
		draw->AddRectFilled( ImVec2( p.x + 1.0f, p.y + 1.0f ), ImVec2( p2.x - 1.0f, p2.y - 1.0f ), ImGui::ColorConvertFloat4ToU32( ImVec4( 0.32f, 0.72f, 0.20f, a ) ), 8.0f );
		draw->AddRect( ImVec2( p.x + 1.0f, p.y + 1.0f ), ImVec2( p2.x - 1.0f, p2.y - 1.0f ), IM_COL32( 100, 220, 64, waiting ? 220 : 120 ), 8.0f, 0, waiting ? 1.5f : 1.0f );
	}
	ImGui::SetCursorPos( ImVec2( 14.0f, 8.0f ) );
	ImGui::Text( "%s", label );
	ImGui::SetCursorPos( ImVec2( 14.0f, 27.0f ) );
	ImGui::TextDisabled( "%s", command );
	ImGui::SetCursorPos( ImVec2( ImGui::GetWindowWidth() - 292.0f, 11.0f ) );
	ImGui::TextColored( waiting ? ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ) : ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", waiting ? "press key..." : keyName );
	ImGui::SameLine( ImGui::GetWindowWidth() - 170.0f );
	if ( ImGui::SmallButton( "Bind" ) ) {
		s_pendingBindCommand = command;
	}
	ImGui::SameLine();
	if ( ImGui::SmallButton( "Clear" ) && keynum > 0 ) {
		Key_SetBinding( keynum, "" );
	}
	ImGui::EndChild();
	ImGui::PopID();
}

static void CL_ImGuiDrawTimerPage( void ) {
	static const char *typeLabels[] = { "In-Game Only", "External LiveSplit" };
	static const int typeValues[] = { 0, 1 };
	static const char *modeLabels[] = { "Full Game", "Chapter", "Individual Level" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *missionLabels[] = { "1: Ominous Rumors", "2: Vengeance", "3: Deadly Designs", "4: Deathshead", "5: Resurrection" };
	static const int missionValues[] = { 1, 2, 3, 4, 5 };
	static const char *compareLabels[] = { "Personal Best", "Best Segments", "Average Segments" };
	static const int compareValues[] = { 0, 1, 2 };
	static const char *timingLabels[] = { "Game Time", "Real Time" };
	static const int timingValues[] = { 0, 1 };
	static const char *mapLabels[] = {
		"Escape!", "Castle Keep", "Tram Ride", "Village", "Catacombs", "Crypt", "Church", "Tomb", "Forest Compound", "Rocket Base", "Radar Installation", "Air Base Assault", "Kugelstadt", "The Bombed Factory", "The Trainyards", "Secret Weapons Facility", "Ice Station Norway", "X-Labs", "Super Soldier", "Bramburg Dam", "Paderborn Village", "Chateau Schufstaffel", "Unhallowed Ground", "The Dig", "Return to Castle Wolfenstein", "Heinrich"
	};
	static const char *mapValues[] = {
		"escape1", "escape2", "tram", "village1", "crypt1", "crypt2", "church", "boss1", "forest", "rocket", "baseout", "assault", "sfm", "factory", "trainyard", "swf", "norway", "xlabs", "boss2", "dam", "village2", "chateau", "dark", "dig", "castle", "end"
	};

	CL_ImGuiSectionHeader( "Timer Configuration", "Core LiveSplit and in-game timer options for the current run." );
	ImGui::BeginChild( "timer_card", ImVec2( 0, 218 ), true );
	CL_ImGuiBoolCvar( "Enable Timer", s_cg_livesplit );
	CL_ImGuiComboCvar( "LiveSplit Type", s_ls_type, typeLabels, typeValues, IM_ARRAYSIZE( typeValues ) );
	CL_ImGuiComboCvar( "Run Mode", s_ls_mode, modeLabels, modeValues, IM_ARRAYSIZE( modeValues ) );
	CL_ImGuiComboCvar( "Chapter", s_ls_mission, missionLabels, missionValues, IM_ARRAYSIZE( missionValues ) );
	CL_ImGuiStringComboCvarName( "IL Map", "ls_map", "escape1", mapLabels, mapValues, IM_ARRAYSIZE( mapValues ) );
	CL_ImGuiBoolCvar( "100% Category", s_ls_100pct );
	CL_ImGuiComboCvar( "Compare Against", s_ls_compare, compareLabels, compareValues, IM_ARRAYSIZE( compareValues ) );
	CL_ImGuiComboCvar( "Timing Method", s_ls_timing, timingLabels, timingValues, IM_ARRAYSIZE( timingValues ) );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Element Visibility", NULL );
	ImGui::BeginChild( "visibility_card", ImVec2( 0, 170 ), true );
	ImGui::Columns( 2, NULL, false );
	CL_ImGuiBoolCvar( "Main Timer", s_ls_showtimer );
	CL_ImGuiBoolCvar( "Headers", s_ls_showheaders );
	CL_ImGuiBoolCvarName( "PB Label", "ls_showpb", "1" );
	CL_ImGuiBoolCvarName( "RGT", "ls_showrgt", "0" );
	CL_ImGuiBoolCvarName( "PB Delta (+/-)", "ls_showdeltas", "1" );
	CL_ImGuiBoolCvarName( "Attempt Counter", "ls_showatt", "1" );
	ImGui::NextColumn();
	CL_ImGuiBoolCvar( "Segment Timer", s_ls_showsegtimer );
	CL_ImGuiBoolCvar( "Statistics", s_ls_showstats );
	CL_ImGuiBoolCvarName( "Best Label", "ls_showbest", "1" );
	CL_ImGuiBoolCvarName( "Separators", "ls_showseps", "1" );
	CL_ImGuiBoolCvarName( "Best Delta (+/-)", "ls_showbestdeltas", "1" );
	ImGui::Columns( 1 );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Demo Recording", NULL );
	ImGui::BeginChild( "demo_card", ImVec2( 0, 76 ), true );
	CL_ImGuiBoolCvar( "Auto-Record Demos", s_sp_autorecord );
	ImGui::TextDisabled( "Automatically records a demo when the timer starts." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawColorsPage( void ) {
	static const float ahead[4] = { 0.25f, 0.85f, 0.25f, 1.00f };
	static const float behind[4] = { 0.85f, 0.25f, 0.25f, 1.00f };
	static const float gold[4] = { 1.00f, 0.85f, 0.20f, 1.00f };
	static const float header[4] = { 0.35f, 0.75f, 0.20f, 1.00f };
	static const float timer[4] = { 0.70f, 0.95f, 0.50f, 1.00f };
	static const float text[4] = { 0.88f, 0.91f, 0.86f, 1.00f };
	static const float bg[4] = { 0.03f, 0.04f, 0.03f, 0.82f };
	static const float border[4] = { 0.25f, 0.42f, 0.18f, 0.70f };
	static const float mapname[4] = { 0.55f, 0.62f, 0.50f, 0.85f };
	static const float current[4] = { 0.18f, 0.30f, 0.12f, 0.85f };
	static const float completed[4] = { 0.45f, 0.75f, 0.32f, 1.00f };
	static const float future[4] = { 0.45f, 0.50f, 0.43f, 0.82f };
	static const float dim[4] = { 0.32f, 0.36f, 0.30f, 0.70f };
	static const float paused[4] = { 1.00f, 0.75f, 0.25f, 1.00f };

	CL_ImGuiSectionHeader( "LiveSplit Colors", "All overlay colors in one scrollable list." );
	ImGui::BeginChild( "colors_card", ImVec2( 0, 0 ), true );
	CL_ImGuiColorCvarName( "Ahead", "ls_clr_ahead", ahead );
	CL_ImGuiColorCvarName( "Behind", "ls_clr_behind", behind );
	CL_ImGuiColorCvarName( "Gold", "ls_clr_gold", gold );
	CL_ImGuiColorCvarName( "Header", "ls_clr_header", header );
	CL_ImGuiColorCvarName( "Timer", "ls_clr_timer", timer );
	CL_ImGuiColorCvarName( "Text", "ls_clr_text", text );
	CL_ImGuiColorCvarName( "Paused", "ls_clr_paused", paused );
	CL_ImGuiColorCvarName( "Background", "ls_clr_bg", bg );
	CL_ImGuiColorCvarName( "Border", "ls_clr_border", border );
	CL_ImGuiColorCvarName( "Map Name", "ls_clr_mapname", mapname );
	CL_ImGuiColorCvarName( "Current Split", "ls_clr_current", current );
	CL_ImGuiColorCvarName( "Completed", "ls_clr_completed", completed );
	CL_ImGuiColorCvarName( "Future", "ls_clr_future", future );
	CL_ImGuiColorCvarName( "Dim", "ls_clr_dim", dim );
	CL_ImGuiColorCvarName( "Segment Timer", "ls_clr_segtimer", timer );
	CL_ImGuiColorCvarName( "Separator", "ls_clr_sep", border );
	CL_ImGuiColorCvarName( "Highlight", "ls_clr_highlight", current );
	CL_ImGuiColorCvarName( "Label", "ls_clr_label", mapname );
	ImGui::EndChild();
}

static void CL_ImGuiDrawDisplayPage( void ) {
	static const char *alignLabels[] = { "Left", "Right" };
	static const int alignValues[] = { 0, 1 };

	CL_ImGuiSectionHeader( "LiveSplit Panel Layout", "These controls affect the existing in-game LiveSplit panel only." );
	ImGui::BeginChild( "display_layout_card", ImVec2( 0, 210 ), true );
	CL_ImGuiComboCvarName( "Panel Side", "ls_align", "0", alignLabels, alignValues, IM_ARRAYSIZE( alignValues ) );
	CL_ImGuiSliderCvarName( "X Position", "ls_x", "8", 0.0f, 580.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Y Position", "ls_y", "80", 0.0f, 440.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Width", "ls_w", "178", 80.0f, 400.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Scale", "ls_scale", "1.0", 0.5f, 2.5f, "%.2f" );
	CL_ImGuiIntSliderCvarName( "Max Visible Splits", "ls_maxrows", "0", 0, 20 );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Transparency", NULL );
	ImGui::BeginChild( "display_opacity_card", ImVec2( 0, 116 ), true );
	CL_ImGuiSliderCvarName( "Splits Opacity", "ls_opacity", "1.0", 0.0f, 1.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Splits In Menu", "ls_opacity_ui", "0.2", 0.0f, 1.0f, "%.2f" );
	ImGui::TextDisabled( "Background alpha is now controlled by Style > Background color alpha." );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Text", NULL );
	ImGui::BeginChild( "display_text_card", ImVec2( 0, 72 ), true );
	CL_ImGuiBoolCvarName( "Text Shadow", "ls_text_shadow", "1" );
	ImGui::TextDisabled( "Colors and LiveSplit layout are grouped in this Style page." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawHudPage( void ) {
	static const char *mouseLabels[] = { "Off", "Clicks Only", "Clicks + Direction" };
	static const int mouseValues[] = { 0, 1, 2 };

	CL_ImGuiSectionHeader( "Keystrokes", "X/Y = 0 keeps the automatic centered position." );
	ImGui::BeginChild( "hud_keys_card", ImVec2( 0, 188 ), true );
	CL_ImGuiBoolCvarName( "Show Keystrokes", "cg_drawKeys", "0" );
	CL_ImGuiSliderCvarName( "Keys X Position", "ks_x", "0", 0.0f, 600.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Keys Y Position", "ks_y", "0", 0.0f, 460.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Keys Scale", "ks_scale", "1.0", 0.3f, 3.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Keys Opacity", "ks_opacity", "1.0", 0.0f, 1.0f, "%.2f" );
	CL_ImGuiComboCvarName( "Mouse Display", "ks_mouse", "0", mouseLabels, mouseValues, IM_ARRAYSIZE( mouseValues ) );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "HUD Elements", NULL );
	ImGui::BeginChild( "hud_elements_card", ImVec2( 0, 150 ), true );
	ImGui::Columns( 2, NULL, false );
	CL_ImGuiBoolCvarName( "Show LiveSplit", "ls_draw", "1" );
	CL_ImGuiBoolCvarName( "Speedometer", "cg_drawVelocity", "0" );
	CL_ImGuiBoolCvarName( "Position HUD", "cg_drawPos", "0" );
	ImGui::NextColumn();
	CL_ImGuiBoolCvarName( "Jump Statistics", "cg_drawJumpStats", "0" );
	CL_ImGuiBoolCvarName( "Show FPS", "cg_drawfps", "0" );
	CL_ImGuiBoolCvarName( "Show Timer", "cg_drawTimer", "0" );
	ImGui::Columns( 1 );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Standalone IGT Timer", NULL );
	ImGui::BeginChild( "hud_igt_card", ImVec2( 0, 164 ), true );
	CL_ImGuiBoolCvarName( "Show IGT Timer", "ls_igttimer", "0" );
	CL_ImGuiSliderCvarName( "IGT X Position", "ls_igttimer_x", "280", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "IGT Y Position", "ls_igttimer_y", "440", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "IGT Scale", "ls_igttimer_scale", "1.0", 0.3f, 4.0f, "%.2f" );
	CL_ImGuiBoolCvarName( "Show IGT Segment Timer", "ls_igtsegtimer", "0" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawStylePage( void ) {
	CL_ImGuiDrawDisplayPage();
	CL_ImGuiDrawColorsPage();
}

static void CL_ImGuiDrawSpeedoPage( void ) {
	static const char *modeLabels[] = { "3D (Full)", "Horizontal (XY)", "Vertical (Z)" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *sizeLabels[] = { "Tiny", "Small", "Big", "Giant" };
	static const int sizeValues[] = { 0, 1, 2, 3 };
	static const char *posLabels[] = { "Bottom", "Center" };
	static const int posValues[] = { 0, 1 };

	CL_ImGuiSectionHeader( "Speedometer", "Position 0/0 uses the old automatic placement." );
	ImGui::BeginChild( "speedo_card", ImVec2( 0, 226 ), true );
	CL_ImGuiComboCvarName( "Mode", "cg_velocity_mode", "0", modeLabels, modeValues, IM_ARRAYSIZE( modeValues ) );
	CL_ImGuiComboCvarName( "Text Size", "cg_velocity_size", "1", sizeLabels, sizeValues, IM_ARRAYSIZE( sizeValues ) );
	CL_ImGuiComboCvarName( "Position", "cg_velocity_type", "0", posLabels, posValues, IM_ARRAYSIZE( posValues ) );
	CL_ImGuiSliderCvarName( "Scale", "cg_velocity_scale", "1.0", 0.25f, 4.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "X Position", "cg_velocity_x", "0", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Y Position", "cg_velocity_y", "0", 0.0f, 480.0f, "%.0f" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "FPS / Timer Placement", NULL );
	ImGui::BeginChild( "fps_card", ImVec2( 0, 132 ), true );
	CL_ImGuiSliderCvarName( "Scale", "cg_fpsScale", "1.0", 0.25f, 4.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "X Position", "cg_fpsX", "500", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Y Position", "cg_fpsY", "0", 0.0f, 440.0f, "%.0f" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawOverlayPage( void ) {
	CL_ImGuiDrawHudPage();
	CL_ImGuiDrawSpeedoPage();
}

static void CL_ImGuiDrawMovementPage( void ) {
	cvar_t *bhMovement = CL_ImGuiCvar( "bh_movement", "0" );
	bool hlBhopEnabled = bhMovement && bhMovement->integer != 0;
	CL_ImGuiSectionHeader( "Bunny Hop / Physics", NULL );
	ImGui::BeginChild( "movement_card", ImVec2( 0, 130 ), true );
	CL_ImGuiBoolCvar( "HL1 Bhop Physics", bhMovement, "No air speed cap + bunny hop acceleration." );
	ImGui::BeginDisabled( !hlBhopEnabled );
	CL_ImGuiBoolCvarName( "Auto Jump (hold space)", "bh_autojump", "0", "Only intended for HL movement categories." );
	ImGui::EndDisabled();
	if ( !hlBhopEnabled ) {
		ImGui::TextDisabled( "Auto Jump is locked until HL1 Bhop Physics is enabled." );
	}
	ImGui::Spacing();
	ImGui::TextWrapped( "Changing movement category options can affect run validity; use livesplit_check before submitting a run." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawViewPage( void ) {
	static const char *fpsLabels[] = { "30", "60", "83", "90", "125", "142" };
	static const int fpsValues[] = { 30, 60, 83, 90, 125, 142 };
	static const char *gunLabels[] = { "Hidden", "Right Hand", "Left Hand" };
	static const int gunValues[] = { 0, 1, 2 };

	CL_ImGuiSectionHeader( "Field of View", NULL );
	ImGui::BeginChild( "view_fov_card", ImVec2( 0, 138 ), true );
	CL_ImGuiSliderCvarName( "FOV Front-Back", "cg_fov", "90", 60.0f, 160.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "FOV Down-Up", "cg_fov_down", "90", 60.0f, 160.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "FOV Left-Right", "cg_fov_lr", "90", 0.0f, 160.0f, "%.0f" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Framerate / Weapon", NULL );
	ImGui::BeginChild( "view_misc_card", ImVec2( 0, 104 ), true );
	CL_ImGuiComboCvarName( "Max FPS", "com_maxfps", "125", fpsLabels, fpsValues, IM_ARRAYSIZE( fpsValues ) );
	CL_ImGuiComboCvarName( "Weapon Hand", "cg_drawGun", "1", gunLabels, gunValues, IM_ARRAYSIZE( gunValues ) );
	ImGui::EndChild();
}

static void CL_ImGuiDrawGhostPage( void );

static void CL_ImGuiDrawGamePage( void ) {
	CL_ImGuiDrawMovementPage();
	CL_ImGuiDrawViewPage();
	CL_ImGuiDrawGhostPage();
}

static void CL_ImGuiDrawBindsPage( void ) {
	CL_ImGuiSectionHeader( "Bind Editor", "Click Bind, press a key or mouse button, Escape cancels. Existing bindings are preserved until replaced." );
	ImGui::BeginChild( "binds_card", ImVec2( 0, 0 ), true );
	ImGui::BeginChild( "binds_intro", ImVec2( 0, 74 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Controls" );
	ImGui::TextWrapped( "Bind the commands you use during runs, routing and demo review. The current key is shown on the right." );
	ImGui::EndChild();
	ImGui::Spacing();
	ImGui::TextDisabled( "Run controls" );
	CL_ImGuiBindingRow( "Start Run", "livesplit_start" );
	CL_ImGuiBindingRow( "Pause / Resume", "livesplit_pause" );
	CL_ImGuiBindingRow( "Undo Split", "livesplit_undo" );
	CL_ImGuiBindingRow( "Skip Split", "livesplit_skip" );
	CL_ImGuiBindingRow( "Reset Run", "livesplit_reset" );
	CL_ImGuiBindingRow( "Reset No Save", "livesplit_reset_nosave" );
	CL_ImGuiBindingRow( "Reset Category", "livesplit_reset_category" );
	CL_ImGuiBindingRow( "Check Settings", "livesplit_check" );
	ImGui::Separator();
	ImGui::TextDisabled( "Demo controls" );
	CL_ImGuiBindingRow( "Demo Pause", "demo_pause" );
	CL_ImGuiBindingRow( "Demo Speed Up", "demo_speedup" );
	CL_ImGuiBindingRow( "Demo Slow Down", "demo_slowdown" );
	CL_ImGuiBindingRow( "Demo Skip Forward", "demo_skipforward" );
	CL_ImGuiBindingRow( "Demo Rewind", "demo_skipbackward" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawHelpPage( void ) {
	CL_ImGuiSectionHeader( "Speedrun Help", "Short operational reference." );
	ImGui::BeginChild( "help_card", ImVec2( 0, 0 ), true );
	ImGui::BeginChild( "help_intro", ImVec2( 0, 86 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "Quick reference" );
	ImGui::TextWrapped( "This page collects the commands that matter while routing, practicing, recording and reviewing speedruns." );
	ImGui::TextDisabled( "Use Controls to bind the most common actions." );
	ImGui::EndChild();
	ImGui::Spacing();
	if ( ImGui::CollapsingHeader( "Getting started", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		ImGui::BulletText( "Open or close the panel with speedrun_gui." );
		ImGui::BulletText( "Choose run mode, category and IL map on the Timer page." );
		ImGui::BulletText( "Run livesplit_check before submitting a run." );
	}
	if ( ImGui::CollapsingHeader( "Run controls", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		ImGui::BulletText( "livesplit_start starts a run." );
		ImGui::BulletText( "livesplit_reset resets with confirmation logic." );
		ImGui::BulletText( "livesplit_pause toggles IGT pause/resume." );
		ImGui::BulletText( "livesplit_undo and livesplit_skip manage the current split." );
	}
	if ( ImGui::CollapsingHeader( "Demos and records" ) ) {
		ImGui::BulletText( "Auto-record is on Timer > Demo Recording." );
		ImGui::BulletText( "Demo playback controls are on the Demos page." );
		ImGui::BulletText( "demo_skipforward / demo_skipbackward move through the current demo." );
		ImGui::BulletText( "Records page reads the current split and history rows." );
	}
	if ( ImGui::CollapsingHeader( "Practice tools", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		ImGui::BulletText( "savepos <slot> stores player position, velocity and view angles." );
		ImGui::BulletText( "loadpos <slot> restores the saved practice position." );
		ImGui::BulletText( "rewind <ms> restores recent player-only state from the rewind buffer." );
		ImGui::BulletText( "rewind <ms> full restores a fuller snapshot with entities/triggers where available." );
		ImGui::BulletText( "Use these for practice/routing only; they are not run-valid actions." );
	}
	if ( ImGui::CollapsingHeader( "Style and layout" ) ) {
		ImGui::BulletText( "Style combines LiveSplit panel layout, opacity and color editing." );
		ImGui::BulletText( "Background alpha is edited through the Background color alpha." );
		ImGui::BulletText( "GUI button opens panel style settings like window opacity, rounding and animations." );
	}
	if ( ImGui::CollapsingHeader( "Developer tools" ) ) {
		ImGui::BulletText( "Dev visualization controls are disabled until sv_cheats is active." );
		ImGui::BulletText( "Enable sv_cheats in Dev Tools; the button runs the command twice for the confirmation guard." );
		ImGui::BulletText( "Default Fonts and some render options may require vid_restart." );
	}
	if ( ImGui::CollapsingHeader( "Recovery" ) ) {
		ImGui::BulletText( "Use Reset on color rows to clear custom LiveSplit colors." );
		ImGui::BulletText( "Use the Actions page if a bind is missing while testing." );
	}
	if ( ImGui::CollapsingHeader( "Useful commands", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		ImGui::BulletText( "speedrun_gui - toggle this panel" );
		ImGui::BulletText( "livesplit_check - verify current speedrun settings" );
		ImGui::BulletText( "vid_restart - apply font/renderer changes when required" );
		ImGui::BulletText( "record <name> / stoprecord - manual demo recording" );
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawDemosPage( void ) {
	int i;
	int visibleCount = 0;
	static const char *filterLabels[] = { "All", "Full Game", "Mission", "Individual Level", "Other" };
	static const int filterValues[] = { -1, SRGUI_DEMOCAT_FULLGAME, SRGUI_DEMOCAT_MISSION, SRGUI_DEMOCAT_IL, SRGUI_DEMOCAT_OTHER };

	CL_ImGuiSectionHeader( "Demo Browser", "Lists demos from demos/*.dm_49 and uses the normal demo command to play them." );
	ImGui::BeginChild( "demos_card", ImVec2( 0, 0 ), true );
	if ( ImGui::Button( "Refresh Demos", ImVec2( 130, 0 ) ) || s_demoCount == 0 ) {
		CL_ImGuiLoadDemos();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Play Selected", ImVec2( 130, 0 ) ) ) {
		CL_ImGuiPlaySelectedDemo();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Open demos folder", ImVec2( 150, 0 ) ) ) {
		Cbuf_AddText( "dir demos\n" );
	}
	ImGui::Spacing();
	if ( ImGui::BeginCombo( "Filter", filterLabels[s_demoFilter == -1 ? 0 : s_demoFilter + 1] ) ) {
		for ( i = 0; i < IM_ARRAYSIZE( filterLabels ); ++i ) {
			bool selected = s_demoFilter == filterValues[i];
			if ( ImGui::Selectable( filterLabels[i], selected ) ) {
				s_demoFilter = filterValues[i];
			}
			if ( selected ) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 210.0f );
	ImGui::InputTextWithHint( "Search", "demo name...", s_demoSearch, sizeof( s_demoSearch ) );
	ImGui::BeginChild( "demo_list", ImVec2( 0, 178 ), true );
	ImGui::Columns( 2, NULL, false );
	ImGui::TextDisabled( "Demo" );
	ImGui::NextColumn();
	ImGui::TextDisabled( "Type" );
	ImGui::NextColumn();
	ImGui::Separator();
	for ( i = 0; i < s_demoCount; ++i ) {
		if ( !CL_ImGuiDemoMatchesFilter( i ) ) {
			continue;
		}
		visibleCount++;
		ImGui::PushID( i );
		if ( ImGui::Selectable( s_demoList[i], s_demoSelected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) ) {
			s_demoSelected = i;
			if ( ImGui::IsMouseDoubleClicked( 0 ) ) {
				CL_ImGuiPlaySelectedDemo();
			}
		}
		ImGui::NextColumn();
		ImGui::TextDisabled( "%s", CL_ImGuiDemoCategoryName( s_demoCategory[i] ) );
		ImGui::NextColumn();
		ImGui::PopID();
	}
	ImGui::Columns( 1 );
	if ( visibleCount == 0 ) {
		ImGui::TextDisabled( "No demos match the current filter/search." );
	}
	ImGui::EndChild();
	ImGui::TextDisabled( "Selected: %s", ( s_demoSelected >= 0 && s_demoSelected < s_demoCount ) ? s_demoList[s_demoSelected] : "none" );
	ImGui::Separator();
	CL_ImGuiDrawDemoStats();
	ImGui::Separator();
	CL_ImGuiActionCard( "Pause / Resume", "Freeze or resume demo playback.", "Pause", "demo_pause", ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ) );
	CL_ImGuiActionCard( "Speed Control", "Step demo speed up or down while reviewing runs.", "Speed Up", "demo_speedup", ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ) );
	CL_ImGuiCommandButton( "Slow Down", "demo_slowdown", "reduce playback speed" );
	CL_ImGuiCommandButton( "Skip Forward", "demo_skipforward", "jump ahead" );
	CL_ImGuiCommandButton( "Rewind", "demo_skipbackward", "jump backward" );
	CL_ImGuiCommandButton( "Freecam", "demo_freecam", "toggle free camera" );
	CL_ImGuiCommandButton( "Toggle HUD", "demo_togglehud", "hide/show HUD" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawGhostPage( void ) {
	CL_ImGuiSectionHeader( "Ghost Replay", NULL );
	ImGui::BeginChild( "ghost_card", ImVec2( 0, 132 ), true );
	CL_ImGuiBoolCvarName( "Show Ghost", "ls_ghost", "0" );
	CL_ImGuiIntSliderCvarName( "Opacity", "ls_ghost_opacity", "60", 5, 255 );
	ImGui::TextWrapped( "Ghost displays your best split as a translucent model and requires a saved ghost recording for the current map." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawRecordsPage( void ) {
	char rowName[32];
	char rowText[256];
	char statsText[256];
	int i;
	int recordCount = 0;
	int summaryAtt = 0;
	int summaryComp = 0;
	float attempts[10] = { 0 };
	float completions[10] = { 0 };
	static const char *modeLabels[] = { "Full Game", "Chapter", "Individual Level" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *diffLabels[] = { "Don't hurt me.", "Bring 'em on!", "I am Death incarnate!" };
	static const int diffValues[] = { 1, 2, 3 };
	static const char *missionLabels[] = { "1: Ominous Rumors", "2: Vengeance", "3: Deadly Designs", "4: Deathshead", "5: Resurrection" };
	static const int missionValues[] = { 1, 2, 3, 4, 5 };

	CL_ImGuiSectionHeader( "Records / Splits Viewer", NULL );
	ImGui::BeginChild( "records_card", ImVec2( 0, 0 ), true );
	Cvar_VariableStringBuffer( "ls_sv_stats", statsText, sizeof( statsText ) );
	CL_ImGuiParseRecordSummary( statsText, &summaryAtt, &summaryComp );
	ImGui::BeginChild( "records_controls", ImVec2( 0, 166 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	CL_ImGuiCommandComboCvarName( "View Mode", "ls_mode", "0", modeLabels, modeValues, IM_ARRAYSIZE( modeValues ), "livesplit_sv_mode" );
	CL_ImGuiCommandComboCvarName( "Difficulty", "g_gameskill", "2", diffLabels, diffValues, IM_ARRAYSIZE( diffValues ), "livesplit_sv_diff" );
	CL_ImGuiCommandComboCvarName( "Chapter", "ls_mission", "1", missionLabels, missionValues, IM_ARRAYSIZE( missionValues ), "livesplit_sv_mission" );
	if ( ImGui::Button( "Refresh", ImVec2( 108, 0 ) ) ) Cbuf_AddText( "livesplit_sv_refresh\n" );
	ImGui::SameLine();
	if ( ImGui::Button( "Page Up", ImVec2( 108, 0 ) ) ) Cbuf_AddText( "livesplit_sv_pgup\n" );
	ImGui::SameLine();
	if ( ImGui::Button( "Page Down", ImVec2( 108, 0 ) ) ) Cbuf_AddText( "livesplit_sv_pgdn\n" );
	ImGui::EndChild();
	ImGui::Separator();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", Cvar_VariableString( "ls_sv_title" ) );
	ImGui::TextDisabled( "%s", statsText );
	if ( summaryAtt > 0 || summaryComp > 0 ) {
		ImGui::SameLine();
		ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "Category: %d/%d", summaryAtt, summaryComp );
	}
	ImGui::TextDisabled( "%s", Cvar_VariableString( "ls_sv_pages" ) );
	ImGui::Separator();
	ImGui::BeginChild( "records_rows", ImVec2( 0, 246 ), true, ImGuiWindowFlags_HorizontalScrollbar );
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.16f, 0.27f, 0.12f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.03f, 0.05f, 0.035f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.07f, 0.11f, 0.06f, 0.68f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.38f, 0.16f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.14f, 0.24f, 0.11f, 0.72f ) );
	if ( ImGui::BeginTable( "records_table", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2( 0, 0 ) ) ) {
		ImGui::TableSetupColumn( "Map", ImGuiTableColumnFlags_WidthStretch, 1.35f );
		ImGui::TableSetupColumn( "Gold", ImGuiTableColumnFlags_WidthFixed, 78.0f );
		ImGui::TableSetupColumn( "PB Seg", ImGuiTableColumnFlags_WidthFixed, 78.0f );
		ImGui::TableSetupColumn( "Att", ImGuiTableColumnFlags_WidthFixed, 48.0f );
		ImGui::TableSetupColumn( "Comp", ImGuiTableColumnFlags_WidthFixed, 52.0f );
		ImGui::TableHeadersRow();
	for ( i = 0; i < 10; ++i ) {
		int att, comp;
		char map[64], gold[32], pb[32];
		Com_sprintf( rowName, sizeof( rowName ), "ls_sv_r%d", i );
		Cvar_VariableStringBuffer( rowName, rowText, sizeof( rowText ) );
		if ( rowText[0] ) {
			if ( CL_ImGuiParseRecordRow( rowText, map, sizeof( map ), gold, sizeof( gold ), pb, sizeof( pb ), &att, &comp ) ) {
				attempts[recordCount] = (float)att;
				completions[recordCount] = (float)comp;
				recordCount++;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 0 ); ImGui::TextUnformatted( map );
				ImGui::TableSetColumnIndex( 1 ); ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "%s", gold );
				ImGui::TableSetColumnIndex( 2 ); ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", pb );
				ImGui::TableSetColumnIndex( 3 ); ImGui::Text( "%d", att );
				ImGui::TableSetColumnIndex( 4 ); ImGui::Text( "%d", comp );
			} else {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 0 );
				ImGui::TextDisabled( "%s", rowText );
			}
		}
	}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor( 5 );
	if ( recordCount == 0 ) {
		ImGui::TextDisabled( "No visible record rows. Click Refresh or change mode/difficulty." );
	}
	ImGui::EndChild();
	ImGui::Separator();
	CL_ImGuiDrawRecordStats( attempts, completions, recordCount );
	ImGui::Separator();
	ImGui::TextDisabled( "Reset commands apply to the currently selected category state." );
	CL_ImGuiCommandButton( "Reset Category", "livesplit_sv_reset_cat", "clear current category records" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawActionsPage( void ) {
	CL_ImGuiSectionHeader( "Run Actions", "One-click controls for timer and category state." );
	ImGui::BeginChild( "actions_card", ImVec2( 0, 0 ), true );
	CL_ImGuiActionCard( "Start Run", "Starts the current run/category and auto-records if enabled.", "Start", "livesplit_start", ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ) );
	CL_ImGuiActionCard( "Pause / Resume", "Toggles IGT pause for safe menu/setup moments.", "Pause / Resume", "livesplit_pause", ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ) );
	CL_ImGuiActionCard( "Check Settings", "Validates speedrun settings and warning conditions.", "Check", "livesplit_check", ImVec4( 0.50f, 0.78f, 0.28f, 1.0f ) );
	ImGui::Separator();
	ImGui::TextDisabled( "Split control" );
	CL_ImGuiCommandButton( "Undo Split", "livesplit_undo", "go back one split" );
	CL_ImGuiCommandButton( "Skip Split", "livesplit_skip", "skip current split" );
	ImGui::Separator();
	ImGui::TextDisabled( "Reset control" );
	CL_ImGuiCommandButton( "Reset Run", "livesplit_reset", "normal reset with save/confirmation logic" );
	CL_ImGuiCommandButton( "Reset No Save", "livesplit_reset_nosave", "reset without saving current result" );
	CL_ImGuiCommandButton( "Reset Category", "livesplit_reset_category", "clear category state" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawDevPage( void ) {
	static const char *clipLabels[] = { "Off", "X-Ray", "X-Ray Alt", "Depth", "Depth Alt" };
	static const int clipValues[] = { 0, 1, 2, 3, 4 };
	bool cheats = Cvar_VariableIntegerValue( "sv_cheats" ) != 0;

	CL_ImGuiSectionHeader( "Developer Visualization", "Cheat/dev cvars are exposed here but not forced on." );
	ImGui::BeginChild( "dev_card", ImVec2( 0, 0 ), true );
	CL_ImGuiActionCard( cheats ? "sv_cheats Enabled" : "Enable sv_cheats", cheats ? "Cheats are currently active; dev visualization tools will respond." : "Runs sv_cheats 1 twice because the patch has a confirmation guard.", cheats ? "Run Again" : "Enable Cheats", "sv_cheats 1\nsv_cheats 1", cheats ? ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ) : ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), cheats );
	ImGui::Separator();
	if ( !cheats ) {
		ImGui::TextDisabled( "Enable sv_cheats to edit the locked visualization controls below." );
	}
	ImGui::BeginDisabled( !cheats );
	CL_ImGuiBoolCvarName( "Draw Triggers", "cg_drawTriggers", "0" );
	CL_ImGuiIntSliderCvarName( "Trigger Opacity", "cg_triggerOpacity", "140", 5, 255 );
	CL_ImGuiBoolCvarName( "Draw Enemies", "cg_drawEnemies", "0" );
	CL_ImGuiIntSliderCvarName( "Enemy Opacity", "cg_enemyOpacity", "140", 5, 255 );
	CL_ImGuiBoolCvarName( "Draw Items", "cg_drawItems", "0" );
	CL_ImGuiIntSliderCvarName( "Item Opacity", "cg_itemOpacity", "140", 5, 255 );
	CL_ImGuiComboCvarName( "Draw Clips", "r_drawClips", "0", clipLabels, clipValues, IM_ARRAYSIZE( clipValues ) );
	CL_ImGuiIntSliderCvarName( "Clip Opacity", "r_clipOpacity", "120", 0, 255 );
	CL_ImGuiBoolCvarName( "Default Fonts", "cg_defaultFonts", "0" );
	ImGui::EndDisabled();
	ImGui::TextDisabled( "Some visualization options require sv_cheats 1." );
	ImGui::TextDisabled( "Default Fonts requires vid_restart." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawAboutPage( void ) {
	CL_ImGuiSectionHeader( "RtCW Speedrun Patch", "Speedrun settings, timer controls and practice tools." );
	ImGui::BeginChild( "about_card", ImVec2( 0, 0 ), true );
	ImGui::BeginChild( "about_hero", ImVec2( 0, 112 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "Return to Castle Wolfenstein 1.45" );
	ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "Speedrun Patch" );
	ImGui::Spacing();
	ImGui::TextWrapped( "Fast in-game setup for timer, split tracking, demos, records, movement tools and practice/debug options." );
	ImGui::TextDisabled( "Based on Knightmare's RtCW Patch 1.42d." );
	ImGui::EndChild();
	ImGui::Spacing();
	ImGui::Columns( 2, NULL, false );
	ImGui::BeginChild( "about_features", ImVec2( 0, 132 ), true );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Included" );
	ImGui::BulletText( "LiveSplit/in-game timer" );
	ImGui::BulletText( "Demo browser and playback controls" );
	ImGui::BulletText( "Records viewer and statistics" );
	ImGui::BulletText( "HUD, movement and dev tools" );
	ImGui::EndChild();
	ImGui::NextColumn();
	ImGui::BeginChild( "about_author", ImVec2( 0, 132 ), true );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Author" );
	ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "KoRrNiK" );
	ImGui::TextDisabled( "Discord: korrnik" );
	ImGui::Spacing();
	ImGui::TextWrapped( "Built for fast setup without leaving the game." );
	ImGui::EndChild();
	ImGui::Columns( 1 );
	ImGui::Separator();
	ImGui::TextDisabled( "Version history" );
	ImGui::BulletText( "1.45a  |  Jun 21, 2021  |  First speedrun patch release" );
	ImGui::BulletText( "1.45b  |  Mar 16, 2026  |  Major update" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawPlaceholder( const char *title, const char *desc ) {
	CL_ImGuiSectionHeader( title, desc );
	ImGui::BeginChild( "placeholder", ImVec2( 0, 0 ), true );
	ImGui::TextWrapped( "This category is ready for more options." );
	ImGui::Spacing();
	ImGui::BulletText( "Settings use existing cvars and commands." );
	ImGui::BulletText( "More controls can be added here without changing the overlay renderer." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawSettings( void ) {
	static int category = 0;
	const char *cats[] = { "Run", "Style", "Overlay", "Game", "Controls", "Records", "Demos", "Actions", "Dev Tools", "Help", "About" };
	int i;
	float alpha = s_imguiAlpha ? Com_Clamp( 0.70f, 1.0f, s_imguiAlpha->value ) : 0.96f;

	ImGui::SetNextWindowPos( ImVec2( 32, 28 ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( 660, 500 ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSizeConstraints( ImVec2( 520, 360 ), ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight ) );
	ImGui::SetNextWindowBgAlpha( alpha );
	if ( !ImGui::Begin( "Speedrun Settings", &s_imguiOpen,
		ImGuiWindowFlags_NoCollapse ) ) {
		ImGui::End();
		return;
	}

	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 && s_imguiAnim < 1.0f ) {
		ImGui::SetCursorPosY( ImGui::GetCursorPosY() + ( 1.0f - s_imguiAnim ) * 12.0f );
	}
	CL_ImGuiDrawBackgroundMist();
	CL_ImGuiDrawAccentBanner();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Speedrun Settings" );
	ImGui::SameLine();
	ImGui::SetCursorPosX( ImGui::GetWindowContentRegionMax().x - 48.0f );
	if ( ImGui::SmallButton( "GUI" ) ) {
		ImGui::OpenPopup( "gui_settings_popup" );
	}
	if ( ImGui::BeginPopup( "gui_settings_popup" ) ) {
		static const float accentFallback[4] = { 0.36f, 0.82f, 0.21f, 1.00f };
		static const float accentAltFallback[4] = { 0.96f, 0.74f, 0.24f, 1.00f };
		ImGui::TextDisabled( "GUI settings" );
		CL_ImGuiSliderCvarName( "Window Opacity", "ui_speedrun_imgui_alpha", "0.96", 0.70f, 1.0f, "%.2f" );
		CL_ImGuiSliderCvarName( "Card Opacity", "ui_speedrun_imgui_card_alpha", "0.92", 0.35f, 1.0f, "%.2f" );
		CL_ImGuiBoolCvarName( "Rounded Corners", "ui_speedrun_imgui_rounding", "1" );
		CL_ImGuiBoolCvarName( "Animations", "ui_speedrun_imgui_animations", "1" );
		ImGui::Separator();
		CL_ImGuiColorCvarName( "Accent", "ui_speedrun_imgui_accent", accentFallback );
		CL_ImGuiColorCvarName( "Accent Gold", "ui_speedrun_imgui_accent_alt", accentAltFallback );
		ImGui::Separator();
		if ( ImGui::Button( "Reset Window", ImVec2( 120, 0 ) ) ) {
			ImGui::SetWindowPos( "Speedrun Settings", ImVec2( 32, 28 ), ImGuiCond_Always );
			ImGui::SetWindowSize( "Speedrun Settings", ImVec2( 660, 500 ), ImGuiCond_Always );
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Reset Style", ImVec2( 120, 0 ) ) ) {
			Cvar_Set( "ui_speedrun_imgui_alpha", "0.96" );
			Cvar_Set( "ui_speedrun_imgui_card_alpha", "0.92" );
			Cvar_Set( "ui_speedrun_imgui_rounding", "1" );
			Cvar_Set( "ui_speedrun_imgui_animations", "1" );
			Cvar_Set( "ui_speedrun_imgui_accent", "92 210 54 1.00" );
			Cvar_Set( "ui_speedrun_imgui_accent_alt", "244 188 62 1.00" );
		}
		ImGui::EndPopup();
	}
	ImGui::Spacing();
	ImGui::Columns( 3, NULL, false );
	CL_ImGuiMiniStat( "Timer", s_cg_livesplit && s_cg_livesplit->integer ? "enabled" : "disabled" );
	ImGui::NextColumn();
	CL_ImGuiMiniStat( "Run Mode", s_ls_mode && s_ls_mode->integer == 2 ? "Individual Level" : ( s_ls_mode && s_ls_mode->integer == 1 ? "Chapter" : "Full Game" ) );
	ImGui::NextColumn();
	CL_ImGuiMiniStat( "IL Map", CL_ImGuiMapLabel( Cvar_VariableString( "ls_map" ) ) );
	ImGui::Columns( 1 );
	ImGui::Spacing();
	ImGui::Separator();

	ImGui::BeginChild( "sidebar", ImVec2( 142, 0 ), true );
	for ( i = 0; i < IM_ARRAYSIZE( cats ); ++i ) {
		bool active = category == i;
		ImDrawList *draw = ImGui::GetWindowDrawList();
		const ImGuiStyle &style = ImGui::GetStyle();
		const float navRounding = style.FrameRounding;
		const float accentRounding = navRounding > 1.0f ? 3.0f : 0.0f;
		const float hoverAccentRounding = navRounding > 1.0f ? 2.0f : 0.0f;
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 size = ImVec2( avail.x, 28.0f );
		ImGui::PushID( i );
		if ( ImGui::InvisibleButton( "nav_item", size ) ) {
			category = i;
		}
		bool hovered = ImGui::IsItemHovered();
		if ( active ) {
			draw->AddRectFilled( p, ImVec2( p.x + size.x, p.y + size.y ), IM_COL32( 42, 78, 32, 190 ), navRounding );
			draw->AddRectFilled( ImVec2( p.x + 2.0f, p.y + 5.0f ), ImVec2( p.x + 5.0f, p.y + size.y - 5.0f ), IM_COL32( 112, 224, 70, 235 ), accentRounding );
			draw->AddRect( p, ImVec2( p.x + size.x, p.y + size.y ), IM_COL32( 104, 206, 66, 200 ), navRounding, 0, 1.0f );
		} else if ( hovered ) {
			float hoverPulse = s_imguiAnimations && s_imguiAnimations->integer != 0 ? ( 0.60f + 0.20f * sinf( (float)ImGui::GetTime() * 6.0f ) ) : 0.60f;
			draw->AddRectFilled( p, ImVec2( p.x + size.x, p.y + size.y ), IM_COL32( 36, 64, 28, 145 ), navRounding );
			draw->AddRect( p, ImVec2( p.x + size.x, p.y + size.y ), ImGui::ColorConvertFloat4ToU32( ImVec4( 0.42f, 0.82f, 0.25f, hoverPulse ) ), navRounding, 0, 1.0f );
			draw->AddRectFilled( ImVec2( p.x + 2.0f, p.y + 7.0f ), ImVec2( p.x + 4.0f, p.y + size.y - 7.0f ), IM_COL32( 100, 210, 64, 160 ), hoverAccentRounding );
		}
		draw->AddText( ImVec2( p.x + 12.0f, p.y + 6.0f ), active ? IM_COL32( 210, 246, 190, 255 ) : ( hovered ? IM_COL32( 190, 226, 170, 255 ) : IM_COL32( 150, 165, 145, 255 ) ), cats[i] );
		ImGui::PopID();
		ImGui::Spacing();
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild( "content", ImVec2( 0, 0 ), true );
	switch ( category ) {
	case 0: CL_ImGuiDrawTimerPage(); break;
	case 1: CL_ImGuiDrawStylePage(); break;
	case 2: CL_ImGuiDrawOverlayPage(); break;
	case 3: CL_ImGuiDrawGamePage(); break;
	case 4: CL_ImGuiDrawBindsPage(); break;
	case 5: CL_ImGuiDrawRecordsPage(); break;
	case 6: CL_ImGuiDrawDemosPage(); break;
	case 7: CL_ImGuiDrawActionsPage(); break;
	case 8: CL_ImGuiDrawDevPage(); break;
	case 9: CL_ImGuiDrawHelpPage(); break;
	default: CL_ImGuiDrawAboutPage(); break;
	}
	ImGui::EndChild();
	ImGui::End();
}

static void CL_SpeedrunImGui_Toggle_f( void ) {
	if ( s_imguiOpen ) {
		CL_SpeedrunImGui_Close();
	} else {
		CL_SpeedrunImGui_Open();
	}
}

static void CL_SpeedrunImGui_Open_f( void ) {
	CL_SpeedrunImGui_Open();
}

static void CL_SpeedrunImGui_Close_f( void ) {
	CL_SpeedrunImGui_Close();
}

extern "C" void CL_SpeedrunImGui_Init( void ) {
	s_imguiEnabled = Cvar_Get( "ui_speedrun_imgui", "0", CVAR_TEMP );
	s_imguiAlpha = Cvar_Get( "ui_speedrun_imgui_alpha", "0.96", CVAR_ARCHIVE );
	s_imguiCardAlpha = Cvar_Get( "ui_speedrun_imgui_card_alpha", "0.92", CVAR_ARCHIVE );
	s_imguiRounding = Cvar_Get( "ui_speedrun_imgui_rounding", "1", CVAR_ARCHIVE );
	s_imguiAccent = Cvar_Get( "ui_speedrun_imgui_accent", "92 210 54 1.00", CVAR_ARCHIVE );
	s_imguiAccentAlt = Cvar_Get( "ui_speedrun_imgui_accent_alt", "244 188 62 1.00", CVAR_ARCHIVE );
	s_imguiAnimations = Cvar_Get( "ui_speedrun_imgui_animations", "1", CVAR_ARCHIVE );
	s_imguiRestorePause = Cvar_Get( "ui_speedrun_imgui_restore_pause", "0", CVAR_TEMP );
	s_cg_livesplit = Cvar_Get( "cg_livesplit", "0", CVAR_ARCHIVE );
	s_ls_type = Cvar_Get( "ls_type", "0", CVAR_ARCHIVE );
	s_ls_mode = Cvar_Get( "ls_mode", "0", CVAR_ARCHIVE );
	s_ls_mission = Cvar_Get( "ls_mission", "1", CVAR_ARCHIVE );
	s_ls_100pct = Cvar_Get( "ls_100pct", "0", CVAR_ARCHIVE );
	s_ls_compare = Cvar_Get( "ls_compare", "0", CVAR_ARCHIVE );
	s_ls_timing = Cvar_Get( "ls_timing", "0", CVAR_ARCHIVE );
	s_sp_autorecord = Cvar_Get( "sp_autorecord", "0", CVAR_ARCHIVE );
	s_ls_showtimer = Cvar_Get( "ls_showtimer", "1", CVAR_ARCHIVE );
	s_ls_showsegtimer = Cvar_Get( "ls_showseg", "1", CVAR_ARCHIVE );
	s_ls_showheaders = Cvar_Get( "ls_showheader", "1", CVAR_ARCHIVE );
	s_ls_showstats = Cvar_Get( "ls_showstats", "1", CVAR_ARCHIVE );
	/* Runtime-open state must never survive a previous session.  If this cvar
	   starts as 1 from an old config, legacy menu/cgame cursor drawing is hidden
	   while ImGui is actually closed, making the normal game cursor disappear. */
	s_imguiOpen = false;
	s_lastEnabledCvar = 0;
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "0" );
	}
	Cmd_AddCommand( "speedrun_gui", CL_SpeedrunImGui_Toggle_f );
	Cmd_AddCommand( "speedrun_imgui", CL_SpeedrunImGui_Toggle_f );
	Cmd_AddCommand( "speedrun_gui_open", CL_SpeedrunImGui_Open_f );
	Cmd_AddCommand( "speedrun_gui_close", CL_SpeedrunImGui_Close_f );
}

extern "C" void CL_SpeedrunImGui_Shutdown( void ) {
	if ( s_imguiInitialized ) {
		ImGui_ImplOpenGL2_Shutdown();
		ImGui::DestroyContext();
		s_imguiInitialized = false;
	}
}

extern "C" int CL_SpeedrunImGui_IsOpen( void ) {
	return s_imguiOpen ? 1 : 0;
}

extern "C" void CL_SpeedrunImGui_Draw( void ) {
	int now;
	float dt;
	int enabledNow;

	enabledNow = s_imguiEnabled ? s_imguiEnabled->integer : 0;
	if ( enabledNow && !s_lastEnabledCvar ) {
		CL_SpeedrunImGui_Open();
	} else if ( !enabledNow && s_lastEnabledCvar && s_imguiOpen ) {
		CL_SpeedrunImGui_Close();
	}
	s_lastEnabledCvar = enabledNow;
	if ( !s_imguiOpen ) {
		return;
	}
	CL_SpeedrunImGui_ClearGameplayInput();
	Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_CONSOLE ) | KEYCATCH_UI );
	if ( cls.state == CA_ACTIVE ) {
		Cvar_Set( "cl_paused", "1" );
		if ( s_imguiRestorePause ) {
			Cvar_Set( s_imguiRestorePause->name, "1" );
		}
	}

	CL_ImGuiLazyInit();
	CL_ImGuiApplyRuntimeStyle();

	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight );
	now = Sys_Milliseconds();
	dt = s_lastFrameMs > 0 ? ( now - s_lastFrameMs ) / 1000.0f : 1.0f / 60.0f;
	if ( dt <= 0.0f ) dt = 1.0f / 60.0f;
	s_lastFrameMs = now;
	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 ) {
		s_imguiAnim = Com_Clamp( 0.0f, 1.0f, s_imguiAnim + dt * 5.5f );
	} else {
		s_imguiAnim = 1.0f;
	}
	io.DeltaTime = dt;
	io.MouseDown[0] = s_mouseDown[0];
	io.MouseDown[1] = s_mouseDown[1];
	io.MouseDown[2] = s_mouseDown[2];
	io.MouseWheel = s_mouseWheel;
	s_mouseWheel = 0.0f;

	ImGui_ImplOpenGL2_NewFrame();
	ImGui::NewFrame();
	ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.25f + 0.75f * s_imguiAnim );
	CL_ImGuiDrawSettings();
	ImGui::PopStyleVar();
	if ( !s_imguiOpen ) {
		/* Window X / close path: keep cvar and state in sync so it does not
		   immediately reopen on the next frame. Do not render this just-closed
		   frame, otherwise the old window can remain visible until the next clear. */
		CL_SpeedrunImGui_Close();
		ImGui::EndFrame();
		return;
	}
	ImGui::Render();
	/* RtCW's fixed-function renderer is sensitive to client array/scissor/state
	   leaks. Keep the full guard here; without it ImGui can leave GL state that
	   causes window trails/ghosting after the panel is closed. */
	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glPushClientAttrib( GL_CLIENT_ALL_ATTRIB_BITS );
	ImGui_ImplOpenGL2_RenderDrawData( ImGui::GetDrawData() );
	glPopClientAttrib();
	glPopAttrib();
}

extern "C" int CL_SpeedrunImGui_WndProc( void *hWnd, unsigned int uMsg, unsigned int wParam, long lParam ) {
	(void)hWnd;

	if ( !s_imguiOpen ) {
		return 0;
	}

	if ( uMsg == WM_SETCURSOR ) {
		SetCursor( NULL );
		return 1;
	}

	if ( s_imguiInitialized ) {
		ImGuiIO &io = ImGui::GetIO();
		switch ( uMsg ) {
		case WM_MOUSEMOVE:
			SetCursor( NULL );
			io.AddMousePosEvent( (float)(short)LOWORD( lParam ), (float)(short)HIWORD( lParam ) );
			return 1;
		case WM_RBUTTONDOWN:
			if ( s_pendingBindCommand ) { CL_ImGuiAssignPendingBind( K_MOUSE2 ); return 1; }
			s_mouseDown[1] = true; io.AddMouseButtonEvent( 1, true ); return 1;
		case WM_MBUTTONDOWN:
			if ( s_pendingBindCommand ) { CL_ImGuiAssignPendingBind( K_MOUSE3 ); return 1; }
			s_mouseDown[2] = true; io.AddMouseButtonEvent( 2, true ); return 1;
		case WM_LBUTTONDOWN:
			if ( s_pendingBindCommand ) { CL_ImGuiAssignPendingBind( K_MOUSE1 ); return 1; }
			s_mouseDown[0] = true; io.AddMouseButtonEvent( 0, true ); return 1;
		case WM_LBUTTONUP: s_mouseDown[0] = false; io.AddMouseButtonEvent( 0, false ); return 1;
		case WM_RBUTTONUP: s_mouseDown[1] = false; io.AddMouseButtonEvent( 1, false ); return 1;
		case WM_MBUTTONUP: s_mouseDown[2] = false; io.AddMouseButtonEvent( 2, false ); return 1;
		case WM_MOUSEWHEEL:
			s_mouseWheel += ( (short)HIWORD( wParam ) > 0 ) ? 1.0f : -1.0f;
			io.AddMouseWheelEvent( 0.0f, ( (short)HIWORD( wParam ) > 0 ) ? 1.0f : -1.0f );
			return 1;
		case WM_CHAR:
			if ( wParam > 0 && wParam < 0x10000 ) {
				io.AddInputCharacter( (unsigned int)wParam );
			}
			return 1;
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if ( s_pendingBindCommand ) {
				CL_ImGuiAssignPendingBind( CL_ImGuiWinKeyToQuake( wParam, lParam ) );
				return 1;
			}
			if ( wParam == VK_ESCAPE ) {
				CL_SpeedrunImGui_Close();
				return 1;
			}
			io.AddKeyEvent( CL_ImGuiMapVK( wParam ), true );
			return 1;
		case WM_KEYUP:
		case WM_SYSKEYUP:
			io.AddKeyEvent( CL_ImGuiMapVK( wParam ), false );
			return 1;
		}
	}

	/* Before the first rendered frame, still consume gameplay input while active. */
	switch ( uMsg ) {
	case WM_SETCURSOR:
	case WM_MOUSEWHEEL:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
	case WM_CHAR:
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
		return 1;
	}
	return 0;
}
