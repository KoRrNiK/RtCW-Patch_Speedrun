// Dear ImGui speedrun settings UI.

#include <windows.h>
#include <GL/gl.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

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
static bool     s_raceGuiOpen = false;
static bool     s_raceGuiPinned = false;
static bool     s_raceGuiRestoreAfterChat = false;
static HWND     s_imguiHwnd = NULL;
static bool     s_raceChatOpen = false;
static bool     s_raceChatFocus = false;
static int      s_raceChatPreviousCatcher = 0;
static int      s_raceChatSuppressInputUntilMs = 0;
static int      s_raceChatIgnoreSubmitUntilMs = 0;
static char     s_raceChatInput[128] = "";
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
static cvar_t  *s_raceGuiWindowX = NULL;
static cvar_t  *s_raceGuiWindowY = NULL;
static cvar_t  *s_raceGuiWindowW = NULL;
static cvar_t  *s_raceGuiWindowH = NULL;
static int      s_lastFrameMs = 0;
static float    s_imguiAnim = 1.0f;
static float    s_raceGuiAnim = 1.0f;
static bool     s_mouseDown[5] = { false, false, false, false, false };
static float    s_mouseWheel = 0.0f;
static const char *s_pendingBindCommand = NULL;
static int      s_imguiCategory = 0;
static bool     s_imguiMinimized = false;
static char     s_settingsSearch[64] = "";
static bool     s_imguiPinned = false;
static ImFont  *s_imguiTimerFont = NULL;
#define SRGUI_LIVESPLIT_FONT_COUNT 33
#define SRGUI_LIVESPLIT_FONT_TIER_COUNT 4
static const float s_imguiLiveSplitFontTierPixels[SRGUI_LIVESPLIT_FONT_TIER_COUNT] = { 15.0f, 22.0f, 32.0f, 45.0f };
static ImFont  *s_imguiLiveSplitFonts[SRGUI_LIVESPLIT_FONT_COUNT] = { NULL };
static ImFont  *s_imguiLiveSplitBoldFonts[SRGUI_LIVESPLIT_FONT_COUNT] = { NULL };
static ImFont  *s_imguiLiveSplitFontTiers[SRGUI_LIVESPLIT_FONT_COUNT][SRGUI_LIVESPLIT_FONT_TIER_COUNT] = { NULL };
static ImFont  *s_imguiLiveSplitBoldFontTiers[SRGUI_LIVESPLIT_FONT_COUNT][SRGUI_LIVESPLIT_FONT_TIER_COUNT] = { NULL };
static bool     s_liveSplitEditActive = false;

static bool CL_SpeedrunImGui_HasPanelOpen( void ) {
	return s_imguiOpen || s_raceGuiOpen;
}

static void CL_ImGuiDrawLiveSplitOverlay( void );
static bool CL_ImGuiShouldDrawZoneTimerOverlay( void );
static void CL_ImGuiDrawZoneTimerOverlay( void );
static bool CL_ImGuiShouldDrawRaceOverlay( void );
static void CL_ImGuiDrawRaceOverlay( void );
static bool CL_ImGuiShouldDrawRaceCountdown( void );
static void CL_ImGuiDrawRaceCenterCountdown( void );

static const char *s_imguiLiveSplitFontPaths[SRGUI_LIVESPLIT_FONT_COUNT] = {
	"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\consola.ttf", "C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\tahoma.ttf",
	"C:\\Windows\\Fonts\\verdana.ttf", "C:\\Windows\\Fonts\\trebuc.ttf", "C:\\Windows\\Fonts\\calibri.ttf", "C:\\Windows\\Fonts\\cour.ttf",
	"C:\\Windows\\Fonts\\impact.ttf", "C:\\Windows\\Fonts\\times.ttf", "C:\\Windows\\Fonts\\georgia.ttf", "C:\\Windows\\Fonts\\lucon.ttf",
	"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\candara.ttf", "C:\\Windows\\Fonts\\corbel.ttf", "C:\\Windows\\Fonts\\calibrib.ttf",
	"C:\\Windows\\Fonts\\segoeuisb.ttf", "C:\\Windows\\Fonts\\segoeuil.ttf", "C:\\Windows\\Fonts\\segoeuii.ttf", "C:\\Windows\\Fonts\\ariali.ttf",
	"C:\\Windows\\Fonts\\arialbi.ttf", "C:\\Windows\\Fonts\\cambria.ttf", "C:\\Windows\\Fonts\\cambriab.ttf", "C:\\Windows\\Fonts\\constan.ttf",
	"C:\\Windows\\Fonts\\constanb.ttf", "C:\\Windows\\Fonts\\comic.ttf", "C:\\Windows\\Fonts\\comicbd.ttf", "C:\\Windows\\Fonts\\gadugi.ttf",
	"C:\\Windows\\Fonts\\gadugib.ttf", "C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\palai.ttf", "C:\\Windows\\Fonts\\palab.ttf",
	"C:\\Windows\\Fonts\\ariblk.ttf"
};

static const char *s_imguiLiveSplitBoldFontPaths[SRGUI_LIVESPLIT_FONT_COUNT] = {
	"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\consolab.ttf", "C:\\Windows\\Fonts\\arialbd.ttf", "C:\\Windows\\Fonts\\tahomabd.ttf",
	"C:\\Windows\\Fonts\\verdanab.ttf", "C:\\Windows\\Fonts\\trebucbd.ttf", "C:\\Windows\\Fonts\\calibrib.ttf", "C:\\Windows\\Fonts\\courbd.ttf",
	"C:\\Windows\\Fonts\\impact.ttf", "C:\\Windows\\Fonts\\timesbd.ttf", "C:\\Windows\\Fonts\\georgiab.ttf", "C:\\Windows\\Fonts\\lucon.ttf",
	"C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\candarab.ttf", "C:\\Windows\\Fonts\\corbelb.ttf", "C:\\Windows\\Fonts\\calibrib.ttf",
	"C:\\Windows\\Fonts\\segoeuisb.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf", "C:\\Windows\\Fonts\\segoeuiz.ttf", "C:\\Windows\\Fonts\\arialbi.ttf",
	"C:\\Windows\\Fonts\\arialbi.ttf", "C:\\Windows\\Fonts\\cambriab.ttf", "C:\\Windows\\Fonts\\cambriab.ttf", "C:\\Windows\\Fonts\\constanb.ttf",
	"C:\\Windows\\Fonts\\constanb.ttf", "C:\\Windows\\Fonts\\comicbd.ttf", "C:\\Windows\\Fonts\\comicbd.ttf", "C:\\Windows\\Fonts\\gadugib.ttf",
	"C:\\Windows\\Fonts\\gadugib.ttf", "C:\\Windows\\Fonts\\bahnschrift.ttf", "C:\\Windows\\Fonts\\palab.ttf", "C:\\Windows\\Fonts\\palab.ttf",
	"C:\\Windows\\Fonts\\ariblk.ttf"
};

static bool CL_ImGuiLoadLiveSplitLargeFontTier( int fontIndex, int tier ) {
	if ( tier <= 1 ) return true;
	return fontIndex == 0 || fontIndex == 1 || fontIndex == 2 || fontIndex == 7 || fontIndex == 12 || fontIndex == 15 || fontIndex == 29 || fontIndex == 32;
}

static float CL_ImGuiAutoBoxHeight( int rows ) {
	float line = ImGui::GetFrameHeightWithSpacing();
	return 2.0f + rows * line + ImGui::GetStyle().WindowPadding.y * 2.0f;
}

static bool CL_ImGuiBeginAutoBox( const char *label ) {
	return ImGui::BeginChild( label, ImVec2( 0, 0 ), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings );
}

static bool CL_ImGuiSameLineIfFits( float nextWidth, float spacing = -1.0f ) {
	float gap = spacing >= 0.0f ? spacing : ImGui::GetStyle().ItemSpacing.x;
	if ( ImGui::GetContentRegionAvail().x < nextWidth + gap ) {
		return false;
	}
	if ( spacing >= 0.0f ) {
		ImGui::SameLine( 0.0f, spacing );
	} else {
		ImGui::SameLine();
	}
	return true;
}

static ImFont *CL_ImGuiAddFontFileSafe( ImGuiIO &io, const char *path, float sizePixels ) {
	if ( !path || GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES ) {
		return NULL;
	}
	ImFontConfig cfg;
	cfg.OversampleH = 1;
	cfg.OversampleV = 1;
	cfg.PixelSnapH = true;
	return io.Fonts->AddFontFromFileTTF( path, sizePixels, &cfg );
}

#define SRGUI_MAX_DEMOS 2048
#define SRGUI_DEMOS_PER_PAGE 128
#define SRGUI_DEMO_SORT_NEWEST 6
#define SRGUI_DEMOCAT_FULLGAME 0
#define SRGUI_DEMOCAT_MISSION  1
#define SRGUI_DEMOCAT_IL       2
#define SRGUI_DEMOCAT_OTHER    3
#define SRGUI_DEMOMETA_PENDING 0
#define SRGUI_DEMOMETA_READY   1
#define SRGUI_DEMOMETA_FAILED  2

static char s_demoList[SRGUI_MAX_DEMOS][MAX_QPATH];
static int  s_demoCategory[SRGUI_MAX_DEMOS];
static int  s_demoSizeBytes[SRGUI_MAX_DEMOS];
static int  s_demoDurationMs[SRGUI_MAX_DEMOS];
static int  s_demoMapCount[SRGUI_MAX_DEMOS];
static int  s_demoMetaState[SRGUI_MAX_DEMOS];
static int  s_demoMTime[SRGUI_MAX_DEMOS];
static int  s_demoCount = 0;
static int  s_demoSelected = -1;
static int  s_demoFilter = -1;
static int  s_demoMetaScanCursor = 0;
static int  s_demoSortColumn = SRGUI_DEMO_SORT_NEWEST;
static bool s_demoSortAscending = true;
static int  s_demoPage = 0;
static char s_demoSearch[64] = "";
static int  s_demoListRevision = 1;
static int  s_demoMetaRevision = 1;
static int  s_demoSortedCache[SRGUI_MAX_DEMOS];
static int  s_demoSortedCount = 0;
static int  s_demoSortedListRevision = -1;
static int  s_demoSortedMetaRevision = -1;
static int  s_demoSortedSortColumn = -1;
static int  s_demoSortedFilter = -2;
static int  s_demoSortedCountKey = -1;
static bool s_demoSortedAscending = true;
static char s_demoSortedSearch[64] = "";

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
	SRGUI_SETTING_INPUT_INT,
	SRGUI_SETTING_COLOR,
	SRGUI_SETTING_COMMAND,
	SRGUI_SETTING_PAGE
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

#define SRGUI_BOOL( label, name, def, tags, cat ) { label, name, def, tags, cat, SRGUI_SETTING_BOOL, 0.0f, 0.0f, NULL }
#define SRGUI_FLOAT( label, name, def, tags, cat, minv, maxv ) { label, name, def, tags, cat, SRGUI_SETTING_FLOAT, minv, maxv, NULL }
#define SRGUI_INT( label, name, def, tags, cat, minv, maxv ) { label, name, def, tags, cat, SRGUI_SETTING_INT, (float)( minv ), (float)( maxv ), NULL }
#define SRGUI_INPUT_INT( label, name, def, tags, cat, minv, maxv ) { label, name, def, tags, cat, SRGUI_SETTING_INPUT_INT, (float)( minv ), (float)( maxv ), NULL }
#define SRGUI_COLOR( label, name, def, tags, cat ) { label, name, def, tags, cat, SRGUI_SETTING_COLOR, 0.0f, 0.0f, NULL }
#define SRGUI_COMMAND( label, command, tags, cat ) { label, command, "", tags, cat, SRGUI_SETTING_COMMAND, 0.0f, 0.0f, command }
#define SRGUI_PAGE( label, name, tags, cat ) { label, name, "", tags, cat, SRGUI_SETTING_PAGE, 0.0f, 0.0f, NULL }

static const srGuiSettingEntry_t s_settingEntries[] = {
	SRGUI_BOOL( "Enable Timer", "cg_livesplit", "0", "timer run livesplit wlacz", 0 ),
	SRGUI_BOOL( "Show LiveSplit Panel", "ls_draw", "1", "overlay panel splits hud", 0 ),
	SRGUI_PAGE( "LiveSplit Type", "ls_type", "timer external in-game typ combo", 0 ),
	SRGUI_PAGE( "Run Mode", "ls_mode", "full game chapter individual level IL tryb", 0 ),
	SRGUI_PAGE( "Chapter", "ls_mission", "mission chapter rozdzial", 0 ),
	SRGUI_PAGE( "IL Map", "ls_map", "individual level map escape castle tram norway boss", 0 ),
	SRGUI_BOOL( "100% Category", "ls_100pct", "0", "100 percent all secrets treasure", 0 ),
	SRGUI_PAGE( "Compare Against", "ls_compare", "personal best segments average compare", 0 ),
	SRGUI_PAGE( "Timing Method", "ls_timing", "game time real time timing czas", 0 ),
	SRGUI_BOOL( "Timer Decimals", "sp_timer_decimals", "1", "timer decimals fractional values", 0 ),
	SRGUI_BOOL( "Auto-Record Demos", "sp_autorecord", "0", "demo record automatic nagrywanie", 0 ),
	SRGUI_COMMAND( "Open Race Control", "ls_race_open", "race lobby ghosts hud overlay control", 11 ),

	SRGUI_PAGE( "Reset LiveSplit Style", "reset_livesplit_style", "style reset preset default wyglad", 2 ),
	SRGUI_PAGE( "LiveSplit Font", "ls_imgui_font", "font czcionka arial black segoe consolas", 2 ),
	SRGUI_BOOL( "Panel Border", "ls_imgui_show_border", "1", "style border ramka", 2 ),
	SRGUI_BOOL( "Header Background", "ls_imgui_header_bg", "0", "style header title background", 2 ),
	SRGUI_BOOL( "Panel Gradient", "ls_imgui_gradient", "0", "style gradient background", 2 ),
	SRGUI_FLOAT( "Panel Width", "ls_w", "215", "style width szerokosc", 2, 80.0f, 400.0f ),
	SRGUI_FLOAT( "Global Scale", "ls_scale", "0.550000", "style scale size", 2, 0.5f, 2.5f ),
	SRGUI_FLOAT( "Padding", "ls_imgui_padding", "5", "style padding odstęp", 2, 3.0f, 14.0f ),
	SRGUI_FLOAT( "Component Gap", "ls_imgui_component_gap", "4", "style gap spacing", 2, 0.0f, 12.0f ),
	SRGUI_FLOAT( "Border Thickness", "ls_imgui_border_size", "0.500000", "style border thickness", 2, 0.5f, 4.0f ),
	SRGUI_FLOAT( "Gradient Angle DEG", "ls_imgui_gradient_angle", "230", "style gradient angle", 2, 0.0f, 360.0f ),
	SRGUI_BOOL( "Gradient on text", "ls_imgui_text_gradient", "0", "style text gradient", 2 ),
	SRGUI_FLOAT( "Text Gradient Angle DEG", "ls_imgui_text_gradient_angle", "0", "style text gradient angle", 2, 0.0f, 360.0f ),
	SRGUI_BOOL( "Title", "ls_imgui_show_title", "0", "style title game header", 2 ),
	SRGUI_BOOL( "Attempt Counter", "ls_showatt", "1", "style attempts counter", 2 ),
	SRGUI_BOOL( "Bold title/category", "ls_imgui_bold_title", "1", "style bold title category", 2 ),
	SRGUI_BOOL( "Bold attempts", "ls_imgui_bold_attempts", "1", "style bold attempts", 2 ),
	SRGUI_BOOL( "LIVE / READY / DONE", "ls_imgui_show_status", "0", "style status chip live ready done", 2 ),
	SRGUI_BOOL( "Bold status text", "ls_imgui_bold_status", "0", "style bold status", 2 ),
	SRGUI_BOOL( "Separators", "ls_showseps", "1", "style separators lines", 2 ),
	SRGUI_BOOL( "Bold column labels", "ls_imgui_bold_header", "1", "style columns labels bold", 2 ),
	SRGUI_BOOL( "Stage PB", "ls_showpb", "1", "style personal best pb", 2 ),
	SRGUI_BOOL( "Stage BEST", "ls_showbest", "1", "style best segments", 2 ),
	SRGUI_BOOL( "Bold IGT", "ls_imgui_bold_timer", "1", "style bold timer igt", 2 ),
	SRGUI_BOOL( "Bold stage", "ls_imgui_bold_stage", "1", "style bold stage", 2 ),
	SRGUI_BOOL( "Bold PB/BEST", "ls_imgui_bold_info", "1", "style bold pb best", 2 ),
	SRGUI_FLOAT( "IGT Size", "ls_imgui_timer_size", "1.630000", "style timer igt size", 2, 0.75f, 2.00f ),
	SRGUI_FLOAT( "Stage Size", "ls_imgui_stage_size", "1.450000", "style stage size", 2, 0.55f, 1.60f ),
	SRGUI_FLOAT( "PB/BEST Size", "ls_imgui_info_size", "0.650000", "style pb best size", 2, 0.55f, 1.25f ),
	SRGUI_FLOAT( "PB/BEST horizontal gap", "ls_imgui_info_gap", "24", "style pb best gap", 2, 24.0f, 120.0f ),
	SRGUI_FLOAT( "Timer / stats gap", "ls_imgui_timer_gap", "2", "style timer stats gap", 2, 2.0f, 24.0f ),
	SRGUI_FLOAT( "PB/BEST side position", "ls_imgui_timer_split", "0.400000", "style pb best side split", 2, 0.40f, 0.76f ),
	SRGUI_BOOL( "PB Delta (+/-)", "ls_showdeltas", "1", "style delta personal best", 2 ),
	SRGUI_BOOL( "Best Delta (+/-)", "ls_showbestdeltas", "1", "style delta best", 2 ),
	SRGUI_BOOL( "Current Row Background", "ls_imgui_current_bg", "1", "style current row background highlight", 2 ),
	SRGUI_BOOL( "Rainbow gold BEST +/-", "ls_imgui_gold_rainbow", "1", "style rainbow gold delta", 2 ),
	SRGUI_BOOL( "Bold all split rows", "ls_imgui_bold_splits", "0", "style bold splits rows", 2 ),
	SRGUI_BOOL( "Bold stage names", "ls_imgui_bold_split_name", "0", "style bold stage split names", 2 ),
	SRGUI_BOOL( "Bold BEST +/-", "ls_imgui_bold_split_best", "1", "style bold best delta", 2 ),
	SRGUI_BOOL( "Bold +/-", "ls_imgui_bold_split_delta", "1", "style bold delta", 2 ),
	SRGUI_BOOL( "Bold split time", "ls_imgui_bold_split_time", "1", "style bold split time", 2 ),
	SRGUI_FLOAT( "Row Size", "ls_imgui_row_size", "0.88", "style split row size", 2, 0.75f, 1.45f ),
	SRGUI_FLOAT( "Map Name Font", "ls_imgui_name_size", "0.92", "style map name font size", 2, 0.65f, 1.60f ),
	SRGUI_FLOAT( "BEST +/- Font", "ls_imgui_bestdelta_size", "0.920000", "style best delta font", 2, 0.65f, 1.60f ),
	SRGUI_FLOAT( "+/- Font", "ls_imgui_delta_size", "0.88", "style delta font", 2, 0.65f, 1.60f ),
	SRGUI_FLOAT( "Time Font", "ls_imgui_time_size", "0.94", "style time font", 2, 0.65f, 1.60f ),
	SRGUI_FLOAT( "Stage Name Width", "ls_imgui_col_name", "0.347525", "style column name width", 2, 0.22f, 0.78f ),
	SRGUI_FLOAT( "BEST +/- Width", "ls_imgui_col_best", "0.574001", "style column best width", 2, 0.34f, 0.88f ),
	SRGUI_FLOAT( "+/- Width", "ls_imgui_col_delta", "0.763122", "style column delta width", 2, 0.44f, 0.94f ),
	SRGUI_BOOL( "Ghost Segment", "ls_imgui_show_ghostseg", "0", "style ghost segment", 2 ),
	SRGUI_BOOL( "Bold ghost", "ls_imgui_bold_ghost", "0", "style bold ghost", 2 ),
	SRGUI_BOOL( "Previous Segment", "ls_imgui_show_prevseg", "1", "style previous segment", 2 ),
	SRGUI_BOOL( "Bold all statistics", "ls_imgui_bold_stats", "0", "style bold stats statistics", 2 ),
	SRGUI_BOOL( "Bold all statistic values", "ls_imgui_bold_stats_values", "0", "style bold stats values", 2 ),
	SRGUI_BOOL( "Show Sum of Best row", "ls_imgui_show_sob", "1", "style sum of best sob row", 2 ),
	SRGUI_BOOL( "Bold Sum of Best label", "ls_imgui_bold_stat_sob_label", "0", "style sob label bold", 2 ),
	SRGUI_BOOL( "Bold Sum of Best value", "ls_imgui_bold_stat_sob_value", "1", "style sob value bold", 2 ),
	SRGUI_BOOL( "Show Possible Save row", "ls_imgui_show_possible_save", "1", "style possible save row", 2 ),
	SRGUI_BOOL( "Bold Possible Save label", "ls_imgui_bold_stat_possible_label", "0", "style possible save label bold", 2 ),
	SRGUI_BOOL( "Bold Possible Save value", "ls_imgui_bold_stat_possible_value", "1", "style possible save value bold", 2 ),
	SRGUI_BOOL( "Show Best Possible row", "ls_imgui_show_best_possible", "1", "style best possible row", 2 ),
	SRGUI_BOOL( "Bold Best Possible label", "ls_imgui_bold_stat_best_label", "0", "style best possible label bold", 2 ),
	SRGUI_BOOL( "Bold Best Possible value", "ls_imgui_bold_stat_best_value", "1", "style best possible value bold", 2 ),
	SRGUI_BOOL( "Rainbow when gold", "ls_imgui_prev_gold_rainbow", "0", "style previous gold rainbow", 2 ),
	SRGUI_BOOL( "Bold previous label", "ls_imgui_bold_prev_label", "0", "style previous label bold", 2 ),
	SRGUI_BOOL( "Bold previous value", "ls_imgui_bold_prev_value", "1", "style previous value bold", 2 ),
	SRGUI_BOOL( "RGT", "ls_showrgt", "1", "style real game time rgt", 2 ),
	SRGUI_BOOL( "Best Segments", "ls_imgui_show_bestsegments", "0", "style best segments", 2 ),
	SRGUI_BOOL( "Bold RGT", "ls_imgui_bold_rgt", "1", "style bold rgt", 2 ),
	SRGUI_FLOAT( "RGT Size", "ls_imgui_rgt_size", "1.250000", "style rgt size", 2, 0.65f, 1.60f ),
	SRGUI_INT( "Max Visible Splits", "ls_maxrows", "6", "style max rows visible splits", 2, 0, 20 ),
	SRGUI_PAGE( "Content Side", "ls_align", "style alignment side left center right", 2 ),
	SRGUI_FLOAT( "Panel X Position", "ls_x", "6.666843", "style position x layout", 2, 0.0f, 580.0f ),
	SRGUI_FLOAT( "Panel Y Position", "ls_y", "92.444427", "style position y layout", 2, 0.0f, 440.0f ),
	SRGUI_FLOAT( "Splits Opacity", "ls_opacity", "0.900000", "style opacity alpha splits", 2, 0.0f, 1.0f ),
	SRGUI_FLOAT( "Splits In Menu", "ls_opacity_ui", "0.900000", "style menu opacity alpha", 2, 0.0f, 1.0f ),
	SRGUI_BOOL( "Text Shadow", "ls_text_shadow", "1", "style shadow text", 2 ),
	SRGUI_FLOAT( "GUI Window Opacity", "ui_speedrun_imgui_alpha", "0.96", "gui window opacity alpha", 2, 0.70f, 1.0f ),
	SRGUI_FLOAT( "GUI Card Opacity", "ui_speedrun_imgui_card_alpha", "0.92", "gui card opacity alpha", 2, 0.35f, 1.0f ),
	SRGUI_BOOL( "GUI Rounded Corners", "ui_speedrun_imgui_rounding", "1", "gui rounded corners", 2 ),
	SRGUI_BOOL( "GUI Animations", "ui_speedrun_imgui_animations", "1", "gui animations animacje", 2 ),
	SRGUI_PAGE( "Reset GUI Window", "gui_reset_window", "gui reset window position size", 2 ),
	SRGUI_PAGE( "Reset GUI Style", "gui_reset_style", "gui reset style colors", 2 ),
	SRGUI_COLOR( "Color: Ahead", "ls_clr_ahead", "72 220 80 1.00", "style color ahead green", 2 ),
	SRGUI_COLOR( "Color: Behind", "ls_clr_behind", "220 72 72 1.00", "style color behind red", 2 ),
	SRGUI_COLOR( "Color: Gold", "ls_clr_gold", "255 220 50 1.00", "style color gold best", 2 ),
	SRGUI_COLOR( "Color: Header", "ls_clr_header", "90 210 58 1.00", "style color header", 2 ),
	SRGUI_COLOR( "Color: Timer", "ls_clr_timer", "224 246 214 1.00", "style color timer igt", 2 ),
	SRGUI_COLOR( "Color: Text", "ls_clr_text", "214 224 210 0.92", "style color text", 2 ),
	SRGUI_COLOR( "Color: Paused", "ls_clr_paused", "255 191 64 1.00", "style color paused", 2 ),
	SRGUI_COLOR( "Color: Background", "ls_clr_bg", "10 10 15 0.63", "style color background gradient top", 2 ),
	SRGUI_COLOR( "Color: Background 2", "ls_clr_bg2", "10 10 15 0.90", "style color gradient bottom", 2 ),
	SRGUI_COLOR( "Color: Border", "ls_clr_border", "29 52 24 0.58", "style color border", 2 ),
	SRGUI_COLOR( "Color: Map Name", "ls_clr_mapname", "140 158 128 0.85", "style color map name", 2 ),
	SRGUI_COLOR( "Color: Current Split", "ls_clr_current", "86 210 55 1.00", "style color current split", 2 ),
	SRGUI_COLOR( "Color: Completed Split", "ls_clr_completed", "120 130 118 0.62", "style color completed split", 2 ),
	SRGUI_COLOR( "Color: Future Split", "ls_clr_future", "124 124 124 1.00", "style color future split", 2 ),
	SRGUI_COLOR( "Color: Time Column All", "ls_clr_split_time", "218 226 214 0.92", "style color time column", 2 ),
	SRGUI_COLOR( "Color: Time Column Current", "ls_clr_split_time_current", "224 246 214 1.00", "style color current time column", 2 ),
	SRGUI_COLOR( "Color: Time Column Completed", "ls_clr_split_time_completed", "150 160 146 0.72", "style color completed time column", 2 ),
	SRGUI_COLOR( "Color: Dim", "ls_clr_dim", "82 92 76 0.70", "style color dim muted", 2 ),
	SRGUI_COLOR( "Color: Segment Timer", "ls_clr_segtimer", "178 190 172 0.84", "style color segment timer", 2 ),
	SRGUI_COLOR( "Color: Separator", "ls_clr_sep", "22 36 18 0.34", "style color separator line", 2 ),
	SRGUI_COLOR( "Color: Highlight", "ls_clr_highlight", "13 28 12 0.27", "style color highlight current row", 2 ),
	SRGUI_COLOR( "Color: Label", "ls_clr_label", "140 158 128 0.85", "style color label", 2 ),
	SRGUI_COLOR( "Color: Text Gradient End", "ls_clr_text_gradient2", "255 255 255 0.59", "style color text gradient", 2 ),
	SRGUI_COLOR( "Color: Header Background", "ls_clr_header_bg", "10 18 12 0.68", "style color header background", 2 ),
	SRGUI_COLOR( "Color: Game Title", "ls_clr_title", "90 210 58 1.00", "style color title", 2 ),
	SRGUI_COLOR( "Color: Category / Attempt", "ls_clr_category", "132 158 120 0.88", "style color category attempt", 2 ),
	SRGUI_COLOR( "Color: LIVE Chip", "ls_clr_status_live", "84 205 55 1.00", "style color status live", 2 ),
	SRGUI_COLOR( "Color: READY Chip", "ls_clr_status_ready", "80 92 76 0.90", "style color status ready", 2 ),
	SRGUI_COLOR( "Color: PAUSE Chip", "ls_clr_status_pause", "255 191 64 1.00", "style color status pause", 2 ),
	SRGUI_COLOR( "Color: DONE Chip", "ls_clr_status_done", "64 217 64 1.00", "style color status done", 2 ),
	SRGUI_COLOR( "Color: Chip Text", "ls_clr_status_text", "6 10 6 0.95", "style color status text", 2 ),
	SRGUI_COLOR( "Color: Column Labels", "ls_clr_column_label", "128 142 118 0.68", "style color column labels", 2 ),
	SRGUI_COLOR( "Color: Stage Timer", "ls_clr_stage_timer", "178 190 172 0.84", "style color stage timer", 2 ),
	SRGUI_COLOR( "Color: PB Label", "ls_clr_pb_label", "128 142 118 0.68", "style color pb label", 2 ),
	SRGUI_COLOR( "Color: PB Value", "ls_clr_pb_value", "218 226 214 0.92", "style color pb value", 2 ),
	SRGUI_COLOR( "Color: BEST Label", "ls_clr_best_label", "128 142 118 0.68", "style color best label", 2 ),
	SRGUI_COLOR( "Color: BEST Value", "ls_clr_best_value", "255 220 50 1.00", "style color best value", 2 ),
	SRGUI_COLOR( "Color: Ghost Label", "ls_clr_ghost_label", "128 142 118 0.68", "style color ghost label", 2 ),
	SRGUI_COLOR( "Color: Ghost Time", "ls_clr_ghost_time", "178 190 172 0.84", "style color ghost time", 2 ),
	SRGUI_COLOR( "Color: Statistics Labels", "ls_clr_stat_label", "128 142 118 0.68", "style color statistics labels", 2 ),
	SRGUI_COLOR( "Color: Sum of Best Label", "ls_clr_stat_sob_label", "128 142 118 0.68", "style color sob label", 2 ),
	SRGUI_COLOR( "Color: Sum of Best Value", "ls_clr_stat_sob", "255 220 50 1.00", "style color sob value", 2 ),
	SRGUI_COLOR( "Color: Possible Save Label", "ls_clr_stat_possible_label", "128 142 118 0.68", "style color possible save label", 2 ),
	SRGUI_COLOR( "Color: Possible Save Value", "ls_clr_stat_possible_save", "72 220 80 1.00", "style color possible save value", 2 ),
	SRGUI_COLOR( "Color: Possible Save Zero", "ls_clr_stat_possible_zero", "112 118 112 0.62", "style color possible save zero", 2 ),
	SRGUI_COLOR( "Color: Possible Save Missing", "ls_clr_stat_possible_missing", "82 92 76 0.70", "style color possible save missing", 2 ),
	SRGUI_COLOR( "Color: Best Possible Label", "ls_clr_stat_best_possible_label", "128 142 118 0.68", "style color best possible label", 2 ),
	SRGUI_COLOR( "Color: Best Possible Value", "ls_clr_stat_best_possible", "255 220 50 1.00", "style color best possible value", 2 ),
	SRGUI_COLOR( "Color: Previous Label", "ls_clr_prev_label", "128 142 118 0.68", "style color previous label", 2 ),
	SRGUI_COLOR( "Color: Previous Ahead", "ls_clr_prev_ahead", "72 220 80 1.00", "style color previous ahead", 2 ),
	SRGUI_COLOR( "Color: Previous Behind", "ls_clr_prev_behind", "220 72 72 1.00", "style color previous behind", 2 ),
	SRGUI_COLOR( "Color: Previous Gold", "ls_clr_prev_gold", "255 220 50 1.00", "style color previous gold", 2 ),
	SRGUI_COLOR( "Color: RGT", "ls_clr_rgt", "218 226 214 0.92", "style color rgt", 2 ),
	SRGUI_COLOR( "Color: Inactive / Empty", "ls_clr_empty", "112 118 112 0.48", "style color empty inactive", 2 ),
	SRGUI_COLOR( "GUI Accent", "ui_speedrun_imgui_accent", "92 210 54 1.00", "gui accent color", 2 ),
	SRGUI_COLOR( "GUI Accent Gold", "ui_speedrun_imgui_accent_alt", "244 188 62 1.00", "gui accent gold color", 2 ),

	SRGUI_BOOL( "Show Keystrokes", "cg_drawKeys", "1", "overlay keys input klawisze", 3 ),
	SRGUI_BOOL( "Only During Gameplay", "ks_ingame_only", "1", "overlay keys gameplay only", 3 ),
	SRGUI_PAGE( "Keys Layout", "ks_layout", "overlay keys layout classic horizontal compact mouse grid active", 3 ),
	SRGUI_PAGE( "Press Effect", "ks_effect", "overlay keys effect glow pulse", 3 ),
	SRGUI_FLOAT( "Keys X Position", "ks_x", "297", "overlay keys position x", 3, 0.0f, 600.0f ),
	SRGUI_FLOAT( "Keys Y Position", "ks_y", "375", "overlay keys position y", 3, 0.0f, 460.0f ),
	SRGUI_FLOAT( "Keys Scale", "ks_scale", "0.750000", "overlay keys scale size", 3, 0.3f, 4.0f ),
	SRGUI_FLOAT( "Keys Font Scale", "ks_font_scale", "0.620000", "overlay keys font scale", 3, 0.30f, 2.4f ),
	SRGUI_FLOAT( "Keys Box Width", "ks_box_w", "22", "overlay keys box width", 3, 14.0f, 80.0f ),
	SRGUI_FLOAT( "Keys Box Height", "ks_box_h", "18", "overlay keys box height", 3, 12.0f, 54.0f ),
	SRGUI_FLOAT( "Keys Gap", "ks_gap", "2", "overlay keys gap spacing", 3, 0.0f, 20.0f ),
	SRGUI_FLOAT( "Keys Border Size", "ks_border_size", "1", "overlay keys border size", 3, 0.0f, 5.0f ),
	SRGUI_FLOAT( "Mouse Grid Size", "ks_mouse_grid_size", "92", "overlay mouse grid size", 3, 48.0f, 220.0f ),
	SRGUI_INT( "Mouse Grid Squares", "ks_mouse_grid_cells", "5", "overlay mouse grid cells squares", 3, 3, 12 ),
	SRGUI_BOOL( "Mouse Total CM Counter", "ks_mouse_grid_cm", "1", "overlay mouse cm total counter", 3 ),
	SRGUI_BOOL( "Mouse Run CM Counter", "ks_mouse_grid_run_cm", "1", "overlay mouse cm run counter", 3 ),
	SRGUI_FLOAT( "Keys Opacity", "ks_opacity", "1.0", "overlay keys opacity alpha", 3, 0.0f, 1.0f ),
	SRGUI_PAGE( "Mouse Display", "ks_mouse", "overlay mouse display clicks direction", 3 ),
	SRGUI_PAGE( "Active Snap Side", "ks_active_anchor", "overlay active snap side anchor", 3 ),
	SRGUI_INT( "Max Active Keys", "ks_active_max", "3", "overlay active max keys", 3, 1, 10 ),
	SRGUI_BOOL( "Show Grid Buttons", "ks_grid_keys", "0", "overlay mouse grid buttons", 3 ),
	SRGUI_PAGE( "Grid Button Direction", "ks_grid_keys_dir", "overlay grid button direction", 3 ),
	SRGUI_FLOAT( "Grid Buttons X", "ks_grid_keys_x", "-35", "overlay grid buttons x", 3, -110.0f, 110.0f ),
	SRGUI_FLOAT( "Grid Buttons Y", "ks_grid_keys_y", "-15", "overlay grid buttons y", 3, -110.0f, 110.0f ),
	SRGUI_FLOAT( "Grid Buttons Font", "ks_grid_keys_font_scale", "0.72", "overlay grid buttons font", 3, 0.30f, 1.20f ),
	SRGUI_BOOL( "Show Use", "ks_show_use", "0", "overlay keys use", 3 ),
	SRGUI_BOOL( "Show Reload", "ks_show_reload", "0", "overlay keys reload", 3 ),
	SRGUI_COLOR( "Keys Idle Background", "ks_clr_bg", "10 10 15 0.72", "overlay keys color idle background", 3 ),
	SRGUI_COLOR( "Keys Active Background", "ks_clr_active", "26 61 18 0.88", "overlay keys color active background", 3 ),
	SRGUI_COLOR( "Keys Idle Border", "ks_clr_border", "51 64 46 0.30", "overlay keys color idle border", 3 ),
	SRGUI_COLOR( "Keys Active Border", "ks_clr_active_border", "107 191 56 0.85", "overlay keys color active border", 3 ),
	SRGUI_COLOR( "Keys Idle Text", "ks_clr_text", "128 143 122 0.78", "overlay keys color idle text", 3 ),
	SRGUI_COLOR( "Keys Active Text", "ks_clr_active_text", "219 247 184 1.00", "overlay keys color active text", 3 ),
	SRGUI_COLOR( "Mouse checker squares", "ks_clr_grid_checker", "20 31 20 0.42", "overlay mouse grid checker color", 3 ),
	SRGUI_COLOR( "Mouse center cross", "ks_clr_grid_cross", "235 219 89 0.72", "overlay mouse grid cross color", 3 ),
	SRGUI_COLOR( "Mouse trail", "ks_clr_grid_trail", "140 242 77 0.90", "overlay mouse trail color", 3 ),
	SRGUI_COLOR( "Mouse CM counter", "ks_clr_grid_cm", "217 242 179 0.88", "overlay mouse cm counter color", 3 ),
	SRGUI_BOOL( "Speedometer", "cg_drawVelocity", "1", "overlay speedometer velocity predkosc", 3 ),
	SRGUI_BOOL( "Position HUD", "cg_drawPos", "0", "overlay position pos coordinates", 3 ),
	SRGUI_BOOL( "Jump Statistics", "cg_drawJumpStats", "0", "overlay jump stats", 3 ),
	SRGUI_BOOL( "Strafe Guide", "cg_strafeGuide", "0", "overlay strafe guide", 3 ),
	SRGUI_BOOL( "Show FPS", "cg_drawfps", "0", "overlay fps counter", 3 ),
	SRGUI_BOOL( "Show Timer", "cg_drawTimer", "0", "overlay timer", 3 ),
	SRGUI_BOOL( "Show IGT Timer", "ls_igttimer", "0", "overlay standalone igt timer", 3 ),
	SRGUI_PAGE( "IGT Align", "ls_igttimer_align", "overlay igt align left center right", 3 ),
	SRGUI_FLOAT( "IGT X Position", "ls_igttimer_x", "638", "overlay igt position x", 3, 0.0f, 640.0f ),
	SRGUI_FLOAT( "IGT Y Position", "ls_igttimer_y", "240", "overlay igt position y", 3, 0.0f, 480.0f ),
	SRGUI_FLOAT( "IGT Scale", "ls_igttimer_scale", "1.0", "overlay igt scale", 3, 0.3f, 4.0f ),
	SRGUI_BOOL( "Show IGT Segment Timer", "ls_igtsegtimer", "0", "overlay igt segment timer", 3 ),
	SRGUI_PAGE( "Speedometer Mode", "cg_velocity_mode", "overlay speedometer mode 3d horizontal vertical", 3 ),
	SRGUI_PAGE( "Speedometer Text Size", "cg_velocity_size", "overlay speedometer text size", 3 ),
	SRGUI_PAGE( "Speedometer Position", "cg_velocity_type", "overlay speedometer position bottom center", 3 ),
	SRGUI_PAGE( "Speedometer Text Align", "cg_velocity_align", "overlay speedometer text align", 3 ),
	SRGUI_BOOL( "Speed Change Color Fade", "cg_velocity_colorfade", "0", "overlay speedometer color fade", 3 ),
	SRGUI_BOOL( "Peak Speed Above", "cg_velocity_peak", "0", "overlay speedometer peak", 3 ),
	SRGUI_FLOAT( "Peak Reset Speed", "cg_velocity_peak_reset", "8", "overlay speedometer peak reset", 3, 1.0f, 80.0f ),
	SRGUI_FLOAT( "Speedometer Scale", "cg_velocity_scale", "1.0", "overlay speedometer scale", 3, 0.25f, 4.0f ),
	SRGUI_FLOAT( "Speedometer X Position", "cg_velocity_x", "320", "overlay speedometer position x", 3, 0.0f, 640.0f ),
	SRGUI_FLOAT( "Speedometer Y Position", "cg_velocity_y", "457", "overlay speedometer position y", 3, 0.0f, 480.0f ),
	SRGUI_FLOAT( "FPS Scale", "cg_fpsScale", "1.0", "overlay fps scale", 3, 0.25f, 4.0f ),
	SRGUI_FLOAT( "FPS X Position", "cg_fpsX", "500", "overlay fps position x", 3, 0.0f, 640.0f ),
	SRGUI_FLOAT( "FPS Y Position", "cg_fpsY", "0", "overlay fps position y", 3, 0.0f, 440.0f ),

	SRGUI_BOOL( "HL1 Bhop Physics", "bh_movement", "0", "game bhop movement physics", 4 ),
	SRGUI_BOOL( "Auto Jump", "bh_autojump", "0", "game bhop auto jump", 4 ),
	SRGUI_FLOAT( "FOV Front-Back", "cg_fov", "90", "game fov field view front back", 4, 60.0f, 160.0f ),
	SRGUI_FLOAT( "FOV Down-Up", "cg_fov_down", "90", "game fov down up", 4, 60.0f, 160.0f ),
	SRGUI_FLOAT( "FOV Left-Right", "cg_fov_lr", "90", "game fov left right", 4, 0.0f, 160.0f ),
	SRGUI_BOOL( "Black Sidebars", "cg_blackbars", "0", "game viewport black bars sidebars safe area gui", 4 ),
	SRGUI_INT( "Left Black Bar", "cg_blackbarLeft", "0", "game viewport left black bar pixels", 4, 0, 4096 ),
	SRGUI_INT( "Right Black Bar", "cg_blackbarRight", "0", "game viewport right black bar pixels", 4, 0, 4096 ),
	SRGUI_COLOR( "Sidebar Color", "cg_blackbarColor", "0 0 0 1.00", "game viewport sidebars color safe area", 4 ),
	SRGUI_PAGE( "Max FPS", "com_maxfps", "game fps max framerate", 4 ),
	SRGUI_PAGE( "Weapon Hand", "cg_drawGun", "game weapon hand gun left right hidden", 4 ),
	SRGUI_PAGE( "Weapon Render", "cg_weapon_color_mode", "game weapon render color rainbow flat xray pulse", 4 ),
	SRGUI_COLOR( "Weapon Tint Color", "cg_weapon_color", "26 191 255 1.00", "game weapon tint color", 4 ),
	SRGUI_FLOAT( "Weapon Tint Opacity", "cg_weapon_color_opacity", "0.35", "game weapon tint opacity alpha", 4, 0.0f, 1.0f ),
	SRGUI_FLOAT( "Weapon X-Ray Strength", "cg_weapon_xray_strength", "0.65", "game weapon xray strength", 4, 0.0f, 1.0f ),
	SRGUI_FLOAT( "Weapon Rainbow Speed", "cg_weapon_rainbow_speed", "1.0", "game weapon rainbow speed", 4, 0.1f, 5.0f ),
	SRGUI_BOOL( "Show Ghost", "ls_ghost", "0", "game ghost replay", 4 ),
	SRGUI_INT( "Ghost Opacity", "ls_ghost_opacity", "60", "game ghost opacity alpha", 4, 5, 255 ),

	SRGUI_BOOL( "Show Grid", "ui_speedrun_grid", "0", "tools grid layout siatka", 5 ),
	SRGUI_BOOL( "HUD Edit Mode", "ui_speedrun_layout_edit", "0", "tools hud edit drag layout", 5 ),
	SRGUI_BOOL( "Show Labels", "ui_speedrun_grid_labels", "1", "tools grid labels", 5 ),
	SRGUI_BOOL( "Center Lines", "ui_speedrun_grid_center", "1", "tools grid center lines", 5 ),
	SRGUI_FLOAT( "Grid Size", "ui_speedrun_grid_size", "32", "tools grid size", 5, 8.0f, 160.0f ),
	SRGUI_INT( "Major Every", "ui_speedrun_grid_major", "5", "tools grid major every", 5, 2, 10 ),
	SRGUI_FLOAT( "Grid Opacity", "ui_speedrun_grid_opacity", "0.22", "tools grid opacity alpha", 5, 0.02f, 0.75f ),
	SRGUI_PAGE( "Reset HUD positions", "reset_hud_positions", "tools reset hud positions layout", 5 ),
	SRGUI_PAGE( "Custom Split Names", "split_names", "tools custom split names maps", 5 ),
	SRGUI_PAGE( "Split Name: Escape!", "ls_name_escape1", "tools split name map escape1", 5 ),
	SRGUI_PAGE( "Split Name: Castle Keep", "ls_name_escape2", "tools split name map escape2", 5 ),
	SRGUI_PAGE( "Split Name: Tram Ride", "ls_name_tram", "tools split name map tram", 5 ),
	SRGUI_PAGE( "Split Name: Village", "ls_name_village1", "tools split name map village1", 5 ),
	SRGUI_PAGE( "Split Name: Catacombs", "ls_name_crypt1", "tools split name map crypt1", 5 ),
	SRGUI_PAGE( "Split Name: Crypt", "ls_name_crypt2", "tools split name map crypt2", 5 ),
	SRGUI_PAGE( "Split Name: Church", "ls_name_church", "tools split name map church", 5 ),
	SRGUI_PAGE( "Split Name: Tomb", "ls_name_boss1", "tools split name map boss1", 5 ),
	SRGUI_PAGE( "Split Name: Forest Compound", "ls_name_forest", "tools split name map forest", 5 ),
	SRGUI_PAGE( "Split Name: Rocket Base", "ls_name_rocket", "tools split name map rocket", 5 ),
	SRGUI_PAGE( "Split Name: Radar Installation", "ls_name_baseout", "tools split name map baseout", 5 ),
	SRGUI_PAGE( "Split Name: Air Base Assault", "ls_name_assault", "tools split name map assault", 5 ),
	SRGUI_PAGE( "Split Name: Kugelstadt", "ls_name_sfm", "tools split name map sfm", 5 ),
	SRGUI_PAGE( "Split Name: The Bombed Factory", "ls_name_factory", "tools split name map factory", 5 ),
	SRGUI_PAGE( "Split Name: The Trainyards", "ls_name_trainyard", "tools split name map trainyard", 5 ),
	SRGUI_PAGE( "Split Name: Secret Weapons Facility", "ls_name_swf", "tools split name map swf", 5 ),
	SRGUI_PAGE( "Split Name: Ice Station Norway", "ls_name_norway", "tools split name map norway", 5 ),
	SRGUI_PAGE( "Split Name: X-Labs", "ls_name_xlabs", "tools split name map xlabs", 5 ),
	SRGUI_PAGE( "Split Name: Super Soldier", "ls_name_boss2", "tools split name map boss2", 5 ),
	SRGUI_PAGE( "Split Name: Bramburg Dam", "ls_name_dam", "tools split name map dam", 5 ),
	SRGUI_PAGE( "Split Name: Paderborn Village", "ls_name_village2", "tools split name map village2", 5 ),
	SRGUI_PAGE( "Split Name: Chateau Schufstaffel", "ls_name_chateau", "tools split name map chateau", 5 ),
	SRGUI_PAGE( "Split Name: Unhallowed Ground", "ls_name_dark", "tools split name map dark", 5 ),
	SRGUI_PAGE( "Split Name: The Dig", "ls_name_dig", "tools split name map dig", 5 ),
	SRGUI_PAGE( "Split Name: Return to Castle Wolfenstein", "ls_name_castle", "tools split name map castle", 5 ),
	SRGUI_PAGE( "Split Name: Heinrich", "ls_name_end", "tools split name map end heinrich", 5 ),

	SRGUI_PAGE( "Bind LiveSplit Start", "bind_livesplit_start", "controls bind livesplit_start", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Pause / Resume", "bind_livesplit_pause", "controls bind livesplit_pause", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Undo Split", "bind_livesplit_undo", "controls bind livesplit_undo", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Skip Split", "bind_livesplit_skip", "controls bind livesplit_skip", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Reset", "bind_livesplit_reset", "controls bind livesplit_reset", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Reset No Save", "bind_livesplit_reset_nosave", "controls bind livesplit_reset_nosave", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Reset Category", "bind_livesplit_reset_category", "controls bind livesplit_reset_category", 6 ),
	SRGUI_PAGE( "Bind LiveSplit Check Settings", "bind_livesplit_check", "controls bind livesplit_check", 6 ),
	SRGUI_PAGE( "Bind Race Chat", "bind_ls_race_open_chat", "controls bind ls_race_open_chat race chat input show", 6 ),
	SRGUI_PAGE( "Bind Demo Pause", "bind_demo_pause", "controls bind demo_pause", 6 ),
	SRGUI_PAGE( "Bind Demo Speed Up", "bind_demo_speedup", "controls bind demo_speedup", 6 ),
	SRGUI_PAGE( "Bind Demo Slow Down", "bind_demo_slowdown", "controls bind demo_slowdown", 6 ),
	SRGUI_PAGE( "Bind Demo Skip Forward", "bind_demo_skipforward", "controls bind demo_skipforward", 6 ),
	SRGUI_PAGE( "Bind Demo Rewind", "bind_demo_skipbackward", "controls bind demo_skipbackward", 6 ),
	SRGUI_PAGE( "Bind Demo Step Frame", "bind_demo_stepframe", "controls bind demo_stepframe", 6 ),
	SRGUI_PAGE( "Bind Demo Step Back", "bind_demo_stepframeback", "controls bind demo_stepframeback", 6 ),
	SRGUI_PAGE( "Bind Demo Freecam", "bind_demo_freecam", "controls bind demo_freecam", 6 ),
	SRGUI_PAGE( "Bind Demo Next Map", "bind_demo_nextmap", "controls bind demo_nextmap", 6 ),
	SRGUI_PAGE( "Bind Demo Previous Map", "bind_demo_prevmap", "controls bind demo_prevmap", 6 ),
	SRGUI_PAGE( "Bind Demo Toggle HUD", "bind_demo_togglehud", "controls bind demo_togglehud", 6 ),
	SRGUI_PAGE( "Bind Demo Toggle Binds", "bind_demo_togglebinds", "controls bind demo_togglebinds", 6 ),
	SRGUI_PAGE( "Bind Practice Save Position", "bind_savepos", "controls bind savepos", 6 ),
	SRGUI_PAGE( "Bind Practice Load Position", "bind_loadpos", "controls bind loadpos", 6 ),
	SRGUI_PAGE( "Bind Practice Rewind", "bind_rewind", "controls bind rewind", 6 ),

	SRGUI_PAGE( "Records View Mode", "ls_mode_records", "records view mode full chapter individual", 7 ),
	SRGUI_PAGE( "Records Difficulty", "g_gameskill_records", "records difficulty skill", 7 ),
	SRGUI_PAGE( "Records Chapter", "ls_mission_records", "records chapter mission", 7 ),
	SRGUI_COMMAND( "Refresh Records", "livesplit_sv_refresh", "records refresh", 7 ),
	SRGUI_COMMAND( "Records Page Up", "livesplit_sv_pgup", "records page up", 7 ),
	SRGUI_COMMAND( "Records Page Down", "livesplit_sv_pgdn", "records page down", 7 ),
	SRGUI_PAGE( "Reset Row Gold", "records_reset_row_gold", "records reset selected row gold", 7 ),
	SRGUI_PAGE( "Reset Row PB Seg", "records_reset_row_pbseg", "records reset selected row pb segment", 7 ),
	SRGUI_PAGE( "Reset Row All", "records_reset_row_all", "records reset selected row all", 7 ),
	SRGUI_COMMAND( "Reset Records Category", "livesplit_sv_reset_cat", "records reset category", 7 ),

	SRGUI_PAGE( "Refresh Demos", "demos_refresh", "demos browser refresh", 8 ),
	SRGUI_PAGE( "Play Selected Demo", "demos_play_selected", "demos browser play selected", 8 ),
	SRGUI_COMMAND( "Open demos folder", "dir demos", "demos folder", 8 ),
	SRGUI_PAGE( "Demo Filter", "demos_filter", "demos filter full mission il other", 8 ),
	SRGUI_PAGE( "Demo Search", "demos_search", "demos search name", 8 ),
	SRGUI_COMMAND( "Demo Pause", "demo_pause", "demo playback pause resume", 8 ),
	SRGUI_COMMAND( "Demo Speed Up", "demo_speedup", "demo playback speed up", 8 ),
	SRGUI_COMMAND( "Demo Slow Down", "demo_slowdown", "demo playback slow down", 8 ),
	SRGUI_COMMAND( "Demo Skip Forward", "demo_skipforward", "demo playback skip forward", 8 ),
	SRGUI_COMMAND( "Demo Rewind", "demo_skipbackward", "demo playback rewind skip backward", 8 ),
	SRGUI_COMMAND( "Demo Freecam", "demo_freecam", "demo camera freecam", 8 ),
	SRGUI_COMMAND( "Demo Toggle HUD", "demo_togglehud", "demo hud toggle", 8 ),
	SRGUI_COMMAND( "Demo Step Frame", "demo_stepframe", "demo step frame", 8 ),
	SRGUI_COMMAND( "Demo Step Back", "demo_stepframeback", "demo step back", 8 ),
	SRGUI_COMMAND( "Demo Next Map", "demo_nextmap", "demo next map", 8 ),
	SRGUI_COMMAND( "Demo Previous Map", "demo_prevmap", "demo previous map", 8 ),

	SRGUI_COMMAND( "Start Run", "livesplit_start", "actions run timer start", 9 ),
	SRGUI_COMMAND( "Pause / Resume", "livesplit_pause", "actions timer pause resume", 9 ),
	SRGUI_COMMAND( "Check Settings", "livesplit_check", "actions validate speedrun settings", 9 ),
	SRGUI_COMMAND( "Undo Split", "livesplit_undo", "actions split undo", 9 ),
	SRGUI_COMMAND( "Skip Split", "livesplit_skip", "actions split skip", 9 ),
	SRGUI_COMMAND( "Reset Run", "livesplit_reset", "actions reset timer run", 9 ),
	SRGUI_COMMAND( "Reset No Save", "livesplit_reset_nosave", "actions reset no save", 9 ),
	SRGUI_COMMAND( "Reset Category", "livesplit_reset_category", "actions reset category", 9 ),
	SRGUI_COMMAND( "Save Position", "savepos", "actions practice save position", 9 ),
	SRGUI_COMMAND( "Load Position", "loadpos", "actions practice load position", 9 ),
	SRGUI_COMMAND( "Rewind Practice", "rewind", "actions practice rewind", 9 ),
	SRGUI_COMMAND( "Toggle Speedrun GUI", "speedrun_gui", "actions gui toggle settings", 9 ),
	SRGUI_COMMAND( "Dump LiveSplit Style", "speedrun_livesplit_dump", "actions style dump copy layout", 9 ),

	SRGUI_BOOL( "Zone Timer", "sp_zone_timer", "0", "zones timer strefy", 10 ),
	SRGUI_BOOL( "Draw Zones", "sp_zone_draw", "0", "zones draw render strefy", 10 ),
	SRGUI_BOOL( "Edit Mode", "sp_zone_edit", "0", "zones editor edit handles", 10 ),
	SRGUI_BOOL( "Progress X/X", "sp_zone_hud_progress", "1", "zones checkpoint progress", 10 ),
	SRGUI_PAGE( "Zone Route Name", "sp_zone_route_name", "zones route name trasa", 10 ),
	SRGUI_COMMAND( "Zone Add Start Look", "sp_zone_add_start", "zones create start look", 10 ),
	SRGUI_COMMAND( "Zone Add Start Here", "sp_zone_add_start here", "zones create start here", 10 ),
	SRGUI_COMMAND( "Zone CP Append Look", "sp_zone_add_checkpoint", "zones create checkpoint append look", 10 ),
	SRGUI_COMMAND( "Zone CP Append Here", "sp_zone_add_checkpoint here", "zones create checkpoint append here", 10 ),
	SRGUI_COMMAND( "Zone CP Insert Look", "sp_zone_insert_checkpoint", "zones create checkpoint insert look", 10 ),
	SRGUI_COMMAND( "Zone CP Insert Here", "sp_zone_insert_checkpoint here", "zones create checkpoint insert here", 10 ),
	SRGUI_COMMAND( "Zone Add Finish Look", "sp_zone_add_finish", "zones create finish look", 10 ),
	SRGUI_COMMAND( "Zone Add Finish Here", "sp_zone_add_finish here", "zones create finish here", 10 ),
	SRGUI_COMMAND( "Zone Add Race Look", "sp_zone_add_race", "zones create race look", 10 ),
	SRGUI_COMMAND( "Zone Add Race Here", "sp_zone_add_race here", "zones create race here", 10 ),
	SRGUI_COMMAND( "Zone Save", "sp_zone_save", "zones save", 10 ),
	SRGUI_COMMAND( "Zone Load", "sp_zone_load", "zones load", 10 ),
	SRGUI_COMMAND( "Zone Status", "sp_zone_status", "zones status", 10 ),
	SRGUI_COMMAND( "Zone Reset Run", "sp_zone_reset", "zones reset run", 10 ),
	SRGUI_COMMAND( "Zone Use Route", "sp_zone_route", "zones route selected use", 10 ),
	SRGUI_COMMAND( "Zone Previous", "sp_zone_prev", "zones selection previous", 10 ),
	SRGUI_COMMAND( "Zone Next", "sp_zone_next", "zones selection next", 10 ),
	SRGUI_COMMAND( "Zone Order Up", "sp_zone_order_up", "zones order up", 10 ),
	SRGUI_COMMAND( "Zone Order Down", "sp_zone_order_down", "zones order down", 10 ),
	SRGUI_COMMAND( "Zone Delete", "sp_zone_delete", "zones delete selected", 10 ),
	SRGUI_COMMAND( "Zone Clear All", "sp_zone_clear", "zones clear all", 10 ),
	SRGUI_COMMAND( "Zone Reset Time", "sp_zone_reset_times selected", "zones reset selected time pb", 10 ),
	SRGUI_COMMAND( "Zone Reset From", "sp_zone_reset_times from", "zones reset times from selected", 10 ),
	SRGUI_COMMAND( "Zone Reset Route", "sp_zone_reset_times route", "zones reset route times", 10 ),
	SRGUI_COMMAND( "Zone Set Start", "sp_zone_type start", "zones type start", 10 ),
	SRGUI_COMMAND( "Zone Set Checkpoint", "sp_zone_type checkpoint", "zones type checkpoint cp", 10 ),
	SRGUI_COMMAND( "Zone Set Finish", "sp_zone_type finish", "zones type finish", 10 ),
	SRGUI_COMMAND( "Zone Set Race", "sp_zone_type race", "zones type race objective", 10 ),
	SRGUI_COMMAND( "Zone Export Builtins", "sp_zone_export_builtins", "zones export builtin race", 10 ),
	SRGUI_COMMAND( "Zone Grow", "sp_zone_grow 8", "zones resize grow", 10 ),
	SRGUI_COMMAND( "Zone Shrink", "sp_zone_grow -8", "zones resize shrink", 10 ),
	SRGUI_COMMAND( "Zone Move Up", "sp_zone_move 0 0 8", "zones move up", 10 ),
	SRGUI_COMMAND( "Zone Move Down", "sp_zone_move 0 0 -8", "zones move down", 10 ),
	SRGUI_COMMAND( "Zone Yaw Left", "sp_zone_rotate yaw -15", "zones rotate yaw left", 10 ),
	SRGUI_COMMAND( "Zone Yaw Right", "sp_zone_rotate yaw 15", "zones rotate yaw right", 10 ),
	SRGUI_COMMAND( "Zone Pitch Down", "sp_zone_rotate pitch -15", "zones rotate pitch down", 10 ),
	SRGUI_COMMAND( "Zone Pitch Up", "sp_zone_rotate pitch 15", "zones rotate pitch up", 10 ),
	SRGUI_COMMAND( "Zone Roll Left", "sp_zone_rotate roll -15", "zones rotate roll left", 10 ),
	SRGUI_COMMAND( "Zone Roll Right", "sp_zone_rotate roll 15", "zones rotate roll right", 10 ),
	SRGUI_COMMAND( "Zone Face View", "sp_zone_rotate view", "zones rotate align view", 10 ),
	SRGUI_COMMAND( "Zone Reset Rotation", "sp_zone_rotate reset", "zones rotate reset", 10 ),
	SRGUI_BOOL( "Draw Start", "sp_zone_draw_start", "1", "zones draw start", 10 ),
	SRGUI_BOOL( "Draw CP", "sp_zone_draw_checkpoints", "1", "zones draw checkpoints cp", 10 ),
	SRGUI_BOOL( "Draw Finish", "sp_zone_draw_finish", "1", "zones draw finish", 10 ),
	SRGUI_BOOL( "Draw Race", "sp_zone_draw_race", "1", "zones draw race objectives", 10 ),
	SRGUI_BOOL( "3D Text", "sp_zone_draw_labels", "1", "zones labels text 3d", 10 ),
	SRGUI_BOOL( "Handles", "sp_zone_draw_handles", "1", "zones handles editor", 10 ),
	SRGUI_BOOL( "Rotation Gizmo", "sp_zone_rotation_gizmo", "1", "zones handles rotation gizmo", 10 ),
	SRGUI_BOOL( "Active Route Only", "sp_zone_draw_active_route_only", "0", "zones active route only", 10 ),
	SRGUI_BOOL( "Dim Other Routes", "sp_zone_dim_inactive", "1", "zones dim inactive routes", 10 ),
	SRGUI_BOOL( "Route Focus", "sp_zone_draw_run_target_only", "1", "zones route focus target only", 10 ),
	SRGUI_BOOL( "Auto Names", "sp_zone_auto_names", "1", "zones auto names", 10 ),
	SRGUI_COLOR( "Zone Start Color", "sp_zone_start_color", "82 255 112 1.00", "zones start color kolor", 10 ),
	SRGUI_COLOR( "Zone CP Color", "sp_zone_color", "82 184 255 1.00", "zones checkpoint color kolor", 10 ),
	SRGUI_COLOR( "Zone Finish Color", "sp_zone_finish_color", "255 108 86 1.00", "zones finish color kolor", 10 ),
	SRGUI_INT( "Zone Opacity", "sp_zone_opacity", "75", "zones opacity alpha", 10, 5, 255 ),
	SRGUI_INT( "Zone Border Alpha", "sp_zone_border_alpha", "230", "zones border alpha", 10, 20, 255 ),
	SRGUI_FLOAT( "Zone Border Width", "sp_zone_border_width", "0.75", "zones border width", 10, 0.25f, 6.0f ),
	SRGUI_INT( "Zone Inactive Alpha", "sp_zone_inactive_alpha", "28", "zones inactive alpha", 10, 0, 255 ),
	SRGUI_FLOAT( "Zone Handle Size", "sp_zone_handle_size", "6", "zones handle size", 10, 2.0f, 24.0f ),
	SRGUI_FLOAT( "Zone Handle Distance", "sp_zone_handle_max_dist", "1200", "zones handle distance", 10, 128.0f, 4096.0f ),
	SRGUI_FLOAT( "Zone Hover Pixels", "sp_zone_hover_pixels", "18", "zones hover pixels", 10, 4.0f, 96.0f ),
	SRGUI_FLOAT( "Zone Drag Speed", "sp_zone_drag_speed", "180", "zones drag speed", 10, 8.0f, 1024.0f ),
	SRGUI_INT( "Zone Start Stop MS", "sp_zone_start_stop_ms", "220", "zones start stop milliseconds", 10, 0, 1500 ),
	SRGUI_FLOAT( "Zone Timer X", "sp_zone_hud_x", "8", "zones timer position x", 10, 0.0f, 640.0f ),
	SRGUI_FLOAT( "Zone Timer Y", "sp_zone_hud_y", "84", "zones timer position y", 10, 0.0f, 480.0f ),
	SRGUI_FLOAT( "Zone Timer Scale", "sp_zone_hud_scale", "1.0", "zones timer scale", 10, 0.55f, 2.5f ),
	SRGUI_FLOAT( "Zone Timer Alpha", "sp_zone_hud_alpha", "0.52", "zones timer alpha opacity", 10, 0.0f, 1.0f ),
	SRGUI_COLOR( "Zone Timer Background Top", "sp_zone_timer_clr_bg2", "14 24 16 0.78", "zones timer color background top", 10 ),
	SRGUI_COLOR( "Zone Timer Background Bottom", "sp_zone_timer_clr_bg", "5 8 7 0.86", "zones timer color background bottom", 10 ),
	SRGUI_COLOR( "Zone Timer Border", "sp_zone_timer_clr_border", "105 170 70 0.46", "zones timer color border", 10 ),
	SRGUI_COLOR( "Zone Timer Time", "sp_zone_timer_clr_time", "186 248 142 1.00", "zones timer color time", 10 ),
	SRGUI_COLOR( "Zone Timer PB / Progress", "sp_zone_timer_clr_muted", "145 164 136 0.92", "zones timer color pb progress muted", 10 ),
	SRGUI_COLOR( "Zone Timer Ahead", "sp_zone_timer_clr_ahead", "108 255 108 1.00", "zones timer color ahead", 10 ),
	SRGUI_COLOR( "Zone Timer Behind", "sp_zone_timer_clr_behind", "255 92 72 1.00", "zones timer color behind", 10 ),
	SRGUI_COLOR( "Zone Timer Gold", "sp_zone_timer_clr_gold", "255 220 46 1.00", "zones timer color gold", 10 ),
	SRGUI_COLOR( "Zone Timer Neutral", "sp_zone_timer_clr_neutral", "235 190 62 1.00", "zones timer color neutral", 10 ),

	SRGUI_COMMAND( "Enable sv_cheats", "sv_cheats 1\nsv_cheats 1", "dev cheats enable", 12 ),
	SRGUI_BOOL( "Draw Triggers", "cg_drawTriggers", "0", "dev draw triggers", 12 ),
	SRGUI_INT( "Trigger Opacity", "cg_triggerOpacity", "140", "dev trigger opacity alpha", 12, 5, 255 ),
	SRGUI_INT( "Trigger Log", "g_triggerLog", "0", "dev trigger log console", 12, 0, 2 ),
	SRGUI_COMMAND( "List Triggers", "sp_trigger_list", "dev trigger list names", 12 ),
	SRGUI_BOOL( "Draw Enemies", "cg_drawEnemies", "0", "dev draw enemies", 12 ),
	SRGUI_INT( "Enemy Opacity", "cg_enemyOpacity", "140", "dev enemy opacity alpha", 12, 5, 255 ),
	SRGUI_BOOL( "Draw Items", "cg_drawItems", "0", "dev draw items", 12 ),
	SRGUI_INT( "Item Opacity", "cg_itemOpacity", "140", "dev item opacity alpha", 12, 5, 255 ),
	SRGUI_BOOL( "Explosive Timers", "cg_explosiveTimers", "0", "dev explosive timers", 12 ),
	SRGUI_BOOL( "Held Grenade/Dynamite Timer", "cg_explosiveTimersHeld", "1", "dev held grenade dynamite timer", 12 ),
	SRGUI_BOOL( "Pinned World Timers", "cg_explosiveTimersWorld", "1", "dev pinned world timers", 12 ),
	SRGUI_PAGE( "Draw Clips", "r_drawClips", "dev draw clips xray depth", 12 ),
	SRGUI_INT( "Clip Opacity", "r_clipOpacity", "120", "dev clip opacity alpha", 12, 0, 255 ),
	SRGUI_BOOL( "Default Fonts", "cg_defaultFonts", "0", "dev default fonts vid_restart", 12 )
};

#undef SRGUI_BOOL
#undef SRGUI_FLOAT
#undef SRGUI_INT
#undef SRGUI_INPUT_INT
#undef SRGUI_COLOR
#undef SRGUI_COMMAND
#undef SRGUI_PAGE
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
	{ "Race Chat", "ls_race_open_chat" },
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

static void CL_ImGuiForgetArchivedCvar( const char *name ) {
	cvar_t *cv = Cvar_Get( name, "", 0 );
	if ( cv && ( cv->flags & CVAR_ARCHIVE ) ) {
		cv->flags &= ~( CVAR_ARCHIVE | CVAR_USER_CREATED );
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	}
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

static void CL_SpeedrunImGui_ClearRaceChatKeys( void ) {
	if ( s_imguiInitialized && ImGui::GetCurrentContext() ) {
		ImGuiIO &io = ImGui::GetIO();
		io.ClearInputKeys();
	}
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

static void CL_SpeedrunImGui_CapturePanelInput( bool hideActiveMenu ) {
	if ( hideActiveMenu && uivm ) {
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE );
	}
	Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_CONSOLE ) | KEYCATCH_UI );
}

static void CL_SpeedrunImGui_Open( void ) {
	bool openedInGame = cls.state == CA_ACTIVE;
	s_raceGuiOpen = false;
	s_raceGuiRestoreAfterChat = false;
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
	CL_SpeedrunImGui_CapturePanelInput( openedInGame );
	if ( openedInGame ) {
		Cvar_Set( "cl_paused", "1" );
	}
}

static void CL_SpeedrunImGui_RestorePanelInput( bool restorePause ) {
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

static void CL_SpeedrunImGui_CloseRaceGui( void ) {
	bool restorePause = s_imguiRestorePause && s_imguiRestorePause->integer != 0;
	s_raceGuiOpen = false;
	s_mouseDown[0] = s_mouseDown[1] = s_mouseDown[2] = false;
	if ( s_raceChatOpen && !s_imguiOpen ) {
		s_raceGuiRestoreAfterChat = true;
		return;
	}
	if ( s_imguiOpen || s_raceChatOpen ) {
		return;
	}
	s_raceGuiRestoreAfterChat = false;
	CL_SpeedrunImGui_RestorePanelInput( restorePause );
}

extern "C" void CL_SpeedrunImGui_OpenRace( void ) {
	bool openedInGame = cls.state == CA_ACTIVE;
	s_imguiOpen = false;
	s_raceGuiOpen = true;
	s_raceGuiRestoreAfterChat = false;
	s_raceGuiAnim = s_imguiAnimations && s_imguiAnimations->integer == 0 ? 1.0f : 0.0f;
	s_lastEnabledCvar = 0;
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "0" );
	}
	SetCursor( NULL );
	CL_SpeedrunImGui_ClearGameplayInput();
	CL_SpeedrunImGui_CapturePanelInput( openedInGame );
	if ( openedInGame ) {
		Cvar_Set( "cl_paused", "1" );
		if ( s_imguiRestorePause ) {
			Cvar_Set( s_imguiRestorePause->name, "1" );
		}
	}
}

extern "C" void CL_SpeedrunImGui_CloseAllForGameplay( void ) {
	s_imguiOpen = false;
	s_raceGuiOpen = false;
	s_raceChatOpen = false;
	s_raceGuiRestoreAfterChat = false;
	s_pendingBindCommand = NULL;
	s_mouseDown[0] = s_mouseDown[1] = s_mouseDown[2] = false;
	s_lastEnabledCvar = 0;
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "0" );
	}
	if ( s_imguiRestorePause ) {
		Cvar_Set( s_imguiRestorePause->name, "0" );
	}
	if ( uivm ) {
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE );
	}
	Key_SetCatcher( Key_GetCatcher() & ~( KEYCATCH_UI | KEYCATCH_CONSOLE ) );
	CL_SpeedrunImGui_ClearGameplayInput();
	Cvar_Set( "cl_paused", "0" );
}

static void CL_SpeedrunImGui_CloseSettingsForRaceStart( void ) {
	CL_SpeedrunImGui_CloseAllForGameplay();
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

static void CL_SpeedrunImGui_CloseRaceChat( void ) {
	s_raceChatOpen = false;
	s_raceChatFocus = false;
	s_raceChatSuppressInputUntilMs = 0;
	s_raceChatIgnoreSubmitUntilMs = Sys_Milliseconds() + 160;
	s_raceChatInput[0] = '\0';
	CL_SpeedrunImGui_ClearRaceChatKeys();
	if ( !CL_SpeedrunImGui_HasPanelOpen() ) {
		if ( s_raceGuiRestoreAfterChat ) {
			bool restorePause = s_imguiRestorePause && s_imguiRestorePause->integer != 0;
			s_raceGuiRestoreAfterChat = false;
			CL_SpeedrunImGui_RestorePanelInput( restorePause );
		} else {
			Key_SetCatcher( s_raceChatPreviousCatcher & ~KEYCATCH_CONSOLE );
			CL_SpeedrunImGui_ClearGameplayInput();
		}
	}
}

extern "C" void CL_SpeedrunImGui_OpenRaceChat( void ) {
	int now = Sys_Milliseconds();
	if ( s_raceChatOpen ) {
		s_raceChatFocus = true;
		s_raceChatSuppressInputUntilMs = now + 140;
		s_raceChatIgnoreSubmitUntilMs = now + 220;
		CL_SpeedrunImGui_ClearRaceChatKeys();
		return;
	}
	s_raceChatPreviousCatcher = Key_GetCatcher();
	s_raceChatOpen = true;
	s_raceChatFocus = true;
	s_raceChatSuppressInputUntilMs = now + 140;
	s_raceChatIgnoreSubmitUntilMs = now + 220;
	s_raceChatInput[0] = '\0';
	CL_SpeedrunImGui_ClearRaceChatKeys();
	if ( !CL_SpeedrunImGui_HasPanelOpen() ) {
		CL_SpeedrunImGui_ClearGameplayInput();
		Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_CONSOLE ) | KEYCATCH_UI );
	}
}

extern "C" int CL_SpeedrunImGui_IsRaceChatOpen( void ) {
	return s_raceChatOpen ? 1 : 0;
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

static void CL_ImGuiUpdateKeyModifiers( ImGuiIO &io ) {
	io.AddKeyEvent( ImGuiMod_Ctrl, ( GetKeyState( VK_CONTROL ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Shift, ( GetKeyState( VK_SHIFT ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Alt, ( GetKeyState( VK_MENU ) & 0x8000 ) != 0 );
	io.AddKeyEvent( ImGuiMod_Super, ( ( GetKeyState( VK_LWIN ) | GetKeyState( VK_RWIN ) ) & 0x8000 ) != 0 );
}

static void CL_ImGuiAddKeyEvent( ImGuiIO &io, WPARAM vk, bool down ) {
	ImGuiKey key = CL_ImGuiMapVK( vk );
	if ( key != ImGuiKey_None ) {
		io.AddKeyEvent( key, down );
	}
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
	static ImGuiContext *lastContext = NULL;
	static int lastRoundingMod = -1;
	static int lastCardAlphaMod = -1;
	static int lastAccentMod = -1;
	static int lastAccentAltMod = -1;
	static bool initialized = false;
	float accent[4];
	float accentAlt[4];
	bool rounded = !s_imguiRounding || s_imguiRounding->integer != 0;
	float cardAlpha = s_imguiCardAlpha ? Com_Clamp( 0.35f, 1.0f, s_imguiCardAlpha->value ) : 0.92f;
	ImGuiContext *context = ImGui::GetCurrentContext();
	int roundingMod = s_imguiRounding ? s_imguiRounding->modificationCount : -2;
	int cardAlphaMod = s_imguiCardAlpha ? s_imguiCardAlpha->modificationCount : -2;
	int accentMod = s_imguiAccent ? s_imguiAccent->modificationCount : -2;
	int accentAltMod = s_imguiAccentAlt ? s_imguiAccentAlt->modificationCount : -2;
	ImGuiStyle &style = ImGui::GetStyle();
	ImVec4 *c = style.Colors;

	if ( initialized && context == lastContext &&
		 roundingMod == lastRoundingMod && cardAlphaMod == lastCardAlphaMod &&
		 accentMod == lastAccentMod && accentAltMod == lastAccentAltMod ) {
		return;
	}
	initialized = true;
	lastContext = context;
	lastRoundingMod = roundingMod;
	lastCardAlphaMod = cardAlphaMod;
	lastAccentMod = accentMod;
	lastAccentAltMod = accentAltMod;

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
	int selectedFont = (int)Com_Clamp( 0.0f, (float)( SRGUI_LIVESPLIT_FONT_COUNT - 1 ), (float)Cvar_VariableIntegerValue( "ls_imgui_font" ) );

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.Fonts->TexDesiredWidth = 2048;
	for ( int fontIndex = 0; fontIndex < SRGUI_LIVESPLIT_FONT_COUNT; ++fontIndex ) {
		for ( int tier = 0; tier < SRGUI_LIVESPLIT_FONT_TIER_COUNT; ++tier ) {
			if ( tier > 0 && fontIndex != selectedFont && fontIndex != 1 ) {
				continue;
			}
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

static void CL_ImGuiEnsureDeviceObjects( void ) {
	if ( !s_imguiInitialized ) {
		return;
	}
	ImGuiIO &io = ImGui::GetIO();
	GLuint fontTex = (GLuint)(uintptr_t)io.Fonts->TexID;
	if ( fontTex && glIsTexture( fontTex ) ) {
		return;
	}
	if ( fontTex ) {
		ImGui_ImplOpenGL2_DestroyDeviceObjects();
	}
	ImGui_ImplOpenGL2_CreateDeviceObjects();
}

static void CL_ImGuiUpdateMousePosition( ImGuiIO &io ) {
	POINT point;
	HWND hwnd;

	if ( !CL_SpeedrunImGui_HasPanelOpen() && !s_raceChatOpen ) {
		return;
	}
	hwnd = s_imguiHwnd ? s_imguiHwnd : GetActiveWindow();
	if ( !hwnd ) {
		return;
	}
	if ( GetCursorPos( &point ) && ScreenToClient( hwnd, &point ) ) {
		io.AddMousePosEvent( (float)point.x, (float)point.y );
	}
}

static void CL_ImGuiOptionTooltip( const char *title, const char *name, const char *desc, const char *defaultValue = NULL ) {
	if ( !ImGui::IsItemHovered() ) {
		return;
	}
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos( ImGui::GetFontSize() * 28.0f );
	if ( title && title[0] ) {
		ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "%s", title );
	}
	if ( desc && desc[0] ) {
		ImGui::TextWrapped( "%s", desc );
	}
	if ( name && name[0] ) {
		ImGui::Separator();
		ImGui::TextDisabled( "cvar/command: %s", name );
	}
	if ( defaultValue && defaultValue[0] ) {
		ImGui::TextDisabled( "default: %s", defaultValue );
	}
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

static const char *CL_ImGuiWidgetHint( const char *kind, const char *label ) {
	return va( "%s for %s.", kind ? kind : "Option", label && label[0] ? label : "this setting" );
}

static const char *CL_ImGuiCanonicalBindableCommand( const char *command ) {
	if ( !command || !command[0] ) {
		return NULL;
	}
	for ( int i = 0; i < IM_ARRAYSIZE( s_bindEntries ); ++i ) {
		if ( Q_stricmp( s_bindEntries[i].command, command ) == 0 ) {
			return s_bindEntries[i].command;
		}
	}
	return NULL;
}

static void CL_ImGuiDrawCommandBindTools( const char *command, const char *desc = NULL ) {
	const char *bindCommand = CL_ImGuiCanonicalBindableCommand( command );
	if ( !bindCommand ) {
		return;
	}
	int keynum = Key_GetKey( bindCommand );
	const char *keyName = keynum > 0 ? Key_KeynumToString( keynum, qtrue ) : "Unbound";
	bool waiting = s_pendingBindCommand == bindCommand;
	ImGui::PushID( bindCommand );
	ImGui::SameLine( 0.0f, 8.0f );
	ImGui::TextDisabled( "%s", waiting ? "press key..." : keyName );
	CL_ImGuiOptionTooltip( "Current bind", bindCommand, desc ? desc : "Keyboard shortcut assigned to this command." );
	ImGui::SameLine( 0.0f, 6.0f );
	if ( ImGui::SmallButton( waiting ? "Cancel" : "Bind" ) ) {
		s_pendingBindCommand = waiting ? NULL : bindCommand;
	}
	CL_ImGuiOptionTooltip( waiting ? "Cancel bind capture" : "Assign bind", bindCommand, waiting ? "Stops waiting for a key." : "Click, then press a key or mouse button for this command." );
	ImGui::PopID();
}

static float CL_ImGuiOptionLabelWidth( void ) {
	float avail = ImGui::GetContentRegionAvail().x;
	float width = avail * 0.34f;
	if ( avail < 300.0f ) return avail;
	if ( width < 130.0f ) width = 130.0f;
	if ( width > 245.0f ) width = 245.0f;
	return width;
}

static bool s_imguiOptionLineCompact = false;

static float CL_ImGuiOptionControlWidth( float minWidth = 120.0f ) {
	float avail = ImGui::GetContentRegionAvail().x;
	float reserve = ( !s_imguiOptionLineCompact && avail > 430.0f ) ? 130.0f : 0.0f;
	float width = avail - reserve;
	if ( width < minWidth ) width = avail;
	if ( width > avail ) width = avail;
	if ( width < 48.0f ) width = 48.0f;
	return width;
}

static bool CL_ImGuiOptionCompactLayout( void ) {
	return ImGui::GetContentRegionAvail().x < 430.0f;
}

static void CL_ImGuiBeginOptionLine( const char *label, const char *name, const char *hint, const char *defaultValue = NULL ) {
	float x = ImGui::GetCursorPosX();
	float labelWidth = CL_ImGuiOptionLabelWidth();
	s_imguiOptionLineCompact = CL_ImGuiOptionCompactLayout();
	ImGui::PushID( name && name[0] ? name : label );
	ImGui::AlignTextToFramePadding();
	if ( s_imguiOptionLineCompact ) {
		ImGui::TextWrapped( "%s", label && label[0] ? label : "Option" );
	} else {
		ImGui::TextUnformatted( label && label[0] ? label : "Option" );
	}
	CL_ImGuiOptionTooltip( label, name, hint, defaultValue );
	if ( !s_imguiOptionLineCompact ) {
		ImGui::SameLine();
		ImGui::SetCursorPosX( x + labelWidth );
	}
}

static void CL_ImGuiEndOptionLine( const char *label, const char *name, const char *hint, const char *defaultValue = NULL ) {
	if ( !s_imguiOptionLineCompact && name && name[0] && ImGui::GetContentRegionAvail().x > 118.0f ) {
		ImGui::SameLine( 0.0f, 8.0f );
		ImGui::TextDisabled( "%s", name );
		CL_ImGuiOptionTooltip( label, name, hint, defaultValue );
	}
	ImGui::PopID();
}

static void CL_ImGuiBoolCvar( const char *label, cvar_t *cv, const char *hint = NULL ) {
	bool v = cv && cv->integer != 0;
	const char *desc = hint ? hint : CL_ImGuiWidgetHint( "Toggles", label );
	CL_ImGuiBeginOptionLine( label, cv ? cv->name : NULL, desc );
	if ( ImGui::Checkbox( "##toggle", &v ) && cv ) {
		Cvar_Set( cv->name, v ? "1" : "0" );
	}
	CL_ImGuiOptionTooltip( label, cv ? cv->name : NULL, desc );
	CL_ImGuiEndOptionLine( label, cv ? cv->name : NULL, desc );
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
	const char *desc = CL_ImGuiWidgetHint( "Adjusts", label );
	CL_ImGuiBeginOptionLine( label, name, desc, defaultValue );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth() );
	if ( ImGui::SliderFloat( "##value", &v, minValue, maxValue, format ) && cv ) {
		Cvar_SetValue( cv->name, v );
	}
	CL_ImGuiOptionTooltip( label, name, desc, defaultValue );
	CL_ImGuiEndOptionLine( label, name, desc, defaultValue );
}

static void CL_ImGuiIntSliderCvarName( const char *label, const char *name, const char *defaultValue, int minValue, int maxValue ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	int v = cv ? cv->integer : 0;
	const char *desc = CL_ImGuiWidgetHint( "Adjusts", label );
	CL_ImGuiBeginOptionLine( label, name, desc, defaultValue );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth() );
	if ( ImGui::SliderInt( "##value", &v, minValue, maxValue ) && cv ) {
		Cvar_SetValue( cv->name, v );
	}
	CL_ImGuiOptionTooltip( label, name, desc, defaultValue );
	CL_ImGuiEndOptionLine( label, name, desc, defaultValue );
}

static void CL_ImGuiIntInputCvarName( const char *label, const char *name, const char *defaultValue, int minValue, int maxValue ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	int value = cv ? cv->integer : atoi( defaultValue ? defaultValue : "0" );
	const char *desc = CL_ImGuiWidgetHint( "Edits", label );
	CL_ImGuiBeginOptionLine( label, name, desc, defaultValue );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth( 92.0f ) );
	if ( ImGui::InputInt( "##value", &value, 0, 0 ) && cv ) {
		if ( value < minValue ) value = minValue;
		if ( value > maxValue ) value = maxValue;
		Cvar_SetValue( cv->name, value );
	}
	CL_ImGuiOptionTooltip( label, name, desc, defaultValue );
	CL_ImGuiEndOptionLine( label, name, desc, defaultValue );
}

static void CL_ImGuiInputCvarName( const char *label, const char *name, const char *defaultValue, ImGuiInputTextFlags flags = 0 ) {
	cvar_t *cv = CL_ImGuiCvar( name, defaultValue );
	char buf[128];
	Q_strncpyz( buf, cv && cv->string ? cv->string : defaultValue, sizeof( buf ) );
	const char *desc = CL_ImGuiWidgetHint( "Edits text", label );
	CL_ImGuiBeginOptionLine( label, name, desc, defaultValue );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth() );
	if ( ImGui::InputText( "##value", buf, sizeof( buf ), flags ) && cv ) {
		Cvar_Set( cv->name, buf );
	}
	CL_ImGuiOptionTooltip( label, name, desc, defaultValue );
	CL_ImGuiEndOptionLine( label, name, desc, defaultValue );
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
	const char *desc = CL_ImGuiWidgetHint( "Chooses", label );
	CL_ImGuiBeginOptionLine( label, cv ? cv->name : NULL, desc );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth() );
	if ( ImGui::BeginCombo( "##value", labels[current] ) ) {
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
	CL_ImGuiOptionTooltip( label, cv ? cv->name : NULL, desc );
	CL_ImGuiEndOptionLine( label, cv ? cv->name : NULL, desc );
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
	const char *desc = command ? command : CL_ImGuiWidgetHint( "Chooses", label );
	CL_ImGuiBeginOptionLine( label, cv ? cv->name : NULL, desc, defaultValue );
	ImGui::PushID( command );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth() );
	if ( ImGui::BeginCombo( "##value", labels[current] ) ) {
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
	CL_ImGuiOptionTooltip( label, cv ? cv->name : NULL, desc, defaultValue );
	ImGui::PopID();
	CL_ImGuiEndOptionLine( label, cv ? cv->name : NULL, desc, defaultValue );
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
	const char *desc = CL_ImGuiWidgetHint( "Chooses", label );
	CL_ImGuiBeginOptionLine( label, name, desc, defaultValue );
	ImGui::SetNextItemWidth( CL_ImGuiOptionControlWidth() );
	if ( ImGui::BeginCombo( "##value", labels[current] ) ) {
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
	CL_ImGuiOptionTooltip( label, name, desc, defaultValue );
	if ( !s_imguiOptionLineCompact && cv && ImGui::GetContentRegionAvail().x > 156.0f ) {
		ImGui::SameLine( 0.0f, 8.0f );
		ImGui::TextDisabled( "%s = %s", cv->name, cv->string && cv->string[0] ? cv->string : values[current] );
		CL_ImGuiOptionTooltip( label, name, desc, defaultValue );
		ImGui::PopID();
	} else {
		CL_ImGuiEndOptionLine( label, name, desc, defaultValue );
	}
}

static void CL_ImGuiColorCvarName( const char *label, const char *name, const float fallback[4] ) {
	cvar_t *cv = CL_ImGuiCvar( name, "" );
	float rgba[4] = { fallback[0], fallback[1], fallback[2], fallback[3] };
	int r, g, b;
	float a;
	char buf[64];
	float controlAvail;
	float resetWidth = 62.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	bool resetInline;
	float colorWidth;

	if ( cv && cv->string && sscanf( cv->string, "%d %d %d %f", &r, &g, &b, &a ) == 4 ) {
		rgba[0] = Com_Clamp( 0.0f, 1.0f, r / 255.0f );
		rgba[1] = Com_Clamp( 0.0f, 1.0f, g / 255.0f );
		rgba[2] = Com_Clamp( 0.0f, 1.0f, b / 255.0f );
		rgba[3] = Com_Clamp( 0.0f, 1.0f, a );
	}

	const char *desc = CL_ImGuiWidgetHint( "Edits color", label );
	CL_ImGuiBeginOptionLine( label, name, desc );
	controlAvail = ImGui::GetContentRegionAvail().x;
	resetInline = controlAvail > 260.0f;
	colorWidth = resetInline ? controlAvail - resetWidth - spacing : controlAvail;
	if ( colorWidth < 120.0f ) colorWidth = controlAvail;
	if ( colorWidth < 48.0f ) colorWidth = 48.0f;
	ImGui::SetNextItemWidth( colorWidth );
	if ( ImGui::ColorEdit4( "##value", rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB ) && cv ) {
		Com_sprintf( buf, sizeof( buf ), "%d %d %d %.2f", (int)( rgba[0] * 255.0f + 0.5f ), (int)( rgba[1] * 255.0f + 0.5f ), (int)( rgba[2] * 255.0f + 0.5f ), rgba[3] );
		Cvar_Set( cv->name, buf );
	}
	CL_ImGuiOptionTooltip( label, name, desc );
	if ( resetInline ) {
		ImGui::SameLine( 0.0f, spacing );
	}
	if ( ImGui::SmallButton( "Reset" ) && cv ) {
		Cvar_Set( cv->name, "" );
	}
	CL_ImGuiOptionTooltip( "Reset color", name, "Clears the custom color and lets the fallback/default color be used." );
	ImGui::PopID();
}

static void CL_ImGuiCommandButton( const char *label, const char *command, const char *hint = NULL ) {
	ImGuiStyle &style = ImGui::GetStyle();
	CL_ImGuiBeginAutoBox( label );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 p0 = ImGui::GetWindowPos();
	ImVec2 p1 = ImVec2( p0.x + 5.0f, p0.y + ImGui::GetWindowHeight() - 1.0f );
	draw->AddRectFilled( ImVec2( p0.x + 1.0f, p0.y + 1.0f ), p1, IM_COL32( 95, 175, 58, 120 ), style.ChildRounding > 1.0f ? style.ChildRounding - 1.0f : 0.0f, ImDrawFlags_RoundCornersLeft );
	ImGui::SetCursorPosX( 16.0f );
	if ( ImGui::Button( label, ImVec2( ImGui::GetContentRegionAvail().x > 180.0f ? 160.0f : 0.0f, 0 ) ) ) {
		Cbuf_AddText( command );
		Cbuf_AddText( "\n" );
	}
	CL_ImGuiOptionTooltip( label, command, hint ? hint : "Runs this command immediately." );
	if ( hint && hint[0] ) {
		CL_ImGuiSameLineIfFits( ImGui::CalcTextSize( hint ).x, 14.0f );
		ImGui::TextDisabled( "%s", hint );
		CL_ImGuiOptionTooltip( label, command, hint );
	}
	CL_ImGuiDrawCommandBindTools( command, hint );
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

static void CL_ImGuiParseColorFallback( const char *value, float out[4] ) {
	int r, g, b;
	float a;
	out[0] = out[1] = out[2] = out[3] = 1.0f;
	if ( value && sscanf( value, "%d %d %d %f", &r, &g, &b, &a ) == 4 ) {
		out[0] = Com_Clamp( 0.0f, 1.0f, r / 255.0f );
		out[1] = Com_Clamp( 0.0f, 1.0f, g / 255.0f );
		out[2] = Com_Clamp( 0.0f, 1.0f, b / 255.0f );
		out[3] = Com_Clamp( 0.0f, 1.0f, a );
	}
}

static void CL_ImGuiRenderSettingEntry( const srGuiSettingEntry_t *entry, bool showCategoryButton ) {
	char favName[96];
	float colorFallback[4];
	bool fav = CL_ImGuiIsFavorite( entry );
	CL_ImGuiFavoriteCvarName( entry, favName, sizeof( favName ) );
	ImGui::PushID( entry->name );
	if ( ImGui::Button( fav ? "-" : "+", ImVec2( 24, 0 ) ) ) {
		Cvar_Set( favName, fav ? "0" : "1" );
	}
	CL_ImGuiSameLineIfFits( 176.0f );
	if ( entry->type == SRGUI_SETTING_BOOL ) {
		CL_ImGuiBoolCvarName( entry->label, entry->name, entry->defaultValue );
	} else if ( entry->type == SRGUI_SETTING_FLOAT ) {
		CL_ImGuiSliderCvarName( entry->label, entry->name, entry->defaultValue, entry->minValue, entry->maxValue, "%.2f" );
	} else if ( entry->type == SRGUI_SETTING_INT ) {
		CL_ImGuiIntSliderCvarName( entry->label, entry->name, entry->defaultValue, (int)entry->minValue, (int)entry->maxValue );
	} else if ( entry->type == SRGUI_SETTING_INPUT_INT ) {
		CL_ImGuiIntInputCvarName( entry->label, entry->name, entry->defaultValue, (int)entry->minValue, (int)entry->maxValue );
	} else if ( entry->type == SRGUI_SETTING_COLOR ) {
		CL_ImGuiParseColorFallback( entry->defaultValue, colorFallback );
		CL_ImGuiColorCvarName( entry->label, entry->name, colorFallback );
	} else if ( entry->type == SRGUI_SETTING_PAGE ) {
		ImGui::Text( "%s", entry->label );
		CL_ImGuiOptionTooltip( entry->label, entry->name, entry->tags );
		if ( entry->name && entry->name[0] ) {
			ImGui::SameLine();
			ImGui::TextDisabled( "%s", entry->name );
			CL_ImGuiOptionTooltip( entry->label, entry->name, entry->tags );
		}
	} else if ( entry->command ) {
		CL_ImGuiCommandButton( entry->label, entry->command, entry->tags );
	}
	if ( showCategoryButton ) {
		CL_ImGuiSameLineIfFits( 36.0f );
		if ( ImGui::SmallButton( "Go" ) ) {
			s_imguiCategory = entry->category;
		}
	}
	ImGui::PopID();
}

static void CL_ImGuiDrawPinnedSettings( void ) {
	int i, count = 0;
	if ( !ImGui::CollapsingHeader( "Pinned Settings", ImGuiTreeNodeFlags_DefaultOpen ) ) return;
	ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.10f, 0.17f, 0.08f, 0.82f ) );
	for ( i = 0; i < IM_ARRAYSIZE( s_settingEntries ); ++i ) {
		if ( CL_ImGuiIsFavorite( &s_settingEntries[i] ) ) {
			CL_ImGuiRenderSettingEntry( &s_settingEntries[i], true );
			count++;
		}
	}
	if ( count == 0 ) {
		ImGui::TextDisabled( "No pinned settings yet. Use Search and press + next to an option." );
	}
	ImGui::PopStyleColor();
	ImGui::Spacing();
}

static void CL_ImGuiDrawSettingsSearchPage( void ) {
	int i, count = 0;
	CL_ImGuiSectionHeader( "Settings Search", "Search cvars, colors, binds, zones, demos, records and commands." );
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputTextWithHint( "##settings_search", "search settings and commands...", s_settingsSearch, sizeof( s_settingsSearch ) );
	ImGui::Spacing();
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

static void CL_ImGuiAdjustFrom640Raw( float *x, float *y, float *w, float *h, scralign_t align ) {
	float xscale = cls.glconfig.vidWidth / 640.0f;
	float yscale = cls.glconfig.vidHeight / 480.0f;
	float minscale = xscale < yscale ? xscale : yscale;
	float tx = x ? *x : 0.0f;
	float ty = y ? *y : 0.0f;

	switch ( align ) {
	case ALIGN_TOP:
	case ALIGN_CENTER:
	case ALIGN_BOTTOM:
		if ( x ) *x = ( tx - 320.0f ) * minscale + cls.glconfig.vidWidth * 0.5f;
		if ( w ) *w *= minscale;
		break;
	case ALIGN_TOPRIGHT:
	case ALIGN_RIGHT:
	case ALIGN_BOTTOMRIGHT:
		if ( x ) *x = ( tx - 640.0f ) * minscale + cls.glconfig.vidWidth;
		if ( w ) *w *= minscale;
		break;
	case ALIGN_TOPLEFT:
	case ALIGN_LEFT:
	case ALIGN_BOTTOMLEFT:
		if ( x ) *x = tx * minscale;
		if ( w ) *w *= minscale;
		break;
	case ALIGN_STRETCH:
	default:
		if ( x ) *x = tx * xscale;
		if ( w ) *w *= xscale;
		break;
	}

	switch ( align ) {
	case ALIGN_LEFT:
	case ALIGN_CENTER:
	case ALIGN_RIGHT:
		if ( y ) *y = ( ty - 240.0f ) * minscale + cls.glconfig.vidHeight * 0.5f;
		if ( h ) *h *= minscale;
		break;
	case ALIGN_BOTTOM:
	case ALIGN_BOTTOMLEFT:
	case ALIGN_BOTTOMRIGHT:
		if ( y ) *y = ( ty - 480.0f ) * minscale + cls.glconfig.vidHeight;
		if ( h ) *h *= minscale;
		break;
	case ALIGN_TOP:
	case ALIGN_TOPLEFT:
	case ALIGN_TOPRIGHT:
		if ( y ) *y = ty * minscale;
		if ( h ) *h *= minscale;
		break;
	case ALIGN_STRETCH:
	default:
		if ( y ) *y = ty * yscale;
		if ( h ) *h *= yscale;
		break;
	}
}

enum {
	SRGUI_LAYOUT_DIM_DEFAULT = 0,
	SRGUI_LAYOUT_DIM_YSCALE,
	SRGUI_LAYOUT_DIM_AVERAGE
};

static void CL_ImGuiDragLayoutElement( const char *label, const char *xCvar, const char *yCvar, const char *xDefault, const char *yDefault, float w, float h, bool autoWhenZero = false, bool mirrorAlign = false, float autoX = 0.0f, float autoY = 0.0f, const char *toggleCvar = NULL, const char *toggleCvar2 = NULL, const char *scaleCvar = NULL, const char *widthCvar = NULL, float minScale = 0.5f, float maxScale = 3.0f, float minWidth = 40.0f, float maxWidth = 640.0f, int anchorMode = 0, const char *alignCvar = NULL, int alignRightValue = 1, scralign_t screenAlign = ALIGN_STRETCH, const char *scaleDefault = "1.0", const char *widthDefault = "178", int alignModeCount = 2, const char *alignDefault = "0", bool rawScreenSpace = false, bool heightScalesWithScale = false, int dimensionScaleMode = SRGUI_LAYOUT_DIM_DEFAULT ) {
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
	float drawH = widthCv ? ( heightScalesWithScale ? h * scale : h ) : h * scale;
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
	float dxScale;
	float dyScale;
	if ( dimensionScaleMode != SRGUI_LAYOUT_DIM_DEFAULT ) {
		float xscale = cls.glconfig.vidWidth / 640.0f;
		float yscale = cls.glconfig.vidHeight / 480.0f;
		float dimScale = dimensionScaleMode == SRGUI_LAYOUT_DIM_AVERAGE ? ( xscale + yscale ) * 0.5f : yscale;
		x = vx * xscale;
		y = vy * yscale;
		screenW = drawW * dimScale;
		screenH = drawH * dimScale;
		dxScale = xscale;
		dyScale = yscale;
	} else if ( rawScreenSpace ) {
		CL_ImGuiAdjustFrom640Raw( &x, &y, &screenW, &screenH, screenAlign );
		dxScale = screenW / drawW;
		dyScale = screenH / drawH;
	} else {
		SCR_AdjustFrom640( &x, &y, &screenW, &screenH, screenAlign );
		dxScale = screenW / drawW;
		dyScale = screenH / drawH;
	}
	ImVec2 pos = ImVec2( x, y );
	ImVec2 size = ImVec2( screenW, screenH );
	if ( size.x < 1.0f ) size.x = 1.0f;
	if ( size.y < 1.0f ) size.y = 1.0f;
	if ( size.y < 56.0f ) size.y = 56.0f;
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
	ImGui::Begin( label, NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus );
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

static float CL_ImGuiRaceOverlayHandleHeight( void ) {
	lsRaceUiSnapshot_t race;
	int rowCount;
	LS_RaceBuildSnapshot( &race );
	rowCount = race.playerCount;
	if ( rowCount > 8 ) rowCount = 8;
	if ( rowCount < 1 ) rowCount = 1;
	return 54.0f + rowCount * 28.0f;
}

static float CL_ImGuiZoneTimerHandleHeight( void ) {
	char lastDelta[32];
	char lastTotalDelta[32];
	Cvar_VariableStringBuffer( "sp_zone_last_delta", lastDelta, sizeof( lastDelta ) );
	Cvar_VariableStringBuffer( "sp_zone_last_total_delta", lastTotalDelta, sizeof( lastTotalDelta ) );
	return ( Cvar_VariableIntegerValue( "sp_zone_delta_visible" ) != 0 && ( lastDelta[0] || lastTotalDelta[0] ) ) ? 76.0f : 54.0f;
}

static void CL_ImGuiDrawLayoutEditor( void ) {
	cvar_t *edit = CL_ImGuiCvar( "ui_speedrun_layout_edit", "0" );
	if ( !edit || edit->integer == 0 || cls.state != CA_ACTIVE ) return;
	if ( !ImGui::GetIO().MouseDown[0] ) {
		s_liveSplitEditActive = false;
	}
	/* LiveSplit ImGui draws its own exact edit handles; the generic HUD box used
	   the old fixed height and caused a second wrong resize/move box. */
	CL_ImGuiDragLayoutElement( "Keys", "ks_x", "ks_y", CL_ImGuiKeysDefaultXString(), "370", CL_ImGuiKeysHandleWidth(), CL_ImGuiKeysHandleHeight(), false, false, 0.0f, 0.0f, "cg_drawKeys", NULL, "ks_scale", NULL, 0.3f, 4.0f, 40.0f, 640.0f, 0, NULL, 1, ALIGN_STRETCH, "1.0", "178", 2, "0", false, false, SRGUI_LAYOUT_DIM_AVERAGE );
	CL_ImGuiDragLayoutElement( "Velocity", "cg_velocity_x", "cg_velocity_y", "320", "457", 44, CL_ImGuiVelocityHandleHeight(), false, false, 0.0f, 0.0f, "cg_drawVelocity", NULL, "cg_velocity_scale", NULL, 0.3f, 3.0f, 40.0f, 640.0f, Cvar_VariableIntegerValue( "cg_velocity_align" ) == 1 ? 1 : ( Cvar_VariableIntegerValue( "cg_velocity_align" ) == 2 ? 2 : 0 ), "cg_velocity_align", 2, Cvar_VariableIntegerValue( "cg_velocity_align" ) == 1 ? ALIGN_TOP : ( Cvar_VariableIntegerValue( "cg_velocity_align" ) == 2 ? ALIGN_TOPRIGHT : ALIGN_TOPLEFT ), "1.0", "178", 3, "1" );
	CL_ImGuiDragLayoutElement( "FPS/Timer", "cg_fpsX", "cg_fpsY", "500", "0", 96, 40, true, false, 500.0f, 0.0f, "cg_drawFPS", "cg_drawTimer", "cg_fpsScale", NULL, 0.25f, 4.0f, 40.0f, 640.0f, Cvar_VariableIntegerValue( "cg_fpsAlign" ) == 1 ? 0 : 2, "cg_fpsAlign", 0, Cvar_VariableIntegerValue( "cg_fpsAlign" ) == 1 ? ALIGN_TOPLEFT : ALIGN_TOPRIGHT );
	CL_ImGuiDragLayoutElement( "IGT", "ls_igttimer_x", "ls_igttimer_y", "638", "240", 72, Cvar_VariableIntegerValue( "ls_igtsegtimer" ) ? 18 : 10, false, false, 0.0f, 0.0f, "ls_igttimer", NULL, "ls_igttimer_scale", NULL, 0.3f, 4.0f, 40.0f, 640.0f, Cvar_VariableIntegerValue( "ls_igttimer_align" ) == 1 ? 1 : ( Cvar_VariableIntegerValue( "ls_igttimer_align" ) == 2 ? 2 : 0 ), "ls_igttimer_align", 2, ALIGN_STRETCH, "1.0", "178", 3, "2" );
	CL_ImGuiDragLayoutElement( "Zone Timer", "sp_zone_hud_x", "sp_zone_hud_y", "8", "84", 214, CL_ImGuiZoneTimerHandleHeight(), false, false, 0.0f, 0.0f, "sp_zone_timer", NULL, "sp_zone_hud_scale", NULL, 0.55f, 2.5f, 40.0f, 640.0f, 0, NULL, 1, ALIGN_STRETCH, "1.0", "178", 2, "0", false, false, SRGUI_LAYOUT_DIM_YSCALE );
	CL_ImGuiDragLayoutElement( "Race", "ls_race_overlay_x", "ls_race_overlay_y", "8", "72", 110, CL_ImGuiRaceOverlayHandleHeight(), false, false, 0.0f, 0.0f, "ls_race_overlay", NULL, "ls_race_overlay_scale", "ls_race_overlay_w", 0.35f, 1.8f, 110.0f, 500.0f, 0, NULL, 1, ALIGN_STRETCH, "0.5", "110", 2, "0", true, true );
	CL_ImGuiDragLayoutElement( "Race Chat", "ls_race_chat_x", "ls_race_chat_y", "12", "300", 285, 132, false, false, 0.0f, 0.0f, "ls_race_chat", NULL, "ls_race_chat_scale", "ls_race_chat_w", 0.50f, 2.00f, 80.0f, 620.0f, 0, NULL, 1, ALIGN_STRETCH, "1.0", "285", 2, "0", true );
	CL_ImGuiDragLayoutElement( "Race Chat Input", "ls_race_chat_input_x", "ls_race_chat_input_y", "12", "444", 285, 28, false, false, 0.0f, 0.0f, "ls_race_chat", NULL, "ls_race_chat_input_scale", "ls_race_chat_input_w", 0.50f, 2.00f, 80.0f, 620.0f, 0, NULL, 1, ALIGN_STRETCH, "1.0", "285", 2, "0", true );
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

static void CL_ImGuiDrawKeystrokeKey( ImDrawList *draw, ImFont *font, float fontSize, float x, float y, float w, float h, const char *label, float t, float rounding, float borderSize, ImU32 idleBg, ImU32 activeBg, ImU32 idleBorder, ImU32 activeBorder, ImU32 idleText, ImU32 activeText, int effect, float appear = 1.0f, float pulse = 0.0f ) {
	ImU32 bg = CL_ImGuiLerpColorU32( idleBg, activeBg, t );
	ImU32 border = CL_ImGuiLerpColorU32( idleBorder, activeBorder, t );
	ImU32 text = CL_ImGuiLerpColorU32( idleText, activeText, t );
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

static int CL_ImGuiCollectActiveKeystrokes( const bool pressed[SRGUI_KEYSTROKE_KEY_COUNT], int out[SRGUI_KEYSTROKE_KEY_COUNT], int maxCount, bool showUse, bool showReload ) {
	static const int order[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10 };
	int count = 0;
	if ( maxCount < 1 ) maxCount = SRGUI_KEYSTROKE_KEY_COUNT;
	for ( int i = 0; i < IM_ARRAYSIZE( order ); ++i ) {
		int idx = order[i];
		if ( idx == 9 && !showUse ) continue;
		if ( idx == 10 && !showReload ) continue;
		if ( pressed[idx] ) {
			out[count++] = idx;
			if ( count >= maxCount ) break;
		}
	}
	return count;
}

static void CL_ImGuiDrawActiveKeystrokeStrip( ImDrawList *draw, ImFont *font, float fontSize, float x, float y, float maxW, float maxH, float itemW, float itemH, float gap, bool vertical, int anchor, int maxCount, const bool pressed[SRGUI_KEYSTROKE_KEY_COUNT], const char *labels[SRGUI_KEYSTROKE_KEY_COUNT], float rounding, float borderSize, ImU32 idleBg, ImU32 activeBg, ImU32 idleBorder, ImU32 activeBorder, ImU32 idleText, ImU32 activeText, int effect, bool showUse, bool showReload ) {
	int active[SRGUI_KEYSTROKE_KEY_COUNT];
	int count = CL_ImGuiCollectActiveKeystrokes( pressed, active, maxCount, showUse, showReload );
	if ( count <= 0 ) return;
	if ( vertical ) {
		float totalH = itemH * (float)count + gap * (float)( count - 1 );
		float startY = ( anchor == 3 ) ? y + maxH - totalH : ( anchor == 2 ? y + ( maxH - totalH ) * 0.5f : y );
		for ( int i = 0; i < count; ++i ) {
			CL_ImGuiDrawKeystrokeKey( draw, font, fontSize, x, startY + ( itemH + gap ) * (float)i, itemW, itemH, labels[active[i]], s_imguiKeystrokeAlpha[active[i]], rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, effect, s_imguiKeystrokeAppear[active[i]], s_imguiKeystrokePulse[active[i]] );
		}
	} else {
		float totalW = itemW * (float)count + gap * (float)( count - 1 );
		float startX = ( anchor == 1 ) ? x + maxW - totalW : ( anchor == 2 ? x + ( maxW - totalW ) * 0.5f : x );
		for ( int i = 0; i < count; ++i ) {
			CL_ImGuiDrawKeystrokeKey( draw, font, fontSize, startX + ( itemW + gap ) * (float)i, y, itemW, itemH, labels[active[i]], s_imguiKeystrokeAlpha[active[i]], rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, effect, s_imguiKeystrokeAppear[active[i]], s_imguiKeystrokePulse[active[i]] );
		}
	}
}

static void CL_ImGuiEnsureKeystrokeCvars( void ) {
	static bool initialized = false;
	if ( initialized ) return;
	initialized = true;

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

static bool CL_ImGuiShouldDrawKeystrokesOverlay( void ) {
	CL_ImGuiEnsureKeystrokeCvars();
	if ( !Cvar_VariableIntegerValue( "cg_drawKeys" ) ) return false;
	if ( cls.state < CA_ACTIVE ) return false;
	if ( Cvar_VariableIntegerValue( "ks_ingame_only" ) && cls.keyCatchers && !Cvar_VariableIntegerValue( "ui_speedrun_layout_edit" ) && !CL_SpeedrunImGui_HasPanelOpen() ) return false;
	return true;
}

static void CL_ImGuiDrawKeystrokesOverlay( void ) {
	CL_ImGuiEnsureKeystrokeCvars();
	if ( !CL_ImGuiShouldDrawKeystrokesOverlay() ) return;

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
	int effect = Cvar_VariableIntegerValue( "ks_effect" );
	bool showUse = Cvar_VariableIntegerValue( "ks_show_use" ) != 0;
	bool showReload = Cvar_VariableIntegerValue( "ks_show_reload" ) != 0;
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

	ImDrawList *draw = ImGui::GetBackgroundDrawList();
	int tier = CL_ImGuiLiveSplitFontTierForScale( scale * localFontScale * screenScale );
	ImFont *font = CL_ImGuiLiveSplitFont( 1, tier );
	float fontSize = 11.5f * scale * localFontScale * screenScale;
	ImU32 idleBg = CL_ImGuiColorU32( "ks_clr_bg", ImVec4( 0.04f, 0.04f, 0.06f, 0.72f ), opacity );
	ImU32 activeBg = CL_ImGuiColorU32( "ks_clr_active", ImVec4( 0.10f, 0.24f, 0.07f, 0.88f ), opacity );
	ImU32 idleBorder = CL_ImGuiColorU32( "ks_clr_border", ImVec4( 0.20f, 0.25f, 0.18f, 0.30f ), opacity );
	ImU32 activeBorder = CL_ImGuiColorU32( "ks_clr_active_border", ImVec4( 0.42f, 0.75f, 0.22f, 0.85f ), opacity );
	ImU32 idleText = CL_ImGuiColorU32( "ks_clr_text", ImVec4( 0.50f, 0.56f, 0.48f, 0.78f ), opacity );
	ImU32 activeText = CL_ImGuiColorU32( "ks_clr_active_text", ImVec4( 0.86f, 0.97f, 0.72f, 1.00f ), opacity );

#define DRAW_KS(idx, px, py, pw) CL_ImGuiDrawKeystrokeKey( draw, font, fontSize, (px), (py), (pw), boxH, labels[(idx)], s_imguiKeystrokeAlpha[(idx)], rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, effect, 1.0f, s_imguiKeystrokePulse[(idx)] )
	if ( layout == 1 ) {
		float cx = x;
		for ( int i = 0; i < 6; ++i ) { DRAW_KS( i, cx, y, i >= 4 ? wideW : boxW ); cx += ( i >= 4 ? wideW : boxW ) + gap; }
		if ( mouseMode >= 1 ) { DRAW_KS( 6, cx, y, wideW ); cx += wideW + gap; DRAW_KS( 8, cx, y, wideW ); cx += wideW + gap; }
		if ( showUse ) { DRAW_KS( 9, cx, y, wideW ); cx += wideW + gap; }
		if ( showReload ) { DRAW_KS( 10, cx, y, wideW ); cx += wideW + gap; }
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
			if ( idx == 9 && !showUse ) continue;
			if ( idx == 10 && !showReload ) continue;
			DRAW_KS( idx, x, cy, wideW );
			cy += boxH + gap;
		}
	} else if ( layout == 5 ) {
		CL_ImGuiDrawActiveKeystrokeStrip( draw, font, fontSize, x, y, CL_ImGuiKeysHandleWidth() * scale * screenScale, boxH, wideW, boxH, gap, false, activeAnchor, activeMax, pressed, labels, rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, effect, showUse, showReload );
	} else if ( layout == 6 ) {
		CL_ImGuiDrawActiveKeystrokeStrip( draw, font, fontSize, x, y, wideW, CL_ImGuiKeysHandleHeight() * scale * screenScale, wideW, boxH, gap, true, activeAnchor, activeMax, pressed, labels, rounding, borderSize, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, effect, showUse, showReload );
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
			CL_ImGuiDrawActiveKeystrokeStrip( draw, font, fontSize * miniFontScale, miniX, miniY, miniMaxW, miniMaxH, miniW, miniH, miniGap, miniVertical, activeAnchor, activeMax, pressed, labels, 0.0f, borderSize * 0.65f, idleBg, activeBg, idleBorder, activeBorder, idleText, activeText, effect, showUse, showReload );
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
			if ( showUse || showReload ) {
				float ex = x;
				float ey = y + ( boxH + gap ) * row;
				if ( showUse ) { DRAW_KS( 9, ex, ey, wideW ); ex += wideW + gap; }
				if ( showReload ) { DRAW_KS( 10, ex, ey, wideW ); ex += wideW + gap; }
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
		int shadowAlpha = (int)( ( ( col >> 24 ) & 0xff ) * 0.67f );
		draw->AddText( ImVec2( pos.x + 1.0f, pos.y + 1.0f ), IM_COL32( 0, 0, 0, shadowAlpha ), text );
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

static void CL_ImGuiAddEllipsizedTextSized( ImDrawList *draw, ImFont *font, float fontSize, ImVec2 pos, ImVec2 clipMin, ImVec2 clipMax, ImU32 col, const char *text, bool shadow ) {
	char tmp[128];
	int len;
	float maxW;
	if ( !text || !text[0] ) return;
	if ( !font ) font = ImGui::GetFont();
	if ( fontSize <= 0.0f ) fontSize = ImGui::GetFontSize();
	maxW = clipMax.x - pos.x;
	if ( maxW <= 8.0f ) return;
	Q_strncpyz( tmp, text, sizeof( tmp ) );
	if ( font->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, tmp ).x > maxW ) {
		len = (int)strlen( tmp );
		while ( len > 3 ) {
			tmp[len - 3] = '.';
			tmp[len - 2] = '.';
			tmp[len - 1] = '.';
			tmp[len] = '\0';
			if ( font->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, tmp ).x <= maxW ) break;
			len--;
			tmp[len] = '\0';
		}
	}
	draw->PushClipRect( clipMin, clipMax, true );
	if ( shadow ) {
		draw->AddText( font, fontSize, ImVec2( pos.x + 1.0f, pos.y + 1.0f ), IM_COL32( 0, 0, 0, ( col >> 24 ) & 255 ), tmp );
	}
	draw->AddText( font, fontSize, pos, col, tmp );
	draw->PopClipRect();
}

static bool CL_ImGuiShouldDrawLiveSplitOverlay( void ) {
	if ( !s_cg_livesplit || !s_cg_livesplit->integer ) return false;
	if ( Cvar_VariableIntegerValue( "ls_type" ) == 1 ) return false;
	if ( !Cvar_VariableIntegerValue( "ls_draw" ) ) return false;
	if ( clc.demoplaying && clc.demoHideHUD ) return false;
	return true;
}

static bool CL_ImGuiShouldDrawZoneTimerOverlay( void ) {
	if ( cls.state != CA_ACTIVE ) return false;
	if ( !Cvar_VariableIntegerValue( "sp_zone_timer" ) && !Cvar_VariableIntegerValue( "sp_zone_edit" ) ) return false;
	return true;
}

static const lsRaceUiSnapshot_t *CL_ImGuiRaceSnapshotThisFrame( void ) {
	static lsRaceUiSnapshot_t race;
	static int frame = -1;

	if ( frame != cls.framecount ) {
		LS_RaceBuildSnapshot( &race );
		frame = cls.framecount;
	}
	return &race;
}

static bool CL_ImGuiShouldDrawRaceOverlay( void ) {
	const lsRaceUiSnapshot_t &race = *CL_ImGuiRaceSnapshotThisFrame();
	if ( !race.active ) return false;
	if ( !Cvar_VariableIntegerValue( "ls_race_overlay" ) ) return false;
	if ( clc.demoplaying ) return false;
	return true;
}

static bool CL_ImGuiShouldDrawRaceCountdown( void ) {
	if ( cls.state != CA_ACTIVE ) return false;
	const lsRaceUiSnapshot_t &race = *CL_ImGuiRaceSnapshotThisFrame();
	if ( !race.active ) return false;
	if ( !Cvar_VariableIntegerValue( "ls_race_countdown_center" ) ) return false;
	if ( clc.demoplaying ) return false;
	return !Q_stricmp( race.state, "Countdown" );
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

static ImU32 CL_ImGuiZoneTimerDeltaColor( const char *delta, bool pb, float alphaMul ) {
	if ( pb ) return CL_ImGuiColorU32( "sp_zone_timer_clr_gold", ImVec4( 1.00f, 0.86f, 0.18f, 1.00f ), alphaMul );
	if ( delta && delta[0] == '-' ) return CL_ImGuiColorU32( "sp_zone_timer_clr_ahead", ImVec4( 0.42f, 1.00f, 0.42f, 1.00f ), alphaMul );
	if ( delta && delta[0] == '+' ) return CL_ImGuiColorU32( "sp_zone_timer_clr_behind", ImVec4( 1.00f, 0.36f, 0.28f, 1.00f ), alphaMul );
	return CL_ImGuiColorU32( "sp_zone_timer_clr_neutral", ImVec4( 0.92f, 0.74f, 0.24f, 1.00f ), alphaMul );
}

static void CL_ImGuiZoneTimerText( ImDrawList *draw, ImFont *font, float fontSize, ImVec2 pos, ImU32 col, const char *text, bool shadow ) {
	if ( !draw || !font || !text || !text[0] ) return;
	if ( shadow ) {
		int shadowAlpha = (int)( ( ( col >> 24 ) & 0xff ) * 0.62f );
		draw->AddText( font, fontSize, ImVec2( pos.x + 1.0f, pos.y + 1.0f ), IM_COL32( 0, 0, 0, shadowAlpha ), text );
	}
	draw->AddText( font, fontSize, pos, col, text );
}

static void CL_ImGuiZoneTimerTextRight( ImDrawList *draw, ImFont *font, float fontSize, float right, float y, ImU32 col, const char *text, bool shadow ) {
	ImVec2 size;

	if ( !font || !text || !text[0] ) return;
	size = font->CalcTextSizeA( fontSize, 10000.0f, 0.0f, text );
	CL_ImGuiZoneTimerText( draw, font, fontSize, ImVec2( right - size.x, y ), col, text, shadow );
}

typedef lsRaceUiPlayer_t srRaceRow_t;

static void CL_ImGuiRacePrettyToken( char *text ) {
	int charIndex;
	if ( !text ) return;
	for ( charIndex = 0; text[charIndex]; ++charIndex ) {
		if ( text[charIndex] == '_' ) text[charIndex] = ' ';
	}
}

static bool CL_ImGuiRaceReadySummary( const char *ready, char *out, int outSize ) {
	int loaded;
	int total;

	if ( out && outSize > 0 ) out[0] = '\0';
	if ( !ready || !ready[0] || !out || outSize <= 0 ) return false;
	if ( sscanf( ready, "%d/%d", &loaded, &total ) != 2 ) return false;
	if ( total <= 0 || loaded >= total ) return false;
	Com_sprintf( out, outSize, "Ready %d/%d", loaded, total );
	return true;
}

static bool CL_ImGuiRaceDrawOverlayState( const char *state ) {
	if ( !state || !state[0] ) return false;
	if ( !Q_stricmp( state, "READY" ) ) return false;
	if ( !Q_stricmp( state, "RUN" ) ) return false;
	return true;
}

static const char *CL_ImGuiRaceStateLabel( const char *state, bool compact ) {
	if ( !state || !state[0] ) return compact ? "-" : "-";
	if ( !Q_stricmp( state, "READY" ) ) return "";
	if ( !Q_stricmp( state, "RUN" ) ) return "";
	if ( !compact ) return state;
	if ( !Q_stricmp( state, "FINISH" ) ) return "FIN";
	if ( !Q_stricmp( state, "LOAD" ) ) return "LOD";
	if ( !Q_stricmp( state, "MENU" ) ) return "MENU";
	if ( !Q_stricmp( state, "LEFT" ) ) return "LEFT";
	if ( !Q_stricmp( state, "TIMEOUT" ) ) return "TO";
	if ( !Q_stricmp( state, "PAUSE" ) ) return "PAUSE";
	if ( !Q_stricmp( state, "DEAD" ) ) return "DEAD";
	return state;
}

static ImU32 CL_ImGuiRaceStateBgU32( const char *state ) {
	if ( state && !Q_stricmp( state, "FINISH" ) ) return IM_COL32( 210, 170, 48, 205 );
	if ( state && !Q_stricmp( state, "RUN" ) ) return IM_COL32( 86, 190, 58, 190 );
	if ( state && !Q_stricmp( state, "READY" ) ) return IM_COL32( 72, 132, 68, 175 );
	if ( state && !Q_stricmp( state, "LOAD" ) ) return IM_COL32( 72, 82, 68, 160 );
	if ( state && !Q_stricmp( state, "MENU" ) ) return IM_COL32( 84, 96, 78, 190 );
	if ( state && !Q_stricmp( state, "LEFT" ) ) return IM_COL32( 114, 96, 76, 190 );
	if ( state && !Q_stricmp( state, "TIMEOUT" ) ) return IM_COL32( 132, 76, 58, 205 );
	if ( state && !Q_stricmp( state, "PAUSE" ) ) return IM_COL32( 225, 154, 54, 205 );
	if ( state && !Q_stricmp( state, "DEAD" ) ) return IM_COL32( 190, 58, 52, 205 );
	return IM_COL32( 76, 84, 74, 150 );
}

static ImU32 CL_ImGuiRaceStateTextU32( const char *state ) {
	if ( state && !Q_stricmp( state, "FINISH" ) ) return IM_COL32( 25, 21, 8, 255 );
	if ( state && !Q_stricmp( state, "RUN" ) ) return IM_COL32( 8, 24, 8, 255 );
	if ( state && !Q_stricmp( state, "PAUSE" ) ) return IM_COL32( 30, 18, 5, 255 );
	return IM_COL32( 232, 242, 224, 255 );
}

static void CL_ImGuiRaceStateChip( const char *state, bool compact ) {
	const char *label = CL_ImGuiRaceStateLabel( state, compact );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 textSize = ImGui::CalcTextSize( label );
	float padX = compact ? 5.0f : 7.0f;
	float padY = compact ? 1.5f : 2.0f;
	ImVec2 boxSize( textSize.x + padX * 2.0f, textSize.y + padY * 2.0f );
	if ( !label || !label[0] ) {
		ImGui::Dummy( ImVec2( 1.0f, ImGui::GetTextLineHeight() ) );
		return;
	}
	draw->AddRectFilled( pos, ImVec2( pos.x + boxSize.x, pos.y + boxSize.y ), CL_ImGuiRaceStateBgU32( state ), compact ? 3.0f : 4.0f );
	draw->AddText( ImVec2( pos.x + padX, pos.y + padY ), CL_ImGuiRaceStateTextU32( state ), label );
	ImGui::Dummy( boxSize );
}

static void CL_ImGuiRaceBuildCheatText( int flags, char *out, int outSize, bool compact ) {
	bool any = false;
	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	if ( flags & 1 ) {
		Q_strcat( out, outSize, compact ? "SV" : "sv_cheats" );
		any = true;
	}
	if ( flags & 2 ) {
		if ( any ) Q_strcat( out, outSize, compact ? "+" : " " );
		Q_strcat( out, outSize, "god" );
		any = true;
	}
	if ( flags & 4 ) {
		if ( any ) Q_strcat( out, outSize, compact ? "+" : " " );
		Q_strcat( out, outSize, compact ? "NC" : "noclip" );
	}
}

static float CL_ImGuiRaceAlertChipWidth( const char *label, float scale ) {
	ImVec2 textSize;
	if ( !label || !label[0] ) return 0.0f;
	textSize = ImGui::CalcTextSize( label );
	return textSize.x + 8.0f * scale;
}

static void CL_ImGuiRaceDrawAlertChipAt( ImDrawList *draw, ImVec2 pos, const char *label, float scale ) {
	ImVec2 textSize;
	ImVec2 boxSize;
	if ( !draw || !label || !label[0] ) return;
	textSize = ImGui::CalcTextSize( label );
	boxSize = ImVec2( textSize.x + 8.0f * scale, textSize.y + 2.0f * scale );
	draw->AddRectFilled( pos, ImVec2( pos.x + boxSize.x, pos.y + boxSize.y ), IM_COL32( 205, 58, 46, 220 ), 3.0f * scale );
	draw->AddText( ImVec2( pos.x + 4.0f * scale, pos.y + 1.0f * scale ), IM_COL32( 255, 238, 226, 255 ), label );
}

static void CL_ImGuiRaceBuildRunnerLine( const srRaceRow_t *row, char *out, int outSize ) {
	char mapText[64];

	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	if ( !row ) return;
	Q_strncpyz( mapText, ( row->map[0] && Q_stricmp( row->map, "-" ) ) ? row->map : row->stage, sizeof( mapText ) );
	if ( mapText[0] && Q_stricmp( mapText, "-" ) ) {
		Com_sprintf( out, outSize, "%s%s - %s", row->nick, row->local ? " *" : "", mapText );
	} else {
		Com_sprintf( out, outSize, "%s%s", row->nick, row->local ? " *" : "" );
	}
}

static void CL_ImGuiDrawRaceOverlayList( srRaceRow_t *rows, int rowCount, bool compact, float scale ) {
	ImDrawList *draw = ImGui::GetWindowDrawList();
	float width = ImGui::GetContentRegionAvail().x;
	float rowH = ( compact ? 22.0f : 26.0f ) * scale;
	float gap = 3.0f * scale;
	float pad = 5.0f * scale;
	float lineH = ImGui::GetTextLineHeight();
	ImU32 textCol = IM_COL32( 226, 238, 218, 238 );
	ImU32 dimCol = IM_COL32( 154, 172, 146, 220 );
	ImU32 goldCol = IM_COL32( 255, 214, 76, 255 );
	int rowIndex;

	if ( width < 80.0f ) return;
	for ( rowIndex = 0; rowIndex < rowCount; ++rowIndex ) {
		srRaceRow_t *row = &rows[rowIndex];
		ImVec2 rowMin = ImGui::GetCursorScreenPos();
		ImVec2 rowMax( rowMin.x + width, rowMin.y + rowH );
		ImU32 bg = row->local ? IM_COL32( 35, 70, 28, 150 ) : ( row->finished ? IM_COL32( 76, 56, 18, 122 ) : ( rowIndex & 1 ? IM_COL32( 19, 25, 18, 132 ) : IM_COL32( 12, 17, 13, 122 ) ) );
		ImU32 runnerCol = IM_COL32( row->red, row->green, row->blue, 245 );
		ImU32 timerCol = row->finished ? goldCol : textCol;
		char rankText[12];
		char runnerText[96];
		char timeText[32];
		int rank = row->rank > 0 ? row->rank : rowIndex + 1;
		float rankW = 17.0f * scale;
		float topY = rowMin.y + ( rowH - lineH ) * 0.5f;
		float right = rowMax.x - pad;
		float timeColW = ( compact ? 58.0f : 72.0f ) * scale;
		float timeW;
		float nameX = rowMin.x + pad + rankW + 2.0f * scale;
		float nameRight;

		if ( topY < rowMin.y + 2.0f * scale ) topY = rowMin.y + 2.0f * scale;
		Q_strncpyz( timeText, row->stageIgt[0] ? row->stageIgt : "--", sizeof( timeText ) );
		timeW = ImGui::CalcTextSize( timeText ).x;
		if ( timeColW < timeW ) timeColW = timeW;
		nameRight = right - timeColW - 7.0f * scale;
		if ( nameRight < nameX ) nameRight = nameX;

		draw->AddRectFilled( rowMin, rowMax, bg, 4.0f * scale );
		Com_sprintf( rankText, sizeof( rankText ), "%d", rank );
		CL_ImGuiRaceBuildRunnerLine( row, runnerText, sizeof( runnerText ) );
		CL_ImGuiAddTextShadow( draw, ImVec2( rowMin.x + pad, topY ), rank == 1 ? goldCol : dimCol, rankText, true );
		CL_ImGuiAddEllipsizedText( draw, ImVec2( nameX, topY ), ImVec2( nameX, rowMin.y ), ImVec2( nameRight, rowMax.y ), runnerCol, runnerText, true );
		CL_ImGuiAddTextRight( draw, right, topY, timerCol, timeText, true );
		ImGui::Dummy( ImVec2( width, rowH + gap ) );
	}
}

static void CL_ImGuiDrawRaceRowsTable( const char *tableId, srRaceRow_t *rows, int rowCount, float height, bool compact ) {
	ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX;
	float rowHeight = compact ? 24.0f : 28.0f;
	bool showProgress = Cvar_VariableIntegerValue( "ls_race_overlay_progress" ) != 0;
	int columnCount = compact ? 5 : ( showProgress ? 8 : 5 );
	if ( height > 0.0f ) flags |= ImGuiTableFlags_ScrollY;
	if ( !compact ) flags |= ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX;
	ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, compact ? ImVec2( 5.0f, 3.0f ) : ImVec2( 7.0f, 5.0f ) );
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.12f, 0.20f, 0.09f, 0.98f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.025f, 0.040f, 0.030f, 0.64f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.055f, 0.085f, 0.045f, 0.74f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.40f, 0.16f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.12f, 0.22f, 0.10f, 0.64f ) );
	if ( ImGui::BeginTable( tableId, columnCount, flags, ImVec2( 0, height ) ) ) {
		ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed, compact ? 30.0f : 42.0f );
		ImGui::TableSetupColumn( compact ? "Runner" : "Runner", ImGuiTableColumnFlags_WidthStretch, compact ? 1.20f : 1.35f );
		ImGui::TableSetupColumn( "Map", ImGuiTableColumnFlags_WidthStretch, compact ? 1.25f : 1.30f );
		if ( compact ) {
			ImGui::TableSetupColumn( showProgress ? "St/Pts" : "St", ImGuiTableColumnFlags_WidthFixed, showProgress ? 54.0f : 42.0f );
			ImGui::TableSetupColumn( "IGT", ImGuiTableColumnFlags_WidthFixed, 64.0f );
		} else {
			ImGui::TableSetupColumn( "State", ImGuiTableColumnFlags_WidthFixed, 76.0f );
			if ( showProgress ) {
				ImGui::TableSetupColumn( "Obj", ImGuiTableColumnFlags_WidthFixed, 70.0f );
				ImGui::TableSetupColumn( "Zones", ImGuiTableColumnFlags_WidthFixed, 74.0f );
				ImGui::TableSetupColumn( "Pts", ImGuiTableColumnFlags_WidthFixed, 50.0f );
			}
			ImGui::TableSetupColumn( "IGT", ImGuiTableColumnFlags_WidthFixed, 92.0f );
		}
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableHeadersRow();
		for ( int rowIndex = 0; rowIndex < rowCount; ++rowIndex ) {
			srRaceRow_t *row = &rows[rowIndex];
			char stageText[64];
			char objText[32];
			char zoneText[32];
			char statePts[32];
			char cheatText[48];
			int rank = row->rank > 0 ? row->rank : rowIndex + 1;
			ImVec4 playerColor = ImVec4( row->red / 255.0f, row->green / 255.0f, row->blue / 255.0f, 1.0f );
			ImGui::TableNextRow( ImGuiTableRowFlags_None, rowHeight );
			if ( row->local ) {
				ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, IM_COL32( 46, 76, 28, 132 ) );
			} else if ( row->finished ) {
				ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, IM_COL32( 78, 62, 18, 96 ) );
			}
			ImGui::TableSetColumnIndex( 0 );
			ImGui::TextColored( rank == 1 ? ImVec4( 1.0f, 0.82f, 0.26f, 1.0f ) : ImVec4( 0.70f, 0.78f, 0.66f, 0.92f ), "%d.", rank );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextColored( playerColor, "%s%s", row->nick, row->local ? " *" : "" );
			CL_ImGuiRaceBuildCheatText( row->cheatFlags, cheatText, sizeof( cheatText ), false );
			if ( cheatText[0] ) {
				ImGui::SameLine();
				ImGui::TextColored( ImVec4( 1.0f, 0.28f, 0.22f, 1.0f ), "%s", cheatText );
			}
			ImGui::TableSetColumnIndex( 2 );
			if ( row->progress > 0 ) Com_sprintf( stageText, sizeof( stageText ), "%02d %s", row->progress, ( row->map[0] && Q_stricmp( row->map, "-" ) ) ? row->map : row->stage );
			else Q_strncpyz( stageText, ( row->map[0] && Q_stricmp( row->map, "-" ) ) ? row->map : row->stage, sizeof( stageText ) );
			ImGui::TextUnformatted( stageText );
			ImGui::TableSetColumnIndex( 3 );
			if ( compact ) {
				if ( showProgress ) {
					const char *stateLabel = CL_ImGuiRaceStateLabel( row->state, true );
					Com_sprintf( statePts, sizeof( statePts ), "%s%s%d", stateLabel, stateLabel[0] ? " " : "", row->score );
					ImGui::TextUnformatted( statePts );
				} else {
					CL_ImGuiRaceStateChip( row->state, true );
				}
				ImGui::TableSetColumnIndex( 4 );
				ImGui::TextColored( row->finished ? ImVec4( 1.0f, 0.82f, 0.26f, 1.0f ) : ImVec4( 0.86f, 0.93f, 0.82f, 1.0f ), "%s", row->stageIgt );
				continue;
			}
			CL_ImGuiRaceStateChip( row->state, false );
			if ( showProgress ) {
				Com_sprintf( objText, sizeof( objText ), "%d/%d", row->objectivesFound, row->objectivesTotal );
				Com_sprintf( zoneText, sizeof( zoneText ), "%d/%d", row->zonesFound, row->zonesTotal );
				ImGui::TableSetColumnIndex( 4 );
				ImGui::TextColored( ImVec4( 0.78f, 0.88f, 0.70f, 1.0f ), "%s", row->objectivesTotal > 0 ? objText : "-" );
				ImGui::TableSetColumnIndex( 5 );
				ImGui::TextColored( ImVec4( 0.78f, 0.88f, 0.70f, 1.0f ), "%s", row->zonesTotal > 0 ? zoneText : "-" );
				ImGui::TableSetColumnIndex( 6 );
				ImGui::TextColored( ImVec4( 0.98f, 0.82f, 0.34f, 1.0f ), "%d", row->score );
				ImGui::TableSetColumnIndex( 7 );
			} else {
				ImGui::TableSetColumnIndex( 4 );
			}
			ImGui::TextColored( row->finished ? ImVec4( 1.0f, 0.82f, 0.26f, 1.0f ) : ImVec4( 0.86f, 0.93f, 0.82f, 1.0f ), "%s", row->stageIgt );
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor( 5 );
	ImGui::PopStyleVar();
}

static void CL_ImGuiDrawRaceCenterCountdown( void ) {
	const lsRaceUiSnapshot_t &race = *CL_ImGuiRaceSnapshotThisFrame();
	char text[32];
	ImDrawList *draw;
	ImFont *font;
	ImFont *numberFont;
	float scale;
	float labelSize;
	float numberSize;
	float maxTextWidth;
	float cx;
	float cy;
	int selectedFont;
	ImVec2 labelText;
	ImVec2 numberText;
	ImVec2 minPos;
	ImVec2 maxPos;
	ImU32 bg = IM_COL32( 10, 14, 10, 176 );
	ImU32 border = IM_COL32( 114, 196, 70, 190 );
	ImU32 muted = IM_COL32( 190, 214, 176, 230 );
	ImU32 gold = IM_COL32( 255, 211, 86, 255 );
	ImU32 shadow = IM_COL32( 0, 0, 0, 170 );

	Q_strncpyz( text, race.countdownText, sizeof( text ) );
	if ( !text[0] ) return;
	draw = ImGui::GetForegroundDrawList();
	selectedFont = (int)Com_Clamp( 0.0f, (float)( SRGUI_LIVESPLIT_FONT_COUNT - 1 ), (float)Cvar_VariableIntegerValue( "ls_imgui_font" ) );
	font = CL_ImGuiLiveSplitFont( selectedFont, 1 );
	numberFont = CL_ImGuiLiveSplitBoldFont( selectedFont, 3 );
	if ( !font ) font = ImGui::GetFont();
	if ( !numberFont ) numberFont = font;
	scale = Com_Clamp( 0.50f, 2.50f, Cvar_VariableValue( "ls_race_countdown_scale" ) );
	labelSize = 13.0f * scale;
	numberSize = 46.0f * scale;
	labelText = font->CalcTextSizeA( labelSize, 10000.0f, 0.0f, "START IN" );
	numberText = numberFont->CalcTextSizeA( numberSize, 10000.0f, 0.0f, text );
	maxTextWidth = labelText.x > numberText.x ? labelText.x : numberText.x;
	cx = cls.glconfig.vidWidth * 0.5f;
	cy = cls.glconfig.vidHeight * 0.40f;
	minPos = ImVec2( cx - maxTextWidth * 0.5f - 28.0f * scale, cy - 14.0f * scale );
	maxPos = ImVec2( cx + maxTextWidth * 0.5f + 28.0f * scale, cy + labelText.y + numberText.y + 20.0f * scale );
	draw->AddRectFilled( minPos, maxPos, bg, 6.0f * scale );
	draw->AddRect( minPos, maxPos, border, 6.0f * scale, 0, 1.0f * scale );
	draw->AddText( font, labelSize, ImVec2( cx - labelText.x * 0.5f + 1.0f, cy + 1.0f ), shadow, "START IN" );
	draw->AddText( font, labelSize, ImVec2( cx - labelText.x * 0.5f, cy ), muted, "START IN" );
	draw->AddText( numberFont, numberSize, ImVec2( cx - numberText.x * 0.5f + 2.0f, cy + labelText.y + 5.0f * scale + 2.0f ), shadow, text );
	draw->AddText( numberFont, numberSize, ImVec2( cx - numberText.x * 0.5f, cy + labelText.y + 5.0f * scale ), gold, text );
}

static void CL_ImGuiDrawRaceOverlay( void ) {
	const lsRaceUiSnapshot_t &race = *CL_ImGuiRaceSnapshotThisFrame();
	srRaceRow_t rows[8];
	int rowCount, rowIndex;
	char state[32];
	char timer[32];
	char ready[32];
	char status[128];
	char category[64];
	char activeFlags[64];
	char headerText[192];
	float screenX, screenY, scale, x, y, width, height, alpha;
	ImGuiWindowFlags flags;
	ImDrawList *draw;
	ImVec2 pos, size;
	ImU32 colBg, colBg2, colBorder;
	ImVec4 headerColor;
	ImVec4 timerColor;
	bool compactOverlay;
	char readySummary[32];
	bool hasActiveFlags;
	bool editMode;
	bool dragHovered = false;
	bool dragActive = false;
	bool resizeHovered = false;
	bool resizeActive = false;

	rowCount = race.playerCount;
	if ( rowCount > IM_ARRAYSIZE( rows ) ) rowCount = IM_ARRAYSIZE( rows );
	for ( rowIndex = 0; rowIndex < rowCount; ++rowIndex ) {
		rows[rowIndex] = race.players[rowIndex];
		CL_ImGuiRacePrettyToken( rows[rowIndex].stage );
		CL_ImGuiRacePrettyToken( rows[rowIndex].map );
	}
	compactOverlay = true;
	editMode = s_imguiOpen && Cvar_VariableIntegerValue( "ui_speedrun_layout_edit" ) != 0;
	Q_strncpyz( state, race.state, sizeof( state ) );
	Q_strncpyz( timer, race.timer, sizeof( timer ) );
	Q_strncpyz( ready, race.ready, sizeof( ready ) );
	Q_strncpyz( status, race.status, sizeof( status ) );
	Q_strncpyz( category, race.category, sizeof( category ) );
	if ( !state[0] ) Q_strncpyz( state, "Idle", sizeof( state ) );
	if ( !timer[0] ) Q_strncpyz( timer, "0.00", sizeof( timer ) );
	if ( !ready[0] ) Q_strncpyz( ready, "0/0", sizeof( ready ) );
	CL_ImGuiRaceReadySummary( ready, readySummary, sizeof( readySummary ) );
	CL_ImGuiRaceBuildCheatText( race.localCheatFlags, activeFlags, sizeof( activeFlags ), false );
	hasActiveFlags = activeFlags[0] != '\0';
	Com_sprintf( headerText, sizeof( headerText ), "Race  %s", state );
	if ( category[0] ) {
		Q_strcat( headerText, sizeof( headerText ), "  " );
		Q_strcat( headerText, sizeof( headerText ), category );
	}
	if ( readySummary[0] ) {
		Q_strcat( headerText, sizeof( headerText ), "  " );
		Q_strcat( headerText, sizeof( headerText ), readySummary );
	}
	if ( hasActiveFlags ) {
		Q_strcat( headerText, sizeof( headerText ), "  " );
		Q_strcat( headerText, sizeof( headerText ), activeFlags );
	}

	screenX = cls.glconfig.vidWidth / 640.0f;
	screenY = cls.glconfig.vidHeight / 480.0f;
	scale = Com_Clamp( 0.35f, 1.80f, Cvar_VariableValue( "ls_race_overlay_scale" ) ) * screenY;
	x = Cvar_VariableValue( "ls_race_overlay_x" ) * screenX;
	y = Cvar_VariableValue( "ls_race_overlay_y" ) * screenY;
	width = Com_Clamp( 110.0f, 500.0f, Cvar_VariableValue( "ls_race_overlay_w" ) ) * screenX;
	height = ( 54.0f + ( rowCount > 0 ? rowCount : 1 ) * 28.0f ) * scale;
	alpha = Com_Clamp( 0.20f, 1.0f, Cvar_VariableValue( "ls_race_overlay_opacity" ) );
	colBg = CL_ImGuiColorU32( "ls_clr_bg", ImVec4( 0.04f, 0.05f, 0.04f, 0.86f ), alpha );
	colBg2 = CL_ImGuiColorU32( "ls_clr_bg2", ImVec4( 0.06f, 0.08f, 0.05f, 0.78f ), alpha );
	colBorder = CL_ImGuiColorU32( "ls_clr_border", ImVec4( 0.18f, 0.32f, 0.14f, 0.72f ), alpha );
	headerColor = ImGui::ColorConvertU32ToFloat4( CL_ImGuiColorU32( "ls_clr_title", ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), alpha ) );
	timerColor = ImGui::ColorConvertU32ToFloat4( CL_ImGuiColorU32( "ls_clr_timer", ImVec4( 0.94f, 0.98f, 0.88f, 1.0f ), alpha ) );

	flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;
	if ( !editMode ) flags |= ImGuiWindowFlags_NoInputs;
	ImGui::SetNextWindowPos( ImVec2( x, y ), ImGuiCond_Always );
	ImGui::SetNextWindowSize( ImVec2( width, height ), ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f * scale, 6.0f * scale ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 6.0f * scale );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::Begin( "Race ImGui Overlay", NULL, flags );
	ImGui::SetWindowFontScale( scale );
	draw = ImGui::GetWindowDrawList();
	pos = ImGui::GetWindowPos();
	size = ImGui::GetWindowSize();
	if ( editMode ) {
		ImVec2 dragSize = ImVec2( size.x, ( 24.0f * scale < size.y ) ? 24.0f * scale : size.y );
		ImVec2 gripSize = ImVec2( 20.0f * scale, 20.0f * scale );
		ImVec2 gripMin = ImVec2( pos.x + size.x - gripSize.x - 4.0f * scale, pos.y + size.y - gripSize.y - 4.0f * scale );
		ImGui::SetCursorScreenPos( pos );
		ImGui::InvisibleButton( "race_overlay_drag", dragSize );
		dragHovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
		dragActive = ImGui::IsItemActive();
		if ( dragActive ) {
			s_liveSplitEditActive = true;
			if ( ImGui::IsMouseDragging( 0 ) ) {
				ImVec2 d = ImGui::GetIO().MouseDelta;
				Cvar_SetValue( "ls_race_overlay_x", Com_Clamp( 0.0f, 640.0f, Cvar_VariableValue( "ls_race_overlay_x" ) + d.x / screenX ) );
				Cvar_SetValue( "ls_race_overlay_y", Com_Clamp( 0.0f, 480.0f, Cvar_VariableValue( "ls_race_overlay_y" ) + d.y / screenY ) );
			}
		}
		ImGui::SetCursorScreenPos( gripMin );
		ImGui::InvisibleButton( "race_overlay_resize", gripSize );
		resizeHovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
		resizeActive = ImGui::IsItemActive();
		if ( resizeActive ) {
			s_liveSplitEditActive = true;
			if ( ImGui::IsMouseDragging( 0 ) ) {
				ImVec2 d = ImGui::GetIO().MouseDelta;
				Cvar_SetValue( "ls_race_overlay_w", Com_Clamp( 110.0f, 500.0f, Cvar_VariableValue( "ls_race_overlay_w" ) + d.x / screenX ) );
				Cvar_SetValue( "ls_race_overlay_scale", Com_Clamp( 0.35f, 1.80f, Cvar_VariableValue( "ls_race_overlay_scale" ) + d.y / screenY / 120.0f ) );
			}
		}
		ImGui::SetCursorScreenPos( ImVec2( pos.x + 8.0f * scale, pos.y + 6.0f * scale ) );
	}
	draw->AddRectFilledMultiColor( pos, ImVec2( pos.x + size.x, pos.y + size.y ), colBg2, colBg2, colBg, colBg );
	CL_ImGuiAddRectLines( draw, pos.x, pos.y, size.x, size.y, colBorder, 1.0f * scale );
	if ( editMode ) {
		ImU32 editCol = ( dragHovered || dragActive || resizeHovered || resizeActive ) ? IM_COL32( 245, 220, 80, 240 ) : IM_COL32( 120, 230, 72, 150 );
		CL_ImGuiAddRectLines( draw, pos.x, pos.y, size.x, size.y, editCol, 1.4f * scale );
		draw->AddTriangleFilled( ImVec2( pos.x + size.x - 4.0f * scale, pos.y + size.y - 17.0f * scale ), ImVec2( pos.x + size.x - 4.0f * scale, pos.y + size.y - 4.0f * scale ), ImVec2( pos.x + size.x - 17.0f * scale, pos.y + size.y - 4.0f * scale ), editCol );
	}

	{
		ImVec2 headerPos = ImGui::GetCursorScreenPos();
		float lineH = ImGui::GetTextLineHeight();
		float contentRight = pos.x + ImGui::GetWindowContentRegionMax().x;
		float timerW = ImGui::CalcTextSize( timer ).x;
		float clipRight = contentRight - timerW - 8.0f * scale;
		if ( clipRight > headerPos.x + 8.0f * scale ) {
			CL_ImGuiAddEllipsizedText( draw, headerPos, ImVec2( headerPos.x, headerPos.y - 2.0f * scale ), ImVec2( clipRight, headerPos.y + lineH + 2.0f * scale ), ImGui::ColorConvertFloat4ToU32( headerColor ), headerText, true );
		}
		CL_ImGuiAddTextRight( draw, contentRight, headerPos.y, ImGui::ColorConvertFloat4ToU32( timerColor ), timer, true );
		ImGui::Dummy( ImVec2( ImGui::GetContentRegionAvail().x, lineH + 2.0f * scale ) );
	}
	if ( status[0] && rowCount == 0 ) ImGui::TextDisabled( "%s", status );
	ImGui::Separator();
	if ( rowCount > 0 ) {
		CL_ImGuiDrawRaceOverlayList( rows, rowCount, compactOverlay, scale );
	} else {
		ImGui::TextDisabled( "No players" );
	}
	ImGui::SetWindowFontScale( 1.0f );
	ImGui::End();
	ImGui::PopStyleVar( 3 );
}

static bool CL_ImGuiShouldDrawRaceChat( void ) {
	const lsRaceUiSnapshot_t &race = *CL_ImGuiRaceSnapshotThisFrame();
	int now;
	int i;
	if ( Cvar_VariableIntegerValue( "ls_race_chat" ) == 0 && !s_raceChatOpen ) return false;
	if ( s_raceChatOpen ) return true;
	now = Sys_Milliseconds();
	for ( i = 0; i < LS_RACE_UI_CHAT_LINES; ++i ) {
		if ( !race.chat[i].text[0] ) continue;
		if ( race.chat[i].timeMs <= 0 || now - race.chat[i].timeMs < 7600 ) return true;
	}
	if ( !race.active ) return false;
	return false;
}

static void CL_ImGuiRaceChatCommandText( const char *text, char *out, int outSize ) {
	int i, o = 0;
	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	if ( !text ) return;
	for ( i = 0; text[i] && o < outSize - 1; ++i ) {
		unsigned char ch = (unsigned char)text[i];
		if ( ch == '\r' || ch == '\n' || ch == ';' || ch == '"' || ch == '\\' ) continue;
		if ( ch >= 32 && ch <= 126 ) out[o++] = (char)ch;
	}
	while ( o > 0 && out[o - 1] == ' ' ) o--;
	out[o] = '\0';
}

static bool CL_ImGuiRaceSubmitChatInput( char *input, int inputSize ) {
	char clean[128];
	char cmd[192];
	if ( !input || inputSize <= 0 ) {
		return false;
	}
	CL_ImGuiRaceChatCommandText( input, clean, sizeof( clean ) );
	input[0] = '\0';
	if ( !clean[0] ) {
		return false;
	}
	Com_sprintf( cmd, sizeof( cmd ), "ls_race_say \"%s\"\n", clean );
	Cbuf_AddText( cmd );
	return true;
}

static void CL_ImGuiDrawRaceChatOverlay( void ) {
	const lsRaceUiSnapshot_t &race = *CL_ImGuiRaceSnapshotThisFrame();
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	ImFont *font = ImGui::GetFont();
	float sx = cls.glconfig.vidWidth / 640.0f;
	float sy = cls.glconfig.vidHeight / 480.0f;
	float scale = Com_Clamp( 0.50f, 2.00f, Cvar_VariableValue( "ls_race_chat_scale" ) ) * Com_Clamp( 1.0f, 1.65f, sy );
	float inputScale = Com_Clamp( 0.50f, 2.00f, Cvar_VariableValue( "ls_race_chat_input_scale" ) ) * Com_Clamp( 1.0f, 1.65f, sy );
	float x = Cvar_VariableValue( "ls_race_chat_x" ) * sx;
	float y = Cvar_VariableValue( "ls_race_chat_y" ) * sy;
	float inputX = Cvar_VariableValue( "ls_race_chat_input_x" ) * sx;
	float inputY = Cvar_VariableValue( "ls_race_chat_input_y" ) * sy;
	float fontSize = ImGui::GetFontSize() * scale;
	float lineH = fontSize + 2.0f * scale;
	float pad = 6.0f * scale;
	float width = Com_Clamp( 80.0f, 620.0f, Cvar_VariableValue( "ls_race_chat_w" ) ) * sx;
	float inputWidth = Com_Clamp( 80.0f, 620.0f, Cvar_VariableValue( "ls_race_chat_input_w" ) ) * sx;
	float rowGap = 4.0f * scale;
	float rowH = lineH + pad * 1.15f;
	float inputH = 25.0f * inputScale;
	int i;
	const lsRaceUiChatLine_t *lines[LS_RACE_UI_CHAT_LINES];
	float alpha[LS_RACE_UI_CHAT_LINES];
	int count = 0;
	int now = Sys_Milliseconds();
	ImVec4 chatBgFallback = ImVec4( 0.03f, 0.04f, 0.03f, 0.50f );
	ImVec4 chatTextFallback = ImVec4( 0.89f, 0.94f, 0.86f, 0.95f );
	ImVec4 inputBgFallback = ImVec4( 0.03f, 0.04f, 0.03f, 0.82f );
	ImVec4 inputTextFallback = ImVec4( 0.88f, 0.94f, 0.84f, 1.0f );
	ImU32 inputBg = CL_ImGuiColorU32( "ls_race_chat_input_bg", inputBgFallback );
	ImU32 border = CL_ImGuiGuiAccentU32( 0.74f );
	ImU32 inputText = CL_ImGuiColorU32( "ls_race_chat_input_text", inputTextFallback );

	for ( i = LS_RACE_UI_CHAT_LINES - 1; i >= 0; --i ) {
		if ( race.chat[i].text[0] && count < IM_ARRAYSIZE( lines ) ) {
			int age = race.chat[i].timeMs > 0 ? now - race.chat[i].timeMs : 0;
			float a = 1.0f;
			if ( age < 0 ) age = 0;
			if ( !s_raceChatOpen ) {
				if ( age >= 7600 ) continue;
				if ( age > 6200 ) a = 1.0f - (float)( age - 6200 ) / 1400.0f;
			}
			alpha[count] = Com_Clamp( 0.0f, 1.0f, a );
			lines[count] = &race.chat[i];
			count++;
		}
	}
	if ( x < 0.0f ) x = 0.0f;
	if ( y < 0.0f ) y = 0.0f;
	if ( inputX < 0.0f ) inputX = 0.0f;
	if ( inputY < 0.0f ) inputY = 0.0f;
	for ( i = 0; i < count; ++i ) {
		const lsRaceUiChatLine_t *line = lines[i];
		char nick[40];
		float a = alpha[i];
		int aText = (int)( 242.0f * a );
		ImVec2 pos( x + pad, y + i * ( rowH + rowGap ) + pad * 0.50f );
		ImU32 bg = CL_ImGuiColorU32( "ls_race_chat_bg", chatBgFallback, a );
		ImU32 nickCol = IM_COL32( line->red, line->green, line->blue, aText );
		ImU32 textCol = CL_ImGuiColorU32( "ls_race_chat_text", chatTextFallback, a );
		float nickW;
		float textW;
		float rowW;
		Com_sprintf( nick, sizeof( nick ), "%s:", line->nick[0] ? line->nick : "Runner" );
		nickW = font->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, nick ).x;
		textW = font->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, line->text ).x;
		rowW = nickW + textW + pad * 3.8f;
		if ( rowW > width ) rowW = width;
		draw->AddRectFilled( ImVec2( x, pos.y - pad * 0.48f ), ImVec2( x + rowW, pos.y + rowH - pad * 0.38f ), bg, 4.0f * scale );
		draw->AddText( font, fontSize, ImVec2( pos.x + 1.0f, pos.y + 1.0f ), IM_COL32( 0, 0, 0, (int)( 170.0f * a ) ), nick );
		draw->AddText( font, fontSize, pos, nickCol, nick );
		CL_ImGuiAddEllipsizedTextSized( draw, font, fontSize, ImVec2( pos.x + nickW + 4.0f * scale, pos.y ), ImVec2( pos.x + nickW, pos.y - 2.0f * scale ), ImVec2( x + width - pad, pos.y + lineH + 2.0f * scale ), textCol, line->text, true );
	}
	if ( s_raceChatOpen ) {
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
		ImGui::SetNextWindowPos( ImVec2( inputX, inputY ), ImGuiCond_Always );
		ImGui::SetNextWindowSize( ImVec2( inputWidth, inputH ), ImGuiCond_Always );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 6.0f * inputScale, 3.0f * inputScale ) );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 4.0f * inputScale );
		ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 4.0f * inputScale, 1.0f * inputScale ) );
		ImGui::PushStyleColor( ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4( inputBg ) );
		ImGui::PushStyleColor( ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4( border ) );
		ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0, 0, 0, 0 ) );
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( inputText ) );
		ImGui::Begin( "Race Chat Input", NULL, flags );
		ImGui::SetWindowFontScale( inputScale );
		if ( s_raceChatFocus ) {
			ImGui::SetKeyboardFocusHere();
			s_raceChatFocus = false;
		}
		ImGui::PushItemWidth( ImGui::GetContentRegionAvail().x );
		if ( ImGui::InputTextWithHint( "##race_chat_input", "race chat...", s_raceChatInput, sizeof( s_raceChatInput ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
			if ( Sys_Milliseconds() < s_raceChatIgnoreSubmitUntilMs ) {
				s_raceChatInput[0] = '\0';
				CL_SpeedrunImGui_ClearRaceChatKeys();
			} else {
				CL_ImGuiRaceSubmitChatInput( s_raceChatInput, sizeof( s_raceChatInput ) );
				CL_SpeedrunImGui_CloseRaceChat();
			}
		}
		ImGui::PopItemWidth();
		ImGui::SetWindowFontScale( 1.0f );
		ImGui::End();
		ImGui::PopStyleColor( 4 );
		ImGui::PopStyleVar( 3 );
	}
}

static void CL_ImGuiDrawZoneTimerOverlay( void ) {
	ImDrawList *draw;
	ImFont *font;
	ImFont *boldFont;
	ImGuiWindowFlags flags;
	char runTime[64];
	char recordTime[64];
	char lastDelta[32];
	char lastTotalDelta[32];
	char progressText[32];
	char label[64];
	float sx, sy, scale, x, y, w, h, pad, alphaMul, rounding;
	float timerSize, smallSize, lineSize;
	int selectedFont, fontTier;
	bool showDelta, showProgress, shadow, lastDeltaPb, lastTotalDeltaPb;
	ImU32 colBg, colBg2, colBorder, colTimer, colMuted, colSeg, colFull;

	Cvar_VariableStringBuffer( "sp_zone_run_time", runTime, sizeof( runTime ) );
	Cvar_VariableStringBuffer( "sp_zone_record_time", recordTime, sizeof( recordTime ) );
	Cvar_VariableStringBuffer( "sp_zone_last_delta", lastDelta, sizeof( lastDelta ) );
	Cvar_VariableStringBuffer( "sp_zone_last_total_delta", lastTotalDelta, sizeof( lastTotalDelta ) );
	Cvar_VariableStringBuffer( "sp_zone_progress_text", progressText, sizeof( progressText ) );
	if ( !runTime[0] ) Q_strncpyz( runTime, "0.000", sizeof( runTime ) );
	if ( !recordTime[0] ) Q_strncpyz( recordTime, "-", sizeof( recordTime ) );
	if ( !progressText[0] ) Q_strncpyz( progressText, "-", sizeof( progressText ) );

	showDelta = Cvar_VariableIntegerValue( "sp_zone_delta_visible" ) != 0 && ( lastDelta[0] || lastTotalDelta[0] );
	showProgress = Cvar_VariableIntegerValue( "sp_zone_hud_progress" ) != 0 && Cvar_VariableIntegerValue( "sp_zone_progress_count" ) > 0;
	lastDeltaPb = Cvar_VariableIntegerValue( "sp_zone_last_delta_pb" ) != 0;
	lastTotalDeltaPb = Cvar_VariableIntegerValue( "sp_zone_last_total_delta_pb" ) != 0;
	shadow = Cvar_VariableIntegerValue( "ls_text_shadow" ) != 0;

	sx = cls.glconfig.vidWidth / 640.0f;
	sy = cls.glconfig.vidHeight / 480.0f;
	scale = Com_Clamp( 0.55f, 2.5f, Cvar_VariableValue( "sp_zone_hud_scale" ) ) * sy;
	x = Cvar_VariableValue( "sp_zone_hud_x" ) * sx;
	y = Cvar_VariableValue( "sp_zone_hud_y" ) * sy;
	w = 214.0f * scale;
	h = ( showDelta ? 76.0f : 54.0f ) * scale;
	pad = 8.0f * scale;
	rounding = 5.0f * scale;
	if ( rounding > 8.0f ) rounding = 8.0f;
	alphaMul = Com_Clamp( 0.0f, 1.0f, Cvar_VariableValue( "sp_zone_hud_alpha" ) );

	selectedFont = (int)Com_Clamp( 0.0f, (float)( SRGUI_LIVESPLIT_FONT_COUNT - 1 ), (float)Cvar_VariableIntegerValue( "ls_imgui_font" ) );
	fontTier = CL_ImGuiLiveSplitFontTierForScale( scale );
	font = CL_ImGuiLiveSplitFont( selectedFont, fontTier );
	boldFont = CL_ImGuiLiveSplitBoldFont( selectedFont, fontTier );
	if ( !font ) font = s_imguiTimerFont;
	if ( !boldFont ) boldFont = font;
	timerSize = 24.0f * scale;
	smallSize = 11.0f * scale;
	lineSize = 12.0f * scale;

	colBg = CL_ImGuiColorU32( "sp_zone_timer_clr_bg", ImVec4( 0.02f, 0.03f, 0.03f, 0.86f ), alphaMul );
	colBg2 = CL_ImGuiColorU32( "sp_zone_timer_clr_bg2", ImVec4( 0.06f, 0.10f, 0.07f, 0.78f ), alphaMul );
	colBorder = CL_ImGuiColorU32( "sp_zone_timer_clr_border", ImVec4( 0.41f, 0.67f, 0.28f, 0.46f ), alphaMul );
	colTimer = CL_ImGuiColorU32( "sp_zone_timer_clr_time", ImVec4( 0.73f, 0.97f, 0.56f, 1.00f ), alphaMul );
	colMuted = CL_ImGuiColorU32( "sp_zone_timer_clr_muted", ImVec4( 0.57f, 0.64f, 0.53f, 0.92f ), alphaMul );
	colSeg = CL_ImGuiZoneTimerDeltaColor( lastDelta, lastDeltaPb, alphaMul );
	colFull = CL_ImGuiZoneTimerDeltaColor( lastTotalDelta, lastTotalDeltaPb, alphaMul );

	flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
	ImGui::SetNextWindowPos( ImVec2( x, y ), ImGuiCond_Always );
	ImGui::SetNextWindowSize( ImVec2( w, h ), ImGuiCond_Always );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, rounding );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
	ImGui::Begin( "Zone Timer ImGui Overlay", NULL, flags );
	draw = ImGui::GetWindowDrawList();
	draw->AddRectFilledMultiColor( ImVec2( x, y ), ImVec2( x + w, y + h ), colBg2, colBg, colBg, colBg2 );
	CL_ImGuiAddRectLines( draw, x, y, w, h, colBorder, 1.0f * scale );
	CL_ImGuiZoneTimerTextRight( draw, boldFont, timerSize, x + w - pad, y + 5.0f * scale, colTimer, runTime, shadow );
	Com_sprintf( label, sizeof( label ), "PB %s", recordTime[0] ? recordTime : "-" );
	CL_ImGuiZoneTimerText( draw, font, smallSize, ImVec2( x + pad, y + 30.0f * scale ), colMuted, label, shadow );
	if ( showProgress ) {
		Com_sprintf( label, sizeof( label ), "CP %s", progressText );
		CL_ImGuiZoneTimerTextRight( draw, font, smallSize, x + w - pad, y + 30.0f * scale, colMuted, label, shadow );
	}
	if ( showDelta ) {
		Com_sprintf( label, sizeof( label ), "SEG %s", lastDelta[0] ? lastDelta : "-" );
		CL_ImGuiZoneTimerTextRight( draw, font, lineSize, x + w - pad, y + 47.0f * scale, colSeg, label, shadow );
		Com_sprintf( label, sizeof( label ), "FULL %s", lastTotalDelta[0] ? lastTotalDelta : "-" );
		CL_ImGuiZoneTimerTextRight( draw, font, lineSize, x + w - pad, y + 61.0f * scale, colFull, label, shadow );
	}
	ImGui::End();
	ImGui::PopStyleVar( 3 );
}

static void CL_ImGuiDrawLiveSplitOverlay( void ) {
	lsWndState_t st;
	ImDrawList *draw;
	ImFont *overlayFont, *boldFont;
	float sx, sy, uiScale, fontScale, x, y, w, rowH, panelH, contentX, contentR, textY, alphaMul, rounding, pad, nameR, componentGap, sepGap, afterSplitsGap, maxFontScale;
	float bestR, deltaR, timeR, statusW, nameFrac, bestFrac, deltaFrac, timerCardH, headerY0, headerY1, splitsY0, splitsY1;
	float timerSize, stageSize, infoSize, rgtSize, sideGap, timerSplit, splitNameSize, splitBestDeltaSize, splitDeltaSize, splitTimeSize, splitMaxSize;
	float timerMainScale, timerMainLineH, timerStageLineH, timerInfoLineH, timerLowerH, timerTopPad, timerLineGap, timerBottomPad;
	int i, scrollStart, scrollEnd, lastRow, cap, visibleRows, totalRows, statRows, selectedFont, fontTier;
	int style, currentRow;
	bool pinLast, hasPostSplits, showTitle, showHeader, showStats, showSeg, showRgt, showPb, showBest, showTimer, showSeps, showDeltas, showBestDeltas, showAtt, show100, showPrev, showGhostSeg, showBestSegments, showBorder, showHeaderBg, showStatus, showGradient, showCurrentBg, shadow, editMode, rightAligned;
	bool showSob, showPossibleSave, showBestPossible, showGoldRainbow, showPrevGoldRainbow, titleBold, attemptsBold, statusBold, headerBold, timerBold, stageBold, infoBold, splitBold, prevLabelBold, prevValueBold, ghostBold, statsBold, statsValueBold, rgtBold;
	bool splitNameBold, splitBestBold, splitDeltaBold, splitTimeBold, statSobLabelBold, statSobValueBold, statPossibleLabelBold, statPossibleValueBold, statBestLabelBold, statBestValueBold;
	float borderThickness;
	const char *curPbSeg, *curBestSeg, *curCompareLabel;
	ImU32 colBg, colBg2, colBorder, colHeader, colTimer, colText, colMap, colCurrent, colCompleted, colFuture, colAhead, colBehind, colGold, colDim, colSeg, colPaused, colSep, colHl, colLabel, colPanelTop, colStatusLive, colStatusReady, colStatusDone;
	ImU32 colStatusText, colPbValue, colBestValue, colPbLabel, colBestLabel, colPrevLabel, colPrevAhead, colPrevBehind, colPrevGold, colGhostLabel, colGhostTime, colStatLabel, colStatSobLabel, colStatSob, colStatPossibleLabel, colStatPossible, colStatPossibleZero, colStatPossibleMissing, colStatBestLabel, colStatBest, colRgt, colEmpty, colSplitTime, colSplitTimeCurrent, colSplitTimeCompleted;
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
	showPrev = showStats && Cvar_VariableIntegerValue( "ls_imgui_show_prevseg" ) != 0;
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
	showPrevGoldRainbow = Cvar_VariableIntegerValue( "ls_imgui_prev_gold_rainbow" ) != 0;
	titleBold = Cvar_VariableIntegerValue( "ls_imgui_bold_title" ) != 0;
	attemptsBold = Cvar_VariableIntegerValue( "ls_imgui_bold_attempts" ) != 0;
	statusBold = Cvar_VariableIntegerValue( "ls_imgui_bold_status" ) != 0;
	headerBold = Cvar_VariableIntegerValue( "ls_imgui_bold_header" ) != 0;
	timerBold = Cvar_VariableIntegerValue( "ls_imgui_bold_timer" ) != 0;
	stageBold = Cvar_VariableIntegerValue( "ls_imgui_bold_stage" ) != 0;
	infoBold = Cvar_VariableIntegerValue( "ls_imgui_bold_info" ) != 0;
	splitBold = Cvar_VariableIntegerValue( "ls_imgui_bold_splits" ) != 0;
	ghostBold = Cvar_VariableIntegerValue( "ls_imgui_bold_ghost" ) != 0;
	statsBold = Cvar_VariableIntegerValue( "ls_imgui_bold_stats" ) != 0;
	statsValueBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stats_values" ) != 0;
	prevLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_prev" ) != 0 || Cvar_VariableIntegerValue( "ls_imgui_bold_prev_label" ) != 0;
	prevValueBold = statsValueBold || Cvar_VariableIntegerValue( "ls_imgui_bold_prev" ) != 0 || Cvar_VariableIntegerValue( "ls_imgui_bold_prev_value" ) != 0;
	rgtBold = Cvar_VariableIntegerValue( "ls_imgui_bold_rgt" ) != 0;
	splitNameBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_name" ) != 0;
	splitBestBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_best" ) != 0;
	splitDeltaBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_delta" ) != 0;
	splitTimeBold = splitBold || Cvar_VariableIntegerValue( "ls_imgui_bold_split_time" ) != 0;
	statSobLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_sob_label" ) != 0;
	statSobValueBold = statsValueBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_sob_value" ) != 0;
	statPossibleLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_possible_label" ) != 0;
	statPossibleValueBold = statsValueBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_possible_value" ) != 0;
	statBestLabelBold = statsBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_best_label" ) != 0;
	statBestValueBold = statsValueBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stat_best_value" ) != 0;
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
	componentGap = Com_Clamp( 0.0f, 12.0f, Cvar_VariableValue( "ls_imgui_component_gap" ) ) * uiScale;
	sepGap = componentGap;
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

	panelH = pad * 2.0f;
	if ( showTitle ) panelH += ( style == 2 ? 26.0f : 34.0f ) * uiScale;
	else panelH += 16.0f * uiScale;
	panelH += sepGap;
	if ( showHeader ) panelH += 13.0f * uiScale + sepGap;
	panelH += totalRows * rowH;
	timerCardH = timerTopPad + timerMainLineH + ( showTimer && showSeg ? timerLineGap : 0.0f ) + timerLowerH + timerBottomPad;
	hasPostSplits = showTimer || showSeg || ( showPrev && st.prevSegValue[0] ) || ( showStats && showPossibleSave ) || ( showGhostSeg && st.ghostSegText[0] ) || statRows > 0 || ( showBestSegments && st.numBestSegs > 0 ) || showRgt || show100;
	afterSplitsGap = hasPostSplits ? sepGap : 0.0f;
	if ( showTimer || showSeg ) panelH += timerCardH;
	panelH += afterSplitsGap;
	if ( showPrev && st.prevSegValue[0] ) panelH += 15.0f * uiScale;
	if ( showStats && showPossibleSave ) panelH += 14.0f * uiScale;
	if ( showGhostSeg && st.ghostSegText[0] ) panelH += 16.0f * uiScale;
	if ( statRows > 0 ) panelH += statRows * 14.0f * uiScale;
	if ( showBestSegments && st.numBestSegs > 0 ) panelH += ( 14.0f * uiScale + sepGap ) + ( st.numBestSegs < 4 ? st.numBestSegs : 4 ) * rowH;
	rgtSize = Com_Clamp( 0.65f, 1.60f, Cvar_VariableValue( "ls_imgui_rgt_size" ) );
	if ( showRgt ) panelH += 13.5f * uiScale * rgtSize;
	if ( show100 ) panelH += 28.0f * uiScale + sepGap;
	if ( pinLast ) panelH += sepGap;

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
	colStatSobLabel = CL_ImGuiColorU32( "ls_clr_stat_sob_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colStatSob = CL_ImGuiColorU32( "ls_clr_stat_sob", ImVec4( 1.00f, 0.85f, 0.20f, 1.00f ), alphaMul );
	colStatPossibleLabel = CL_ImGuiColorU32( "ls_clr_stat_possible_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colStatPossible = CL_ImGuiColorU32( "ls_clr_stat_possible_save", ImVec4( 0.25f, 0.85f, 0.25f, 1.00f ), alphaMul );
	colStatPossibleZero = CL_ImGuiColorU32( "ls_clr_stat_possible_zero", ImVec4( 0.48f, 0.48f, 0.50f, 0.62f ), alphaMul );
	colStatPossibleMissing = CL_ImGuiColorU32( "ls_clr_stat_possible_missing", ImVec4( 0.32f, 0.36f, 0.30f, 0.70f ), alphaMul );
	colStatBestLabel = CL_ImGuiColorU32( "ls_clr_stat_best_possible_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.62f ), alphaMul );
	colStatBest = CL_ImGuiColorU32( "ls_clr_stat_best_possible", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colRgt = CL_ImGuiColorU32( "ls_clr_rgt", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colEmpty = CL_ImGuiColorU32( "ls_clr_empty", ImVec4( 0.48f, 0.48f, 0.50f, 0.52f ), alphaMul );
	colSplitTime = CL_ImGuiColorU32( "ls_clr_split_time", ImVec4( 0.85f, 0.88f, 0.85f, 0.90f ), alphaMul );
	colSplitTimeCurrent = CL_ImGuiColorU32( "ls_clr_split_time_current", ImVec4( 1.00f, 1.00f, 0.60f, 1.00f ), alphaMul );
	colSplitTimeCompleted = CL_ImGuiColorU32( "ls_clr_split_time_completed", ImVec4( 0.72f, 0.72f, 0.72f, 0.80f ), alphaMul );
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
	maxFontScale = splitMaxSize;
	if ( timerMainScale > maxFontScale ) maxFontScale = timerMainScale;
	if ( stageSize > maxFontScale ) maxFontScale = stageSize;
	if ( infoSize > maxFontScale ) maxFontScale = infoSize;
	if ( rgtSize > maxFontScale ) maxFontScale = rgtSize;
	fontTier = CL_ImGuiLiveSplitFontTierForScale( uiScale * maxFontScale );
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
	textY += sepGap;
	splitsY0 = textY;

	if ( showHeader ) {
		if ( headerBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, bestR, textY, colLabel, showBestDeltas ? "Best +/-" : "", shadow );
		CL_ImGuiAddTextRight( draw, deltaR, textY, colLabel, showDeltas ? "+/-" : "", shadow );
		CL_ImGuiAddTextRight( draw, timeR, textY, colLabel, "Time", shadow );
		if ( headerBold && boldFont ) ImGui::PopFont();
		textY += 13.0f * uiScale;
		if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
		textY += sepGap;
	}

	for ( i = scrollStart; i <= scrollEnd && i < st.numRows; ++i ) {
		ImU32 rowCol = st.rows[i].state == 1 ? colCurrent : ( st.rows[i].state == 2 ? colCompleted : colFuture );
		ImU32 rowTimeCol = st.rows[i].state == 1 ? colSplitTimeCurrent : ( st.rows[i].state == 2 ? colSplitTimeCompleted : colSplitTime );
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
			CL_ImGuiAddTextRight( draw, timeR, textY, rowTimeCol, st.rows[i].splitTime, shadow );
			if ( splitTimeBold && boldFont ) ImGui::PopFont();
		} else if ( st.rows[i].pbSplitTime[0] && st.rows[i].state != 2 ) {
			ImGui::SetWindowFontScale( fontScale * splitTimeSize );
			if ( splitTimeBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, rowTimeCol, st.rows[i].pbSplitTime, shadow );
			if ( splitTimeBold && boldFont ) ImGui::PopFont();
		}
		ImGui::SetWindowFontScale( fontScale );
		textY += rowH;
	}
	if ( pinLast && lastRow >= 0 ) {
		if ( showSeps ) draw->AddLine( ImVec2( contentX, textY ), ImVec2( contentR, textY ), colSep );
		textY += sepGap;
		i = lastRow;
		ImGui::SetWindowFontScale( fontScale * splitNameSize );
		if ( splitNameBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddEllipsizedText( draw, ImVec2( contentX, textY ), ImVec2( contentX, textY - 2.0f * uiScale ), ImVec2( nameR, textY + rowH ), st.rows[i].state == 2 ? colCompleted : colFuture, st.rows[i].name, shadow );
		if ( splitNameBold && boldFont ) ImGui::PopFont();
		ImGui::SetWindowFontScale( fontScale * splitTimeSize );
		if ( splitTimeBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, timeR, textY, st.rows[i].state == 1 ? colSplitTimeCurrent : ( st.rows[i].state == 2 ? colSplitTimeCompleted : colSplitTime ), st.rows[i].splitTime[0] ? st.rows[i].splitTime : st.rows[i].pbSplitTime, shadow );
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
	textY += afterSplitsGap;
	if ( showTimer || showSeg ) {
		float cardY = textY;
		float leftBlock = contentX;
		float rightBlock = contentR;
		float midX = x + w * timerSplit;
		float timerLeft = rightAligned ? midX + sideGap : leftBlock;
		float timerRight = rightAligned ? rightBlock : midX - sideGap;
		float mainY = timerTopPad;
		float lowerY = showTimer ? ( mainY + timerMainLineH + timerLineGap ) : timerTopPad;
		if ( showSeps ) draw->AddLine( ImVec2( contentX, cardY ), ImVec2( contentR, cardY ), colSep );
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
		ImU32 prevValueCol = st.prevSegGold ? ( showPrevGoldRainbow ? CL_ImGuiRainbowColorU32( alphaMul ) : colPrevGold ) : ( st.prevSegBehind ? colPrevBehind : colPrevAhead );
		if ( prevLabelBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colPrevLabel, st.prevSegLabel[0] ? st.prevSegLabel : "Previous", shadow );
		if ( prevLabelBold && boldFont ) ImGui::PopFont();
		if ( prevValueBold && boldFont ) ImGui::PushFont( boldFont );
		CL_ImGuiAddTextRight( draw, timeR, textY, prevValueCol, st.prevSegValue, shadow );
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
			CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colStatSobLabel, "Sum of best segment", shadow );
			if ( statSobLabelBold && boldFont ) ImGui::PopFont();
			if ( statSobValueBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextRight( draw, timeR, textY, colStatSob, st.sobText[0] ? st.sobText : "-----", shadow );
			if ( statSobValueBold && boldFont ) ImGui::PopFont();
			textY += 14.0f * uiScale;
		}
		if ( showBestPossible ) {
			if ( statBestLabelBold && boldFont ) ImGui::PushFont( boldFont );
			CL_ImGuiAddTextShadow( draw, ImVec2( contentX, textY ), colStatBestLabel, "Best Possible", shadow );
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
		textY += sepGap;
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
			CL_ImGuiAddTextRight( draw, timeR, textY, st.bestSegs[i].time[0] ? ( st.bestSegs[i].state == 1 ? colSplitTimeCurrent : ( st.bestSegs[i].state == 2 ? colSplitTimeCompleted : colSplitTime ) ) : colDim, st.bestSegs[i].time[0] ? st.bestSegs[i].time : "-----", shadow );
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
		textY += sepGap;
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
	CL_ImGuiOptionTooltip( button, command, desc );
	CL_ImGuiDrawCommandBindTools( command, desc );
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

static const char *CL_ImGuiFormatDemoSize( int bytes, char *out, int outSize ) {
	double value;
	const char *unit;
	if ( bytes < 0 ) {
		Q_strncpyz( out, "...", outSize );
		return out;
	}
	value = (double)bytes;
	unit = "B";
	if ( value >= 1024.0 ) {
		value /= 1024.0;
		unit = "KB";
	}
	if ( value >= 1024.0 ) {
		value /= 1024.0;
		unit = "MB";
	}
	if ( value >= 1024.0 ) {
		value /= 1024.0;
		unit = "GB";
	}
	if ( unit[0] == 'B' ) {
		Com_sprintf( out, outSize, "%d B", bytes );
	} else {
		Com_sprintf( out, outSize, "%.1f %s", value, unit );
	}
	return out;
}

static const char *CL_ImGuiFormatDemoDuration( int durationMs, char *out, int outSize ) {
	int totalSec, hours, minutes, seconds;
	if ( durationMs < 0 ) {
		Q_strncpyz( out, "...", outSize );
		return out;
	}
	totalSec = durationMs / 1000;
	hours = totalSec / 3600;
	minutes = ( totalSec / 60 ) % 60;
	seconds = totalSec % 60;
	if ( hours > 0 ) {
		Com_sprintf( out, outSize, "%d:%02d:%02d", hours, minutes, seconds );
	} else {
		Com_sprintf( out, outSize, "%d:%02d", minutes, seconds );
	}
	return out;
}

static void CL_ImGuiScanDemoMetadata( int index ) {
	int fileSize, durationMs, mapCount;
	if ( index < 0 || index >= s_demoCount ) {
		return;
	}
	if ( s_demoMetaState[index] != SRGUI_DEMOMETA_PENDING ) {
		return;
	}
	fileSize = -1;
	durationMs = -1;
	mapCount = 0;
	if ( CL_DemoGetMetadata( s_demoList[index], &fileSize, &durationMs, &mapCount ) ) {
		s_demoSizeBytes[index] = fileSize;
		s_demoDurationMs[index] = durationMs;
		s_demoMapCount[index] = mapCount;
		s_demoMetaState[index] = SRGUI_DEMOMETA_READY;
	} else {
		s_demoMetaState[index] = SRGUI_DEMOMETA_FAILED;
	}
	s_demoMetaRevision++;
}

static void CL_ImGuiScanDemoMetadataBudget( int maxScans ) {
	int attempts, scanned;
	if ( s_demoCount <= 0 || maxScans <= 0 ) {
		return;
	}
	if ( s_demoMetaScanCursor < 0 || s_demoMetaScanCursor >= s_demoCount ) {
		s_demoMetaScanCursor = 0;
	}
	scanned = 0;
	for ( attempts = 0; attempts < s_demoCount && scanned < maxScans; ++attempts ) {
		int index = ( s_demoMetaScanCursor + attempts ) % s_demoCount;
		if ( s_demoMetaState[index] == SRGUI_DEMOMETA_PENDING ) {
			CL_ImGuiScanDemoMetadata( index );
			s_demoMetaScanCursor = ( index + 1 ) % s_demoCount;
			scanned++;
		}
	}
}

static bool CL_ImGuiDemoMatchesFilter( int index );

static int CL_ImGuiDemoCompareValue( int a, int b, int column ) {
	int av = 0;
	int bv = 0;
	switch ( column ) {
	case SRGUI_DEMO_SORT_NEWEST:
		if ( s_demoMTime[a] != s_demoMTime[b] ) return s_demoMTime[b] - s_demoMTime[a];
		return -Q_stricmp( s_demoList[a], s_demoList[b] );
	case 2:
		av = s_demoCategory[a];
		bv = s_demoCategory[b];
		break;
	case 3:
		av = ( s_demoMetaState[a] == SRGUI_DEMOMETA_READY ) ? s_demoSizeBytes[a] : -1;
		bv = ( s_demoMetaState[b] == SRGUI_DEMOMETA_READY ) ? s_demoSizeBytes[b] : -1;
		break;
	case 4:
		av = ( s_demoMetaState[a] == SRGUI_DEMOMETA_READY ) ? s_demoDurationMs[a] : -1;
		bv = ( s_demoMetaState[b] == SRGUI_DEMOMETA_READY ) ? s_demoDurationMs[b] : -1;
		break;
	case 5:
		av = ( s_demoMetaState[a] == SRGUI_DEMOMETA_READY ) ? s_demoMapCount[a] : -1;
		bv = ( s_demoMetaState[b] == SRGUI_DEMOMETA_READY ) ? s_demoMapCount[b] : -1;
		break;
	default:
		return Q_stricmp( s_demoList[a], s_demoList[b] );
	}
	if ( av < 0 && bv >= 0 ) return 1;
	if ( av >= 0 && bv < 0 ) return -1;
	if ( av < bv ) return -1;
	if ( av > bv ) return 1;
	return Q_stricmp( s_demoList[a], s_demoList[b] );
}

static int QDECL CL_ImGuiDemoSortCompare( const void *lhs, const void *rhs ) {
	int a = *(const int *)lhs;
	int b = *(const int *)rhs;
	int cmp = CL_ImGuiDemoCompareValue( a, b, s_demoSortColumn );
	if ( cmp == 0 ) return 0;
	return s_demoSortAscending ? cmp : -cmp;
}

static int CL_ImGuiBuildSortedDemoIndices( int *indices, int maxIndices ) {
	int i, count = 0;
	bool sortUsesMeta;
	bool cacheValid;
	if ( !indices || maxIndices <= 0 ) return 0;
	sortUsesMeta = s_demoSortColumn >= 3 && s_demoSortColumn <= 5;
	cacheValid = s_demoSortedListRevision == s_demoListRevision &&
		s_demoSortedSortColumn == s_demoSortColumn &&
		s_demoSortedAscending == s_demoSortAscending &&
		s_demoSortedFilter == s_demoFilter &&
		s_demoSortedCountKey == s_demoCount &&
		!Q_stricmp( s_demoSortedSearch, s_demoSearch ) &&
		( !sortUsesMeta || s_demoSortedMetaRevision == s_demoMetaRevision );
	if ( cacheValid ) {
		count = s_demoSortedCount < maxIndices ? s_demoSortedCount : maxIndices;
		for ( i = 0; i < count; ++i ) {
			indices[i] = s_demoSortedCache[i];
		}
		return count;
	}

	for ( i = 0; i < s_demoCount && count < SRGUI_MAX_DEMOS; ++i ) {
		if ( CL_ImGuiDemoMatchesFilter( i ) ) {
			s_demoSortedCache[count++] = i;
		}
	}
	if ( count > 1 ) {
		qsort( s_demoSortedCache, count, sizeof( s_demoSortedCache[0] ), CL_ImGuiDemoSortCompare );
	}
	s_demoSortedCount = count;
	s_demoSortedListRevision = s_demoListRevision;
	s_demoSortedMetaRevision = s_demoMetaRevision;
	s_demoSortedSortColumn = s_demoSortColumn;
	s_demoSortedAscending = s_demoSortAscending;
	s_demoSortedFilter = s_demoFilter;
	s_demoSortedCountKey = s_demoCount;
	Q_strncpyz( s_demoSortedSearch, s_demoSearch, sizeof( s_demoSortedSearch ) );

	count = s_demoSortedCount < maxIndices ? s_demoSortedCount : maxIndices;
	for ( i = 0; i < count; ++i ) {
		indices[i] = s_demoSortedCache[i];
	}
	return count;
}

static void CL_ImGuiLoadDemos( void ) {
	char listBuf[131072];
	char ext[32];
	char demoQpath[MAX_QPATH + 16];
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
	s_demoMetaScanCursor = 0;
	s_demoPage = 0;
	name = listBuf;
	Com_sprintf( ext, sizeof( ext ), ".dm_%d", PROTOCOL_VERSION );
	for ( i = 0; i < count; ++i ) {
		len = strlen( name );
		if ( len > 0 ) {
			Com_sprintf( demoQpath, sizeof( demoQpath ), "demos/%s", name );
			Q_strncpyz( s_demoList[s_demoCount], name, sizeof( s_demoList[s_demoCount] ) );
			if ( len > (int)strlen( ext ) && !Q_stricmp( s_demoList[s_demoCount] + len - strlen( ext ), ext ) ) {
				s_demoList[s_demoCount][len - strlen( ext )] = '\0';
			}
			s_demoCategory[s_demoCount] = CL_ImGuiDemoCategory( s_demoList[s_demoCount] );
			s_demoMTime[s_demoCount] = FS_GetFileMTime( demoQpath );
			s_demoSizeBytes[s_demoCount] = -1;
			s_demoDurationMs[s_demoCount] = -1;
			s_demoMapCount[s_demoCount] = 0;
			s_demoMetaState[s_demoCount] = SRGUI_DEMOMETA_PENDING;
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
	s_demoListRevision++;
	s_demoMetaRevision++;
	s_demoSortedListRevision = -1;
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
	s_imguiOpen = false;
	s_raceGuiOpen = false;
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "0" );
	}
}

static void CL_ImGuiDrawDemoStats( int visibleCount ) {
	int counts[4] = { 0, 0, 0, 0 };
	int i;
	for ( i = 0; i < s_demoCount; ++i ) {
		if ( s_demoCategory[i] >= 0 && s_demoCategory[i] < 4 ) {
			counts[s_demoCategory[i]]++;
		}
	}
	CL_ImGuiBeginAutoBox( "demo_stats" );
	if ( ImGui::BeginTable( "demo_stats_table", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Total" ); ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%d", s_demoCount );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Visible" ); ImGui::Text( "%d", visibleCount );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Full Game" ); ImGui::Text( "%d", counts[SRGUI_DEMOCAT_FULLGAME] );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Mission" ); ImGui::Text( "%d", counts[SRGUI_DEMOCAT_MISSION] );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "IL" ); ImGui::Text( "%d", counts[SRGUI_DEMOCAT_IL] );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Other" ); ImGui::Text( "%d", counts[SRGUI_DEMOCAT_OTHER] );
		ImGui::EndTable();
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawDemoPagination( int visibleCount, int pageCount ) {
	int p;
	int lastDrawn = -1;
	char label[32];
	int first = visibleCount > 0 ? s_demoPage * SRGUI_DEMOS_PER_PAGE + 1 : 0;
	int last = ( s_demoPage + 1 ) * SRGUI_DEMOS_PER_PAGE;
	if ( last > visibleCount ) last = visibleCount;
	CL_ImGuiBeginAutoBox( "demo_pages" );
	ImGui::TextDisabled( "Showing %d-%d of %d", first, last, visibleCount );
	if ( pageCount > 1 ) {
		CL_ImGuiSameLineIfFits( 96.0f );
		ImGui::BeginDisabled( s_demoPage <= 0 );
		if ( ImGui::SmallButton( "Prev##demo_page_prev" ) ) s_demoPage--;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled( s_demoPage >= pageCount - 1 );
		if ( ImGui::SmallButton( "Next##demo_page_next" ) ) s_demoPage++;
		ImGui::EndDisabled();
		for ( p = 0; p < pageCount; ++p ) {
			if ( p == 0 || p == pageCount - 1 || abs( p - s_demoPage ) <= 2 ) {
				if ( lastDrawn >= 0 && p - lastDrawn > 1 ) {
					ImGui::SameLine();
					ImGui::TextDisabled( "..." );
				}
				ImGui::SameLine();
				Com_sprintf( label, sizeof( label ), "%d##demo_page_%d", p + 1, p );
				if ( p == s_demoPage ) {
					ImGui::BeginDisabled( true );
					ImGui::SmallButton( label );
					ImGui::EndDisabled();
				} else if ( ImGui::SmallButton( label ) ) {
					s_demoPage = p;
				}
				lastDrawn = p;
			}
		}
	}
	if ( s_demoPage < 0 ) s_demoPage = 0;
	if ( s_demoPage >= pageCount ) s_demoPage = pageCount > 0 ? pageCount - 1 : 0;
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
	const char *p1 = strchr( text, '|' );
	if ( p1 ) {
		const char *p2 = strchr( p1 + 1, '|' );
		const char *p3 = p2 ? strchr( p2 + 1, '|' ) : NULL;
		const char *p4 = p3 ? strchr( p3 + 1, '|' ) : NULL;
		int len;
		if ( p2 && p3 && p4 ) {
			len = (int)( p1 - text );
			if ( len >= mapSize ) len = mapSize - 1;
			memcpy( map, text, len );
			map[len] = '\0';
			len = (int)( p2 - ( p1 + 1 ) );
			if ( len >= goldSize ) len = goldSize - 1;
			memcpy( gold, p1 + 1, len );
			gold[len] = '\0';
			len = (int)( p3 - ( p2 + 1 ) );
			if ( len >= pbSize ) len = pbSize - 1;
			memcpy( pb, p2 + 1, len );
			pb[len] = '\0';
			*attempts = atoi( p3 + 1 );
			*completions = atoi( p4 + 1 );
			return true;
		}
	}
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

static void CL_ImGuiFormatRecordTimeMs( int ms, char *out, int outSize ) {
	int hours, minutes, seconds, hundredths;
	if ( !out || outSize <= 0 ) return;
	if ( ms <= 0 ) {
		Q_strncpyz( out, "---", outSize );
		return;
	}
	hours = ms / 3600000;
	minutes = ( ms / 60000 ) % 60;
	seconds = ( ms / 1000 ) % 60;
	hundredths = ( ms % 1000 ) / 10;
	if ( hours > 0 ) {
		Com_sprintf( out, outSize, "%d:%02d:%02d.%02d", hours, minutes, seconds, hundredths );
	} else {
		Com_sprintf( out, outSize, "%d:%02d.%02d", minutes, seconds, hundredths );
	}
}

static int CL_ImGuiParseRecordPbChart( const char *text, int *values, int maxValues ) {
	int count = 0;
	const char *p = text;
	if ( !text || !values || maxValues <= 0 ) return 0;
	while ( *p && count < maxValues ) {
		while ( *p == ',' || *p == ' ' || *p == '\t' ) p++;
		if ( !*p ) break;
		values[count++] = atoi( p );
		while ( *p && *p != ',' ) p++;
	}
	return count;
}

static ImVec4 CL_ImGuiColorVec4( const char *name, const ImVec4 &fallback, float alphaMul = 1.0f ) {
	return ImGui::ColorConvertU32ToFloat4( CL_ImGuiColorU32( name, fallback, alphaMul ) );
}

static void CL_ImGuiDrawRecordPbChart( const int *values, int count ) {
	ImGuiStyle &style = ImGui::GetStyle();
	ImDrawList *draw;
	ImVec2 canvasPos;
	ImVec2 canvasSize;
	ImVec2 plotMin;
	ImVec2 plotMax;
	ImVec2 mouse;
	int i;
	int bestMs, worstMs;
	int hoverIndex = -1;
	int hoverMs = 0;
	float hoverDistSq = 1000000.0f;
	char label[32];
	ImU32 bgTop = CL_ImGuiColorU32( "ls_clr_bg2", ImVec4( 0.02f, 0.03f, 0.02f, 0.96f ), 1.0f );
	ImU32 bgBottom = CL_ImGuiColorU32( "ls_clr_bg", ImVec4( 0.04f, 0.05f, 0.04f, 0.94f ), 1.0f );
	ImU32 grid = CL_ImGuiColorU32( "ls_clr_sep", ImVec4( 0.22f, 0.38f, 0.12f, 0.18f ), 0.90f );
	ImU32 axis = CL_ImGuiColorU32( "ls_clr_column_label", ImVec4( 0.42f, 0.48f, 0.38f, 0.42f ), 0.90f );
	ImU32 border = CL_ImGuiColorU32( "ls_clr_border", ImVec4( 0.20f, 0.35f, 0.15f, 0.50f ), 1.0f );
	ImU32 line = CL_ImGuiColorU32( "ls_clr_pb_value", ImVec4( 0.82f, 0.90f, 0.72f, 1.00f ), 1.0f );
	ImU32 lineGlow = CL_ImGuiColorU32( "ls_clr_title", ImVec4( 0.58f, 0.92f, 0.34f, 1.00f ), 0.30f );
	ImU32 point = CL_ImGuiColorU32( "ls_clr_gold", ImVec4( 1.00f, 0.85f, 0.20f, 1.00f ), 1.0f );
	ImU32 pointCore = CL_ImGuiColorU32( "ls_clr_bg", ImVec4( 0.04f, 0.05f, 0.04f, 1.00f ), 1.0f );
	ImU32 hoverRing = CL_ImGuiColorU32( "ls_clr_timer", ImVec4( 0.94f, 0.98f, 0.88f, 1.00f ), 1.0f );

	ImGui::TextColored( CL_ImGuiColorVec4( "ls_clr_title", ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ) ), "PB Progression" );
	canvasSize = ImVec2( ImGui::GetContentRegionAvail().x, 178.0f );
	if ( canvasSize.x < 240.0f ) canvasSize.x = 240.0f;
	canvasPos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton( "##records_pb_chart", canvasSize );
	draw = ImGui::GetWindowDrawList();
	draw->AddRectFilledMultiColor( canvasPos, ImVec2( canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y ), bgTop, bgTop, bgBottom, bgBottom );
	draw->AddRect( canvasPos, ImVec2( canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y ), border, style.ChildRounding );

	if ( count <= 0 ) {
		draw->AddText( ImVec2( canvasPos.x + 14.0f, canvasPos.y + canvasSize.y * 0.5f - ImGui::GetTextLineHeight() * 0.5f ), ImGui::GetColorU32( ImGuiCol_TextDisabled ), "No PB history" );
		return;
	}

	bestMs = values[0];
	worstMs = values[0];
	for ( i = 1; i < count; ++i ) {
		if ( values[i] < bestMs ) bestMs = values[i];
		if ( values[i] > worstMs ) worstMs = values[i];
	}
	if ( worstMs <= bestMs ) worstMs = bestMs + 1;

	plotMin = ImVec2( canvasPos.x + 78.0f, canvasPos.y + 16.0f );
	plotMax = ImVec2( canvasPos.x + canvasSize.x - 14.0f, canvasPos.y + canvasSize.y - 24.0f );
	if ( plotMax.x <= plotMin.x + 1.0f ) plotMax.x = plotMin.x + 1.0f;

	for ( i = 0; i < 3; ++i ) {
		float y = plotMin.y + ( plotMax.y - plotMin.y ) * (float)i / 2.0f;
		int labelMs = i == 0 ? worstMs : ( i == 1 ? ( worstMs + bestMs ) / 2 : bestMs );
		CL_ImGuiFormatRecordTimeMs( labelMs, label, sizeof( label ) );
		draw->AddLine( ImVec2( plotMin.x, y ), ImVec2( plotMax.x, y ), grid );
		draw->AddText( ImVec2( canvasPos.x + 12.0f, y - ImGui::GetTextLineHeight() * 0.5f ), ImGui::GetColorU32( ImGuiCol_TextDisabled ), label );
	}
	draw->AddLine( ImVec2( plotMin.x, plotMin.y ), ImVec2( plotMin.x, plotMax.y ), axis );
	draw->AddLine( ImVec2( plotMin.x, plotMax.y ), ImVec2( plotMax.x, plotMax.y ), axis );

	mouse = ImGui::GetIO().MousePos;
	for ( i = 0; i < count; ++i ) {
		float x = count > 1 ? plotMin.x + ( plotMax.x - plotMin.x ) * (float)i / (float)( count - 1 ) : ( plotMin.x + plotMax.x ) * 0.5f;
		float y = plotMin.y + ( plotMax.y - plotMin.y ) * (float)( worstMs - values[i] ) / (float)( worstMs - bestMs );
		if ( i > 0 ) {
			float px = count > 1 ? plotMin.x + ( plotMax.x - plotMin.x ) * (float)( i - 1 ) / (float)( count - 1 ) : x;
			float py = plotMin.y + ( plotMax.y - plotMin.y ) * (float)( worstMs - values[i - 1] ) / (float)( worstMs - bestMs );
			draw->AddLine( ImVec2( px, py ), ImVec2( x, y ), lineGlow, 5.0f );
			draw->AddLine( ImVec2( px, py ), ImVec2( x, y ), line, 2.2f );
		}
		draw->AddCircleFilled( ImVec2( x, y ), 4.6f, point );
		draw->AddCircleFilled( ImVec2( x, y ), 2.2f, pointCore );
		if ( ImGui::IsItemHovered() ) {
			float dx = mouse.x - x;
			float dy = mouse.y - y;
			float distSq = dx * dx + dy * dy;
			if ( distSq < hoverDistSq && distSq <= 64.0f ) {
				hoverDistSq = distSq;
				hoverIndex = i + 1;
				hoverMs = values[i];
			}
		}
	}

	if ( ImGui::IsItemHovered() && hoverMs > 0 ) {
		float x = count > 1 ? plotMin.x + ( plotMax.x - plotMin.x ) * (float)( hoverIndex - 1 ) / (float)( count - 1 ) : ( plotMin.x + plotMax.x ) * 0.5f;
		float y = plotMin.y + ( plotMax.y - plotMin.y ) * (float)( worstMs - hoverMs ) / (float)( worstMs - bestMs );
		draw->AddCircle( ImVec2( x, y ), 7.5f, hoverRing, 24, 1.6f );
		CL_ImGuiFormatRecordTimeMs( hoverMs, label, sizeof( label ) );
		ImGui::BeginTooltip();
		if ( hoverIndex > 0 ) ImGui::Text( "PB #%d", hoverIndex );
		ImGui::TextColored( CL_ImGuiColorVec4( "ls_clr_timer", ImVec4( 0.94f, 0.98f, 0.88f, 1.0f ) ), "%s", label );
		ImGui::EndTooltip();
	}
}

static void CL_ImGuiDrawRecordStats( const float *attempts, const float *completions, int count, const int *pbChart, int pbChartCount ) {
	float totalAttempts = 0.0f;
	float totalCompletions = 0.0f;
	int i;
	for ( i = 0; i < count; ++i ) {
		totalAttempts += attempts[i];
		totalCompletions += completions[i];
	}
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Records Statistics" );
	ImGui::TextDisabled( "Visible rows: %d    Attempts: %.0f    Completions: %.0f", count, totalAttempts, totalCompletions );
	CL_ImGuiDrawRecordPbChart( pbChart, pbChartCount );
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
	case VK_NUMPAD0: return K_KP_INS;
	case VK_NUMPAD1: return K_KP_END;
	case VK_NUMPAD2: return K_KP_DOWNARROW;
	case VK_NUMPAD3: return K_KP_PGDN;
	case VK_NUMPAD4: return K_KP_LEFTARROW;
	case VK_NUMPAD5: return K_KP_5;
	case VK_NUMPAD6: return K_KP_RIGHTARROW;
	case VK_NUMPAD7: return K_KP_HOME;
	case VK_NUMPAD8: return K_KP_UPARROW;
	case VK_NUMPAD9: return K_KP_PGUP;
	case VK_DECIMAL: return K_KP_DEL;
	case VK_DIVIDE: return K_KP_SLASH;
	case VK_MULTIPLY: return K_KP_STAR;
	case VK_SUBTRACT: return K_KP_MINUS;
	case VK_ADD: return K_KP_PLUS;
	case VK_NUMLOCK: return K_KP_NUMLOCK;
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

static void CL_ImGuiClearBindingCommand( const char *command ) {
	if ( !command || !command[0] ) {
		return;
	}
	for ( int key = 0; key < 256; ++key ) {
		const char *binding = Key_GetBinding( key );
		if ( binding && binding[0] && Q_stricmp( binding, command ) == 0 ) {
			Key_SetBinding( key, "" );
		}
	}
	if ( s_pendingBindCommand == command ) {
		s_pendingBindCommand = NULL;
	}
}

static void CL_ImGuiAssignPendingBind( int keynum ) {
	if ( !s_pendingBindCommand ) {
		return;
	}
	const char *command = s_pendingBindCommand;
	s_pendingBindCommand = NULL;
	if ( keynum > 0 && keynum != K_ESCAPE ) {
		CL_ImGuiClearBindingCommand( command );
		Key_SetBinding( keynum, command );
	}
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
	if ( ImGui::SmallButton( "Clear" ) ) {
		CL_ImGuiClearBindingCommand( command );
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

#if 0
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
	CL_ImGuiColorCvarName( "Time Column - all", "ls_clr_split_time", text );
	CL_ImGuiColorCvarName( "Time Column - current", "ls_clr_split_time_current", current );
	CL_ImGuiColorCvarName( "Time Column - completed", "ls_clr_split_time_completed", completed );
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
#include "speedrun_imgui/sr_imgui_livesplit_pages.inl"
	Cvar_Set( "ls_clr_split_time_completed", "150 160 146 0.72" );
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
	Cvar_Set( "ls_clr_stat_sob_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_sob", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_stat_possible_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_possible_save", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_stat_possible_zero", "112 118 112 0.62" );
	Cvar_Set( "ls_clr_stat_possible_missing", "82 92 76 0.70" );
	Cvar_Set( "ls_clr_stat_best_possible_label", "128 142 118 0.68" );
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
	static const char *fontLabels[] = { "Segoe UI", "Consolas", "Arial", "Tahoma", "Verdana", "Trebuchet", "Calibri", "Courier", "Impact", "Times", "Georgia", "Lucida Console", "Segoe Bold", "Candara", "Corbel", "Calibri Bold", "Segoe Semibold", "Segoe Light", "Segoe Italic", "Arial Italic", "Arial Bold Italic", "Cambria", "Cambria Bold", "Constantia", "Constantia Bold", "Comic Sans", "Comic Sans Bold", "Gadugi", "Gadugi Bold", "Bahnschrift", "Palatino Italic", "Palatino Bold", "Arial Black" };
	static const int fontValues[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
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

	ImGui::BeginChild( "ls_box_panel", ImVec2( 0, CL_ImGuiAutoBoxHeight( 19 ) ), true, 0 );
	ImGui::TextUnformatted( "Panel / background" );
	CL_ImGuiBoolCvarName( "Show LiveSplit Panel", "ls_draw", "1" );
	CL_ImGuiBoolCvarName( "Panel Border", "ls_imgui_show_border", "1" );
	CL_ImGuiBoolCvarName( "Header Background", "ls_imgui_header_bg", "0" );
	CL_ImGuiBoolCvarName( "Panel Gradient", "ls_imgui_gradient", "0" );
	CL_ImGuiComboCvarName( "LiveSplit Font", "ls_imgui_font", "1", fontLabels, fontValues, IM_ARRAYSIZE( fontValues ) );
	CL_ImGuiSliderCvarName( "Width", "ls_w", "215", 80.0f, 400.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Global Scale", "ls_scale", "0.550000", 0.5f, 2.5f, "%.2f" );
	CL_ImGuiSliderCvarName( "Padding", "ls_imgui_padding", "5", 3.0f, 14.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Component Gap", "ls_imgui_component_gap", "4", 0.0f, 12.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Border Thickness", "ls_imgui_border_size", "0.500000", 0.5f, 4.0f, "%.1f" );
	CL_ImGuiSliderCvarName( "Gradient Angle DEG", "ls_imgui_gradient_angle", "230", 0.0f, 360.0f, "%.0f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_panel", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Gradient top / solid background", "ls_clr_bg", bg );
		CL_ImGuiColorCvarName( "Gradient bottom", "ls_clr_bg2", dim );
		CL_ImGuiColorCvarName( "Panel Border", "ls_clr_border", border );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_text_gradient", ImVec2( 0, CL_ImGuiAutoBoxHeight( 6 ) ), true, 0 );
	ImGui::TextUnformatted( "Text gradient" );
	CL_ImGuiBoolCvarName( "Gradient on text", "ls_imgui_text_gradient", "0" );
	CL_ImGuiSliderCvarName( "Text Gradient Angle DEG", "ls_imgui_text_gradient_angle", "0", 0.0f, 360.0f, "%.0f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_text_gradient", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Text gradient end", "ls_clr_text_gradient2", header );
		ImGui::TreePop();
	}
	ImGui::TextDisabled( "Uses each component color as start and this color as end." );
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_title", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, 0 );
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

	ImGui::BeginChild( "ls_box_status", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, 0 );
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

	ImGui::BeginChild( "ls_box_columns", ImVec2( 0, CL_ImGuiAutoBoxHeight( 7 ) ), true, 0 );
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

	ImGui::BeginChild( "ls_box_timer", ImVec2( 0, CL_ImGuiAutoBoxHeight( 27 ) ), true, 0 );
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

	ImGui::BeginChild( "ls_box_splits", ImVec2( 0, CL_ImGuiAutoBoxHeight( 30 ) ), true, 0 );
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
		CL_ImGuiColorCvarName( "Time column - all", "ls_clr_split_time", text );
		CL_ImGuiColorCvarName( "Time column - current", "ls_clr_split_time_current", current );
		CL_ImGuiColorCvarName( "Time column - completed", "ls_clr_split_time_completed", completed );
		CL_ImGuiColorCvarName( "Gold / best delta", "ls_clr_gold", gold );
		CL_ImGuiColorCvarName( "Ahead delta", "ls_clr_ahead", ahead );
		CL_ImGuiColorCvarName( "Behind delta", "ls_clr_behind", behind );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_ghost", ImVec2( 0, CL_ImGuiAutoBoxHeight( 6 ) ), true, 0 );
	ImGui::TextUnformatted( "Ghost segment" );
	CL_ImGuiBoolCvarName( "Ghost Segment", "ls_imgui_show_ghostseg", "0" );
	CL_ImGuiBoolCvarName( "Bold ghost", "ls_imgui_bold_ghost", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_ghost", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Ghost label", "ls_clr_ghost_label", mapname );
		CL_ImGuiColorCvarName( "Ghost time", "ls_clr_ghost_time", timer );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stats", ImVec2( 0, CL_ImGuiAutoBoxHeight( 8 ) ), true, 0 );
	ImGui::TextUnformatted( "Statistics: master" );
	CL_ImGuiBoolCvar( "Statistics", s_ls_showstats );
	CL_ImGuiBoolCvarName( "Previous Segment", "ls_imgui_show_prevseg", "1" );
	CL_ImGuiBoolCvarName( "Bold all statistics", "ls_imgui_bold_stats", "0" );
	CL_ImGuiBoolCvarName( "Bold all statistic values", "ls_imgui_bold_stats_values", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_stats", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Statistics labels", "ls_clr_stat_label", mapname );
		ImGui::TreePop();
	}
	ImGui::EndChild();
	const bool statAllBold = Cvar_VariableIntegerValue( "ls_imgui_bold_stats" ) != 0;
	const bool statValueAllBold = statAllBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stats_values" ) != 0;

	ImGui::BeginChild( "ls_box_stat_sob", ImVec2( 0, CL_ImGuiAutoBoxHeight( 8 ) ), true, 0 );
	ImGui::TextUnformatted( "Statistic: Sum of best" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_sob", "1" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_sob_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_sob_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_sob", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Sum of best label", "ls_clr_stat_sob_label", mapname );
		CL_ImGuiColorCvarName( "Sum of best value", "ls_clr_stat_sob", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stat_possible", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, 0 );
	ImGui::TextUnformatted( "Statistic: Possible save" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_possible_save", "1" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_possible_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_possible_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_possible", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Possible save label", "ls_clr_stat_possible_label", mapname );
		CL_ImGuiColorCvarName( "Possible save value", "ls_clr_stat_possible_save", ahead );
		CL_ImGuiColorCvarName( "Possible save zero", "ls_clr_stat_possible_zero", dim );
		CL_ImGuiColorCvarName( "Possible save missing", "ls_clr_stat_possible_missing", dim );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_stat_best", ImVec2( 0, CL_ImGuiAutoBoxHeight( 8 ) ), true, 0 );
	ImGui::TextUnformatted( "Statistic: Best possible" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_best_possible", "1" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_best_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_best_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_best", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Best possible label", "ls_clr_stat_best_possible_label", mapname );
		CL_ImGuiColorCvarName( "Best possible value", "ls_clr_stat_best_possible", text );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_prev", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, 0 );
	ImGui::TextUnformatted( "Statistic: Previous segment" );
	ImGui::TextDisabled( "Shown with Statistics; this box keeps separate styling." );
	CL_ImGuiBoolCvarName( "Rainbow when gold", "ls_imgui_prev_gold_rainbow", "0" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold previous label", "ls_imgui_bold_prev_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold previous value", "ls_imgui_bold_prev_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_prev", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Previous label", "ls_clr_prev_label", mapname );
		CL_ImGuiColorCvarName( "Previous ahead", "ls_clr_prev_ahead", ahead );
		CL_ImGuiColorCvarName( "Previous behind", "ls_clr_prev_behind", behind );
		CL_ImGuiColorCvarName( "Previous gold", "ls_clr_prev_gold", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	ImGui::BeginChild( "ls_box_extra", ImVec2( 0, CL_ImGuiAutoBoxHeight( 10 ) ), true, 0 );
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

#endif

static bool CL_ImGuiRaceSettingsLocked( void ) {
	return Cvar_VariableIntegerValue( "ls_race_active" ) != 0;
}

#include "speedrun_imgui/sr_imgui_livesplit_pages.inl"

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

	CL_ImGuiSectionHeader( "Keystrokes", "Position, shape and motion feedback for the keyboard/mouse overlay." );
	CL_ImGuiBeginAutoBox( "hud_keys_behavior" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Behavior" );
	CL_ImGuiBoolCvarName( "Show Keystrokes", "cg_drawKeys", "1" );
	CL_ImGuiBoolCvarName( "Only During Gameplay", "ks_ingame_only", "1" );
	Cvar_Set( "ks_imgui", "1" );
	CL_ImGuiComboCvarName( "Keys Layout", "ks_layout", "0", keysLayoutLabels, keysLayoutValues, IM_ARRAYSIZE( keysLayoutValues ) );
	CL_ImGuiComboCvarName( "Press Effect", "ks_effect", "1", keysEffectLabels, keysEffectValues, IM_ARRAYSIZE( keysEffectValues ) );
	CL_ImGuiSliderCvarName( "Keys Opacity", "ks_opacity", "1.0", 0.0f, 1.0f, "%.2f" );
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "hud_keys_geometry" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Layout and sizing" );
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
	if ( usesMouseDisplay ) {
		CL_ImGuiComboCvarName( "Mouse Display", "ks_mouse", "2", mouseLabels, mouseValues, IM_ARRAYSIZE( mouseValues ) );
	} else {
		ImGui::TextDisabled( "Mouse display is built into the selected grid layout." );
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
	ImGui::EndChild();

	if ( usesExtraKeys ) {
		CL_ImGuiBeginAutoBox( "hud_keys_extra" );
		ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Optional keys" );
		CL_ImGuiBoolCvarName( "Show Use", "ks_show_use", "0" );
		CL_ImGuiBoolCvarName( "Show Reload", "ks_show_reload", "0" );
		ImGui::EndChild();
	}
	if ( ImGui::CollapsingHeader( "Keystroke Colors" ) ) {
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
	}

	CL_ImGuiSectionHeader( "HUD Elements", NULL );
	CL_ImGuiBeginAutoBox( "hud_elements_card" );
	CL_ImGuiBoolCvarName( "Show LiveSplit", "ls_draw", "1" );
	CL_ImGuiBoolCvarName( "Speedometer", "cg_drawVelocity", "1" );
	CL_ImGuiBoolCvarName( "Position HUD", "cg_drawPos", "0" );
	CL_ImGuiBoolCvarName( "Jump Statistics", "cg_drawJumpStats", "0" );
	CL_ImGuiBoolCvarName( "Strafe Guide", "cg_strafeGuide", "0" );
	CL_ImGuiBoolCvarName( "Show FPS", "cg_drawfps", "0" );
	CL_ImGuiBoolCvarName( "Show Timer", "cg_drawTimer", "0" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Standalone IGT Timer", NULL );
	CL_ImGuiBeginAutoBox( "hud_igt_card" );
	CL_ImGuiBoolCvarName( "Show IGT Timer", "ls_igttimer", "0" );
	CL_ImGuiComboCvarName( "IGT Align", "ls_igttimer_align", "2", alignLabels, alignValues, IM_ARRAYSIZE( alignValues ) );
	CL_ImGuiSliderCvarName( "IGT X Position", "ls_igttimer_x", "638", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "IGT Y Position", "ls_igttimer_y", "240", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "IGT Scale", "ls_igttimer_scale", "1.0", 0.3f, 4.0f, "%.2f" );
	CL_ImGuiBoolCvarName( "Show IGT Segment Timer", "ls_igtsegtimer", "0" );
	ImGui::EndChild();
}

#if 0
static void CL_ImGuiDrawStylePage( void ) {
	CL_ImGuiDrawLiveSplitElementStylesPage();
	CL_ImGuiDrawDisplayPage();
}
#endif

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
	CL_ImGuiBeginAutoBox( "speedo_card" );
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
	CL_ImGuiBeginAutoBox( "fps_card" );
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
	bool raceLocked = CL_ImGuiRaceSettingsLocked();
	CL_ImGuiSectionHeader( "Bunny Hop / Physics", NULL );
	CL_ImGuiBeginAutoBox( "movement_card" );
	if ( raceLocked ) {
		ImGui::TextDisabled( "Race host settings control movement until you leave the lobby." );
	}
	ImGui::BeginDisabled( raceLocked );
	CL_ImGuiBoolCvar( "HL1 Bhop Physics", bhMovement, "No air speed cap + bunny hop acceleration." );
	ImGui::BeginDisabled( !hlBhopEnabled );
	CL_ImGuiBoolCvarName( "Auto Jump (hold space)", "bh_autojump", "0", "Only intended for HL movement categories." );
	ImGui::EndDisabled();
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
	int weaponMode = Cvar_VariableIntegerValue( "cg_weapon_color_mode" );

	CL_ImGuiSectionHeader( "Field of View", NULL );
	CL_ImGuiBeginAutoBox( "view_fov_card" );
	CL_ImGuiSliderCvarName( "FOV Front-Back", "cg_fov", "90", 60.0f, 160.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "FOV Down-Up", "cg_fov_down", "90", 60.0f, 160.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "FOV Left-Right", "cg_fov_lr", "90", 0.0f, 160.0f, "%.0f" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Viewport Safe Area", NULL );
	CL_ImGuiBeginAutoBox( "view_safe_area_card" );
	CL_ImGuiBoolCvarName( "Black Sidebars", "cg_blackbars", "0" );
	CL_ImGuiIntInputCvarName( "Left Bar px", "cg_blackbarLeft", "0", 0, 4096 );
	CL_ImGuiIntInputCvarName( "Right Bar px", "cg_blackbarRight", "0", 0, 4096 );
	{
		static const float sidebarColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		CL_ImGuiColorCvarName( "Sidebar Color", "cg_blackbarColor", sidebarColor );
	}
	ImGui::TextDisabled( "Example: 1400 window, 1000 game -> 100 left / 300 right." );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Framerate / Weapon", NULL );
	CL_ImGuiBeginAutoBox( "view_misc_card" );
	CL_ImGuiComboCvarName( "Max FPS", "com_maxfps", "125", fpsLabels, fpsValues, IM_ARRAYSIZE( fpsValues ) );
	CL_ImGuiComboCvarName( "Weapon Hand", "cg_drawGun", "1", gunLabels, gunValues, IM_ARRAYSIZE( gunValues ) );
	ImGui::Separator();
	CL_ImGuiComboCvarName( "Weapon Render", "cg_weapon_color_mode", "0", weaponColorLabels, weaponColorValues, IM_ARRAYSIZE( weaponColorValues ) );
	if ( weaponMode != 0 ) {
		CL_ImGuiColorCvarName( "Tint Color", "cg_weapon_color", weaponColorFallback );
	}
	if ( weaponMode == 1 || weaponMode == 3 || weaponMode == 5 || weaponMode == 7 ) {
		CL_ImGuiSliderCvarName( "Tint Opacity", "cg_weapon_color_opacity", "0.35", 0.0f, 1.0f, "%.2f" );
	}
	if ( weaponMode == 5 || weaponMode == 6 ) {
		CL_ImGuiSliderCvarName( "X-Ray Strength", "cg_weapon_xray_strength", "0.65", 0.0f, 1.0f, "%.2f" );
	}
	if ( weaponMode == 2 || weaponMode == 4 || weaponMode == 6 || weaponMode == 7 ) {
		CL_ImGuiSliderCvarName( "Rainbow Speed", "cg_weapon_rainbow_speed", "1.0", 0.1f, 5.0f, "%.1f" );
	}
	ImGui::TextDisabled( weaponMode == 0 ? "Weapon recolor is off." : "Only controls used by the selected render mode are shown." );
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
	ImGui::TextWrapped( "Bind the commands you use during runs, routing and demo review. The current key is shown on the right." );
	ImGui::Spacing();
	CL_ImGuiDrawBindConflicts();
	ImGui::Spacing();
	for ( int i = 0; i < IM_ARRAYSIZE( s_bindEntries ); ++i ) {
		if ( i == 0 ) ImGui::TextDisabled( "LiveSplit controls" );
		if ( i == 8 ) { ImGui::Separator(); ImGui::TextDisabled( "Demo controls" ); }
		if ( i == 20 ) { ImGui::Separator(); ImGui::TextDisabled( "Practice controls" ); }
		CL_ImGuiBindingRow( s_bindEntries[i].label, s_bindEntries[i].command );
	}
}

static void CL_ImGuiDrawHelpPage( void ) {
	CL_ImGuiSectionHeader( "Speedrun Help", "Short operational reference." );
	ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "Quick reference" );
	ImGui::TextWrapped( "This page collects the commands that matter while routing, practicing, recording and reviewing speedruns." );
	ImGui::TextDisabled( "Use Controls to bind the most common actions." );
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
}

static void CL_ImGuiDemoControlButton( const char *label, const char *command ) {
	if ( ImGui::Button( label, ImVec2( -FLT_MIN, 0 ) ) ) {
		Cbuf_AddText( command );
		Cbuf_AddText( "\n" );
	}
	CL_ImGuiOptionTooltip( label, command, NULL );
}

static void CL_ImGuiDrawDemosPage( void ) {
	int i;
	int visibleCount;
	int pageCount;
	int pageStart;
	int pageEnd;
	int sortedIndices[SRGUI_MAX_DEMOS];
	float tableHeight;
	static const char *filterLabels[] = { "All", "Full Game", "Mission", "Individual Level", "Other" };
	static const int filterValues[] = { -1, SRGUI_DEMOCAT_FULLGAME, SRGUI_DEMOCAT_MISSION, SRGUI_DEMOCAT_IL, SRGUI_DEMOCAT_OTHER };

	CL_ImGuiSectionHeader( "Demo Browser", NULL );
	if ( s_demoCount == 0 ) {
		CL_ImGuiLoadDemos();
	}

	CL_ImGuiBeginAutoBox( "demo_toolbar" );
	if ( ImGui::Button( "Refresh", ImVec2( 96, 0 ) ) ) {
		CL_ImGuiLoadDemos();
	}
	CL_ImGuiSameLineIfFits( 112.0f );
	ImGui::BeginDisabled( s_demoSelected < 0 || s_demoSelected >= s_demoCount );
	if ( ImGui::Button( "Play", ImVec2( 86, 0 ) ) ) {
		CL_ImGuiPlaySelectedDemo();
	}
	ImGui::EndDisabled();
	CL_ImGuiSameLineIfFits( 128.0f );
	if ( ImGui::Button( "Folder", ImVec2( 94, 0 ) ) ) {
		Cbuf_AddText( "dir demos\n" );
	}
	ImGui::Separator();
	ImGui::SetNextItemWidth( 190.0f );
	if ( ImGui::BeginCombo( "Filter", filterLabels[s_demoFilter == -1 ? 0 : s_demoFilter + 1] ) ) {
		for ( i = 0; i < IM_ARRAYSIZE( filterLabels ); ++i ) {
			bool selected = s_demoFilter == filterValues[i];
			if ( ImGui::Selectable( filterLabels[i], selected ) ) {
				s_demoFilter = filterValues[i];
				s_demoPage = 0;
			}
			if ( selected ) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	CL_ImGuiSameLineIfFits( 252.0f );
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x > 260.0f ? 240.0f : -FLT_MIN );
	if ( ImGui::InputTextWithHint( "Search", "demo name", s_demoSearch, sizeof( s_demoSearch ) ) ) {
		s_demoPage = 0;
	}
	ImGui::EndChild();

	CL_ImGuiScanDemoMetadata( s_demoSelected );
	CL_ImGuiScanDemoMetadataBudget( ( s_demoSortColumn >= 3 && s_demoSortColumn <= 5 ) ? 8 : 1 );
	visibleCount = CL_ImGuiBuildSortedDemoIndices( sortedIndices, SRGUI_MAX_DEMOS );
	CL_ImGuiDrawDemoStats( visibleCount );
	pageCount = ( visibleCount + SRGUI_DEMOS_PER_PAGE - 1 ) / SRGUI_DEMOS_PER_PAGE;
	if ( pageCount < 1 ) pageCount = 1;
	if ( s_demoPage >= pageCount ) s_demoPage = pageCount - 1;
	if ( s_demoPage < 0 ) s_demoPage = 0;
	CL_ImGuiDrawDemoPagination( visibleCount, pageCount );
	pageStart = s_demoPage * SRGUI_DEMOS_PER_PAGE;
	pageEnd = pageStart + SRGUI_DEMOS_PER_PAGE;
	if ( pageEnd > visibleCount ) pageEnd = visibleCount;

	tableHeight = ImGui::GetContentRegionAvail().y * 0.46f;
	if ( tableHeight < 220.0f ) tableHeight = 220.0f;
	if ( tableHeight > 360.0f ) tableHeight = 360.0f;
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.16f, 0.27f, 0.12f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.03f, 0.05f, 0.035f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.07f, 0.11f, 0.06f, 0.68f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.38f, 0.16f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.14f, 0.24f, 0.11f, 0.72f ) );
	if ( ImGui::BeginTable( "demo_table", 6, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate, ImVec2( 0, tableHeight ) ) ) {
		ImGuiTableSortSpecs *sortSpecs;
		int row = 0;
		ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 42.0f );
		ImGui::TableSetupColumn( "Demo", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending, 1.0f );
		ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortAscending, 118.0f );
		ImGui::TableSetupColumn( "Size", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 86.0f );
		ImGui::TableSetupColumn( "Duration", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 86.0f );
		ImGui::TableSetupColumn( "Maps", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 56.0f );
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableHeadersRow();
		sortSpecs = ImGui::TableGetSortSpecs();
		if ( sortSpecs && sortSpecs->SpecsCount > 0 ) {
			const ImGuiTableColumnSortSpecs *spec = &sortSpecs->Specs[0];
			if ( s_demoSortColumn != spec->ColumnIndex || s_demoSortAscending != ( spec->SortDirection != ImGuiSortDirection_Descending ) ) {
				s_demoPage = 0;
			}
			s_demoSortColumn = spec->ColumnIndex;
			s_demoSortAscending = spec->SortDirection != ImGuiSortDirection_Descending;
			sortSpecs->SpecsDirty = false;
		} else if ( sortSpecs && sortSpecs->SpecsDirty ) {
			s_demoSortColumn = SRGUI_DEMO_SORT_NEWEST;
			s_demoSortAscending = true;
			s_demoPage = 0;
			sortSpecs->SpecsDirty = false;
		}
		for ( i = pageStart; i < pageEnd; ++i ) {
			char sizeBuf[32];
			char durationBuf[32];
			int demoIndex = sortedIndices[i];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			row++;
			ImGui::TextDisabled( "%04d", i + 1 );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::PushID( demoIndex );
			if ( ImGui::Selectable( s_demoList[demoIndex], s_demoSelected == demoIndex, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) ) {
				s_demoSelected = demoIndex;
				if ( ImGui::IsMouseDoubleClicked( 0 ) ) {
					CL_ImGuiPlaySelectedDemo();
				}
			}
			ImGui::PopID();
			ImGui::TableSetColumnIndex( 2 );
			ImGui::TextDisabled( "%s", CL_ImGuiDemoCategoryName( s_demoCategory[demoIndex] ) );
			ImGui::TableSetColumnIndex( 3 );
			if ( s_demoMetaState[demoIndex] == SRGUI_DEMOMETA_FAILED ) {
				ImGui::TextDisabled( "-" );
			} else {
				ImGui::TextDisabled( "%s", CL_ImGuiFormatDemoSize( s_demoSizeBytes[demoIndex], sizeBuf, sizeof( sizeBuf ) ) );
			}
			ImGui::TableSetColumnIndex( 4 );
			if ( s_demoMetaState[demoIndex] == SRGUI_DEMOMETA_FAILED ) {
				ImGui::TextDisabled( "-" );
			} else {
				ImGui::TextDisabled( "%s", CL_ImGuiFormatDemoDuration( s_demoDurationMs[demoIndex], durationBuf, sizeof( durationBuf ) ) );
			}
			ImGui::TableSetColumnIndex( 5 );
			if ( s_demoMetaState[demoIndex] == SRGUI_DEMOMETA_READY ) {
				ImGui::TextDisabled( "%d", s_demoMapCount[demoIndex] );
			} else if ( s_demoMetaState[demoIndex] == SRGUI_DEMOMETA_FAILED ) {
				ImGui::TextDisabled( "-" );
			} else {
				ImGui::TextDisabled( "..." );
			}
		}
		if ( row == 0 ) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextDisabled( "No demos" );
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor( 5 );

	CL_ImGuiBeginAutoBox( "demo_selected" );
	ImGui::TextDisabled( "Selected" );
	if ( s_demoSelected >= 0 && s_demoSelected < s_demoCount ) {
		char sizeBuf[32];
		char durationBuf[32];
		CL_ImGuiScanDemoMetadata( s_demoSelected );
		ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", s_demoList[s_demoSelected] );
		ImGui::TextDisabled( "%s", CL_ImGuiDemoCategoryName( s_demoCategory[s_demoSelected] ) );
		ImGui::TextDisabled( "Size: %s", s_demoMetaState[s_demoSelected] == SRGUI_DEMOMETA_FAILED ? "-" : CL_ImGuiFormatDemoSize( s_demoSizeBytes[s_demoSelected], sizeBuf, sizeof( sizeBuf ) ) );
		ImGui::TextDisabled( "Duration: %s", s_demoMetaState[s_demoSelected] == SRGUI_DEMOMETA_FAILED ? "-" : CL_ImGuiFormatDemoDuration( s_demoDurationMs[s_demoSelected], durationBuf, sizeof( durationBuf ) ) );
		if ( s_demoMetaState[s_demoSelected] == SRGUI_DEMOMETA_READY ) {
			ImGui::TextDisabled( "Maps: %d", s_demoMapCount[s_demoSelected] );
		}
	} else {
		ImGui::TextDisabled( "none" );
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "demo_controls" );
	if ( ImGui::BeginTable( "demo_controls_table", 4, ImGuiTableFlags_SizingStretchSame ) ) {
		const char *labels[] = { "Pause", "Speed +", "Speed -", "Rewind", "Forward", "Frame -", "Frame +", "Freecam", "HUD", "Binds", "Prev Map", "Next Map" };
		const char *commands[] = { "demo_pause", "demo_speedup", "demo_slowdown", "demo_skipbackward", "demo_skipforward", "demo_stepframeback", "demo_stepframe", "demo_freecam", "demo_togglehud", "demo_togglebinds", "demo_prevmap", "demo_nextmap" };
		for ( i = 0; i < IM_ARRAYSIZE( labels ); ++i ) {
			if ( i % 4 == 0 ) ImGui::TableNextRow();
			ImGui::TableNextColumn();
			CL_ImGuiDemoControlButton( labels[i], commands[i] );
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawGhostPage( void ) {
	CL_ImGuiSectionHeader( "Ghost Replay", "In-game ghost preview for saved best split recordings." );
	CL_ImGuiBeginAutoBox( "ghost_card" );
	CL_ImGuiBoolCvarName( "Show Ghost", "ls_ghost", "0" );
	CL_ImGuiIntSliderCvarName( "Opacity", "ls_ghost_opacity", "60", 5, 255 );
	ImGui::TextWrapped( "Ghost displays your best split as a translucent model and requires a saved ghost recording for the current map." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawLayoutToolsPage( void ) {
	CL_ImGuiSectionHeader( "Layout Grid", "Enable while the game is paused behind this panel to align HUD elements." );
	CL_ImGuiBeginAutoBox( "grid_card" );
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
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.16f, 0.27f, 0.12f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.03f, 0.05f, 0.035f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.07f, 0.11f, 0.06f, 0.68f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.38f, 0.16f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.14f, 0.24f, 0.11f, 0.72f ) );
	if ( ImGui::BeginTable( "split_names_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2( 0, 300 ) ) ) {
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
}

static void CL_ImGuiDrawToolsPage( void ) {
	CL_ImGuiDrawLayoutToolsPage();
	CL_ImGuiDrawSplitNamesPage();
}
typedef struct zoneGuiRow_s {
	int index;
	int route;
	int order;
	int bestMsec;
	int bestTotalMsec;
	int builtin;
	char type[32];
	char name[64];
} zoneGuiRow_t;
static bool CL_ImGuiParseZoneRow( const char *text, zoneGuiRow_t *row ) {
	if ( !text || !text[0] || !row ) {
		return false;
	}
	row->index = -1;
	row->route = 0;
	row->order = 0;
	row->bestMsec = 0;
	row->bestTotalMsec = 0;
	row->builtin = 0;
	row->type[0] = '\0';
	row->name[0] = '\0';
	return sscanf( text, "%d|%d|%d|%31[^|]|%63[^|]|%d|%d|%d", &row->index, &row->route, &row->order, row->type, row->name, &row->bestMsec, &row->bestTotalMsec, &row->builtin ) >= 7;
}
static void CL_ImGuiFormatZoneMsec( int msec, char *out, size_t outSize ) {
	int minutes;
	int seconds;
	int millis;
	if ( !out || outSize <= 0 ) {
		return;
	}
	if ( msec <= 0 ) {
		Q_strncpyz( out, "-", (int)outSize );
		return;
	}
	minutes = msec / 60000;
	seconds = ( msec / 1000 ) % 60;
	millis = msec % 1000;
	if ( !Cvar_VariableIntegerValue( "sp_timer_decimals" ) ) {
		if ( minutes > 0 ) {
			Com_sprintf( out, (int)outSize, "%d:%02d", minutes, seconds );
		} else {
			Com_sprintf( out, (int)outSize, "%d", seconds );
		}
		return;
	}
	if ( minutes > 0 ) {
		Com_sprintf( out, (int)outSize, "%d:%02d.%03d", minutes, seconds, millis );
	} else {
		Com_sprintf( out, (int)outSize, "%d.%03d", seconds, millis );
	}
}
static void CL_ImGuiSanitizeZoneToken( char *text ) {
	int i;

	if ( !text ) {
		return;
	}
	for ( i = 0; text[i]; i++ ) {
		char c = text[i];
		if ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) {
			continue;
		}
		text[i] = '_';
	}
}
static void CL_ImGuiDrawZoneTableRow( const zoneGuiRow_t *row, int selected, bool routeButton ) {
	char label[96];
	char cmd[96];
	char best[32];
	char bestTotal[32];
	ImVec4 typeColor;
	if ( !row ) {
		return;
	}
	typeColor = ImVec4( 0.58f, 0.92f, 0.34f, 1.0f );
	if ( !Q_stricmp( row->type, "checkpoint" ) ) {
		typeColor = ImVec4( 0.55f, 0.78f, 0.96f, 1.0f );
	} else if ( !Q_stricmp( row->type, "finish" ) ) {
		typeColor = ImVec4( 0.95f, 0.74f, 0.28f, 1.0f );
	} else if ( !Q_stricmp( row->type, "race" ) ) {
		typeColor = ImVec4( 1.0f, 0.84f, 0.25f, 1.0f );
	}
	CL_ImGuiFormatZoneMsec( row->bestMsec, best, sizeof( best ) );
	CL_ImGuiFormatZoneMsec( row->bestTotalMsec, bestTotal, sizeof( bestTotal ) );
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	Com_sprintf( label, sizeof( label ), "%d##zone_select_%d", row->index, row->index );
	if ( ImGui::Selectable( label, selected == row->index, ImGuiSelectableFlags_SpanAllColumns ) ) {
		Com_sprintf( cmd, sizeof( cmd ), "sp_zone_select %d\n", row->index );
		Cbuf_AddText( cmd );
	}
	ImGui::TableSetColumnIndex( 1 ); ImGui::Text( "%d", row->route );
	ImGui::TableSetColumnIndex( 2 ); ImGui::Text( "%d", row->order );
	ImGui::TableSetColumnIndex( 3 ); ImGui::TextColored( typeColor, "%s", row->type );
	ImGui::TableSetColumnIndex( 4 );
	if ( row->builtin ) {
		ImGui::Text( "%s *", row->name[0] ? row->name : "-" );
	} else {
		ImGui::TextUnformatted( row->name[0] ? row->name : "-" );
	}
	ImGui::TableSetColumnIndex( 5 ); ImGui::TextUnformatted( best );
	ImGui::TableSetColumnIndex( 6 ); ImGui::TextUnformatted( bestTotal );
	ImGui::TableSetColumnIndex( 7 );
	if ( routeButton && row->route > 0 && ImGui::SmallButton( va( "Route##zone_route_%d", row->index ) ) ) {
		Com_sprintf( cmd, sizeof( cmd ), "sp_zone_select %d\nsp_zone_route\n", row->index );
		Cbuf_AddText( cmd );
	}
}

static bool CL_ImGuiZoneRaceDebugEnabled( void ) {
	return Cvar_VariableIntegerValue( "sp_zone_race_debug" ) != 0;
}

static void CL_ImGuiPushZoneTableStyle( void ) {
	ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, ImVec2( 7.0f, 5.0f ) );
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.16f, 0.27f, 0.12f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.03f, 0.05f, 0.035f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.07f, 0.11f, 0.06f, 0.68f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.38f, 0.16f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.14f, 0.24f, 0.11f, 0.72f ) );
}

static void CL_ImGuiPopZoneTableStyle( void ) {
	ImGui::PopStyleColor( 5 );
	ImGui::PopStyleVar();
}

static void CL_ImGuiDrawZonesPage( void ) {
	static const float zoneStartColor[4] = { 82.0f / 255.0f, 1.0f, 112.0f / 255.0f, 1.0f };
	static const float zoneColor[4] = { 82.0f / 255.0f, 184.0f / 255.0f, 1.0f, 1.0f };
	static const float zoneFinishColor[4] = { 1.0f, 108.0f / 255.0f, 86.0f / 255.0f, 1.0f };
	static const float zoneRaceColor[4] = { 1.0f, 210.0f / 255.0f, 64.0f / 255.0f, 1.0f };
	char status[256];
	char selectedName[64];
	char selectedType[32];
	char selectedMins[96];
	char selectedMaxs[96];
	char selectedAngles[96];
	char filePath[128];
	char runTime[64];
	char recordTime[64];
	char lastDelta[32];
	char lastTotalDelta[32];
	char progressText[32];
	char activeStartName[64];
	char routeName[64];
	char selectedBest[32];
	char selectedBestTotal[32];
	char routeNameCmd[128];
	char rowCvar[32];
	char rowText[160];
	int zoneCount;
	int selected;
	int activeRoute;
	int activeStart;
	int selectedBuiltin;
	int selectedRoute;
	int selectedOrder;
	int viewRoute;
	int rowCount;
	int routeRowCount;
	bool raceDebug;
	bool lastDeltaPb;
	bool lastTotalDeltaPb;
	int i;
	zoneGuiRow_t row;
	static char routeNameEdit[64] = "";
	static int routeNameEditRoute = -1;
	static const float ztBg[4] = { 0.02f, 0.03f, 0.03f, 0.86f };
	static const float ztBg2[4] = { 0.06f, 0.10f, 0.07f, 0.78f };
	static const float ztBorder[4] = { 0.41f, 0.67f, 0.28f, 0.46f };
	static const float ztTime[4] = { 0.73f, 0.97f, 0.56f, 1.00f };
	static const float ztMuted[4] = { 0.57f, 0.64f, 0.53f, 0.92f };
	static const float ztAhead[4] = { 0.42f, 1.00f, 0.42f, 1.00f };
	static const float ztBehind[4] = { 1.00f, 0.36f, 0.28f, 1.00f };
	static const float ztGold[4] = { 1.00f, 0.86f, 0.18f, 1.00f };
	static const float ztNeutral[4] = { 0.92f, 0.74f, 0.24f, 1.00f };

	Cvar_VariableStringBuffer( "sp_zone_status_text", status, sizeof( status ) );
	Cvar_VariableStringBuffer( "sp_zone_selected_name", selectedName, sizeof( selectedName ) );
	Cvar_VariableStringBuffer( "sp_zone_selected_type", selectedType, sizeof( selectedType ) );
	Cvar_VariableStringBuffer( "sp_zone_selected_mins", selectedMins, sizeof( selectedMins ) );
	Cvar_VariableStringBuffer( "sp_zone_selected_maxs", selectedMaxs, sizeof( selectedMaxs ) );
	Cvar_VariableStringBuffer( "sp_zone_selected_angles", selectedAngles, sizeof( selectedAngles ) );
	Cvar_VariableStringBuffer( "sp_zone_file", filePath, sizeof( filePath ) );
	Cvar_VariableStringBuffer( "sp_zone_run_time", runTime, sizeof( runTime ) );
	Cvar_VariableStringBuffer( "sp_zone_record_time", recordTime, sizeof( recordTime ) );
	Cvar_VariableStringBuffer( "sp_zone_last_delta", lastDelta, sizeof( lastDelta ) );
	Cvar_VariableStringBuffer( "sp_zone_last_total_delta", lastTotalDelta, sizeof( lastTotalDelta ) );
	Cvar_VariableStringBuffer( "sp_zone_progress_text", progressText, sizeof( progressText ) );
	Cvar_VariableStringBuffer( "sp_zone_active_start_name", activeStartName, sizeof( activeStartName ) );
	Cvar_VariableStringBuffer( "sp_zone_route_name", routeName, sizeof( routeName ) );
	CL_ImGuiFormatZoneMsec( Cvar_VariableIntegerValue( "sp_zone_selected_best" ), selectedBest, sizeof( selectedBest ) );
	CL_ImGuiFormatZoneMsec( Cvar_VariableIntegerValue( "sp_zone_selected_best_total" ), selectedBestTotal, sizeof( selectedBestTotal ) );
	zoneCount = Cvar_VariableIntegerValue( "sp_zone_count" );
	selected = Cvar_VariableIntegerValue( "sp_zone_selected" );
	activeRoute = Cvar_VariableIntegerValue( "sp_zone_active_route" );
	activeStart = Cvar_VariableIntegerValue( "sp_zone_active_start" );
	selectedBuiltin = Cvar_VariableIntegerValue( "sp_zone_selected_builtin" );
	selectedRoute = Cvar_VariableIntegerValue( "sp_zone_selected_route" );
	selectedOrder = Cvar_VariableIntegerValue( "sp_zone_selected_order" );
	viewRoute = Cvar_VariableIntegerValue( "sp_zone_view_route" );
	rowCount = Cvar_VariableIntegerValue( "sp_zone_row_count" );
	routeRowCount = Cvar_VariableIntegerValue( "sp_zone_route_row_count" );
	raceDebug = CL_ImGuiZoneRaceDebugEnabled();
	lastDeltaPb = Cvar_VariableIntegerValue( "sp_zone_last_delta_pb" ) != 0;
	lastTotalDeltaPb = Cvar_VariableIntegerValue( "sp_zone_last_total_delta_pb" ) != 0;
	if ( routeNameEditRoute != ( viewRoute > 0 ? viewRoute : activeRoute ) ) {
		Q_strncpyz( routeNameEdit, routeName[0] ? routeName : "route", sizeof( routeNameEdit ) );
		routeNameEditRoute = viewRoute > 0 ? viewRoute : activeRoute;
	}

	CL_ImGuiSectionHeader( "Zones", "Route timer, checkpoint editor and zone display tuning." );
	CL_ImGuiBeginAutoBox( "zones_status" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Timer HUD" );
	CL_ImGuiBoolCvarName( "Zone Timer", "sp_zone_timer", "0" );
	CL_ImGuiSameLineIfFits( 136.0f );
	CL_ImGuiBoolCvarName( "Progress X/X", "sp_zone_hud_progress", "1" );
	ImGui::Separator();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Editor Draw" );
	{
		bool cheatsEnabled = Cvar_VariableIntegerValue( "sv_cheats" ) != 0;
		if ( !cheatsEnabled ) {
			if ( Cvar_VariableIntegerValue( "sp_zone_draw" ) ) Cvar_Set( "sp_zone_draw", "0" );
			if ( Cvar_VariableIntegerValue( "sp_zone_edit" ) ) Cvar_Set( "sp_zone_edit", "0" );
			ImGui::BeginDisabled();
		}
		CL_ImGuiBoolCvarName( "Draw Zones", "sp_zone_draw", "0" );
		CL_ImGuiSameLineIfFits( 132.0f );
		CL_ImGuiBoolCvarName( "Edit Mode", "sp_zone_edit", "0" );
		if ( !cheatsEnabled ) {
			ImGui::EndDisabled();
			ImGui::TextDisabled( "sv_cheats 1 required" );
		}
	}
	ImGui::Separator();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", runTime[0] ? runTime : "0.000" );
	CL_ImGuiSameLineIfFits( 100.0f );
	ImGui::TextDisabled( "PB %s", recordTime[0] ? recordTime : "-" );
	CL_ImGuiSameLineIfFits( 92.0f );
	if ( lastDelta[0] ) {
		ImGui::TextColored( lastDeltaPb ? ImVec4( 1.0f, 0.86f, 0.18f, 1.0f ) : ( lastDelta[0] == '-' ? ImVec4( 0.42f, 1.0f, 0.42f, 1.0f ) : ( lastDelta[0] == '+' ? ImVec4( 1.0f, 0.36f, 0.28f, 1.0f ) : ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ) ) ), "Seg %s", lastDelta );
	} else {
		ImGui::TextDisabled( "Seg -" );
	}
	CL_ImGuiSameLineIfFits( 92.0f );
	if ( lastTotalDelta[0] ) {
		ImGui::TextColored( lastTotalDeltaPb ? ImVec4( 1.0f, 0.86f, 0.18f, 1.0f ) : ( lastTotalDelta[0] == '-' ? ImVec4( 0.42f, 1.0f, 0.42f, 1.0f ) : ( lastTotalDelta[0] == '+' ? ImVec4( 1.0f, 0.36f, 0.28f, 1.0f ) : ImVec4( 0.66f, 0.86f, 1.0f, 1.0f ) ) ), "Full %s", lastTotalDelta );
	} else {
		ImGui::TextDisabled( "Full -" );
	}
	if ( Cvar_VariableIntegerValue( "sp_zone_hud_progress" ) && progressText[0] ) {
		CL_ImGuiSameLineIfFits( 72.0f );
		ImGui::TextDisabled( "CP %s", progressText );
	}
	if ( status[0] ) ImGui::TextWrapped( "%s", status );
	ImGui::TextDisabled( "%d zones   selected %d   route %d   %s", zoneCount, selected, viewRoute > 0 ? viewRoute : activeRoute, routeName[0] ? routeName : "-" );
	ImGui::TextDisabled( "start %d %s", activeStart, activeStartName[0] ? activeStartName : "" );
	ImGui::TextWrapped( "%s", filePath[0] ? filePath : "-" );
	ImGui::TextDisabled( "Route Name" );
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x > 270.0f ? ImGui::GetContentRegionAvail().x - 86.0f : ImGui::GetContentRegionAvail().x );
	ImGui::InputTextWithHint( "##route_name", "route", routeNameEdit, sizeof( routeNameEdit ) );
	CL_ImGuiSameLineIfFits( 82.0f );
	if ( ImGui::Button( "Apply##route_name", ImVec2( 74, 0 ) ) ) {
		CL_ImGuiSanitizeZoneToken( routeNameEdit );
		Com_sprintf( routeNameCmd, sizeof( routeNameCmd ), "sp_zone_route_name %s\n", routeNameEdit[0] ? routeNameEdit : "route" );
		Cbuf_AddText( routeNameCmd );
	}
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Create", NULL );
	CL_ImGuiBeginAutoBox( "zones_create" );
	if ( ImGui::Button( "Start Look", ImVec2( 118, 0 ) ) ) Cbuf_AddText( "sp_zone_add_start\n" );
	CL_ImGuiSameLineIfFits( 126.0f );
	if ( ImGui::Button( "Start Here", ImVec2( 118, 0 ) ) ) Cbuf_AddText( "sp_zone_add_start here\n" );
	if ( ImGui::Button( "CP Append Look", ImVec2( 138, 0 ) ) ) Cbuf_AddText( "sp_zone_add_checkpoint\n" );
	CL_ImGuiSameLineIfFits( 146.0f );
	if ( ImGui::Button( "CP Append Here", ImVec2( 138, 0 ) ) ) Cbuf_AddText( "sp_zone_add_checkpoint here\n" );
	if ( ImGui::Button( "CP Insert Look", ImVec2( 138, 0 ) ) ) Cbuf_AddText( "sp_zone_insert_checkpoint\n" );
	CL_ImGuiSameLineIfFits( 146.0f );
	if ( ImGui::Button( "CP Insert Here", ImVec2( 138, 0 ) ) ) Cbuf_AddText( "sp_zone_insert_checkpoint here\n" );
	if ( ImGui::Button( "Finish Look", ImVec2( 118, 0 ) ) ) Cbuf_AddText( "sp_zone_add_finish\n" );
	CL_ImGuiSameLineIfFits( 126.0f );
	if ( ImGui::Button( "Finish Here", ImVec2( 118, 0 ) ) ) Cbuf_AddText( "sp_zone_add_finish here\n" );
	if ( raceDebug ) {
		if ( ImGui::Button( "Race Look", ImVec2( 118, 0 ) ) ) Cbuf_AddText( "sp_zone_add_race\n" );
		CL_ImGuiSameLineIfFits( 126.0f );
		if ( ImGui::Button( "Race Here", ImVec2( 118, 0 ) ) ) Cbuf_AddText( "sp_zone_add_race here\n" );
	}
	if ( ImGui::Button( "Save", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_save\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( ImGui::Button( "Load", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_load\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( ImGui::Button( "Status", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_status\n" );
	CL_ImGuiSameLineIfFits( 116.0f );
	if ( ImGui::Button( "Reset Run", ImVec2( 108, 0 ) ) ) Cbuf_AddText( "sp_zone_reset\n" );
	CL_ImGuiSameLineIfFits( 132.0f );
	if ( raceDebug ) {
		if ( ImGui::Button( "Export C", ImVec2( 116, 0 ) ) ) Cbuf_AddText( "sp_zone_export_builtins\n" );
	}
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "All Zones", NULL );
	CL_ImGuiPushZoneTableStyle();
	if ( ImGui::BeginTable( "zones_table", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_PadOuterX, ImVec2( 0, 220 ) ) ) {
		ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed, 44.0f );
		ImGui::TableSetupColumn( "R", ImGuiTableColumnFlags_WidthFixed, 36.0f );
		ImGui::TableSetupColumn( "Ord", ImGuiTableColumnFlags_WidthFixed, 42.0f );
		ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f );
		ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "Seg", ImGuiTableColumnFlags_WidthFixed, 74.0f );
		ImGui::TableSetupColumn( "Full", ImGuiTableColumnFlags_WidthFixed, 74.0f );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 62.0f );
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableHeadersRow();
		for ( i = 0; i < rowCount && i < 64; i++ ) {
			Com_sprintf( rowCvar, sizeof( rowCvar ), "sp_zone_row_%02d", i );
			Cvar_VariableStringBuffer( rowCvar, rowText, sizeof( rowText ) );
			if ( CL_ImGuiParseZoneRow( rowText, &row ) && ( raceDebug || Q_stricmp( row.type, "race" ) ) ) {
				CL_ImGuiDrawZoneTableRow( &row, selected, true );
			}
		}
		ImGui::EndTable();
	}
	CL_ImGuiPopZoneTableStyle();

	CL_ImGuiSectionHeader( "Route", NULL );
	CL_ImGuiPushZoneTableStyle();
	if ( ImGui::BeginTable( "zones_route_table", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_PadOuterX, ImVec2( 0, 156 ) ) ) {
		ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed, 44.0f );
		ImGui::TableSetupColumn( "R", ImGuiTableColumnFlags_WidthFixed, 36.0f );
		ImGui::TableSetupColumn( "Ord", ImGuiTableColumnFlags_WidthFixed, 42.0f );
		ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f );
		ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "Seg", ImGuiTableColumnFlags_WidthFixed, 74.0f );
		ImGui::TableSetupColumn( "Full", ImGuiTableColumnFlags_WidthFixed, 74.0f );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 62.0f );
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableHeadersRow();
		for ( i = 0; i < routeRowCount && i < 64; i++ ) {
			Com_sprintf( rowCvar, sizeof( rowCvar ), "sp_zone_route_row_%02d", i );
			Cvar_VariableStringBuffer( rowCvar, rowText, sizeof( rowText ) );
			if ( CL_ImGuiParseZoneRow( rowText, &row ) ) {
				CL_ImGuiDrawZoneTableRow( &row, selected, false );
			}
		}
		ImGui::EndTable();
	}
	CL_ImGuiPopZoneTableStyle();

	CL_ImGuiSectionHeader( "Selected Zone", NULL );
	CL_ImGuiBeginAutoBox( "zones_selected" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", selectedName[0] ? selectedName : "No selection" );
	ImGui::TextDisabled( "Type: %s   Route: %d   Ord: %d%s", selectedType[0] ? selectedType : "-", selectedRoute, selectedOrder, selectedBuiltin ? "   built-in" : "" );
	ImGui::TextDisabled( "Best: seg %s   full %s", selectedBest, selectedBestTotal );
	ImGui::TextDisabled( "Mins: %s", selectedMins[0] ? selectedMins : "-" );
	ImGui::TextDisabled( "Maxs: %s", selectedMaxs[0] ? selectedMaxs : "-" );
	ImGui::TextDisabled( "Angles: %s", selectedAngles[0] ? selectedAngles : "-" );
	ImGui::Separator();
	if ( selected < 0 ) {
		ImGui::TextDisabled( "Select a row in one of the zone tables to edit or reset it." );
	} else {
	if ( ImGui::Button( "Use Route", ImVec2( 102, 0 ) ) ) Cbuf_AddText( "sp_zone_route\n" );
	CL_ImGuiSameLineIfFits( 90.0f );
	if ( ImGui::Button( "Prev", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_prev\n" );
	CL_ImGuiSameLineIfFits( 90.0f );
	if ( ImGui::Button( "Next", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_next\n" );
	CL_ImGuiSameLineIfFits( 90.0f );
	if ( ImGui::Button( "Ord Up", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_order_up\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( ImGui::Button( "Ord Down", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_order_down\n" );
	if ( ImGui::Button( "Delete", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_delete\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( ImGui::Button( "Clear All", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_clear\n" );
	if ( ImGui::Button( "Reset Time", ImVec2( 102, 0 ) ) ) Cbuf_AddText( "sp_zone_reset_times selected\n" );
	CL_ImGuiSameLineIfFits( 120.0f );
	if ( ImGui::Button( "Reset From", ImVec2( 112, 0 ) ) ) Cbuf_AddText( "sp_zone_reset_times from\n" );
	CL_ImGuiSameLineIfFits( 120.0f );
	if ( ImGui::Button( "Reset Route", ImVec2( 112, 0 ) ) ) Cbuf_AddText( "sp_zone_reset_times route\n" );
	if ( ImGui::Button( "Set Start", ImVec2( 102, 0 ) ) ) Cbuf_AddText( "sp_zone_type start\n" );
	CL_ImGuiSameLineIfFits( 140.0f );
	if ( ImGui::Button( "Set Checkpoint", ImVec2( 132, 0 ) ) ) Cbuf_AddText( "sp_zone_type checkpoint\n" );
	CL_ImGuiSameLineIfFits( 110.0f );
	if ( ImGui::Button( "Set Finish", ImVec2( 102, 0 ) ) ) Cbuf_AddText( "sp_zone_type finish\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( raceDebug ) {
		if ( ImGui::Button( "Set Race", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_type race\n" );
	}
	if ( ImGui::Button( "Grow", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_grow 8\n" );
	CL_ImGuiSameLineIfFits( 90.0f );
	if ( ImGui::Button( "Shrink", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_grow -8\n" );
	CL_ImGuiSameLineIfFits( 80.0f );
	if ( ImGui::Button( "Up", ImVec2( 72, 0 ) ) ) Cbuf_AddText( "sp_zone_move 0 0 8\n" );
	CL_ImGuiSameLineIfFits( 80.0f );
	if ( ImGui::Button( "Down", ImVec2( 72, 0 ) ) ) Cbuf_AddText( "sp_zone_move 0 0 -8\n" );
	if ( ImGui::Button( "Yaw -15", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate yaw -15\n" );
	CL_ImGuiSameLineIfFits( 90.0f );
	if ( ImGui::Button( "Yaw +15", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate yaw 15\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( ImGui::Button( "Pitch -15", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate pitch -15\n" );
	CL_ImGuiSameLineIfFits( 100.0f );
	if ( ImGui::Button( "Pitch +15", ImVec2( 92, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate pitch 15\n" );
	if ( ImGui::Button( "Roll -15", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate roll -15\n" );
	CL_ImGuiSameLineIfFits( 90.0f );
	if ( ImGui::Button( "Roll +15", ImVec2( 82, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate roll 15\n" );
	CL_ImGuiSameLineIfFits( 94.0f );
	if ( ImGui::Button( "Face View", ImVec2( 86, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate view\n" );
	CL_ImGuiSameLineIfFits( 98.0f );
	if ( ImGui::Button( "Reset Rot", ImVec2( 90, 0 ) ) ) Cbuf_AddText( "sp_zone_rotate reset\n" );
	}
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Editor Tuning", NULL );
	CL_ImGuiBeginAutoBox( "zones_tuning" );
	CL_ImGuiBoolCvarName( "Draw Start", "sp_zone_draw_start", "1" );
	CL_ImGuiSameLineIfFits( 112.0f );
	CL_ImGuiBoolCvarName( "Draw CP", "sp_zone_draw_checkpoints", "1" );
	CL_ImGuiSameLineIfFits( 124.0f );
	CL_ImGuiBoolCvarName( "Draw Finish", "sp_zone_draw_finish", "1" );
	CL_ImGuiSameLineIfFits( 118.0f );
	if ( raceDebug ) {
		CL_ImGuiBoolCvarName( "Draw Race", "sp_zone_draw_race", "1" );
	}
	CL_ImGuiBoolCvarName( "3D Text", "sp_zone_draw_labels", "1" );
	CL_ImGuiSameLineIfFits( 118.0f );
	CL_ImGuiBoolCvarName( "Handles", "sp_zone_draw_handles", "1" );
	CL_ImGuiSameLineIfFits( 132.0f );
	CL_ImGuiBoolCvarName( "Rotation Gizmo", "sp_zone_rotation_gizmo", "1" );
	CL_ImGuiBoolCvarName( "Active Route Only", "sp_zone_draw_active_route_only", "0" );
	CL_ImGuiSameLineIfFits( 144.0f );
	CL_ImGuiBoolCvarName( "Dim Other Routes", "sp_zone_dim_inactive", "1" );
	CL_ImGuiBoolCvarName( "Route Focus", "sp_zone_draw_run_target_only", "1" );
	CL_ImGuiSameLineIfFits( 118.0f );
	CL_ImGuiBoolCvarName( "Auto Names", "sp_zone_auto_names", "1" );
	ImGui::Separator();
	CL_ImGuiColorCvarName( "Start Color", "sp_zone_start_color", zoneStartColor );
	CL_ImGuiColorCvarName( "Checkpoint Color", "sp_zone_color", zoneColor );
	CL_ImGuiColorCvarName( "Finish Color", "sp_zone_finish_color", zoneFinishColor );
	if ( raceDebug ) {
		CL_ImGuiColorCvarName( "Race Color", "sp_zone_race_color", zoneRaceColor );
	}
	CL_ImGuiIntSliderCvarName( "Zone Opacity", "sp_zone_opacity", "75", 5, 255 );
	CL_ImGuiIntSliderCvarName( "Border Alpha", "sp_zone_border_alpha", "230", 20, 255 );
	CL_ImGuiSliderCvarName( "Border Width", "sp_zone_border_width", "0.75", 0.25f, 6.0f, "%.2f" );
	CL_ImGuiIntSliderCvarName( "Inactive Alpha", "sp_zone_inactive_alpha", "28", 0, 255 );
	CL_ImGuiSliderCvarName( "Handle Size", "sp_zone_handle_size", "6", 2.0f, 24.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Handle Distance", "sp_zone_handle_max_dist", "1200", 128.0f, 4096.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Hover Pixels", "sp_zone_hover_pixels", "18", 4.0f, 96.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Drag Speed", "sp_zone_drag_speed", "180", 8.0f, 1024.0f, "%.0f" );
	CL_ImGuiIntSliderCvarName( "Start Stop MS", "sp_zone_start_stop_ms", "220", 0, 1500 );
	ImGui::Separator();
	CL_ImGuiSliderCvarName( "Timer X", "sp_zone_hud_x", "8", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Timer Y", "sp_zone_hud_y", "84", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Timer Scale", "sp_zone_hud_scale", "1.0", 0.55f, 2.5f, "%.2f" );
	CL_ImGuiSliderCvarName( "Timer Alpha", "sp_zone_hud_alpha", "0.52", 0.0f, 1.0f, "%.2f" );
	if ( ImGui::TreeNodeEx( "Timer Colors##zone_timer_colors", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Background top", "sp_zone_timer_clr_bg2", ztBg2 );
		CL_ImGuiColorCvarName( "Background bottom", "sp_zone_timer_clr_bg", ztBg );
		CL_ImGuiColorCvarName( "Border", "sp_zone_timer_clr_border", ztBorder );
		CL_ImGuiColorCvarName( "Timer", "sp_zone_timer_clr_time", ztTime );
		CL_ImGuiColorCvarName( "PB / progress", "sp_zone_timer_clr_muted", ztMuted );
		CL_ImGuiColorCvarName( "Ahead", "sp_zone_timer_clr_ahead", ztAhead );
		CL_ImGuiColorCvarName( "Behind", "sp_zone_timer_clr_behind", ztBehind );
		CL_ImGuiColorCvarName( "Gold", "sp_zone_timer_clr_gold", ztGold );
		CL_ImGuiColorCvarName( "Neutral", "sp_zone_timer_clr_neutral", ztNeutral );
		ImGui::TreePop();
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawRecordsPage( void ) {
	char rowName[32];
	char rowText[256];
	char statsText[256];
	char titleText[256];
	char metricPB[32];
	char metricRgtPB[32];
	char metricSob[32];
	char metricRate[32];
	char chartText[256];
	char cmd[96];
	int i;
	int recordCount = 0;
	int pbChart[24] = { 0 };
	int pbChartCount = 0;
	int summaryAtt = 0;
	int summaryComp = 0;
	int goldCount = 0;
	int totalRows = 0;
	float attempts[40] = { 0 };
	float completions[40] = { 0 };
	static int selectedRecordRow = -1;
	static bool requestedInitialRefresh = false;
	static const char *modeLabels[] = { "Full Game", "Chapter", "Individual Level" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *diffLabels[] = { "Don't hurt me.", "Bring 'em on!", "I am Death incarnate!" };
	static const int diffValues[] = { 1, 2, 3 };
	static const char *missionLabels[] = { "1: Ominous Rumors", "2: Vengeance", "3: Deadly Designs", "4: Deathshead", "5: Resurrection" };
	static const int missionValues[] = { 1, 2, 3, 4, 5 };
	static const char *variantLabels[] = { "Any%", "100%", "HL1 Any%", "HL1 100%" };
	static const int variantValues[] = { 0, 1, 2, 3 };
	bool raceLocked = CL_ImGuiRaceSettingsLocked();

	CL_ImGuiSectionHeader( "Records / Splits Viewer", NULL );
	Cvar_VariableStringBuffer( "ls_sv_title", titleText, sizeof( titleText ) );
	Cvar_VariableStringBuffer( "ls_sv_stats", statsText, sizeof( statsText ) );
	Cvar_VariableStringBuffer( "ls_sv_pb", metricPB, sizeof( metricPB ) );
	Cvar_VariableStringBuffer( "ls_sv_rgt_pb", metricRgtPB, sizeof( metricRgtPB ) );
	Cvar_VariableStringBuffer( "ls_sv_sob", metricSob, sizeof( metricSob ) );
	Cvar_VariableStringBuffer( "ls_sv_completion_rate", metricRate, sizeof( metricRate ) );
	Cvar_VariableStringBuffer( "ls_sv_pb_chart", chartText, sizeof( chartText ) );
	pbChartCount = CL_ImGuiParseRecordPbChart( chartText, pbChart, IM_ARRAYSIZE( pbChart ) );
	goldCount = Cvar_VariableIntegerValue( "ls_sv_golds" );
	totalRows = Cvar_VariableIntegerValue( "ls_sv_total_rows" );
	if ( !requestedInitialRefresh || !titleText[0] ) {
		Cbuf_AddText( "livesplit_sv_refresh\n" );
		requestedInitialRefresh = true;
	}
	CL_ImGuiParseRecordSummary( statsText, &summaryAtt, &summaryComp );
	CL_ImGuiBeginAutoBox( "records_controls" );
	if ( raceLocked ) {
		ImGui::TextDisabled( "Race controls category and difficulty until you leave the lobby." );
	}
	ImGui::BeginDisabled( raceLocked );
	CL_ImGuiCommandComboCvarName( "View Mode", "ls_mode", "0", modeLabels, modeValues, IM_ARRAYSIZE( modeValues ), "livesplit_sv_mode" );
	CL_ImGuiCommandComboCvarName( "Difficulty", "ls_sv_diff", "3", diffLabels, diffValues, IM_ARRAYSIZE( diffValues ), "livesplit_sv_diff" );
	CL_ImGuiCommandComboCvarName( "Chapter", "ls_mission", "1", missionLabels, missionValues, IM_ARRAYSIZE( missionValues ), "livesplit_sv_mission" );
	ImGui::EndDisabled();
	CL_ImGuiCommandComboCvarName( "Category", "ls_sv_variant", "0", variantLabels, variantValues, IM_ARRAYSIZE( variantValues ), "livesplit_sv_variant" );
	if ( ImGui::Button( "Refresh", ImVec2( 108, 0 ) ) ) Cbuf_AddText( "livesplit_sv_refresh\n" );
	ImGui::EndChild();
	ImGui::Separator();
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", titleText[0] ? titleText : "Records" );
	ImGui::TextDisabled( "%s", statsText[0] ? statsText : "Refresh records to load saved splits." );
	if ( ImGui::BeginTable( "records_metrics", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextDisabled( "PB" ); ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", metricPB );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "RGT PB" ); ImGui::TextColored( ImVec4( 0.72f, 0.86f, 1.0f, 1.0f ), "%s", metricRgtPB );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "SOB" ); ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "%s", metricSob );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Attempts" ); ImGui::Text( "%d", summaryAtt );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Completions" ); ImGui::Text( "%d", summaryComp );
		ImGui::TableNextColumn(); ImGui::TextDisabled( "Golds" ); ImGui::Text( "%d/%d", goldCount, totalRows );
		ImGui::EndTable();
	}
	ImGui::TextDisabled( "Completion rate: %s    %s", metricRate, Cvar_VariableString( "ls_sv_pages" ) );
	ImGui::Separator();
	ImGui::PushStyleColor( ImGuiCol_TableHeaderBg, ImVec4( 0.16f, 0.27f, 0.12f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBg, ImVec4( 0.03f, 0.05f, 0.035f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_TableRowBgAlt, ImVec4( 0.07f, 0.11f, 0.06f, 0.68f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.22f, 0.38f, 0.16f, 0.90f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.14f, 0.24f, 0.11f, 0.72f ) );
	if ( ImGui::BeginTable( "records_table", 6, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) ) {
		ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed, 34.0f );
		ImGui::TableSetupColumn( "Map", ImGuiTableColumnFlags_WidthStretch, 1.45f );
		ImGui::TableSetupColumn( "Gold", ImGuiTableColumnFlags_WidthFixed, 78.0f );
		ImGui::TableSetupColumn( "PB Seg", ImGuiTableColumnFlags_WidthFixed, 78.0f );
		ImGui::TableSetupColumn( "Att", ImGuiTableColumnFlags_WidthFixed, 48.0f );
		ImGui::TableSetupColumn( "Comp", ImGuiTableColumnFlags_WidthFixed, 52.0f );
		ImGui::TableHeadersRow();
		for ( i = 0; i < 40; ++i ) {
		int att, comp;
		char map[64], gold[32], pb[32];
		Com_sprintf( rowName, sizeof( rowName ), "ls_sv_r%d", i );
		Cvar_VariableStringBuffer( rowName, rowText, sizeof( rowText ) );
		if ( rowText[0] ) {
			if ( CL_ImGuiParseRecordRow( rowText, map, sizeof( map ), gold, sizeof( gold ), pb, sizeof( pb ), &att, &comp ) ) {
				if ( recordCount < 40 ) {
					attempts[recordCount] = (float)att;
					completions[recordCount] = (float)comp;
				}
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 0 ); ImGui::TextDisabled( "%02d", recordCount + 1 );
				ImGui::TableSetColumnIndex( 1 );
				ImGui::PushID( i );
				if ( ImGui::Selectable( map, selectedRecordRow == i, ImGuiSelectableFlags_SpanAllColumns ) ) {
					selectedRecordRow = i;
				}
				ImGui::PopID();
				ImGui::TableSetColumnIndex( 2 ); ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "%s", gold );
				ImGui::TableSetColumnIndex( 3 ); ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "%s", pb );
				ImGui::TableSetColumnIndex( 4 ); ImGui::Text( "%d", att );
				ImGui::TableSetColumnIndex( 5 ); ImGui::Text( "%d", comp );
				recordCount++;
			} else {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex( 1 );
				ImGui::TextDisabled( "%s", rowText );
			}
		}
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor( 5 );
	if ( selectedRecordRow >= recordCount ) selectedRecordRow = -1;
	if ( recordCount == 0 ) {
		ImGui::TextDisabled( "No visible record rows. Click Refresh or change mode/difficulty." );
	}
	ImGui::Separator();
	CL_ImGuiDrawRecordStats( attempts, completions, recordCount, pbChart, pbChartCount );
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
}

static void CL_ImGuiDrawActionsPage( void ) {
	lsRaceUiSnapshot_t race;
	bool raceActive;
	LS_RaceBuildSnapshot( &race );
	raceActive = race.active != 0;
	CL_ImGuiSectionHeader( "Run Actions", "One-click controls for timer and category state." );
	CL_ImGuiActionCard( "Start Run", "Starts the current run/category and auto-records if enabled.", "Start", "livesplit_start", ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ) );
	CL_ImGuiActionCard( "Pause / Resume", "Toggles IGT pause for safe menu/setup moments.", "Pause / Resume", "livesplit_pause", ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ) );
	CL_ImGuiActionCard( "Check Settings", "Validates speedrun settings and warning conditions.", "Check", "livesplit_check", ImVec4( 0.50f, 0.78f, 0.28f, 1.0f ) );
	ImGui::Separator();
	ImGui::TextDisabled( "Split control" );
	CL_ImGuiCommandButton( "Undo Split", "livesplit_undo", "go back one split" );
	CL_ImGuiCommandButton( "Skip Split", "livesplit_skip", "skip current split" );
	ImGui::Separator();
	ImGui::TextDisabled( "Reset control" );
	if ( raceActive ) {
		ImGui::TextDisabled( "Locked while Race is active. Leave Race to reset." );
	}
	ImGui::BeginDisabled( raceActive );
	CL_ImGuiCommandButton( "Reset Run", "livesplit_reset", "normal reset with save/confirmation logic" );
	CL_ImGuiCommandButton( "Reset No Save", "livesplit_reset_nosave", "reset without saving current result" );
	CL_ImGuiCommandButton( "Reset Category", "livesplit_reset_category", "clear category state" );
	ImGui::EndDisabled();
}

#include "speedrun_imgui/sr_imgui_race.inl"

static void CL_ImGuiDrawDevPage( void ) {
	static const char *clipLabels[] = { "Off", "X-Ray", "X-Ray Alt", "Depth", "Depth Alt" };
	static const int clipValues[] = { 0, 1, 2, 3, 4 };
	static char triggerName[96] = "";
	bool cheats = Cvar_VariableIntegerValue( "sv_cheats" ) != 0;
	char triggerCommand[160];

	CL_ImGuiSectionHeader( "Developer Visualization", "Cheat/dev cvars are exposed here but not forced on." );
	CL_ImGuiActionCard( cheats ? "sv_cheats Enabled" : "Enable sv_cheats", cheats ? "Cheats are currently active; dev visualization tools will respond." : "Runs sv_cheats 1 twice because the patch has a confirmation guard.", cheats ? "Run Again" : "Enable Cheats", "sv_cheats 1\nsv_cheats 1", cheats ? ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ) : ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), cheats );
	if ( cheats ) {
		CL_ImGuiCommandButton( "Disable sv_cheats", "sv_cheats 0", "Turn cheat-gated visualization back off." );
	}
	ImGui::Separator();
	if ( !cheats ) {
		ImGui::TextDisabled( "Enable sv_cheats to edit the locked visualization controls below." );
	}
	ImGui::BeginDisabled( !cheats );
	CL_ImGuiBoolCvarName( "Draw Triggers", "cg_drawTriggers", "0" );
	CL_ImGuiIntSliderCvarName( "Trigger Opacity", "cg_triggerOpacity", "140", 5, 255 );
	CL_ImGuiIntSliderCvarName( "Trigger Log", "g_triggerLog", "0", 0, 2 );
	ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x > 370.0f ? 260.0f : ImGui::GetContentRegionAvail().x );
	ImGui::InputTextWithHint( "Trigger Name##dev_trigger_name", "targetname", triggerName, sizeof( triggerName ) );
	CL_ImGuiSameLineIfFits( 126.0f );
	ImGui::BeginDisabled( triggerName[0] == '\0' );
	if ( ImGui::Button( "Run Trigger", ImVec2( 118, 0 ) ) ) {
		Com_sprintf( triggerCommand, sizeof( triggerCommand ), "sp_trigger \"%s\"\n", triggerName );
		Cbuf_AddText( triggerCommand );
	}
	ImGui::EndDisabled();
	CL_ImGuiSameLineIfFits( 82.0f );
	if ( ImGui::Button( "List", ImVec2( 74, 0 ) ) ) Cbuf_AddText( "sp_trigger_list\n" );
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
}

static void CL_ImGuiDrawAboutPage( void ) {
	CL_ImGuiSectionHeader( "RtCW Speedrun Patch", "Timer, routing, demo review and practice tooling for Return to Castle Wolfenstein." );
	CL_ImGuiBeginAutoBox( "about_hero" );
	ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "Return to Castle Wolfenstein 1.45" );
	ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "Speedrun Patch" );
	ImGui::Spacing();
	ImGui::TextWrapped( "A focused in-game control center for LiveSplit setup, split records, demo playback, HUD layout, Race tools and practice/debug workflows." );
	ImGui::TextDisabled( "Based on Knightmare's RtCW Patch 1.42d." );
	ImGui::EndChild();
	ImGui::Spacing();
	bool wideAbout = ImGui::GetContentRegionAvail().x > 620.0f;
	if ( wideAbout ) {
		ImGui::Columns( 2, NULL, false );
	}
	CL_ImGuiBeginAutoBox( "about_features" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Included" );
	ImGui::BulletText( "LiveSplit/in-game timer" );
	ImGui::BulletText( "Demo browser and playback controls" );
	ImGui::BulletText( "Records viewer and statistics" );
	ImGui::BulletText( "HUD, Race, movement and dev tools" );
	ImGui::EndChild();
	if ( wideAbout ) {
		ImGui::NextColumn();
	} else {
		ImGui::Spacing();
	}
	CL_ImGuiBeginAutoBox( "about_author" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Author" );
	ImGui::TextColored( ImVec4( 0.95f, 0.74f, 0.28f, 1.0f ), "KoRrNiK" );
	ImGui::TextDisabled( "Discord: korrnik" );
	ImGui::Spacing();
	ImGui::TextWrapped( "Built for fast setup without leaving the game, with tournament and practice workflows close at hand." );
	ImGui::EndChild();
	if ( wideAbout ) {
		ImGui::Columns( 1 );
	}
	ImGui::Spacing();
	CL_ImGuiBeginAutoBox( "about_credits" );
	ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Credits" );
	ImGui::TextWrapped( "WolfETPlayer from RealRTCW for explaining and sharing several files." );
	ImGui::TextDisabled( "Additional thanks to the RtCW speedrun community for testing workflows, route ideas and feedback." );
	ImGui::EndChild();
	ImGui::Separator();
	ImGui::TextDisabled( "Version history" );
	ImGui::BulletText( "1.45a  |  Jun 21, 2021  |  First speedrun patch release" );
	ImGui::BulletText( "1.45b  |  Mar 16, 2026  |  Major update" );
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

typedef struct srGuiModernCategory_s {
	const char *icon;
	const char *nav;
	const char *title;
	const char *desc;
	const char *tag;
} srGuiModernCategory_t;

static const srGuiModernCategory_t s_imguiModernCategories[] = {
	{ "RUN", "Run Setup", "Run Setup", "Timer, category, difficulty, IL map and recording defaults.", "core" },
	{ "FND", "Finder", "Settings Finder", "Search every exposed option, cvar, command and page.", "quick" },
	{ "LS", "LiveSplit", "LiveSplit Look", "Overlay layout, text, colors and component styling.", "style" },
	{ "HUD", "HUD", "HUD Overlay", "Keystrokes, speedometer, FPS/timer positions and HUD toggles.", "visual" },
	{ "MOV", "Gameplay", "Gameplay Setup", "Movement, FOV, viewport, weapon and ghost settings.", "game" },
	{ "LAY", "Layout", "Layout Tools", "Alignment grid, HUD drag mode and split name editing.", "tools" },
	{ "KEY", "Keybinds", "Keybinds", "Assign run, demo and practice controls from one clean table.", "binds" },
	{ "REC", "Records", "Records", "Browse split records, attempts, completions and reset tools.", "data" },
	{ "DEM", "Demos", "Demo Review", "Find demos and control playback without leaving the panel.", "review" },
	{ "ACT", "Actions", "Run Actions", "Start, pause, reset and validate the current run state.", "run" },
	{ "ZON", "Zones", "Zone Timer", "Route zones, checkpoints, route timing and editor visuals.", "route" },
	{ "RCE", "Race", "Race Styling", "Race overlay, ghost and nametag styling. Control the lobby in the separate Race Control window.", "style" },
	{ "DEV", "Developer", "Developer Tools", "Cheat-gated visualization and renderer debug controls.", "dev" },
	{ "REF", "Guide", "Guide", "Short command reference for routing, demos and recovery.", "help" },
	{ "i", "About", "About", "Patch identity, author notes and feature overview.", "info" }
};

static void CL_ImGuiDrawModernNavItem( int index, const srGuiModernCategory_t *cat ) {
	ImGuiStyle &style = ImGui::GetStyle();
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImVec2 size = ImVec2( avail.x, 46.0f );
	bool active = s_imguiCategory == index;
	ImGui::PushID( index );
	if ( ImGui::InvisibleButton( "modern_nav", size ) ) {
		s_imguiCategory = index;
	}
	bool hovered = ImGui::IsItemHovered();
	ImU32 iconBg = active ? IM_COL32( 101, 198, 63, 235 ) : ( hovered ? IM_COL32( 64, 108, 48, 225 ) : IM_COL32( 39, 56, 34, 230 ) );
	ImU32 iconText = active ? IM_COL32( 10, 18, 10, 255 ) : IM_COL32( 196, 224, 181, 245 );
	float rounding = style.FrameRounding;
	if ( active ) {
		draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 31, 62, 28, 218 ), rounding );
		draw->AddRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 110, 218, 74, 210 ), rounding, 0, 1.2f );
		draw->AddRectFilled( ImVec2( pos.x + 4.0f, pos.y + 8.0f ), ImVec2( pos.x + 8.0f, pos.y + size.y - 8.0f ), IM_COL32( 126, 232, 84, 245 ), 2.0f );
	} else if ( hovered ) {
		draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 28, 48, 25, 168 ), rounding );
		draw->AddRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 86, 170, 58, 150 ), rounding, 0, 1.0f );
	}
	ImVec2 badge0 = ImVec2( pos.x + 14.0f, pos.y + 9.0f );
	ImVec2 badge1 = ImVec2( pos.x + 44.0f, pos.y + 37.0f );
	ImVec2 iconSize = ImGui::CalcTextSize( cat->icon );
	draw->AddRectFilled( badge0, badge1, iconBg, 5.0f );
	draw->AddRect( badge0, badge1, IM_COL32( 126, 232, 84, active ? 130 : 70 ), 5.0f, 0, 1.0f );
	draw->AddText( ImVec2( badge0.x + ( 30.0f - iconSize.x ) * 0.5f, badge0.y + ( 28.0f - iconSize.y ) * 0.5f ), iconText, cat->icon );
	draw->AddText( ImVec2( pos.x + 56.0f, pos.y + 8.0f ), active ? IM_COL32( 225, 248, 211, 255 ) : ( hovered ? IM_COL32( 202, 226, 190, 255 ) : IM_COL32( 166, 178, 158, 255 ) ), cat->nav );
	draw->AddText( ImVec2( pos.x + 56.0f, pos.y + 27.0f ), IM_COL32( 112, 128, 106, active ? 245 : 205 ), cat->tag );
	if ( hovered ) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos( ImGui::GetFontSize() * 28.0f );
		ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "%s", cat->title );
		ImGui::TextWrapped( "%s", cat->desc );
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	ImGui::PopID();
	ImGui::Dummy( ImVec2( 1.0f, 2.0f ) );
}

static void CL_ImGuiDrawSettings( void ) {
	int i;
	float openEase = ( s_imguiAnimations && s_imguiAnimations->integer != 0 ) ? CL_ImGuiEaseOutCubic( s_imguiAnim ) : 1.0f;
	float alpha = s_imguiAlpha ? Com_Clamp( 0.70f, 1.0f, s_imguiAlpha->value ) : 0.96f;
	ImGuiWindowFlags settingsFlags = ImGuiWindowFlags_NoCollapse | ( s_imguiPinned ? ImGuiWindowFlags_NoMove : 0 );
	ImGuiWindowFlags minimizedFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ( s_imguiPinned ? ImGuiWindowFlags_NoMove : 0 );

	if ( s_imguiCategory < 0 || s_imguiCategory >= IM_ARRAYSIZE( s_imguiModernCategories ) ) {
		s_imguiCategory = 0;
	}

	if ( s_imguiMinimized ) {
		ImGui::SetNextWindowPos( ImVec2( 32, 28 ), ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSize( ImVec2( 360, 44 ), ImGuiCond_Always );
		ImGui::SetNextWindowBgAlpha( alpha );
		if ( ImGui::Begin( "Speedrun Control Center##minimized", &s_imguiOpen, minimizedFlags ) ) {
			ImGui::SetCursorPosY( 10.0f );
			ImGui::TextColored( ImVec4( 0.58f, 0.92f, 0.34f, 1.0f ), "Speedrun Control Center" );
			ImGui::SameLine();
			ImGui::SetCursorPosX( ImGui::GetWindowWidth() - 178.0f );
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
		float baseW = s_imguiWindowW ? s_imguiWindowW->value : 840.0f;
		float baseH = s_imguiWindowH ? s_imguiWindowH->value : 570.0f;
		ImGui::SetNextWindowPos( ImVec2( baseX + inv * 18.0f, baseY + inv * 26.0f ), ImGuiCond_Always );
		ImGui::SetNextWindowSize( ImVec2( baseW - inv * 42.0f, baseH - inv * 34.0f ), ImGuiCond_Always );
	} else {
		ImGui::SetNextWindowPos( ImVec2( s_imguiWindowX ? s_imguiWindowX->value : 32.0f, s_imguiWindowY ? s_imguiWindowY->value : 28.0f ), ImGuiCond_Appearing );
		ImGui::SetNextWindowSize( ImVec2( s_imguiWindowW ? s_imguiWindowW->value : 840.0f, s_imguiWindowH ? s_imguiWindowH->value : 570.0f ), ImGuiCond_Appearing );
	}
	ImGui::SetNextWindowSizeConstraints( ImVec2( 720, 430 ), ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight ) );
	ImGui::SetNextWindowBgAlpha( alpha * ( 0.20f + 0.80f * openEase ) );
	if ( !ImGui::Begin( "Speedrun Control Center", &s_imguiOpen, settingsFlags ) ) {
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
	CL_ImGuiBeginAutoBox( "main_header" );
	ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "Speedrun Control Center" );
	ImGui::TextWrapped( "%s", s_imguiModernCategories[s_imguiCategory].desc );
	ImGui::Spacing();
	if ( ImGui::SmallButton( "Find" ) ) {
		s_imguiCategory = 1;
	}
	CL_ImGuiOptionTooltip( "Settings Finder", NULL, "Jump to the search page and filter all settings by label, cvar, tag or command." );
	CL_ImGuiSameLineIfFits( 58.0f );
	if ( ImGui::SmallButton( "Race" ) ) {
		s_imguiOpen = false;
		CL_SpeedrunImGui_OpenRace();
	}
	CL_ImGuiOptionTooltip( "Race Control", NULL, "Switches from the Speedrun settings window to the Race Control window." );
	CL_ImGuiSameLineIfFits( 86.0f );
	if ( ImGui::SmallButton( s_imguiMinimized ? "Restore" : "Minimize" ) ) {
		s_imguiMinimized = !s_imguiMinimized;
	}
	CL_ImGuiOptionTooltip( "Minimize", NULL, "Collapse the settings window into a compact title bar." );
	CL_ImGuiSameLineIfFits( 56.0f );
	if ( ImGui::SmallButton( s_imguiPinned ? "Unpin" : "Pin" ) ) {
		s_imguiPinned = !s_imguiPinned;
	}
	CL_ImGuiOptionTooltip( s_imguiPinned ? "Unpin window" : "Pin window", NULL, s_imguiPinned ? "Allow the settings window to move again." : "Locks the settings window position while editing." );
	CL_ImGuiSameLineIfFits( 60.0f );
	if ( ImGui::SmallButton( "Style" ) ) {
		ImGui::OpenPopup( "gui_settings_popup" );
	}
	CL_ImGuiOptionTooltip( "Panel Style", NULL, "Opens window opacity, accent and animation controls." );
	ImGui::SetNextWindowSize( ImVec2( 390.0f, 0.0f ), ImGuiCond_Appearing );
	ImGui::SetNextWindowSizeConstraints( ImVec2( 320.0f, 0.0f ), ImVec2( 430.0f, (float)cls.glconfig.vidHeight * 0.85f ) );
	if ( ImGui::BeginPopup( "gui_settings_popup" ) ) {
		static const float accentFallback[4] = { 0.36f, 0.82f, 0.21f, 1.00f };
		static const float accentAltFallback[4] = { 0.96f, 0.74f, 0.24f, 1.00f };
		ImGui::TextDisabled( "Panel style" );
		CL_ImGuiSliderCvarName( "Window Opacity", "ui_speedrun_imgui_alpha", "0.96", 0.70f, 1.0f, "%.2f" );
		CL_ImGuiSliderCvarName( "Card Opacity", "ui_speedrun_imgui_card_alpha", "0.92", 0.35f, 1.0f, "%.2f" );
		CL_ImGuiBoolCvarName( "Rounded Corners", "ui_speedrun_imgui_rounding", "1" );
		CL_ImGuiBoolCvarName( "Animations", "ui_speedrun_imgui_animations", "1" );
		ImGui::Separator();
		CL_ImGuiColorCvarName( "Accent", "ui_speedrun_imgui_accent", accentFallback );
		CL_ImGuiColorCvarName( "Accent Gold", "ui_speedrun_imgui_accent_alt", accentAltFallback );
		ImGui::Separator();
		if ( ImGui::Button( "Reset Window", ImVec2( 120, 0 ) ) ) {
			ImGui::SetWindowPos( "Speedrun Control Center", ImVec2( 32, 28 ), ImGuiCond_Always );
			ImGui::SetWindowSize( "Speedrun Control Center", ImVec2( 840, 570 ), ImGuiCond_Always );
			if ( s_imguiWindowX ) Cvar_Set( s_imguiWindowX->name, "32" );
			if ( s_imguiWindowY ) Cvar_Set( s_imguiWindowY->name, "28" );
			if ( s_imguiWindowW ) Cvar_Set( s_imguiWindowW->name, "840" );
			if ( s_imguiWindowH ) Cvar_Set( s_imguiWindowH->name, "570" );
		}
		CL_ImGuiOptionTooltip( "Reset Window", NULL, "Restores the refreshed default window size and position." );
		ImGui::SameLine();
		if ( ImGui::Button( "Reset Style", ImVec2( 120, 0 ) ) ) {
			Cvar_Set( "ui_speedrun_imgui_alpha", "0.96" );
			Cvar_Set( "ui_speedrun_imgui_card_alpha", "0.92" );
			Cvar_Set( "ui_speedrun_imgui_rounding", "1" );
			Cvar_Set( "ui_speedrun_imgui_animations", "1" );
			Cvar_Set( "ui_speedrun_imgui_accent", "92 210 54 1.00" );
			Cvar_Set( "ui_speedrun_imgui_accent_alt", "244 188 62 1.00" );
		}
		CL_ImGuiOptionTooltip( "Reset Style", NULL, "Restores opacity, rounding, animations and accent colors." );
		ImGui::EndPopup();
	}
	ImGui::EndChild();
	ImGui::Spacing();
	CL_ImGuiDrawUpdateNotice();
	if ( ImGui::BeginTable( "status_strip", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
		lsRaceUiSnapshot_t race;
		LS_RaceBuildSnapshot( &race );
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex( 0 );
		CL_ImGuiMiniStat( "Timer", s_cg_livesplit && s_cg_livesplit->integer ? "enabled" : "disabled" );
		ImGui::TableSetColumnIndex( 1 );
		CL_ImGuiMiniStat( "Mode", s_ls_mode && s_ls_mode->integer == 2 ? "Individual Level" : ( s_ls_mode && s_ls_mode->integer == 1 ? "Chapter" : "Full Game" ) );
		ImGui::TableSetColumnIndex( 2 );
		CL_ImGuiMiniStat( "IL Map", CL_ImGuiMapLabel( Cvar_VariableString( "ls_map" ) ) );
		ImGui::TableSetColumnIndex( 3 );
		CL_ImGuiMiniStat( "Race", race.active ? "active" : "idle" );
		ImGui::EndTable();
	}
	ImGui::Spacing();
	ImGui::Separator();

	ImGui::PushStyleColor( ImGuiCol_TableBorderStrong, ImVec4( 0.28f, 0.58f, 0.18f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_TableBorderLight, ImVec4( 0.18f, 0.40f, 0.14f, 0.82f ) );
	if ( ImGui::BeginTable( "modern_settings_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings, ImVec2( 0, 0 ) ) ) {
		ImGui::TableSetupColumn( "Navigation", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 232.0f );
		ImGui::TableSetupColumn( "Content", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex( 0 );
		ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8.0f, 7.0f ) );
		ImGui::BeginChild( "modern_nav_panel", ImVec2( 0, 0 ), ImGuiChildFlags_AlwaysUseWindowPadding );
		ImGui::TextDisabled( "CATEGORIES" );
		ImGui::Separator();
		for ( i = 0; i < IM_ARRAYSIZE( s_imguiModernCategories ); ++i ) {
			CL_ImGuiDrawModernNavItem( i, &s_imguiModernCategories[i] );
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();

		ImGui::TableSetColumnIndex( 1 );
		ImGui::BeginChild( "modern_content_panel", ImVec2( 0, 0 ), false );
		CL_ImGuiBeginAutoBox( "modern_content_header" );
		ImGui::TextColored( ImVec4( 0.70f, 0.95f, 0.45f, 1.0f ), "%s  %s", s_imguiModernCategories[s_imguiCategory].icon, s_imguiModernCategories[s_imguiCategory].title );
		ImGui::TextWrapped( "%s", s_imguiModernCategories[s_imguiCategory].desc );
		ImGui::EndChild();
		ImGui::Spacing();
		ImGui::BeginChild( "modern_content_body", ImVec2( 0, 0 ), true );
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
		case 10: CL_ImGuiDrawZonesPage(); break;
		case 11: CL_ImGuiDrawRacePage(); break;
		case 12: CL_ImGuiDrawDevPage(); break;
		case 13: CL_ImGuiDrawHelpPage(); break;
		default: CL_ImGuiDrawAboutPage(); break;
		}
		ImGui::EndChild();
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::PopStyleColor( 2 );
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

static void CL_SpeedrunImGui_ToggleRace_f( void ) {
	if ( s_raceGuiOpen ) {
		CL_SpeedrunImGui_CloseRaceGui();
	} else {
		CL_SpeedrunImGui_OpenRace();
	}
}

static void CL_SpeedrunImGui_OpenRace_f( void ) {
	CL_SpeedrunImGui_OpenRace();
}

static void CL_SpeedrunImGui_CloseRace_f( void ) {
	CL_SpeedrunImGui_CloseRaceGui();
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
		"ls_imgui_rounding", "ls_imgui_padding", "ls_imgui_component_gap", "ls_imgui_show_title", "ls_imgui_show_border", "ls_imgui_header_bg", "ls_imgui_show_status", "ls_imgui_gradient", "ls_imgui_gradient_angle", "ls_imgui_current_bg", "ls_imgui_border_size", "ls_imgui_font",
		"ls_imgui_show_prevseg", "ls_imgui_show_ghostseg", "ls_imgui_show_bestsegments", "ls_imgui_prev_gold_rainbow", "ls_imgui_row_size", "ls_imgui_name_size", "ls_imgui_bestdelta_size", "ls_imgui_delta_size", "ls_imgui_time_size", "ls_imgui_gold_rainbow",
		"ls_imgui_col_name", "ls_imgui_col_best", "ls_imgui_col_delta", "ls_imgui_timer_size", "ls_imgui_stage_size", "ls_imgui_info_size", "ls_imgui_rgt_size", "ls_imgui_timer_gap", "ls_imgui_info_gap", "ls_imgui_timer_split",
		"ls_imgui_show_sob", "ls_imgui_show_possible_save", "ls_imgui_show_best_possible",
		"ls_imgui_bold_title", "ls_imgui_bold_attempts", "ls_imgui_bold_status", "ls_imgui_bold_header", "ls_imgui_bold_timer", "ls_imgui_bold_stage", "ls_imgui_bold_info", "ls_imgui_bold_splits", "ls_imgui_bold_split_name", "ls_imgui_bold_split_best", "ls_imgui_bold_split_delta", "ls_imgui_bold_split_time", "ls_imgui_bold_prev", "ls_imgui_bold_prev_label", "ls_imgui_bold_prev_value", "ls_imgui_bold_ghost", "ls_imgui_bold_stats", "ls_imgui_bold_stats_values", "ls_imgui_bold_stat_sob_label", "ls_imgui_bold_stat_sob_value", "ls_imgui_bold_stat_possible_label", "ls_imgui_bold_stat_possible_value", "ls_imgui_bold_stat_best_label", "ls_imgui_bold_stat_best_value", "ls_imgui_bold_rgt",
		"ls_imgui_text_gradient", "ls_imgui_text_gradient_angle",
		"ls_clr_bg", "ls_clr_bg2", "ls_clr_border", "ls_clr_sep", "ls_clr_highlight", "ls_clr_text", "ls_clr_text_gradient2", "ls_clr_timer", "ls_clr_title", "ls_clr_category", "ls_clr_header_bg", "ls_clr_column_label",
		"ls_clr_current", "ls_clr_completed", "ls_clr_future", "ls_clr_split_time", "ls_clr_split_time_current", "ls_clr_split_time_completed", "ls_clr_ahead", "ls_clr_behind", "ls_clr_gold", "ls_clr_stage_timer",
		"ls_clr_status_live", "ls_clr_status_ready", "ls_clr_status_pause", "ls_clr_status_done", "ls_clr_status_text",
		"ls_clr_pb_label", "ls_clr_pb_value", "ls_clr_best_label", "ls_clr_best_value",
		"ls_clr_prev_label", "ls_clr_prev_ahead", "ls_clr_prev_behind", "ls_clr_prev_gold",
		"ls_clr_ghost_label", "ls_clr_ghost_time", "ls_clr_stat_label", "ls_clr_stat_sob_label", "ls_clr_stat_sob", "ls_clr_stat_possible_label", "ls_clr_stat_possible_save", "ls_clr_stat_possible_zero", "ls_clr_stat_possible_missing", "ls_clr_stat_best_possible_label", "ls_clr_stat_best_possible", "ls_clr_rgt", "ls_clr_empty",
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
	s_raceGuiWindowX = Cvar_Get( "ui_race_imgui_x", "42", CVAR_ARCHIVE );
	s_raceGuiWindowY = Cvar_Get( "ui_race_imgui_y", "34", CVAR_ARCHIVE );
	s_raceGuiWindowW = Cvar_Get( "ui_race_imgui_w", "900", CVAR_ARCHIVE );
	s_raceGuiWindowH = Cvar_Get( "ui_race_imgui_h", "640", CVAR_ARCHIVE );
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
	CL_ImGuiForgetArchivedCvar( "ls_imgui_detached" );
	CL_ImGuiForgetArchivedCvar( "ls_imgui_detached_x" );
	CL_ImGuiForgetArchivedCvar( "ls_imgui_detached_y" );
	CL_ImGuiForgetArchivedCvar( "ls_imgui_detached_w" );
	CL_ImGuiForgetArchivedCvar( "ls_imgui_detached_h" );
	CL_ImGuiForgetArchivedCvar( "ls_imgui_detached_ontop" );
	Cvar_Set( "ls_imgui", "1" );
	Cvar_Get( "ls_imgui_rounding", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_padding", "5", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui_component_gap", "4", CVAR_ARCHIVE );
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
	Cvar_Get( "ls_imgui_prev_gold_rainbow", "0", CVAR_ARCHIVE );
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
	Cvar_Get( "ls_imgui_bold_stats_values", "0", CVAR_ARCHIVE );
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
	Cvar_Get( "ls_clr_split_time", "218 226 214 0.92", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_split_time_current", "224 246 214 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_split_time_completed", "150 160 146 0.72", CVAR_ARCHIVE );
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
	Cvar_Get( "ls_clr_stat_sob_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_sob", "255 220 50 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_save", "72 220 80 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_zero", "112 118 112 0.62", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_possible_missing", "82 92 76 0.70", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_best_possible_label", "128 142 118 0.68", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_stat_best_possible", "255 220 50 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_rgt", "218 226 214 0.92", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_empty", "112 118 112 0.48", CVAR_ARCHIVE );
	Cvar_Get( "sp_timer_decimals", "1", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer", "0", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_hud_progress", "1", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_edit", "0", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_draw", "0", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_draw_run_target_only", "1", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_opacity", "75", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_border_width", "0.75", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_start_color", "82 255 112 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_color", "82 184 255 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_finish_color", "255 108 86 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_handle_size", "6", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_handle_max_dist", "1200", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_hover_pixels", "18", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_drag_speed", "180", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_hud_x", "8", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_hud_y", "84", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_hud_scale", "1.0", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_hud_alpha", "0.52", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_bg", "5 8 7 0.86", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_bg2", "14 24 16 0.78", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_border", "105 170 70 0.46", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_time", "186 248 142 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_muted", "145 164 136 0.92", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_ahead", "108 255 108 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_behind", "255 92 72 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_gold", "255 220 46 1.00", CVAR_ARCHIVE );
	Cvar_Get( "sp_zone_timer_clr_neutral", "235 190 62 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_clr_text_gradient2", "255 255 255 0.59", CVAR_ARCHIVE );
	/* Runtime-open state must never survive a previous session.  If this cvar
	   starts as 1 from an old config, legacy menu/cgame cursor drawing is hidden
	   while ImGui is actually closed, making the normal game cursor disappear. */
	s_imguiOpen = false;
	s_raceGuiOpen = false;
	s_lastEnabledCvar = 0;
	if ( s_imguiEnabled ) {
		Cvar_Set( s_imguiEnabled->name, "0" );
	}
	Cmd_AddCommand( "speedrun_gui", CL_SpeedrunImGui_Toggle_f );
	Cmd_AddCommand( "speedrun_imgui", CL_SpeedrunImGui_Toggle_f );
	Cmd_AddCommand( "speedrun_gui_open", CL_SpeedrunImGui_Open_f );
	Cmd_AddCommand( "speedrun_gui_close", CL_SpeedrunImGui_Close_f );
	Cmd_AddCommand( "race_gui", CL_SpeedrunImGui_ToggleRace_f );
	Cmd_AddCommand( "race_gui_open", CL_SpeedrunImGui_OpenRace_f );
	Cmd_AddCommand( "race_gui_close", CL_SpeedrunImGui_CloseRace_f );
	Cmd_AddCommand( "ls_race_open", CL_SpeedrunImGui_OpenRace_f );
	Cmd_AddCommand( "ls_race_close", CL_SpeedrunImGui_CloseRace_f );
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
	return CL_SpeedrunImGui_HasPanelOpen() ? 1 : 0;
}

extern "C" void CL_SpeedrunImGui_Draw( void ) {
	int now;
	float dt;
	int enabledNow;
	bool liveSplitVisible;
	bool zoneTimerVisible;
	bool raceVisible;
	bool raceCountdownVisible;
	bool raceChatVisible;
	bool keystrokesVisible;
	bool panelOpen;
	bool raceGuiWasOpen;

	enabledNow = s_imguiEnabled ? s_imguiEnabled->integer : 0;
	if ( enabledNow && !s_lastEnabledCvar ) {
		CL_SpeedrunImGui_Open();
	} else if ( !enabledNow && s_lastEnabledCvar && s_imguiOpen ) {
		CL_SpeedrunImGui_Close();
	}
	s_lastEnabledCvar = enabledNow;
	zoneTimerVisible = CL_ImGuiShouldDrawZoneTimerOverlay();
	raceVisible = CL_ImGuiShouldDrawRaceOverlay();
	raceCountdownVisible = CL_ImGuiShouldDrawRaceCountdown();
	raceChatVisible = CL_ImGuiShouldDrawRaceChat();
	keystrokesVisible = CL_ImGuiShouldDrawKeystrokesOverlay();
	liveSplitVisible = CL_ImGuiShouldDrawLiveSplitOverlay();
	if ( !CL_SpeedrunImGui_HasPanelOpen() && !liveSplitVisible && !zoneTimerVisible && !raceVisible && !raceCountdownVisible && !raceChatVisible && !keystrokesVisible ) {
		return;
	}
	if ( CL_SpeedrunImGui_HasPanelOpen() || s_raceChatOpen ) {
		CL_SpeedrunImGui_ClearGameplayInput();
		if ( CL_SpeedrunImGui_HasPanelOpen() ) Key_SetCatcher( ( Key_GetCatcher() & ~KEYCATCH_CONSOLE ) | KEYCATCH_UI );
		if ( cls.state == CA_ACTIVE ) {
			if ( CL_SpeedrunImGui_HasPanelOpen() && ( !cl_paused || !cl_paused->integer ) ) {
				Cvar_Set( "cl_paused", "1" );
			}
			if ( CL_SpeedrunImGui_HasPanelOpen() && s_imguiRestorePause && !s_imguiRestorePause->integer ) {
				Cvar_Set( s_imguiRestorePause->name, "1" );
			}
		}
	}

	CL_ImGuiLazyInit();
	CL_ImGuiEnsureDeviceObjects();
	CL_ImGuiApplyRuntimeStyle();

	ImGuiIO &io = ImGui::GetIO();
	now = Sys_Milliseconds();
	dt = s_lastFrameMs > 0 ? ( now - s_lastFrameMs ) / 1000.0f : 1.0f / 60.0f;
	if ( dt <= 0.0f ) dt = 1.0f / 60.0f;
	s_lastFrameMs = now;
	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 ) {
		s_imguiAnim = Com_Clamp( 0.0f, 1.0f, s_imguiAnim + dt * 3.8f );
		s_raceGuiAnim = Com_Clamp( 0.0f, 1.0f, s_raceGuiAnim + dt * 3.8f );
	} else {
		s_imguiAnim = 1.0f;
		s_raceGuiAnim = 1.0f;
	}
	io.DisplaySize = ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight );
	io.DeltaTime = dt;
	panelOpen = CL_SpeedrunImGui_HasPanelOpen();
	CL_ImGuiUpdateMousePosition( io );
	io.MouseDown[0] = panelOpen ? s_mouseDown[0] : false;
	io.MouseDown[1] = panelOpen ? s_mouseDown[1] : false;
	io.MouseDown[2] = panelOpen ? s_mouseDown[2] : false;
	io.MouseWheel = panelOpen ? s_mouseWheel : 0.0f;
	io.MouseDrawCursor = panelOpen || s_raceChatOpen;

	ImGui_ImplOpenGL2_NewFrame();
	ImGui::NewFrame();
	if ( liveSplitVisible ) {
		CL_ImGuiDrawLiveSplitOverlay();
	}
	if ( zoneTimerVisible ) {
		CL_ImGuiDrawZoneTimerOverlay();
	}
	if ( raceVisible ) {
		CL_ImGuiDrawRaceOverlay();
	}
	if ( raceCountdownVisible ) {
		CL_ImGuiDrawRaceCenterCountdown();
	}
	if ( raceChatVisible ) {
		CL_ImGuiDrawRaceChatOverlay();
	}
	CL_ImGuiDrawKeystrokesOverlay();
	if ( s_imguiOpen ) {
		CL_ImGuiDrawLayoutGrid();
		CL_ImGuiDrawLayoutEditor();
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.08f + 0.92f * CL_ImGuiEaseOutCubic( s_imguiAnim ) );
		CL_ImGuiDrawSettings();
		ImGui::PopStyleVar();
	}
	raceGuiWasOpen = s_raceGuiOpen;
	if ( s_raceGuiOpen ) {
		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, 0.08f + 0.92f * CL_ImGuiEaseOutCubic( s_raceGuiAnim ) );
		CL_ImGuiDrawRaceControlWindow();
		ImGui::PopStyleVar();
	}
	if ( enabledNow && !s_imguiOpen ) {
		/* Window X / close path: keep cvar and state in sync so it does not
		   immediately reopen on the next frame. Do not render this just-closed
		   frame, otherwise the old window can remain visible until the next clear. */
		CL_SpeedrunImGui_Close();
		ImGui::EndFrame();
	} else if ( raceGuiWasOpen && !s_raceGuiOpen && !s_imguiOpen ) {
		CL_SpeedrunImGui_CloseRaceGui();
		ImGui::EndFrame();
	} else {
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
	s_mouseWheel = 0.0f;
}

extern "C" int CL_SpeedrunImGui_WndProc( void *hWnd, unsigned int uMsg, unsigned int wParam, long lParam ) {
	bool panelOpen;
	s_imguiHwnd = (HWND)hWnd;
	panelOpen = CL_SpeedrunImGui_HasPanelOpen();

	if ( !panelOpen && !s_raceChatOpen ) {
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
			if ( s_raceChatOpen && !panelOpen ) return 1;
			if ( s_pendingBindCommand ) { CL_ImGuiAssignPendingBind( K_MOUSE2 ); return 1; }
			s_mouseDown[1] = true; io.AddMouseButtonEvent( 1, true ); return 1;
		case WM_MBUTTONDOWN:
			if ( s_raceChatOpen && !panelOpen ) return 1;
			if ( s_pendingBindCommand ) { CL_ImGuiAssignPendingBind( K_MOUSE3 ); return 1; }
			s_mouseDown[2] = true; io.AddMouseButtonEvent( 2, true ); return 1;
		case WM_LBUTTONDOWN:
			if ( s_raceChatOpen && !panelOpen ) return 1;
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
			if ( s_raceChatOpen && Sys_Milliseconds() < s_raceChatSuppressInputUntilMs ) return 1;
			if ( wParam > 0 && wParam < 0x10000 ) {
				io.AddInputCharacter( (unsigned int)wParam );
			}
			return 1;
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			if ( s_raceChatOpen && Sys_Milliseconds() < s_raceChatSuppressInputUntilMs && wParam != VK_ESCAPE ) return 1;
			if ( s_pendingBindCommand ) {
				CL_ImGuiAssignPendingBind( CL_ImGuiWinKeyToQuake( wParam, lParam ) );
				return 1;
			}
			if ( wParam == VK_ESCAPE && s_raceChatOpen ) {
				CL_SpeedrunImGui_CloseRaceChat();
				return 1;
			}
			if ( wParam == VK_ESCAPE && s_raceGuiOpen ) {
				CL_SpeedrunImGui_CloseRaceGui();
				return 1;
			}
			if ( wParam == VK_ESCAPE ) {
				CL_SpeedrunImGui_Close();
				return 1;
			}
			CL_ImGuiUpdateKeyModifiers( io );
			CL_ImGuiAddKeyEvent( io, wParam, true );
			return 1;
		case WM_KEYUP:
		case WM_SYSKEYUP:
			if ( s_raceChatOpen && Sys_Milliseconds() < s_raceChatSuppressInputUntilMs ) return 1;
			CL_ImGuiUpdateKeyModifiers( io );
			CL_ImGuiAddKeyEvent( io, wParam, false );
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
