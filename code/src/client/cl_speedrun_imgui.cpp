// Dear ImGui speedrun settings UI.

#include <windows.h>
#include <GL/gl.h>
#include <ctype.h>
#include <float.h>
#include <math.h>

extern "C" {
#include "client.h"
#include "livesplit/ls_types.h"
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
static cvar_t  *s_imguiUpdateDismissed = NULL;
static cvar_t  *s_imguiWindowX = NULL;
static cvar_t  *s_imguiWindowY = NULL;
static cvar_t  *s_imguiWindowW = NULL;
static cvar_t  *s_imguiWindowH = NULL;
static int      s_lastFrameMs = 0;
static float    s_imguiAnim = 1.0f;
static bool     s_mouseDown[5] = { false, false, false, false, false };
static float    s_mouseWheel = 0.0f;
static const char *s_pendingBindCommand = NULL;
static int      s_imguiCategory = 0;
static bool     s_imguiMinimized = false;
static char     s_settingsSearch[64] = "";
static bool     s_imguiPinned = false;
static ImFont  *s_imguiTimerFont = NULL;
#define SRGUI_LIVESPLIT_FONT_COUNT 32
#define SRGUI_LIVESPLIT_FONT_TIER_COUNT 4
static const float s_imguiLiveSplitFontTierPixels[SRGUI_LIVESPLIT_FONT_TIER_COUNT] = { 15.0f, 22.0f, 32.0f, 45.0f };
static ImFont  *s_imguiLiveSplitFonts[SRGUI_LIVESPLIT_FONT_COUNT] = { NULL };
static ImFont  *s_imguiLiveSplitBoldFonts[SRGUI_LIVESPLIT_FONT_COUNT] = { NULL };
static ImFont  *s_imguiLiveSplitFontTiers[SRGUI_LIVESPLIT_FONT_COUNT][SRGUI_LIVESPLIT_FONT_TIER_COUNT] = { NULL };
static ImFont  *s_imguiLiveSplitBoldFontTiers[SRGUI_LIVESPLIT_FONT_COUNT][SRGUI_LIVESPLIT_FONT_TIER_COUNT] = { NULL };
static bool     s_liveSplitEditActive = false;

static const char *s_imguiLiveSplitFontPaths[SRGUI_LIVESPLIT_FONT_COUNT] = {
	"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\tahoma.ttf",
	"C:\\Windows\\Fonts\\verdana.ttf", "C:\\Windows\\Fonts\\trebuc.ttf", "C:\\Windows\\Fonts\\calibri.ttf", "C:\\Windows\\Fonts\\cour.ttf",
	"C:\\Windows\\Fonts\\impact.ttf", "C:\\Windows\\Fonts\\times.ttf", "C:\\Windows\\Fonts\\georgia.ttf", "C:\\Windows\\Fonts\\lucon.ttf",
	"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\candara.ttf", "C:\\Windows\\Fonts\\corbel.ttf", "C:\\Windows\\Fonts\\calibrib.ttf",
	"C:\\Windows\\Fonts\\segoeuisb.ttf", "C:\\Windows\\Fonts\\segoeuil.ttf", "C:\\Windows\\Fonts\\segoeuii.ttf", "C:\\Windows\\Fonts\\ariali.ttf",
	"C:\\Windows\\Fonts\\arialbi.ttf", "C:\\Windows\\Fonts\\cambria.ttf", "C:\\Windows\\Fonts\\cambriab.ttf", "C:\\Windows\\Fonts\\constan.ttf",
	"C:\\Windows\\Fonts\\constanb.ttf", "C:\\Windows\\Fonts\\comic.ttf", "C:\\Windows\\Fonts\\comicbd.ttf", "C:\\Windows\\Fonts\\gadugi.ttf",
	"C:\\Windows\\Fonts\\gadugib.ttf", "C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\palai.ttf", "C:\\Windows\\Fonts\\palab.ttf"
};

static const char *s_imguiLiveSplitBoldFontPaths[SRGUI_LIVESPLIT_FONT_COUNT] = {
	"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\consolab.ttf", "C:\\Windows\\Fonts\\arialbd.ttf", "C:\\Windows\\Fonts\\tahomabd.ttf",
	"C:\\Windows\\Fonts\\verdanab.ttf", "C:\\Windows\\Fonts\\trebucbd.ttf", "C:\\Windows\\Fonts\\calibrib.ttf", "C:\\Windows\\Fonts\\courbd.ttf",
	"C:\\Windows\\Fonts\\impact.ttf", "C:\\Windows\\Fonts\\timesbd.ttf", "C:\\Windows\\Fonts\\georgiab.ttf", "C:\\Windows\\Fonts\\lucon.ttf",
	"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\candarab.ttf", "C:\\Windows\\Fonts\\corbelb.ttf", "C:\\Windows\\Fonts\\calibrib.ttf",
	"C:\\Windows\\Fonts\\segoeuisb.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\segoeuiz.ttf", "C:\\Windows\\Fonts\\arialbi.ttf",
	"C:\\Windows\\Fonts\\arialbi.ttf", "C:\\Windows\\Fonts\\cambriab.ttf", "C:\\Windows\\Fonts\\cambriab.ttf", "C:\\Windows\\Fonts\\constanb.ttf",
	"C:\\Windows\\Fonts\\constanb.ttf", "C:\\Windows\\Fonts\\comicbd.ttf", "C:\\Windows\\Fonts\\comicbd.ttf", "C:\\Windows\\Fonts\\gadugib.ttf",
	"C:\\Windows\\Fonts\\gadugib.ttf", "C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\palab.ttf", "C:\\Windows\\Fonts\\palab.ttf"
};

static bool CL_ImGuiLoadLiveSplitLargeFontTier( int fontIndex, int tier ) {
	if ( tier <= 1 ) return true;
	return fontIndex == 0 || fontIndex == 1 || fontIndex == 2 || fontIndex == 7 || fontIndex == 12 || fontIndex == 15 || fontIndex == 29;
}

static float CL_ImGuiAutoBoxHeight( int rows ) {
	float line = ImGui::GetFrameHeightWithSpacing();
	return 8.0f + rows * line + ImGui::GetStyle().WindowPadding.y * 2.0f;
}

static ImFont *CL_ImGuiAddFontFileSafe( ImGuiIO &io, const char *path, float sizePixels ) {
	if ( !path || GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES ) {
		return NULL;
	}
	return io.Fonts->AddFontFromFileTTF( path, sizePixels );
}

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
static cvar_t *s_ls_imgui = NULL;

static const char *s_mapLabels[] = {
	"Escape!", "Castle Keep", "Tram Ride", "Village", "Catacombs", "Crypt", "Church", "Tomb", "Forest Compound", "Rocket Base", "Radar Installation", "Air Base Assault", "Kugelstadt", "The Bombed Factory", "The Trainyards", "Secret Weapons Facility", "Ice Station Norway", "X-Labs", "Super Soldier", "Bramburg Dam", "Paderborn Village", "Chateau Schufstaffel", "Unhallowed Ground", "The Dig", "Return to Castle Wolfenstein", "Heinrich"
};
static const char *s_mapValues[] = {
	"escape1", "escape2", "tram", "village1", "crypt1", "crypt2", "church", "boss1", "forest", "rocket", "baseout", "assault", "sfm", "factory", "trainyard", "swf", "norway", "xlabs", "boss2", "dam", "village2", "chateau", "dark", "dig", "castle", "end"
};

enum srGuiSettingType_t {
	SRGUI_SETTING_BOOL,
	SRGUI_SETTING_FLOAT,
	SRGUI_SETTING_INT,
	SRGUI_SETTING_COMMAND
};

typedef struct {
	const char *label;
	const char *name;
	const char *defaultValue;
	const char *tags;
	int category;
	srGuiSettingType_t type;
	float minValue;
	float maxValue;
	const char *command;
} srGuiSettingEntry_t;

static const srGuiSettingEntry_t s_settingEntries[] = {
	{ "Enable Timer", "cg_livesplit", "0", "timer run livesplit", 0, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Show LiveSplit", "ls_draw", "1", "overlay splits hud", 3, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Splits Opacity", "ls_opacity", "0.900000", "opacity alpha splits", 2, SRGUI_SETTING_FLOAT, 0.0f, 1.0f, NULL },
	{ "Panel X", "ls_x", "6.666843", "position layout", 2, SRGUI_SETTING_FLOAT, 0.0f, 580.0f, NULL },
	{ "Panel Y", "ls_y", "92.444427", "position layout", 2, SRGUI_SETTING_FLOAT, 0.0f, 440.0f, NULL },
	{ "Panel Width", "ls_w", "215", "width layout", 2, SRGUI_SETTING_FLOAT, 80.0f, 400.0f, NULL },
	{ "Show Ghost", "ls_ghost", "0", "ghost replay", 4, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Ghost Opacity", "ls_ghost_opacity", "60", "ghost opacity alpha", 4, SRGUI_SETTING_INT, 5, 255, NULL },
	{ "Weapon Render Mode", "cg_weapon_color_mode", "0", "weapon color rainbow flat xray", 4, SRGUI_SETTING_INT, 0, 7, NULL },
	{ "Weapon Opacity", "cg_weapon_color_opacity", "0.35", "weapon opacity alpha tint", 4, SRGUI_SETTING_FLOAT, 0.0f, 1.0f, NULL },
	{ "Grid", "ui_speedrun_grid", "0", "grid layout siatka", 5, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Grid Size", "ui_speedrun_grid_size", "32", "grid size wymiar", 5, SRGUI_SETTING_FLOAT, 8.0f, 160.0f, NULL },
	{ "Grid Opacity", "ui_speedrun_grid_opacity", "0.22", "grid opacity alpha", 5, SRGUI_SETTING_FLOAT, 0.02f, 0.75f, NULL },
	{ "HUD Edit Mode", "ui_speedrun_layout_edit", "0", "drag move layout hud livesplit", 5, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Show FPS", "cg_drawfps", "0", "fps counter", 3, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "FPS X", "cg_fpsX", "500", "fps position", 3, SRGUI_SETTING_FLOAT, 0.0f, 640.0f, NULL },
	{ "FPS Y", "cg_fpsY", "0", "fps position", 3, SRGUI_SETTING_FLOAT, 0.0f, 440.0f, NULL },
	{ "Show Keystrokes", "cg_drawKeys", "1", "keys input overlay", 3, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Speedometer", "cg_drawVelocity", "1", "speed fps velocity", 3, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Velocity X", "cg_velocity_x", "320", "speedometer position", 3, SRGUI_SETTING_FLOAT, 0.0f, 640.0f, NULL },
	{ "Velocity Y", "cg_velocity_y", "457", "speedometer position", 3, SRGUI_SETTING_FLOAT, 0.0f, 480.0f, NULL },
	{ "HL1 Bhop Physics", "bh_movement", "0", "bhop movement", 4, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Auto Jump", "bh_autojump", "0", "bhop jump", 4, SRGUI_SETTING_BOOL, 0, 0, NULL },
	{ "Start Run", "livesplit_start", "", "run timer start", 9, SRGUI_SETTING_COMMAND, 0, 0, "livesplit_start" },
	{ "Reset Run", "livesplit_reset", "", "reset timer run", 9, SRGUI_SETTING_COMMAND, 0, 0, "livesplit_reset" },
	{ "Save Position", "savepos", "", "save practice", 9, SRGUI_SETTING_COMMAND, 0, 0, "savepos" },
	{ "Load Position", "loadpos", "", "load practice", 9, SRGUI_SETTING_COMMAND, 0, 0, "loadpos" },
	{ "Demo Pause", "demo_pause", "", "demo playback", 8, SRGUI_SETTING_COMMAND, 0, 0, "demo_pause" },
	{ "Demo Freecam", "demo_freecam", "", "demo camera", 8, SRGUI_SETTING_COMMAND, 0, 0, "demo_freecam" }
};
typedef struct {
	const char *label;
	const char *command;
} srGuiBindEntry_t;

static const srGuiBindEntry_t s_bindEntries[] = {
	{ "LiveSplit Start", "livesplit_start" },
	{ "LiveSplit Pause / Resume", "livesplit_pause" },
	{ "LiveSplit Undo Split", "livesplit_undo" },
	{ "LiveSplit Skip Split", "livesplit_skip" },
	{ "LiveSplit Reset", "livesplit_reset" },
	{ "LiveSplit Reset No Save", "livesplit_reset_nosave" },
	{ "LiveSplit Reset Category", "livesplit_reset_category" },
	{ "LiveSplit Check Settings", "livesplit_check" },
	{ "Demo Pause", "demo_pause" },
	{ "Demo Speed Up", "demo_speedup" },
	{ "Demo Slow Down", "demo_slowdown" },
	{ "Demo Skip Forward", "demo_skipforward" },
	{ "Demo Rewind", "demo_skipbackward" },
	{ "Demo Step Frame", "demo_stepframe" },
	{ "Demo Step Back", "demo_stepframeback" },
	{ "Demo Freecam", "demo_freecam" },
	{ "Demo Next Map", "demo_nextmap" },
	{ "Demo Previous Map", "demo_prevmap" },
	{ "Demo Toggle HUD", "demo_togglehud" },
	{ "Demo Toggle Binds", "demo_togglebinds" },
	{ "Practice Save Position", "savepos" },
	{ "Practice Load Position", "loadpos" },
	{ "Practice Rewind", "rewind" }
};

static void CL_ImGuiSectionHeader( const char *title, const char *subtitle = NULL );

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

static void CL_SpeedrunImGui_SetupKnownGLState( void ) {
	/* RtCW's renderer can leave fixed-function state behind after options such
	   as r_vertexLight / vid_restart.  ImGui's OpenGL2 backend sets the common
	   state, but not every old GL bit used by the game.  Reset the extra bits
	   that can make the font atlas render as solid blocks or washed-out text. */
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_FOG );
	glDisable( GL_LIGHTING );
	glDisable( GL_COLOR_MATERIAL );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glEnable( GL_TEXTURE_2D );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glDepthMask( GL_FALSE );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
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
	for ( int fontIndex = 0; fontIndex < SRGUI_LIVESPLIT_FONT_COUNT; ++fontIndex ) {
		for ( int tier = 0; tier < SRGUI_LIVESPLIT_FONT_TIER_COUNT; ++tier ) {
			if ( !CL_ImGuiLoadLiveSplitLargeFontTier( fontIndex, tier ) ) {
				continue;
			}
			float size = s_imguiLiveSplitFontTierPixels[tier];
			s_imguiLiveSplitFontTiers[fontIndex][tier] = CL_ImGuiAddFontFileSafe( io, s_imguiLiveSplitFontPaths[fontIndex], size );
			s_imguiLiveSplitBoldFontTiers[fontIndex][tier] = CL_ImGuiAddFontFileSafe( io, s_imguiLiveSplitBoldFontPaths[fontIndex], size );
			if ( !s_imguiLiveSplitBoldFontTiers[fontIndex][tier] ) {
				s_imguiLiveSplitBoldFontTiers[fontIndex][tier] = s_imguiLiveSplitFontTiers[fontIndex][tier];
			}
		}
		s_imguiLiveSplitFonts[fontIndex] = s_imguiLiveSplitFontTiers[fontIndex][0];
		s_imguiLiveSplitBoldFonts[fontIndex] = s_imguiLiveSplitBoldFontTiers[fontIndex][0];
	}
	if ( !s_imguiLiveSplitFonts[0] ) {
		s_imguiLiveSplitFonts[0] = io.Fonts->AddFontDefault();
		s_imguiLiveSplitFontTiers[0][0] = s_imguiLiveSplitFonts[0];
	}
	if ( !s_imguiLiveSplitBoldFonts[0] ) {
		s_imguiLiveSplitBoldFonts[0] = s_imguiLiveSplitFonts[0];
		s_imguiLiveSplitBoldFontTiers[0][0] = s_imguiLiveSplitFonts[0];
	}
	s_imguiTimerFont = s_imguiLiveSplitFonts[1] ? s_imguiLiveSplitFonts[1] : s_imguiLiveSplitFonts[0];
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
	ImGuiStyle &style = ImGui::GetStyle();
	ImGui::BeginChild( label, ImVec2( 0, 42 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p0 = ImGui::GetWindowPos();
	ImVec2 p1 = ImVec2( p0.x + 5.0f, p0.y + ImGui::GetWindowHeight() - 1.0f );
	draw->AddRectFilled( ImVec2( p0.x + 1.0f, p0.y + 1.0f ), p1, IM_COL32( 95, 175, 58, 120 ), style.ChildRounding > 1.0f ? style.ChildRounding - 1.0f : 0.0f, ImDrawFlags_RoundCornersLeft );
	ImGui::SetCursorPos( ImVec2( 16.0f, 8.0f ) );
	if ( ImGui::Button( label, ImVec2( 160, 0 ) ) ) {
		Cbuf_AddText( command );
		Cbuf_AddText( "\n" );
	}
	if ( hint && hint[0] ) {
		ImGui::SameLine( 0.0f, 14.0f );
		ImGui::TextDisabled( "%s", hint );
	}
	ImGui::EndChild();
}

static bool CL_ImGuiTextContainsNoCase( const char *text, const char *needle ) {
	int i, j;
	if ( !needle || !needle[0] ) return true;
	if ( !text ) return false;
	for ( i = 0; text[i]; ++i ) {
		for ( j = 0; needle[j]; ++j ) {
			if ( !text[i + j] || tolower( (unsigned char)text[i + j] ) != tolower( (unsigned char)needle[j] ) ) {
				break;
			}
		}
		if ( !needle[j] ) return true;
	}
	return false;
}

static bool CL_ImGuiSettingMatches( const srGuiSettingEntry_t *entry, const char *query ) {
	return CL_ImGuiTextContainsNoCase( entry->label, query ) || CL_ImGuiTextContainsNoCase( entry->name, query ) || CL_ImGuiTextContainsNoCase( entry->tags, query );
}

static void CL_ImGuiFavoriteCvarName( const srGuiSettingEntry_t *entry, char *out, int outSize ) {
	char clean[64];
	int i, j = 0;
	const char *src = entry->name && entry->name[0] ? entry->name : entry->label;
	for ( i = 0; src[i] && j < (int)sizeof( clean ) - 1; ++i ) {
		char c = src[i];
		clean[j++] = ( c >= 'A' && c <= 'Z' ) ? (char)( c + 32 ) : ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ? c : '_' );
	}
	clean[j] = '\0';
	Com_sprintf( out, outSize, "ui_speedrun_fav_%s", clean );
}

static bool CL_ImGuiIsFavorite( const srGuiSettingEntry_t *entry ) {
	char cvarName[96];
	CL_ImGuiFavoriteCvarName( entry, cvarName, sizeof( cvarName ) );
	return Cvar_VariableIntegerValue( cvarName ) != 0;
}

static void CL_ImGuiRenderSettingEntry( const srGuiSettingEntry_t *entry, bool showCategoryButton ) {
	char favName[96];
	bool fav = CL_ImGuiIsFavorite( entry );
	CL_ImGuiFavoriteCvarName( entry, favName, sizeof( favName ) );
	ImGui::PushID( entry->name );
	if ( ImGui::Button( fav ? "-" : "+", ImVec2( 24, 0 ) ) ) {
		Cvar_Set( favName, fav ? "0" : "1" );
	}
	ImGui::SameLine();
	if ( entry->type == SRGUI_SETTING_BOOL ) {
		CL_ImGuiBoolCvarName( entry->label, entry->name, entry->defaultValue );
	} else if ( entry->type == SRGUI_SETTING_FLOAT ) {
		CL_ImGuiSliderCvarName( entry->label, entry->name, entry->defaultValue, entry->minValue, entry->maxValue, "%.2f" );
	} else if ( entry->type == SRGUI_SETTING_INT ) {
		CL_ImGuiIntSliderCvarName( entry->label, entry->name, entry->defaultValue, (int)entry->minValue, (int)entry->maxValue );
	} else if ( entry->command ) {
		CL_ImGuiCommandButton( entry->label, entry->command, entry->tags );
	}
	if ( showCategoryButton ) {
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Go" ) ) {
			s_imguiCategory = entry->category;
		}
	}
	ImGui::PopID();
}

static void CL_ImGuiDrawPinnedSettings( void ) {
	int i, count = 0;
	if ( !ImGui::CollapsingHeader( "Pinned Settings", ImGuiTreeNodeFlags_DefaultOpen ) ) return;
	ImGui::BeginChild( "pinned_settings", ImVec2( 0, 154 ), true );
	for ( i = 0; i < IM_ARRAYSIZE( s_settingEntries ); ++i ) {
		if ( CL_ImGuiIsFavorite( &s_settingEntries[i] ) ) {
			CL_ImGuiRenderSettingEntry( &s_settingEntries[i], true );
			count++;
		}
	}
	if ( count == 0 ) {
		ImGui::TextDisabled( "No pinned settings yet. Use Search and press + next to an option." );
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawSettingsSearchPage( void ) {
	int i, count = 0;
	CL_ImGuiSectionHeader( "Settings Search", "Type opacity, ghost, fps, bhop, reset, demo..." );
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputTextWithHint( "##settings_search", "search settings and commands...", s_settingsSearch, sizeof( s_settingsSearch ) );
	ImGui::BeginChild( "settings_search_results", ImVec2( 0, 0 ), true );
	if ( !s_settingsSearch[0] ) {
		ImGui::TextDisabled( "Start typing to filter matching settings." );
	} else {
		for ( i = 0; i < IM_ARRAYSIZE( s_settingEntries ); ++i ) {
			if ( CL_ImGuiSettingMatches( &s_settingEntries[i], s_settingsSearch ) ) {
				CL_ImGuiRenderSettingEntry( &s_settingEntries[i], true );
				count++;
			}
		}
		if ( count == 0 ) {
			ImGui::TextDisabled( "No settings match '%s'.", s_settingsSearch );
		}
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawLayoutGrid( void ) {
	cvar_t *grid = CL_ImGuiCvar( "ui_speedrun_grid", "0" );
	if ( !grid || grid->integer == 0 || cls.state != CA_ACTIVE ) {
		return;
	}
	cvar_t *sizeCv = CL_ImGuiCvar( "ui_speedrun_grid_size", "32" );
	cvar_t *opacityCv = CL_ImGuiCvar( "ui_speedrun_grid_opacity", "0.22" );
	cvar_t *majorCv = CL_ImGuiCvar( "ui_speedrun_grid_major", "5" );
	cvar_t *labelsCv = CL_ImGuiCvar( "ui_speedrun_grid_labels", "1" );
	cvar_t *centerCv = CL_ImGuiCvar( "ui_speedrun_grid_center", "1" );
	float step = sizeCv ? sizeCv->value : 32.0f;
	float alpha = opacityCv ? Com_Clamp( 0.02f, 0.75f, opacityCv->value ) : 0.22f;
	int majorEvery = majorCv ? majorCv->integer : 5;
	int a = (int)( alpha * 255.0f );
	int majorA = (int)( alpha * 255.0f * 1.55f );
	int x, y;
	char label[32];
	if ( step < 8.0f ) step = 8.0f;
	if ( step > 160.0f ) step = 160.0f;
	if ( majorEvery < 2 ) majorEvery = 2;
	if ( majorEvery > 10 ) majorEvery = 10;
	if ( majorA > 255 ) majorA = 255;
	ImDrawList *draw = ImGui::GetBackgroundDrawList();
	for ( x = 0; x <= cls.glconfig.vidWidth; x += (int)step ) {
		bool major = ( ( x / (int)step ) % majorEvery ) == 0;
		draw->AddLine( ImVec2( (float)x, 0.0f ), ImVec2( (float)x, (float)cls.glconfig.vidHeight ), major ? IM_COL32( 120, 230, 72, majorA ) : IM_COL32( 120, 230, 72, a ), major ? 1.25f : 1.0f );
		if ( labelsCv && labelsCv->integer && major && x > 0 ) {
			Com_sprintf( label, sizeof( label ), "%d", (int)( (float)x * 640.0f / (float)cls.glconfig.vidWidth ) );
			draw->AddText( ImVec2( (float)x + 3.0f, 4.0f ), IM_COL32( 190, 245, 150, majorA ), label );
		}
	}
	for ( y = 0; y <= cls.glconfig.vidHeight; y += (int)step ) {
		bool major = ( ( y / (int)step ) % majorEvery ) == 0;
		draw->AddLine( ImVec2( 0.0f, (float)y ), ImVec2( (float)cls.glconfig.vidWidth, (float)y ), major ? IM_COL32( 120, 230, 72, majorA ) : IM_COL32( 120, 230, 72, a ), major ? 1.25f : 1.0f );
		if ( labelsCv && labelsCv->integer && major && y > 0 ) {
			Com_sprintf( label, sizeof( label ), "%d", (int)( (float)y * 480.0f / (float)cls.glconfig.vidHeight ) );
			draw->AddText( ImVec2( 4.0f, (float)y + 3.0f ), IM_COL32( 190, 245, 150, majorA ), label );
		}
	}
	if ( centerCv && centerCv->integer ) {
		float cx = cls.glconfig.vidWidth * 0.5f;
		float cy = cls.glconfig.vidHeight * 0.5f;
		draw->AddLine( ImVec2( cx, 0.0f ), ImVec2( cx, (float)cls.glconfig.vidHeight ), IM_COL32( 245, 220, 90, majorA ), 1.75f );
		draw->AddLine( ImVec2( 0.0f, cy ), ImVec2( (float)cls.glconfig.vidWidth, cy ), IM_COL32( 245, 220, 90, majorA ), 1.75f );
	}
	if ( labelsCv && labelsCv->integer ) {
		draw->AddText( ImVec2( 10.0f, 22.0f ), IM_COL32( 245, 220, 120, majorA ), "Layout grid: virtual 640x480" );
	}
}

static float CL_ImGuiLayoutCvarFloat( const char *name, const char *fallback ) {
	cvar_t *cv = CL_ImGuiCvar( name, fallback );
	return cv ? cv->value : ( fallback ? atof( fallback ) : 0.0f );
}

static float CL_ImGuiLiveSplitHandleHeight( void ) {
	float scale = CL_ImGuiLayoutCvarFloat( "ls_scale", "1.0" );
	float maxRows = CL_ImGuiLayoutCvarFloat( "ls_maxrows", "6" );
	if ( scale < 0.5f ) scale = 0.5f;
	if ( scale > 3.0f ) scale = 3.0f;
	if ( maxRows < 2.0f ) maxRows = 8.0f;
	if ( maxRows > 16.0f ) maxRows = 16.0f;
	return ( 84.0f + maxRows * 12.0f ) * scale;
}

static float CL_ImGuiKeysHandleWidth( void ) {
	float boxW = Com_Clamp( 14.0f, 80.0f, CL_ImGuiLayoutCvarFloat( "ks_box_w", "22" ) );
	float boxH = Com_Clamp( 12.0f, 54.0f, CL_ImGuiLayoutCvarFloat( "ks_box_h", "18" ) );
	float gap = Com_Clamp( 0.0f, 20.0f, CL_ImGuiLayoutCvarFloat( "ks_gap", "2" ) );
	float gridSize = Com_Clamp( 48.0f, 220.0f, CL_ImGuiLayoutCvarFloat( "ks_mouse_grid_size", "92" ) );
	int layout = Cvar_VariableIntegerValue( "ks_layout" );
	int extra = ( Cvar_VariableIntegerValue( "ks_show_use" ) ? 1 : 0 ) + ( Cvar_VariableIntegerValue( "ks_show_reload" ) ? 1 : 0 );
	float wideW = ( boxW * 3.0f + gap ) * 0.5f;
	if ( layout == 1 ) {
		int count = 6 + ( Cvar_VariableIntegerValue( "ks_mouse" ) >= 1 ? 2 : 0 ) + extra;
		return boxW * 4.0f + wideW * (float)( count - 4 ) + gap * (float)( count - 1 );
	}
	if ( layout == 3 ) {
		float keysH = boxH * 3.0f + gap * 2.0f;
		if ( gridSize < keysH ) gridSize = keysH;
		return boxW * 3.0f + gap * 4.0f + gridSize;
	}
	if ( layout == 4 || layout == 6 ) {
		return wideW;
	}
	if ( layout == 5 ) {
		int count = Cvar_VariableIntegerValue( "ks_active_max" );
		if ( count < 1 ) count = 1;
		if ( count > 10 ) count = 10;
		return wideW * (float)count + gap * (float)( count - 1 );
	}
	if ( layout == 7 ) {
		return gridSize;
	}
	return boxW * 3.0f + gap * 2.0f;
}

static float CL_ImGuiKeysHandleHeight( void ) {
	float boxH = Com_Clamp( 12.0f, 54.0f, CL_ImGuiLayoutCvarFloat( "ks_box_h", "18" ) );
	float gap = Com_Clamp( 0.0f, 20.0f, CL_ImGuiLayoutCvarFloat( "ks_gap", "2" ) );
	float gridSize = Com_Clamp( 48.0f, 220.0f, CL_ImGuiLayoutCvarFloat( "ks_mouse_grid_size", "92" ) );
	int layout = Cvar_VariableIntegerValue( "ks_layout" );
	int extra = ( Cvar_VariableIntegerValue( "ks_show_use" ) ? 1 : 0 ) + ( Cvar_VariableIntegerValue( "ks_show_reload" ) ? 1 : 0 );
	int rows = 3;
	if ( layout == 1 || layout == 5 ) rows = 1;
	else if ( layout == 3 ) {
		float keysH = boxH * 3.0f + gap * 2.0f;
		return gridSize > keysH ? gridSize : keysH;
	}
	else if ( layout == 4 || layout == 6 ) {
		rows = layout == 6 ? Cvar_VariableIntegerValue( "ks_active_max" ) : 8 + extra;
		if ( rows < 1 ) rows = 1;
		if ( rows > 10 ) rows = 10;
	}
	else if ( layout == 7 ) {
		return gridSize;
	}
	else if ( layout != 2 ) {
		if ( Cvar_VariableIntegerValue( "ks_mouse" ) >= 1 ) rows++;
		if ( Cvar_VariableIntegerValue( "ks_show_use" ) || Cvar_VariableIntegerValue( "ks_show_reload" ) ) rows++;
	}
	return boxH * (float)rows + gap * (float)( rows - 1 );
}

static float CL_ImGuiKeysDefaultX( void ) {
	float scale = Com_Clamp( 0.3f, 4.0f, CL_ImGuiLayoutCvarFloat( "ks_scale", "1.0" ) );
	float width = CL_ImGuiKeysHandleWidth() * scale;
	return Com_Clamp( 0.0f, 640.0f, ( 640.0f - width ) * 0.5f );
}

static const char *CL_ImGuiKeysDefaultXString( void ) {
	static char value[32];
	Com_sprintf( value, sizeof( value ), "%.0f", CL_ImGuiKeysDefaultX() );
	return value;
}

static float CL_ImGuiVelocityHandleHeight( void ) {
	float scale = CL_ImGuiLayoutCvarFloat( "cg_velocity_scale", "1.0" );
	float fontH;
	if ( scale < 0.3f ) scale = 0.3f;
	if ( scale > 3.0f ) scale = 3.0f;
	switch ( Cvar_VariableIntegerValue( "cg_velocity_size" ) ) {
		case 1: fontH = 16.0f; break;
		case 2: fontH = 16.0f; break;
		case 3: fontH = 48.0f; break;
		default: fontH = 8.0f; break;
	}
	return fontH * scale;
}

static float CL_ImGuiVelocityAutoY( void ) {
	float fontH = CL_ImGuiVelocityHandleHeight();
	if ( Cvar_VariableIntegerValue( "cg_velocity_type" ) == 0 ) {
		return fontH >= 48.0f ? 422.0f : 457.0f;
	}
	return fontH >= 48.0f ? 266.0f : 256.0f;
}

static float CL_ImGuiAlignAnchorOffset( int align, float width ) {
	if ( align == 1 ) return width * 0.5f;
	if ( align == 2 ) return width;
	return 0.0f;
}

static void CL_ImGuiSetAnchorAlignedCvar( cvar_t *xCv, cvar_t *alignCv, int oldAlign, int newAlign, int cvarValue, float width ) {
	float visualLeft;
	float newX;
	if ( !alignCv ) return;
	if ( xCv ) {
		visualLeft = xCv->value - CL_ImGuiAlignAnchorOffset( oldAlign, width );
		newX = visualLeft + CL_ImGuiAlignAnchorOffset( newAlign, width );
		newX = Com_Clamp( CL_ImGuiAlignAnchorOffset( newAlign, width ), 640.0f - width + CL_ImGuiAlignAnchorOffset( newAlign, width ), newX );
		Cvar_SetValue( xCv->name, newX );
	}
	Cvar_SetValue( alignCv->name, cvarValue );
}

static void CL_ImGuiDragLayoutElement( const char *label, const char *xCvar, const char *yCvar, const char *xDefault, const char *yDefault, float w, float h, bool autoWhenZero = false, bool mirrorAlign = false, float autoX = 0.0f, float autoY = 0.0f, const char *toggleCvar = NULL, const char *toggleCvar2 = NULL, const char *scaleCvar = NULL, const char *widthCvar = NULL, float minScale = 0.5f, float maxScale = 3.0f, float minWidth = 40.0f, float maxWidth = 640.0f, int anchorMode = 0, const char *alignCvar = NULL, int alignRightValue = 1, scralign_t screenAlign = ALIGN_STRETCH, const char *scaleDefault = "1.0", const char *widthDefault = "178", int alignModeCount = 2, const char *alignDefault = "0" ) {
	cvar_t *xCv = CL_ImGuiCvar( xCvar, xDefault );
	cvar_t *yCv = CL_ImGuiCvar( yCvar, yDefault );
	cvar_t *toggleCv = toggleCvar ? CL_ImGuiCvar( toggleCvar, "1" ) : NULL;
	cvar_t *toggleCv2 = toggleCvar2 ? CL_ImGuiCvar( toggleCvar2, "1" ) : NULL;
	cvar_t *scaleCv = scaleCvar ? CL_ImGuiCvar( scaleCvar, scaleDefault ) : NULL;
	cvar_t *widthCv = widthCvar ? CL_ImGuiCvar( widthCvar, widthDefault ) : NULL;
	cvar_t *alignCv = alignCvar ? CL_ImGuiCvar( alignCvar, alignDefault ) : NULL;
	float vx = xCv ? xCv->value : 0.0f;
	float vy = yCv ? yCv->value : 0.0f;
	float labelX = vx;
	float labelY = vy;
	float scale = scaleCv ? Com_Clamp( minScale, maxScale, scaleCv->value ) : 1.0f;
	float drawW = widthCv ? Com_Clamp( minWidth, maxWidth, widthCv->value ) : w * scale;
	float drawH = widthCv ? h : h * scale;
	bool enabled = true;
	if ( toggleCv && toggleCv->integer == 0 ) enabled = false;
	if ( toggleCv2 && toggleCv2->integer == 0 ) enabled = false;
	bool autoPos = autoWhenZero && fabsf( vx ) <= 0.01f && fabsf( vy ) <= 0.01f;
	if ( autoPos ) {
		vx = autoX;
		vy = autoY;
		labelX = autoX;
		labelY = autoY;
	}
	if ( mirrorAlign && Cvar_VariableIntegerValue( "ls_align" ) == 1 ) {
		vx = 640.0f - vx - drawW;
	}
	if ( anchorMode == 1 ) {
		vx -= drawW * 0.5f;
	} else if ( anchorMode == 2 ) {
		vx -= drawW;
	}
	float x = vx;
	float y = vy;
	float screenW = drawW;
	float screenH = drawH;
	SCR_AdjustFrom640( &x, &y, &screenW, &screenH, screenAlign );
	ImVec2 pos = ImVec2( x, y );
	ImVec2 size = ImVec2( screenW, screenH );
	if ( size.x < 1.0f ) size.x = 1.0f;
	if ( size.y < 1.0f ) size.y = 1.0f;
	float dxScale = screenW / drawW;
	float dyScale = screenH / drawH;
	if ( dxScale <= 0.0f ) dxScale = 1.0f;
	if ( dyScale <= 0.0f ) dyScale = 1.0f;
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.0f, 0.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowMinSize, ImVec2( 1.0f, 1.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 5.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::SetNextWindowPos( pos, ImGuiCond_Always );
	ImGui::SetNextWindowSize( size, ImGuiCond_Always );
	ImGui::PushStyleColor( ImGuiCol_WindowBg, enabled ? ImVec4( 0.04f, 0.08f, 0.03f, 0.10f ) : ImVec4( 0.12f, 0.03f, 0.03f, 0.13f ) );
	ImGui::PushStyleColor( ImGuiCol_Border, enabled ? ImVec4( 0.58f, 0.92f, 0.34f, 0.32f ) : ImVec4( 0.95f, 0.28f, 0.20f, 0.42f ) );
	ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.10f, 0.18f, 0.07f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.24f, 0.42f, 0.13f, 0.96f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.36f, 0.62f, 0.18f, 1.00f ) );
	ImGui::Begin( label, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus );
	ImGui::PushID( label );
	pos = ImGui::GetWindowPos();
	size = ImGui::GetWindowSize();
	ImDrawList *draw = ImGui::GetWindowDrawList();
	bool hovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
	bool resizing = false;
	bool controlsHovered = false;
	if ( hovered ) {
		ImGui::SetCursorPos( ImVec2( 5.0f, 5.0f ) );
		if ( ImGui::SmallButton( "Reset" ) ) {
			if ( xCv ) Cvar_Set( xCv->name, xDefault );
			if ( yCv ) Cvar_Set( yCv->name, yDefault );
			if ( scaleCv ) Cvar_Set( scaleCv->name, scaleDefault );
			if ( widthCv ) Cvar_Set( widthCv->name, widthDefault );
			if ( alignCv ) Cvar_Set( alignCv->name, alignDefault );
		}
		controlsHovered = controlsHovered || ImGui::IsItemHovered() || ImGui::IsItemActive();
		if ( toggleCv ) {
			ImGui::SameLine( 0.0f, 4.0f );
			if ( ImGui::SmallButton( enabled ? "Off" : "On" ) ) {
				Cvar_Set( toggleCv->name, enabled ? "0" : "1" );
				if ( toggleCv2 ) Cvar_Set( toggleCv2->name, enabled ? "0" : "1" );
			}
			controlsHovered = controlsHovered || ImGui::IsItemHovered() || ImGui::IsItemActive();
		}
		if ( alignCv ) {
			int alignValue = alignCv->integer;
			bool alignRight = alignValue == alignRightValue;
			const char *alignLabel = alignRight ? "Right" : "Left";
			if ( alignModeCount >= 3 ) {
				if ( alignValue == 1 ) alignLabel = "Center";
				else if ( alignValue == 2 ) alignLabel = "Right";
				else alignLabel = "Left";
			}
			ImGui::SameLine( 0.0f, 4.0f );
			if ( ImGui::SmallButton( alignLabel ) ) {
				if ( alignModeCount >= 3 ) {
					int newAlign = ( alignValue + 1 ) % 3;
					CL_ImGuiSetAnchorAlignedCvar( xCv, alignCv, alignValue, newAlign, newAlign, drawW );
				} else {
					int newAlign = alignRight ? ( alignRightValue ? 0 : 1 ) : alignRightValue;
					CL_ImGuiSetAnchorAlignedCvar( xCv, alignCv, alignValue == alignRightValue ? 2 : 0, newAlign == alignRightValue ? 2 : 0, newAlign, drawW );
				}
			}
			controlsHovered = controlsHovered || ImGui::IsItemHovered() || ImGui::IsItemActive();
		}
	}
	ImVec2 gripPos = ImVec2( ImGui::GetWindowWidth() - 16.0f, ImGui::GetWindowHeight() - 16.0f );
	ImGui::SetCursorPos( gripPos );
	ImGui::InvisibleButton( "resize", ImVec2( 16.0f, 16.0f ) );
	resizing = ImGui::IsItemActive();
	controlsHovered = controlsHovered || ImGui::IsItemHovered() || ImGui::IsItemActive();
	bool moving = !s_liveSplitEditActive && ImGui::IsMouseDragging( 0 ) && hovered && !controlsHovered && !resizing;
	if ( moving && xCv && yCv ) {
		ImVec2 d = ImGui::GetIO().MouseDelta;
		float baseX = autoPos ? autoX : xCv->value;
		float baseY = autoPos ? autoY : yCv->value;
		float nextX = baseX + ( mirrorAlign && Cvar_VariableIntegerValue( "ls_align" ) == 1 ? -d.x / dxScale : d.x / dxScale );
		float maxX = 640.0f;
		Cvar_SetValue( xCv->name, Com_Clamp( 0.0f, maxX, nextX ) );
		Cvar_SetValue( yCv->name, Com_Clamp( 0.0f, 480.0f, baseY + d.y / dyScale ) );
	}
	if ( resizing ) {
		ImVec2 d = ImGui::GetIO().MouseDelta;
		if ( widthCv ) {
			float nextW = widthCv->value + d.x / dxScale;
			Cvar_SetValue( widthCv->name, Com_Clamp( minWidth, maxWidth, nextW ) );
		}
		if ( scaleCv ) {
			float scaleDelta = widthCv ? d.y / dyScale / 120.0f : ( d.x / dxScale / w + d.y / dyScale / h ) * 0.5f;
			Cvar_SetValue( scaleCv->name, Com_Clamp( minScale, maxScale, scaleCv->value + scaleDelta ) );
		}
	}
	if ( hovered && ImGui::IsMouseDoubleClicked( 0 ) && !controlsHovered && xCv && yCv ) {
		Cvar_Set( xCv->name, xDefault );
		Cvar_Set( yCv->name, yDefault );
		if ( scaleCv ) Cvar_Set( scaleCv->name, scaleDefault );
		if ( widthCv ) Cvar_Set( widthCv->name, widthDefault );
		if ( alignCv ) Cvar_Set( alignCv->name, alignDefault );
	}
	draw->AddRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), hovered ? IM_COL32( 245, 220, 80, 245 ) : ( enabled ? IM_COL32( 120, 230, 72, 145 ) : IM_COL32( 245, 70, 55, 150 ) ), 5.0f, 0, hovered ? 2.0f : 1.0f );
	draw->AddText( ImVec2( pos.x + 6.0f, pos.y + ( hovered ? 25.0f : 5.0f ) ), hovered ? IM_COL32( 255, 236, 120, 255 ) : ( enabled ? IM_COL32( 190, 245, 150, 210 ) : IM_COL32( 245, 120, 105, 220 ) ), autoPos ? va( "%s (auto 0,0)", label ) : ( enabled ? label : va( "%s (off)", label ) ) );
	if ( hovered || moving || resizing ) {
		float shownX = xCv ? xCv->value : labelX;
		float shownY = yCv ? yCv->value : labelY;
		if ( autoPos && !moving && !resizing ) {
			shownX = labelX;
			shownY = labelY;
		}
		draw->AddText( ImVec2( pos.x + 6.0f, pos.y + size.y - 19.0f ), IM_COL32( 255, 236, 120, 245 ), va( "x %.0f  y %.0f  scale %.2f", shownX, shownY, scaleCv ? scaleCv->value : scale ) );
	}
	draw->AddTriangleFilled( ImVec2( pos.x + size.x - 4.0f, pos.y + size.y - 16.0f ), ImVec2( pos.x + size.x - 4.0f, pos.y + size.y - 4.0f ), ImVec2( pos.x + size.x - 16.0f, pos.y + size.y - 4.0f ), hovered ? IM_COL32( 245, 220, 80, 220 ) : IM_COL32( 120, 230, 72, 150 ) );
	ImGui::PopID();
	ImGui::End();
	ImGui::PopStyleColor( 5 );
	ImGui::PopStyleVar( 4 );
}

static void CL_ImGuiDrawLayoutEditor( void ) {
	cvar_t *edit = CL_ImGuiCvar( "ui_speedrun_layout_edit", "0" );
	if ( !edit || edit->integer == 0 || cls.state != CA_ACTIVE ) return;
	if ( !ImGui::GetIO().MouseDown[0] ) {
		s_liveSplitEditActive = false;
	}
	/* LiveSplit ImGui draws its own exact edit handles; the generic HUD box used
	   the old fixed height and caused a second wrong resize/move box. */
	CL_ImGuiDragLayoutElement( "Keys", "ks_x", "ks_y", CL_ImGuiKeysDefaultXString(), "370", CL_ImGuiKeysHandleWidth(), CL_ImGuiKeysHandleHeight(), false, false, 0.0f, 0.0f, "cg_drawKeys", NULL, "ks_scale", NULL, 0.3f, 4.0f, 40.0f, 640.0f, 0, NULL, 1, ALIGN_STRETCH );
	CL_ImGuiDragLayoutElement( "Velocity", "cg_velocity_x", "cg_velocity_y", "320", "457", 44, CL_ImGuiVelocityHandleHeight(), false, false, 0.0f, 0.0f, "cg_drawVelocity", NULL, "cg_velocity_scale", NULL, 0.3f, 3.0f, 40.0f, 640.0f, Cvar_VariableIntegerValue( "cg_velocity_align" ) == 1 ? 1 : ( Cvar_VariableIntegerValue( "cg_velocity_align" ) == 2 ? 2 : 0 ), "cg_velocity_align", 2, Cvar_VariableIntegerValue( "cg_velocity_align" ) == 1 ? ALIGN_TOP : ( Cvar_VariableIntegerValue( "cg_velocity_align" ) == 2 ? ALIGN_TOPRIGHT : ALIGN_TOPLEFT ), "1.0", "178", 3, "1" );
	CL_ImGuiDragLayoutElement( "FPS/Timer", "cg_fpsX", "cg_fpsY", "500", "0", 96, 40, true, false, 500.0f, 0.0f, "cg_drawFPS", "cg_drawTimer", "cg_fpsScale", NULL, 0.25f, 4.0f, 40.0f, 640.0f, Cvar_VariableIntegerValue( "cg_fpsAlign" ) == 1 ? 0 : 2, "cg_fpsAlign", 0, Cvar_VariableIntegerValue( "cg_fpsAlign" ) == 1 ? ALIGN_TOPLEFT : ALIGN_TOPRIGHT );
	CL_ImGuiDragLayoutElement( "IGT", "ls_igttimer_x", "ls_igttimer_y", "638", "240", 72, Cvar_VariableIntegerValue( "ls_igtsegtimer" ) ? 18 : 10, false, false, 0.0f, 0.0f, "ls_igttimer", NULL, "ls_igttimer_scale", NULL, 0.3f, 4.0f, 40.0f, 640.0f, Cvar_VariableIntegerValue( "ls_igttimer_align" ) == 1 ? 1 : ( Cvar_VariableIntegerValue( "ls_igttimer_align" ) == 2 ? 2 : 0 ), "ls_igttimer_align", 2, ALIGN_STRETCH, "1.0", "178", 3, "2" );
}

static ImU32 CL_ImGuiColorU32( const char *name, const ImVec4 &fallback, float alphaMul = 1.0f ) {
	char buf[128];
	float r = fallback.x * 255.0f;
	float g = fallback.y * 255.0f;
	float b = fallback.z * 255.0f;
	float a = fallback.w;
	Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	if ( buf[0] ) {
		sscanf( buf, "%f %f %f %f", &r, &g, &b, &a );
	}
	a = Com_Clamp( 0.0f, 1.0f, a * alphaMul );
	return IM_COL32( (int)Com_Clamp( 0.0f, 255.0f, r ), (int)Com_Clamp( 0.0f, 255.0f, g ), (int)Com_Clamp( 0.0f, 255.0f, b ), (int)( a * 255.0f ) );
}

static ImU32 CL_ImGuiPackedColorU32( const char *text, const ImVec4 &fallback, float alphaMul = 1.0f ) {
	float r = fallback.x * 255.0f;
	float g = fallback.y * 255.0f;
	float b = fallback.z * 255.0f;
	float a = fallback.w;
	if ( text && text[0] ) {
		sscanf( text, "%f %f %f %f", &r, &g, &b, &a );
	}
	a = Com_Clamp( 0.0f, 1.0f, a * alphaMul );
	return IM_COL32( (int)Com_Clamp( 0.0f, 255.0f, r ), (int)Com_Clamp( 0.0f, 255.0f, g ), (int)Com_Clamp( 0.0f, 255.0f, b ), (int)( a * 255.0f ) );
}

static ImU32 CL_ImGuiGuiAccentU32( float alphaMul = 1.0f ) {
	return CL_ImGuiPackedColorU32( s_imguiAccent && s_imguiAccent->string ? s_imguiAccent->string : NULL, ImVec4( 0.36f, 0.82f, 0.21f, 1.00f ), alphaMul );
}

static ImU32 CL_ImGuiGuiAccentAltU32( float alphaMul = 1.0f ) {
	return CL_ImGuiPackedColorU32( s_imguiAccentAlt && s_imguiAccentAlt->string ? s_imguiAccentAlt->string : NULL, ImVec4( 0.96f, 0.74f, 0.24f, 1.00f ), alphaMul );
}

static ImU32 CL_ImGuiLerpColorU32( ImU32 a, ImU32 b, float t ) {
	ImVec4 ca = ImGui::ColorConvertU32ToFloat4( a );
	ImVec4 cb = ImGui::ColorConvertU32ToFloat4( b );
	t = Com_Clamp( 0.0f, 1.0f, t );
	return ImGui::ColorConvertFloat4ToU32( ImVec4(
		ca.x + ( cb.x - ca.x ) * t,
		ca.y + ( cb.y - ca.y ) * t,
		ca.z + ( cb.z - ca.z ) * t,
		ca.w + ( cb.w - ca.w ) * t ) );
}

static float CL_ImGuiGradientCornerT( float px, float py, float minX, float minY, float maxX, float maxY, float angleDeg ) {
	float rad = angleDeg * 0.01745329252f;
	float dx = cosf( rad );
	float dy = sinf( rad );
	float cx = ( minX + maxX ) * 0.5f;
	float cy = ( minY + maxY ) * 0.5f;
	float hx = ( maxX - minX ) * 0.5f;
	float hy = ( maxY - minY ) * 0.5f;
	float extent = fabsf( dx ) * hx + fabsf( dy ) * hy;
	if ( extent <= 0.001f ) return 0.0f;
	return Com_Clamp( 0.0f, 1.0f, ( ( px - cx ) * dx + ( py - cy ) * dy ) / ( extent * 2.0f ) + 0.5f );
}

static void CL_ImGuiAddRectFilledAngledGradient( ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 colA, ImU32 colB, float angleDeg ) {
	ImU32 c00 = CL_ImGuiLerpColorU32( colA, colB, CL_ImGuiGradientCornerT( min.x, min.y, min.x, min.y, max.x, max.y, angleDeg ) );
	ImU32 c10 = CL_ImGuiLerpColorU32( colA, colB, CL_ImGuiGradientCornerT( max.x, min.y, min.x, min.y, max.x, max.y, angleDeg ) );
	ImU32 c11 = CL_ImGuiLerpColorU32( colA, colB, CL_ImGuiGradientCornerT( max.x, max.y, min.x, min.y, max.x, max.y, angleDeg ) );
	ImU32 c01 = CL_ImGuiLerpColorU32( colA, colB, CL_ImGuiGradientCornerT( min.x, max.y, min.x, min.y, max.x, max.y, angleDeg ) );
	draw->AddRectFilledMultiColor( min, max, c00, c10, c11, c01 );
}

#define SRGUI_KEYSTROKE_KEY_COUNT 13
#define SRGUI_KEYSTROKE_TRAIL_POINT_COUNT 64

static int CL_ImGuiLiveSplitFontTierForScale( float uiScale );
static ImFont *CL_ImGuiLiveSplitFont( int fontIndex, int tier );

static float s_imguiKeystrokeAlpha[SRGUI_KEYSTROKE_KEY_COUNT] = { 0.0f };
static float s_imguiKeystrokeAppear[SRGUI_KEYSTROKE_KEY_COUNT] = { 0.0f };
static float s_imguiKeystrokePulse[SRGUI_KEYSTROKE_KEY_COUNT] = { 0.0f };
static bool  s_imguiKeystrokeWasPressed[SRGUI_KEYSTROKE_KEY_COUNT] = { false };
static float s_imguiKeystrokeMouseX = 0.0f;
static float s_imguiKeystrokeMouseY = 0.0f;
static float s_imguiKeystrokeMouseSpeed = 0.0f;
static float s_imguiKeystrokePrevYaw = 0.0f;
static float s_imguiKeystrokePrevPitch = 0.0f;
static bool  s_imguiKeystrokePrevViewInit = false;
static float s_imguiKeystrokeGridOffsetX = 0.0f;
static float s_imguiKeystrokeGridOffsetY = 0.0f;
static float s_imguiKeystrokeGridDirX = 0.0f;
static float s_imguiKeystrokeGridDirY = 0.0f;
static ImVec2 s_imguiKeystrokeTrailPoints[SRGUI_KEYSTROKE_TRAIL_POINT_COUNT];
static float s_imguiKeystrokeTrailAge[SRGUI_KEYSTROKE_TRAIL_POINT_COUNT];
static int   s_imguiKeystrokeTrailHead = 0;
static bool  s_imguiKeystrokeTrailInit = false;
static float s_imguiKeystrokeMouseCm = 0.0f;
static float s_imguiKeystrokeRunCm = 0.0f;
static float s_imguiKeystrokeCmSaveAccum = 0.0f;
static bool  s_imguiKeystrokeCmInit = false;
static bool  s_imguiKeystrokeRunWasActive = false;

static void CL_ImGuiDrawKeystrokeKey( ImDrawList *draw, ImFont *font, float fontSize, float x, float y, float w, float h, const char *label, float t, float rounding, float borderSize, ImU32 idleBg, ImU32 activeBg, ImU32 idleBorder, ImU32 activeBorder, ImU32 idleText, ImU32 activeText, float appear = 1.0f, float pulse = 0.0f ) {
	ImU32 bg = CL_ImGuiLerpColorU32( idleBg, activeBg, t );
	ImU32 border = CL_ImGuiLerpColorU32( idleBorder, activeBorder, t );
	ImU32 text = CL_ImGuiLerpColorU32( idleText, activeText, t );
	int effect = Cvar_VariableIntegerValue( "ks_effect" );
	appear = Com_Clamp( 0.0f, 1.0f, appear );
	if ( appear <= 0.01f ) return;
	if ( appear < 0.999f || pulse > 0.001f ) {
		float grow = 0.84f + 0.16f * appear + Com_Clamp( 0.0f, 1.0f, pulse ) * 0.10f;
		float cx = x + w * 0.5f;
		float cy = y + h * 0.5f;
		w *= grow;
		h *= grow;
		x = cx - w * 0.5f;
		y = cy - h * 0.5f;
	}
	if ( appear < 0.999f ) {
		ImVec4 c;
		c = ImGui::ColorConvertU32ToFloat4( bg ); c.w *= appear; bg = ImGui::ColorConvertFloat4ToU32( c );
		c = ImGui::ColorConvertU32ToFloat4( border ); c.w *= appear; border = ImGui::ColorConvertFloat4ToU32( c );
		c = ImGui::ColorConvertU32ToFloat4( text ); c.w *= appear; text = ImGui::ColorConvertFloat4ToU32( c );
	}
	ImVec2 textSize = font ? font->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, label ) : ImGui::CalcTextSize( label );
	ImVec2 textPos( x + ( w - textSize.x ) * 0.5f, y + ( h - textSize.y ) * 0.5f );
	draw->AddRectFilled( ImVec2( x, y ), ImVec2( x + w, y + h ), bg, rounding );
	if ( effect > 0 && t > 0.01f ) {
		float barH = ( 1.5f + h * 0.12f * t );
		ImVec4 accent = ImGui::ColorConvertU32ToFloat4( activeBorder );
		accent.w *= 0.38f * t;
		draw->AddRectFilled( ImVec2( x + 1.0f, y + h - barH - 1.0f ), ImVec2( x + w - 1.0f, y + h - 1.0f ), ImGui::ColorConvertFloat4ToU32( accent ), rounding * 0.45f );
		if ( effect >= 2 ) {
			accent.w = 0.18f * t;
			draw->AddRectFilled( ImVec2( x + 1.0f, y + 1.0f ), ImVec2( x + w - 1.0f, y + h - 1.0f ), ImGui::ColorConvertFloat4ToU32( accent ), rounding );
		}
		if ( effect >= 3 ) {
			float pulse = 0.55f + 0.45f * sinf( (float)ImGui::GetTime() * 7.0f );
			accent.w = ( 0.20f + 0.22f * pulse ) * t;
			draw->AddRect( ImVec2( x + 1.5f, y + 1.5f ), ImVec2( x + w - 1.5f, y + h - 1.5f ), ImGui::ColorConvertFloat4ToU32( accent ), rounding, 0, 1.5f );
		}
	}
	if ( borderSize > 0.0f ) draw->AddRect( ImVec2( x, y ), ImVec2( x + w, y + h ), border, rounding, 0, borderSize );
	if ( font ) draw->AddText( font, fontSize, textPos, text, label );
	else draw->AddText( textPos, text, label );
}

static void CL_ImGuiDrawKeystrokeMouseGrid( ImDrawList *draw, float x, float y, float w, float h, float dirX, float dirY, float rounding, float borderSize, ImU32 bg, ImU32 border, ImU32 dotColor ) {
	int gridLines = Cvar_VariableIntegerValue( "ks_mouse_grid_cells" );
	float dist = sqrtf( dirX * dirX + dirY * dirY );
	float size = w < h ? w : h;
	float cell, cx, cy, dotR, dt, autoSens, smooth, clipInset, fadeSeconds, oldOffsetX, oldOffsetY, paperDx, paperDy, localX, localY;
	int baseCellX, baseCellY;
	char cmText[32];
	ImVec4 borderF = ImGui::ColorConvertU32ToFloat4( border );
	ImVec4 checkerF = ImGui::ColorConvertU32ToFloat4( CL_ImGuiColorU32( "ks_clr_grid_checker", ImVec4( 0.08f, 0.12f, 0.08f, 0.42f ), 1.0f ) );
	ImVec4 dotF = ImGui::ColorConvertU32ToFloat4( dotColor );
	ImU32 centerLine = CL_ImGuiColorU32( "ks_clr_grid_cross", ImVec4( 0.92f, 0.86f, 0.35f, 0.72f ), 1.0f );
	ImU32 trailCol = CL_ImGuiColorU32( "ks_clr_grid_trail", ImVec4( 0.55f, 0.95f, 0.30f, 0.90f ), 1.0f );
	ImU32 cmCol = CL_ImGuiColorU32( "ks_clr_grid_cm", ImVec4( 0.85f, 0.95f, 0.70f, 0.88f ), 1.0f );
	if ( dist > 1.0f ) {
		dirX /= dist;
		dirY /= dist;
	}
	if ( size < 1.0f ) return;
	if ( gridLines < 3 ) gridLines = 3;
	if ( gridLines > 12 ) gridLines = 12;
	w = size;
	h = size;
	cell = size / (float)gridLines;
	dt = ImGui::GetIO().DeltaTime;
	if ( dt <= 0.0f || dt > 0.05f ) dt = 1.0f / 60.0f;
	autoSens = Com_Clamp( 0.45f, 4.0f, 0.70f + s_imguiKeystrokeMouseSpeed * 0.42f + dist * 1.10f );
	smooth = 1.0f - powf( 0.035f, dt * 60.0f );
	s_imguiKeystrokeGridDirX += ( dirX - s_imguiKeystrokeGridDirX ) * smooth;
	s_imguiKeystrokeGridDirY += ( dirY - s_imguiKeystrokeGridDirY ) * smooth;
	oldOffsetX = s_imguiKeystrokeGridOffsetX;
	oldOffsetY = s_imguiKeystrokeGridOffsetY;
	s_imguiKeystrokeGridOffsetX -= s_imguiKeystrokeGridDirX * cell * 8.0f * autoSens * dt;
	s_imguiKeystrokeGridOffsetY -= s_imguiKeystrokeGridDirY * cell * 8.0f * autoSens * dt;
	paperDx = s_imguiKeystrokeGridOffsetX - oldOffsetX;
	paperDy = s_imguiKeystrokeGridOffsetY - oldOffsetY;
	if ( !s_imguiKeystrokeCmInit ) {
		s_imguiKeystrokeMouseCm = Cvar_VariableValue( "ks_mouse_grid_total_cm_value" );
		s_imguiKeystrokeCmInit = true;
	}
	{
		bool runActive = Cvar_VariableIntegerValue( "ls_running" ) != 0;
		float cmDelta = ( s_imguiKeystrokeMouseSpeed * 2.40f + dist * 1.20f ) * dt;
		if ( runActive && !s_imguiKeystrokeRunWasActive ) {
			s_imguiKeystrokeRunCm = 0.0f;
		}
		s_imguiKeystrokeRunWasActive = runActive;
		s_imguiKeystrokeMouseCm += cmDelta;
		if ( runActive ) {
			s_imguiKeystrokeRunCm += cmDelta;
		}
		s_imguiKeystrokeCmSaveAccum += dt;
		if ( s_imguiKeystrokeCmSaveAccum >= 1.0f ) {
			Cvar_SetValue( "ks_mouse_grid_total_cm_value", s_imguiKeystrokeMouseCm );
			s_imguiKeystrokeCmSaveAccum = 0.0f;
		}
	}
	draw->AddRectFilled( ImVec2( x, y ), ImVec2( x + size, y + size ), bg, 0.0f );
	clipInset = 1.0f;
	if ( clipInset < borderSize + 1.0f ) clipInset = borderSize + 1.0f;
	if ( clipInset > cell * 0.42f ) clipInset = cell * 0.42f;
	baseCellX = (int)floorf( s_imguiKeystrokeGridOffsetX / cell );
	baseCellY = (int)floorf( s_imguiKeystrokeGridOffsetY / cell );
	localX = s_imguiKeystrokeGridOffsetX - (float)baseCellX * cell;
	localY = s_imguiKeystrokeGridOffsetY - (float)baseCellY * cell;
	draw->PushClipRect( ImVec2( x + clipInset, y + clipInset ), ImVec2( x + size - clipInset, y + size - clipInset ), true );
	for ( int iy = -2; iy <= gridLines + 2; ++iy ) {
		for ( int ix = -2; ix <= gridLines + 2; ++ix ) {
			if ( ( ( baseCellX + ix + baseCellY + iy ) & 1 ) == 0 ) {
				float rx = x + localX + cell * (float)ix;
				float ry = y + localY + cell * (float)iy;
				draw->AddRectFilled( ImVec2( rx, ry ), ImVec2( rx + cell, ry + cell ), ImGui::ColorConvertFloat4ToU32( checkerF ) );
			}
		}
	}
	for ( int i = -1; i <= gridLines + 1; ++i ) {
		float gx = x + localX + cell * (float)i;
		float gy = y + localY + cell * (float)i;
		ImVec4 lineCol = borderF;
		lineCol.w *= 0.58f;
		draw->AddLine( ImVec2( gx, y + clipInset ), ImVec2( gx, y + size - clipInset ), ImGui::ColorConvertFloat4ToU32( lineCol ), 1.0f );
		draw->AddLine( ImVec2( x + clipInset, gy ), ImVec2( x + size - clipInset, gy ), ImGui::ColorConvertFloat4ToU32( lineCol ), 1.0f );
	}
	cx = x + size * 0.5f;
	cy = y + size * 0.5f;
	dotR = 3.0f + Com_Clamp( 0.0f, 1.0f, dist ) * 2.5f;
	if ( !s_imguiKeystrokeTrailInit ) {
		for ( int i = 0; i < SRGUI_KEYSTROKE_TRAIL_POINT_COUNT; ++i ) {
			s_imguiKeystrokeTrailPoints[i] = ImVec2( 0.0f, 0.0f );
			s_imguiKeystrokeTrailAge[i] = 999.0f;
		}
		s_imguiKeystrokeTrailInit = true;
	}
	for ( int i = 0; i < SRGUI_KEYSTROKE_TRAIL_POINT_COUNT; ++i ) {
		s_imguiKeystrokeTrailAge[i] += dt;
		if ( s_imguiKeystrokeTrailAge[i] < 0.95f ) {
			s_imguiKeystrokeTrailPoints[i].x += paperDx;
			s_imguiKeystrokeTrailPoints[i].y += paperDy;
		}
	}
	if ( dist > 0.015f ) {
		int prev = ( s_imguiKeystrokeTrailHead + SRGUI_KEYSTROKE_TRAIL_POINT_COUNT - 1 ) % SRGUI_KEYSTROKE_TRAIL_POINT_COUNT;
		float dx = s_imguiKeystrokeTrailPoints[prev].x;
		float dy = s_imguiKeystrokeTrailPoints[prev].y;
		if ( s_imguiKeystrokeTrailAge[prev] > 0.030f || sqrtf( dx * dx + dy * dy ) > 1.4f ) {
			s_imguiKeystrokeTrailPoints[s_imguiKeystrokeTrailHead] = ImVec2( 0.0f, 0.0f );
			s_imguiKeystrokeTrailAge[s_imguiKeystrokeTrailHead] = 0.0f;
			s_imguiKeystrokeTrailHead = ( s_imguiKeystrokeTrailHead + 1 ) % SRGUI_KEYSTROKE_TRAIL_POINT_COUNT;
		}
	}
	fadeSeconds = 0.95f;
	for ( int i = 0; i < SRGUI_KEYSTROKE_TRAIL_POINT_COUNT - 1; ++i ) {
		int aIdx = ( s_imguiKeystrokeTrailHead + i ) % SRGUI_KEYSTROKE_TRAIL_POINT_COUNT;
		int bIdx = ( s_imguiKeystrokeTrailHead + i + 1 ) % SRGUI_KEYSTROKE_TRAIL_POINT_COUNT;
		if ( s_imguiKeystrokeTrailAge[aIdx] < fadeSeconds && s_imguiKeystrokeTrailAge[bIdx] < fadeSeconds ) {
			float a = 1.0f - ( ( s_imguiKeystrokeTrailAge[aIdx] > s_imguiKeystrokeTrailAge[bIdx] ? s_imguiKeystrokeTrailAge[aIdx] : s_imguiKeystrokeTrailAge[bIdx] ) / fadeSeconds );
			ImVec4 trailGlow = ImGui::ColorConvertU32ToFloat4( trailCol );
			ImVec4 lineCol = trailGlow;
			a = Com_Clamp( 0.0f, 1.0f, a );
			trailGlow.w *= a * a * 0.22f;
			lineCol.w *= a * 0.92f;
			draw->AddLine( ImVec2( cx + s_imguiKeystrokeTrailPoints[aIdx].x, cy + s_imguiKeystrokeTrailPoints[aIdx].y ), ImVec2( cx + s_imguiKeystrokeTrailPoints[bIdx].x, cy + s_imguiKeystrokeTrailPoints[bIdx].y ), ImGui::ColorConvertFloat4ToU32( trailGlow ), dotR * 3.0f );
			draw->AddLine( ImVec2( cx + s_imguiKeystrokeTrailPoints[aIdx].x, cy + s_imguiKeystrokeTrailPoints[aIdx].y ), ImVec2( cx + s_imguiKeystrokeTrailPoints[bIdx].x, cy + s_imguiKeystrokeTrailPoints[bIdx].y ), ImGui::ColorConvertFloat4ToU32( lineCol ), 2.0f + a * 0.8f );
		}
	}
	draw->AddLine( ImVec2( cx, y + clipInset ), ImVec2( cx, y + size - clipInset ), centerLine, 1.3f );
	draw->AddLine( ImVec2( x + clipInset, cy ), ImVec2( x + size - clipInset, cy ), centerLine, 1.3f );
	dotF = ImGui::ColorConvertU32ToFloat4( dotColor );
	dotF.w *= 0.25f;
	draw->AddCircleFilled( ImVec2( cx, cy ), dotR + 4.0f, centerLine, 24 );
	draw->AddCircleFilled( ImVec2( cx, cy ), dotR, dotColor, 24 );
	draw->AddCircle( ImVec2( cx, cy ), dotR + 1.5f, dotColor, 24, 1.2f );
	if ( Cvar_VariableIntegerValue( "ks_mouse_grid_cm" ) ) {
		Com_sprintf( cmText, sizeof( cmText ), "%.1f cm", s_imguiKeystrokeMouseCm );
		draw->AddText( ImVec2( x + size - ImGui::CalcTextSize( cmText ).x - 5.0f, y + size - ImGui::GetTextLineHeight() * ( Cvar_VariableIntegerValue( "ks_mouse_grid_run_cm" ) ? 2.0f : 1.0f ) - 4.0f ), cmCol, cmText );
	}
	if ( Cvar_VariableIntegerValue( "ks_mouse_grid_run_cm" ) ) {
		Com_sprintf( cmText, sizeof( cmText ), "run %.1f cm", s_imguiKeystrokeRunCm );
		draw->AddText( ImVec2( x + size - ImGui::CalcTextSize( cmText ).x - 5.0f, y + size - ImGui::GetTextLineHeight() - 4.0f ), cmCol, cmText );
	}
	draw->PopClipRect();
	if ( borderSize > 0.0f ) draw->AddRect( ImVec2( x, y ), ImVec2( x + size, y + size ), border, 0.0f, 0, borderSize );
}

static void CL_ImGuiDrawKeystrokeMouseDirection( ImDrawList *draw, float x, float y, float size, float dirX, float dirY, float rounding, float borderSize, ImU32 bg, ImU32 border, ImU32 dotColor ) {
	float radius = size * 0.5f;
	float dotR = size * 0.12f;
	float maxOff = radius - dotR - 2.0f;
	float dist = sqrtf( dirX * dirX + dirY * dirY );
	if ( dist > 1.0f ) {
		dirX /= dist;
		dirY /= dist;
	}
	draw->AddRectFilled( ImVec2( x, y ), ImVec2( x + size, y + size ), bg, rounding );
	if ( borderSize > 0.0f ) draw->AddRect( ImVec2( x, y ), ImVec2( x + size, y + size ), border, rounding, 0, borderSize );
	draw->AddCircleFilled( ImVec2( x + radius + dirX * maxOff, y + radius + dirY * maxOff ), dotR, dotColor, 18 );
}

static int CL_ImGuiCollectActiveKeystrokes( const bool pressed[SRGUI_KEYSTROKE_KEY_COUNT], int out[SRGUI_KEYSTROKE_KEY_COUNT], int maxCount ) {
	static const int order[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10 };
	int count = 0;
	if ( maxCount < 1 ) maxCount = SRGUI_KEYSTROKE_KEY_COUNT;
	for ( int i = 0; i < IM_ARRAYSIZE( order ); ++i ) {
		int idx = order[i];
		if ( idx == 9 && !Cvar_VariableIntegerValue( "ks_show_use" ) ) continue;
		if ( idx == 10 && !Cvar_VariableIntegerValue( "ks_show_reload" ) ) continue;
		if ( pressed[idx] ) {
			out[count++] = idx;
			if ( count >= maxCount ) break;
		}
	}
	return count;
}

static void CL_ImGuiDrawActiveKeystrokeStrip( ImDrawList *draw, ImFont *font, float fontSize, float x, float y, float maxW, float maxH, float itemW, float itemH, float gap, bool vertical, int anchor, int maxCount, const bool pressed[SRGUI_KEYSTROKE_KEY_COUNT], const char *labels[SRGUI_KEYSTROKE_KEY_COUNT], float rounding, float borderSize, ImU32 idleBg, ImU32 activeBg, ImU32 idleBorder, ImU32 activeBorder, ImU32 idleText, ImU32 activeText ) {
	int active[SRGUI_KEYSTROKE_KEY_COUNT];
	int count = CL_ImGuiCollectActiveKeystrokes( pressed, active, maxCount );
	if ( count <= 0 ) return;
	if ( vertical ) {
		float totalH = itemH * (float)count + gap * (float)( count - 1 );
		float startY = ( anchor == 3 ) ? y + maxH - totalH : ( anchor == 2 ? y + ( maxH - totalH ) * 0.5f : y );
		for ( int i = 0; i < count; ++i ) {
			CL_ImGuiDrawKeystrokeKey( draw, font, fontSize, x, startY + ( itemH + gap ) * (float)i, itemW, itemH, labels[active[i]], s_imguiKeystrokeAlpha[active[i]], rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, s_imguiKeystrokeAppear[active[i]], s_imguiKeystrokePulse[active[i]] );
		}
	} else {
		float totalW = itemW * (float)count + gap * (float)( count - 1 );
		float startX = ( anchor == 1 ) ? x + maxW - totalW : ( anchor == 2 ? x + ( maxW - totalW ) * 0.5f : x );
		for ( int i = 0; i < count; ++i ) {
			CL_ImGuiDrawKeystrokeKey( draw, font, fontSize, startX + ( itemW + gap ) * (float)i, y, itemW, itemH, labels[active[i]], s_imguiKeystrokeAlpha[active[i]], rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, s_imguiKeystrokeAppear[active[i]], s_imguiKeystrokePulse[active[i]] );
		}
	}
}

static void CL_ImGuiEnsureKeystrokeCvars( void ) {
	CL_ImGuiCvar( "ks_imgui", "1" );
	Cvar_Set( "ks_imgui", "1" );
	CL_ImGuiCvar( "ks_layout", "0" );
	CL_ImGuiCvar( "ks_box_w", "22" );
	CL_ImGuiCvar( "ks_box_h", "18" );
	CL_ImGuiCvar( "ks_gap", "2" );
	CL_ImGuiCvar( "ks_rounding", "0" );
	Cvar_Set( "ks_rounding", "0" );
	CL_ImGuiCvar( "ks_border_size", "1" );
	CL_ImGuiCvar( "ks_font_scale", "0.620000" );
	CL_ImGuiCvar( "ks_effect", "1" );
	CL_ImGuiCvar( "ks_mouse_grid_size", "92" );
	CL_ImGuiCvar( "ks_mouse_grid_cells", "5" );
	CL_ImGuiCvar( "ks_mouse_grid_cm", "1" );
	CL_ImGuiCvar( "ks_mouse_grid_run_cm", "1" );
	CL_ImGuiCvar( "ks_mouse_grid_total_cm_value", "232.626083" );
	CL_ImGuiCvar( "ks_active_anchor", "0" );
	CL_ImGuiCvar( "ks_active_max", "3" );
	CL_ImGuiCvar( "ks_grid_keys", "0" );
	CL_ImGuiCvar( "ks_grid_keys_dir", "1" );
	CL_ImGuiCvar( "ks_grid_keys_x", "-35" );
	CL_ImGuiCvar( "ks_grid_keys_y", "-15" );
	CL_ImGuiCvar( "ks_grid_keys_font_scale", "0.72" );
	CL_ImGuiCvar( "ks_ingame_only", "1" );
	CL_ImGuiCvar( "ks_show_use", "0" );
	CL_ImGuiCvar( "ks_show_reload", "0" );
	CL_ImGuiCvar( "ks_clr_bg", "" );
	CL_ImGuiCvar( "ks_clr_active", "" );
	CL_ImGuiCvar( "ks_clr_border", "" );
	CL_ImGuiCvar( "ks_clr_active_border", "" );
	CL_ImGuiCvar( "ks_clr_text", "" );
	CL_ImGuiCvar( "ks_clr_active_text", "" );
	CL_ImGuiCvar( "ks_clr_grid_checker", "" );
	CL_ImGuiCvar( "ks_clr_grid_cross", "" );
	CL_ImGuiCvar( "ks_clr_grid_trail", "" );
	CL_ImGuiCvar( "ks_clr_grid_cm", "" );
}

static void CL_ImGuiDrawKeystrokesOverlay( void ) {
	CL_ImGuiEnsureKeystrokeCvars();
	if ( !Cvar_VariableIntegerValue( "cg_drawKeys" ) ) return;
	if ( cls.state < CA_ACTIVE ) return;
	if ( Cvar_VariableIntegerValue( "ks_ingame_only" ) && cls.keyCatchers && !Cvar_VariableIntegerValue( "ui_speedrun_layout_edit" ) ) return;

	usercmd_t cmd = cl.cmds[cl.cmdNumber & CMD_MASK];
	float sx = cls.glconfig.vidWidth / 640.0f;
	float sy = cls.glconfig.vidHeight / 480.0f;
	float screenScale = ( sx + sy ) * 0.5f;
	float scale = Com_Clamp( 0.30f, 4.0f, Cvar_VariableValue( "ks_scale" ) );
	float localFontScale = Com_Clamp( 0.30f, 2.4f, Cvar_VariableValue( "ks_font_scale" ) );
	float opacity = Com_Clamp( 0.0f, 1.0f, Cvar_VariableValue( "ks_opacity" ) );
	float boxW = Com_Clamp( 14.0f, 80.0f, Cvar_VariableValue( "ks_box_w" ) ) * scale * screenScale;
	float boxH = Com_Clamp( 12.0f, 54.0f, Cvar_VariableValue( "ks_box_h" ) ) * scale * screenScale;
	float gap = Com_Clamp( 0.0f, 20.0f, Cvar_VariableValue( "ks_gap" ) ) * scale * screenScale;
	float rounding = 0.0f;
	float borderSize = Com_Clamp( 0.0f, 5.0f, Cvar_VariableValue( "ks_border_size" ) ) * scale * screenScale;
	float x = ( Cvar_VariableValue( "ks_x" ) > 0.01f ? Cvar_VariableValue( "ks_x" ) : CL_ImGuiKeysDefaultX() ) * sx;
	float y = ( Cvar_VariableValue( "ks_y" ) > 0.01f ? Cvar_VariableValue( "ks_y" ) : 370.0f ) * sy;
	int layout = Cvar_VariableIntegerValue( "ks_layout" );
	int mouseMode = Cvar_VariableIntegerValue( "ks_mouse" );
	int activeAnchor = Cvar_VariableIntegerValue( "ks_active_anchor" );
	int activeMax = Cvar_VariableIntegerValue( "ks_active_max" );
	float gridSize = Com_Clamp( 48.0f, 220.0f, Cvar_VariableValue( "ks_mouse_grid_size" ) ) * scale * screenScale;
	bool pressed[SRGUI_KEYSTROKE_KEY_COUNT];
	const char *labels[SRGUI_KEYSTROKE_KEY_COUNT] = { "W", "A", "S", "D", "JUMP", "DUCK", "FIRE", "RMB", "RUN", "USE", "RLD", "LEAN L", "LEAN R" };
	float wideW = ( boxW * 3.0f + gap * 2.0f - gap ) * 0.5f;
	float dt = ImGui::GetIO().DeltaTime;
	if ( opacity <= 0.0f ) return;
	if ( dt <= 0.0f || dt > 0.05f ) dt = 1.0f / 60.0f;
	if ( activeMax < 1 ) activeMax = 1;
	if ( activeMax > SRGUI_KEYSTROKE_KEY_COUNT ) activeMax = SRGUI_KEYSTROKE_KEY_COUNT;

	pressed[0] = cmd.forwardmove > 0;
	pressed[1] = cmd.rightmove < 0;
	pressed[2] = cmd.forwardmove < 0;
	pressed[3] = cmd.rightmove > 0;
	pressed[4] = ( cmd.wbuttons & WBUTTON_JUMP ) != 0;
	pressed[5] = ( cmd.wbuttons & WBUTTON_CROUCH ) != 0;
	pressed[6] = ( cmd.buttons & BUTTON_ATTACK ) != 0;
	pressed[7] = ( cmd.wbuttons & WBUTTON_ATTACK2 ) != 0;
	pressed[8] = ( cmd.buttons & BUTTON_SPRINT ) != 0;
	pressed[9] = ( cmd.buttons & BUTTON_ACTIVATE ) != 0;
	pressed[10] = ( cmd.wbuttons & WBUTTON_RELOAD ) != 0;
	pressed[11] = ( cmd.wbuttons & WBUTTON_LEANLEFT ) != 0;
	pressed[12] = ( cmd.wbuttons & WBUTTON_LEANRIGHT ) != 0;
	for ( int i = 0; i < SRGUI_KEYSTROKE_KEY_COUNT; ++i ) {
		float target = pressed[i] ? 1.0f : 0.0f;
		float speed = pressed[i] ? 18.0f : 8.0f;
		float appearTarget = pressed[i] ? 1.0f : 0.0f;
		if ( pressed[i] && !s_imguiKeystrokeWasPressed[i] ) {
			s_imguiKeystrokePulse[i] = 1.0f;
		}
		s_imguiKeystrokeWasPressed[i] = pressed[i];
		if ( s_imguiKeystrokeAlpha[i] < target ) {
			s_imguiKeystrokeAlpha[i] += speed * dt;
			if ( s_imguiKeystrokeAlpha[i] > target ) s_imguiKeystrokeAlpha[i] = target;
		} else if ( s_imguiKeystrokeAlpha[i] > target ) {
			s_imguiKeystrokeAlpha[i] -= speed * dt;
			if ( s_imguiKeystrokeAlpha[i] < target ) s_imguiKeystrokeAlpha[i] = target;
		}
		if ( s_imguiKeystrokeAppear[i] < appearTarget ) {
			s_imguiKeystrokeAppear[i] += 14.0f * dt;
			if ( s_imguiKeystrokeAppear[i] > appearTarget ) s_imguiKeystrokeAppear[i] = appearTarget;
		} else if ( s_imguiKeystrokeAppear[i] > appearTarget ) {
			s_imguiKeystrokeAppear[i] -= 9.0f * dt;
			if ( s_imguiKeystrokeAppear[i] < appearTarget ) s_imguiKeystrokeAppear[i] = appearTarget;
		}
		s_imguiKeystrokePulse[i] -= 7.5f * dt;
		if ( s_imguiKeystrokePulse[i] < 0.0f ) s_imguiKeystrokePulse[i] = 0.0f;
	}

	if ( mouseMode >= 2 || layout == 3 || layout == 7 ) {
		if ( s_imguiKeystrokePrevViewInit ) {
			float dYaw = AngleSubtract( cl.viewangles[YAW], s_imguiKeystrokePrevYaw );
			float dPitch = cl.viewangles[PITCH] - s_imguiKeystrokePrevPitch;
			float rawSpeed = sqrtf( dYaw * dYaw + dPitch * dPitch );
			s_imguiKeystrokeMouseX = Com_Clamp( -1.0f, 1.0f, s_imguiKeystrokeMouseX * 0.70f + dYaw * 0.08f );
			s_imguiKeystrokeMouseY = Com_Clamp( -1.0f, 1.0f, s_imguiKeystrokeMouseY * 0.70f + dPitch * 0.08f );
			s_imguiKeystrokeMouseSpeed += ( rawSpeed - s_imguiKeystrokeMouseSpeed ) * 0.24f;
		} else {
			s_imguiKeystrokePrevViewInit = true;
		}
		s_imguiKeystrokePrevYaw = cl.viewangles[YAW];
		s_imguiKeystrokePrevPitch = cl.viewangles[PITCH];
	} else {
		s_imguiKeystrokeMouseX *= 0.90f;
		s_imguiKeystrokeMouseY *= 0.90f;
		s_imguiKeystrokeMouseSpeed *= 0.90f;
	}

	ImDrawList *draw = ImGui::GetForegroundDrawList();
	int tier = CL_ImGuiLiveSplitFontTierForScale( scale * localFontScale * screenScale );
	ImFont *font = CL_ImGuiLiveSplitFont( 1, tier );
	float fontSize = 11.5f * scale * localFontScale * screenScale;
	ImU32 idleBg = CL_ImGuiColorU32( "ks_clr_bg", ImVec4( 0.04f, 0.04f, 0.06f, 0.72f ), opacity );
	ImU32 activeBg = CL_ImGuiColorU32( "ks_clr_active", ImVec4( 0.10f, 0.24f, 0.07f, 0.88f ), opacity );
	ImU32 idleBorder = CL_ImGuiColorU32( "ks_clr_border", ImVec4( 0.20f, 0.25f, 0.18f, 0.30f ), opacity );
	ImU32 activeBorder = CL_ImGuiColorU32( "ks_clr_active_border", ImVec4( 0.42f, 0.75f, 0.22f, 0.85f ), opacity );
	ImU32 idleText = CL_ImGuiColorU32( "ks_clr_text", ImVec4( 0.50f, 0.56f, 0.48f, 0.78f ), opacity );
	ImU32 activeText = CL_ImGuiColorU32( "ks_clr_active_text", ImVec4( 0.86f, 0.97f, 0.72f, 1.00f ), opacity );

#define DRAW_KS(idx, px, py, pw) CL_ImGuiDrawKeystrokeKey( draw, font, fontSize, (px), (py), (pw), boxH, labels[(idx)], s_imguiKeystrokeAlpha[(idx)], rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, 1.0f, s_imguiKeystrokePulse[(idx)] )
	if ( layout == 1 ) {
		float cx = x;
		for ( int i = 0; i < 6; ++i ) { DRAW_KS( i, cx, y, i >= 4 ? wideW : boxW ); cx += ( i >= 4 ? wideW : boxW ) + gap; }
		if ( mouseMode >= 1 ) { DRAW_KS( 6, cx, y, wideW ); cx += wideW + gap; DRAW_KS( 8, cx, y, wideW ); cx += wideW + gap; }
		if ( Cvar_VariableIntegerValue( "ks_show_use" ) ) { DRAW_KS( 9, cx, y, wideW ); cx += wideW + gap; }
		if ( Cvar_VariableIntegerValue( "ks_show_reload" ) ) { DRAW_KS( 10, cx, y, wideW ); cx += wideW + gap; }
	} else if ( layout == 3 ) {
		float keysW = boxW * 3.0f + gap * 2.0f;
		float ctrlW = boxW * 1.35f;
		float spaceW = keysW - ctrlW - gap;
		float gridX = x + keysW + gap * 2.0f;
		float gridH = boxH * 3.0f + gap * 2.0f;
		if ( gridSize < gridH ) gridSize = gridH;
		DRAW_KS( 6, x, y, boxW );
		DRAW_KS( 0, x + boxW + gap, y, boxW );
		DRAW_KS( 8, x + ( boxW + gap ) * 2.0f, y, boxW );
		DRAW_KS( 1, x, y + boxH + gap, boxW );
		DRAW_KS( 2, x + boxW + gap, y + boxH + gap, boxW );
		DRAW_KS( 3, x + ( boxW + gap ) * 2.0f, y + boxH + gap, boxW );
		DRAW_KS( 5, x, y + ( boxH + gap ) * 2.0f, ctrlW );
		DRAW_KS( 4, x + ctrlW + gap, y + ( boxH + gap ) * 2.0f, spaceW );
		CL_ImGuiDrawKeystrokeMouseGrid( draw, gridX, y, gridSize, gridSize, -s_imguiKeystrokeMouseX, s_imguiKeystrokeMouseY, rounding, borderSize, idleBg, idleBorder, activeText );
	} else if ( layout == 4 ) {
		int order[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10 };
		float cy = y;
		for ( int i = 0; i < IM_ARRAYSIZE( order ); ++i ) {
			int idx = order[i];
			if ( idx == 9 && !Cvar_VariableIntegerValue( "ks_show_use" ) ) continue;
			if ( idx == 10 && !Cvar_VariableIntegerValue( "ks_show_reload" ) ) continue;
			DRAW_KS( idx, x, cy, wideW );
			cy += boxH + gap;
		}
	} else if ( layout == 5 ) {
		CL_ImGuiDrawActiveKeystrokeStrip( draw, font, fontSize, x, y, CL_ImGuiKeysHandleWidth() * scale * screenScale, boxH, wideW, boxH, gap, false, activeAnchor, activeMax, pressed, labels, rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText );
	} else if ( layout == 6 ) {
		CL_ImGuiDrawActiveKeystrokeStrip( draw, font, fontSize, x, y, wideW, CL_ImGuiKeysHandleHeight() * scale * screenScale, wideW, boxH, gap, true, activeAnchor, activeMax, pressed, labels, rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText );
	} else if ( layout == 7 ) {
		float miniFontScale = Com_Clamp( 0.30f, 1.20f, Cvar_VariableValue( "ks_grid_keys_font_scale" ) );
		float miniW = gridSize * 0.20f;
		float miniH = boxH * 0.72f;
		float miniGap = gap > 2.0f ? gap * 0.55f : 1.0f;
		bool miniVertical = Cvar_VariableIntegerValue( "ks_grid_keys_dir" ) != 0;
		float miniMaxW = gridSize * 0.78f;
		float miniMaxH = gridSize * 0.62f;
		float miniX = x + ( gridSize - ( miniVertical ? miniW : miniMaxW ) ) * 0.5f + Cvar_VariableValue( "ks_grid_keys_x" ) * scale * screenScale;
		float miniY = y + ( gridSize - ( miniVertical ? miniMaxH : miniH ) ) * 0.5f + Cvar_VariableValue( "ks_grid_keys_y" ) * scale * screenScale;
		CL_ImGuiDrawKeystrokeMouseGrid( draw, x, y, gridSize, gridSize, -s_imguiKeystrokeMouseX, s_imguiKeystrokeMouseY, rounding, borderSize, idleBg, idleBorder, activeText );
		if ( Cvar_VariableIntegerValue( "ks_grid_keys" ) ) {
			CL_ImGuiDrawActiveKeystrokeStrip( draw, font, fontSize * miniFontScale, miniX, miniY, miniMaxW, miniMaxH, miniW, miniH, miniGap, miniVertical, activeAnchor, activeMax, pressed, labels, 0.0f, borderSize * 0.65f, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText );
		}
	} else {
		DRAW_KS( 0, x + boxW + gap, y, boxW );
		DRAW_KS( 1, x, y + boxH + gap, boxW );
		DRAW_KS( 2, x + boxW + gap, y + boxH + gap, boxW );
		DRAW_KS( 3, x + ( boxW + gap ) * 2.0f, y + boxH + gap, boxW );
		DRAW_KS( 4, x, y + ( boxH + gap ) * 2.0f, wideW );
		DRAW_KS( 5, x + wideW + gap, y + ( boxH + gap ) * 2.0f, wideW );
		if ( layout != 2 ) {
			float row = 3.0f;
			if ( mouseMode >= 1 ) {
				float my = y + ( boxH + gap ) * row;
				if ( mouseMode >= 2 ) {
					float dirSize = boxH;
					float btnW = ( boxW * 3.0f + gap * 2.0f - dirSize - gap * 2.0f ) * 0.5f;
					DRAW_KS( 6, x, my, btnW );
					CL_ImGuiDrawKeystrokeMouseDirection( draw, x + btnW + gap, my, dirSize, -s_imguiKeystrokeMouseX, s_imguiKeystrokeMouseY, rounding, borderSize, idleBg, idleBorder, activeText );
					DRAW_KS( 8, x + btnW + gap + dirSize + gap, my, btnW );
				} else {
					DRAW_KS( 6, x, my, wideW );
					DRAW_KS( 8, x + wideW + gap, my, wideW );
				}
				row += 1.0f;
			}
			if ( Cvar_VariableIntegerValue( "ks_show_use" ) || Cvar_VariableIntegerValue( "ks_show_reload" ) ) {
				float ex = x;
				float ey = y + ( boxH + gap ) * row;
				if ( Cvar_VariableIntegerValue( "ks_show_use" ) ) { DRAW_KS( 9, ex, ey, wideW ); ex += wideW + gap; }
				if ( Cvar_VariableIntegerValue( "ks_show_reload" ) ) { DRAW_KS( 10, ex, ey, wideW ); ex += wideW + gap; }
			}
		}
	}
#undef DRAW_KS
}

static void CL_ImGuiLiveSplitShowAll( void ) {
	Cvar_Set( "ls_draw", "1" );
	Cvar_Set( "ls_showtimer", "1" );
	Cvar_Set( "ls_showheader", "1" );
	Cvar_Set( "ls_showstats", "1" );
	Cvar_Set( "ls_showseg", "1" );
	Cvar_Set( "ls_showrgt", "1" );
	Cvar_Set( "ls_showpb", "1" );
	Cvar_Set( "ls_showbest", "1" );
	Cvar_Set( "ls_showseps", "1" );
	Cvar_Set( "ls_showdeltas", "1" );
	Cvar_Set( "ls_showbestdeltas", "1" );
	Cvar_Set( "ls_showatt", "1" );
	Cvar_Set( "ls_imgui_show_title", "1" );
	Cvar_Set( "ls_imgui_show_prevseg", "1" );
	Cvar_Set( "ls_imgui_show_ghostseg", "1" );
	Cvar_Set( "ls_imgui_show_bestsegments", "1" );
}

static void CL_ImGuiLiveSplitMinimal( void ) {
	Cvar_Set( "ls_draw", "1" );
	Cvar_Set( "ls_showtimer", "1" );
	Cvar_Set( "ls_showheader", "0" );
	Cvar_Set( "ls_showstats", "0" );
	Cvar_Set( "ls_showseg", "0" );
	Cvar_Set( "ls_showrgt", "0" );
	Cvar_Set( "ls_showpb", "0" );
	Cvar_Set( "ls_showbest", "0" );
	Cvar_Set( "ls_showseps", "0" );
	Cvar_Set( "ls_showdeltas", "0" );
	Cvar_Set( "ls_showbestdeltas", "0" );
	Cvar_Set( "ls_showatt", "0" );
	Cvar_Set( "ls_100pct", "0" );
	Cvar_Set( "ls_imgui_show_title", "1" );
	Cvar_Set( "ls_imgui_show_prevseg", "0" );
	Cvar_Set( "ls_imgui_show_ghostseg", "0" );
	Cvar_Set( "ls_imgui_show_bestsegments", "0" );
}

static bool CL_ImGuiLiveSplitToggleButton( const char *label, const char *name, const char *defaultValue = "1" ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	bool enabled = cv && cv->integer != 0;
	bool clicked;
	ImGui::PushID( name );
	ImGui::PushStyleColor( ImGuiCol_Button, enabled ? ImVec4( 0.12f, 0.22f, 0.08f, 0.96f ) : ImVec4( 0.07f, 0.08f, 0.07f, 0.72f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonHovered, enabled ? ImVec4( 0.20f, 0.36f, 0.12f, 1.00f ) : ImVec4( 0.13f, 0.15f, 0.12f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_Text, enabled ? ImVec4( 0.84f, 0.96f, 0.74f, 1.00f ) : ImVec4( 0.52f, 0.56f, 0.50f, 1.00f ) );
	clicked = ImGui::SmallButton( label );
	if ( clicked && cv ) {
		Cvar_Set( cv->name, enabled ? "0" : "1" );
	}
	ImGui::PopStyleColor( 3 );
	ImGui::PopID();
	return clicked;
}

static void CL_ImGuiAddTextGradient( ImDrawList *draw, ImVec2 pos, ImU32 colA, ImU32 colB, const char *text, float angleDeg ) {
	float rad = angleDeg * 0.01745329252f;
	float dx = cosf( rad );
	float dy = sinf( rad );
	ImVec2 full = ImGui::CalcTextSize( text );
	float extent = fabsf( dx ) * full.x + fabsf( dy ) * full.y;
	float advance = 0.0f;
	char glyph[8];
	int i;
	if ( extent <= 0.001f ) {
		draw->AddText( pos, colA, text );
		return;
	}
	for ( i = 0; text[i]; ++i ) {
		glyph[0] = text[i];
		glyph[1] = '\0';
		ImVec2 glyphSize = ImGui::CalcTextSize( glyph );
		float centerX = advance + glyphSize.x * 0.5f - full.x * 0.5f;
		float centerY = -full.y * 0.5f;
		float t = ( centerX * dx + centerY * dy ) / extent + 0.5f;
		draw->AddText( ImVec2( pos.x + advance, pos.y ), CL_ImGuiLerpColorU32( colA, colB, t ), glyph );
		advance += glyphSize.x;
	}
}

static void CL_ImGuiAddTextShadow( ImDrawList *draw, ImVec2 pos, ImU32 col, const char *text, bool shadow ) {
	bool textGradient = Cvar_VariableIntegerValue( "ls_imgui_text_gradient" ) != 0;
	if ( !text || !text[0] ) return;
	if ( shadow ) {
		draw->AddText( ImVec2( pos.x + 1.0f, pos.y + 1.0f ), IM_COL32( 0, 0, 0, 170 ), text );
	}
	if ( textGradient ) {
		ImU32 col2 = CL_ImGuiColorU32( "ls_clr_text_gradient2", ImVec4( 0.36f, 0.82f, 0.21f, 1.00f ), 1.0f );
		CL_ImGuiAddTextGradient( draw, pos, col, col2, text, Cvar_VariableValue( "ls_imgui_text_gradient_angle" ) );
		return;
	}
	draw->AddText( pos, col, text );
}

static void CL_ImGuiAddTextRight( ImDrawList *draw, float right, float y, ImU32 col, const char *text, bool shadow ) {
	ImVec2 sz;
	if ( !text || !text[0] ) return;
	sz = ImGui::CalcTextSize( text );
	CL_ImGuiAddTextShadow( draw, ImVec2( right - sz.x, y ), col, text, shadow );
}

static void CL_ImGuiAddTextCentered( ImDrawList *draw, float center, float y, ImU32 col, const char *text, bool shadow ) {
	ImVec2 sz;
	if ( !text || !text[0] ) return;
	sz = ImGui::CalcTextSize( text );
	CL_ImGuiAddTextShadow( draw, ImVec2( center - sz.x * 0.5f, y ), col, text, shadow );
}

static void CL_ImGuiAddClippedText( ImDrawList *draw, ImVec2 pos, ImVec2 clipMin, ImVec2 clipMax, ImU32 col, const char *text, bool shadow ) {
	if ( !text || !text[0] ) return;
	draw->PushClipRect( clipMin, clipMax, true );
	CL_ImGuiAddTextShadow( draw, pos, col, text, shadow );
	draw->PopClipRect();
}

static void CL_ImGuiAddRectLines( ImDrawList *draw, float x, float y, float w, float h, ImU32 col, float thickness ) {
	float x0 = floorf( x ) + 0.5f;
	float y0 = floorf( y ) + 0.5f;
	float x1 = floorf( x + w ) - 0.5f;
	float y1 = floorf( y + h ) - 0.5f;
	draw->PushClipRect( ImVec2( x - 3.0f, y - 3.0f ), ImVec2( x + w + 3.0f, y + h + 3.0f ), false );
	draw->AddLine( ImVec2( x0, y0 ), ImVec2( x1, y0 ), col, thickness );
	draw->AddLine( ImVec2( x1, y0 ), ImVec2( x1, y1 ), col, thickness );
	draw->AddLine( ImVec2( x1, y1 ), ImVec2( x0, y1 ), col, thickness );
	draw->AddLine( ImVec2( x0, y1 ), ImVec2( x0, y0 ), col, thickness );
	draw->PopClipRect();
}

static void CL_ImGuiAddEllipsizedText( ImDrawList *draw, ImVec2 pos, ImVec2 clipMin, ImVec2 clipMax, ImU32 col, const char *text, bool shadow ) {
	char tmp[128];
	int len;
	float maxW;
	if ( !text || !text[0] ) return;
	maxW = clipMax.x - pos.x;
	if ( maxW <= 8.0f ) return;
	Q_strncpyz( tmp, text, sizeof( tmp ) );
	if ( ImGui::CalcTextSize( tmp ).x > maxW ) {
		len = (int)strlen( tmp );
		while ( len > 3 ) {
			tmp[len - 3] = '.';
			tmp[len - 2] = '.';
			tmp[len - 1] = '.';
			tmp[len] = '\0';
			if ( ImGui::CalcTextSize( tmp ).x <= maxW ) break;
			len--;
			tmp[len] = '\0';
		}
	}
	draw->PushClipRect( clipMin, clipMax, true );
	CL_ImGuiAddTextShadow( draw, pos, col, tmp, shadow );
	draw->PopClipRect();
}

static bool CL_ImGuiShouldDrawLiveSplitOverlay( void ) {
	if ( !s_cg_livesplit || !s_cg_livesplit->integer ) return false;
	if ( Cvar_VariableIntegerValue( "ls_type" ) == 1 ) return false;
	if ( !Cvar_VariableIntegerValue( "ls_draw" ) ) return false;
	if ( clc.demoplaying ) return false;
	return true;
}

static int CL_ImGuiLiveSplitFontTierForScale( float uiScale ) {
	int bestTier = 0;
	float desiredPixels = 15.0f * uiScale;
	float bestDiff = fabsf( desiredPixels - s_imguiLiveSplitFontTierPixels[0] );
	for ( int tier = 1; tier < SRGUI_LIVESPLIT_FONT_TIER_COUNT; ++tier ) {
		float diff = fabsf( desiredPixels - s_imguiLiveSplitFontTierPixels[tier] );
		if ( diff < bestDiff ) {
			bestDiff = diff;
			bestTier = tier;
		}
	}
	return bestTier;
}

static ImFont *CL_ImGuiLiveSplitFont( int fontIndex, int tier ) {
	if ( fontIndex < 0 ) fontIndex = 0;
	if ( fontIndex >= SRGUI_LIVESPLIT_FONT_COUNT ) fontIndex = SRGUI_LIVESPLIT_FONT_COUNT - 1;
	if ( tier < 0 ) tier = 0;
	if ( tier >= SRGUI_LIVESPLIT_FONT_TIER_COUNT ) tier = SRGUI_LIVESPLIT_FONT_TIER_COUNT - 1;
	if ( s_imguiLiveSplitFontTiers[fontIndex][tier] ) return s_imguiLiveSplitFontTiers[fontIndex][tier];
	for ( int t = tier - 1; t >= 0; --t ) {
		if ( s_imguiLiveSplitFontTiers[fontIndex][t] ) return s_imguiLiveSplitFontTiers[fontIndex][t];
	}
	for ( int t = tier + 1; t < SRGUI_LIVESPLIT_FONT_TIER_COUNT; ++t ) {
		if ( s_imguiLiveSplitFontTiers[fontIndex][t] ) return s_imguiLiveSplitFontTiers[fontIndex][t];
	}
	if ( s_imguiLiveSplitFonts[fontIndex] ) return s_imguiLiveSplitFonts[fontIndex];
	return s_imguiTimerFont ? s_imguiTimerFont : s_imguiLiveSplitFonts[0];
}

static ImFont *CL_ImGuiLiveSplitBoldFont( int fontIndex, int tier ) {
	if ( fontIndex < 0 ) fontIndex = 0;
	if ( fontIndex >= SRGUI_LIVESPLIT_FONT_COUNT ) fontIndex = SRGUI_LIVESPLIT_FONT_COUNT - 1;
	if ( tier < 0 ) tier = 0;
	if ( tier >= SRGUI_LIVESPLIT_FONT_TIER_COUNT ) tier = SRGUI_LIVESPLIT_FONT_TIER_COUNT - 1;
	if ( s_imguiLiveSplitBoldFontTiers[fontIndex][tier] ) return s_imguiLiveSplitBoldFontTiers[fontIndex][tier];
	for ( int t = tier - 1; t >= 0; --t ) {
		if ( s_imguiLiveSplitBoldFontTiers[fontIndex][t] ) return s_imguiLiveSplitBoldFontTiers[fontIndex][t];
	}
	for ( int t = tier + 1; t < SRGUI_LIVESPLIT_FONT_TIER_COUNT; ++t ) {
		if ( s_imguiLiveSplitBoldFontTiers[fontIndex][t] ) return s_imguiLiveSplitBoldFontTiers[fontIndex][t];
	}
	if ( s_imguiLiveSplitBoldFonts[fontIndex] ) return s_imguiLiveSplitBoldFonts[fontIndex];
	if ( s_imguiLiveSplitFontTiers[fontIndex][tier] ) return s_imguiLiveSplitFontTiers[fontIndex][tier];
	if ( s_imguiLiveSplitFonts[fontIndex] ) return s_imguiLiveSplitFonts[fontIndex];
	return s_imguiLiveSplitFonts[12] ? s_imguiLiveSplitFonts[12] : ( s_imguiLiveSplitFonts[15] ? s_imguiLiveSplitFonts[15] : s_imguiTimerFont );
}

static ImU32 CL_ImGuiRainbowColorU32( float alphaMul ) {
	float r, g, b;
	float hue = fmodf( (float)ImGui::GetTime() * 0.16f, 1.0f );
	float pulse = 0.82f + 0.18f * sinf( (float)ImGui::GetTime() * 4.0f );
	ImGui::ColorConvertHSVtoRGB( hue, 0.62f, 1.0f, r, g, b );
	return IM_COL32( (int)( r * 255.0f ), (int)( g * 255.0f ), (int)( b * 255.0f ), (int)( Com_Clamp( 0.0f, 1.0f, alphaMul * pulse ) * 255.0f ) );
}

static void CL_ImGuiDrawLiveSplitOverlay( void ) {
	lsWndState_t st;
	ImDrawList *draw;
	ImFont *overlayFont, *boldFont;
	float sx, sy, uiScale, fontScale, x, y, w, rowH, panelH, contentX, contentR, textY, alphaMul, rounding, pad, nameR;
	float bestR, deltaR, timeR, statusW, nameFrac, bestFrac, deltaFrac, timerCardH, headerY0, headerY1, splitsY0, splitsY1;
	float timerSize, stageSize, infoSize, rgtSize, sideGap, timerSplit, splitNameSize, splitBestDeltaSize, splitDeltaSize, splitTimeSize, splitMaxSize;
	float timerMainScale, timerMainLineH, timerStageLineH, timerInfoLineH, timerLowerH, timerTopPad, timerLineGap, timerBottomPad;
	int i, scrollStart, scrollEnd, lastRow, cap, visibleRows, totalRows, statRows, selectedFont, fontTier;
	int style, currentRow;
	bool pinLast, showTitle, showHeader, showStats, showSeg, showRgt, showPb, showBest, showTimer, showSeps, showDeltas, showBestDeltas, showAtt, show100, showPrev, showGhostSeg, showBestSegments, showBorder, showHeaderBg, showStatus, showGradient, showCurrentBg, shadow, editMode, rightAligned;
	bool showSob, showPossibleSave, showBestPossible, showGoldRainbow, titleBold, attemptsBold, statusBold, headerBold, timerBold, stageBold, infoBold, splitBold, prevLabelBold, prevValueBold, ghostBold, statsBold, rgtBold;
	bool splitNameBold, splitBestBold, splitDeltaBold, splitTimeBold, statSobLabelBold, statSobValueBold, statPossibleLabelBold, statPossibleValueBold, statBestLabelBold, statBestValueBold;
	float borderThickness;
	const char *curPbSeg, *curBestSeg, *curCompareLabel;
	ImU32 colBg, colBg2, colBorder, colHeader, colTimer, colText, colMap, colCurrent, colCompleted, colFuture, colAhead, colBehind, colGold, colDim, colSeg, colPaused, colSep, colHl, colLabel, colPanelTop, colStatusLive, colStatusReady, colStatusDone;
	ImU32 colStatusText, colPbValue, colBestValue, colPbLabel, colBestLabel, colPrevLabel, colPrevAhead, colPrevBehind, colPrevGold, colGhostLabel, colGhostTime, colStatLabel, colStatSob, colStatPossibleLabel, colStatPossible, colStatPossibleZero, colStatPossibleMissing, colStatBest, colRgt, colEmpty;
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;

	memcpy( &st, (const void *)&lswnd_state, sizeof( st ) );
	if ( !st.gameName[0] && st.numRows <= 0 ) return;

	style = 0;
	showTitle = Cvar_VariableIntegerValue( "ls_imgui_show_title" ) != 0;
	showHeader = Cvar_VariableIntegerValue( "ls_showheader" ) != 0;
	showStats = Cvar_VariableIntegerValue( "ls_showstats" ) != 0;
	showSeg = Cvar_VariableIntegerValue( "ls_showseg" ) != 0;
	showRgt = Cvar_VariableIntegerValue( "ls_showrgt" ) != 0;
	showPb = Cvar_VariableIntegerValue( "ls_showpb" ) != 0;
	showBest = Cvar_VariableIntegerValue( "ls_showbest" ) != 0;
	showTimer = Cvar_VariableIntegerValue( "ls_showtimer" ) != 0;
	showSeps = Cvar_VariableIntegerValue( "ls_showseps" ) != 0;
	showDeltas = Cvar_VariableIntegerValue( "ls_showdeltas" ) != 0;
	showBestDeltas = Cvar_VariableIntegerValue( "ls_showbestdeltas" ) != 0;
	showAtt = Cvar_VariableIntegerValue( "ls_showatt" ) != 0;
	show100 = Cvar_VariableIntegerValue( "ls_100pct" ) != 0;
	showPrev = Cvar_VariableIntegerValue( "ls_imgui_show_prevseg" ) != 0;
	showGhostSeg = Cvar_VariableIntegerValue( "ls_imgui_show_ghostseg" ) != 0;
	showBestSegments = Cvar_VariableIntegerValue( "ls_imgui_show_bestsegments" ) != 0;
	showBorder = Cvar_VariableIntegerValue( "ls_imgui_show_border" ) != 0;
	showHeaderBg = Cvar_VariableIntegerValue( "ls_imgui_header_bg" ) != 0;
	showStatus = Cvar_VariableIntegerValue( "ls_imgui_show_status" ) != 0;
	showGradient = Cvar_VariableIntegerValue( "ls_imgui_gradient" ) != 0;
	showCurrentBg = Cvar_VariableIntegerValue( "ls_imgui_current_bg" ) != 0;
	showSob = Cvar_VariableIntegerValue( "ls_imgui_show_sob" ) != 0;
	showPossibleSave = Cvar_VariableIntegerValue( "ls_imgui_show_possible_save" ) != 0;
	showBestPossible = Cvar_VariableIntegerValue( "ls_imgui_show_best_possible" ) != 0;
	showGoldRainbow = Cvar_VariableIntegerValue( "ls_imgui_gold_rainbow" ) != 0;
	titleBold = Cvar_VariableIntegerValue( "ls_imgui_bold_title" ) != 0;
	attemptsBold = Cvar_VariableIntegerValue( "ls_imgui_bold_attempts" ) != 0;
	statusBold = Cvar_VariableIntegerValue( "ls_imgui_bold_status" ) != 0;
	headerBold = Cvar_VariableIntegerValue( "ls_imgui_bold_header" ) != 0;
	timerBold = Cvar_VariableIntegerValue( "ls_imgui_bold_timer" ) != 0;
	stageBold = Cvar_VariableIntegerValue( "ls_imgui_bold_stage" ) != 0;
	infoBold = Cvar_VariableIntegerValue( "ls_imgui_bold_info" ) != 0;
	splitBold = Cvar_VariableIntegerValue( "ls_imgui_bold_splits" ) != 0;
	prevLabelBold = Cvar_VariableIntegerValue( "ls_imgui_bold_prev" ) != 0 || Cvar_VariableIntegerValue( "ls_imgui_bold_prev_label" ) != 0;
	prevValueBold = Cvar_VariableIntegerValue( "ls_imgui_bold_prev" ) != 0 || Cvar_VariableIntegerValue( "ls_imgui_bold_prev_value" ) != 0;
	ghostBold = Cvar_VariableIntegerValue( "ls_imgui_bold_ghost" ) != 0;
	statsBold = Cvar_VariableIntegerValue( "ls_imgui_bold_stats" ) != 0;
	rgtBold = Cvar_VariableIntegerValue( "ls_imgui_bold_rgt" ) != 0;
	splitNameBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_name" ) != 0;
	splitBestBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_best" ) != 0;
	splitDeltaBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_delta" ) != 0;
	splitTimeBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_time" ) != 0;
	statSobLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_sob_label" ) != 0;
	statSobValueBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_sob_value" ) != 0;
	statPossibleLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_possible_label" ) != 0;
	statPossibleValueBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_possible_value" ) != 0;
	statBestLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_best_label" ) != 0;
	statBestValueBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_best_value" ) != 0;
	shadow = Cvar_VariableIntegerValue( "ls_text_shadow" ) != 0;
	editMode = s_imguiOpen && Cvar_VariableIntegerValue( "ui_speedrun_layout_edit" ) != 0;
	rightAligned = Cvar_VariableIntegerValue( "ls_align" ) == 1;
	if ( !editMode ) {
		flags |= ImGuiWindowFlags_NoInputs;
	}
	if ( !ImGui::GetIO().MouseDown[0] ) {
		s_liveSplitEditActive = false;
	}
	currentRow = st.curRow >= 0 && st.curRow < st.numRows ? st.curRow : -1;
	if ( currentRow < 0 ) {
		for ( i = 0; i < st.numRows; ++i ) {
			if ( st.rows[i].state == 1 ) {
				currentRow = i;
				break;
			}
		}
	}
	if ( currentRow < 0 && st.numRows > 0 ) currentRow = 0;
	curPbSeg = currentRow >= 0 && st.rows[currentRow].pbSegTime[0] ? st.rows[currentRow].pbSegTime : "-----";
	curBestSeg = currentRow >= 0 && st.rows[currentRow].bestSeg[0] ? st.rows[currentRow].bestSeg : "-----";
	curCompareLabel = "PB";
	if ( !Q_stricmp( st.compareLabel, "Best Segments" ) ) curCompareLabel = "BEST";
	else if ( !Q_stricmp( st.compareLabel, "Average Segments" ) ) curCompareLabel = "AVG";
	statRows = showStats ? ( ( showSob ? 1 : 0 ) + ( showBestPossible ? 1 : 0 ) ) : 0;

	sx = cls.glconfig.vidWidth / 640.0f;
	sy = cls.glconfig.vidHeight / 480.0f;
	uiScale = Com_Clamp( 0.65f, 3.0f, Cvar_VariableValue( "ls_scale" ) * ( sy * 0.78f ) );
	if ( style == 2 ) uiScale *= 0.88f;
	if ( style == 4 ) uiScale *= 1.08f;
	w = Com_Clamp( 80.0f, 420.0f, Cvar_VariableValue( "ls_w" ) ) * sx * Cvar_VariableValue( "ls_scale" );
	x = Cvar_VariableValue( "ls_x" );
	x *= sx;
	y = Cvar_VariableValue( "ls_y" ) * sy;
	splitNameSize = Com_Clamp( 0.65f, 1.60f, Cvar_VariableValue( "ls_imgui_name_size" ) );
	splitBestDeltaSize = Com_Clamp( 0.65f, 1.60f, Cvar_VariableValue( "ls_imgui_bestdelta_size" ) );
	splitDeltaSize = Com_Clamp( 0.65f, 1.60f, Cvar_VariableValue( "ls_imgui_delta_size" ) );
	splitTimeSize = Com_Clamp( 0.65f, 1.60f, Cvar_VariableValue( "ls_imgui_time_size" ) );
	timerSize = Com_Clamp( 0.75f, 2.00f, Cvar_VariableValue( "ls_imgui_timer_size" ) );
	stageSize = Com_Clamp( 0.55f, 1.60f, Cvar_VariableValue( "ls_imgui_stage_size" ) );
	infoSize = Com_Clamp( 0.55f, 1.25f, Cvar_VariableValue( "ls_imgui_info_size" ) );
	sideGap = Com_Clamp( 2.0f, 24.0f, Cvar_VariableValue( "ls_imgui_timer_gap" ) ) * uiScale;
	timerSplit = Com_Clamp( 0.40f, 0.76f, Cvar_VariableValue( "ls_imgui_timer_split" ) );
	splitMaxSize = splitNameSize > splitBestDeltaSize ? splitNameSize : splitBestDeltaSize;
	if ( splitDeltaSize > splitMaxSize ) splitMaxSize = splitDeltaSize;
	if ( splitTimeSize > splitMaxSize ) splitMaxSize = splitTimeSize;
	rowH = ( style == 2 ? 15.0f : 17.0f ) * uiScale * Com_Clamp( 0.75f, 1.45f, Cvar_VariableValue( "ls_imgui_row_size" ) ) * splitMaxSize;
	timerMainScale = showTimer ? ( showSeg ? timerSize * 1.30f : timerSize * 1.34f ) : 0.0f;
	timerMainLineH = timerMainScale > 0.0f ? 13.0f * uiScale * timerMainScale : 0.0f;
	timerStageLineH = showSeg ? 13.0f * uiScale * ( showTimer ? stageSize : timerSize * 1.08f ) : 0.0f;
	timerInfoLineH = ( showSeg && ( showPb || showBest ) ) ? 13.0f * uiScale * infoSize : 0.0f;
	timerLowerH = timerStageLineH > ( timerInfoLineH * 2.0f + 2.0f * uiScale ) ? timerStageLineH : ( timerInfoLineH * 2.0f + 2.0f * uiScale );
	timerTopPad = 4.0f * uiScale;
	timerLineGap = 3.0f * uiScale;
	timerBottomPad = 5.0f * uiScale;
	rounding = 0.0f;
	pad = Com_Clamp( 3.0f, 14.0f, Cvar_VariableValue( "ls_imgui_padding" ) ) * uiScale;
	cap = Cvar_VariableIntegerValue( "ls_maxrows" );
	if ( cap < 2 ) cap = 6;
	if ( cap > 16 ) cap = 16;

	lastRow = st.numRows - 1;
	pinLast = false;
	if ( st.numRows <= cap ) {
		scrollStart = 0;
		scrollEnd = lastRow;
	} else {
		int scrollSize = cap - 1;
		scrollStart = st.curRow >= 0 ? st.curRow - scrollSize / 2 : 0;
		if ( scrollStart < 0 ) scrollStart = 0;
		scrollEnd = scrollStart + scrollSize - 1;
		if ( scrollEnd >= lastRow - 1 ) {
			scrollEnd = lastRow;
			scrollStart = scrollEnd - scrollSize;
			if ( scrollStart < 0 ) scrollStart = 0;
		} else {
			pinLast = true;
		}
	}
	visibleRows = scrollEnd >= scrollStart ? scrollEnd - scrollStart + 1 : 0;
	totalRows = visibleRows + ( pinLast ? 1 : 0 );

	panelH = pad * 2.0f + 4.0f * uiScale;
	if ( showTitle ) panelH += ( style == 2 ? 26.0f : 34.0f ) * uiScale;
	else panelH += 16.0f * uiScale;
	if ( showHeader ) panelH += 15.0f * uiScale;
	panelH += totalRows * rowH;
	timerCardH = timerTopPad + timerMainLineH + ( showTimer && showSeg ? timerLineGap : 0.0f ) + timerLowerH + timerBottomPad;
	if ( showTimer || showSeg ) panelH += timerCardH;
	if ( showPrev && st.prevSegValue[0] ) panelH += 15.0f * uiScale;
	if ( showStats && showPossibleSave ) panelH += 14.0f * uiScale;
	if ( showGhostSeg && st.ghostSegText[0] ) panelH += 16.0f * uiScale;
	if ( statRows > 0 ) panelH += ( 4.0f + statRows * 14.0f ) * uiScale;
	if ( showBestSegments && st.numBestSegs > 0 ) panelH += 18.0f * uiScale + ( st.numBestSegs < 4 ? st.numBestSegs : 4 ) * rowH;
	rgtSize = Com_Clamp( 0.65f, 1.60f, Cvar_VariableValue( "ls_imgui_rgt_size" ) );
	if ( showRgt ) panelH += 13.5f * uiScale * rgtSize;
	if ( show100 ) panelH += 32.0f * uiScale;

	alphaMul = Cvar_VariableValue( "ls_opacity" );
	if ( s_imguiOpen ) alphaMul *= Cvar_VariableValue( "ls_opacity_ui" );
	alphaMul = Com_Clamp( 0.05f, 1.0f, alphaMul );
	colBg = CL_ImGuiColorU32( "ls_clr_bg", ImVec4( 0.04f, 0.04f, 0.06f, Cvar_VariableValue( "ls_bgalpha" ) ), alphaMul );
	colBg2 = CL_ImGuiColorU32( "ls_clr_bg2", ImVec4( 0.02f, 0.03f, 0.02f, Cvar_VariableValue( "ls_bgalpha" ) * 0.72f ), alphaMul );
	colBorder = CL_ImGuiColorU32( "ls_clr_border", ImVec4( 0.20f, 0.35f, 0.15f, 0.30f ), alphaMul );
	colHeader = CL_ImGuiColorU32( "ls_clr_title", ImVec4( 0.35f, 0.75f, 0.20f, 1.00f ), alphaMul );
	colTimer = CL_ImGuiColorU32( "ls_clr_timer", ImVec4( 0.70f, 0.95f, 0.50f, 1.00f ), alphaMul );
	colText = CL_ImGuiColorU32( "ls_clr_text", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colMap = CL_ImGuiColorU32( "ls_clr_category", ImVec4( 0.55f, 0.62f, 0.50f, 0.85f ), alphaMul );
	colCurrent = CL_ImGuiColorU32( "ls_clr_current", ImVec4( 1.00f, 1.00f, 0.60f, 1.00f ), alphaMul );
	colCompleted = CL_ImGuiColorU32( "ls_clr_completed", ImVec4( 0.72f, 0.72f, 0.72f, 0.80f ), alphaMul );
	colFuture = CL_ImGuiColorU32( "ls_clr_future", ImVec4( 0.36f, 0.36f, 0.40f, 0.48f ), alphaMul );
	colAhead = CL_ImGuiColorU32( "ls_clr_ahead", ImVec4( 0.25f, 0.85f, 0.25f, 1.00f ), alphaMul );
	colBehind = CL_ImGuiColorU32( "ls_clr_behind", ImVec4( 0.85f, 0.25f, 0.25f, 1.00f ), alphaMul );
	colGold = CL_ImGuiColorU32( "ls_clr_gold", ImVec4( 1.00f, 0.85f, 0.20f, 1.00f ), alphaMul );
	colDim = CL_ImGuiColorU32( "ls_clr_dim", ImVec4( 0.48f, 0.48f, 0.50f, 0.52f ), alphaMul );
	colSeg = CL_ImGuiColorU32( "ls_clr_stage_timer", ImVec4( 0.62f, 0.65f, 0.62f, 0.82f ), alphaMul );
	colPaused = CL_ImGuiColorU32( "ls_clr_status_pause", ImVec4( 0.90f, 0.70f, 0.20f, 1.00f ), alphaMul );
	colSep = CL_ImGuiColorU32( "ls_clr_sep", ImVec4( 0.22f, 0.38f, 0.12f, 0.18f ), alphaMul );
	colHl = CL_ImGuiColorU32( "ls_clr_highlight", ImVec4( 0.10f, 0.20f, 0.06f, 0.32f ), alphaMul );
	colLabel = CL_ImGuiColorU32( "ls_clr_column_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colPanelTop = CL_ImGuiColorU32( "ls_clr_header_bg", ImVec4( 0.16f, 0.32f, 0.10f, 0.22f ), alphaMul );
	colStatusLive = CL_ImGuiColorU32( "ls_clr_status_live", ImVec4( 0.35f, 0.75f, 0.20f, 1.00f ), alphaMul );
	colStatusReady = CL_ImGuiColorU32( "ls_clr_status_ready", ImVec4( 0.32f, 0.36f, 0.30f, 0.90f ), alphaMul );
	colStatusDone = CL_ImGuiColorU32( "ls_clr_status_done", ImVec4( 0.25f, 0.85f, 0.25f, 1.00f ), alphaMul );
	colStatusText = CL_ImGuiColorU32( "ls_clr_status_text", ImVec4( 0.03f, 0.05f, 0.03f, 0.92f ), alphaMul );
	colPbValue = CL_ImGuiColorU32( "ls_clr_pb_value", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colBestValue = CL_ImGuiColorU32( "ls_clr_best_value", ImVec4( 1.00f, 0.85f, 0.20f, 1.00f ), alphaMul );
	colPbLabel = CL_ImGuiColorU32( "ls_clr_pb_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colBestLabel = CL_ImGuiColorU32( "ls_clr_best_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colPrevLabel = CL_ImGuiColorU32( "ls_clr_prev_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colPrevAhead = CL_ImGuiColorU32( "ls_clr_prev_ahead", ImVec4( 0.25f, 0.85f, 0.25f, 1.00f ), alphaMul );
	colPrevBehind = CL_ImGuiColorU32( "ls_clr_prev_behind", ImVec4( 0.85f, 0.25f, 0.25f, 1.00f ), alphaMul );
	colPrevGold = CL_ImGuiColorU32( "ls_clr_prev_gold", ImVec4( 1.00f, 0.85f, 0.20f, 1.00f ), alphaMul );
	colGhostLabel = CL_ImGuiColorU32( "ls_clr_ghost_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colGhostTime = CL_ImGuiColorU32( "ls_clr_ghost_time", ImVec4( 0.62f, 0.65f, 0.62f, 0.82f ), alphaMul );
	colStatLabel = CL_ImGuiColorU32( "ls_clr_stat_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colStatSob = CL_ImGuiColorU32( "ls_clr_stat_sob", ImVec4( 1.00f, 0.85f, 0.20f, 1.00f ), alphaMul );
	colStatPossibleLabel = CL_ImGuiColorU32( "ls_clr_stat_possible_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colStatPossible = CL_ImGuiColorU32( "ls_clr_stat_possible_save", ImVec4( 0.25f, 0.85f, 0.25f, 1.00f ), alphaMul );
	colStatPossibleZero = CL_ImGuiColorU32( "ls_clr_stat_possible_zero", ImVec4( 0.48f, 0.48f, 0.50f, 0.62f ), alphaMul );
	colStatPossibleMissing = CL_ImGuiColorU32( "ls_clr_stat_possible_missing", ImVec4( 0.32f, 0.36f, 0.30f, 0.70f ), alphaMul );
	colStatBest = CL_ImGuiColorU32( "ls_clr_stat_best_possible", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colRgt = CL_ImGuiColorU32( "ls_clr_rgt", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colEmpty = CL_ImGuiColorU32( "ls_clr_empty", ImVec4( 0.48f, 0.48f, 0.50f, 0.52f ), alphaMul );
	if ( style == 0 ) {
		colBg = CL_ImGuiColorU32( "ls_clr_bg", ImVec4( 0.020f, 0.026f, 0.024f, Cvar_VariableValue( "ls_bgalpha" ) ), alphaMul );
		colBg2 = CL_ImGuiColorU32( "ls_clr_bg2", ImVec4( 0.035f, 0.044f, 0.040f, Cvar_VariableValue( "ls_bgalpha" ) * 0.84f ), alphaMul );
		colBorder = CL_ImGuiColorU32( "ls_clr_border", ImVec4( 0.28f, 0.42f, 0.22f, 0.48f ), alphaMul );
		colPanelTop = CL_ImGuiColorU32( "ls_clr_header_bg", ImVec4( 0.055f, 0.095f, 0.063f, 0.74f ), alphaMul );
	}
	if ( style == 1 ) {
		rounding = 0.0f;
		colBg2 = colBg;
	} else if ( style == 3 ) {
		colBg = IM_COL32( 0, 0, 0, (int)( 80.0f * alphaMul ) );
		colBg2 = colBg;
		colBorder = IM_COL32( 0, 0, 0, 0 );
		colPanelTop = IM_COL32( 0, 0, 0, 0 );
	}

	ImGui::SetNextWindowPos( ImVec2( x, y ), ImGuiCond_Always );
	ImGui::SetNextWindowSize( ImVec2( w, panelH ), ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, rounding );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::Begin( "LiveSplit ImGui Overlay", NULL, flags );
	selectedFont = (int)Com_Clamp( 0.0f, (float)( SRGUI_LIVESPLIT_FONT_COUNT - 1 ), (float)Cvar_VariableIntegerValue( "ls_imgui_font" ) );
	fontTier = CL_ImGuiLiveSplitFontTierForScale( uiScale );
	while ( fontTier > 0 && !s_imguiLiveSplitFontTiers[selectedFont][fontTier] ) {
		fontTier--;
	}
	fontScale = uiScale * ( 15.0f / s_imguiLiveSplitFontTierPixels[fontTier] );
	overlayFont = CL_ImGuiLiveSplitFont( selectedFont, fontTier );
	if ( !overlayFont ) overlayFont = s_imguiTimerFont;
	boldFont = CL_ImGuiLiveSplitBoldFont( selectedFont, fontTier );
	if ( overlayFont ) {
		ImGui::PushFont( overlayFont );
	}
	ImGui::SetWindowFontScale( fontScale );
	draw = ImGui::GetWindowDrawList();
	if ( editMode ) {
		ImGui::SetCursorScreenPos( ImVec2( x, y ) );
		ImGui::InvisibleButton( "ls_imgui_drag", ImVec2( w, ( showTitle ? 32.0f : 20.0f ) * uiScale ) );
		if ( ImGui::IsItemActive() ) {
			s_liveSplitEditActive = true;
		}
		if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0 ) ) {
			ImVec2 d = ImGui::GetIO().MouseDelta;
			float nextX = Cvar_VariableValue( "ls_x" ) + d.x / sx;
			float nextY = Cvar_VariableValue( "ls_y" ) + d.y / sy;
			Cvar_SetValue( "ls_x", Com_Clamp( 0.0f, 640.0f, nextX ) );
			Cvar_SetValue( "ls_y", Com_Clamp( 0.0f, 480.0f, nextY ) );
		}
	}
	contentX = x + pad;
	contentR = x + w - pad;
	textY = y + pad;
	headerY0 = textY;
	headerY1 = textY;
	splitsY0 = textY;
	splitsY1 = textY;
	nameFrac = Com_Clamp( 0.22f, 0.78f, Cvar_VariableValue( "ls_imgui_col_name" ) );
	bestFrac = Com_Clamp( nameFrac + 0.08f, 0.88f, Cvar_VariableValue( "ls_imgui_col_best" ) );
	deltaFrac = Com_Clamp( bestFrac + 0.06f, 0.94f, Cvar_VariableValue( "ls_imgui_col_delta" ) );
	nameR = x + w * nameFrac;
	bestR = x + w * bestFrac;
	deltaR = x + w * deltaFrac;
	timeR = contentR;
	nameR -= 5.0f * uiScale;

	borderThickness = Com_Clamp( 0.5f, 4.0f, Cvar_VariableValue( "ls_imgui_border_size" ) );
	if ( showGradient ) CL_ImGuiAddRectFilledAngledGradient( draw, ImVec2( x, y ), ImVec2( x + w, y + panelH ), colBg, colBg2, Cvar_VariableValue( "ls_imgui_gradient_angle" ) );
	else draw->AddRectFilled( ImVec2( x, y ), ImVec2( x + w, y + panelH ), colBg );
	if ( showHeaderBg ) {
		draw->AddRectFilled( ImVec2( x + 1.0f, y + 1.0f ), ImVec2( x + w - 1.0f, y + ( showTitle ? 30.0f : 18.0f ) * uiScale ), colPanelTop );
	}
	if ( showBorder ) {
		CL_ImGuiAddRectLines( draw, x, y, w, panelH, colBorder, borderThickness );
	}

	if ( showTitle ) {
		statusW = 0.0f;
		if ( showStatus ) {
			const char *statusText = st.finished ? "DONE" : ( st.paused ? "PAUSE" : ( st.active ? "LIVE" : "READY" ) );
			ImU32 statusCol = st.finished ? colStatusDone : ( st.paused ? colPaused : ( st.active ? colStatusLive : colStatusReady ) );
			statusW = ImGui::CalcTextSize( statusText ).x + 12.0f * uiScale;
			draw->AddRectFilled( ImVec2( contentR - statusW, textY - 1.0f * uiScale ), ImVec2( contentR, textY + 13.0f * uiScale ), statusCol, 7.0f * uiScale );
			if ( statusBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextCentered( draw, contentR - statusW * 0.5f, textY, colStatusText, statusText, false );
			if ( statusBold && boldFont ) ImGui::PopFont();
		}
		if ( titleBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddClippedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( contentR - ( showStatus ? statusW + 4.0f * uiScale : 0.0f ), textY + 15.0f * uiScale ), colHeader, st.gameName[0] ? st.gameName : "RtCW Speedrun", shadow );
		textY += 14.0f * uiScale;
		CL_ImGuiAddClippedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( contentR, textY + 14.0f * uiScale ), colMap, st.categoryText[0] ? st.categoryText : "LiveSplit", shadow );
		if ( titleBold && boldFont ) ImGui::PopFont();
		if ( showAtt ) {
			char attempts[32];
			Com_sprintf( attempts, sizeof( attempts ), "%d/%d", st.completions, st.attempts );
			if ( attemptsBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, contentR, textY, colMap, attempts, shadow );
			if ( attemptsBold && boldFont ) ImGui::PopFont();
		}
		textY += 17.0f * uiScale;
	} else {
		if ( showStatus ) {
			const char *statusText = st.finished ? "DONE" : ( st.paused ? "PAUSE" : ( st.active ? "LIVE" : "READY" ) );
			ImU32 statusCol = st.finished ? colStatusDone : ( st.paused ? colPaused : ( st.active ? colStatusLive : colStatusReady ) );
			statusW = ImGui::CalcTextSize( statusText ).x + 8.0f * uiScale;
			if ( statusBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), statusCol, statusText, shadow );
			if ( statusBold && boldFont ) ImGui::PopFont();
			if ( titleBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddClippedText( draw, ImVec2( contentX + statusW, textY ), ImVec2( contentX + statusW, textY - 2.0f * uiScale ), ImVec2( contentR, textY + 14.0f * uiScale ), colHeader, st.categoryText[0] ? st.categoryText : "LiveSplit", shadow );
			if ( titleBold && boldFont ) ImGui::PopFont();
		} else {
			if ( titleBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddClippedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( contentR, textY + 14.0f * uiScale ), colHeader, st.categoryText[0] ? st.categoryText : "LiveSplit", shadow );
			if ( titleBold && boldFont ) ImGui::PopFont();
		}
		if ( showAtt ) {
			char attempts[32];
			Com_sprintf( attempts, sizeof( attempts ), "%d/%d", st.completions, st.attempts );
			if ( attemptsBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, contentR, textY, colMap, attempts, shadow );
			if ( attemptsBold && boldFont ) ImGui::PopFont();
		}
		textY += 15.0f * uiScale;
	}
	headerY1 = textY;
	if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
	textY += 4.0f * uiScale;
	splitsY0 = textY;

	if ( showHeader ) {
		if ( headerBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, bestR, textY, colLabel, showBestDeltas ? "Best +/-" : "", shadow );
		CL_ImGuiAddTextRight( draw, deltaR, textY, colLabel, showDeltas ? "+/-" : "", shadow );
		CL_ImGuiAddTextRight( draw, timeR, textY, colLabel, "Time", shadow );
		if ( headerBold && boldFont ) ImGui::PopFont();
		textY += 13.0f * uiScale;
		if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
		textY += 3.0f * uiScale;
	}

	for ( i = scrollStart; i <= scrollEnd && i < st.numRows; ++i ) {
		ImU32 rowCol = st.rows[i].state == 1 ? colCurrent : ( st.rows[i].state == 2 ? colCompleted : colFuture );
		const char *name = ( w < 190.0f && st.rows[i].shortName[0] ) ? st.rows[i].shortName : st.rows[i].name;
		if ( showCurrentBg && st.rows[i].state == 1 ) {
			draw->AddRectFilled( ImVec2( x + 4.0f, textY - 1.0f ), ImVec2( x + w - 4.0f, textY + rowH - 2.0f ), colHl );
		}
		ImGui::SetWindowFontScale( fontScale * splitNameSize );
		if ( splitNameBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddEllipsizedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( nameR, textY + rowH ), rowCol, name, shadow );
		if ( splitNameBold && boldFont ) ImGui::PopFont();
		if ( showBestDeltas && st.rows[i].deltaBest[0] ) {
			ImU32 bestDeltaCol = st.rows[i].isGold ? ( showGoldRainbow ? CL_ImGuiRainbowColorU32( alphaMul ) : colGold ) : ( st.rows[i].deltaBest[0] == '+' ? colBehind : colAhead );
			ImGui::SetWindowFontScale( fontScale * splitBestDeltaSize );
			if ( splitBestBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, bestR, textY, bestDeltaCol, st.rows[i].deltaBest, shadow );
			if ( splitBestBold && boldFont ) ImGui::PopFont();
		}
		if ( showDeltas && st.rows[i].delta[0] ) {
			ImU32 deltaCol = st.rows[i].isGold ? ( showGoldRainbow ? CL_ImGuiRainbowColorU32( alphaMul ) : colGold ) : ( st.rows[i].isBehind ? colBehind : colAhead );
			ImGui::SetWindowFontScale( fontScale * splitDeltaSize );
			if ( splitDeltaBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, deltaR, textY, deltaCol, st.rows[i].delta, shadow );
			if ( splitDeltaBold && boldFont ) ImGui::PopFont();
		}
		if ( st.rows[i].splitTime[0] ) {
			ImGui::SetWindowFontScale( fontScale * splitTimeSize );
			if ( splitTimeBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, st.rows[i].state == 1 ? colCurrent : colText, st.rows[i].splitTime, shadow );
			if ( splitTimeBold && boldFont ) ImGui::PopFont();
		} else if ( st.rows[i].pbSplitTime[0] && st.rows[i].state != 2 ) {
			ImGui::SetWindowFontScale( fontScale * splitTimeSize );
			if ( splitTimeBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, colDim, st.rows[i].pbSplitTime, shadow );
			if ( splitTimeBold && boldFont ) ImGui::PopFont();
		}
		ImGui::SetWindowFontScale( fontScale );
		textY += rowH;
	}
	if ( pinLast && lastRow >= 0 ) {
		if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
		textY += 3.0f * uiScale;
		i = lastRow;
		ImGui::SetWindowFontScale( fontScale * splitNameSize );
		if ( splitNameBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddEllipsizedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( nameR, textY + rowH ), st.rows[i].state == 2 ? colCompleted : colFuture, st.rows[i].name, shadow );
		if ( splitNameBold && boldFont ) ImGui::PopFont();
		ImGui::SetWindowFontScale( fontScale * splitTimeSize );
		if ( splitTimeBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, timeR, textY, st.rows[i].splitTime[0] ? colText : colDim, st.rows[i].splitTime[0] ? st.rows[i].splitTime : st.rows[i].pbSplitTime, shadow );
		if ( splitTimeBold && boldFont ) ImGui::PopFont();
		ImGui::SetWindowFontScale( fontScale );
		textY += rowH;
	}
	splitsY1 = textY;
	if ( editMode ) {
		float splitH = splitsY1 > splitsY0 ? splitsY1 - splitsY0 : 48.0f * uiScale;
		draw->AddLine( ImVec2( nameR + 5.0f * uiScale, splitsY0 ), ImVec2( nameR + 5.0f * uiScale, splitsY1 ), CL_ImGuiGuiAccentU32( alphaMul * 0.55f ), 1.2f );
		draw->AddLine( ImVec2( bestR, splitsY0 ), ImVec2( bestR, splitsY1 ), CL_ImGuiGuiAccentU32( alphaMul * 0.75f ), 1.5f );
		draw->AddLine( ImVec2( deltaR, splitsY0 ), ImVec2( deltaR, splitsY1 ), CL_ImGuiGuiAccentAltU32( alphaMul * 0.75f ), 1.5f );
		ImGui::SetCursorScreenPos( ImVec2( nameR + uiScale, splitsY0 ) );
		ImGui::InvisibleButton( "ls_col_name_resize", ImVec2( 8.0f * uiScale, splitH ) );
		if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0 ) ) {
			float next = nameFrac + ImGui::GetIO().MouseDelta.x / w;
			Cvar_SetValue( "ls_imgui_col_name", Com_Clamp( 0.22f, bestFrac - 0.08f, next ) );
		}
		ImGui::SetCursorScreenPos( ImVec2( bestR - 4.0f * uiScale, splitsY0 ) );
		ImGui::InvisibleButton( "ls_col_best_resize", ImVec2( 8.0f * uiScale, splitH ) );
		if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0 ) ) {
			float next = bestFrac + ImGui::GetIO().MouseDelta.x / w;
			Cvar_SetValue( "ls_imgui_col_best", Com_Clamp( 0.48f, deltaFrac - 0.06f, next ) );
		}
		ImGui::SetCursorScreenPos( ImVec2( deltaR - 4.0f * uiScale, splitsY0 ) );
		ImGui::InvisibleButton( "ls_col_delta_resize", ImVec2( 8.0f * uiScale, splitH ) );
		if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0 ) ) {
			float next = deltaFrac + ImGui::GetIO().MouseDelta.x / w;
			Cvar_SetValue( "ls_imgui_col_delta", Com_Clamp( bestFrac + 0.06f, 0.94f, next ) );
		}
	}

	if ( showSeps && !( showTimer || showSeg ) ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
	textY += 4.0f * uiScale;
	if ( showTimer || showSeg ) {
		float cardY = textY;
		float leftBlock = contentX;
		float rightBlock = contentR;
		float midX = x + w * timerSplit;
		float timerLeft = rightAligned ? midX + sideGap : leftBlock;
		float timerRight = rightAligned ? rightBlock : midX - sideGap;
		float mainY = timerTopPad;
		float lowerY = showTimer ? ( mainY + timerMainLineH + timerLineGap ) : timerTopPad;
		draw->AddLine( ImVec2( contentX, cardY ), ImVec2( contentR, cardY ), colSep );
		if ( showTimer ) {
			if ( timerBold && boldFont ) ImGui::PushFont( boldFont );
			ImGui::SetWindowFontScale( fontScale * timerMainScale );
			if ( rightAligned ) CL_ImGuiAddTextRight( draw, rightBlock, cardY + mainY, st.finished ? colAhead : ( st.timerBehind ? colBehind : colTimer ), st.timerText, shadow );
			else CL_ImGuiAddTextShadow( draw, ImVec2( leftBlock, cardY + mainY ), st.finished ? colAhead : ( st.timerBehind ? colBehind : colTimer ), st.timerText, shadow );
			ImGui::SetWindowFontScale( fontScale );
			if ( timerBold && boldFont ) ImGui::PopFont();
		}
		if ( showSeg ) {
			float segScale = showTimer ? stageSize : timerSize * 1.08f;
			float segY = showTimer ? lowerY : 16.0f * uiScale;
			if ( stageBold && boldFont ) ImGui::PushFont( boldFont );
			ImGui::SetWindowFontScale( fontScale * segScale );
			if ( rightAligned ) CL_ImGuiAddTextRight( draw, timerRight, cardY + segY, colSeg, st.segTimerText[0] ? st.segTimerText : "0:00.00", shadow );
			else CL_ImGuiAddTextShadow( draw, ImVec2( timerLeft, cardY + segY ), colSeg, st.segTimerText[0] ? st.segTimerText : "0:00.00", shadow );
			ImGui::SetWindowFontScale( fontScale );
			if ( stageBold && boldFont ) ImGui::PopFont();
			if ( showPb || showBest ) {
				float pbY = segY;
				float bestY = segY + timerInfoLineH + 1.0f * uiScale;
				float edge = rightAligned ? leftBlock : rightBlock;
				ImGui::SetWindowFontScale( fontScale * infoSize );
				if ( infoBold && boldFont ) ImGui::PushFont( boldFont );
				if ( showPb ) {
					if ( rightAligned ) {
						CL_ImGuiAddTextShadow( draw, ImVec2( edge, cardY + pbY ), colPbLabel, curCompareLabel, shadow );
						CL_ImGuiAddTextShadow( draw, ImVec2( edge + ImGui::CalcTextSize( curCompareLabel ).x + 3.0f * uiScale, cardY + pbY ), colPbValue, curPbSeg, shadow );
					} else {
						CL_ImGuiAddTextRight( draw, edge, cardY + pbY, colPbValue, curPbSeg, shadow );
						CL_ImGuiAddTextRight( draw, edge - ImGui::CalcTextSize( curPbSeg ).x - 3.0f * uiScale, cardY + pbY, colPbLabel, curCompareLabel, shadow );
					}
				}
				if ( showBest ) {
					if ( rightAligned ) {
						CL_ImGuiAddTextShadow( draw, ImVec2( edge, cardY + bestY ), colBestLabel, "BEST", shadow );
						CL_ImGuiAddTextShadow( draw, ImVec2( edge + ImGui::CalcTextSize( "BEST" ).x + 3.0f * uiScale, cardY + bestY ), colBestValue, curBestSeg, shadow );
					} else {
						CL_ImGuiAddTextRight( draw, edge, cardY + bestY, colBestValue, curBestSeg, shadow );
						CL_ImGuiAddTextRight( draw, edge - ImGui::CalcTextSize( curBestSeg ).x - 3.0f * uiScale, cardY + bestY, colBestLabel, "BEST", shadow );
					}
				}
				if ( infoBold && boldFont ) ImGui::PopFont();
				ImGui::SetWindowFontScale( fontScale );
			}
		}
		ImGui::SetWindowFontScale( fontScale );
		textY += timerCardH;
	}
	if ( showPrev && st.prevSegValue[0] ) {
		if ( prevLabelBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colPrevLabel, st.prevSegLabel[0] ? st.prevSegLabel : "Previous", shadow );
		if ( prevLabelBold && boldFont ) ImGui::PopFont();
		if ( prevValueBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, timeR, textY, st.prevSegGold ? colPrevGold : ( st.prevSegBehind ? colPrevBehind : colPrevAhead ), st.prevSegValue, shadow );
		if ( prevValueBold && boldFont ) ImGui::PopFont();
		textY += 15.0f * uiScale;
	}
	if ( showStats && showPossibleSave ) {
		ImU32 possibleValueCol = st.ptsMs == (int)0x80000000 ? colStatPossibleMissing : ( st.ptsMs <= 0 ? colStatPossibleZero : colStatPossible );
		if ( statPossibleLabelBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colStatPossibleLabel, "Possible Save", shadow );
		if ( statPossibleLabelBold && boldFont ) ImGui::PopFont();
		if ( statPossibleValueBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, timeR, textY, possibleValueCol, st.ptsText[0] ? st.ptsText : "-", shadow );
		if ( statPossibleValueBold && boldFont ) ImGui::PopFont();
		textY += 14.0f * uiScale;
	}
	if ( showGhostSeg && st.ghostSegText[0] ) {
		if ( ghostBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colGhostLabel, "Ghost Segment", shadow );
		CL_ImGuiAddTextRight( draw, timeR, textY, colGhostTime, st.ghostSegText, shadow );
		if ( ghostBold && boldFont ) ImGui::PopFont();
		textY += 16.0f * uiScale;
	}
	if ( statRows > 0 ) {
		if ( showSob ) {
			if ( statSobLabelBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colStatLabel, "Sum of best segment", shadow );
			if ( statSobLabelBold && boldFont ) ImGui::PopFont();
			if ( statSobValueBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, colStatSob, st.sobText[0] ? st.sobText : "-----", shadow );
			if ( statSobValueBold && boldFont ) ImGui::PopFont();
			textY += 14.0f * uiScale;
		}
		if ( showBestPossible ) {
			if ( statBestLabelBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colStatLabel, "Best Possible", shadow );
			if ( statBestLabelBold && boldFont ) ImGui::PopFont();
			if ( statBestValueBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, colStatBest, st.bptText[0] ? st.bptText : "-----", shadow );
			if ( statBestValueBold && boldFont ) ImGui::PopFont();
			textY += 14.0f * uiScale;
		}
	}
	if ( showBestSegments && st.numBestSegs > 0 ) {
		int rows = st.numBestSegs < 4 ? st.numBestSegs : 4;
		if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
		textY += 4.0f * uiScale;
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colLabel, "Best Segments", shadow );
		textY += 14.0f * uiScale;
		for ( i = 0; i < rows; ++i ) {
			ImU32 bestCol = st.bestSegs[i].isGold ? ( showGoldRainbow ? CL_ImGuiRainbowColorU32( alphaMul ) : colGold ) : ( st.bestSegs[i].state == 1 ? colCurrent : ( st.bestSegs[i].state == 2 ? colCompleted : colFuture ) );
			ImGui::SetWindowFontScale( fontScale * splitNameSize );
			if ( splitNameBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddEllipsizedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( nameR, textY + rowH ), bestCol, st.bestSegs[i].name, shadow );
			if ( splitNameBold && boldFont ) ImGui::PopFont();
			if ( st.bestSegs[i].delta[0] ) {
				ImGui::SetWindowFontScale( fontScale * splitDeltaSize );
				if ( splitDeltaBold && boldFont ) ImGui::PushFont( boldFont );
				CL_ImGuiAddTextRight( draw, deltaR, textY, st.bestSegs[i].deltaMs <= 0 ? colAhead : colBehind, st.bestSegs[i].delta, shadow );
				if ( splitDeltaBold && boldFont ) ImGui::PopFont();
			}
			ImGui::SetWindowFontScale( fontScale * splitTimeSize );
			if ( splitTimeBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, st.bestSegs[i].time[0] ? bestCol : colDim, st.bestSegs[i].time[0] ? st.bestSegs[i].time : "-----", shadow );
			if ( splitTimeBold && boldFont ) ImGui::PopFont();
			ImGui::SetWindowFontScale( fontScale );
			textY += rowH;
		}
	}
	if ( showRgt ) {
		if ( rgtBold && boldFont ) ImGui::PushFont( boldFont );
		ImGui::SetWindowFontScale( fontScale * rgtSize );
		if ( rightAligned ) CL_ImGuiAddTextRight( draw, timeR, textY, colRgt, st.rtTimerText, shadow );
		else CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colRgt, st.rtTimerText, shadow );
		ImGui::SetWindowFontScale( fontScale );
		if ( rgtBold && boldFont ) ImGui::PopFont();
		textY += 13.5f * uiScale * rgtSize;
	}
	if ( show100 ) {
		char secBuf[64], treBuf[64];
		if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
		textY += 4.0f * uiScale;
		Com_sprintf( secBuf, sizeof( secBuf ), "Secrets %d/%d", st.pctSecretsFound, st.pctSecretsTotal );
		Com_sprintf( treBuf, sizeof( treBuf ), "Treasure %d/%d", st.pctTreasureFound, st.pctTreasureTotal );
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colAhead, secBuf, shadow );
		textY += 14.0f * uiScale;
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colAhead, treBuf, shadow );
		textY += 14.0f * uiScale;
	}
	if ( editMode ) {
		ImVec2 gripMin = ImVec2( x + w - 23.0f * uiScale, y + panelH - 23.0f * uiScale );
		ImVec2 gripMax = ImVec2( x + w - 5.0f * uiScale, y + panelH - 5.0f * uiScale );
		ImGui::SetCursorScreenPos( gripMin );
		ImGui::InvisibleButton( "ls_imgui_resize", ImVec2( 22.0f * uiScale, 22.0f * uiScale ) );
		if ( ImGui::IsItemActive() ) {
			s_liveSplitEditActive = true;
		}
		if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0 ) ) {
			ImVec2 d = ImGui::GetIO().MouseDelta;
			float scaleForResize = Com_Clamp( 0.5f, 3.0f, Cvar_VariableValue( "ls_scale" ) );
			float nextW = Cvar_VariableValue( "ls_w" ) + d.x / ( sx * scaleForResize );
			float nextScale = Cvar_VariableValue( "ls_scale" ) + d.y / sy / 140.0f;
			Cvar_SetValue( "ls_w", Com_Clamp( 80.0f, 420.0f, nextW ) );
			Cvar_SetValue( "ls_scale", Com_Clamp( 0.5f, 3.0f, nextScale ) );
		}
		draw->AddTriangleFilled( ImVec2( gripMax.x, gripMin.y ), gripMax, ImVec2( gripMin.x, gripMax.y ), ImGui::IsItemHovered() || ImGui::IsItemActive() ? CL_ImGuiGuiAccentAltU32( alphaMul ) : CL_ImGuiGuiAccentU32( alphaMul * 0.75f ) );
		CL_ImGuiAddRectLines( draw, x, y, w, panelH, ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) ? CL_ImGuiGuiAccentAltU32( alphaMul ) : CL_ImGuiGuiAccentU32( alphaMul * 0.55f ), borderThickness + 0.6f );
	}
	ImGui::SetWindowFontScale( 1.0f );
	if ( overlayFont ) {
		ImGui::PopFont();
	}
	ImGui::End();
	ImGui::PopStyleVar( 3 );
}

static void CL_ImGuiResetHudLayoutDefaults( void ) {
	Cvar_Set( "ls_x", "6.666843" );
	Cvar_Set( "ls_y", "92.444427" );
	Cvar_Set( "ls_w", "215" );
	Cvar_Set( "ls_scale", "0.550000" );
	Cvar_Set( "ls_align", "0" );
	Cvar_Set( "cg_drawKeys", "1" );
	Cvar_Set( "ks_scale", "0.750000" );
	Cvar_Set( "ks_ingame_only", "1" );
	Cvar_Set( "ks_imgui", "1" );
	Cvar_Set( "ks_layout", "0" );
	Cvar_Set( "ks_x", "297" );
	Cvar_Set( "ks_y", "375" );
	Cvar_Set( "ks_mouse", "2" );
	Cvar_Set( "ks_effect", "1" );
	Cvar_Set( "ks_box_w", "22" );
	Cvar_Set( "ks_box_h", "18" );
	Cvar_Set( "ks_gap", "2" );
	Cvar_Set( "ks_rounding", "0" );
	Cvar_Set( "ks_border_size", "1" );
	Cvar_Set( "ks_mouse_grid_size", "92" );
	Cvar_Set( "ks_mouse_grid_cells", "5" );
	Cvar_Set( "ks_mouse_grid_cm", "1" );
	Cvar_Set( "ks_mouse_grid_run_cm", "1" );
	Cvar_Set( "ks_active_anchor", "0" );
	Cvar_Set( "ks_active_max", "3" );
	Cvar_Set( "ks_grid_keys", "0" );
	Cvar_Set( "ks_grid_keys_dir", "1" );
	Cvar_Set( "ks_grid_keys_x", "-35" );
	Cvar_Set( "ks_grid_keys_y", "-15" );
	Cvar_Set( "ks_grid_keys_font_scale", "0.72" );
	Cvar_Set( "ks_font_scale", "0.620000" );
	Cvar_Set( "cg_drawVelocity", "1" );
	Cvar_Set( "cg_velocity_size", "2" );
	Cvar_Set( "cg_velocity_mode", "0" );
	Cvar_Set( "cg_velocity_x", "320" );
	Cvar_Set( "cg_velocity_y", "457" );
	Cvar_Set( "cg_velocity_scale", "1.0" );
	Cvar_Set( "cg_velocity_align", "1" );
	Cvar_Set( "cg_fpsX", "500" );
	Cvar_Set( "cg_fpsY", "0" );
	Cvar_Set( "cg_fpsScale", "1.0" );
	Cvar_Set( "cg_fpsAlign", "0" );
	Cvar_Set( "ls_igttimer_x", "638" );
	Cvar_Set( "ls_igttimer_y", "240" );
	Cvar_Set( "ls_igttimer_scale", "1.0" );
	Cvar_Set( "ls_igttimer_align", "2" );
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

static void CL_ImGuiDrawUpdateNotice( void ) {
	char available[16];
	char version[32];
	const char *dismissed = s_imguiUpdateDismissed ? s_imguiUpdateDismissed->string : "";
	float pulse;

	Cvar_VariableStringBuffer( "sp_updateAvailable", available, sizeof( available ) );
	Cvar_VariableStringBuffer( "sp_updateVersion", version, sizeof( version ) );
	if ( atoi( available ) == 0 || !version[0] ) {
		return;
	}
	if ( dismissed && dismissed[0] && !Q_stricmp( dismissed, version ) ) {
		return;
	}

	pulse = s_imguiAnimations && s_imguiAnimations->integer != 0 ? ( 0.85f + 0.15f * sinf( (float)ImGui::GetTime() * 3.0f ) ) : 1.0f;
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.10f, 0.070f, 0.020f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( 0.95f, 0.74f, 0.24f, 0.75f * pulse ) );
	ImGui::BeginChild( "update_notice", ImVec2( 0, 62 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetWindowPos();
	ImVec2 s = ImGui::GetWindowSize();
	draw->AddRectFilled( ImVec2( p.x + 1.0f, p.y + 1.0f ), ImVec2( p.x + 5.0f, p.y + s.y - 1.0f ), IM_COL32( 244, 188, 62, 230 ), 3.0f );
	ImGui::SetCursorPos( ImVec2( 16.0f, 9.0f ) );
	ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.24f, 1.0f ), "Update available: v%s", version );
	ImGui::SetCursorPos( ImVec2( 16.0f, 30.0f ) );
	ImGui::TextDisabled( "A newer RtCW Speedrun Patch release is ready." );
	ImGui::SetCursorPos( ImVec2( ImGui::GetWindowWidth() - 196.0f, 18.0f ) );
	if ( ImGui::Button( "Open", ImVec2( 82, 0 ) ) ) {
		Cbuf_AddText( "sp_openUpdate\n" );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Dismiss", ImVec2( 92, 0 ) ) && s_imguiUpdateDismissed ) {
		Cvar_Set( s_imguiUpdateDismissed->name, version );
	}
	ImGui::EndChild();
	ImGui::PopStyleColor( 2 );
	ImGui::Spacing();
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

static float CL_ImGuiEaseOutCubic( float t ) {
	t = Com_Clamp( 0.0f, 1.0f, t );
	t = 1.0f - t;
	return 1.0f - t * t * t;
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

static void CL_ImGuiDrawBindConflicts( void ) {
	int i, j, conflicts = 0;
	ImGui::BeginChild( "bind_conflicts", ImVec2( 0, 84 ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "Bind conflict detector" );
	for ( i = 0; i < IM_ARRAYSIZE( s_bindEntries ); ++i ) {
		int keyA = Key_GetKey( s_bindEntries[i].command );
		if ( keyA <= 0 ) continue;
		for ( j = i + 1; j < IM_ARRAYSIZE( s_bindEntries ); ++j ) {
			int keyB = Key_GetKey( s_bindEntries[j].command );
			if ( keyA == keyB ) {
				ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.25f, 1.0f ), "%s conflicts with %s on %s", s_bindEntries[i].label, s_bindEntries[j].label, Key_KeynumToString( keyA, qtrue ) );
				conflicts++;
			}
		}
	}
	if ( conflicts == 0 ) {
		ImGui::TextDisabled( "No conflicts detected in speedrun/demo binds." );
	}
	ImGui::EndChild();
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

	CL_ImGuiDrawPinnedSettings();
	CL_ImGuiSectionHeader( "Timer Configuration", "Core LiveSplit and in-game timer options for the current run." );
	ImGui::BeginChild( "timer_card", ImVec2( 0, 268 ), true );
	CL_ImGuiBoolCvar( "Enable Timer", s_cg_livesplit );
	CL_ImGuiBoolCvarName( "Show LiveSplit Panel", "ls_draw", "1" );
	CL_ImGuiComboCvar( "LiveSplit Type", s_ls_type, typeLabels, typeValues, IM_ARRAYSIZE( typeValues ) );
	CL_ImGuiComboCvar( "Run Mode", s_ls_mode, modeLabels, modeValues, IM_ARRAYSIZE( modeValues ) );
	CL_ImGuiComboCvar( "Chapter", s_ls_mission, missionLabels, missionValues, IM_ARRAYSIZE( missionValues ) );
	CL_ImGuiStringComboCvarName( "IL Map", "ls_map", "escape1", s_mapLabels, s_mapValues, IM_ARRAYSIZE( s_mapValues ) );
	CL_ImGuiBoolCvar( "100% Category", s_ls_100pct );
	CL_ImGuiComboCvar( "Compare Against", s_ls_compare, compareLabels, compareValues, IM_ARRAYSIZE( compareValues ) );
	CL_ImGuiComboCvar( "Timing Method", s_ls_timing, timingLabels, timingValues, IM_ARRAYSIZE( timingValues ) );
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
	static const float bg[4] = { 0.04f, 0.04f, 0.06f, 0.82f };
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

static void CL_ImGuiResetLiveSplitStyleDefaults( void ) {
	Cvar_Set( "ls_x", "6.666843" );
	Cvar_Set( "ls_y", "92.444427" );
	Cvar_Set( "ls_w", "215" );
	Cvar_Set( "ls_scale", "0.550000" );
	Cvar_Set( "ls_align", "0" );
	Cvar_Set( "ls_maxrows", "6" );
	Cvar_Set( "ls_opacity", "0.900000" );
	Cvar_Set( "ls_opacity_ui", "0.900000" );
	Cvar_Set( "ls_bgalpha", "0.78" );
	Cvar_Set( "ls_text_shadow", "1" );
	Cvar_Set( "ls_draw", "1" );
	Cvar_Set( "ls_showtimer", "1" );
	Cvar_Set( "ls_showheader", "1" );
	Cvar_Set( "ls_showstats", "1" );
	Cvar_Set( "ls_showseg", "1" );
	Cvar_Set( "ls_showrgt", "1" );
	Cvar_Set( "ls_showpb", "1" );
	Cvar_Set( "ls_showbest", "1" );
	Cvar_Set( "ls_showseps", "1" );
	Cvar_Set( "ls_showdeltas", "1" );
	Cvar_Set( "ls_showbestdeltas", "1" );
	Cvar_Set( "ls_showatt", "1" );
	Cvar_Set( "ls_100pct", "0" );
	Cvar_Set( "ls_imgui_rounding", "0" );
	Cvar_Set( "ls_imgui_padding", "5" );
	Cvar_Set( "ls_imgui_show_title", "0" );
	Cvar_Set( "ls_imgui_show_border", "1" );
	Cvar_Set( "ls_imgui_header_bg", "0" );
	Cvar_Set( "ls_imgui_show_status", "0" );
	Cvar_Set( "ls_imgui_gradient", "0" );
	Cvar_Set( "ls_imgui_gradient_angle", "230" );
	Cvar_Set( "ls_imgui_current_bg", "1" );
	Cvar_Set( "ls_imgui_border_size", "0.500000" );
	Cvar_Set( "ls_imgui_font", "1" );
	Cvar_Set( "ls_imgui_show_prevseg", "1" );
	Cvar_Set( "ls_imgui_show_ghostseg", "0" );
	Cvar_Set( "ls_imgui_show_bestsegments", "0" );
	Cvar_Set( "ls_imgui_row_size", "0.88" );
	Cvar_Set( "ls_imgui_name_size", "0.92" );
	Cvar_Set( "ls_imgui_bestdelta_size", "0.920000" );
	Cvar_Set( "ls_imgui_delta_size", "0.88" );
	Cvar_Set( "ls_imgui_time_size", "0.94" );
	Cvar_Set( "ls_split_countdown_lead", "10" );
	Cvar_Set( "ls_imgui_gold_rainbow", "1" );
	Cvar_Set( "ls_imgui_col_name", "0.347525" );
	Cvar_Set( "ls_imgui_col_best", "0.574001" );
	Cvar_Set( "ls_imgui_col_delta", "0.763122" );
	Cvar_Set( "ls_imgui_timer_size", "1.630000" );
	Cvar_Set( "ls_imgui_stage_size", "1.450000" );
	Cvar_Set( "ls_imgui_info_size", "0.650000" );
	Cvar_Set( "ls_imgui_rgt_size", "1.250000" );
	Cvar_Set( "ls_imgui_timer_gap", "2" );
	Cvar_Set( "ls_imgui_info_gap", "24" );
	Cvar_Set( "ls_imgui_timer_split", "0.400000" );
	Cvar_Set( "ls_imgui_show_sob", "1" );
	Cvar_Set( "ls_imgui_show_possible_save", "1" );
	Cvar_Set( "ls_imgui_show_best_possible", "1" );
	Cvar_Set( "ls_imgui_bold_title", "1" );
	Cvar_Set( "ls_imgui_bold_attempts", "1" );
	Cvar_Set( "ls_imgui_bold_status", "0" );
	Cvar_Set( "ls_imgui_bold_header", "1" );
	Cvar_Set( "ls_imgui_bold_timer", "1" );
	Cvar_Set( "ls_imgui_bold_stage", "1" );
	Cvar_Set( "ls_imgui_bold_info", "1" );
	Cvar_Set( "ls_imgui_bold_splits", "0" );
	Cvar_Set( "ls_imgui_bold_split_name", "0" );
	Cvar_Set( "ls_imgui_bold_split_best", "1" );
	Cvar_Set( "ls_imgui_bold_split_delta", "1" );
	Cvar_Set( "ls_imgui_bold_split_time", "1" );
	Cvar_Set( "ls_imgui_bold_prev", "0" );
	Cvar_Set( "ls_imgui_bold_prev_label", "0" );
	Cvar_Set( "ls_imgui_bold_prev_value", "1" );
	Cvar_Set( "ls_imgui_bold_ghost", "0" );
	Cvar_Set( "ls_imgui_bold_stats", "0" );
	Cvar_Set( "ls_imgui_bold_stat_sob_label", "0" );
	Cvar_Set( "ls_imgui_bold_stat_sob_value", "1" );
	Cvar_Set( "ls_imgui_bold_stat_possible_label", "0" );
	Cvar_Set( "ls_imgui_bold_stat_possible_value", "1" );
	Cvar_Set( "ls_imgui_bold_stat_best_label", "0" );
	Cvar_Set( "ls_imgui_bold_stat_best_value", "1" );
	Cvar_Set( "ls_imgui_bold_rgt", "1" );
	Cvar_Set( "ls_imgui_text_gradient", "0" );
	Cvar_Set( "ls_imgui_text_gradient_angle", "0" );
	Cvar_Set( "ls_clr_bg", "10 10 15 0.63" );
	Cvar_Set( "ls_clr_bg2", "10 10 15 0.90" );
	Cvar_Set( "ls_clr_border", "29 52 24 0.58" );
	Cvar_Set( "ls_clr_sep", "22 36 18 0.34" );
	Cvar_Set( "ls_clr_highlight", "13 28 12 0.27" );
	Cvar_Set( "ls_clr_text", "214 224 210 0.92" );
	Cvar_Set( "ls_clr_text_gradient2", "255 255 255 0.59" );
	Cvar_Set( "ls_clr_timer", "224 246 214 1.00" );
	Cvar_Set( "ls_clr_title", "90 210 58 1.00" );
	Cvar_Set( "ls_clr_category", "132 158 120 0.88" );
	Cvar_Set( "ls_clr_header_bg", "10 18 12 0.68" );
	Cvar_Set( "ls_clr_column_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_current", "86 210 55 1.00" );
	Cvar_Set( "ls_clr_completed", "120 130 118 0.62" );
	Cvar_Set( "ls_clr_future", "124 124 124 1.00" );
	Cvar_Set( "ls_clr_ahead", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_behind", "220 72 72 1.00" );
	Cvar_Set( "ls_clr_gold", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_stage_timer", "178 190 172 0.84" );
	Cvar_Set( "ls_clr_status_live", "84 205 55 1.00" );
	Cvar_Set( "ls_clr_status_ready", "80 92 76 0.90" );
	Cvar_Set( "ls_clr_status_pause", "255 191 64 1.00" );
	Cvar_Set( "ls_clr_status_done", "64 217 64 1.00" );
	Cvar_Set( "ls_clr_status_text", "6 10 6 0.95" );
	Cvar_Set( "ls_clr_pb_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_pb_value", "218 226 214 0.92" );
	Cvar_Set( "ls_clr_best_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_best_value", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_prev_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_prev_ahead", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_prev_behind", "220 72 72 1.00" );
	Cvar_Set( "ls_clr_prev_gold", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_ghost_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_ghost_time", "178 190 172 0.84" );
	Cvar_Set( "ls_clr_stat_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_sob", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_stat_possible_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_possible_save", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_stat_possible_zero", "112 118 112 0.62" );
	Cvar_Set( "ls_clr_stat_possible_missing", "82 92 76 0.70" );
	Cvar_Set( "ls_clr_stat_best_possible", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_rgt", "218 226 214 0.92" );
	Cvar_Set( "ls_clr_empty", "112 118 112 0.48" );
}

static void CL_ImGuiDrawLiveSplitElementStylesPage( void ) {
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
	static const char *fontLabels[] = { "Segoe UI", "Consolas", "Arial", "Tahoma", "Verdana", "Trebuchet", "Calibri", "Courier", "Impact", "Times", "Georgia", "Lucida Console", "Segoe Bold", "Candara", "Corbel", "Calibri Bold", "Segoe Semibold", "Segoe Light", "Segoe Italic", "Arial Italic", "Arial Bold Italic", "Cambria", "Cambria Bold", "Constantia", "Constantia Bold", "Comic Sans", "Comic Sans Bold", "Gadugi", "Gadugi Bold", "Bahnschrift", "Palatino Italic", "Palatino Bold" };
	static const int fontValues[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
	static const float statusLive[4] = { 0.35f, 0.75f, 0.20f, 1.00f };
	static const float statusReady[4] = { 0.32f, 0.36f, 0.30f, 0.90f };
	static const float statusPause[4] = { 1.00f, 0.75f, 0.25f, 1.00f };
	static const float statusDone[4] = { 0.25f, 0.85f, 0.25f, 1.00f };

	CL_ImGuiSectionHeader( "LiveSplit Elements", "Every component has its own visibility, bold and color options." );
	if ( ImGui::Button( "Reset LiveSplit Style", ImVec2( 170, 0 ) ) ) {
		CL_ImGuiResetLiveSplitStyleDefaults();
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "small dark default preset" );
	ImGui::Spacing();

	ImGui::BeginChild( "ls_box_panel", ImVec2( 0, CL_ImGuiAutoBoxHeight( 17 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Panel / background" );
	CL_ImGuiBoolCvarName( "Show LiveSplit Panel", "ls_draw", "1" );
	CL_ImGuiBoolCvarName( "Panel Border", "ls_imgui_show_border", "1" );
	CL_ImGuiBoolCvarName( "Header Background", "ls_imgui_header_bg", "0" );
	CL_ImGuiBoolCvarName( "Panel Gradient", "ls_imgui_gradient", "0" );
	CL_ImGuiComboCvarName( "LiveSplit Font", "ls_imgui_font", "1", fontLabels, fontValues, IM_ARRAYSIZE( fontValues ) );
	CL_ImGuiSliderCvarName( "Width", "ls_w", "215", 80.0f, 400.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Global Scale", "ls_scale", "0.550000", 0.5f, 2.5f, "%.2f" );
	CL_ImGuiSliderCvarName( "Padding", "ls_imgui_padding", "5", 3.0f, 14.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Border Thickness", "ls_imgui_border_size", "0.500000", 0.5f, 4.0f, "%.1f" );
	CL_ImGuiSliderCvarName( "Gradient Angle DEG", "ls_imgui_gradient_angle", "230", 0.0f, 360.0f, "%.0f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_panel", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Gradient top / solid background", "ls_clr_bg", bg );
		CL_ImGuiColorCvarName( "Gradient bottom", "ls_clr_bg2", dim );
		CL_ImGuiColorCvarName( "Panel Border", "ls_clr_border", border );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_text_gradient", ImVec2( 0, CL_ImGuiAutoBoxHeight( 6 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Text gradient" );
	CL_ImGuiBoolCvarName( "Gradient on text", "ls_imgui_text_gradient", "0" );
	CL_ImGuiSliderCvarName( "Text Gradient Angle DEG", "ls_imgui_text_gradient_angle", "0", 0.0f, 360.0f, "%.0f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_text_gradient", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Text gradient end", "ls_clr_text_gradient2", header );
		ImGui::TreePop();
	}
	ImGui::TextDisabled( "Uses each component color as start and this color as end." );
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_title", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Title / category" );
	CL_ImGuiBoolCvarName( "Title", "ls_imgui_show_title", "0" );
	CL_ImGuiBoolCvarName( "Attempt Counter", "ls_showatt", "1" );
	CL_ImGuiBoolCvarName( "Bold title/category", "ls_imgui_bold_title", "1" );
	CL_ImGuiBoolCvarName( "Bold attempts", "ls_imgui_bold_attempts", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_title", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Header background fill", "ls_clr_header_bg", header );
		CL_ImGuiColorCvarName( "Game title text", "ls_clr_title", header );
		CL_ImGuiColorCvarName( "Category / attempt text", "ls_clr_category", mapname );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_status", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Status chip" );
	CL_ImGuiBoolCvarName( "LIVE / READY / DONE", "ls_imgui_show_status", "0" );
	CL_ImGuiBoolCvarName( "Bold status text", "ls_imgui_bold_status", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_status", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "LIVE chip background", "ls_clr_status_live", statusLive );
		CL_ImGuiColorCvarName( "READY chip background", "ls_clr_status_ready", statusReady );
		CL_ImGuiColorCvarName( "PAUSE chip background", "ls_clr_status_pause", statusPause );
		CL_ImGuiColorCvarName( "DONE chip background", "ls_clr_status_done", statusDone );
		CL_ImGuiColorCvarName( "Chip text", "ls_clr_status_text", text );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_columns", ImVec2( 0, CL_ImGuiAutoBoxHeight( 7 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Column header row" );
	CL_ImGuiBoolCvar( "Header columns", s_ls_showheaders );
	CL_ImGuiBoolCvarName( "Separators", "ls_showseps", "1" );
	CL_ImGuiBoolCvarName( "Bold column labels", "ls_imgui_bold_header", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_columns", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Column labels", "ls_clr_column_label", mapname );
		CL_ImGuiColorCvarName( "Separator lines", "ls_clr_sep", border );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_timer", ImVec2( 0, CL_ImGuiAutoBoxHeight( 27 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Timer component: IGT + Stage + PB/BEST" );
	CL_ImGuiBoolCvar( "IGT Main Timer", s_ls_showtimer );
	CL_ImGuiBoolCvar( "Stage Timer", s_ls_showsegtimer );
	CL_ImGuiBoolCvarName( "Stage PB", "ls_showpb", "1" );
	CL_ImGuiBoolCvarName( "Stage BEST", "ls_showbest", "1" );
	ImGui::Separator();
	CL_ImGuiBoolCvarName( "Bold IGT", "ls_imgui_bold_timer", "1" );
	CL_ImGuiBoolCvarName( "Bold stage", "ls_imgui_bold_stage", "1" );
	CL_ImGuiBoolCvarName( "Bold PB/BEST", "ls_imgui_bold_info", "1" );
	CL_ImGuiSliderCvarName( "IGT Size", "ls_imgui_timer_size", "1.630000", 0.75f, 2.00f, "%.2f" );
	CL_ImGuiSliderCvarName( "Stage Size", "ls_imgui_stage_size", "1.450000", 0.55f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "PB/BEST Size", "ls_imgui_info_size", "0.650000", 0.55f, 1.25f, "%.2f" );
	CL_ImGuiSliderCvarName( "PB/BEST horizontal gap", "ls_imgui_info_gap", "24", 24.0f, 120.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Timer / stats gap", "ls_imgui_timer_gap", "2", 2.0f, 24.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "PB/BEST side position", "ls_imgui_timer_split", "0.400000", 0.40f, 0.76f, "%.2f" );
	ImGui::Separator();
	if ( ImGui::TreeNodeEx( "Colors##ls_timer", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "IGT normal", "ls_clr_timer", timer );
		CL_ImGuiColorCvarName( "IGT ahead / finished", "ls_clr_ahead", ahead );
		CL_ImGuiColorCvarName( "IGT behind", "ls_clr_behind", behind );
		CL_ImGuiColorCvarName( "Stage Color", "ls_clr_stage_timer", timer );
		CL_ImGuiColorCvarName( "PB label", "ls_clr_pb_label", mapname );
		CL_ImGuiColorCvarName( "PB value", "ls_clr_pb_value", text );
		CL_ImGuiColorCvarName( "BEST label", "ls_clr_best_label", mapname );
		CL_ImGuiColorCvarName( "BEST value", "ls_clr_best_value", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_splits", ImVec2( 0, CL_ImGuiAutoBoxHeight( 28 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Split rows" );
	CL_ImGuiBoolCvarName( "PB Delta (+/-)", "ls_showdeltas", "1" );
	CL_ImGuiBoolCvarName( "Best Delta (+/-)", "ls_showbestdeltas", "1" );
	CL_ImGuiBoolCvarName( "Current Row Background", "ls_imgui_current_bg", "1" );
	CL_ImGuiBoolCvarName( "Rainbow gold BEST +/-", "ls_imgui_gold_rainbow", "1" );
	CL_ImGuiBoolCvarName( "Bold all split rows", "ls_imgui_bold_splits", "0" );
	CL_ImGuiBoolCvarName( "Bold stage names", "ls_imgui_bold_split_name", "0" );
	CL_ImGuiBoolCvarName( "Bold BEST +/-", "ls_imgui_bold_split_best", "1" );
	CL_ImGuiBoolCvarName( "Bold +/-", "ls_imgui_bold_split_delta", "1" );
	CL_ImGuiBoolCvarName( "Bold split time", "ls_imgui_bold_split_time", "1" );
	CL_ImGuiSliderCvarName( "Row Size", "ls_imgui_row_size", "0.88", 0.75f, 1.45f, "%.2f" );
	CL_ImGuiSliderCvarName( "Map Name Font", "ls_imgui_name_size", "0.92", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "BEST +/- Font", "ls_imgui_bestdelta_size", "0.920000", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "+/- Font", "ls_imgui_delta_size", "0.88", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "Time Font", "ls_imgui_time_size", "0.94", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "Stage Name Width", "ls_imgui_col_name", "0.347525", 0.22f, 0.78f, "%.2f" );
	CL_ImGuiSliderCvarName( "BEST +/- Width", "ls_imgui_col_best", "0.574001", 0.34f, 0.88f, "%.2f" );
	CL_ImGuiSliderCvarName( "+/- Width", "ls_imgui_col_delta", "0.763122", 0.44f, 0.94f, "%.2f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_splits", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Future split name", "ls_clr_future", future );
		CL_ImGuiColorCvarName( "Completed split name", "ls_clr_completed", completed );
		CL_ImGuiColorCvarName( "Current split name", "ls_clr_current", current );
		CL_ImGuiColorCvarName( "Current row background", "ls_clr_highlight", current );
		CL_ImGuiColorCvarName( "Gold / best delta", "ls_clr_gold", gold );
		CL_ImGuiColorCvarName( "Ahead delta", "ls_clr_ahead", ahead );
		CL_ImGuiColorCvarName( "Behind delta", "ls_clr_behind", behind );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_prev", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Previous segment" );
	CL_ImGuiBoolCvarName( "Previous Segment", "ls_imgui_show_prevseg", "1" );
	CL_ImGuiBoolCvarName( "Bold previous label", "ls_imgui_bold_prev_label", "0" );
	CL_ImGuiBoolCvarName( "Bold previous value", "ls_imgui_bold_prev_value", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_prev", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Previous label", "ls_clr_prev_label", mapname );
		CL_ImGuiColorCvarName( "Previous ahead", "ls_clr_prev_ahead", ahead );
		CL_ImGuiColorCvarName( "Previous behind", "ls_clr_prev_behind", behind );
		CL_ImGuiColorCvarName( "Previous gold", "ls_clr_prev_gold", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_ghost", ImVec2( 0, CL_ImGuiAutoBoxHeight( 6 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Ghost segment" );
	CL_ImGuiBoolCvarName( "Ghost Segment", "ls_imgui_show_ghostseg", "0" );
	CL_ImGuiBoolCvarName( "Bold ghost", "ls_imgui_bold_ghost", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_ghost", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Ghost label", "ls_clr_ghost_label", mapname );
		CL_ImGuiColorCvarName( "Ghost time", "ls_clr_ghost_time", timer );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stats", ImVec2( 0, CL_ImGuiAutoBoxHeight( 5 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Statistics: master" );
	CL_ImGuiBoolCvar( "Statistics", s_ls_showstats );
	CL_ImGuiBoolCvarName( "Bold all statistics", "ls_imgui_bold_stats", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_stats", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Statistics labels", "ls_clr_stat_label", mapname );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stat_sob", ImVec2( 0, CL_ImGuiAutoBoxHeight( 7 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Statistic: Sum of best" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_sob", "1" );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_sob_label", "0" );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_sob_value", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_sob", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Sum of best value", "ls_clr_stat_sob", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stat_possible", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Statistic: Possible save" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_possible_save", "1" );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_possible_label", "0" );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_possible_value", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_possible", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Possible save label", "ls_clr_stat_possible_label", mapname );
		CL_ImGuiColorCvarName( "Possible save value", "ls_clr_stat_possible_save", ahead );
		CL_ImGuiColorCvarName( "Possible save zero", "ls_clr_stat_possible_zero", dim );
		CL_ImGuiColorCvarName( "Possible save missing", "ls_clr_stat_possible_missing", dim );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stat_best", ImVec2( 0, CL_ImGuiAutoBoxHeight( 7 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "Statistic: Best possible" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_best_possible", "1" );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_best_label", "0" );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_best_value", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_best", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Best possible value", "ls_clr_stat_best_possible", text );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_extra", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( "RGT / best segments" );
	CL_ImGuiBoolCvarName( "RGT", "ls_showrgt", "1" );
	CL_ImGuiBoolCvarName( "Best Segments", "ls_imgui_show_bestsegments", "0" );
	CL_ImGuiBoolCvarName( "Bold RGT", "ls_imgui_bold_rgt", "1" );
	CL_ImGuiSliderCvarName( "RGT Size", "ls_imgui_rgt_size", "1.250000", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiIntSliderCvarName( "Max Visible Splits", "ls_maxrows", "6", 0, 20 );
	if ( ImGui::TreeNodeEx( "Colors##ls_extra", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "RGT", "ls_clr_rgt", text );
		CL_ImGuiColorCvarName( "Inactive / empty", "ls_clr_empty", dim );
		ImGui::TreePop();
	}
	ImGui::EndChild();

}

static void CL_ImGuiDrawDisplayPage( void ) {
	static const char *alignLabels[] = { "Left", "Right" };
	static const int alignValues[] = { 0, 1 };

	CL_ImGuiSectionHeader( "LiveSplit Panel Layout", "These controls affect both the new ImGui overlay and the fallback renderer panel." );
	ImGui::BeginChild( "display_layout_card", ImVec2( 0, CL_ImGuiAutoBoxHeight( 4 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	CL_ImGuiComboCvarName( "Content Side", "ls_align", "0", alignLabels, alignValues, IM_ARRAYSIZE( alignValues ) );
	CL_ImGuiSliderCvarName( "X Position", "ls_x", "6.666843", 0.0f, 580.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Y Position", "ls_y", "92.444427", 0.0f, 440.0f, "%.0f" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Transparency", NULL );
	ImGui::BeginChild( "display_opacity_card", ImVec2( 0, CL_ImGuiAutoBoxHeight( 4 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	CL_ImGuiSliderCvarName( "Splits Opacity", "ls_opacity", "0.900000", 0.0f, 1.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Splits In Menu", "ls_opacity_ui", "0.900000", 0.0f, 1.0f, "%.2f" );
	ImGui::TextDisabled( "Background alpha is now controlled by Style > Background color alpha." );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Text", NULL );
	ImGui::BeginChild( "display_text_card", ImVec2( 0, CL_ImGuiAutoBoxHeight( 3 ) ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	CL_ImGuiBoolCvarName( "Text Shadow", "ls_text_shadow", "1" );
	ImGui::TextDisabled( "Colors and LiveSplit layout are grouped in this Style page." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawHudPage( void ) {
	static const char *mouseLabels[] = { "Off", "Clicks Only", "Clicks + Direction" };
	static const int mouseValues[] = { 0, 1, 2 };
	static const char *keysLayoutLabels[] = { "Classic", "Horizontal", "Compact", "Mouse Grid", "Vertical", "Active Horizontal", "Active Vertical", "Mouse Grid Only" };
	static const int keysLayoutValues[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	static const char *keysEffectLabels[] = { "Off", "Clean", "Soft Glow", "Pulse" };
	static const int keysEffectValues[] = { 0, 1, 2, 3 };
	static const char *alignLabels[] = { "Left", "Center", "Right" };
	static const int alignValues[] = { 0, 1, 2 };
	static const char *activeAnchorLabels[] = { "Left / Top", "Right", "Center", "Bottom" };
	static const int activeAnchorValues[] = { 0, 1, 2, 3 };
	static const char *gridKeysDirLabels[] = { "Horizontal", "Vertical" };
	static const int gridKeysDirValues[] = { 0, 1 };
	static const float ksBg[4] = { 0.04f, 0.04f, 0.06f, 0.72f };
	static const float ksActive[4] = { 0.10f, 0.24f, 0.07f, 0.88f };
	static const float ksBorder[4] = { 0.20f, 0.25f, 0.18f, 0.30f };
	static const float ksActiveBorder[4] = { 0.42f, 0.75f, 0.22f, 0.85f };
	static const float ksText[4] = { 0.50f, 0.56f, 0.48f, 0.78f };
	static const float ksActiveText[4] = { 0.86f, 0.97f, 0.72f, 1.00f };
	static const float ksGridChecker[4] = { 0.08f, 0.12f, 0.08f, 0.42f };
	static const float ksGridCross[4] = { 0.92f, 0.86f, 0.35f, 0.72f };
	static const float ksGridTrail[4] = { 0.55f, 0.95f, 0.30f, 0.90f };
	static const float ksGridCm[4] = { 0.85f, 0.95f, 0.70f, 0.88f };
	int currentLayout = Cvar_VariableIntegerValue( "ks_layout" );
	bool usesExtraKeys = currentLayout != 2 && currentLayout != 3 && currentLayout != 7;
	bool usesMouseDisplay = currentLayout != 3 && currentLayout != 7;

	CL_ImGuiSectionHeader( "Keystrokes", "Defaults use a fixed HUD position instead of auto-centering." );
	ImGui::BeginChild( "hud_keys_card", ImVec2( 0, 452 ), true );
	CL_ImGuiBoolCvarName( "Show Keystrokes", "cg_drawKeys", "1" );
	CL_ImGuiBoolCvarName( "Only During Gameplay", "ks_ingame_only", "1" );
	Cvar_Set( "ks_imgui", "1" );
	CL_ImGuiComboCvarName( "Keys Layout", "ks_layout", "0", keysLayoutLabels, keysLayoutValues, IM_ARRAYSIZE( keysLayoutValues ) );
	CL_ImGuiComboCvarName( "Press Effect", "ks_effect", "1", keysEffectLabels, keysEffectValues, IM_ARRAYSIZE( keysEffectValues ) );
	CL_ImGuiSliderCvarName( "Keys X Position", "ks_x", "297", 0.0f, 600.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Keys Y Position", "ks_y", "375", 0.0f, 460.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Keys Scale", "ks_scale", "0.750000", 0.3f, 4.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Font Scale", "ks_font_scale", "0.620000", 0.30f, 2.4f, "%.2f" );
	CL_ImGuiSliderCvarName( "Box Width", "ks_box_w", "22", 14.0f, 80.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Box Height", "ks_box_h", "18", 12.0f, 54.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Gap", "ks_gap", "2", 0.0f, 20.0f, "%.0f" );
	Cvar_Set( "ks_rounding", "0" );
	CL_ImGuiSliderCvarName( "Border Size", "ks_border_size", "1", 0.0f, 5.0f, "%.1f" );
	CL_ImGuiSliderCvarName( "Mouse Grid Size", "ks_mouse_grid_size", "92", 48.0f, 220.0f, "%.0f" );
	CL_ImGuiIntSliderCvarName( "Mouse Grid Squares", "ks_mouse_grid_cells", "5", 3, 12 );
	CL_ImGuiBoolCvarName( "Mouse Total CM Counter", "ks_mouse_grid_cm", "1" );
	CL_ImGuiBoolCvarName( "Mouse Run CM Counter", "ks_mouse_grid_run_cm", "1" );
	CL_ImGuiSliderCvarName( "Keys Opacity", "ks_opacity", "1.0", 0.0f, 1.0f, "%.2f" );
	if ( usesMouseDisplay ) {
		CL_ImGuiComboCvarName( "Mouse Display", "ks_mouse", "2", mouseLabels, mouseValues, IM_ARRAYSIZE( mouseValues ) );
	}
	if ( currentLayout == 5 || currentLayout == 6 || currentLayout == 7 ) {
		CL_ImGuiComboCvarName( "Active Snap Side", "ks_active_anchor", "0", activeAnchorLabels, activeAnchorValues, IM_ARRAYSIZE( activeAnchorValues ) );
		CL_ImGuiIntSliderCvarName( "Max Active Keys", "ks_active_max", "3", 1, 10 );
	}
	if ( currentLayout == 7 ) {
		CL_ImGuiBoolCvarName( "Show Grid Buttons", "ks_grid_keys", "0" );
		CL_ImGuiComboCvarName( "Grid Button Direction", "ks_grid_keys_dir", "1", gridKeysDirLabels, gridKeysDirValues, IM_ARRAYSIZE( gridKeysDirValues ) );
		CL_ImGuiSliderCvarName( "Grid Buttons X", "ks_grid_keys_x", "-35", -110.0f, 110.0f, "%.0f" );
		CL_ImGuiSliderCvarName( "Grid Buttons Y", "ks_grid_keys_y", "-15", -110.0f, 110.0f, "%.0f" );
		CL_ImGuiSliderCvarName( "Grid Buttons Font", "ks_grid_keys_font_scale", "0.72", 0.30f, 1.20f, "%.2f" );
	}
	if ( usesExtraKeys ) {
		ImGui::Columns( 2, NULL, false );
		CL_ImGuiBoolCvarName( "Show Use", "ks_show_use", "0" );
		ImGui::NextColumn();
		CL_ImGuiBoolCvarName( "Show Reload", "ks_show_reload", "0" );
		ImGui::Columns( 1 );
	}
	if ( ImGui::TreeNodeEx( "Keystroke Colors", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Idle Background", "ks_clr_bg", ksBg );
		CL_ImGuiColorCvarName( "Active Background", "ks_clr_active", ksActive );
		CL_ImGuiColorCvarName( "Idle Border", "ks_clr_border", ksBorder );
		CL_ImGuiColorCvarName( "Active Border", "ks_clr_active_border", ksActiveBorder );
		CL_ImGuiColorCvarName( "Idle Text", "ks_clr_text", ksText );
		CL_ImGuiColorCvarName( "Active Text", "ks_clr_active_text", ksActiveText );
		CL_ImGuiColorCvarName( "Mouse checker squares", "ks_clr_grid_checker", ksGridChecker );
		CL_ImGuiColorCvarName( "Mouse center cross", "ks_clr_grid_cross", ksGridCross );
		CL_ImGuiColorCvarName( "Mouse trail", "ks_clr_grid_trail", ksGridTrail );
		CL_ImGuiColorCvarName( "Mouse CM counter", "ks_clr_grid_cm", ksGridCm );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "HUD Elements", NULL );
	ImGui::BeginChild( "hud_elements_card", ImVec2( 0, 165 ), true );
	ImGui::Columns( 2, NULL, false );
	CL_ImGuiBoolCvarName( "Show LiveSplit", "ls_draw", "1" );
	CL_ImGuiBoolCvarName( "Speedometer", "cg_drawVelocity", "1" );
	CL_ImGuiBoolCvarName( "Position HUD", "cg_drawPos", "0" );
	ImGui::NextColumn();
	CL_ImGuiBoolCvarName( "Jump Statistics", "cg_drawJumpStats", "0" );
	CL_ImGuiBoolCvarName( "Strafe Guide", "cg_strafeGuide", "0" );
	CL_ImGuiBoolCvarName( "Show FPS", "cg_drawfps", "0" );
	CL_ImGuiBoolCvarName( "Show Timer", "cg_drawTimer", "0" );
	ImGui::Columns( 1 );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Standalone IGT Timer", NULL );
	ImGui::BeginChild( "hud_igt_card", ImVec2( 0, 190 ), true );
	CL_ImGuiBoolCvarName( "Show IGT Timer", "ls_igttimer", "0" );
	CL_ImGuiComboCvarName( "IGT Align", "ls_igttimer_align", "2", alignLabels, alignValues, IM_ARRAYSIZE( alignValues ) );
	CL_ImGuiSliderCvarName( "IGT X Position", "ls_igttimer_x", "638", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "IGT Y Position", "ls_igttimer_y", "240", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "IGT Scale", "ls_igttimer_scale", "1.0", 0.3f, 4.0f, "%.2f" );
	CL_ImGuiBoolCvarName( "Show IGT Segment Timer", "ls_igtsegtimer", "0" );
	ImGui::EndChild();
}

static void CL_ImGuiDrawStylePage( void ) {
	CL_ImGuiDrawLiveSplitElementStylesPage();
	CL_ImGuiDrawDisplayPage();
}

static void CL_ImGuiDrawSpeedoPage( void ) {
	static const char *modeLabels[] = { "3D (Full)", "Horizontal (XY)", "Vertical (Z)" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *sizeLabels[] = { "Tiny", "Small", "Big", "Giant" };
	static const int sizeValues[] = { 0, 1, 2, 3 };
	static const char *posLabels[] = { "Bottom", "Center" };
	static const int posValues[] = { 0, 1 };
	static const char *alignLabels[] = { "Left", "Center", "Right" };
	static const int alignValues[] = { 0, 1, 2 };

	CL_ImGuiSectionHeader( "Speedometer", "Text align decides whether X is the left, center or right anchor." );
	ImGui::BeginChild( "speedo_card", ImVec2( 0, 322 ), true );
	CL_ImGuiComboCvarName( "Mode", "cg_velocity_mode", "0", modeLabels, modeValues, IM_ARRAYSIZE( modeValues ) );
	CL_ImGuiComboCvarName( "Text Size", "cg_velocity_size", "2", sizeLabels, sizeValues, IM_ARRAYSIZE( sizeValues ) );
	CL_ImGuiComboCvarName( "Position", "cg_velocity_type", "0", posLabels, posValues, IM_ARRAYSIZE( posValues ) );
	CL_ImGuiComboCvarName( "Text Align", "cg_velocity_align", "1", alignLabels, alignValues, IM_ARRAYSIZE( alignValues ) );
	CL_ImGuiBoolCvarName( "Speed Change Color Fade", "cg_velocity_colorfade", "0" );
	CL_ImGuiBoolCvarName( "Peak Speed Above", "cg_velocity_peak", "0" );
	CL_ImGuiSliderCvarName( "Peak Reset Speed", "cg_velocity_peak_reset", "8", 1.0f, 80.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Scale", "cg_velocity_scale", "1.0", 0.25f, 4.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "X Position", "cg_velocity_x", "320", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Y Position", "cg_velocity_y", "457", 0.0f, 480.0f, "%.0f" );
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
	static const char *weaponColorLabels[] = { "Off", "Tint", "Rainbow", "Flat Color", "Flat Rainbow", "X-Ray", "X-Ray Rainbow", "Pulse" };
	static const int weaponColorValues[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	static const float weaponColorFallback[4] = { 0.10f, 0.75f, 1.00f, 1.00f };

	CL_ImGuiSectionHeader( "Field of View", NULL );
	ImGui::BeginChild( "view_fov_card", ImVec2( 0, 138 ), true );
	CL_ImGuiSliderCvarName( "FOV Front-Back", "cg_fov", "90", 60.0f, 160.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "FOV Down-Up", "cg_fov_down", "90", 60.0f, 160.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "FOV Left-Right", "cg_fov_lr", "90", 0.0f, 160.0f, "%.0f" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Framerate / Weapon", NULL );
	ImGui::BeginChild( "view_misc_card", ImVec2( 0, 280 ), true );
	CL_ImGuiComboCvarName( "Max FPS", "com_maxfps", "125", fpsLabels, fpsValues, IM_ARRAYSIZE( fpsValues ) );
	CL_ImGuiComboCvarName( "Weapon Hand", "cg_drawGun", "1", gunLabels, gunValues, IM_ARRAYSIZE( gunValues ) );
	ImGui::Separator();
	CL_ImGuiComboCvarName( "Weapon Render", "cg_weapon_color_mode", "0", weaponColorLabels, weaponColorValues, IM_ARRAYSIZE( weaponColorValues ) );
	CL_ImGuiColorCvarName( "Tint Color", "cg_weapon_color", weaponColorFallback );
	CL_ImGuiSliderCvarName( "Tint Opacity", "cg_weapon_color_opacity", "0.35", 0.0f, 1.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "X-Ray Strength", "cg_weapon_xray_strength", "0.65", 0.0f, 1.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Rainbow Speed", "cg_weapon_rainbow_speed", "1.0", 0.1f, 5.0f, "%.1f" );
	ImGui::TextDisabled( "Tint keeps textures, Flat removes textures, X-Ray adds a stronger colored shell." );
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
	CL_ImGuiDrawBindConflicts();
	ImGui::Spacing();
	for ( int i = 0; i < IM_ARRAYSIZE( s_bindEntries ); ++i ) {
		if ( i == 0 ) ImGui::TextDisabled( "LiveSplit controls" );
		if ( i == 8 ) { ImGui::Separator(); ImGui::TextDisabled( "Demo controls" ); }
		if ( i == 20 ) { ImGui::Separator(); ImGui::TextDisabled( "Practice controls" ); }
		CL_ImGuiBindingRow( s_bindEntries[i].label, s_bindEntries[i].command );
	}
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
		ImGui::BulletText( "speedrun_livesplit_dump - print/copy/save current ImGui LiveSplit layout/style" );
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
	CL_ImGuiSectionHeader( "Ghost Replay", "In-game ghost preview for saved best split recordings." );
	ImGui::BeginChild( "ghost_card", ImVec2( 0, 116 ), true );
	CL_ImGuiBoolCvarName( "Show Ghost", "ls_ghost", "0" );
	CL_ImGuiIntSliderCvarName( "Opacity", "ls_ghost_opacity", "60", 5, 255 );
	ImGui::TextWrapped( "Ghost displays your best split as a translucent model and requires a saved ghost recording for the current map." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawLayoutToolsPage( void ) {
	CL_ImGuiSectionHeader( "Layout Grid", "Enable while the game is paused behind this panel to align HUD elements." );
	ImGui::BeginChild( "grid_card", ImVec2( 0, 286 ), true );
	CL_ImGuiBoolCvarName( "Show Grid", "ui_speedrun_grid", "0" );
	CL_ImGuiBoolCvarName( "HUD Edit Mode", "ui_speedrun_layout_edit", "0", "hover boxes and drag HUD elements" );
	CL_ImGuiBoolCvarName( "Show Labels", "ui_speedrun_grid_labels", "1" );
	CL_ImGuiBoolCvarName( "Center Lines", "ui_speedrun_grid_center", "1" );
	CL_ImGuiSliderCvarName( "Grid Size", "ui_speedrun_grid_size", "32", 8.0f, 160.0f, "%.0f" );
	CL_ImGuiIntSliderCvarName( "Major Every", "ui_speedrun_grid_major", "5", 2, 10 );
	CL_ImGuiSliderCvarName( "Grid Opacity", "ui_speedrun_grid_opacity", "0.22", 0.02f, 0.75f, "%.2f" );
	ImGui::Separator();
	if ( ImGui::Button( "Reset HUD positions", ImVec2( 170, 0 ) ) ) {
		CL_ImGuiResetHudLayoutDefaults();
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "Default spots for LiveSplit, Keys, Velocity, FPS/Timer and IGT." );
	ImGui::TextDisabled( "Grid and drag handles are drawn only while the Speedrun GUI is open." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawSplitNamesPage( void ) {
	int i;

	CL_ImGuiSectionHeader( "Custom Split Names", "Edit all map names in one table. Empty field = default name." );
	ImGui::BeginChild( "split_names_card", ImVec2( 0, 316 ), true );
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.16f, 0.27f, 0.12f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.03f, 0.05f, 0.035f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.07f, 0.11f, 0.06f, 0.68f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.38f, 0.16f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.14f, 0.24f, 0.11f, 0.72f ) );
	if ( ImGui::BeginTable( "split_names_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2( 0, 260 ) ) ) {
		ImGui::TableSetupColumn( "Map", ImGuiTableColumnFlags_WidthFixed, 78.0f );
		ImGui::TableSetupColumn( "Default", ImGuiTableColumnFlags_WidthFixed, 160.0f );
		ImGui::TableSetupColumn( "Custom", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthFixed, 62.0f );
		ImGui::TableHeadersRow();
		for ( i = 0; i < IM_ARRAYSIZE( s_mapValues ); ++i ) {
			char cvarName[64];
			char custom[64];
			cvar_t *nameCv;
			Com_sprintf( cvarName, sizeof( cvarName ), "ls_name_%s", s_mapValues[i] );
			nameCv = CL_ImGuiCvar( cvarName, "" );
			Q_strncpyz( custom, nameCv && nameCv->string ? nameCv->string : "", sizeof( custom ) );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 ); ImGui::TextUnformatted( s_mapValues[i] );
			ImGui::TableSetColumnIndex( 1 ); ImGui::TextUnformatted( s_mapLabels[i] );
			ImGui::TableSetColumnIndex( 2 );
			ImGui::PushID( cvarName );
			ImGui::SetNextItemWidth( -1.0f );
			if ( ImGui::InputTextWithHint( "##custom", s_mapLabels[i], custom, sizeof( custom ) ) && nameCv ) {
				Cvar_Set( nameCv->name, custom );
				Cbuf_AddText( "livesplit_sv_refresh\n" );
			}
			ImGui::TableSetColumnIndex( 3 );
			if ( ImGui::SmallButton( "Reset" ) && nameCv ) {
				Cvar_Set( nameCv->name, "" );
				Cbuf_AddText( "livesplit_sv_refresh\n" );
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor( 5 );
	ImGui::TextWrapped( "Names are archived cvars, so they stay in your config and do not modify .lss files." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawToolsPage( void ) {
	CL_ImGuiDrawLayoutToolsPage();
	CL_ImGuiDrawSplitNamesPage();
}

static void CL_ImGuiDrawRecordsPage( void ) {
	char rowName[32];
	char rowText[256];
	char statsText[256];
	char cmd[96];
	int i;
	int recordCount = 0;
	int summaryAtt = 0;
	int summaryComp = 0;
	float attempts[10] = { 0 };
	float completions[10] = { 0 };
	static int selectedRecordRow = -1;
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
				ImGui::TableSetColumnIndex( 0 );
				ImGui::PushID( i );
				if ( ImGui::Selectable( map, selectedRecordRow == i, ImGuiSelectableFlags_SpanAllColumns ) ) {
					selectedRecordRow = i;
				}
				ImGui::PopID();
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
	ImGui::BeginDisabled( selectedRecordRow < 0 );
	Com_sprintf( cmd, sizeof( cmd ), "livesplit_sv_reset_gold %d", selectedRecordRow );
	CL_ImGuiCommandButton( "Reset Row Gold", cmd, "selected split" );
	Com_sprintf( cmd, sizeof( cmd ), "livesplit_sv_reset_pbseg %d", selectedRecordRow );
	CL_ImGuiCommandButton( "Reset Row PB Seg", cmd, "selected split" );
	Com_sprintf( cmd, sizeof( cmd ), "livesplit_sv_reset_split %d", selectedRecordRow );
	CL_ImGuiCommandButton( "Reset Row All", cmd, "gold, PB segment, attempts, completions" );
	ImGui::EndDisabled();
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
	ImGui::Separator();
	CL_ImGuiBoolCvarName( "Explosive Timers", "cg_explosiveTimers", "0" );
	CL_ImGuiBoolCvarName( "Held Grenade/Dynamite Timer", "cg_explosiveTimersHeld", "1" );
	CL_ImGuiBoolCvarName( "Pinned World Timers", "cg_explosiveTimersWorld", "1" );
	ImGui::Separator();
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
	const char *cats[] = { "Run", "Search", "Style", "Overlay", "Game", "Tools", "Controls", "Records", "Demos", "Actions", "Dev Tools", "Help", "About" };
	int i;
	float openEase = ( s_imguiAnimations && s_imguiAnimations->integer != 0 ) ? CL_ImGuiEaseOutCubic( s_imguiAnim ) : 1.0f;
	float alpha = s_imguiAlpha ? Com_Clamp( 0.70f, 1.0f, s_imguiAlpha->value ) : 0.96f;
	ImGuiWindowFlags settingsFlags = ImGuiWindowFlags_NoCollapse | ( s_imguiPinned ? ImGuiWindowFlags_NoMove : 0 );
	ImGuiWindowFlags minimizedFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ( s_imguiPinned ? ImGuiWindowFlags_NoMove : 0 );

	if ( s_imguiMinimized ) {
		ImGui::SetNextWindowPos( ImVec2( 32, 28 ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSize( ImVec2( 318, 42 ), ImGuiCond_Always );
		ImGui::SetNextWindowBgAlpha( alpha );
		if ( ImGui::Begin( "Speedrun Settings##minimized", &s_imguiOpen, minimizedFlags ) ) {
			ImGui::SetCursorPosY( 10.0f );
			ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Speedrun Settings" );
			ImGui::SameLine();
			ImGui::SetCursorPosX( ImGui::GetWindowWidth() - 172.0f );
			if ( ImGui::SmallButton( "Restore" ) ) {
				s_imguiMinimized = false;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( s_imguiPinned ? "Unpin" : "Pin" ) ) {
				s_imguiPinned = !s_imguiPinned;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "X" ) ) {
				s_imguiOpen = false;
			}
		}
		ImGui::End();
		return;
	}

	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 && s_imguiAnim < 1.0f ) {
		float inv = 1.0f - openEase;
		float baseX = s_imguiWindowX ? s_imguiWindowX->value : 32.0f;
		float baseY = s_imguiWindowY ? s_imguiWindowY->value : 28.0f;
		float baseW = s_imguiWindowW ? s_imguiWindowW->value : 660.0f;
		float baseH = s_imguiWindowH ? s_imguiWindowH->value : 500.0f;
		ImGui::SetNextWindowPos( ImVec2( baseX + inv * 18.0f, baseY + inv * 26.0f ), ImGuiCond_Always );
		ImGui::SetNextWindowSize( ImVec2( baseW - inv * 42.0f, baseH - inv * 34.0f ), ImGuiCond_Always );
	} else {
		ImGui::SetNextWindowPos( ImVec2( s_imguiWindowX ? s_imguiWindowX->value : 32.0f, s_imguiWindowY ? s_imguiWindowY->value : 28.0f ), ImGuiCond_Appearing );
		ImGui::SetNextWindowSize( ImVec2( s_imguiWindowW ? s_imguiWindowW->value : 660.0f, s_imguiWindowH ? s_imguiWindowH->value : 500.0f ), ImGuiCond_Appearing );
	}
	ImGui::SetNextWindowSizeConstraints( ImVec2( 520, 360 ), ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight ) );
	ImGui::SetNextWindowBgAlpha( alpha * ( 0.20f + 0.80f * openEase ) );
	if ( !ImGui::Begin( "Speedrun Settings", &s_imguiOpen, settingsFlags ) ) {
		ImGui::End();
		return;
	}

	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 && s_imguiAnim < 1.0f ) {
		ImGui::SetCursorPosY( ImGui::GetCursorPosY() + ( 1.0f - openEase ) * 18.0f );
	}
	if ( s_imguiAnim >= 1.0f ) {
		ImVec2 winPos = ImGui::GetWindowPos();
		ImVec2 winSize = ImGui::GetWindowSize();
		if ( s_imguiWindowX ) Cvar_SetValue( s_imguiWindowX->name, winPos.x );
		if ( s_imguiWindowY ) Cvar_SetValue( s_imguiWindowY->name, winPos.y );
		if ( s_imguiWindowW ) Cvar_SetValue( s_imguiWindowW->name, winSize.x );
		if ( s_imguiWindowH ) Cvar_SetValue( s_imguiWindowH->name, winSize.y );
	}
	CL_ImGuiDrawBackgroundMist();
	CL_ImGuiDrawAccentBanner();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Speedrun Settings" );
	ImGui::SameLine();
	if ( ImGui::SmallButton( s_imguiMinimized ? "Restore" : "Minimize" ) ) {
		s_imguiMinimized = !s_imguiMinimized;
	}
	ImGui::SameLine();
	ImGui::SetCursorPosX( ImGui::GetWindowContentRegionMax().x - 108.0f );
	if ( ImGui::SmallButton( s_imguiPinned ? "Unpin" : "Pin" ) ) {
		s_imguiPinned = !s_imguiPinned;
	}
	ImGui::SameLine();
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
			if ( s_imguiWindowX ) Cvar_Set( s_imguiWindowX->name, "32" );
			if ( s_imguiWindowY ) Cvar_Set( s_imguiWindowY->name, "28" );
			if ( s_imguiWindowW ) Cvar_Set( s_imguiWindowW->name, "660" );
			if ( s_imguiWindowH ) Cvar_Set( s_imguiWindowH->name, "500" );
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
	CL_ImGuiDrawUpdateNotice();
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
		bool active = s_imguiCategory == i;
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
			s_imguiCategory = i;
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
	switch ( s_imguiCategory ) {
	case 0: CL_ImGuiDrawTimerPage(); break;
	case 1: CL_ImGuiDrawSettingsSearchPage(); break;
	case 2: CL_ImGuiDrawStylePage(); break;
	case 3: CL_ImGuiDrawOverlayPage(); break;
	case 4: CL_ImGuiDrawGamePage(); break;
	case 5: CL_ImGuiDrawToolsPage(); break;
	case 6: CL_ImGuiDrawBindsPage(); break;
	case 7: CL_ImGuiDrawRecordsPage(); break;
	case 8: CL_ImGuiDrawDemosPage(); break;
	case 9: CL_ImGuiDrawActionsPage(); break;
	case 10: CL_ImGuiDrawDevPage(); break;
	case 11: CL_ImGuiDrawHelpPage(); break;
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

static bool CL_ImGuiCopyTextToClipboard( const char *text ) {
	HGLOBAL mem;
	char *dst;
	size_t len;
	if ( !text ) return false;
	len = strlen( text ) + 1;
	if ( !OpenClipboard( NULL ) ) return false;
	mem = GlobalAlloc( GMEM_MOVEABLE, len );
	if ( !mem ) {
		CloseClipboard();
		return false;
	}
	dst = (char *)GlobalLock( mem );
	if ( !dst ) {
		GlobalFree( mem );
		CloseClipboard();
		return false;
	}
	memcpy( dst, text, len );
	GlobalUnlock( mem );
	EmptyClipboard();
	if ( !SetClipboardData( CF_TEXT, mem ) ) {
		GlobalFree( mem );
		CloseClipboard();
		return false;
	}
	CloseClipboard();
	return true;
}

static void CL_SpeedrunImGui_DumpLiveSplitLayout_f( void ) {
	static const char *names[] = {
		"ls_x", "ls_y", "ls_w", "ls_scale", "ls_align", "ls_maxrows", "ls_opacity", "ls_opacity_ui", "ls_bgalpha", "ls_text_shadow",
		"ls_draw", "ls_showtimer", "ls_showheader", "ls_showstats", "ls_showseg", "ls_showrgt", "ls_showpb", "ls_showbest", "ls_showseps", "ls_showdeltas", "ls_showbestdeltas", "ls_showatt", "ls_100pct",
		"ls_imgui_rounding", "ls_imgui_padding", "ls_imgui_show_title", "ls_imgui_show_border", "ls_imgui_header_bg", "ls_imgui_show_status", "ls_imgui_gradient", "ls_imgui_gradient_angle", "ls_imgui_current_bg", "ls_imgui_border_size", "ls_imgui_font",
		"ls_imgui_show_prevseg", "ls_imgui_show_ghostseg", "ls_imgui_show_bestsegments", "ls_imgui_row_size", "ls_imgui_name_size", "ls_imgui_bestdelta_size", "ls_imgui_delta_size", "ls_imgui_time_size", "ls_imgui_gold_rainbow",
		"ls_imgui_col_name", "ls_imgui_col_best", "ls_imgui_col_delta", "ls_imgui_timer_size", "ls_imgui_stage_size", "ls_imgui_info_size", "ls_imgui_rgt_size", "ls_imgui_timer_gap", "ls_imgui_info_gap", "ls_imgui_timer_split",
		"ls_imgui_show_sob", "ls_imgui_show_possible_save", "ls_imgui_show_best_possible",
		"ls_imgui_bold_title", "ls_imgui_bold_attempts", "ls_imgui_bold_status", "ls_imgui_bold_header", "ls_imgui_bold_timer", "ls_imgui_bold_stage", "ls_imgui_bold_info", "ls_imgui_bold_splits", "ls_imgui_bold_split_name", "ls_imgui_bold_split_best", "ls_imgui_bold_split_delta", "ls_imgui_bold_split_time", "ls_imgui_bold_prev", "ls_imgui_bold_prev_label", "ls_imgui_bold_prev_value", "ls_imgui_bold_ghost", "ls_imgui_bold_stats", "ls_imgui_bold_stat_sob_label", "ls_imgui_bold_stat_sob_value", "ls_imgui_bold_stat_possible_label", "ls_imgui_bold_stat_possible_value", "ls_imgui_bold_stat_best_label", "ls_imgui_bold_stat_best_value", "ls_imgui_bold_rgt",
		"ls_imgui_text_gradient", "ls_imgui_text_gradient_angle",
		"ls_clr_bg", "ls_clr_bg2", "ls_clr_border", "ls_clr_sep", "ls_clr_highlight", "ls_clr_text", "ls_clr_text_gradient2", "ls_clr_timer", "ls_clr_title", "ls_clr_category", "ls_clr_header_bg", "ls_clr_column_label",
		"ls_clr_current", "ls_clr_completed", "ls_clr_future", "ls_clr_ahead", "ls_clr_behind", "ls_clr_gold", "ls_clr_stage_timer",
		"ls_clr_status_live", "ls_clr_status_ready", "ls_clr_status_pause", "ls_clr_status_done", "ls_clr_status_text",
		"ls_clr_pb_label", "ls_clr_pb_value", "ls_clr_best_label", "ls_clr_best_value",
		"ls_clr_prev_label", "ls_clr_prev_ahead", "ls_clr_prev_behind", "ls_clr_prev_gold",
		"ls_clr_ghost_label", "ls_clr_ghost_time", "ls_clr_stat_label", "ls_clr_stat_sob", "ls_clr_stat_possible_label", "ls_clr_stat_possible_save", "ls_clr_stat_possible_zero", "ls_clr_stat_possible_missing", "ls_clr_stat_best_possible", "ls_clr_rgt", "ls_clr_empty",
		"cg_drawKeys", "ks_ingame_only", "ks_x", "ks_y", "ks_scale", "ks_opacity", "ks_mouse", "ks_imgui", "ks_layout", "ks_effect", "ks_box_w", "ks_box_h", "ks_gap", "ks_rounding", "ks_border_size", "ks_font_scale",
		"ks_mouse_grid_size", "ks_mouse_grid_cells", "ks_mouse_grid_cm", "ks_mouse_grid_run_cm", "ks_mouse_grid_total_cm_value", "ks_active_anchor", "ks_active_max", "ks_grid_keys", "ks_grid_keys_dir", "ks_grid_keys_x", "ks_grid_keys_y", "ks_grid_keys_font_scale",
		"ks_show_use", "ks_show_reload", "ks_clr_bg", "ks_clr_active", "ks_clr_border", "ks_clr_active_border", "ks_clr_text", "ks_clr_active_text", "ks_clr_grid_checker", "ks_clr_grid_cross", "ks_clr_grid_trail", "ks_clr_grid_cm",
		"cg_drawVelocity", "cg_velocity_type", "cg_velocity_size", "cg_velocity_mode", "cg_velocity_x", "cg_velocity_y", "cg_velocity_scale", "cg_velocity_align", "cg_velocity_colorfade", "cg_velocity_peak", "cg_velocity_peak_reset",
		"cg_explosiveTimers", "cg_explosiveTimersHeld", "cg_explosiveTimersWorld"
	};
	char dump[65536];
	char line[512];
	size_t used = 0;
	fileHandle_t f;
	int i;
	dump[0] = '\0';
	Com_sprintf( line, sizeof( line ), "// ---- LiveSplit ImGui layout dump ----\n" );
	Q_strcat( dump, sizeof( dump ), line );
	used = strlen( dump );
	for ( i = 0; i < IM_ARRAYSIZE( names ); ++i ) {
		const char *value = Cvar_VariableString( names[i] );
		Com_sprintf( line, sizeof( line ), "seta %s \"%s\"\n", names[i], value && value[0] ? value : "" );
		if ( used + strlen( line ) + 1 < sizeof( dump ) ) {
			Q_strcat( dump, sizeof( dump ), line );
			used = strlen( dump );
		}
	}
	Q_strcat( dump, sizeof( dump ), "// ---- binds ----\n" );
	used = strlen( dump );
	for ( i = 0; i < IM_ARRAYSIZE( s_bindEntries ); ++i ) {
		int key = Key_GetKey( s_bindEntries[i].command );
		if ( key > 0 ) {
			const char *keyName = Key_KeynumToString( key, qfalse );
			Com_sprintf( line, sizeof( line ), "bind %s \"%s\" // %s\n", keyName && keyName[0] ? keyName : "UNKNOWN", s_bindEntries[i].command, s_bindEntries[i].label );
			if ( used + strlen( line ) + 1 < sizeof( dump ) ) {
				Q_strcat( dump, sizeof( dump ), line );
				used = strlen( dump );
			}
		}
	}
	Q_strcat( dump, sizeof( dump ), "// ---- end LiveSplit ImGui layout dump ----\n" );

	Com_Printf( "\n%s\n", dump );
	f = FS_FOpenFileWrite( "livesplit_dump.cfg" );
	if ( f ) {
		FS_Write( dump, strlen( dump ), f );
		FS_FCloseFile( f );
		Com_Printf( "^2LiveSplit: dump saved to livesplit_dump.cfg\n" );
	} else {
		Com_Printf( "^1LiveSplit: failed to write livesplit_dump.cfg\n" );
	}
	if ( CL_ImGuiCopyTextToClipboard( dump ) ) {
		Com_Printf( "^2LiveSplit: dump copied to clipboard\n" );
	} else {
		Com_Printf( "^3LiveSplit: clipboard copy failed; use livesplit_dump.cfg\n" );
	}
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
	s_imguiUpdateDismissed = Cvar_Get( "ui_speedrun_imgui_update_dismissed", "", CVAR_ARCHIVE );
	s_imguiWindowX = Cvar_Get( "ui_speedrun_imgui_x", "32", CVAR_ARCHIVE );
	s_imguiWindowY = Cvar_Get( "ui_speedrun_imgui_y", "28", CVAR_ARCHIVE );
	s_imguiWindowW = Cvar_Get( "ui_speedrun_imgui_w", "660", CVAR_ARCHIVE );
	s_imguiWindowH = Cvar_Get( "ui_speedrun_imgui_h", "500", CVAR_ARCHIVE );
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
	s_ls_imgui = Cvar_Get( "ls_imgui", "1", CVAR_ARCHIVE );
	Cvar_Set( "ls_imgui", "1" );
	Cvar_Get( "ls_imgui_rounding", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_padding", "5", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_title", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_border", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_header_bg", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_status", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_gradient", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_gradient_angle", "230", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_current_bg", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_border_size", "0.500000", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_font", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_prevseg", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_ghostseg", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_bestsegments", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_row_size", "0.88", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_name_size", "0.92", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bestdelta_size", "0.920000", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_delta_size", "0.88", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_time_size", "0.94", CVAR_ARCHIVE );
	Cvar_Get( "ls_split_countdown_lead", "10", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_gold_rainbow", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_col_name", "0.347525", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_col_best", "0.574001", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_col_delta", "0.763122", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_timer_size", "1.630000", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_stage_size", "1.450000", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_info_size", "0.650000", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_rgt_size", "1.250000", CVAR_ARCHIVE );
	Cvar_Get( "ks_ingame_only", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_timer_gap", "2", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_info_gap", "24", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_timer_split", "0.400000", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_sob", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_possible_save", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_show_best_possible", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_title", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_attempts", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_status", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_header", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_timer", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stage", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_info", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_splits", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_split_name", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_split_best", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_split_delta", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_split_time", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_prev", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_prev_label", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_prev_value", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_ghost", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stats", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stat_sob_label", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stat_sob_value", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stat_possible_label", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stat_possible_value", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stat_best_label", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_stat_best_value", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_bold_rgt", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_text_gradient", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_text_gradient_angle", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_status_live", "84 205 55 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_status_ready", "80 92 76 0.90", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_status_pause", "255 191 64 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_status_done", "64 217 64 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_status_text", "6 10 6 0.95", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_bg2", "10 10 15 0.90", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_title", "90 210 58 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_category", "132 158 120 0.88", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_header_bg", "10 18 12 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_column_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stage_timer", "178 190 172 0.84", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_pb_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_pb_value", "218 226 214 0.92", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_best_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_best_value", "255 220 50 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_prev_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_prev_ahead", "72 220 80 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_prev_behind", "220 72 72 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_prev_gold", "255 220 50 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_ghost_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_ghost_time", "178 190 172 0.84", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_sob", "255 220 50 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_save", "72 220 80 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_zero", "112 118 112 0.62", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_missing", "82 92 76 0.70", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_best_possible", "255 220 50 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_rgt", "218 226 214 0.92", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_empty", "112 118 112 0.48", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_text_gradient2", "255 255 255 0.59", CVAR_ARCHIVE );
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
	Cmd_AddCommand( "speedrun_livesplit_dump", CL_SpeedrunImGui_DumpLiveSplitLayout_f );
}

extern "C" void CL_SpeedrunImGui_Shutdown( void ) {
	if ( s_imguiInitialized ) {
		ImGui_ImplOpenGL2_Shutdown();
		ImGui::DestroyContext();
		s_imguiInitialized = false;
	}
}

extern "C" void CL_SpeedrunImGui_InvalidateDeviceObjects( void ) {
	if ( s_imguiInitialized ) {
		ImGui_ImplOpenGL2_DestroyDeviceObjects();
	}
}

extern "C" int CL_SpeedrunImGui_IsOpen( void ) {
	return s_imguiOpen ? 1 : 0;
}

extern "C" void CL_SpeedrunImGui_Draw( void ) {
	int now;
	float dt;
	int enabledNow;
	bool drawLiveSplitOverlay;

	enabledNow = s_imguiEnabled ? s_imguiEnabled->integer : 0;
	if ( enabledNow && !s_lastEnabledCvar ) {
		CL_SpeedrunImGui_Open();
	} else if ( !enabledNow && s_lastEnabledCvar && s_imguiOpen ) {
		CL_SpeedrunImGui_Close();
	}
	s_lastEnabledCvar = enabledNow;
	drawLiveSplitOverlay = CL_ImGuiShouldDrawLiveSplitOverlay();
	if ( !s_imguiOpen && !drawLiveSplitOverlay ) {
		return;
	}
	if ( s_imguiOpen ) {
		CL_SpeedrunImGui_ClearGameplayInput();
		Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_CONSOLE ) | KEYCATCH_UI );
		if ( cls.state == CA_ACTIVE ) {
			if ( !cl_paused || !cl_paused->integer ) {
				Cvar_Set( "cl_paused", "1" );
			}
			if ( s_imguiRestorePause && !s_imguiRestorePause->integer ) {
				Cvar_Set( s_imguiRestorePause->name, "1" );
			}
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
		s_imguiAnim = Com_Clamp( 0.0f, 1.0f, s_imguiAnim + dt * 3.8f );
	} else {
		s_imguiAnim = 1.0f;
	}
	io.DeltaTime = dt;
	io.MouseDown[0] = s_imguiOpen ? s_mouseDown[0] : false;
	io.MouseDown[1] = s_imguiOpen ? s_mouseDown[1] : false;
	io.MouseDown[2] = s_imguiOpen ? s_mouseDown[2] : false;
	io.MouseWheel = s_imguiOpen ? s_mouseWheel : 0.0f;
	io.MouseDrawCursor = s_imguiOpen;
	s_mouseWheel = 0.0f;

	ImGui_ImplOpenGL2_NewFrame();
	ImGui::NewFrame();
	if ( drawLiveSplitOverlay ) {
		CL_ImGuiDrawLiveSplitOverlay();
	}
	CL_ImGuiDrawKeystrokesOverlay();
	if ( s_imguiOpen ) {
		CL_ImGuiDrawLayoutGrid();
		CL_ImGuiDrawLayoutEditor();
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.08f + 0.92f * CL_ImGuiEaseOutCubic( s_imguiAnim ) );
		CL_ImGuiDrawSettings();
		ImGui::PopStyleVar();
	}
	if ( enabledNow && !s_imguiOpen ) {
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
	CL_SpeedrunImGui_SetupKnownGLState();
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
