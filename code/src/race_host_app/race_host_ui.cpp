#include "race_host_shared.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <float.h>
#include <math.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <GL/gl.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl2.h"

#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define DWMWA_CAPTION_COLOR 35
#define DWMWA_TEXT_COLOR 36
#define RACE_TAB_HOST 0
#define RACE_TAB_RACE 1
#define RACE_TAB_RULES 2
#define RACE_TAB_CONFIG 3
#define RACE_TAB_RUNS 4
#define RACE_CONFIG_EDITOR_MAX 16384

HINSTANCE gInstance;
HWND gMainWnd;
static HDC gGlDc;
static HGLRC gGlRc;
static LARGE_INTEGER gPerfFrequency;
static LARGE_INTEGER gLastFrameCounter;
static double gDeltaTime = 1.0 / 60.0;
static int gMouseButtonsDown[5];
static int gMouseTracked;
static HWND gMouseHwnd;
static HCURSOR gCursorArrow;
static HCURSOR gCursorText;
static HCURSOR gCursorHand;
static HCURSOR gCursorSizeWE;
static HCURSOR gCursorSizeNS;
static HCURSOR gCursorSizeAll;
static HCURSOR gCursorNo;
static ImFont *gImguiFontUi;
static ImFont *gImguiFontBold;
static ImFont *gImguiFontLarge;
static ImFont *gImguiFontMono;

char gUiHostName[RACE_NICK_MAX] = "RaceHost";
char gUiIlMap[RACE_MAP_MAX] = "escape1";
char gUiBindAddress[64] = "0.0.0.0";
char gUiChat[RACE_CHAT_MAX] = "";
char gUiCommand[512] = "";
char gUiLog[12000] = "";
char gHostError[512] = "";
char gConfigPath[MAX_PATH] = "";
char gConfigNotice[512] = "";
static char gConfigEditor[RACE_CONFIG_EDITOR_MAX] = "";
static char gConfigEditorStatus[512] = "";
static int gConfigEditorLoaded;
static int gConfigEditorDirty;
static char gRunsExportPath[MAX_PATH] = "race_runs.csv";
static char gRunsExportStatus[512] = "";
int gUiPort = 27960;
int gUiCountdownSec = 5;
int gUiSelectedSlot = 0;
int gUiScrollLog = 0;
int gThemeDark = 1;
float gThemeBlend = 1.0f;
float gThemeTarget = 1.0f;
float gUiAnimTime = 0.0f;
raceLogEntry_t gUiLogEntries[RACE_LOG_MAX];
int gUiLogCount = 0;
int gConfigLoaded = 0;
int gRunning = 1;
static int gActiveTab = 0;
static int gRulesTab = 0;
static float gMetricPulse = 0.0f;
static float gStatusGlow = 0.0f;
static DWORD gLastPingRefreshMs;
static int gDisplayPingMs[RACE_MAX_PLAYERS];
#define RACE_DELTA_SAMPLES 120
static float gDeltaHistory[RACE_MAX_PLAYERS][RACE_DELTA_SAMPLES];
static unsigned char gDeltaValid[RACE_MAX_PLAYERS][RACE_DELTA_SAMPLES];
static int gDeltaHead;
static DWORD gLastDeltaSampleMs;
static int gDeltaRunSession;
static DWORD gDeltaRunStartMs;
static ImVec4 Race_Rgba( int r, int g, int b, int a ) {
    return ImVec4( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );
}

static ImVec4 Race_LerpColor( ImVec4 light, ImVec4 dark, float t ) {
    if ( t < 0.0f ) t = 0.0f;
    if ( t > 1.0f ) t = 1.0f;
    return ImVec4(
        light.x + ( dark.x - light.x ) * t,
        light.y + ( dark.y - light.y ) * t,
        light.z + ( dark.z - light.z ) * t,
        light.w + ( dark.w - light.w ) * t );
}

static ImVec4 Race_ThemeColor( ImVec4 light, ImVec4 dark ) {
    return Race_LerpColor( light, dark, gThemeBlend );
}

static void Race_SetStyleColor( ImGuiCol idx, ImVec4 light, ImVec4 dark ) {
    ImGui::GetStyle().Colors[idx] = Race_ThemeColor( light, dark );
}

static ImVec4 Race_AccentColor( void ) {
    return Race_ThemeColor( Race_Rgba( 0, 95, 184, 255 ), Race_Rgba( 96, 178, 255, 255 ) );
}

static COLORREF Race_ColorRefFromVec4( ImVec4 c ) {
    int r = (int)( c.x * 255.0f + 0.5f );
    int g = (int)( c.y * 255.0f + 0.5f );
    int b = (int)( c.z * 255.0f + 0.5f );
    if ( r < 0 ) r = 0; if ( r > 255 ) r = 255;
    if ( g < 0 ) g = 0; if ( g > 255 ) g = 255;
    if ( b < 0 ) b = 0; if ( b > 255 ) b = 255;
    return RGB( r, g, b );
}

static void Race_UpdateWindowChrome( void ) {
    static COLORREF lastCaption = 0xffffffff;
    static COLORREF lastText = 0xffffffff;
    static BOOL lastDark = 2;
    COLORREF caption;
    COLORREF text;
    BOOL dark;
    if ( !gMainWnd ) return;
    caption = Race_ColorRefFromVec4( Race_ThemeColor( Race_Rgba( 243, 243, 243, 255 ), Race_Rgba( 30, 30, 30, 255 ) ) );
    text = Race_ColorRefFromVec4( Race_ThemeColor( Race_Rgba( 26, 26, 26, 255 ), Race_Rgba( 242, 242, 242, 255 ) ) );
    dark = ( gThemeBlend > 0.5f ) ? TRUE : FALSE;
    if ( dark != lastDark ) {
        DwmSetWindowAttribute( gMainWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof( dark ) );
        lastDark = dark;
    }
    if ( caption != lastCaption ) {
        DwmSetWindowAttribute( gMainWnd, DWMWA_CAPTION_COLOR, &caption, sizeof( caption ) );
        lastCaption = caption;
    }
    if ( text != lastText ) {
        DwmSetWindowAttribute( gMainWnd, DWMWA_TEXT_COLOR, &text, sizeof( text ) );
        lastText = text;
    }
}

static ImVec4 Race_StateColor( int state ) {
    switch ( state ) {
        case RACE_STATE_OFFLINE:   return Race_ThemeColor( Race_Rgba( 92, 98, 108, 255 ), Race_Rgba( 154, 160, 166, 255 ) );
        case RACE_STATE_LOADING:   return Race_ThemeColor( Race_Rgba( 142, 84, 0, 255 ), Race_Rgba( 255, 190, 82, 255 ) );
        case RACE_STATE_COUNTDOWN: return Race_ThemeColor( Race_Rgba( 184, 80, 0, 255 ), Race_Rgba( 255, 160, 67, 255 ) );
        case RACE_STATE_RACING:    return Race_ThemeColor( Race_Rgba( 16, 124, 16, 255 ), Race_Rgba( 78, 203, 113, 255 ) );
        case RACE_STATE_FINISHED:  return Race_ThemeColor( Race_Rgba( 92, 45, 145, 255 ), Race_Rgba( 191, 151, 255, 255 ) );
        default:                   return Race_AccentColor();
    }
}

static ImVec4 Race_LogColor( raceLogKind_t kind ) {
    switch ( kind ) {
        case RACE_LOG_SERVER:  return Race_ThemeColor( Race_Rgba( 0, 95, 184, 255 ), Race_Rgba( 110, 190, 255, 255 ) );
        case RACE_LOG_COMMAND: return Race_ThemeColor( Race_Rgba( 92, 45, 145, 255 ), Race_Rgba( 207, 176, 255, 255 ) );
        case RACE_LOG_CHAT:    return Race_ThemeColor( Race_Rgba( 16, 124, 16, 255 ), Race_Rgba( 105, 220, 140, 255 ) );
        case RACE_LOG_SUCCESS: return Race_ThemeColor( Race_Rgba( 0, 128, 96, 255 ), Race_Rgba( 80, 220, 180, 255 ) );
        case RACE_LOG_WARN:    return Race_ThemeColor( Race_Rgba( 184, 104, 0, 255 ), Race_Rgba( 255, 195, 92, 255 ) );
        case RACE_LOG_ERROR:   return Race_ThemeColor( Race_Rgba( 196, 43, 28, 255 ), Race_Rgba( 255, 122, 108, 255 ) );
        default:               return Race_ThemeColor( Race_Rgba( 36, 36, 36, 255 ), Race_Rgba( 226, 226, 226, 255 ) );
    }
}

static void Race_ApplyImGuiStyle( void ) {
    static float lastBlend = -1.0f;
    ImGuiStyle &style = ImGui::GetStyle();
    if ( lastBlend == gThemeBlend ) return;
    lastBlend = gThemeBlend;
    style.WindowPadding = ImVec2( 10.0f, 10.0f );
    style.FramePadding = ImVec2( 12.0f, 8.0f );
    style.CellPadding = ImVec2( 10.0f, 7.0f );
    style.ItemSpacing = ImVec2( 10.0f, 9.0f );
    style.ItemInnerSpacing = ImVec2( 8.0f, 7.0f );
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 14.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    Race_SetStyleColor( ImGuiCol_Text,          Race_Rgba( 26, 29, 32, 255 ),    Race_Rgba( 224, 229, 232, 255 ) );
    Race_SetStyleColor( ImGuiCol_TextDisabled,  Race_Rgba( 108, 116, 124, 255 ),  Race_Rgba( 132, 141, 148, 255 ) );
    Race_SetStyleColor( ImGuiCol_WindowBg,      Race_Rgba( 246, 247, 248, 255 ),  Race_Rgba( 18, 21, 22, 255 ) );
    Race_SetStyleColor( ImGuiCol_ChildBg,       Race_Rgba( 255, 255, 255, 255 ),  Race_Rgba( 24, 28, 29, 255 ) );
    Race_SetStyleColor( ImGuiCol_PopupBg,       Race_Rgba( 255, 255, 255, 255 ),  Race_Rgba( 31, 36, 37, 255 ) );
    Race_SetStyleColor( ImGuiCol_Border,        Race_Rgba( 220, 224, 227, 255 ),  Race_Rgba( 48, 55, 57, 255 ) );
    Race_SetStyleColor( ImGuiCol_FrameBg,       Race_Rgba( 248, 250, 251, 255 ),  Race_Rgba( 31, 36, 37, 255 ) );
    Race_SetStyleColor( ImGuiCol_FrameBgHovered,Race_Rgba( 232, 246, 252, 255 ),  Race_Rgba( 37, 48, 52, 255 ) );
    Race_SetStyleColor( ImGuiCol_FrameBgActive, Race_Rgba( 210, 236, 247, 255 ),  Race_Rgba( 44, 59, 64, 255 ) );
    Race_SetStyleColor( ImGuiCol_TitleBg,       Race_Rgba( 246, 247, 248, 255 ),  Race_Rgba( 33, 38, 40, 255 ) );
    Race_SetStyleColor( ImGuiCol_TitleBgActive, Race_Rgba( 246, 247, 248, 255 ),  Race_Rgba( 33, 38, 40, 255 ) );
    Race_SetStyleColor( ImGuiCol_Button,        Race_Rgba( 238, 242, 244, 255 ),  Race_Rgba( 31, 36, 37, 255 ) );
    Race_SetStyleColor( ImGuiCol_ButtonHovered, Race_Rgba( 218, 242, 251, 255 ),  Race_Rgba( 42, 52, 55, 255 ) );
    Race_SetStyleColor( ImGuiCol_ButtonActive,  Race_Rgba( 192, 229, 243, 255 ),  Race_Rgba( 10, 132, 166, 255 ) );
    Race_SetStyleColor( ImGuiCol_Header,        Race_Rgba( 222, 244, 252, 255 ),  Race_Rgba( 30, 64, 74, 255 ) );
    Race_SetStyleColor( ImGuiCol_HeaderHovered, Race_Rgba( 204, 237, 249, 255 ),  Race_Rgba( 37, 77, 89, 255 ) );
    Race_SetStyleColor( ImGuiCol_HeaderActive,  Race_Rgba( 184, 226, 242, 255 ),  Race_Rgba( 10, 132, 166, 255 ) );
    Race_SetStyleColor( ImGuiCol_CheckMark,     Race_Rgba( 0, 115, 153, 255 ),    Race_Rgba( 35, 185, 220, 255 ) );
    Race_SetStyleColor( ImGuiCol_SliderGrab,    Race_Rgba( 0, 115, 153, 255 ),    Race_Rgba( 35, 185, 220, 255 ) );
    Race_SetStyleColor( ImGuiCol_SliderGrabActive, Race_Rgba( 0, 89, 122, 255 ),  Race_Rgba( 72, 205, 235, 255 ) );
    Race_SetStyleColor( ImGuiCol_ResizeGrip,    Race_Rgba( 0, 115, 153, 70 ),     Race_Rgba( 35, 185, 220, 70 ) );
    Race_SetStyleColor( ImGuiCol_ResizeGripHovered, Race_Rgba( 0, 115, 153, 140 ), Race_Rgba( 35, 185, 220, 140 ) );
    Race_SetStyleColor( ImGuiCol_ResizeGripActive, Race_Rgba( 0, 115, 153, 200 ),  Race_Rgba( 35, 185, 220, 200 ) );
    Race_SetStyleColor( ImGuiCol_Separator,     Race_Rgba( 220, 224, 227, 255 ),  Race_Rgba( 43, 49, 51, 255 ) );
    Race_SetStyleColor( ImGuiCol_TableHeaderBg, Race_Rgba( 241, 244, 246, 255 ),  Race_Rgba( 28, 33, 34, 255 ) );
    Race_SetStyleColor( ImGuiCol_TableBorderStrong, Race_Rgba( 212, 218, 222, 255 ), Race_Rgba( 48, 55, 57, 255 ) );
    Race_SetStyleColor( ImGuiCol_TableBorderLight, Race_Rgba( 231, 235, 238, 255 ), Race_Rgba( 36, 42, 44, 255 ) );
    Race_SetStyleColor( ImGuiCol_TableRowBg,    Race_Rgba( 255, 255, 255, 0 ),    Race_Rgba( 255, 255, 255, 0 ) );
    Race_SetStyleColor( ImGuiCol_TableRowBgAlt, Race_Rgba( 247, 249, 250, 255 ),  Race_Rgba( 21, 25, 26, 255 ) );
    Race_SetStyleColor( ImGuiCol_ScrollbarBg,   Race_Rgba( 244, 246, 247, 255 ),  Race_Rgba( 20, 24, 25, 255 ) );
    Race_SetStyleColor( ImGuiCol_ScrollbarGrab, Race_Rgba( 184, 193, 199, 255 ),  Race_Rgba( 76, 86, 89, 255 ) );
    Race_SetStyleColor( ImGuiCol_ScrollbarGrabHovered, Race_Rgba( 152, 164, 172, 255 ), Race_Rgba( 96, 110, 114, 255 ) );
}

void Race_InitUiState( void ) {
    Race_Copy( gUiHostName, sizeof( gUiHostName ), raceHost.hostName );
    Race_Copy( gUiIlMap, sizeof( gUiIlMap ), raceHost.ilMap );
    Race_Copy( gUiBindAddress, sizeof( gUiBindAddress ), raceHost.bindAddress[0] ? raceHost.bindAddress : "0.0.0.0" );
    gUiPort = raceHost.port > 0 ? raceHost.port : 27960;
    gUiCountdownSec = raceHost.countdownMs / 1000;
    if ( gUiCountdownSec < 1 ) gUiCountdownSec = 5;
    gUiChat[0] = '\0';
    gUiCommand[0] = '\0';
    gUiLog[0] = '\0';
    gHostError[0] = '\0';
    gUiLogCount = 0;
}

static raceLogKind_t Race_DetectLogKind( const char *line ) {
    if ( !line || !line[0] ) return RACE_LOG_INFO;
    if ( line[0] == '!' || strstr( line, "failed" ) || strstr( line, "cannot" ) ) return RACE_LOG_ERROR;
    if ( line[0] == '>' ) return RACE_LOG_COMMAND;
    if ( !_strnicmp( line, "<server>", 8 ) ) return RACE_LOG_SERVER;
    if ( line[0] == '<' ) return RACE_LOG_CHAT;
    if ( line[0] == '+' || strstr( line, "online" ) || strstr( line, "STARTED" ) ) return RACE_LOG_SUCCESS;
    if ( line[0] == '-' || strstr( line, "stopped" ) || strstr( line, "reset" ) ) return RACE_LOG_WARN;
    return RACE_LOG_INFO;
}

static void Race_PushLogEntry( const char *text, raceLogKind_t kind ) {
    if ( gUiLogCount >= RACE_LOG_MAX ) {
        memmove( &gUiLogEntries[0], &gUiLogEntries[1], sizeof( gUiLogEntries[0] ) * ( RACE_LOG_MAX - 1 ) );
        gUiLogCount = RACE_LOG_MAX - 1;
    }
    Race_Copy( gUiLogEntries[gUiLogCount].text, sizeof( gUiLogEntries[gUiLogCount].text ), text );
    gUiLogEntries[gUiLogCount].kind = kind;
    gUiLogCount++;
}

static void Race_AppendLog( const char *line ) {
    SYSTEMTIME st;
    char entry[768];
    size_t need;
    size_t len;

    GetLocalTime( &st );
    snprintf( entry, sizeof( entry ), "%s[%02d:%02d:%02d] %s",
              gUiLog[0] ? "\n" : "", st.wHour, st.wMinute, st.wSecond, line ? line : "" );
    entry[sizeof( entry ) - 1] = '\0';
    Race_PushLogEntry( entry[0] == '\n' ? entry + 1 : entry, Race_DetectLogKind( line ) );

    need = strlen( entry );
    len = strlen( gUiLog );
    if ( len + need + 1 >= sizeof( gUiLog ) ) {
        size_t drop = ( len + need + 1 ) - sizeof( gUiLog ) + 2048;
        char *nl = NULL;
        if ( drop < len ) nl = strchr( gUiLog + drop, '\n' );
        if ( nl ) memmove( gUiLog, nl + 1, strlen( nl + 1 ) + 1 );
        else gUiLog[0] = '\0';
    }
    strncat( gUiLog, entry, sizeof( gUiLog ) - strlen( gUiLog ) - 1 );
    gUiScrollLog = 1;
}

void Race_GuiLog( const char *fmt, ... ) {
    char line[512];
    va_list args;
    va_start( args, fmt );
    vsnprintf( line, sizeof( line ), fmt, args );
    va_end( args );
    line[sizeof( line ) - 1] = '\0';
    Race_AppendLog( line );
}

void Race_GuiLogSrv( const char *fmt, ... ) {
    char body[480];
    char line[512];
    va_list args;
    va_start( args, fmt );
    vsnprintf( body, sizeof( body ), fmt, args );
    va_end( args );
    body[sizeof( body ) - 1] = '\0';
    snprintf( line, sizeof( line ), "<server> %s", body );
    line[sizeof( line ) - 1] = '\0';
    Race_AppendLog( line );
}

void Race_GuiRefresh( void ) {
    char title[256];
    if ( !gMainWnd ) return;
    snprintf( title, sizeof( title ), "RtCW Race Host  ::  %s  ::  %d/%d players",
              Race_StateName( raceHost.state ), Race_PlayerCount(), raceHost.maxPlayers );
    SetWindowTextA( gMainWnd, title );
}

static void Race_ApplyModeVisibility( void ) {
}

void Race_OnSettingsChanged( int writeBack ) {
    Race_NormalizeSettings();
    if ( writeBack && Race_HostIsRunning() ) Race_BroadcastConfig();
}

static int Race_GetSelectedSlot( void ) {
    return gUiSelectedSlot;
}

static void Race_KickSelected( void ) {
    int slot = Race_GetSelectedSlot();
    racePlayer_t *p = Race_FindPlayerBySlot( slot );
    if ( !p ) { Race_GuiLogSrv( "kick: no player selected" ); return; }
    Race_SendText( &p->address, "srace reject You_have_been_kicked_from_the_race" );
    p->kicked = 1;
    p->inMenu = 1;
    p->loaded = 0;
    p->started = 0;
    p->lastKickRejectMs = GetTickCount();
    Race_GuiLogSrv( "kicked slot %d (%s)", p->slot, p->nick );
    Race_BroadcastRoster();
}

static int Race_LoadConfigEditor( void ) {
    FILE *f;
    long fileSize = 0;
    size_t readSize;
    if ( !gConfigPath[0] ) Race_BuildConfigPath();
    f = fopen( gConfigPath, "rb" );
    if ( !f ) {
        if ( !Race_LoadServerConfig( 0 ) ) {
            snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "could not create/read %s", Race_FileNameFromPath( gConfigPath ) );
            gConfigEditorStatus[sizeof( gConfigEditorStatus ) - 1] = '\0';
            return 0;
        }
        f = fopen( gConfigPath, "rb" );
    }
    if ( !f ) {
        snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "could not open %s", Race_FileNameFromPath( gConfigPath ) );
        gConfigEditorStatus[sizeof( gConfigEditorStatus ) - 1] = '\0';
        return 0;
    }
    if ( fseek( f, 0, SEEK_END ) == 0 ) {
        fileSize = ftell( f );
        fseek( f, 0, SEEK_SET );
    }
    readSize = fread( gConfigEditor, 1, sizeof( gConfigEditor ) - 1, f );
    gConfigEditor[readSize] = '\0';
    fclose( f );
    gConfigEditorLoaded = 1;
    gConfigEditorDirty = 0;
    if ( fileSize >= (long)sizeof( gConfigEditor ) ) {
        snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "loaded first %u bytes; file is larger than editor buffer", (unsigned int)( sizeof( gConfigEditor ) - 1 ) );
    } else {
        snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "loaded %u bytes from %s", (unsigned int)readSize, Race_FileNameFromPath( gConfigPath ) );
    }
    gConfigEditorStatus[sizeof( gConfigEditorStatus ) - 1] = '\0';
    return 1;
}

static int Race_SaveConfigEditor( void ) {
    FILE *f;
    size_t len;
    size_t wrote;
    if ( !gConfigPath[0] ) Race_BuildConfigPath();
    f = fopen( gConfigPath, "wb" );
    if ( !f ) {
        snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "could not write %s", Race_FileNameFromPath( gConfigPath ) );
        Race_GuiLog( "! %s", gConfigEditorStatus );
        return 0;
    }
    len = strlen( gConfigEditor );
    wrote = fwrite( gConfigEditor, 1, len, f );
    fclose( f );
    if ( wrote != len ) {
        snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "partial write: %u/%u bytes", (unsigned int)wrote, (unsigned int)len );
        Race_GuiLog( "! %s", gConfigEditorStatus );
        return 0;
    }
    gConfigEditorDirty = 0;
    snprintf( gConfigEditorStatus, sizeof( gConfigEditorStatus ), "saved %u bytes to %s", (unsigned int)len, Race_FileNameFromPath( gConfigPath ) );
    Race_GuiLogSrv( "%s", gConfigEditorStatus );
    Race_LoadServerConfig( 0 );
    return 1;
}

static void Race_SaySomething( void ) {
    if ( !gUiChat[0] ) return;
    if ( !Race_HostIsRunning() ) {
        Race_GuiLog( "! cannot send chat: host is offline" );
        gUiChat[0] = '\0';
        return;
    }
    Race_BroadcastText( "srace chat %d -1 %s %s", raceHost.session, raceHost.hostName, gUiChat );
    Race_GuiLog( "<%s> %s", raceHost.hostName, gUiChat );
    gUiChat[0] = '\0';
}

void Race_PrintHelp( void ) {
    Race_GuiLogSrv( "commands:" );
    Race_GuiLogSrv( "  status | list                 show lobby state" );
    Race_GuiLogSrv( "  hoststart | hoststop          open/close UDP lobby" );
    Race_GuiLogSrv( "  bind <ip> | port <1..65535>   set host bind before start" );
    Race_GuiLogSrv( "  start | stop | reset          race controls" );
    Race_GuiLogSrv( "  kick <slot>                   kick a player" );
    Race_GuiLogSrv( "  mode <0 full|1 chapter|2 il>  set race mode" );
    Race_GuiLogSrv( "  chapter <1..5>                set chapter" );
    Race_GuiLogSrv( "  diff <1..3>                   set difficulty" );
    Race_GuiLogSrv( "  ilmap <name>                  set IL map" );
    Race_GuiLogSrv( "  countdown <1..30>             set countdown seconds" );
    Race_GuiLogSrv( "  100 <0|1> | hl1 <0|1>         set rules" );
    Race_GuiLogSrv( "  autojump <0|1> | ac <0|1>     set helper/anti-cheat" );
    Race_GuiLogSrv( "  queue <0|1> | private <0|1>   set lobby access" );
    Race_GuiLogSrv( "  password <text> | readycheck <0|1>" );
    Race_GuiLogSrv( "  exportcsv <path> | exportjson <path>" );
    Race_GuiLogSrv( "  hostname <name>               set host chat name" );
    Race_GuiLogSrv( "  say <text>                    send chat message" );
    Race_GuiLogSrv( "  clear                         clear log" );
    Race_GuiLogSrv( "  quit | exit                   close the host" );
}

static void Race_PrintStatus( void ) {
    int i;
    Race_GuiLogSrv( "state=%s  session=%d  bind=%s:%d  players=%d/%d",
        Race_StateName( raceHost.state ), raceHost.session,
        raceHost.bindAddress[0] ? raceHost.bindAddress : gUiBindAddress, raceHost.port,
        Race_PlayerCount(), raceHost.maxPlayers );
    Race_GuiLogSrv( "mode=%d chapter=%d diff=%d 100%%=%d HL1=%d autoJump=%d AC=%d target=%s",
        raceHost.mode, raceHost.mission, raceHost.difficulty, raceHost.percent100,
        raceHost.hl1Movement, raceHost.autoJump, raceHost.antiCheat, Race_ResolveStartMap() );
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( !p->used ) continue;
        Race_GuiLogSrv( "  #%d %-16s %-6s map=%-10s addr=%s",
            p->slot, p->nick, Race_PlayerStateText( p ),
            p->map[0] ? p->map : "-", Race_AddrToString( &p->address ) );
    }
}

static void Race_ProcessCmdLine( const char *line ) {
    char buf[512];
    char *tokens[16];
    int n;
    Race_Copy( buf, sizeof( buf ), line );
    while ( buf[0] == ' ' || buf[0] == '\t' ) memmove( buf, buf + 1, strlen( buf ) );
    if ( !buf[0] ) return;
    Race_GuiLog( "> %s", buf );
    n = Race_Tokenize( buf, tokens, 16 );
    if ( n <= 0 ) return;
    if ( !_stricmp( tokens[0], "help" ) || !_stricmp( tokens[0], "?" ) ) {
        Race_PrintHelp();
    } else if ( !_stricmp( tokens[0], "status" ) || !_stricmp( tokens[0], "list" ) ) {
        Race_PrintStatus();
    } else if ( !_stricmp( tokens[0], "hoststart" ) || !_stricmp( tokens[0], "open" ) ) {
        Race_StartHost();
    } else if ( !_stricmp( tokens[0], "hoststop" ) || !_stricmp( tokens[0], "close" ) ) {
        Race_StopHost();
    } else if ( !_stricmp( tokens[0], "bind" ) ) {
        if ( Race_HostIsRunning() ) { Race_GuiLogSrv( "bind: stop host before changing IP" ); return; }
        if ( n < 2 ) { Race_GuiLogSrv( "usage: bind <ip>" ); return; }
        Race_Copy( gUiBindAddress, sizeof( gUiBindAddress ), tokens[1] );
        Race_GuiLogSrv( "bind=%s", gUiBindAddress );
    } else if ( !_stricmp( tokens[0], "port" ) ) {
        if ( Race_HostIsRunning() ) { Race_GuiLogSrv( "port: stop host before changing port" ); return; }
        if ( n < 2 ) { Race_GuiLogSrv( "usage: port <1..65535>" ); return; }
        gUiPort = atoi( tokens[1] );
        if ( gUiPort < 1 ) gUiPort = 1;
        if ( gUiPort > 65535 ) gUiPort = 65535;
        raceHost.port = gUiPort;
        Race_GuiLogSrv( "port=%d", gUiPort );
    } else if ( !_stricmp( tokens[0], "start" ) ) {
        Race_OnSettingsChanged( 1 );
        Race_StartRace();
    } else if ( !_stricmp( tokens[0], "stop" ) ) {
        Race_StopRace();
    } else if ( !_stricmp( tokens[0], "reset" ) ) {
        Race_ResetLobby();
    } else if ( !_stricmp( tokens[0], "kick" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: kick <slot>" ); return; }
        { int slot = atoi( tokens[1] );
          racePlayer_t *p = Race_FindPlayerBySlot( slot );
          if ( !p ) { Race_GuiLogSrv( "kick: no player at slot %d", slot ); return; }
          Race_SendText( &p->address, "srace reject You_have_been_kicked_from_the_race" );
          p->kicked = 1; p->inMenu = 1; p->loaded = 0; p->started = 0;
          p->lastKickRejectMs = GetTickCount();
          Race_GuiLogSrv( "kicked slot %d (%s)", p->slot, p->nick );
          Race_BroadcastRoster();
        }
    } else if ( !_stricmp( tokens[0], "mode" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: mode <0|1|2>" ); return; }
        raceHost.mode = atoi( tokens[1] );
        if ( raceHost.mode < 0 || raceHost.mode > 2 ) raceHost.mode = 0;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "mode=%d", raceHost.mode );
    } else if ( !_stricmp( tokens[0], "chapter" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: chapter <1..5>" ); return; }
        raceHost.mission = atoi( tokens[1] );
        if ( raceHost.mission < 1 ) raceHost.mission = 1;
        if ( raceHost.mission > 5 ) raceHost.mission = 5;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "chapter=%d (%s)", raceHost.mission, Race_ResolveStartMap() );
    } else if ( !_stricmp( tokens[0], "diff" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: diff <1..3>" ); return; }
        raceHost.difficulty = atoi( tokens[1] );
        if ( raceHost.difficulty < 1 ) raceHost.difficulty = 1;
        if ( raceHost.difficulty > 3 ) raceHost.difficulty = 3;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "difficulty=%d", raceHost.difficulty );
    } else if ( !_stricmp( tokens[0], "ilmap" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: ilmap <name>" ); return; }
        Race_Copy( gUiIlMap, sizeof( gUiIlMap ), tokens[1] );
        Race_SanitizeToken( gUiIlMap, raceHost.ilMap, sizeof( raceHost.ilMap ) );
        Race_Copy( gUiIlMap, sizeof( gUiIlMap ), raceHost.ilMap );
        Race_BroadcastConfig();
        Race_GuiLogSrv( "ilmap=%s", raceHost.ilMap );
    } else if ( !_stricmp( tokens[0], "countdown" ) ) {
        int s = ( n > 1 ) ? atoi( tokens[1] ) : 5;
        if ( s < 1 ) s = 1; if ( s > 30 ) s = 30;
        gUiCountdownSec = s;
        raceHost.countdownMs = s * 1000;
        Race_GuiLogSrv( "countdown=%ds", s );
    } else if ( !_stricmp( tokens[0], "100" ) ) {
        raceHost.percent100 = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "100%%=%d", raceHost.percent100 );
    } else if ( !_stricmp( tokens[0], "hl1" ) ) {
        raceHost.hl1Movement = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "hl1=%d", raceHost.hl1Movement );
    } else if ( !_stricmp( tokens[0], "autojump" ) ) {
        raceHost.autoJump = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "autoJump=%d", raceHost.autoJump );
    } else if ( !_stricmp( tokens[0], "anticheat" ) || !_stricmp( tokens[0], "ac" ) ) {
        raceHost.antiCheat = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_BroadcastConfig();
        Race_GuiLogSrv( "anticheat=%d", raceHost.antiCheat );
    } else if ( !_stricmp( tokens[0], "queue" ) ) {
        raceHost.queueEnabled = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_GuiLogSrv( "queue=%d", raceHost.queueEnabled );
    } else if ( !_stricmp( tokens[0], "private" ) ) {
        raceHost.privateLobby = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_GuiLogSrv( "private=%d", raceHost.privateLobby );
    } else if ( !_stricmp( tokens[0], "password" ) ) {
        if ( n < 2 ) { raceHost.password[0] = '\0'; Race_GuiLogSrv( "password cleared" ); }
        else { Race_SanitizeOptionalToken( tokens[1], raceHost.password, sizeof( raceHost.password ) ); Race_GuiLogSrv( "password set" ); }
    } else if ( !_stricmp( tokens[0], "readycheck" ) ) {
        raceHost.autoReadyCheck = ( n > 1 && atoi( tokens[1] ) ) ? 1 : 0;
        Race_GuiLogSrv( "readycheck=%d", raceHost.autoReadyCheck );
    } else if ( !_stricmp( tokens[0], "exportcsv" ) || !_stricmp( tokens[0], "exportjson" ) ) {
        char status[512];
        const char *path = n > 1 ? tokens[1] : ( !_stricmp( tokens[0], "exportcsv" ) ? "race_runs.csv" : "race_runs.json" );
        if ( !_stricmp( tokens[0], "exportcsv" ) ) Race_ExportRunsCsv( path, status, sizeof( status ) );
        else Race_ExportRunsJson( path, status, sizeof( status ) );
        Race_GuiLogSrv( "%s", status[0] ? status : "export complete" );
    } else if ( !_stricmp( tokens[0], "say" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: say <text>" ); return; }
        if ( !Race_HostIsRunning() ) { Race_GuiLogSrv( "say: host is offline" ); return; }
        { const char *rest = strchr( line, ' ' );
          while ( rest && ( *rest == ' ' || *rest == '\t' ) ) rest++;
          if ( rest && rest[0] ) {
              Race_BroadcastText( "srace chat %d -1 %s %s", raceHost.session, raceHost.hostName, rest );
              Race_GuiLog( "<%s> %s", raceHost.hostName, rest );
          }
        }
    } else if ( !_stricmp( tokens[0], "hostname" ) ) {
        if ( n < 2 ) { Race_GuiLogSrv( "usage: hostname <name>" ); return; }
        Race_SanitizeToken( tokens[1], raceHost.hostName, sizeof( raceHost.hostName ) );
        Race_Copy( gUiHostName, sizeof( gUiHostName ), raceHost.hostName );
        Race_GuiLogSrv( "hostname=%s", raceHost.hostName );
    } else if ( !_stricmp( tokens[0], "clear" ) ) {
        gUiLog[0] = '\0';
        gUiLogCount = 0;
    } else if ( !_stricmp( tokens[0], "quit" ) || !_stricmp( tokens[0], "exit" ) ) {
        if ( gMainWnd ) PostMessageA( gMainWnd, WM_CLOSE, 0, 0 );
    } else {
        Race_GuiLogSrv( "unknown command: %s - try 'help'", tokens[0] );
    }
    Race_GuiRefresh();
}

static ImGuiKey Race_VirtualKeyToImGuiKey( WPARAM vk ) {
    if ( vk >= '0' && vk <= '9' ) return (ImGuiKey)( ImGuiKey_0 + ( vk - '0' ) );
    if ( vk >= 'A' && vk <= 'Z' ) return (ImGuiKey)( ImGuiKey_A + ( vk - 'A' ) );
    if ( vk >= VK_F1 && vk <= VK_F24 ) return (ImGuiKey)( ImGuiKey_F1 + ( vk - VK_F1 ) );
    if ( vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9 ) return (ImGuiKey)( ImGuiKey_Keypad0 + ( vk - VK_NUMPAD0 ) );
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
        case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
        case VK_DIVIDE: return ImGuiKey_KeypadDivide;
        case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case VK_ADD: return ImGuiKey_KeypadAdd;
        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
        case VK_LSHIFT: return ImGuiKey_LeftShift;
        case VK_LMENU: return ImGuiKey_LeftAlt;
        case VK_LWIN: return ImGuiKey_LeftSuper;
        case VK_RCONTROL: return ImGuiKey_RightCtrl;
        case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_RMENU: return ImGuiKey_RightAlt;
        case VK_RWIN: return ImGuiKey_RightSuper;
        case VK_APPS: return ImGuiKey_Menu;
        default: return ImGuiKey_None;
    }
}

static void Race_UpdateKeyMods( void ) {
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent( ImGuiMod_Ctrl,  ( GetKeyState( VK_CONTROL ) & 0x8000 ) != 0 );
    io.AddKeyEvent( ImGuiMod_Shift, ( GetKeyState( VK_SHIFT ) & 0x8000 ) != 0 );
    io.AddKeyEvent( ImGuiMod_Alt,   ( GetKeyState( VK_MENU ) & 0x8000 ) != 0 );
    io.AddKeyEvent( ImGuiMod_Super, ( ( GetKeyState( VK_LWIN ) | GetKeyState( VK_RWIN ) ) & 0x8000 ) != 0 );
}

static void Race_UpdateMouseCursor( void ) {
    ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
    if ( ImGui::GetIO().MouseDrawCursor || cursor == ImGuiMouseCursor_None ) {
        SetCursor( NULL );
        return;
    }
    switch ( cursor ) {
        case ImGuiMouseCursor_TextInput: SetCursor( LoadCursor( NULL, IDC_IBEAM ) ); break;
        case ImGuiMouseCursor_ResizeAll: SetCursor( LoadCursor( NULL, IDC_SIZEALL ) ); break;
        case ImGuiMouseCursor_ResizeEW:  SetCursor( LoadCursor( NULL, IDC_SIZEWE ) ); break;
        case ImGuiMouseCursor_ResizeNS:  SetCursor( LoadCursor( NULL, IDC_SIZENS ) ); break;
        case ImGuiMouseCursor_ResizeNESW:SetCursor( LoadCursor( NULL, IDC_SIZENESW ) ); break;
        case ImGuiMouseCursor_ResizeNWSE:SetCursor( LoadCursor( NULL, IDC_SIZENWSE ) ); break;
        case ImGuiMouseCursor_Hand:      SetCursor( LoadCursor( NULL, IDC_HAND ) ); break;
        default:                         SetCursor( LoadCursor( NULL, IDC_ARROW ) ); break;
    }
}

LRESULT CALLBACK Race_WindowProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
    ImGuiIO *io = ImGui::GetCurrentContext() ? &ImGui::GetIO() : NULL;
    switch ( msg ) {
        case WM_MOUSEMOVE:
            if ( io ) io->AddMousePosEvent( (float)GET_X_LPARAM( lParam ), (float)GET_Y_LPARAM( lParam ) );
            if ( !gMouseTracked ) {
                TRACKMOUSEEVENT tme;
                memset( &tme, 0, sizeof( tme ) );
                tme.cbSize = sizeof( tme );
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent( &tme );
                gMouseTracked = 1;
            }
            return 0;
        case WM_MOUSELEAVE:
            if ( io ) io->AddMousePosEvent( -FLT_MAX, -FLT_MAX );
            gMouseTracked = 0;
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            SetCapture( hwnd );
            if ( io ) io->AddMouseButtonEvent( 0, true );
            return 0;
        case WM_LBUTTONUP:
            if ( io ) io->AddMouseButtonEvent( 0, false );
            ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
            SetCapture( hwnd );
            if ( io ) io->AddMouseButtonEvent( 1, true );
            return 0;
        case WM_RBUTTONUP:
            if ( io ) io->AddMouseButtonEvent( 1, false );
            ReleaseCapture();
            return 0;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            SetCapture( hwnd );
            if ( io ) io->AddMouseButtonEvent( 2, true );
            return 0;
        case WM_MBUTTONUP:
            if ( io ) io->AddMouseButtonEvent( 2, false );
            ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            if ( io ) io->AddMouseWheelEvent( 0.0f, (float)GET_WHEEL_DELTA_WPARAM( wParam ) / (float)WHEEL_DELTA );
            return 0;
        case WM_MOUSEHWHEEL:
            if ( io ) io->AddMouseWheelEvent( -(float)GET_WHEEL_DELTA_WPARAM( wParam ) / (float)WHEEL_DELTA, 0.0f );
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if ( io ) {
                ImGuiKey key = Race_VirtualKeyToImGuiKey( wParam );
                Race_UpdateKeyMods();
                if ( key != ImGuiKey_None ) io->AddKeyEvent( key, msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN );
            }
            return 0;
        case WM_CHAR:
            if ( io && wParam > 0 && wParam < 0x10000 ) io->AddInputCharacter( (unsigned int)wParam );
            return 0;
        case WM_SETCURSOR:
            if ( LOWORD( lParam ) == HTCLIENT && ImGui::GetCurrentContext() ) {
                Race_UpdateMouseCursor();
                return 1;
            }
            break;
        case WM_SIZE:
            return 0;
        case WM_CLOSE:
            DestroyWindow( hwnd );
            return 0;
        case WM_DESTROY:
            gRunning = 0;
            PostQuitMessage( 0 );
            return 0;
    }
    return DefWindowProcA( hwnd, msg, wParam, lParam );
}

int Race_CreateOpenGLContext( HWND hwnd ) {
    PIXELFORMATDESCRIPTOR pfd;
    int pixelFormat;
    memset( &pfd, 0, sizeof( pfd ) );
    pfd.nSize = sizeof( pfd );
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    gGlDc = GetDC( hwnd );
    if ( !gGlDc ) return 0;
    pixelFormat = ChoosePixelFormat( gGlDc, &pfd );
    if ( !pixelFormat ) return 0;
    if ( !SetPixelFormat( gGlDc, pixelFormat, &pfd ) ) return 0;
    gGlRc = wglCreateContext( gGlDc );
    if ( !gGlRc ) return 0;
    if ( !wglMakeCurrent( gGlDc, gGlRc ) ) return 0;
    return 1;
}

int Race_InitImGui( void ) {
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendPlatformName = "rtcw_racehost_win32";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    gImguiFontUi = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeui.ttf", 18.0f );
    gImguiFontBold = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeuib.ttf", 18.0f );
    gImguiFontLarge = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeuib.ttf", 32.0f );
    gImguiFontMono = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\consola.ttf", 16.0f );
    if ( !gImguiFontUi ) gImguiFontUi = io.Fonts->AddFontDefault();
    if ( !gImguiFontBold ) gImguiFontBold = gImguiFontUi;
    if ( !gImguiFontLarge ) gImguiFontLarge = gImguiFontUi;
    if ( !gImguiFontMono ) gImguiFontMono = gImguiFontUi;

    Race_ApplyImGuiStyle();
    if ( !ImGui_ImplOpenGL2_Init() ) return 0;
    QueryPerformanceFrequency( &gPerfFrequency );
    QueryPerformanceCounter( &gLastFrameCounter );
    return 1;
}

void Race_ShutdownImGui( void ) {
    if ( ImGui::GetCurrentContext() ) {
        ImGui_ImplOpenGL2_Shutdown();
        ImGui::DestroyContext();
    }
    if ( gGlRc ) {
        wglMakeCurrent( NULL, NULL );
        wglDeleteContext( gGlRc );
        gGlRc = NULL;
    }
    if ( gGlDc && gMainWnd ) {
        ReleaseDC( gMainWnd, gGlDc );
        gGlDc = NULL;
    }
}

static void Race_DrawSectionTitle( const char *text ) {
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::TextUnformatted( text );
    ImGui::PopFont();
    ImGui::Spacing();
}

static void Race_DrawFieldLabel( const char *text ) {
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::TextDisabled( "%s", text );
    ImGui::PopFont();
}

static void Race_DrawMetric( const char *label, const char *value, ImVec4 color ) {
    ImDrawList *drawList;
    ImVec2 p0;
    ImVec2 p1;
    float pulse = 0.65f + 0.35f * (float)( 0.5 + 0.5 * sin( gUiAnimTime * 3.2f ) );
    ImVec4 glow = color;
    glow.w = 0.16f * pulse;
    ImGui::BeginChild( label, ImVec2( 0.0f, 96.0f ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    drawList = ImGui::GetWindowDrawList();
    p0 = ImGui::GetWindowPos();
    p1 = ImVec2( p0.x + ImGui::GetWindowSize().x, p0.y + 3.0f );
    drawList->AddRectFilled( p0, p1, ImGui::ColorConvertFloat4ToU32( color ), 8.0f, ImDrawFlags_RoundCornersTop );
    drawList->AddRectFilled( ImVec2( p0.x, p0.y + 3.0f ), ImVec2( p0.x + ImGui::GetWindowSize().x, p0.y + 20.0f ),
        ImGui::ColorConvertFloat4ToU32( glow ) );
    ImGui::TextDisabled( "%s", label );
    ImGui::PushStyleColor( ImGuiCol_Text, color );
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x );
    ImGui::TextUnformatted( value );
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

static void Race_DrawThemeToggle( void ) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 p;
    float width = 48.0f;
    float height = 24.0f;
    float frameHeight = ImGui::GetFrameHeight();
    float radius = height * 0.5f;
    ImVec4 track = Race_LerpColor( Race_Rgba( 196, 201, 208, 255 ), Race_Rgba( 96, 178, 255, 255 ), gThemeBlend );
    ImVec4 knob = Race_LerpColor( Race_Rgba( 255, 255, 255, 255 ), Race_Rgba( 24, 24, 24, 255 ), gThemeBlend );
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted( gThemeDark ? "Dark" : "Light" );
    ImGui::SameLine();
    p = ImGui::GetCursorScreenPos();
    p.y += ( frameHeight - height ) * 0.5f;
    ImGui::SetCursorScreenPos( p );
    ImGui::InvisibleButton( "##theme_toggle", ImVec2( width, height ) );
    if ( ImGui::IsItemClicked() ) {
        gThemeDark = !gThemeDark;
        gThemeTarget = gThemeDark ? 1.0f : 0.0f;
    }
    drawList->AddRectFilled( p, ImVec2( p.x + width, p.y + height ),
        ImGui::ColorConvertFloat4ToU32( track ), radius );
    drawList->AddCircleFilled( ImVec2( p.x + radius + ( width - height ) * gThemeBlend, p.y + radius ),
        radius - 3.0f, ImGui::ColorConvertFloat4ToU32( knob ) );
}

static void Race_DrawStatusDot( ImVec4 color ) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float frameHeight = ImGui::GetFrameHeight();
    float centerY = p.y + frameHeight * 0.5f;
    float pulse = 0.55f + 0.45f * (float)( 0.5 + 0.5 * sin( gUiAnimTime * 4.0f ) );
    ImVec4 glow = color;
    glow.w = 0.22f * pulse;
    drawList->AddCircleFilled( ImVec2( p.x + 11.0f, centerY ), 9.0f + 5.0f * pulse, ImGui::ColorConvertFloat4ToU32( glow ) );
    drawList->AddCircleFilled( ImVec2( p.x + 11.0f, centerY ), 6.0f, ImGui::ColorConvertFloat4ToU32( color ) );
    ImGui::Dummy( ImVec2( 24.0f, frameHeight ) );
}

static void Race_DrawTooltip( const char *text ) {
    if ( !text || !text[0] ) return;
    if ( !ImGui::IsItemHovered() ) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos( ImGui::GetFontSize() * 28.0f );
    ImGui::TextUnformatted( text );
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

static void Race_DrawNoticeBanner( const char *text, raceLogKind_t kind ) {
    ImVec4 color;
    ImVec4 bg;
    ImVec2 p0;
    ImVec2 p1;
    if ( !text || !text[0] ) return;
    color = Race_LogColor( kind );
    if ( gThemeBlend > 0.5f ) {
        bg = Race_Rgba( 13, 16, 18, 246 );
    } else {
        bg = color;
        bg.w = 0.10f;
    }
    ImGui::PushID( text );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, bg );
    ImGui::BeginChild( "notice", ImVec2( 0.0f, 56.0f ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    p0 = ImGui::GetWindowPos();
    p1 = ImVec2( p0.x + 4.0f, p0.y + ImGui::GetWindowSize().y );
    ImGui::GetWindowDrawList()->AddRectFilled( p0, p1, ImGui::ColorConvertFloat4ToU32( color ), 4.0f, ImDrawFlags_RoundCornersLeft );
    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 6.0f );
    ImGui::PushStyleColor( ImGuiCol_Text, color );
    ImGui::TextWrapped( "%s", text );
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Race_DrawTooltip( text );
    ImGui::PopID();
}

static int Race_DrawPrimaryButton( const char *label, int enabled, float height ) {
    int pressed;
    ImVec4 accent = Race_AccentColor();
    ImGui::BeginDisabled( !enabled );
    ImGui::PushStyleColor( ImGuiCol_Button, accent );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, Race_ThemeColor( Race_Rgba( 0, 120, 212, 255 ), Race_Rgba( 120, 210, 255, 255 ) ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive, Race_ThemeColor( Race_Rgba( 0, 84, 148, 255 ), Race_Rgba( 54, 180, 230, 255 ) ) );
    ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( 255, 255, 255, 255 ) );
    pressed = ImGui::Button( label, ImVec2( -1.0f, height ) );
    ImGui::PopStyleColor( 4 );
    ImGui::EndDisabled();
    return pressed;
}

static void Race_DrawStatusTile( const char *id, const char *label, const char *value, ImVec4 color, const char *hint, float width ) {
    ImDrawList *drawList;
    ImVec2 p0;
    ImVec2 p1;
    ImGui::BeginChild( id, ImVec2( width, 70.0f ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    drawList = ImGui::GetWindowDrawList();
    p0 = ImGui::GetWindowPos();
    p1 = ImVec2( p0.x + ImGui::GetWindowSize().x, p0.y + 3.0f );
    drawList->AddRectFilled( p0, p1, ImGui::ColorConvertFloat4ToU32( color ), 6.0f, ImDrawFlags_RoundCornersTop );
    ImGui::TextDisabled( "%s", label );
    ImGui::PushStyleColor( ImGuiCol_Text, color );
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::TextWrapped( "%s", value && value[0] ? value : "-" );
    ImGui::PopFont();
    ImGui::PopStyleColor();
    if ( hint && hint[0] ) ImGui::TextDisabled( "%s", hint );
    ImGui::EndChild();
    Race_DrawTooltip( hint );
}

#include "race_host_ui_safety.inl"

static void Race_DrawSettingsPanel( float height ) {
    static const char *modeLabels[] = { "Full Game", "Chapter", "Individual Level" };
    int hostRunning = Race_HostIsRunning();
    int lobby = ( hostRunning && raceHost.state == RACE_STATE_LOBBY );
    int changed = 0;
    char hostReason[256];
    char raceReason[256];
    int hostSettingsOk = Race_ValidateHostSettings( hostReason, sizeof( hostReason ) );
    int raceSettingsOk = Race_ValidateRaceSettings( raceReason, sizeof( raceReason ) );

    ImGui::BeginChild( "settings", ImVec2( 430.0f, height ), true );
    if ( ImGui::BeginTabBar( "racehost_tabs", ImGuiTabBarFlags_FittingPolicyScroll ) ) {
        if ( ImGui::BeginTabItem( "Host" ) ) {
            gActiveTab = RACE_TAB_HOST;
            Race_DrawSectionTitle( "Lobby Host" );
            ImGui::BeginDisabled( hostRunning );
            Race_DrawFieldLabel( "Bind IP" );
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputText( "##bind_ip", gUiBindAddress, sizeof( gUiBindAddress ) );
            Race_DrawFieldLabel( "UDP port" );
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::InputInt( "##udp_port", &gUiPort, 1, 100 ) ) {
                if ( gUiPort < 1 ) gUiPort = 1;
                if ( gUiPort > 65535 ) gUiPort = 65535;
                raceHost.port = gUiPort;
            }
            ImGui::EndDisabled();

            Race_DrawFieldLabel( "Host name" );
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::InputText( "##host_name", gUiHostName, sizeof( gUiHostName ) ) ) {
                Race_OnSettingsChanged( 0 );
            }
            {
                bool privateLobby = raceHost.privateLobby != 0;
                if ( ImGui::Checkbox( "Private lobby", &privateLobby ) ) { raceHost.privateLobby = privateLobby ? 1 : 0; changed = 1; }
                Race_DrawFieldLabel( "Lobby password" );
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputText( "##lobby_password", raceHost.password, sizeof( raceHost.password ), ImGuiInputTextFlags_Password ) ) changed = 1;
            }

            ImGui::Spacing();
            if ( hostRunning ) {
                if ( Race_DrawPrimaryButton( "Stop Host", 1, 58.0f ) ) Race_RequestStopHost();
            } else {
                if ( Race_DrawPrimaryButton( "Start Host", hostSettingsOk, 58.0f ) ) Race_StartHost();
            }

            if ( gHostError[0] ) {
                Race_DrawNoticeBanner( gHostError, RACE_LOG_ERROR );
            } else if ( !hostRunning && !hostSettingsOk ) {
                Race_DrawNoticeBanner( hostReason, RACE_LOG_WARN );
            } else {
                Race_DrawNoticeBanner( hostRunning ? "Lobby is online and discoverable." : "Lobby is offline until you start the host.", RACE_LOG_INFO );
            }

            ImGui::EndTabItem();
        }

        if ( ImGui::BeginTabItem( "Race" ) ) {
            gActiveTab = RACE_TAB_RACE;
            Race_DrawSectionTitle( "Race Setup" );
            ImGui::BeginDisabled( !lobby );
            Race_DrawFieldLabel( "Run mode" );
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::Combo( "##run_mode", &raceHost.mode, modeLabels, 3 ) ) changed = 1;

            if ( raceHost.mode == 1 ) {
                const char *current = raceChapters[raceHost.mission - 1].displayName;
                Race_DrawFieldLabel( "Chapter" );
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::BeginCombo( "##chapter", current ) ) {
                    int i;
                    for ( i = 0; i < raceChapterCount; ++i ) {
                        int selected = ( raceHost.mission == raceChapters[i].chapter );
                        if ( ImGui::Selectable( raceChapters[i].displayName, selected ) ) {
                            raceHost.mission = raceChapters[i].chapter;
                            changed = 1;
                        }
                        if ( selected ) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            if ( raceHost.mode == 2 ) {
                Race_DrawFieldLabel( "IL map" );
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputText( "##il_map", gUiIlMap, sizeof( gUiIlMap ) ) ) changed = 1;
                Race_DrawFieldLabel( "Known maps" );
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::BeginCombo( "##known_maps", gUiIlMap ) ) {
                    int i;
                    for ( i = 0; i < raceILMapCount; ++i ) {
                        int selected = !_stricmp( gUiIlMap, raceILMaps[i] );
                        if ( ImGui::Selectable( raceILMaps[i], selected ) ) {
                            Race_Copy( gUiIlMap, sizeof( gUiIlMap ), raceILMaps[i] );
                            changed = 1;
                        }
                        if ( selected ) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            Race_DrawFieldLabel( "Difficulty" );
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::BeginCombo( "##difficulty", raceDifficulties[raceHost.difficulty - 1] ) ) {
                int i;
                for ( i = 0; i < 3; ++i ) {
                    int selected = ( raceHost.difficulty == i + 1 );
                    if ( ImGui::Selectable( raceDifficulties[i], selected ) ) {
                        raceHost.difficulty = i + 1;
                        changed = 1;
                    }
                    if ( selected ) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            Race_DrawFieldLabel( "Countdown (seconds)" );
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::InputInt( "##countdown", &gUiCountdownSec, 1, 5 ) ) changed = 1;
            if ( gUiCountdownSec < 1 ) gUiCountdownSec = 1;
            if ( gUiCountdownSec > 30 ) gUiCountdownSec = 30;
            ImGui::EndDisabled();

            ImGui::Separator();
            if ( Race_DrawPrimaryButton( "Start Race", raceSettingsOk, 60.0f ) ) {
                Race_OnSettingsChanged( 1 );
                Race_StartRace();
            }
            if ( !raceSettingsOk ) Race_DrawNoticeBanner( raceReason, RACE_LOG_WARN );
            ImGui::BeginDisabled( !hostRunning || raceHost.state == RACE_STATE_LOBBY );
            if ( ImGui::Button( "Stop Race", ImVec2( -1.0f, 52.0f ) ) ) Race_RequestStopRace();
            ImGui::EndDisabled();
            ImGui::BeginDisabled( !hostRunning || raceHost.state == RACE_STATE_RACING || raceHost.state == RACE_STATE_COUNTDOWN );
            if ( ImGui::Button( "Reset Lobby", ImVec2( -1.0f, 52.0f ) ) ) Race_RequestResetLobby();
            ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        if ( ImGui::BeginTabItem( "Rules" ) ) {
            gActiveTab = RACE_TAB_RULES;
            Race_DrawSectionTitle( "Rules" );
            ImGui::BeginDisabled( !lobby );
            {
                bool percent100 = raceHost.percent100 != 0;
                bool hl1 = raceHost.hl1Movement != 0;
                bool autoJump = raceHost.autoJump != 0;
                bool antiCheat = raceHost.antiCheat != 0;
                bool autoReady = raceHost.autoReadyCheck != 0;
                bool queueEnabled = raceHost.queueEnabled != 0;
                if ( ImGui::Checkbox( "100% category", &percent100 ) ) { raceHost.percent100 = percent100 ? 1 : 0; changed = 1; }
                if ( ImGui::Checkbox( "HL1 bhop physics", &hl1 ) ) { raceHost.hl1Movement = hl1 ? 1 : 0; changed = 1; }
                if ( ImGui::Checkbox( "Auto jump", &autoJump ) ) { raceHost.autoJump = autoJump ? 1 : 0; changed = 1; }
                if ( ImGui::Checkbox( "Anti-cheat", &antiCheat ) ) { raceHost.antiCheat = antiCheat ? 1 : 0; changed = 1; }
                if ( ImGui::Checkbox( "Auto-ready check before start", &autoReady ) ) { raceHost.autoReadyCheck = autoReady ? 1 : 0; changed = 1; }
                if ( ImGui::Checkbox( "Queue joins during active run", &queueEnabled ) ) { raceHost.queueEnabled = queueEnabled ? 1 : 0; changed = 1; }
                Race_DrawFieldLabel( "Pause alert seconds" );
                ImGui::SetNextItemWidth( -1.0f );
                { int pauseSec = raceHost.pauseAlertMs / 1000;
                  if ( ImGui::InputInt( "##pause_alert", &pauseSec, 5, 10 ) ) {
                      if ( pauseSec < 0 ) pauseSec = 0;
                      if ( pauseSec > 600 ) pauseSec = 600;
                      raceHost.pauseAlertMs = pauseSec * 1000;
                      changed = 1;
                  }
                }
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::TextDisabled( "Rules are broadcast to players while the lobby is online." );
            ImGui::TextDisabled( "Queue: %d waiting", Race_QueueCount() );
            if ( raceHost.privateLobby && !raceHost.password[0] ) {
                Race_DrawNoticeBanner( "Private lobby is enabled, but the password is empty.", RACE_LOG_WARN );
            }
            ImGui::EndTabItem();
        }
        if ( ImGui::BeginTabItem( "Runs" ) ) {
            gActiveTab = RACE_TAB_RUNS;
            Race_DrawSectionTitle( "Run History" );
            ImGui::TextDisabled( "%d saved run%s in memory.", raceHost.runCount, raceHost.runCount == 1 ? "" : "s" );
            Race_DrawFieldLabel( "Export path" );
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputText( "##runs_export_path", gRunsExportPath, sizeof( gRunsExportPath ) );
            if ( ImGui::Button( "Export CSV", ImVec2( -1.0f, 48.0f ) ) ) {
                Race_ExportRunsCsv( gRunsExportPath, gRunsExportStatus, sizeof( gRunsExportStatus ) );
            }
            if ( ImGui::Button( "Export JSON", ImVec2( -1.0f, 48.0f ) ) ) {
                char jsonPath[MAX_PATH];
                Race_Copy( jsonPath, sizeof( jsonPath ), gRunsExportPath[0] ? gRunsExportPath : "race_runs.json" );
                { char *dot = strrchr( jsonPath, '.' );
                  if ( dot ) Race_Copy( dot, sizeof( jsonPath ) - (size_t)( dot - jsonPath ), ".json" );
                  else strncat( jsonPath, ".json", sizeof( jsonPath ) - strlen( jsonPath ) - 1 );
                }
                Race_ExportRunsJson( jsonPath, gRunsExportStatus, sizeof( gRunsExportStatus ) );
            }
            if ( gRunsExportStatus[0] ) ImGui::TextDisabled( "%s", gRunsExportStatus );
            ImGui::EndTabItem();
        }
        if ( ImGui::BeginTabItem( "Config" ) ) {
            gActiveTab = RACE_TAB_CONFIG;
            if ( !gConfigEditorLoaded ) Race_LoadConfigEditor();
            Race_DrawSectionTitle( "Server Config" );
            ImGui::TextWrapped( "Config file:" );
            ImGui::PushFont( gImguiFontMono ? gImguiFontMono : gImguiFontUi );
            ImGui::TextWrapped( "%s", gConfigPath[0] ? gConfigPath : "serverconfig.cfg" );
            ImGui::PopFont();
            if ( ImGui::Button( "Reload File", ImVec2( -1.0f, 48.0f ) ) ) Race_LoadConfigEditor();
            if ( ImGui::Button( "Save & Apply Editor", ImVec2( -1.0f, 48.0f ) ) ) Race_SaveConfigEditor();
            if ( ImGui::Button( "Generate Current Settings", ImVec2( -1.0f, 48.0f ) ) ) {
                if ( Race_SaveServerConfig() ) Race_LoadConfigEditor();
            }
            if ( gConfigNotice[0] ) ImGui::TextDisabled( "%s", gConfigNotice );
            if ( gConfigEditorDirty ) ImGui::TextColored( Race_AccentColor(), "editor has unsaved changes" );
            ImGui::Separator();
            ImGui::TextDisabled( "The full editor opens in the main panel. Missing file is created with defaults at startup." );
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if ( changed ) Race_OnSettingsChanged( 1 );

    ImGui::EndChild();
}

static void Race_DrawConfigEditorPanel( float height ) {
    float editorHeight;
    if ( !gConfigEditorLoaded ) Race_LoadConfigEditor();
    ImGui::BeginChild( "config_editor_workspace", ImVec2( 0.0f, height ), true );
    Race_DrawSectionTitle( "Server Config Editor" );
    ImGui::SameLine();
    ImGui::TextDisabled( "%s", gConfigEditorDirty ? "modified" : "saved" );
    ImGui::TextDisabled( "%s", gConfigPath[0] ? gConfigPath : "serverconfig.cfg" );
    if ( ImGui::Button( "Reload", ImVec2( 110.0f, 42.0f ) ) ) Race_LoadConfigEditor();
    ImGui::SameLine();
    if ( ImGui::Button( "Save & Apply", ImVec2( 140.0f, 42.0f ) ) ) Race_SaveConfigEditor();
    ImGui::SameLine();
    if ( ImGui::Button( "Generate From UI", ImVec2( 160.0f, 42.0f ) ) ) {
        if ( Race_SaveServerConfig() ) Race_LoadConfigEditor();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Copy Path", ImVec2( 110.0f, 42.0f ) ) ) {
        ImGui::SetClipboardText( gConfigPath[0] ? gConfigPath : "serverconfig.cfg" );
    }
    if ( gConfigEditorStatus[0] ) ImGui::TextDisabled( "%s", gConfigEditorStatus );
    if ( gConfigNotice[0] ) ImGui::TextDisabled( "%s", gConfigNotice );
    editorHeight = ImGui::GetContentRegionAvail().y;
    if ( editorHeight < 180.0f ) editorHeight = 180.0f;
    ImGui::PushFont( gImguiFontMono ? gImguiFontMono : gImguiFontUi );
    if ( ImGui::InputTextMultiline( "##server_config_editor", gConfigEditor, sizeof( gConfigEditor ),
        ImVec2( -1.0f, editorHeight ), ImGuiInputTextFlags_AllowTabInput ) ) {
        gConfigEditorDirty = 1;
    }
    ImGui::PopFont();
    ImGui::EndChild();
}

static void Race_DrawRunsPanel( float height ) {
    int i, p;
    ImGui::BeginChild( "runs_workspace", ImVec2( 0.0f, height ), true );
    Race_DrawSectionTitle( "Run History" );
    ImGui::SameLine();
    ImGui::TextDisabled( "%d/%d", raceHost.runCount, RACE_RUN_HISTORY_MAX );
    ImGui::SetNextItemWidth( 420.0f );
    ImGui::InputText( "##runs_export_path_main", gRunsExportPath, sizeof( gRunsExportPath ) );
    ImGui::SameLine();
    if ( ImGui::Button( "CSV", ImVec2( 70.0f, 0.0f ) ) ) {
        Race_ExportRunsCsv( gRunsExportPath, gRunsExportStatus, sizeof( gRunsExportStatus ) );
    }
    ImGui::SameLine();
    if ( ImGui::Button( "JSON", ImVec2( 70.0f, 0.0f ) ) ) {
        char jsonPath[MAX_PATH];
        Race_Copy( jsonPath, sizeof( jsonPath ), gRunsExportPath[0] ? gRunsExportPath : "race_runs.json" );
        { char *dot = strrchr( jsonPath, '.' );
          if ( dot ) Race_Copy( dot, sizeof( jsonPath ) - (size_t)( dot - jsonPath ), ".json" );
          else strncat( jsonPath, ".json", sizeof( jsonPath ) - strlen( jsonPath ) - 1 );
        }
        Race_ExportRunsJson( jsonPath, gRunsExportStatus, sizeof( gRunsExportStatus ) );
    }
    if ( gRunsExportStatus[0] ) ImGui::TextDisabled( "%s", gRunsExportStatus );
    if ( ImGui::BeginTable( "runs_table", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2( 0.0f, -1.0f ) ) ) {
        ImGui::TableSetupColumn( "Run", ImGuiTableColumnFlags_WidthFixed, 60.0f );
        ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed, 74.0f );
        ImGui::TableSetupColumn( "Target", ImGuiTableColumnFlags_WidthFixed, 92.0f );
        ImGui::TableSetupColumn( "Player" );
        ImGui::TableSetupColumn( "RGT", ImGuiTableColumnFlags_WidthFixed, 82.0f );
        ImGui::TableSetupColumn( "IGT", ImGuiTableColumnFlags_WidthFixed, 82.0f );
        ImGui::TableSetupColumn( "Map", ImGuiTableColumnFlags_WidthFixed, 88.0f );
        ImGui::TableSetupColumn( "Flags", ImGuiTableColumnFlags_WidthFixed, 96.0f );
        ImGui::TableHeadersRow();
        for ( i = 0; i < RACE_RUN_HISTORY_MAX; ++i ) {
            raceRunHistory_t *run = &raceHost.runs[i];
            if ( !run->used ) continue;
            for ( p = 0; p < run->playerCount && p < RACE_MAX_PLAYERS; ++p ) {
                raceRunPlayerResult_t *r = &run->players[p];
                char rgt[24], igt[24], flags[96];
                if ( r->timeMs > 0 ) Race_FormatTime( r->timeMs, rgt, sizeof( rgt ) ); else Race_Copy( rgt, sizeof( rgt ), "-" );
                if ( r->igtMs > 0 ) Race_FormatTime( r->igtMs, igt, sizeof( igt ) ); else Race_Copy( igt, sizeof( igt ), "-" );
                snprintf( flags, sizeof( flags ), "%s%s%s%s",
                    r->finished ? "done" : "dnf",
                    r->cheatFlags ? " cheat" : "",
                    r->timedOut ? " timeout" : "",
                    r->left ? " left" : "" );
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text( "#%d", run->runId );
                ImGui::TableNextColumn(); ImGui::TextUnformatted( run->completed ? "complete" : "stopped" );
                ImGui::TableNextColumn(); ImGui::TextUnformatted( run->targetMap );
                ImGui::TableNextColumn();
                ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( r->color[0], r->color[1], r->color[2], 255 ) );
                ImGui::TextUnformatted( r->nick );
                ImGui::PopStyleColor();
                ImGui::TableNextColumn(); ImGui::TextUnformatted( rgt );
                ImGui::TableNextColumn(); ImGui::TextUnformatted( igt );
                ImGui::TableNextColumn(); ImGui::TextUnformatted( r->map );
                ImGui::TableNextColumn(); ImGui::TextUnformatted( flags );
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

static int Race_QueueEntryHasActivePlayer( const raceQueueEntry_t *q ) {
    int i;
    if ( !q || !q->used ) return 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( !p->used || p->left || p->timedOut || p->kicked ) continue;
        if ( p->address.sin_addr.s_addr == q->address.sin_addr.s_addr &&
             p->address.sin_port == q->address.sin_port ) return 1;
    }
    return 0;
}

static raceLogKind_t Race_PlayerRiskText( const racePlayer_t *p, DWORD now, char *out, size_t outSize ) {
    if ( !out || outSize == 0 ) return RACE_LOG_INFO;
    if ( !p || !p->used ) {
        Race_Copy( out, outSize, "-" );
        return RACE_LOG_INFO;
    }
    if ( p->kicked ) {
        Race_Copy( out, outSize, "kicked" );
        return RACE_LOG_WARN;
    }
    if ( p->timedOut ) {
        Race_Copy( out, outSize, "timeout" );
        return RACE_LOG_ERROR;
    }
    if ( p->left ) {
        Race_Copy( out, outSize, "left" );
        return RACE_LOG_WARN;
    }
    if ( p->cheatFlags ) {
        Race_Copy( out, outSize, "AC flag" );
        return RACE_LOG_ERROR;
    }
    if ( p->lastHeardMs && (DWORD)( now - p->lastHeardMs ) > 10000 ) {
        Race_Copy( out, outSize, "stale" );
        return RACE_LOG_WARN;
    }
    if ( p->paused || p->inMenu ) {
        Race_Copy( out, outSize, "pause" );
        return RACE_LOG_WARN;
    }
    Race_Copy( out, outSize, "ok" );
    return RACE_LOG_SUCCESS;
}

static void Race_DrawPlayersPanel( float height ) {
    int i;
    DWORD now = GetTickCount();
    float tableHeight = height - 78.0f;
    if ( tableHeight < 120.0f ) tableHeight = 120.0f;
    if ( !gLastPingRefreshMs || (DWORD)( now - gLastPingRefreshMs ) >= 750 ) {
        for ( i = 0; i < raceHost.maxPlayers; ++i ) {
            racePlayer_t *p = &raceHost.players[i];
            if ( p->used && p->lastHeardMs ) gDisplayPingMs[i] = (int)( now - p->lastHeardMs );
            else gDisplayPingMs[i] = 0;
        }
        gLastPingRefreshMs = now;
    }
    ImGui::BeginChild( "players", ImVec2( 0.0f, height ), true );
    Race_DrawSectionTitle( "Players" );
    ImGui::SameLine();
    if ( Race_HostIsRunning() ) ImGui::TextDisabled( "%d/%d connected, %d queued", Race_PlayerCount(), raceHost.maxPlayers, Race_QueueCount() );
    else ImGui::TextDisabled( "host offline" );
    if ( ImGui::BeginTable( "players_table", 8,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
        ImVec2( 0.0f, tableHeight ) ) ) {
        ImGui::TableSetupColumn( "Slot", ImGuiTableColumnFlags_WidthFixed, 48.0f );
        ImGui::TableSetupColumn( "Nick" );
        ImGui::TableSetupColumn( "State", ImGuiTableColumnFlags_WidthFixed, 92.0f );
        ImGui::TableSetupColumn( "Map", ImGuiTableColumnFlags_WidthFixed, 90.0f );
        ImGui::TableSetupColumn( "RGT", ImGuiTableColumnFlags_WidthFixed, 82.0f );
        ImGui::TableSetupColumn( "IGT", ImGuiTableColumnFlags_WidthFixed, 82.0f );
        ImGui::TableSetupColumn( "Ping", ImGuiTableColumnFlags_WidthFixed, 64.0f );
        ImGui::TableSetupColumn( "Guard", ImGuiTableColumnFlags_WidthFixed, 72.0f );
        ImGui::TableHeadersRow();
        for ( i = 0; i < raceHost.maxPlayers; ++i ) {
            racePlayer_t *p = &raceHost.players[i];
            char slotText[16];
            char timeText[24];
            char igtText[24];
            char pingText[24];
            char riskText[32];
            raceLogKind_t riskKind;
            if ( !p->used ) continue;
            if ( p->timeMs > 0 ) Race_FormatTime( p->timeMs, timeText, sizeof( timeText ) );
            else Race_Copy( timeText, sizeof( timeText ), "-" );
            if ( p->igtMs > 0 ) Race_FormatTime( p->igtMs, igtText, sizeof( igtText ) );
            else Race_Copy( igtText, sizeof( igtText ), "-" );
            snprintf( pingText, sizeof( pingText ), "%dms", gDisplayPingMs[i] );
            snprintf( slotText, sizeof( slotText ), "#%d", p->slot );
            riskKind = Race_PlayerRiskText( p, now, riskText, sizeof( riskText ) );

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if ( ImGui::Selectable( slotText, gUiSelectedSlot == p->slot, ImGuiSelectableFlags_SpanAllColumns ) ) {
                gUiSelectedSlot = p->slot;
            }
            ImGui::TableNextColumn();
            ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( p->color[0], p->color[1], p->color[2], 255 ) );
            ImGui::TextUnformatted( p->nick );
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( Race_PlayerStateText( p ) );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( p->map[0] ? p->map : "-" );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( timeText );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( igtText );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( pingText );
            ImGui::TableNextColumn();
            ImGui::PushStyleColor( ImGuiCol_Text, Race_LogColor( riskKind ) );
            ImGui::TextUnformatted( riskText );
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    if ( Race_QueueCount() > 0 ) {
        int shown = 0;
        ImGui::Spacing();
        Race_DrawSectionTitle( "Queue" );
        for ( i = 0; i < RACE_QUEUE_MAX; ++i ) {
            raceQueueEntry_t *q = &raceHost.queue[i];
            DWORD waitMs;
            if ( !q->used || Race_QueueEntryHasActivePlayer( q ) ) continue;
            waitMs = now - q->requestMs;
            shown++;
            ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( q->color[0], q->color[1], q->color[2], 255 ) );
            ImGui::Text( "#Q%d  %s", shown, q->nick[0] ? q->nick : "Runner" );
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled( "waiting %.0fs", waitMs / 1000.0f );
        }
    }
    ImGui::BeginDisabled( gUiSelectedSlot < 0 );
    if ( ImGui::Button( "Kick Selected", ImVec2( 170.0f, 44.0f ) ) ) Race_KickSelected();
    ImGui::EndDisabled();
    ImGui::EndChild();
}

static float Race_PlayerProgressPercent( const racePlayer_t *p ) {
    float score = 0.0f;
    float maxScore = 0.0f;
    if ( !p ) return 0.0f;
    if ( p->objectivesTotal > 0 ) {
        score += (float)p->objectivesFound;
        maxScore += (float)p->objectivesTotal;
    }
    if ( p->zoneTotal > 0 ) {
        score += (float)p->zoneProgress;
        maxScore += (float)p->zoneTotal;
    }
    if ( maxScore <= 0.0f ) return p->finished ? 100.0f : 0.0f;
    score = ( score / maxScore ) * 100.0f;
    if ( score < 0.0f ) score = 0.0f;
    if ( score > 100.0f ) score = 100.0f;
    return score;
}

static float Race_PlayerProgressScore( const racePlayer_t *p ) {
    if ( !p ) return 0.0f;
    return (float)p->stageProgress * 100.0f + Race_PlayerProgressPercent( p );
}

#include "race_host_ui_inspector.inl"

static void Race_ResetDeltaHistory( void ) {
    memset( gDeltaHistory, 0, sizeof( gDeltaHistory ) );
    memset( gDeltaValid, 0, sizeof( gDeltaValid ) );
    gDeltaHead = 0;
    gLastDeltaSampleMs = 0;
    gDeltaRunSession = 0;
    gDeltaRunStartMs = 0;
}

static void Race_SampleDeltaHistory( void ) {
    DWORD now = GetTickCount();
    float leaderScore = -1.0f;
    int i;
    if ( !Race_HostIsRunning() || !raceHost.raceStartMs ) {
        Race_ResetDeltaHistory();
        return;
    }
    if ( raceHost.state != RACE_STATE_RACING ) {
        if ( raceHost.state == RACE_STATE_FINISHED && gDeltaRunSession == raceHost.session && gDeltaRunStartMs == raceHost.raceStartMs ) return;
        Race_ResetDeltaHistory();
        return;
    }
    if ( gDeltaRunSession != raceHost.session || gDeltaRunStartMs != raceHost.raceStartMs ) {
        Race_ResetDeltaHistory();
        gDeltaRunSession = raceHost.session;
        gDeltaRunStartMs = raceHost.raceStartMs;
    }
    if ( gLastDeltaSampleMs && (DWORD)( now - gLastDeltaSampleMs ) < 1000 ) return;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( !p->used || p->left || p->timedOut || p->kicked ) continue;
        if ( Race_PlayerProgressScore( p ) > leaderScore ) leaderScore = Race_PlayerProgressScore( p );
    }
    gDeltaHead = ( gDeltaHead + 1 ) % RACE_DELTA_SAMPLES;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( leaderScore >= 0.0f && p->used && !p->left && !p->timedOut && !p->kicked ) {
            gDeltaHistory[i][gDeltaHead] = Race_PlayerProgressScore( p ) - leaderScore;
            gDeltaValid[i][gDeltaHead] = 1;
        } else {
            gDeltaHistory[i][gDeltaHead] = 0.0f;
            gDeltaValid[i][gDeltaHead] = 0;
        }
    }
    gLastDeltaSampleMs = now;
}

static void Race_DrawDeltaLegend( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        ImVec4 color;
        if ( !p->used || p->left || p->timedOut || p->kicked ) continue;
        color = Race_Rgba( p->color[0], p->color[1], p->color[2], 255 );
        ImGui::PushStyleColor( ImGuiCol_Text, color );
        ImGui::Text( "%s  %.0f pts  %s/%s",
            p->nick[0] ? p->nick : "Player",
            Race_PlayerProgressScore( p ),
            p->map[0] ? p->map : "-",
            p->stageName[0] ? p->stageName : "-" );
        ImGui::PopStyleColor();
    }
}

static void Race_DrawProgressPanel( float height ) {
    int i;
    int shown = 0;
    ImDrawList *drawList;
    ImVec2 canvasPos;
    ImVec2 canvasSize;
    ImVec2 legendSize;
    float minDelta = -50.0f;
    float maxDelta = 0.0f;
    float leftPad = 42.0f;
    float topPad = 10.0f;
    float bottomPad = 24.0f;
    int sample, slot;

    Race_SampleDeltaHistory();
    ImGui::BeginChild( "progress_chart", ImVec2( 0.0f, height ), true );
    Race_DrawSectionTitle( "Live Progress" );

    legendSize = ImVec2( 220.0f, ImGui::GetContentRegionAvail().y );
    canvasSize = ImVec2( ImGui::GetContentRegionAvail().x - legendSize.x - 12.0f, ImGui::GetContentRegionAvail().y );
    if ( canvasSize.x < 240.0f ) canvasSize.x = ImGui::GetContentRegionAvail().x;
    if ( canvasSize.y < 120.0f ) canvasSize.y = 120.0f;

    for ( slot = 0; slot < raceHost.maxPlayers; ++slot ) {
        for ( sample = 0; sample < RACE_DELTA_SAMPLES; ++sample ) {
            if ( gDeltaValid[slot][sample] && gDeltaHistory[slot][sample] < minDelta ) minDelta = gDeltaHistory[slot][sample];
        }
    }
    minDelta = floorf( minDelta / 25.0f ) * 25.0f;
    if ( minDelta > -25.0f ) minDelta = -25.0f;

    canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton( "delta_canvas", canvasSize );
    drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled( canvasPos, ImVec2( canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y ),
        ImGui::ColorConvertFloat4ToU32( Race_ThemeColor( Race_Rgba( 251, 252, 253, 255 ), Race_Rgba( 17, 20, 21, 255 ) ) ), 5.0f );
    drawList->AddRect( canvasPos, ImVec2( canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y ),
        ImGui::ColorConvertFloat4ToU32( Race_ThemeColor( Race_Rgba( 215, 221, 225, 255 ), Race_Rgba( 44, 52, 54, 255 ) ) ), 5.0f );

    for ( i = 0; i <= 4; ++i ) {
        float t = i / 4.0f;
        float y = canvasPos.y + topPad + ( canvasSize.y - topPad - bottomPad ) * t;
        float value = maxDelta + ( minDelta - maxDelta ) * t;
        char label[32];
        drawList->AddLine( ImVec2( canvasPos.x + leftPad, y ), ImVec2( canvasPos.x + canvasSize.x - 8.0f, y ),
            ImGui::ColorConvertFloat4ToU32( Race_ThemeColor( Race_Rgba( 229, 233, 236, 255 ), Race_Rgba( 34, 40, 42, 255 ) ) ) );
        snprintf( label, sizeof( label ), "%.0f", value );
        drawList->AddText( ImVec2( canvasPos.x + 8.0f, y - 8.0f ),
            ImGui::ColorConvertFloat4ToU32( Race_ThemeColor( Race_Rgba( 92, 100, 108, 255 ), Race_Rgba( 126, 136, 142, 255 ) ) ), label );
    }
    drawList->AddText( ImVec2( canvasPos.x + leftPad, canvasPos.y + canvasSize.y - 19.0f ),
        ImGui::ColorConvertFloat4ToU32( Race_ThemeColor( Race_Rgba( 92, 100, 108, 255 ), Race_Rgba( 126, 136, 142, 255 ) ) ),
        "last 120s, leader = 0, lower = behind" );

    for ( slot = 0; slot < raceHost.maxPlayers; ++slot ) {
        racePlayer_t *p = &raceHost.players[slot];
        ImVec2 prev;
        int havePrev = 0;
        ImVec4 color;
        if ( !p->used || p->left || p->timedOut || p->kicked ) continue;
        color = Race_Rgba( p->color[0], p->color[1], p->color[2], 255 );
        shown++;
        for ( sample = 0; sample < RACE_DELTA_SAMPLES; ++sample ) {
            int idx = ( gDeltaHead + 1 + sample ) % RACE_DELTA_SAMPLES;
            float x;
            float y;
            float norm;
            ImVec2 pt;
            if ( !gDeltaValid[slot][idx] ) {
                havePrev = 0;
                continue;
            }
            x = canvasPos.x + leftPad + ( canvasSize.x - leftPad - 10.0f ) * ( sample / (float)( RACE_DELTA_SAMPLES - 1 ) );
            norm = ( gDeltaHistory[slot][idx] - maxDelta ) / ( minDelta - maxDelta );
            if ( norm < 0.0f ) norm = 0.0f;
            if ( norm > 1.0f ) norm = 1.0f;
            y = canvasPos.y + topPad + ( canvasSize.y - topPad - bottomPad ) * norm;
            pt = ImVec2( x, y );
            if ( havePrev ) {
                drawList->AddLine( prev, pt, ImGui::ColorConvertFloat4ToU32( color ), 2.4f );
            }
            drawList->AddCircleFilled( pt, 2.2f, ImGui::ColorConvertFloat4ToU32( color ) );
            prev = pt;
            havePrev = 1;
        }
    }

    if ( canvasSize.x < ImGui::GetContentRegionAvail().x - 16.0f ) {
        ImGui::SameLine();
        ImGui::BeginChild( "delta_legend", legendSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        Race_DrawDeltaLegend();
        ImGui::EndChild();
    }
    if ( !shown ) {
        const char *emptyText;
        if ( !Race_HostIsRunning() ) emptyText = "Start the host to collect progress.";
        else if ( raceHost.state != RACE_STATE_RACING && raceHost.state != RACE_STATE_FINISHED ) emptyText = "Waiting for RUN START...";
        else if ( raceHost.state == RACE_STATE_FINISHED ) emptyText = "Run finished. Start another run to collect a new graph.";
        else emptyText = "Waiting for player telemetry...";
        ImGui::SetCursorScreenPos( ImVec2( canvasPos.x + leftPad, canvasPos.y + 42.0f ) );
        ImGui::TextDisabled( "%s", emptyText );
    }
    ImGui::EndChild();
}

static void Race_DrawLogPanel( float height ) {
    int i;
    float inputHeight = ImGui::GetFrameHeightWithSpacing() + 12.0f;
    ImGui::BeginChild( "activity", ImVec2( 0.0f, height ), true );
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::TextUnformatted( "Console" );
    ImGui::PopFont();
    ImGui::SameLine( ImGui::GetWindowWidth() - 238.0f );
    if ( ImGui::Button( "Copy Address", ImVec2( 116.0f, 0.0f ) ) ) {
        char addr[96];
        snprintf( addr, sizeof( addr ), "%s:%d", raceHost.bindAddress[0] ? raceHost.bindAddress : gUiBindAddress, raceHost.port ? raceHost.port : gUiPort );
        ImGui::SetClipboardText( addr );
        Race_GuiLogSrv( "copied address %s", addr );
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Clear", ImVec2( 82.0f, 0.0f ) ) ) {
        gUiLog[0] = '\0';
        gUiLogCount = 0;
    }
    ImGui::Spacing();
    ImGui::BeginChild( "log_scroll", ImVec2( 0.0f, -inputHeight ), false, ImGuiWindowFlags_HorizontalScrollbar );
    ImGui::PushFont( gImguiFontMono ? gImguiFontMono : gImguiFontUi );
    if ( gUiLogCount <= 0 ) {
        ImGui::TextDisabled( "%s", Race_HostIsRunning() ? "Waiting for players..." : "Start the host to open a lobby." );
    } else {
        for ( i = 0; i < gUiLogCount; ++i ) {
            ImGui::PushStyleColor( ImGuiCol_Text, Race_LogColor( gUiLogEntries[i].kind ) );
            ImGui::TextUnformatted( gUiLogEntries[i].text );
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopFont();
    if ( gUiScrollLog ) {
        ImGui::SetScrollHereY( 1.0f );
        gUiScrollLog = 0;
    }
    ImGui::EndChild();

    ImGui::PushFont( gImguiFontMono ? gImguiFontMono : gImguiFontUi );
    if ( ImGui::InputTextWithHint( "##console", "Console command (try help)", gUiCommand, sizeof( gUiCommand ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
        if ( gUiCommand[0] ) {
            Race_ProcessCmdLine( gUiCommand );
            gUiCommand[0] = '\0';
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
}

static void Race_DrawChatPanel( void ) {
    ImGui::BeginChild( "chat", ImVec2( 0.0f, 78.0f ), true );
    ImGui::PushItemWidth( -112.0f );
    if ( ImGui::InputTextWithHint( "##chat", "Chat message", gUiChat, sizeof( gUiChat ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
        Race_SaySomething();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if ( ImGui::Button( "Send", ImVec2( 102.0f, 0.0f ) ) ) Race_SaySomething();
    ImGui::EndChild();
}

static void Race_DrawMainUi( void ) {
    DWORD now = GetTickCount();
    int loaded = 0, total = 0;
    char timerText[64];
    char playersText[64];
    char readyText[64];
    char targetText[96];
    char sessionText[128];
    float rightHeight;
    float mainHeight;
    float playersHeight;
    float inspectorHeight;
    float progressHeight;
    float logHeight;
    const float chatHeight = 78.0f;
    const float topBarHeight = 54.0f;

    Race_LoadedCounts( &loaded, &total );
    if ( raceHost.state == RACE_STATE_OFFLINE ) {
        Race_Copy( timerText, sizeof( timerText ), "not bound" );
    } else if ( raceHost.state == RACE_STATE_COUNTDOWN ) {
        Race_FormatCountdown( Race_CountdownRemaining( now ), timerText, sizeof( timerText ) );
    } else if ( raceHost.state == RACE_STATE_RACING && raceHost.raceStartMs ) {
        Race_FormatTime( (int)( now - raceHost.raceStartMs ), timerText, sizeof( timerText ) );
    } else if ( raceHost.state == RACE_STATE_LOADING ) {
        snprintf( timerText, sizeof( timerText ), "%d/%d loaded", loaded, total );
    } else {
        Race_Copy( timerText, sizeof( timerText ), "--:--.--" );
    }
    if ( Race_HostIsRunning() ) {
        int queueCount = Race_QueueCount();
        if ( queueCount > 0 ) snprintf( playersText, sizeof( playersText ), "%d/%d +%d queue", Race_PlayerCount(), raceHost.maxPlayers, queueCount );
        else snprintf( playersText, sizeof( playersText ), "%d/%d", Race_PlayerCount(), raceHost.maxPlayers );
    } else {
        Race_Copy( playersText, sizeof( playersText ), "offline" );
    }
    snprintf( readyText, sizeof( readyText ), "%d/%d ready", loaded, total );
    snprintf( targetText, sizeof( targetText ), "%s", Race_HostIsRunning() ? Race_ResolveStartMap() : "no lobby" );
    snprintf( sessionText, sizeof( sessionText ), "%s  |  %s:%d",
        Race_HostIsRunning() ? "Online" : "Offline",
        raceHost.bindAddress[0] ? raceHost.bindAddress : gUiBindAddress,
        raceHost.port );

    ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ) );
    ImGui::SetNextWindowSize( ImGui::GetIO().DisplaySize );
    ImGui::Begin( "RaceHostRoot", NULL,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

    ImGui::PushStyleColor( ImGuiCol_ChildBg, Race_ThemeColor( Race_Rgba( 255, 255, 255, 255 ), Race_Rgba( 31, 36, 37, 255 ) ) );
    ImGui::BeginChild( "topbar", ImVec2( 0.0f, topBarHeight ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGui::Button( "Menu", ImVec2( 70.0f, 34.0f ) );
    ImGui::SameLine();
    Race_DrawStatusDot( Race_StateColor( raceHost.state ) );
    ImGui::SameLine();
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::TextUnformatted( "RtCW Race Host" );
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled( "Lobby / %s / %s", Race_StateName( raceHost.state ), targetText );
    ImGui::SameLine( ImGui::GetWindowWidth() - 150.0f );
    Race_DrawThemeToggle();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    mainHeight = ImGui::GetContentRegionAvail().y - 26.0f;
    if ( mainHeight < 400.0f ) mainHeight = 400.0f;
    Race_DrawSettingsPanel( mainHeight );
    ImGui::SameLine();

    ImGui::BeginChild( "workspace", ImVec2( 0.0f, mainHeight ), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGui::BeginChild( "status_strip", ImVec2( 0.0f, 86.0f ), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    {
        float gap = ImGui::GetStyle().ItemSpacing.x;
        float tileW = ( ImGui::GetContentRegionAvail().x - gap * 3.0f ) * 0.25f;
        if ( tileW < 130.0f ) tileW = 130.0f;
        Race_DrawStatusTile( "status_state", "State", Race_StateName( raceHost.state ), Race_StateColor( raceHost.state ), Race_HostIsRunning() ? "host online" : "host offline", tileW );
        ImGui::SameLine();
        Race_DrawStatusTile( "status_timer", "Timer", timerText, Race_AccentColor(), raceHost.state == RACE_STATE_LOADING ? readyText : "", tileW );
        ImGui::SameLine();
        Race_DrawStatusTile( "status_players", "Players", playersText, Race_ThemeColor( Race_Rgba( 0, 108, 190, 255 ), Race_Rgba( 110, 190, 255, 255 ) ), Race_QueueCount() > 0 ? "queue active" : "", tileW );
        ImGui::SameLine();
        Race_DrawStatusTile( "status_bind", "Bind", sessionText, Race_ThemeColor( Race_Rgba( 92, 45, 145, 255 ), Race_Rgba( 207, 176, 255, 255 ) ), targetText, tileW );
    }
    ImGui::EndChild();

    rightHeight = ImGui::GetContentRegionAvail().y;
    if ( gActiveTab == RACE_TAB_CONFIG ) {
        Race_DrawConfigEditorPanel( rightHeight );
    } else if ( gActiveTab == RACE_TAB_RUNS ) {
        Race_DrawRunsPanel( rightHeight );
    } else {
        float spacing = ImGui::GetStyle().ItemSpacing.y;
        float stackHeight = rightHeight - chatHeight - spacing * 4.0f;
        if ( stackHeight < 260.0f ) stackHeight = 260.0f;
        playersHeight = stackHeight * 0.28f;
        inspectorHeight = stackHeight * 0.24f;
        progressHeight = stackHeight * 0.24f;
        logHeight = stackHeight - playersHeight - inspectorHeight - progressHeight;
        if ( playersHeight < 120.0f ) playersHeight = 120.0f;
        if ( inspectorHeight < 120.0f ) inspectorHeight = 120.0f;
        if ( progressHeight < 130.0f ) progressHeight = 130.0f;
        logHeight = rightHeight - playersHeight - inspectorHeight - progressHeight - chatHeight - spacing * 4.0f;
        if ( logHeight < 105.0f ) {
            float squeeze = 105.0f - logHeight;
            float reducePlayers = squeeze * 0.35f;
            float reduceInspector = squeeze * 0.30f;
            float reduceProgress = squeeze - reducePlayers - reduceInspector;
            if ( playersHeight - reducePlayers >= 96.0f ) playersHeight -= reducePlayers;
            if ( inspectorHeight - reduceInspector >= 96.0f ) inspectorHeight -= reduceInspector;
            if ( progressHeight - reduceProgress >= 106.0f ) progressHeight -= reduceProgress;
            logHeight = rightHeight - playersHeight - inspectorHeight - progressHeight - chatHeight - spacing * 4.0f;
            if ( logHeight < 84.0f ) logHeight = 84.0f;
        }
        Race_DrawPlayersPanel( playersHeight );
        Race_DrawInspectorPanel( inspectorHeight );
        Race_DrawProgressPanel( progressHeight );
        Race_DrawLogPanel( logHeight );
        Race_DrawChatPanel();
    }
    ImGui::EndChild();

    ImGui::TextDisabled( "%s  |  %s  |  %s  |  100%%=%d  HL1=%d  autoJump=%d  AC=%d",
        Race_HostIsRunning() ? readyText : "host offline",
        raceHost.bindAddress[0] ? raceHost.bindAddress : gUiBindAddress,
        raceHost.mode == 0 ? "Full Game" : raceHost.mode == 1 ? "Chapter" : "IL",
        raceHost.percent100, raceHost.hl1Movement, raceHost.autoJump, raceHost.antiCheat );

    Race_DrawConfirmModal();
    ImGui::End();
}

void Race_RenderFrame( void ) {
    RECT rc;
    LARGE_INTEGER nowCounter;
    float dt;
    ImVec4 clearColor;

    QueryPerformanceCounter( &nowCounter );
    dt = (float)( (double)( nowCounter.QuadPart - gLastFrameCounter.QuadPart ) / (double)gPerfFrequency.QuadPart );
    if ( dt <= 0.0f ) dt = 1.0f / 60.0f;
    gLastFrameCounter = nowCounter;
    if ( gThemeBlend < gThemeTarget ) {
        gThemeBlend += dt * 5.0f;
        if ( gThemeBlend > gThemeTarget ) gThemeBlend = gThemeTarget;
    } else if ( gThemeBlend > gThemeTarget ) {
        gThemeBlend -= dt * 5.0f;
        if ( gThemeBlend < gThemeTarget ) gThemeBlend = gThemeTarget;
    }
    gUiAnimTime += dt;
    Race_ApplyImGuiStyle();
    Race_UpdateWindowChrome();

    GetClientRect( gMainWnd, &rc );
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2( (float)( rc.right - rc.left ), (float)( rc.bottom - rc.top ) );
    io.DeltaTime = dt;

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();
    Race_DrawMainUi();
    ImGui::Render();

    clearColor = Race_ThemeColor( Race_Rgba( 243, 243, 243, 255 ), Race_Rgba( 30, 30, 30, 255 ) );
    glViewport( 0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y );
    glClearColor( clearColor.x, clearColor.y, clearColor.z, clearColor.w );
    glClear( GL_COLOR_BUFFER_BIT );
    ImGui_ImplOpenGL2_RenderDrawData( ImGui::GetDrawData() );
    SwapBuffers( gGlDc );
}

void Race_BuildGui( void ) {
    Race_InitUiState();
    Race_GuiRefresh();
}
