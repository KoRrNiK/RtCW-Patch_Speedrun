/*
===========================================================================
cl_livesplit.c  -  Engine-level LiveSplit speedrun timer for RtCW SP

Runs inside the client engine (WolfSP.exe), NOT in the cgame DLL.
Persists across map loads, cgame restarts, menus, loading screens.

Features
--------
- Three run modes: Full Game / Mission / Individual Level
- Per-difficulty (1-3) golds, PB segments, and statistics
- Endgame cinematic trigger finishes the run on the "end" map
- Run history logging for HTML export

Cvars:
  cg_livesplit  1          Toggle the HUD panel
  ls_mode       0|1|2      0=Full Game  1=Mission  2=Individual Level
  ls_100pct     0          100% category - display secrets/treasures
  ls_mission    1-5         Which mission group (mode 1)
  ls_map        ""          BSP name override for IL (e.g. "tram", "rocket")
  ls_x          8           Panel X position (virtual 640x480)
  ls_y          80          Panel Y position
  ls_w          178         Panel width
  ls_scale      1.0         Text/row scale multiplier
  ls_opacity    1.0         Panel opacity (0.0-1.0)
  ls_opacity_ui 0.2         Opacity multiplier when console/UI is open (0.0-1.0)

Color customization (format: "R G B A", RGB 0-255, Alpha 0.0-1.0, empty = default):
  ls_clr_ahead     ""   Ahead of PB color       (default: 64 217 64 1.0)
  ls_clr_behind    ""   Behind PB color          (default: 217 64 64 1.0)
  ls_clr_gold      ""   Gold/best split color    (default: 255 217 51 1.0)
  ls_clr_header    ""   Header/active dot color  (default: 89 191 51 1.0)
  ls_clr_timer     ""   Main timer text color    (default: 217 242 204 1.0)
  ls_clr_text      ""   Normal text color        (default: 217 224 217 0.9)
  ls_clr_bg        ""   Panel background color   (default: 10 10 15 0.82)
  ls_clr_border    ""   Panel border color       (default: 51 89 38 0.08)
  ls_clr_mapname   ""   Map name text color      (default: 140 158 128 0.85)
  ls_clr_current   ""   Current map name color   (default: 255 255 153 1.0)
  ls_clr_completed ""   Completed map color      (default: 184 184 184 0.8)
  ls_clr_future    ""   Future map color         (default: 92 92 102 0.48)
  ls_clr_dim       ""   Dim/secondary text color (default: 122 122 128 0.52)
  ls_clr_segtimer  ""   Segment timer color      (default: 158 166 158 0.82)
  ls_clr_paused    ""   Paused indicator color   (default: 230 179 51 1.0)
  ls_clr_sep       ""   Separator line color     (default: 56 97 31 0.18)
  ls_clr_highlight ""   Current row highlight    (default: 26 51 15 0.32)
  ls_clr_label     ""   Label text (PB, Best)    (default: 107 122 97 0.62)

Commands:
  livesplit_start           Start/arm the timer (works anytime, even mid-map)
  livesplit_reset           Reset current run (saves golds)
  livesplit_reset_nosave    Reset current run (does NOT save golds)
  livesplit_reset_bests     Reset ALL stats and bests
  livesplit_reset_category  Reset bests for current category + difficulty only
  livesplit_check           Check all gameplay settings against defaults
===========================================================================
*/

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "client.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

/* =====================================================================
   ASL (LiveSplit Auto Splitter) shared state
   Global (non-static) so LiveSplit can find it via signature scan.
   ===================================================================== */
#define LS_ASL_MAGIC  0x52574C53   /* 'RWLS' */
#define LS_ASL_VER    2

typedef struct {
	unsigned int magic;          /*  0: 0x52574C53 signature            */
	int          version;        /*  4: struct layout version           */
	int          active;         /*  8: 1 = timer running               */
	int          finished;       /* 12: 1 = run finished                */
	int          totalIGTMs;     /* 16: cumulative IGT (milliseconds)   */
	int          splitsDoneCount;/* 20: # of completed non-cutscene splits */
	int          splitCount;     /* 24: total non-cutscene splits expected */
	int          runMode;        /* 28: 0=FG 1=Mission 2=IL             */
	int          currentMapIndex;/* 32: current map index in table      */
	int          loading;        /* 36: 1 = in loading screen           */
	int          runStartCount;  /* 40: increments on every new run     */
	int          runMission;     /* 44: mission group 1-5               */
	int          splitSeqNum;    /* 48: +1 per LS_CompleteSplit call only*/
	char         currentMapName[32]; /* 52: current map e.g. "escape1"  */
} lsASLState_t;

lsASLState_t ls_aslState = { LS_ASL_MAGIC, LS_ASL_VER };

/* =====================================================================
   Map definitions - full RtCW SP campaign
   ===================================================================== */
#define LS_MAX_MAPS         40
#define LS_MAX_MAPNAME      64
#define LS_MAX_DIFFICULTIES 3
#define LS_NUM_MODES 3  /* fullgame=0, mission=1, IL=2 */
#define LS_TOTAL_DIFF_SLOTS (LS_NUM_MODES * LS_MAX_DIFFICULTIES)  /* 9 */
#define LS_SAVE_FILE        "livesplit_stats.dat"
#define LS_CS_MISSIONSTATS  23   /* configstring index for mission stats */
#define LS_HISTORY_FILE     "livesplit_history.dat"
#define LS_MAX_VIS_ROWS     6

/* run modes */
#define LS_MODE_FULLGAME    0
#define LS_MODE_MISSION     1
#define LS_MODE_IL          2

typedef struct {
	const char *name;
	const char *displayName;
	const char *shortName;
	int         mission;
	qboolean    cutscene;
	int         secrets;     /* total secrets on this map */
	int         treasure;    /* total treasure items on this map */
} lsMapDef_t;

static const lsMapDef_t ls_mapDefs[] = {
	/*                name           displayName                       shortName     mis  cut   S   T  */
	/* Mission 1 */
	{ "cutscene1",   NULL,                              NULL,       1, qtrue,   0,  0 },
	{ "escape1",     "Escape!",                         "Escape!",  1, qfalse,  7,  9 },
	{ "escape2",     "Castle Keep",                     "Keep",     1, qfalse,  4,  7 },
	{ "tram",        "Tram Ride",                       "Tram",     1, qfalse,  0,  0 },
	/* Mission 2 */
	{ "village1",    "Village",                         "Village",  2, qfalse,  2,  3 },
	{ "crypt1",      "Catacombs",                       "Catacombs",2, qfalse,  1,  1 },
	{ "crypt2",      "Crypt",                           "Crypt",    2, qfalse,  2,  2 },
	{ "church",      "Church",                          "Church",   2, qfalse,  1,  1 },
	{ "boss1",       "Tomb",                            "Tomb",     2, qfalse,  0,  0 },
	/* Mission 3 */
	{ "cutscene6",   NULL,                              NULL,       3, qtrue,   0,  0 },
	{ "forest",      "Forest Compound",                 "Forest",   3, qfalse,  1,  1 },
	{ "rocket",      "Rocket Base",                     "Rocket",   3, qfalse,  1,  2 },
	{ "baseout",     "Radar Installation",              "Radar",    3, qfalse,  1,  2 },
	{ "assault",     "Air Base Assault",                "Assault",  3, qfalse,  0,  0 },
	/* Mission 4 */
	{ "cutscene9",   NULL,                              NULL,       4, qtrue,   0,  0 },
	{ "sfm",         "Kugelstadt",                      "Kugelstadt",4, qfalse, 1,  0 },
	{ "factory",     "The Bombed Factory",              "Factory",  4, qfalse,  0,  0 },
	{ "trainyard",  "The Trainyards",                  "Trainyards",4, qfalse,  2,  2 },
	{ "swf",         "Secret Weapons Facility",         "SWF",      4, qfalse,  4,  0 },
	/* Mission 5 */
	{ "cutscene11",  NULL,                              NULL,       5, qtrue,   0,  0 },
	{ "norway",      "Ice Station Norway",              "Norway",   5, qfalse,  1,  0 },
	{ "xlabs",       "X-Labs",                          "X-Labs",   5, qfalse,  1,  0 },
	{ "boss2",       "Super Soldier Chambers",          "SSC",      5, qfalse,  0,  0 },
	/* Mission 6 */
	{ "cutscene14",  NULL,                              NULL,       6, qtrue,   0,  0 },
	{ "dam",         "Bramburg Dam",                    "Dam",      6, qfalse,  1,  0 },
	{ "village2",    "Paderborn Village",               "Paderborn",6, qfalse,  5, 12 },
	{ "chateau",     "Chateau Schufstaffel",            "Chateau",  6, qfalse,  2,  7 },
	{ "dark",        "Unhallowed Ground",               "Unhallowed",6, qfalse, 1,  9 },
	/* Mission 7 */
	{ "dig",         "The Dig",                         "Dig",      7, qfalse,  2,  3 },
	{ "castle",      "Return to Castle Wolfenstein",    "RTCW",     7, qfalse,  2,  6 },
	{ "end",         "Heinrich",                        "Heinrich", 7, qfalse,  0,  0 },
	{ "cutscene19",  NULL,                              NULL,       7, qtrue,   0,  0 },
	{ NULL,          NULL,                              NULL,       0, qfalse,  0,  0 }
};

/* Mission group mapping: groups combine adjacent original missions */
#define LS_NUM_MISSION_GROUPS 5
typedef struct {
	const char *name;
	int         firstMission;  /* first original mission number in group */
	int         lastMission;   /* last original mission number in group */
} lsMissionGroup_t;

static const lsMissionGroup_t ls_missionGroups[LS_NUM_MISSION_GROUPS] = {
	{ "Ominous Rumors, Dark Secret",            1, 2 },  /* Group 1: Mission 1+2 */
	{ "Weapons of Vengeance",                   3, 3 },  /* Group 2: Mission 3   */
	{ "Deadly Designs",                         4, 4 },  /* Group 3: Mission 4   */
	{ "Deathshead's Playground",                5, 5 },  /* Group 4: Mission 5   */
	{ "Re. Engagement, Op. Resurrection",       6, 7 },  /* Group 5: Mission 6+7 */
};

/* =====================================================================
   Data structures
   ===================================================================== */

/* Per-difficulty persistent data per split */
typedef struct {
	int bestTimeMs;
	int pbSegmentMs;
	int totalAttempts;
	int totalCompletions;
} lsDiffSplitData_t;

typedef struct {
	char                mapname[LS_MAX_MAPNAME];
	int                 mission;
	qboolean            cutscene;
	const char         *displayName;  /* human-readable full name */
	const char         *shortName;    /* abbreviated name for narrow UI */

	/* per-difficulty-per-mode persistent: [mode*3 + skill]  0-8 */
	lsDiffSplitData_t   d[LS_TOTAL_DIFF_SLOTS];

	/* transient current-run data */
	qboolean            splitDone;
	qboolean            splitSkipped;   /* qtrue when split was skipped (show --- ) */
	int                 currentTimeMs;
	int                 prevGoldMs;
	int                 goldFlashMs;    /* Sys_Milliseconds() when a new gold was set (0 = none) */

	/* 100% category: per-split secrets/treasures snapshot */
	int                 secretsFound;
	int                 secretsTotal;
	int                 treasureFound;
	int                 treasureTotal;
} lsSplit_t;

/* Run history entry */
#define LS_MAX_HISTORY_RUNS 500

typedef struct {
	int difficulty;                  /* 1-3 */
	int mode;                        /* 0/1/2 */
	int missionNum;                  /* for mission mode */
	int totalIGTMs;
	int totalRGTMs;
	int numSplits;
	int splitTimes[LS_MAX_MAPS];
} lsRunHistory_t;

typedef struct {
	qboolean    initialized;
	qboolean    active;
	qboolean    runFinished;

	/* IGT */
	int         runTotalIGTMs;
	int         lastServerTime;
	int         lastRealTimeMs;      /* Sys_Milliseconds() of previous frame, for unclamped delta */

	/* RGT */
	int         runStartRealMs;
	int         runSavedRealMs;

	/* current run settings */
	int         currentDifficulty;   /* 1-3, detected from g_gameskill */
	int         runMode;             /* LS_MODE_* */
	int         runMission;          /* 1-5, mission group for mission mode */

	int         currentMapIndex;     /* tracked split in splits[] */
	lsSplit_t   splits[LS_MAX_MAPS];
	int         numMaps;             /* always full campaign count */

	/* visible (non-cutscene) rows - rebuilt per mode */
	int         visMap[LS_MAX_MAPS];
	int         numVisible;
	int         curVisRow;

	/* mode-dependent first/last indices for the active set */
	int         modeFirstIdx;
	int         modeLastIdx;
	int         modeEndMapIdx;       /* the "end map" for this mode's run */

	char        prevMapname[LS_MAX_MAPNAME];
	char        actualMapname[LS_MAX_MAPNAME];

	/* per-category per-difficulty lifetime stats */
	/* fullgame */
	int         fgAttempts[LS_MAX_DIFFICULTIES];
	int         fgCompletions[LS_MAX_DIFFICULTIES];
	int         fgPB[LS_MAX_DIFFICULTIES];
	/* mission: [group 0-4][difficulty 0-2] */
	int         msAttempts[LS_NUM_MISSION_GROUPS][LS_MAX_DIFFICULTIES];
	int         msCompletions[LS_NUM_MISSION_GROUPS][LS_MAX_DIFFICULTIES];
	int         msPB[LS_NUM_MISSION_GROUPS][LS_MAX_DIFFICULTIES];
	/* IL: uses per-split d[di].totalAttempts/totalCompletions/bestTimeMs */

	/* run history */
	lsRunHistory_t history[LS_MAX_HISTORY_RUNS];
	int            numHistoryRuns;

	/* Heinrich end trigger tracking */
	qboolean    heinrichDead;

	/* Ending cutscene detection for Mission/IL modes:
	   Armed once cg_letterbox has been 0 on the end map (gameplay started).
	   endCutsceneSnapMs: snapshot of currentTimeMs taken on each cg_letterbox
	   0>1 transition; CLEARED on every 1>0 transition (cutscene ended without
	   changelevel > was an intro or mid-level cutscene, not the ending).
	   This ensures the snapshot always reflects the START of the LAST
	   (ending) cutscene when g_reloading or ls_changelevel finally fires.
	   When the 0>1 transition occurs after arming, the run is finished
	   immediately at the snapshot time - no delay, purely automatic. */
	qboolean    endCutsceneArmed;
	int         endCutsceneSnapMs;    /* -1 = not set */
	qboolean    endCutscenePrevLB;    /* previous frame cg_letterbox state */

	/* Manual pause (livesplit_pause command) */
	qboolean    manualPause;
	int         pauseSavedRealMs;   /* accumulated RGT at moment of pause */

	/* Anti-cheat counters (lifetime) */
	int         totalPauses;
	int         totalUndos;
	int         totalSkips;

	/* sv_cheats detection (per-run) */
	qboolean    cheatsUsed;

	/* Settings validation (per-run) */
	qboolean    settingsModified;
	int         settingsModCount;

	/* Pre-run PB snapshot (for nosave restore) */
	int         savedPBMs;
	int         savedRgtPBMs;

	/* Per-category PB realtime */
	int         fgPBRgt[LS_MAX_DIFFICULTIES];
	int         msPBRgt[LS_NUM_MISSION_GROUPS][LS_MAX_DIFFICULTIES];

	/* Map-load freeze: set on real map transition (changelevel), cleared
	   when the player clicks 'continue' on the pregame screen (ls_loading
	   cvar goes to 0).  Prevents IGT ticking during new map loading.
	   mapLoadFreezeStartMs is used as a failsafe timeout: if the briefing
	   screen never appears (cutscene maps), the freeze auto-clears. */
	qboolean    mapLoadFreeze;
	int         mapLoadFreezeStartMs;

	/* Deferred Full Game start: the run is NOT activated during map-change
	   detection for cutscene1.  Instead we wait until cls.state >= CA_ACTIVE
	   (the cutscene is actually playing) to start the timer, so loading
	   screen time is never counted. */
	qboolean    fgPendingStart;

	/* 100% category: live stats from CS_MISSIONSTATS for current map */
	int         liveSecretsFound;
	int         liveSecretsTotal;
	int         liveTreasureFound;
	int         liveTreasureTotal;

	/* Previous-frame live stats (used to get correct per-stage values
	   during map transitions when the configstring already reflects
	   the NEW map's totals). */
	int         prevLiveSecretsFound;
	int         prevLiveSecretsTotal;
	int         prevLiveTreasureFound;
	int         prevLiveTreasureTotal;

	/* 100% category: cumulative base at start of current map.
	   player->numSecretsFound is cumulative across all maps (never
	   reset by the game), so we subtract the base to get per-map
	   found counts: perMapFound = liveSecretsFound - baseSecretsFound. */
	int         baseSecretsFound;
	int         baseTreasureFound;
} lsState_t;

static lsState_t ls;
static cvar_t *cg_livesplit   = NULL;
static cvar_t *ls_modeCvar    = NULL;
static cvar_t *ls_missionCvar = NULL;
static cvar_t *ls_mapCvar     = NULL;
static cvar_t *ls_xCvar       = NULL;
static cvar_t *ls_yCvar       = NULL;
static cvar_t *ls_wCvar       = NULL;
static cvar_t *ls_scaleCvar   = NULL;
static cvar_t *ls_opacityCvar = NULL;
static cvar_t *ls_opacityUiCvar = NULL;
static cvar_t *ls_bgalphaCvar = NULL;
static cvar_t *ls_alignCvar   = NULL;   /* 0=left, 1=right - column side for time/delta */
static cvar_t *ls_showheaderCvar = NULL; /* show column header labels */
static cvar_t *ls_showstatsCvar  = NULL; /* show stat rows at bottom */
static cvar_t *ls_showsegCvar    = NULL; /* show segment timer */
static cvar_t *ls_showrgtCvar    = NULL; /* show RGT at bottom */
static cvar_t *ls_maxrowsCvar    = NULL; /* max visible split rows (0=auto ~6) */
static cvar_t *ls_showpbCvar     = NULL; /* show PB label next to timer */
static cvar_t *ls_showbestCvar   = NULL; /* show Best label next to timer/seg */
static cvar_t *ls_showtimerCvar  = NULL; /* show the main big timer */
static cvar_t *ls_showsepsCvar   = NULL; /* show separator lines */
static cvar_t *ls_showdeltasCvar = NULL; /* show PB delta column (+/-)  */
static cvar_t *ls_showbestdeltasCvar = NULL; /* show best/gold delta column (Best +/-) */
static cvar_t *ls_showattCvar    = NULL; /* show attempt counter in header */
static cvar_t *ls_drawCvar       = NULL; /* show/hide LiveSplit panel (timers still run) */
static cvar_t *ls_100pctCvar     = NULL; /* 100% category: display secrets & treasures */

/* Color customization cvars (format: "R G B A", RGB 0-255, Alpha 0.0-1.0) */
static cvar_t *ls_clr_aheadCvar   = NULL;  /* ahead of PB (green) */
static cvar_t *ls_clr_behindCvar  = NULL;  /* behind PB (red) */
static cvar_t *ls_clr_goldCvar    = NULL;  /* gold/best split */
static cvar_t *ls_clr_headerCvar  = NULL;  /* header dot / active color */
static cvar_t *ls_clr_timerCvar   = NULL;  /* main timer text */
static cvar_t *ls_clr_textCvar    = NULL;  /* normal text (map names, times) */
static cvar_t *ls_clr_bgCvar      = NULL;  /* panel background */
static cvar_t *ls_clr_borderCvar  = NULL;  /* panel border */
static cvar_t *ls_clr_mapnameCvar = NULL;  /* map name text */
static cvar_t *ls_clr_currentCvar = NULL;  /* current map highlight */
static cvar_t *ls_clr_completedCvar = NULL; /* completed maps */
static cvar_t *ls_clr_futureCvar  = NULL;  /* future maps */
static cvar_t *ls_clr_dimCvar     = NULL;  /* dim/secondary text */
static cvar_t *ls_clr_segtimerCvar = NULL; /* segment timer */
static cvar_t *ls_clr_pausedCvar  = NULL;  /* paused indicator */
static cvar_t *ls_clr_sepCvar     = NULL;  /* separator lines */
static cvar_t *ls_clr_highlightCvar = NULL; /* current row highlight bg */
static cvar_t *ls_clr_labelCvar   = NULL;  /* label text (PB, Best, etc.) */

static cvar_t *ls_text_shadowCvar = NULL;  /* text drop-shadow toggle */

/* Standalone IGT timer overlay cvars */
static cvar_t *ls_igttimerCvar   = NULL; /* 0=off, 1=on */
static cvar_t *ls_igttimer_xCvar = NULL;
static cvar_t *ls_igttimer_yCvar = NULL;
static cvar_t *ls_igttimer_scaleCvar = NULL;

/* Standalone segment timer overlay (below IGT timer) */
static cvar_t *ls_igtsegtimer = NULL; /* 0=off, 1=on */

/* Reset confirmation state */
static int ls_resetPending = 0; /* 0=none, 1=waiting for user confirm */

/* ls_type cvar: 0=in-game only, 1=external LiveSplit only, 2=both */
static cvar_t *ls_typeCvar = NULL;
#define LS_TYPE_INGAME   0
#define LS_TYPE_EXTERNAL 1
#define LS_TYPE_BOTH     2

/* Keystroke overlay cvars (engine-side, read by cgame) */
static cvar_t *ks_xCvar       = NULL;
static cvar_t *ks_yCvar       = NULL;
static cvar_t *ks_scaleCvar   = NULL;
static cvar_t *ks_opacityCvar = NULL;
static cvar_t *ks_mouseCvar   = NULL;  /* 0=off, 1=clicks only, 2=clicks+direction */

/* Demo auto-record cvar */
static cvar_t *sp_autorecordCvar = NULL;
static cvar_t *sp_demofpsCvar    = NULL;  /* target sv_fps during SP demo recording */
static qboolean ls_autoRecordActive = qfalse; /* qtrue while we auto-started a demo */

/* =====================================================================
   External LiveSplit Server TCP client
   Connects to LiveSplit Server component (livesplit.org) on 127.0.0.1:16834
   Protocol: newline-terminated text commands over TCP
   ===================================================================== */

/* Forward declarations - defined later in file */
static int LS_CumulativeTime( int upToIdx );
static void LS_CompleteSplit( int idx, int nowReal );
static void LS_DoResetSave( void );

#ifdef _WIN32

#define LSEXT_PORT        16834
#define LSEXT_RETRY_MS    3000   /* retry connection every 3 seconds */

static SOCKET  lsext_socket = INVALID_SOCKET;
static int     lsext_lastConnectAttempt = 0;
static qboolean lsext_connected = qfalse;

/* Bidirectional sync: poll ext LS timer phase to detect external reset */
#define LSEXT_POLL_MS     2000   /* poll timer phase every 2 seconds */
#define LSEXT_POLL_GRACE  5000   /* grace period after start before polling */
static int     lsext_lastPollMs = 0;
static char    lsext_recvBuf[512];
static int     lsext_recvLen = 0;

static qboolean LS_ExtEnabled( void ) {
	if ( !ls_typeCvar ) return qfalse;
	return ( ls_typeCvar->integer == LS_TYPE_EXTERNAL ||
			 ls_typeCvar->integer == LS_TYPE_BOTH );
}

static qboolean LS_InGameEnabled( void ) {
	if ( !ls_typeCvar ) return qtrue;
	return ( ls_typeCvar->integer == LS_TYPE_INGAME ||
			 ls_typeCvar->integer == LS_TYPE_BOTH );
}

static void LS_ExtConnect( void ) {
	struct sockaddr_in addr;
	u_long nonBlocking = 1;
	int result;

	if ( lsext_socket != INVALID_SOCKET ) {
		closesocket( lsext_socket );
		lsext_socket = INVALID_SOCKET;
	}
	lsext_connected = qfalse;

	lsext_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	if ( lsext_socket == INVALID_SOCKET ) {
		return;
	}

	/* Set non-blocking so connect doesn't stall the game */
	ioctlsocket( lsext_socket, FIONBIO, &nonBlocking );

	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_port = htons( LSEXT_PORT );
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );

	result = connect( lsext_socket, (struct sockaddr *)&addr, sizeof( addr ) );
	if ( result == SOCKET_ERROR ) {
		int err = WSAGetLastError();
		if ( err != WSAEWOULDBLOCK ) {
			closesocket( lsext_socket );
			lsext_socket = INVALID_SOCKET;
			return;
		}
		/* WSAEWOULDBLOCK = connection in progress, will check later */
	} else {
		lsext_connected = qtrue;
		Com_Printf( "^2LiveSplit: Connected to external LiveSplit Server\n" );
	}
}

static void LS_ExtCheckConnection( void ) {
	fd_set writefds, errfds;
	struct timeval tv = { 0, 0 };

	if ( lsext_socket == INVALID_SOCKET || lsext_connected ) return;

	FD_ZERO( &writefds );
	FD_ZERO( &errfds );
	FD_SET( lsext_socket, &writefds );
	FD_SET( lsext_socket, &errfds );

	if ( select( 0, NULL, &writefds, &errfds, &tv ) > 0 ) {
		if ( FD_ISSET( lsext_socket, &writefds ) ) {
			lsext_connected = qtrue;
			Com_Printf( "^2LiveSplit: Connected to external LiveSplit Server\n" );
		} else {
			closesocket( lsext_socket );
			lsext_socket = INVALID_SOCKET;
		}
	}
}

static void LS_ExtDisconnect( void ) {
	if ( lsext_socket != INVALID_SOCKET ) {
		closesocket( lsext_socket );
		lsext_socket = INVALID_SOCKET;
	}
	lsext_connected = qfalse;
	lsext_recvLen = 0;
}

static void LS_ExtSend( const char *cmd ) {
	char buf[256];
	int len, sent;

	if ( !lsext_connected || lsext_socket == INVALID_SOCKET ) return;

	Com_sprintf( buf, sizeof( buf ), "%s\r\n", cmd );
	len = strlen( buf );
	sent = send( lsext_socket, buf, len, 0 );
	if ( sent == SOCKET_ERROR ) {
		Com_Printf( "^3LiveSplit: Lost connection to external LiveSplit Server\n" );
		LS_ExtDisconnect();
	}
}

/* Send current IGT to external LiveSplit every frame.
   No dedup - we must keep sending even when value is unchanged
   so LiveSplit's own internal timer doesn't drift ahead. */
static void LS_ExtSendGameTime( int igtMs ) {
	char buf[64];
	if ( !lsext_connected ) return;
	if ( igtMs < 0 ) igtMs = 0;

	Com_sprintf( buf, sizeof( buf ), "setgametime %.3f",
				 (float)igtMs / 1000.0f );
	LS_ExtSend( buf );
}

/* Check for incoming data from external LiveSplit Server (non-blocking).
   Used to detect when the user resets in LiveSplit so we can sync. */
static void LS_ExtRecvCheck( void ) {
	int bytesRead;
	char tmp[256];
	fd_set readfds;
	struct timeval tv = { 0, 0 };

	if ( !lsext_connected || lsext_socket == INVALID_SOCKET ) return;

	/* Non-blocking check for incoming data */
	FD_ZERO( &readfds );
	FD_SET( lsext_socket, &readfds );

	while ( select( 0, &readfds, NULL, NULL, &tv ) > 0 &&
			FD_ISSET( lsext_socket, &readfds ) ) {
		bytesRead = recv( lsext_socket, tmp, sizeof( tmp ) - 1, 0 );
		if ( bytesRead > 0 ) {
			/* Append to recv buffer (prevent overflow) */
			if ( lsext_recvLen + bytesRead < (int)sizeof( lsext_recvBuf ) - 1 ) {
				memcpy( lsext_recvBuf + lsext_recvLen, tmp, bytesRead );
				lsext_recvLen += bytesRead;
				lsext_recvBuf[lsext_recvLen] = '\0';
			} else {
				/* Buffer full, discard old data */
				lsext_recvLen = 0;
			}
		} else if ( bytesRead == 0 ) {
			/* Connection closed by server */
			Com_Printf( "^3LiveSplit: External LiveSplit Server closed connection\n" );
			LS_ExtDisconnect();
			return;
		} else {
			break; /* SOCKET_ERROR or no more data */
		}
		/* Re-init for next iteration */
		FD_ZERO( &readfds );
		FD_SET( lsext_socket, &readfds );
		tv.tv_sec = 0; tv.tv_usec = 0;
	}

	/* Process complete lines in recv buffer */
	while ( lsext_recvLen > 0 ) {
		char *cr = strchr( lsext_recvBuf, '\r' );
		char *lf = strchr( lsext_recvBuf, '\n' );
		char *eol;
		int lineLen;

		/* Find end of line (\r\n or \n) */
		if ( cr && lf && lf == cr + 1 ) {
			eol = cr;
			lineLen = (int)( lf - lsext_recvBuf ) + 1;
		} else if ( lf ) {
			eol = lf;
			lineLen = (int)( lf - lsext_recvBuf ) + 1;
		} else {
			break; /* incomplete line, wait for more data */
		}

		*eol = '\0';

		/* Check for timer phase response */
		if ( !Q_stricmp( lsext_recvBuf, "NotRunning" ) ) {
			/* During the grace period after starttimer, lsext_lastPollMs
			   is set into the future.  Any NotRunning arriving now is a
			   stale response from before the timer started - ignore it. */
			if ( Sys_Milliseconds() - lsext_lastPollMs < 0 ) {
				/* stale response inside grace window, discard */
			} else if ( ls.active || ls.runFinished ) {
				Com_Printf( "^2LiveSplit: External reset detected - syncing in-game\n" );
				LS_DoResetSave();
			}
		}

		/* Remove processed line from buffer */
		memmove( lsext_recvBuf, lsext_recvBuf + lineLen, lsext_recvLen - lineLen + 1 );
		lsext_recvLen -= lineLen;
		if ( lsext_recvLen < 0 ) lsext_recvLen = 0;
	}
}

/* Called each frame to maintain connection when ext LS is enabled */
static void LS_ExtFrame( void ) {
	int now;

	if ( !LS_ExtEnabled() ) {
		if ( lsext_socket != INVALID_SOCKET ) {
			LS_ExtDisconnect();
		}
		return;
	}

	if ( !lsext_connected ) {
		LS_ExtCheckConnection();
	}

	now = Sys_Milliseconds();

	if ( !lsext_connected && lsext_socket == INVALID_SOCKET ) {
		if ( now - lsext_lastConnectAttempt >= LSEXT_RETRY_MS ) {
			lsext_lastConnectAttempt = now;
			LS_ExtConnect();
		}
	}

	/* Send current game time each frame if run is active.
	   Game-time is always paused in ext LS (paused right after starttimer)
	   so LiveSplit never ticks on its own - we just set the exact value
	   every frame for a perfect 1:1 match with the in-game timer. */
	if ( lsext_connected && ls.active && !ls.runFinished ) {
		int igtMs = LS_CumulativeTime( ls.modeLastIdx );
		LS_ExtSendGameTime( igtMs );
	}

	/* Read any incoming data from LiveSplit Server */
	LS_ExtRecvCheck();

	/* Periodically poll the timer phase so we can detect external resets */
	if ( lsext_connected && ( ls.active || ls.runFinished ) ) {
		if ( now - lsext_lastPollMs >= LSEXT_POLL_MS ) {
			lsext_lastPollMs = now;
			LS_ExtSend( "getcurrenttimerphase" );
		}
	}
}

#else /* non-Win32 stubs */

static qboolean LS_ExtEnabled( void ) { return qfalse; }
static qboolean LS_InGameEnabled( void ) { return qtrue; }
static void LS_ExtConnect( void ) {}
static void LS_ExtDisconnect( void ) {}
static void LS_ExtSend( const char *cmd ) { (void)cmd; }
static void LS_ExtSendGameTime( int igtMs ) { (void)igtMs; }
static void LS_ExtFrame( void ) {}

#endif /* _WIN32 */

/* =====================================================================
   Difficulty accessor helpers  (skill index 0-2)
   ===================================================================== */

/* Clamp difficulty to 1-3, return array index 0-2 */
static int LS_DiffIdx( int skill ) {
	if ( skill < 1 ) skill = 1;
	if ( skill > 3 ) skill = 3;
	return skill - 1;
}

static int LS_CurDiffIdx( void ) {
	int base = LS_DiffIdx( ls.currentDifficulty );
	int mode = ls.runMode;
	if ( mode < 0 ) mode = 0;
	if ( mode >= LS_NUM_MODES ) mode = LS_NUM_MODES - 1;
	return mode * LS_MAX_DIFFICULTIES + base;
}

/* ---- Per-category stat accessors ---- */
/* Returns the attempts/completions/PB counters for the current mode+difficulty.
   For IL mode, returns pointers into the per-split data of the active map. */
static void LS_GetCatAttempts( int **outAtt ) {
	int di = LS_CurDiffIdx();
	switch ( ls.runMode ) {
	case LS_MODE_MISSION: {
		int gi = ls.runMission - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		*outAtt = &ls.msAttempts[gi][di];
		break;
	}
	case LS_MODE_IL: {
		int idx = ls.modeFirstIdx;
		if ( idx < 0 || idx >= ls.numMaps ) idx = 0;
		*outAtt = &ls.splits[idx].d[di].totalAttempts;
		break;
	}
	default:
		*outAtt = &ls.fgAttempts[di];
		break;
	}
}

static void LS_GetCatCompletions( int **outComp ) {
	int di = LS_CurDiffIdx();
	switch ( ls.runMode ) {
	case LS_MODE_MISSION: {
		int gi = ls.runMission - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		*outComp = &ls.msCompletions[gi][di];
		break;
	}
	case LS_MODE_IL: {
		int idx = ls.modeFirstIdx;
		if ( idx < 0 || idx >= ls.numMaps ) idx = 0;
		*outComp = &ls.splits[idx].d[di].totalCompletions;
		break;
	}
	default:
		*outComp = &ls.fgCompletions[di];
		break;
	}
}

static void LS_GetCatPB( int **outPB ) {
	int di = LS_CurDiffIdx();
	switch ( ls.runMode ) {
	case LS_MODE_MISSION: {
		int gi = ls.runMission - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		*outPB = &ls.msPB[gi][di];
		break;
	}
	case LS_MODE_IL: {
		int idx = ls.modeFirstIdx;
		if ( idx < 0 || idx >= ls.numMaps ) idx = 0;
		*outPB = &ls.splits[idx].d[di].bestTimeMs;
		break;
	}
	default:
		*outPB = &ls.fgPB[di];
		break;
	}
}

/* shortcut: get difficulty slot for a split at current difficulty */
#define SD(i) ls.splits[(i)].d[LS_CurDiffIdx()]

/* Save pre-run PB values for nosave-reset restoration */
static void LS_SavePreRunPBs( void ) {
	int di = LS_CurDiffIdx();
	int *pb;
	LS_GetCatPB( &pb );
	ls.savedPBMs = *pb;

	if ( ls.runMode == LS_MODE_FULLGAME ) {
		ls.savedRgtPBMs = ls.fgPBRgt[di];
	} else if ( ls.runMode == LS_MODE_MISSION ) {
		int gi = ls.runMission - 1;
		if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS )
			ls.savedRgtPBMs = ls.msPBRgt[gi][di];
		else
			ls.savedRgtPBMs = 0;
	} else {
		ls.savedRgtPBMs = 0;
	}
}

/* Restore pre-run PB values (called from nosave reset) */
static void LS_RestorePreRunPBs( void ) {
	int di = LS_CurDiffIdx();
	int *pb;
	LS_GetCatPB( &pb );
	*pb = ls.savedPBMs;

	if ( ls.runMode == LS_MODE_FULLGAME ) {
		ls.fgPBRgt[di] = ls.savedRgtPBMs;
	} else if ( ls.runMode == LS_MODE_MISSION ) {
		int gi = ls.runMission - 1;
		if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS )
			ls.msPBRgt[gi][di] = ls.savedRgtPBMs;
	}
}

/* =====================================================================
   Generic helpers
   ===================================================================== */

static void LS_ExtractMapname( const char *fullpath, char *out, int outSize ) {
	const char *p;
	int len;
	p = strrchr( fullpath, '/' );
	if ( p ) { p++; } else { p = fullpath; }
	Q_strncpyz( out, p, outSize );
	len = strlen( out );
	if ( len > 4 && !Q_stricmp( out + len - 4, ".bsp" ) ) {
		out[len - 4] = '\0';
	}
}

static int LS_FindMapIndex( const char *mapname ) {
	int i;
	for ( i = 0; i < ls.numMaps; i++ ) {
		if ( !Q_stricmp( mapname, ls.splits[i].mapname ) ) return i;
	}
	return -1;
}

/* Is this index within the active set for the current mode? */
static qboolean LS_InActiveSet( int idx ) {
	if ( idx < ls.modeFirstIdx || idx > ls.modeLastIdx ) return qfalse;
	return qtrue;
}

/* Get the display name for a split, picking full/short/bsp depending on available width.
   maxPixels = max width in virtual pixels, charSz = char width.
   Returns pointer to a static buf (truncated with ...) or the name directly. */
static char ls_nameBuf[128];
static const char *LS_SplitDisplayName( int idx, float maxPixels, float charSz ) {
	const char *full, *shrt, *bsp;
	int maxChars, len;

	if ( idx < 0 || idx >= ls.numMaps ) return "---";
	full = ls.splits[idx].displayName;
	shrt = ls.splits[idx].shortName;
	bsp  = ls.splits[idx].mapname;

	if ( charSz <= 0 ) charSz = 5.0f;
	maxChars = (int)( maxPixels / charSz );
	if ( maxChars < 3 ) maxChars = 3;

	/* Try full name first */
	if ( full && full[0] ) {
		len = (int)strlen( full );
		if ( len <= maxChars ) return full;
	}

	/* Try short name */
	if ( shrt && shrt[0] ) {
		len = (int)strlen( shrt );
		if ( len <= maxChars ) return shrt;
	}

	/* Fall back to BSP name */
	len = (int)strlen( bsp );
	if ( len <= maxChars ) return bsp;

	/* Truncate with ... */
	if ( maxChars <= 3 ) {
		Q_strncpyz( ls_nameBuf, "...", sizeof( ls_nameBuf ) );
	} else {
		int copyLen = maxChars - 3;
		/* Pick the shortest source that has content */
		const char *src = ( shrt && shrt[0] ) ? shrt : ( full && full[0] ) ? full : bsp;
		if ( copyLen >= (int)sizeof( ls_nameBuf ) - 4 ) copyLen = (int)sizeof( ls_nameBuf ) - 4;
		memcpy( ls_nameBuf, src, copyLen );
		ls_nameBuf[copyLen]     = '.';
		ls_nameBuf[copyLen + 1] = '.';
		ls_nameBuf[copyLen + 2] = '.';
		ls_nameBuf[copyLen + 3] = '\0';
	}
	return ls_nameBuf;
}

static int LS_NextRealSplit( int from ) {
	int idx;
	for ( idx = from + 1; idx < ls.numMaps; idx++ ) {
		if ( !ls.splits[idx].cutscene ) return idx;
	}
	return -1;
}

static int LS_PrevRealSplit( int from ) {
	int idx;
	for ( idx = from - 1; idx >= 0; idx-- ) {
		if ( !ls.splits[idx].cutscene ) return idx;
	}
	return -1;
}

/* Detect current difficulty from cvars */
static int LS_DetectDifficulty( void ) {
	int skill = Cvar_VariableIntegerValue( "g_gameskill" );
	if ( skill >= 1 && skill <= 3 ) return skill;
	skill = Cvar_VariableIntegerValue( "g_spSkill" );
	if ( skill >= 1 && skill <= 5 ) {
		if ( skill <= 2 ) return 1;
		if ( skill <= 4 ) return 2;
		return 3;
	}
	return 2; /* default Bring 'em on */
}

/* =====================================================================
   Visible row management - rebuilds visMap per mode
   ===================================================================== */

static void LS_RebuildVisMap( void ) {
	int i, vis = 0;
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( !ls.splits[i].cutscene ) {
			ls.visMap[vis] = i;
			vis++;
		}
	}
	ls.numVisible = vis;
}

static void LS_UpdateCurVisRow( void ) {
	int i;
	ls.curVisRow = -1;
	if ( ls.currentMapIndex < 0 ) return;

	for ( i = 0; i < ls.numVisible; i++ ) {
		if ( ls.visMap[i] == ls.currentMapIndex ) {
			ls.curVisRow = i;
			return;
		}
	}

	/* If current map is a cutscene, highlight the target real split */
	if ( ls.splits[ls.currentMapIndex].cutscene ) {
		int target;
		if ( ls.currentMapIndex == ls.modeFirstIdx ) {
			target = LS_NextRealSplit( ls.currentMapIndex );
		} else {
			target = LS_PrevRealSplit( ls.currentMapIndex );
		}
		if ( target >= 0 ) {
			for ( i = 0; i < ls.numVisible; i++ ) {
				if ( ls.visMap[i] == target ) {
					ls.curVisRow = i;
					return;
				}
			}
		}
	}
}

static const char *LS_GetDisplayMapName( void ) {
	int idx;
	if ( ls.currentMapIndex < 0 ) {
		return ls.actualMapname[0] ? ls.actualMapname : "---";
	}
	if ( ls.splits[ls.currentMapIndex].cutscene ) {
		if ( ls.currentMapIndex == ls.modeFirstIdx ) {
			idx = LS_NextRealSplit( ls.currentMapIndex );
		} else {
			idx = LS_PrevRealSplit( ls.currentMapIndex );
		}
		if ( idx >= 0 ) return ls.splits[idx].mapname;
	}
	return ls.splits[ls.currentMapIndex].mapname;
}

/* =====================================================================
   Formatting
   ===================================================================== */

static void LS_FormatTime( int ms, char *out, int outSize ) {
	int mins, secs, hundredths;
	qboolean neg = qfalse;
	if ( ms < 0 ) { neg = qtrue; ms = -ms; }
	hundredths = ( ms % 1000 ) / 10;
	secs       = ( ms / 1000 ) % 60;
	mins       = ms / 60000;
	if ( neg ) {
		Com_sprintf( out, outSize, "-%d:%02d.%02d", mins, secs, hundredths );
	} else {
		Com_sprintf( out, outSize, "%d:%02d.%02d", mins, secs, hundredths );
	}
}

static void LS_FormatDelta( int deltaMs, char *out, int outSize ) {
	int ad, secs, hundredths;
	if ( deltaMs == 0 ) { Com_sprintf( out, outSize, "0.00" ); return; }
	ad         = deltaMs < 0 ? -deltaMs : deltaMs;
	secs       = ad / 1000;
	hundredths = ( ad % 1000 ) / 10;
	if ( secs >= 60 ) {
		Com_sprintf( out, outSize, "%c%d:%02d",
			deltaMs < 0 ? '-' : '+', secs / 60, secs % 60 );
	} else {
		Com_sprintf( out, outSize, "%c%d.%02d",
			deltaMs < 0 ? '-' : '+', secs, hundredths );
	}
}

/* ---- 100% category: parse secrets/treasures from CS_MISSIONSTATS ---- */
/* Configstring format: "s=,H,M,S,objF,objT,secF,secT,tresF,tresT,attempts"
   No spaces between fields - all comma-separated with "s=" prefix. */
static void LS_ParseMissionStats( void ) {
	const char *info;
	int offset;
	int vals[11], n = 0;
	char num[16];
	int numLen = 0;

	if ( cls.state < CA_ACTIVE ) return;

	offset = cl.gameState.stringOffsets[LS_CS_MISSIONSTATS];
	if ( !offset ) return;

	info = cl.gameState.stringData + offset;
	if ( !info[0] ) return;

	/* Skip "s=" prefix */
	if ( info[0] == 's' && info[1] == '=' ) info += 2;

	/* Parse comma-separated integers */
	while ( *info && n < 11 ) {
		if ( *info == ',' ) {
			num[numLen] = '\0';
			if ( numLen > 0 ) vals[n++] = atoi( num );
			numLen = 0;
		} else {
			if ( numLen < 15 ) num[numLen++] = *info;
		}
		info++;
	}
	if ( numLen > 0 && n < 11 ) {
		num[numLen] = '\0';
		vals[n++] = atoi( num );
	}

	/* Format: H,M,S,objF,objT,secF,secT,tresF,tresT[,attempts]
	   Index:  0,1,2, 3,   4,   5,   6,   7,    8      9        */
	if ( n >= 9 ) {
		ls.liveSecretsFound  = vals[5];
		ls.liveSecretsTotal  = vals[6];
		ls.liveTreasureFound = vals[7];
		ls.liveTreasureTotal = vals[8];
	}
}

/* =====================================================================
   Calculation helpers (mode-aware: only count active-set splits)
   ===================================================================== */

static int LS_CalcSumOfBests( void ) {
	int i, sum = 0, di = LS_CurDiffIdx();
	qboolean allGold = qtrue;
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].d[di].bestTimeMs > 0 ) {
			sum += ls.splits[i].d[di].bestTimeMs;
		} else {
			allGold = qfalse;
		}
	}
	return allGold ? sum : -1;
}

static int LS_CalcBestPossible( void ) {
	int i, bpt = 0, di = LS_CurDiffIdx();

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].splitDone ) {
			/* Completed: locked in, use actual time */
			bpt += ls.splits[i].currentTimeMs;
		} else if ( i == ls.currentMapIndex || ls.splits[i].currentTimeMs > 0 ) {
			/* Currently running or deferred: use max(actual, gold).
			   If still under gold, assume we can hit gold.
			   If already past gold, use actual (time lost). */
			int gold = ls.splits[i].d[di].bestTimeMs;
			if ( gold > 0 && gold > ls.splits[i].currentTimeMs ) {
				bpt += gold;
			} else {
				bpt += ls.splits[i].currentTimeMs;
			}
		} else {
			/* Future split: use gold (best possible segment) */
			if ( ls.splits[i].d[di].bestTimeMs > 0 ) {
				bpt += ls.splits[i].d[di].bestTimeMs;
			} else {
				return -1;
			}
		}
	}
	return bpt;
}

static int LS_CalcPossibleTimeSave( void ) {
	int di = LS_CurDiffIdx();
	if ( ls.currentMapIndex < 0 || ls.currentMapIndex >= ls.numMaps ) return -1;
	if ( ls.splits[ls.currentMapIndex].d[di].bestTimeMs <= 0 ) return -1;
	return ls.splits[ls.currentMapIndex].currentTimeMs - ls.splits[ls.currentMapIndex].d[di].bestTimeMs;
}

static int LS_CalcPreviousSegment( qboolean *hasData ) {
	int i, di = LS_CurDiffIdx();
	*hasData = qfalse;
	if ( ls.currentMapIndex <= 0 ) return 0;
	for ( i = ls.currentMapIndex - 1; i >= ls.modeFirstIdx; i-- ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].splitDone ) {
			if ( ls.splits[i].d[di].bestTimeMs > 0 ) {
				*hasData = qtrue;
				return ls.splits[i].currentTimeMs - ls.splits[i].d[di].bestTimeMs;
			}
			return 0;
		}
		return 0;
	}
	return 0;
}

static int LS_CalcBestSegmentsDelta( qboolean *hasData ) {
	int i, delta = 0, di = LS_CurDiffIdx();
	*hasData = qfalse;
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].splitDone && ls.splits[i].d[di].bestTimeMs > 0 ) {
			delta += ls.splits[i].currentTimeMs - ls.splits[i].d[di].bestTimeMs;
			*hasData = qtrue;
		}
	}
	return delta;
}

/* Check if the current run has any unsaved progress worth keeping:
   - Any split where goldFlashMs > 0 (new gold was set this run)
   - Or the run finished (potential new PB) */
static qboolean LS_HasUnsavedProgress( void ) {
	int i;

	/* A finished run always has something to save */
	if ( ls.runFinished ) return qtrue;

	/* Check for any new golds set during this run */
	for ( i = 0; i < ls.numMaps; i++ ) {
		if ( ls.splits[i].splitDone && ls.splits[i].goldFlashMs > 0 ) {
			return qtrue;
		}
	}
	return qfalse;
}

/* Cumulative time up to and including splitIdx (active set only, non-cutscene) */
static int LS_CumulativeTime( int upToIdx ) {
	int i, sum = 0;
	for ( i = ls.modeFirstIdx; i <= upToIdx && i < ls.numMaps; i++ ) {
		if ( !ls.splits[i].cutscene ) sum += ls.splits[i].currentTimeMs;
	}
	return sum;
}

/* Cumulative best (golds) up to and including splitIdx.
   Returns -1 if any non-cutscene split has no gold. */
static int LS_CumulativeBest( int upToIdx ) {
	int i, sum = 0, di = LS_CurDiffIdx();
	for ( i = ls.modeFirstIdx; i <= upToIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].d[di].bestTimeMs > 0 ) sum += ls.splits[i].d[di].bestTimeMs;
		else return -1;
	}
	return sum;
}

/* Cumulative PB segment time up to and including splitIdx.
   Returns -1 if any non-cutscene split has no PB segment. */
static int LS_CumulativePB( int upToIdx ) {
	int i, sum = 0, di = LS_CurDiffIdx();
	for ( i = ls.modeFirstIdx; i <= upToIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].d[di].pbSegmentMs > 0 ) sum += ls.splits[i].d[di].pbSegmentMs;
		else return -1;
	}
	return sum;
}

/* Resolve the active real split index when on a cutscene */
static int LS_ActiveRealSplit( void ) {
	if ( ls.currentMapIndex < 0 ) return -1;
	if ( !ls.splits[ls.currentMapIndex].cutscene ) return ls.currentMapIndex;
	if ( ls.currentMapIndex == ls.modeFirstIdx ) return LS_NextRealSplit( ls.modeFirstIdx );
	return LS_PrevRealSplit( ls.currentMapIndex );
}

/* Save PB segments for current difficulty (called on new PB) */
static void LS_SavePbSegments( void ) {
	int i, di = LS_CurDiffIdx();
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( !ls.splits[i].cutscene ) {
			ls.splits[i].d[di].pbSegmentMs = ls.splits[i].currentTimeMs;
		}
	}
}

/* =====================================================================
   Drawing helpers  (engine-level, 640x480 virtual coords)
   ===================================================================== */

static float ls_opacity = 1.0f;
static float ls_bgalpha = 0.82f;

static void LS_FillRect( float x, float y, float w, float h, const float *color ) {
	vec4_t c;
	c[0] = color[0]; c[1] = color[1]; c[2] = color[2]; c[3] = color[3] * ls_opacity;
	SCR_FillRect( x, y, w, h, c );
}

static void LS_DrawRect( float x, float y, float w, float h, const float *color ) {
	LS_FillRect( x, y, w, 1, color );
	LS_FillRect( x, y + h - 1, w, 1, color );
	LS_FillRect( x, y + 1, 1, h - 2, color );
	LS_FillRect( x + w - 1, y + 1, 1, h - 2, color );
}

static void LS_DrawString( int x, int y, float charSize, const char *str, float *color ) {
	vec4_t c, shadow;
	const char *s;
	int xx;

	c[0] = color[0]; c[1] = color[1]; c[2] = color[2]; c[3] = color[3] * ls_opacity;

	/* subtle 1px drop shadow (toggled by ls_text_shadow) */
	if ( ls_text_shadowCvar && ls_text_shadowCvar->integer ) {
		shadow[0] = 0.0f; shadow[1] = 0.0f; shadow[2] = 0.0f; shadow[3] = c[3] * 0.45f;
		re.SetColor( shadow );
		s = str; xx = x;
		while ( *s ) {
			if ( Q_IsColorString( s ) ) { s += 2; continue; }
			SCR_DrawChar( xx + 1, y + 1, charSize, *s );
			xx += (int)charSize; s++;
		}
	}

	/* main text */
	re.SetColor( c );
	s = str; xx = x;
	while ( *s ) {
		if ( Q_IsColorString( s ) ) { s += 2; continue; }
		SCR_DrawChar( xx, y, charSize, *s );
		xx += (int)charSize; s++;
	}
	re.SetColor( NULL );
}

/* =====================================================================
   File I/O  (V8 format - per-category stats + multi-format backward compat)
   ===================================================================== */

static void LS_Save( void ) {
	fileHandle_t f;
	int i, di, gi;
	char buf[512];
	int rgtElapsed;

	FS_FOpenFileByMode( LS_SAVE_FILE, &f, FS_WRITE );
	if ( !f ) {
		Com_Printf( "^1LiveSplit: Could not save stats\n" );
		return;
	}

	if ( ls.active && !ls.runFinished ) {
		rgtElapsed = Sys_Milliseconds() - ls.runStartRealMs;
	} else {
		rgtElapsed = ls.runSavedRealMs;
	}

	Com_sprintf( buf, sizeof( buf ), "LIVESPLIT_V10\n" );
	FS_Write( buf, strlen( buf ), f );

	/* RUN line: active, finished, igt, rgt, mapIndex, difficulty, mode, mission */
	Com_sprintf( buf, sizeof( buf ), "RUN %d %d %d %d %d %d %d %d\n",
		(int)ls.active, (int)ls.runFinished,
		ls.runTotalIGTMs, rgtElapsed,
		ls.currentMapIndex,
		ls.currentDifficulty, ls.runMode, ls.runMission );
	FS_Write( buf, strlen( buf ), f );

	/* Per-difficulty fullgame stats */
	for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
		Com_sprintf( buf, sizeof( buf ), "FGDIFF %d %d %d %d\n",
			di + 1,
			ls.fgAttempts[di],
			ls.fgCompletions[di],
			ls.fgPB[di] );
		FS_Write( buf, strlen( buf ), f );
	}

	/* Per-mission-group per-difficulty stats */
	for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
		for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
			Com_sprintf( buf, sizeof( buf ), "MSDIFF %d %d %d %d %d\n",
				gi + 1, di + 1,
				ls.msAttempts[gi][di],
				ls.msCompletions[gi][di],
				ls.msPB[gi][di] );
			FS_Write( buf, strlen( buf ), f );
		}
	}

	/* V9: Per-difficulty fullgame PB realtime */
	for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
		Com_sprintf( buf, sizeof( buf ), "FGRGT %d %d\n",
			di + 1, ls.fgPBRgt[di] );
		FS_Write( buf, strlen( buf ), f );
	}

	/* V9: Per-mission-group per-difficulty PB realtime */
	for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
		for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
			Com_sprintf( buf, sizeof( buf ), "MSRGT %d %d %d\n",
				gi + 1, di + 1, ls.msPBRgt[gi][di] );
			FS_Write( buf, strlen( buf ), f );
		}
	}

	/* V9: Anti-cheat counters */
	Com_sprintf( buf, sizeof( buf ), "CHEATS %d %d %d\n",
		ls.totalPauses, ls.totalUndos, ls.totalSkips );
	FS_Write( buf, strlen( buf ), f );

	/* Per-mode per-difficulty per-map golds/PB segments */
	{
		int mi;
		for ( mi = 0; mi < LS_NUM_MODES; mi++ ) {
			for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
				int slot = mi * LS_MAX_DIFFICULTIES + di;
				for ( i = 0; i < ls.numMaps; i++ ) {
					if ( ls.splits[i].cutscene ) continue;
					Com_sprintf( buf, sizeof( buf ), "MMAP %d %d %s %d %d %d %d\n",
						mi, di + 1,
						ls.splits[i].mapname,
						ls.splits[i].d[slot].bestTimeMs,
						ls.splits[i].d[slot].pbSegmentMs,
						ls.splits[i].d[slot].totalAttempts,
						ls.splits[i].d[slot].totalCompletions );
					FS_Write( buf, strlen( buf ), f );
				}
			}
		}
	}

	/* Current run map times */
	for ( i = 0; i < ls.numMaps; i++ ) {
		Com_sprintf( buf, sizeof( buf ), "CMAP %s %d %d\n",
			ls.splits[i].mapname,
			ls.splits[i].currentTimeMs,
			(int)ls.splits[i].splitDone );
		FS_Write( buf, strlen( buf ), f );
	}

	FS_FCloseFile( f );
}

static void LS_SaveHistory( void ) {
	fileHandle_t f;
	int i, j;
	char buf[4096];

	FS_FOpenFileByMode( LS_HISTORY_FILE, &f, FS_WRITE );
	if ( !f ) return;

	Com_sprintf( buf, sizeof( buf ), "LSHISTORY_V2\n" );
	FS_Write( buf, strlen( buf ), f );

	Com_sprintf( buf, sizeof( buf ), "COUNT %d\n", ls.numHistoryRuns );
	FS_Write( buf, strlen( buf ), f );

	for ( i = 0; i < ls.numHistoryRuns; i++ ) {
		lsRunHistory_t *r = &ls.history[i];
		char tmp[32];

		Com_sprintf( buf, sizeof( buf ), "HRUN %d %d %d %d %d %d",
			r->difficulty, r->mode, r->missionNum,
			r->totalIGTMs, r->totalRGTMs, r->numSplits );
		for ( j = 0; j < r->numSplits && j < LS_MAX_MAPS; j++ ) {
			Com_sprintf( tmp, sizeof( tmp ), " %d", r->splitTimes[j] );
			Q_strcat( buf, sizeof( buf ), tmp );
		}
		Q_strcat( buf, sizeof( buf ), "\n" );
		FS_Write( buf, strlen( buf ), f );
	}

	FS_FCloseFile( f );
}

static void LS_LoadHistory( void ) {
	fileHandle_t f;
	int fileLen;
	char *bigbuf, *text, *token;
	qboolean isV2 = qfalse;

	fileLen = FS_FOpenFileByMode( LS_HISTORY_FILE, &f, FS_READ );
	if ( fileLen <= 0 ) return;

	bigbuf = Z_Malloc( fileLen + 1 );
	FS_Read( bigbuf, fileLen, f );
	bigbuf[fileLen] = '\0';
	FS_FCloseFile( f );

	text = bigbuf;
	token = COM_Parse( &text );

	if ( !Q_stricmp( token, "LSHISTORY_V2" ) ) {
		isV2 = qtrue;
	} else {
		Com_Printf( "^1LiveSplit: Unknown history format (expected LSHISTORY_V2)\n" );
		Z_Free( bigbuf );
		return;
	}

	/* COUNT */
	token = COM_Parse( &text ); /* "COUNT" */
	token = COM_Parse( &text );
	ls.numHistoryRuns = atoi( token );
	if ( ls.numHistoryRuns > LS_MAX_HISTORY_RUNS ) ls.numHistoryRuns = LS_MAX_HISTORY_RUNS;

	{
		int i, j;
		for ( i = 0; i < ls.numHistoryRuns; i++ ) {
			lsRunHistory_t *r = &ls.history[i];
			token = COM_Parse( &text ); /* "HRUN" */
			if ( Q_stricmp( token, "HRUN" ) ) break;
			token = COM_Parse( &text ); r->difficulty = atoi( token );
			token = COM_Parse( &text ); r->mode       = atoi( token );
			token = COM_Parse( &text ); r->missionNum = atoi( token );
			token = COM_Parse( &text ); r->totalIGTMs = atoi( token );
			if ( isV2 ) {
				token = COM_Parse( &text ); r->totalRGTMs = atoi( token );
			} else {
				r->totalRGTMs = 0;
			}
			token = COM_Parse( &text ); r->numSplits  = atoi( token );
			if ( r->numSplits > LS_MAX_MAPS ) r->numSplits = LS_MAX_MAPS;
			for ( j = 0; j < r->numSplits; j++ ) {
				token = COM_Parse( &text );
				r->splitTimes[j] = atoi( token );
			}
		}
	}

	Z_Free( bigbuf );
	Com_Printf( "^2LiveSplit: %d history runs loaded%s\n", ls.numHistoryRuns, isV2 ? " (V2)" : "" );
}

static void LS_Load( void ) {
	fileHandle_t f;
	int fileLen;
	char *bigbuf, *text, *token;

	fileLen = FS_FOpenFileByMode( LS_SAVE_FILE, &f, FS_READ );
	if ( fileLen <= 0 ) return;

	bigbuf = Z_Malloc( fileLen + 1 );
	FS_Read( bigbuf, fileLen, f );
	bigbuf[fileLen] = '\0';
	FS_FCloseFile( f );

	text = bigbuf;
	token = COM_Parse( &text );

	if ( Q_stricmp( token, "LIVESPLIT_V10" ) ) {
		Com_Printf( "^1LiveSplit: Unknown save format (expected LIVESPLIT_V10)\n" );
		Z_Free( bigbuf );
		return;
	}

	{

		while ( 1 ) {
			token = COM_Parse( &text );
			if ( !token[0] ) break;

			if ( !Q_stricmp( token, "RUN" ) ) {
				int savedRgt, loadedIdx;
				token = COM_Parse( &text ); ls.active             = (qboolean)atoi( token );
				token = COM_Parse( &text ); ls.runFinished         = (qboolean)atoi( token );
				token = COM_Parse( &text ); ls.runTotalIGTMs       = atoi( token );
				token = COM_Parse( &text ); savedRgt               = atoi( token );
				token = COM_Parse( &text ); loadedIdx              = atoi( token );
				token = COM_Parse( &text ); ls.currentDifficulty   = atoi( token );
				token = COM_Parse( &text ); ls.runMode             = atoi( token );
				token = COM_Parse( &text ); ls.runMission          = atoi( token );
				/* Validate map index bounds */
				if ( loadedIdx >= ls.numMaps ) loadedIdx = ls.numMaps - 1;
				if ( loadedIdx < -1 ) loadedIdx = -1;
				ls.currentMapIndex = loadedIdx;
				ls.runSavedRealMs = savedRgt;
				if ( ls.active && !ls.runFinished )
					ls.runStartRealMs = Sys_Milliseconds() - savedRgt;
			} else if ( !Q_stricmp( token, "FGDIFF" ) ) {
				int di;
				token = COM_Parse( &text ); di = atoi( token ) - 1;
				if ( di >= 0 && di < LS_MAX_DIFFICULTIES ) {
					token = COM_Parse( &text ); ls.fgAttempts[di]    = atoi( token );
					token = COM_Parse( &text ); ls.fgCompletions[di] = atoi( token );
					token = COM_Parse( &text ); ls.fgPB[di]          = atoi( token );
				} else {
					COM_Parse( &text ); COM_Parse( &text ); COM_Parse( &text );
				}
			} else if ( !Q_stricmp( token, "FGRGT" ) ) {
				int di;
				token = COM_Parse( &text ); di = atoi( token ) - 1;
				if ( di >= 0 && di < LS_MAX_DIFFICULTIES ) {
					token = COM_Parse( &text ); ls.fgPBRgt[di] = atoi( token );
				} else {
					COM_Parse( &text );
				}
			} else if ( !Q_stricmp( token, "MSDIFF" ) ) {
				int gi, di;
				token = COM_Parse( &text ); gi = atoi( token ) - 1;
				token = COM_Parse( &text ); di = atoi( token ) - 1;
				if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS &&
					 di >= 0 && di < LS_MAX_DIFFICULTIES ) {
					token = COM_Parse( &text ); ls.msAttempts[gi][di]    = atoi( token );
					token = COM_Parse( &text ); ls.msCompletions[gi][di] = atoi( token );
					token = COM_Parse( &text ); ls.msPB[gi][di]          = atoi( token );
				} else {
					COM_Parse( &text ); COM_Parse( &text ); COM_Parse( &text );
				}
			} else if ( !Q_stricmp( token, "MSRGT" ) ) {
				int gi, di;
				token = COM_Parse( &text ); gi = atoi( token ) - 1;
				token = COM_Parse( &text ); di = atoi( token ) - 1;
				if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS &&
					 di >= 0 && di < LS_MAX_DIFFICULTIES ) {
					token = COM_Parse( &text ); ls.msPBRgt[gi][di] = atoi( token );
				} else {
					COM_Parse( &text );
				}
			} else if ( !Q_stricmp( token, "CHEATS" ) ) {
				token = COM_Parse( &text ); ls.totalPauses = atoi( token );
				token = COM_Parse( &text ); ls.totalUndos  = atoi( token );
				token = COM_Parse( &text ); ls.totalSkips  = atoi( token );
			} else if ( !Q_stricmp( token, "MMAP" ) ) {
				/* V10 mode-specific per-difficulty map data */
				int mi, di, idx, slot;
				token = COM_Parse( &text ); mi = atoi( token );
				token = COM_Parse( &text ); di = atoi( token ) - 1;
				token = COM_Parse( &text );
				idx = LS_FindMapIndex( token );
				if ( mi >= 0 && mi < LS_NUM_MODES &&
					 di >= 0 && di < LS_MAX_DIFFICULTIES && idx >= 0 ) {
					slot = mi * LS_MAX_DIFFICULTIES + di;
					token = COM_Parse( &text ); ls.splits[idx].d[slot].bestTimeMs       = atoi( token );
					token = COM_Parse( &text ); ls.splits[idx].d[slot].pbSegmentMs      = atoi( token );
					token = COM_Parse( &text ); ls.splits[idx].d[slot].totalAttempts     = atoi( token );
					token = COM_Parse( &text ); ls.splits[idx].d[slot].totalCompletions  = atoi( token );
				} else {
					COM_Parse( &text ); COM_Parse( &text );
					COM_Parse( &text ); COM_Parse( &text );
				}
			} else if ( !Q_stricmp( token, "CMAP" ) ) {
				int idx;
				token = COM_Parse( &text );
				idx = LS_FindMapIndex( token );
				if ( idx >= 0 ) {
					token = COM_Parse( &text ); ls.splits[idx].currentTimeMs = atoi( token );
					token = COM_Parse( &text ); ls.splits[idx].splitDone     = (qboolean)atoi( token );
				} else {
					COM_Parse( &text ); COM_Parse( &text );
				}
			}
		}
		LS_UpdateCurVisRow();
		Com_Printf( "^2LiveSplit: Stats loaded (V10)\n" );
		Z_Free( bigbuf );
		return;
	}
}

/* =====================================================================
   Mode management
   ===================================================================== */

/* Compute modeFirstIdx / modeLastIdx / modeEndMapIdx and rebuild visMap */
static void LS_SetupMode( int mode, int mission ) {
	int i;

	ls.runMode    = mode;
	ls.runMission = mission;

	switch ( mode ) {
	case LS_MODE_MISSION:
		if ( mission < 1 ) mission = 1;
		if ( mission > LS_NUM_MISSION_GROUPS ) mission = LS_NUM_MISSION_GROUPS;
		ls.runMission = mission;
		{
			int grpFirst = ls_missionGroups[mission - 1].firstMission;
			int grpLast  = ls_missionGroups[mission - 1].lastMission;

			/* Find first and last map indices covering all missions in this group */
			ls.modeFirstIdx = -1;
			ls.modeLastIdx  = -1;
			for ( i = 0; i < ls.numMaps; i++ ) {
				if ( ls.splits[i].mission >= grpFirst && ls.splits[i].mission <= grpLast ) {
					if ( ls.modeFirstIdx < 0 ) ls.modeFirstIdx = i;
					ls.modeLastIdx = i;
				}
			}
			if ( ls.modeFirstIdx < 0 ) {
				ls.modeFirstIdx = 0;
				ls.modeLastIdx  = ls.numMaps - 1;
			}
		}
		break;

	case LS_MODE_IL:
		/* IL mode: ls_map cvar overrides, else use current loaded map */
		{
			int idx = -1;
			if ( ls_mapCvar && ls_mapCvar->string[0] ) {
				idx = LS_FindMapIndex( ls_mapCvar->string );
				if ( idx >= 0 && ls.splits[idx].cutscene ) idx = -1;
			}
			if ( idx < 0 ) {
				idx = LS_FindMapIndex( ls.actualMapname );
			}
			if ( idx >= 0 && !ls.splits[idx].cutscene ) {
				ls.modeFirstIdx = idx;
				ls.modeLastIdx  = idx;
			} else {
				/* fallback: first non-cutscene */
				ls.modeFirstIdx = 0;
				ls.modeLastIdx  = 0;
				for ( i = 0; i < ls.numMaps; i++ ) {
					if ( !ls.splits[i].cutscene ) {
						ls.modeFirstIdx = i;
						ls.modeLastIdx  = i;
						break;
					}
				}
			}
		}
		break;

	default: /* LS_MODE_FULLGAME */
		ls.runMode      = LS_MODE_FULLGAME;
		ls.modeFirstIdx = 0;
		ls.modeLastIdx  = ls.numMaps - 1;
		break;
	}

	/* Find the "end map" for run-finish detection */
	ls.modeEndMapIdx = -1;
	for ( i = ls.modeLastIdx; i >= ls.modeFirstIdx; i-- ) {
		if ( !ls.splits[i].cutscene ) {
			ls.modeEndMapIdx = i;
			break;
		}
	}

	LS_RebuildVisMap();
	LS_UpdateCurVisRow();
}

/* =====================================================================
   Init / Console commands
   ===================================================================== */

static void LS_BuildSplitTable( void ) {
	int i;
	memset( &ls, 0, sizeof( ls ) );

	for ( i = 0; ls_mapDefs[i].name && i < LS_MAX_MAPS; i++ ) {
		Q_strncpyz( ls.splits[i].mapname, ls_mapDefs[i].name, LS_MAX_MAPNAME );
		ls.splits[i].mission  = ls_mapDefs[i].mission;
		ls.splits[i].cutscene = ls_mapDefs[i].cutscene;
		ls.splits[i].displayName = ls_mapDefs[i].displayName;
		ls.splits[i].shortName   = ls_mapDefs[i].shortName;
		ls.splits[i].secretsTotal  = ls_mapDefs[i].secrets;
		ls.splits[i].treasureTotal = ls_mapDefs[i].treasure;
	}
	ls.numMaps         = i;
	ls.currentMapIndex = -1;
	ls.curVisRow       = -1;
	ls.prevMapname[0]  = '\0';
	ls.actualMapname[0] = '\0';
	ls.fgPendingStart  = qfalse;
	ls.initialized     = qtrue;

	/* Default: full game mode */
	LS_SetupMode( LS_MODE_FULLGAME, 1 );
}

/* =====================================================================
   Demo auto-record helpers
   ===================================================================== */

static char    ls_pendingRecordName[128];
static qboolean ls_autoRecordPending;

static void LS_AutoRecordStart( void ) {
	time_t rawTime;
	struct tm *ti;
	const char *modeStr;

	if ( !sp_autorecordCvar || !sp_autorecordCvar->integer ) return;
	if ( clc.demorecording ) return;           /* already recording */
	if ( ls_autoRecordPending ) return;        /* already waiting */

	switch ( ls.runMode ) {
		case LS_MODE_MISSION: modeStr = "mission"; break;
		case LS_MODE_IL:      modeStr = "il"; break;
		default:              modeStr = "fullgame"; break;
	}

	time( &rawTime );
	ti = localtime( &rawTime );

	Com_sprintf( ls_pendingRecordName, sizeof( ls_pendingRecordName ),
		"speedrun_%s_%04d%02d%02d_%02d%02d%02d",
		modeStr,
		ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
		ti->tm_hour, ti->tm_min, ti->tm_sec );

	ls_autoRecordPending = qtrue;
	Com_Printf( "^2LiveSplit: Auto-record pending for '%s'\n", ls_pendingRecordName );
}

static void LS_AutoRecordStop( void ) {
	ls_autoRecordPending = qfalse;
	if ( !ls_autoRecordActive ) return;
	if ( !clc.demorecording ) {
		ls_autoRecordActive = qfalse;
		return;
	}
	Cbuf_AddText( "stoprecord\n" );
	ls_autoRecordActive = qfalse;
	Com_Printf( "^2LiveSplit: Auto-record stopped\n" );
}

static void LS_DoResetSave( void ) {
	int i;
	Com_Printf( "^2LiveSplit: Run reset (saving golds)\n" );

	LS_AutoRecordStop();
	LS_ExtSend( "reset" );
	ls_resetPending = 0;

	ls.active         = qfalse;
	ls.runFinished    = qfalse;
	ls.runTotalIGTMs  = 0;
	ls.runStartRealMs = 0;
	ls.runSavedRealMs = 0;
	ls.heinrichDead   = qfalse;
	ls.endCutsceneArmed = qfalse;
	ls.endCutsceneSnapMs = -1;
	ls.endCutscenePrevLB = qfalse;
	Cvar_Set( "ls_changelevel", "0" );
	ls.fgPendingStart = qfalse;
	ls.manualPause    = qfalse;
	ls.pauseSavedRealMs = 0;
	ls.cheatsUsed     = qfalse;
	ls.settingsModified = qfalse;
	ls.settingsModCount = 0;
	ls.liveSecretsFound = 0;
	ls.liveSecretsTotal = 0;
	ls.liveTreasureFound = 0;
	ls.liveTreasureTotal = 0;
	ls.baseSecretsFound  = 0;
	ls.baseTreasureFound = 0;
	for ( i = 0; i < ls.numMaps; i++ ) {
		ls.splits[i].currentTimeMs = 0;
		ls.splits[i].splitDone     = qfalse;
		ls.splits[i].splitSkipped  = qfalse;
		ls.splits[i].prevGoldMs    = 0;
		ls.splits[i].goldFlashMs   = 0;
		ls.splits[i].secretsFound  = 0;
		ls.splits[i].treasureFound = 0;
	}

	ls.currentMapIndex  = -1;
	ls.curVisRow        = -1;

	Cvar_Set( "ls_ghost_visible", "0" );
	/* Clear prevMapname so auto-start fires reliably even when
	   the brief disconnect during map reload is too fast for
	   LS_Frame to see (same-map IL retry). */
	ls.prevMapname[0] = '\0';
	if ( ls_modeCvar ) LS_SetupMode( ls_modeCvar->integer, ls_missionCvar ? ls_missionCvar->integer : 1 );

	LS_Save();
}

static void LS_DoResetNoSave( void ) {
	int i, di;
	qboolean wasFinished = ls.runFinished;
	Com_Printf( "^2LiveSplit: Run reset (no save - golds/PBs reverted)\n" );

	LS_AutoRecordStop();

	/* Undo all completed splits in ext LS (reverse order) before reset
	   so LiveSplit won't save golds for this run */
	{
		int s;
		for ( s = ls.modeLastIdx; s >= ls.modeFirstIdx; s-- ) {
			if ( ls.splits[s].cutscene ) continue;
			if ( ls.splits[s].splitDone ) {
				LS_ExtSend( "unsplit" );
			}
		}
	}
	LS_ExtSend( "reset" );
	ls_resetPending = 0;

	di = LS_CurDiffIdx();
	for ( i = 0; i < ls.numMaps; i++ ) {
		if ( ls.splits[i].splitDone ) {
			ls.splits[i].d[di].bestTimeMs = ls.splits[i].prevGoldMs;
		}
		ls.splits[i].currentTimeMs = 0;
		ls.splits[i].splitDone     = qfalse;
		ls.splits[i].splitSkipped  = qfalse;
		ls.splits[i].prevGoldMs    = 0;
		ls.splits[i].goldFlashMs   = 0;
		ls.splits[i].secretsFound  = 0;
		ls.splits[i].treasureFound = 0;
	}

	if ( wasFinished ) {
		LS_RestorePreRunPBs();
		if ( ls.numHistoryRuns > 0 )
			ls.numHistoryRuns--;
	}

	ls.active         = qfalse;
	ls.runFinished    = qfalse;
	ls.runTotalIGTMs  = 0;
	ls.runStartRealMs = 0;
	ls.runSavedRealMs = 0;
	ls.heinrichDead   = qfalse;
	ls.endCutsceneArmed = qfalse;
	ls.endCutsceneSnapMs = -1;
	ls.endCutscenePrevLB = qfalse;
	Cvar_Set( "ls_changelevel", "0" );
	ls.fgPendingStart = qfalse;
	ls.manualPause    = qfalse;
	ls.pauseSavedRealMs = 0;
	ls.cheatsUsed     = qfalse;
	ls.settingsModified = qfalse;
	ls.settingsModCount = 0;
	ls.baseSecretsFound  = 0;
	ls.baseTreasureFound = 0;

	ls.currentMapIndex  = -1;
	ls.curVisRow        = -1;

	/* Clear prevMapname so auto-start fires reliably even when
	   the brief disconnect during map reload is too fast for
	   LS_Frame to see (same-map IL retry). */
	ls.prevMapname[0] = '\0';
	if ( ls_modeCvar ) LS_SetupMode( ls_modeCvar->integer, ls_missionCvar ? ls_missionCvar->integer : 1 );

	LS_Save();
	LS_SaveHistory();
}

static void LS_Reset_f( void ) {
	/* If run has unsaved golds or new PB, show confirmation popup */
	if ( ls.active || ls.runFinished ) {
		if ( LS_HasUnsavedProgress() ) {
			ls_resetPending = 1;
			Com_Printf( "^3LiveSplit: Run has unsaved golds/PB. Press Y to save, N to discard.\n" );
			return;
		}
	}
	LS_DoResetSave();
}

static void LS_ResetNoSave_f( void ) {
	/* If run has unsaved golds or new PB, show confirmation popup */
	if ( ls.active || ls.runFinished ) {
		if ( LS_HasUnsavedProgress() ) {
			ls_resetPending = 1;
			Com_Printf( "^3LiveSplit: Run has unsaved golds/PB. Press Y to save, N to discard.\n" );
			return;
		}
	}
	LS_DoResetNoSave();
}

/* Confirmation commands for reset popup */
static void LS_ResetConfirmSave_f( void ) {
	if ( !ls_resetPending ) return;
	LS_DoResetSave();
}

static void LS_ResetConfirmDiscard_f( void ) {
	if ( !ls_resetPending ) return;
	LS_DoResetNoSave();
}

static void LS_ResetCancel_f( void ) {
	if ( !ls_resetPending ) return;
	ls_resetPending = 0;
	Com_Printf( "^2LiveSplit: Reset cancelled\n" );
}

static void LS_Start_f( void ) {
	int nowReal, newIdx;
	if ( !ls.initialized ) return;

	nowReal = Sys_Milliseconds();

	/* If already active: manual split (advance to next split) */
	if ( ls.active && !ls.runFinished ) {
		int curIdx = ls.currentMapIndex;
		if ( curIdx >= 0 && curIdx < ls.numMaps && !ls.splits[curIdx].splitDone ) {
			LS_CompleteSplit( curIdx, nowReal );
			Com_Printf( "^2LiveSplit: Manual split on '%s'\n",
				ls.splits[curIdx].displayName ? ls.splits[curIdx].displayName : ls.splits[curIdx].mapname );
			/* Advance to next split (unless run just finished via end map) */
			if ( !ls.runFinished && curIdx + 1 <= ls.modeLastIdx ) {
				ls.currentMapIndex = curIdx + 1;
				ls.splits[curIdx + 1].currentTimeMs = 0;
				LS_UpdateCurVisRow();
			}
		} else {
			Com_Printf( "^2LiveSplit: No pending split to advance\n" );
		}
		return;
	}

	/* If finished, do a quick reset first */
	if ( ls.runFinished ) {
		int k;
		ls.active          = qfalse;
		ls.runFinished     = qfalse;
		ls.runTotalIGTMs   = 0;
		ls.runStartRealMs  = 0;
		ls.runSavedRealMs  = 0;
		ls.heinrichDead    = qfalse;
		ls.endCutsceneArmed = qfalse;
		ls.endCutsceneSnapMs = -1;
		ls.endCutscenePrevLB = qfalse;
		Cvar_Set( "ls_changelevel", "0" );
		ls.manualPause     = qfalse;
		ls.cheatsUsed      = qfalse;
		ls.settingsModified = qfalse;
		ls.settingsModCount = 0;
		for ( k = 0; k < ls.numMaps; k++ ) {
			ls.splits[k].currentTimeMs = 0;
			ls.splits[k].splitDone     = qfalse;
			ls.splits[k].splitSkipped  = qfalse;
			ls.splits[k].prevGoldMs    = 0;
			ls.splits[k].goldFlashMs   = 0;
			ls.splits[k].secretsFound  = 0;
			ls.splits[k].treasureFound = 0;
		}
		ls.currentMapIndex = -1;
		ls.curVisRow       = -1;
	}

	/* Start immediately if connected and on a map */
	if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
		char currentMap[LS_MAX_MAPNAME];
		int di;

		LS_ExtractMapname( cl.mapname, currentMap, sizeof( currentMap ) );
		newIdx = LS_FindMapIndex( currentMap );

		/* IL mode: do NOT change IL target - only ls_map cvar changes it */

		ls.active            = qtrue;
		ls_aslState.runStartCount++;
		ls_aslState.splitSeqNum = 0;
		ls.runStartRealMs    = nowReal;
		ls.lastServerTime    = cl.serverTime;
		ls.lastRealTimeMs    = nowReal;
		ls.currentDifficulty = LS_DetectDifficulty();
		di = LS_CurDiffIdx();
		LS_SavePreRunPBs();

		if ( newIdx >= 0 ) {
			ls.currentMapIndex = newIdx;
			LS_UpdateCurVisRow();
		}

		/* Increment attempt counter */
		if ( ls.runMode == LS_MODE_IL && newIdx >= 0 ) {
			ls.splits[newIdx].d[di].totalAttempts++;
		} else if ( ls.runMode == LS_MODE_MISSION ) {
			int gi = ls.runMission - 1;
			if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS )
				ls.msAttempts[gi][di]++;
		} else {
			ls.fgAttempts[di]++;
		}

		Q_strncpyz( ls.prevMapname, currentMap, LS_MAX_MAPNAME );
		LS_Save();
		LS_AutoRecordStart();
		Com_Printf( "^2LiveSplit: Timer STARTED on '%s'\n", currentMap );
		lsext_recvLen = 0; /* flush stale recv data */
		LS_ExtSend( "initgametime" );
		LS_ExtSend( "starttimer" );
		LS_ExtSend( "pausegametime" );
		lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;
	} else {
		/* Not connected - just arm for next map load */
		ls.prevMapname[0] = '\0';
		Com_Printf( "^2LiveSplit: Timer armed (will start on next map load)\n" );
	}
}

/* =====================================================================
   LS_BackupFile - copy a file to a timestamped backup
   ===================================================================== */
static void LS_BackupFile( const char *filename ) {
	fileHandle_t fIn, fOut;
	int          fileLen;
	char         *buf;
	char         backupName[256];
	time_t       rawTime;
	struct tm    *ti;

	fileLen = FS_FOpenFileByMode( filename, &fIn, FS_READ );
	if ( fileLen <= 0 ) {
		if ( fIn ) {
			FS_FCloseFile( fIn );
		}
		return;                      /* nothing to back up */
	}

	buf = (char *)Z_Malloc( fileLen + 1 );
	FS_Read( buf, fileLen, fIn );
	FS_FCloseFile( fIn );

	time( &rawTime );
	ti = localtime( &rawTime );

	Com_sprintf( backupName, sizeof( backupName ),
		"%.*s_backup_%04d%02d%02d_%02d%02d%02d.dat",
		(int)( strlen( filename ) - 4 ), filename,   /* strip .dat */
		ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
		ti->tm_hour, ti->tm_min, ti->tm_sec );

	FS_FOpenFileByMode( backupName, &fOut, FS_WRITE );
	if ( !fOut ) {
		Z_Free( buf );
		return;
	}
	FS_Write( buf, fileLen, fOut );
	FS_FCloseFile( fOut );
	Z_Free( buf );

	Com_Printf( "^2LiveSplit: Backup saved to %s\n", backupName );
}

static void LS_ResetBests_f( void ) {
	int i, di, gi;

	/* create backups before wiping */
	LS_BackupFile( LS_SAVE_FILE );
	LS_BackupFile( LS_HISTORY_FILE );

	Com_Printf( "^2LiveSplit: All stats & bests reset\n" );

	for ( i = 0; i < ls.numMaps; i++ ) {
		for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
			ls.splits[i].d[di].bestTimeMs       = 0;
			ls.splits[i].d[di].pbSegmentMs      = 0;
			ls.splits[i].d[di].totalAttempts     = 0;
			ls.splits[i].d[di].totalCompletions  = 0;
		}
		ls.splits[i].currentTimeMs = 0;
		ls.splits[i].splitDone     = qfalse;
		ls.splits[i].splitSkipped  = qfalse;
		ls.splits[i].prevGoldMs    = 0;
		ls.splits[i].goldFlashMs   = 0;
	}

	for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
		ls.fgAttempts[di]    = 0;
		ls.fgCompletions[di] = 0;
		ls.fgPB[di]          = 0;
		ls.fgPBRgt[di]       = 0;
	}

	for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
		for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
			ls.msAttempts[gi][di]    = 0;
			ls.msCompletions[gi][di] = 0;
			ls.msPB[gi][di]          = 0;
			ls.msPBRgt[gi][di]       = 0;
		}
	}

	ls.active              = qfalse;
	ls.runFinished         = qfalse;
	ls.runTotalIGTMs       = 0;
	ls.runStartRealMs      = 0;
	ls.runSavedRealMs      = 0;
	ls.currentMapIndex     = -1;
	ls.curVisRow           = -1;
	ls.prevMapname[0]      = '\0';
	ls.heinrichDead        = qfalse;
	ls.endCutsceneArmed    = qfalse;
	ls.endCutsceneSnapMs   = -1;
	ls.endCutscenePrevLB   = qfalse;
	Cvar_Set( "ls_changelevel", "0" );
	ls.numHistoryRuns      = 0;
	ls.manualPause         = qfalse;
	ls.pauseSavedRealMs    = 0;
	ls.totalPauses         = 0;
	ls.totalUndos          = 0;
	ls.totalSkips          = 0;
	LS_Save();
	LS_SaveHistory();
}

static void LS_ResetCategory_f( void ) {
	int di, i;
	const char *modeName;
	const char *skillName;

	LS_BackupFile( LS_SAVE_FILE );

	di = LS_CurDiffIdx();

	switch ( ls.currentDifficulty ) {
		case 1:  skillName = "Don't hurt me."; break;
		case 2:  skillName = "Bring 'em on!"; break;
		case 3:  skillName = "I am Death incarnate!"; break;
		default: skillName = "Normal"; break;
	}

	switch ( ls.runMode ) {
	case LS_MODE_MISSION: {
		int gi = ls.runMission - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		modeName = ls_missionGroups[gi].name;

		ls.msAttempts[gi][di]    = 0;
		ls.msCompletions[gi][di] = 0;
		ls.msPB[gi][di]          = 0;
		ls.msPBRgt[gi][di]       = 0;

		/* Reset split bests for maps in this mission group */
		for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
			ls.splits[i].d[di].bestTimeMs       = 0;
			ls.splits[i].d[di].pbSegmentMs      = 0;
			ls.splits[i].d[di].totalAttempts     = 0;
			ls.splits[i].d[di].totalCompletions  = 0;
		}
		break;
	}
	case LS_MODE_IL: {
		int idx = ls.modeFirstIdx;
		if ( idx >= 0 && idx < ls.numMaps ) {
			modeName = ls.splits[idx].displayName ? ls.splits[idx].displayName : ls.splits[idx].mapname;
			ls.splits[idx].d[di].bestTimeMs       = 0;
			ls.splits[idx].d[di].pbSegmentMs      = 0;
			ls.splits[idx].d[di].totalAttempts     = 0;
			ls.splits[idx].d[di].totalCompletions  = 0;
		} else {
			modeName = "IL";
		}
		break;
	}
	default: {
		modeName = "Full Game";

		ls.fgAttempts[di]    = 0;
		ls.fgCompletions[di] = 0;
		ls.fgPB[di]          = 0;
		ls.fgPBRgt[di]       = 0;

		/* Reset ALL split bests for this difficulty (fullgame uses all maps) */
		for ( i = 0; i < ls.numMaps; i++ ) {
			ls.splits[i].d[di].bestTimeMs       = 0;
			ls.splits[i].d[di].pbSegmentMs      = 0;
			ls.splits[i].d[di].totalAttempts     = 0;
			ls.splits[i].d[di].totalCompletions  = 0;
		}
		break;
	}
	}

	Com_Printf( "^2LiveSplit: Reset bests for '%s' on %s\n", modeName, skillName );

	/* Also reset the current run */
	{
		ls.active         = qfalse;
		ls.runFinished    = qfalse;
		ls.runTotalIGTMs  = 0;
		ls.runStartRealMs = 0;
		ls.runSavedRealMs = 0;
		ls.currentMapIndex = -1;
		ls.curVisRow       = -1;
		for ( i = 0; i < ls.numMaps; i++ ) {
			ls.splits[i].currentTimeMs = 0;
			ls.splits[i].splitDone     = qfalse;
			ls.splits[i].splitSkipped  = qfalse;
			ls.splits[i].prevGoldMs    = 0;
			ls.splits[i].goldFlashMs   = 0;
		}
	}

	LS_Save();
}

/* =====================================================================
   livesplit_pause  - toggle manual timer pause
   ===================================================================== */
static void LS_Pause_f( void ) {
	int nowReal;

	if ( !ls.initialized || !ls.active || ls.runFinished ) {
		Com_Printf( "^2LiveSplit: No active run to pause\n" );
		return;
	}

	nowReal = Sys_Milliseconds();

	if ( ls.manualPause ) {
		/* Unpause: adjust runStartRealMs so RGT stays consistent */
		int pauseDuration = nowReal - ls.pauseSavedRealMs;
		if ( pauseDuration > 0 ) {
			ls.runStartRealMs += pauseDuration;
		}
		ls.manualPause      = qfalse;
		ls.pauseSavedRealMs = 0;
		Com_Printf( "^2LiveSplit: Timer RESUMED\n" );
	} else {
		/* Pause */
		ls.manualPause      = qtrue;
		ls.pauseSavedRealMs = nowReal;
		ls.totalPauses++;
		Com_Printf( "^2LiveSplit: Timer PAUSED (total pauses: %d)\n", ls.totalPauses );
	}
}

/* =====================================================================
   livesplit_undo  - undo the last completed split
   ===================================================================== */
static void LS_Undo_f( void ) {
	int idx, di;

	if ( !ls.initialized ) return;
	if ( !ls.active && !ls.runFinished ) {
		Com_Printf( "^2LiveSplit: No active/finished run to undo\n" );
		return;
	}

	/* Find the last split that was completed (searching backwards) */
	idx = -1;
	{
		int i;
		for ( i = ls.modeLastIdx; i >= ls.modeFirstIdx; i-- ) {
			if ( ls.splits[i].cutscene ) continue;
			if ( ls.splits[i].splitDone ) {
				idx = i;
				break;
			}
		}
	}

	if ( idx < 0 ) {
		Com_Printf( "^2LiveSplit: No split to undo\n" );
		return;
	}

	di = LS_CurDiffIdx();

	/* Undo the split - subtract from total and let timer continue from current time */
	ls.splits[idx].splitDone = qfalse;
	ls.splits[idx].splitSkipped = qfalse;

	/* Roll back 100% base counters so live display shows correct
	   per-map deltas again.  Keep secretsFound/treasureFound intact
	   so the snapshot persists through the undo. */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		ls.baseSecretsFound  -= ls.splits[idx].secretsFound;
		ls.baseTreasureFound -= ls.splits[idx].treasureFound;
		if ( ls.baseSecretsFound  < 0 ) ls.baseSecretsFound  = 0;
		if ( ls.baseTreasureFound < 0 ) ls.baseTreasureFound = 0;
	}

	/* Subtract this split's contribution from the cumulative total.
	   The segment timer will keep running from the existing currentTimeMs. */
	if ( ls.splits[idx].currentTimeMs > 0 ) {
		ls.runTotalIGTMs -= ls.splits[idx].currentTimeMs;
		if ( ls.runTotalIGTMs < 0 ) ls.runTotalIGTMs = 0;
	}

	/* Restore previous gold time */
	if ( ls.splits[idx].prevGoldMs > 0 ) {
		ls.splits[idx].d[di].bestTimeMs = ls.splits[idx].prevGoldMs;
	}

	/* Decrement completions for this split */
	if ( ls.splits[idx].d[di].totalCompletions > 0 ) {
		ls.splits[idx].d[di].totalCompletions--;
	}

	/* Un-finish the run if it was finished */
	if ( ls.runFinished ) {
		int *comp;
		ls.runFinished = qfalse;
		LS_GetCatCompletions( &comp );
		if ( *comp > 0 ) ( *comp )--;
		/* Remove last history entry if it matches */
		if ( ls.numHistoryRuns > 0 ) {
			ls.numHistoryRuns--;
		}
	}

	/* Move currentMapIndex back to the undone split */
	ls.currentMapIndex = idx;
	LS_UpdateCurVisRow();

	ls.totalUndos++;
	Com_Printf( "^2LiveSplit: Undo split '%s' (total undos: %d)\n",
		ls.splits[idx].displayName ? ls.splits[idx].displayName : ls.splits[idx].mapname,
		ls.totalUndos );

	/* Notify external LiveSplit Server */
	LS_ExtSend( "unsplit" );

	LS_Save();
}

/* =====================================================================
   livesplit_skip  - skip the current split (mark done with 0 time)
   ===================================================================== */
static void LS_Skip_f( void ) {
	int idx, nextIdx;

	if ( !ls.initialized || !ls.active || ls.runFinished ) {
		Com_Printf( "^2LiveSplit: No active run / cannot skip\n" );
		return;
	}

	/* Find the current real (non-cutscene) split being timed */
	idx = ls.currentMapIndex;
	if ( idx < 0 || idx >= ls.numMaps ) {
		Com_Printf( "^2LiveSplit: No current split to skip\n" );
		return;
	}

	/* If on a cutscene, find the real split being timed */
	if ( ls.splits[idx].cutscene ) {
		if ( idx == ls.modeFirstIdx ) {
			idx = LS_NextRealSplit( idx );
		} else {
			idx = LS_PrevRealSplit( idx );
		}
		if ( idx < 0 || !LS_InActiveSet( idx ) ) {
			Com_Printf( "^2LiveSplit: No real split to skip\n" );
			return;
		}
	}

	if ( ls.splits[idx].splitDone ) {
		Com_Printf( "^2LiveSplit: Split already done\n" );
		return;
	}

	/* Skip: mark as done, preserve accumulated time in total - timer should not jump back */
	ls.splits[idx].splitDone     = qtrue;
	ls.splits[idx].splitSkipped  = qtrue;
	ls.runTotalIGTMs            += ls.splits[idx].currentTimeMs;
	ls.splits[idx].prevGoldMs    = 0;
	ls.splits[idx].goldFlashMs   = 0;
	/* Don't increment completions or update gold - it's a skip */

	/* 100% tracking: save per-map secrets/treasure and advance base */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		if ( ls.splits[idx].secretsFound == 0 && ls.splits[idx].treasureFound == 0 ) {
			int sf = ls.liveSecretsFound  - ls.baseSecretsFound;
			int tf = ls.liveTreasureFound - ls.baseTreasureFound;
			ls.splits[idx].secretsFound  = ( sf > 0 ) ? sf : 0;
			ls.splits[idx].treasureFound = ( tf > 0 ) ? tf : 0;
		}
		ls.baseSecretsFound  += ls.splits[idx].secretsFound;
		ls.baseTreasureFound += ls.splits[idx].treasureFound;
	}

	/* Advance to next real split if available */
	nextIdx = LS_NextRealSplit( idx );
	if ( nextIdx >= 0 && LS_InActiveSet( nextIdx ) ) {
		ls.currentMapIndex = nextIdx;
		LS_UpdateCurVisRow();
	}

	ls.totalSkips++;
	Com_Printf( "^2LiveSplit: Skipped split '%s' (total skips: %d)\n",
		ls.splits[idx].displayName ? ls.splits[idx].displayName : ls.splits[idx].mapname,
		ls.totalSkips );

	/* Notify external LiveSplit Server */
	{
		char gtBuf[64];
		Com_sprintf( gtBuf, sizeof( gtBuf ), "setgametime %.3f",
					 (float)ls.runTotalIGTMs / 1000.0f );
		LS_ExtSend( gtBuf );
		LS_ExtSend( "skipsplit" );
	}

	LS_Save();
}

static void LS_CheckSettings_f( void );
static void LS_ResetNoSave_f( void );
static void LS_ResetCategory_f( void );

/* =====================================================================
   Ghost System  - record & replay player positions per map
   ===================================================================== */

#define GHOST_MAX_FRAMES   18000    /* 15 minutes at 20Hz */
#define GHOST_SAMPLE_MS    50       /* 20Hz                */
#define GHOST_FILE_VERSION 1

typedef struct {
	int    timeMs;       /* ms of map IGT at this frame */
	float  x, y, z;     /* player origin               */
	float  yaw;          /* viewangles[YAW]             */
} ghostFrame_t;          /* 20 bytes                    */

static struct {
	/* Recording buffer: current run */
	ghostFrame_t rec[GHOST_MAX_FRAMES];
	int          recCount;
	int          lastRecMs;

	/* Playback buffer: loaded from file (best gold) */
	ghostFrame_t play[GHOST_MAX_FRAMES];
	int          playCount;
	qboolean     playLoaded;
	int          playSearchIdx;   /* cached search position */

	/* State */
	char         loadedMap[LS_MAX_MAPNAME];
	char         loadedCategory[32]; /* category string when ghost was loaded */
	cvar_t       *enableCvar;     /* ls_ghost 0/1 */
} ls_ghost;

/* Build a category-specific ghost file path.
   Fullgame:  ghostruns/fg/d<diff>/<mapname>.ghost
   Mission N: ghostruns/m<N>/d<diff>/<mapname>.ghost
   IL:        ghostruns/il/d<diff>/<mapname>.ghost  */
static void LS_GhostBuildPath( const char *mapname, char *out, int outSize ) {
	int diff = ls.currentDifficulty;
	if ( diff < 1 ) diff = 1;
	if ( diff > 3 ) diff = 3;

	switch ( ls.runMode ) {
	case LS_MODE_FULLGAME:
		Com_sprintf( out, outSize, "ghostruns/fg/d%d/%s.ghost", diff, mapname );
		break;
	case LS_MODE_MISSION:
		Com_sprintf( out, outSize, "ghostruns/m%d/d%d/%s.ghost", ls.runMission, diff, mapname );
		break;
	case LS_MODE_IL:
		Com_sprintf( out, outSize, "ghostruns/il/d%d/%s.ghost", diff, mapname );
		break;
	default:
		Com_sprintf( out, outSize, "ghostruns/fg/d%d/%s.ghost", diff, mapname );
		break;
	}
}

/* Build a string identifying the current category for cache invalidation */
static void LS_GhostCategoryStr( char *out, int outSize ) {
	int diff = ls.currentDifficulty;
	if ( diff < 1 ) diff = 1;
	if ( diff > 3 ) diff = 3;
	Com_sprintf( out, outSize, "%d_%d_d%d", ls.runMode, ls.runMission, diff );
}

/* Save ghost recording to file (per-category path) */
static void LS_GhostSave( const char *mapname ) {
	fileHandle_t f;
	char path[MAX_QPATH];
	int version = GHOST_FILE_VERSION;

	if ( ls_ghost.recCount <= 0 ) return;

	LS_GhostBuildPath( mapname, path, sizeof( path ) );
	f = FS_FOpenFileWrite( path );
	if ( !f ) {
		Com_Printf( "^1Ghost: failed to write %s\n", path );
		return;
	}

	FS_Write( &version, sizeof( int ), f );
	FS_Write( &ls_ghost.recCount, sizeof( int ), f );
	FS_Write( ls_ghost.rec, sizeof( ghostFrame_t ) * ls_ghost.recCount, f );
	FS_FCloseFile( f );

	Com_Printf( "^2Ghost: saved %d frames to %s\n", ls_ghost.recCount, path );
}

/* Load ghost data for a map */
static void LS_GhostLoad( const char *mapname ) {
	fileHandle_t f;
	char path[MAX_QPATH];
	int fileLen, version, count;

	ls_ghost.playCount     = 0;
	ls_ghost.playLoaded    = qfalse;
	ls_ghost.playSearchIdx = 0;
	ls_ghost.loadedMap[0]  = '\0';
	ls_ghost.loadedCategory[0] = '\0';

	LS_GhostBuildPath( mapname, path, sizeof( path ) );
	fileLen = FS_FOpenFileRead( path, &f, qtrue );
	if ( !f || fileLen <= 0 ) return;

	FS_Read( &version, sizeof( int ), f );
	if ( version != GHOST_FILE_VERSION ) {
		FS_FCloseFile( f );
		return;
	}

	FS_Read( &count, sizeof( int ), f );
	if ( count <= 0 || count > GHOST_MAX_FRAMES ) {
		FS_FCloseFile( f );
		return;
	}

	FS_Read( ls_ghost.play, sizeof( ghostFrame_t ) * count, f );
	FS_FCloseFile( f );

	ls_ghost.playCount  = count;
	ls_ghost.playLoaded = qtrue;
	Q_strncpyz( ls_ghost.loadedMap, mapname, sizeof( ls_ghost.loadedMap ) );
	LS_GhostCategoryStr( ls_ghost.loadedCategory, sizeof( ls_ghost.loadedCategory ) );
	Com_Printf( "^2Ghost: loaded %d frames from %s\n", count, path );
}

/* Called each active frame from LS_Frame */
static void LS_GhostFrame( void ) {
	int    mapIGT, curIdx;

	if ( !ls_ghost.enableCvar || !ls_ghost.enableCvar->integer ) {
		Cvar_Set( "ls_ghost_visible", "0" );
		return;
	}

	if ( !ls.active || ls.currentMapIndex < 0 ) {
		Cvar_Set( "ls_ghost_visible", "0" );
		/* Clear loadedMap so ghost reloads when run re-activates */
		ls_ghost.loadedMap[0] = '\0';
		ls_ghost.recCount     = 0;
		ls_ghost.playSearchIdx = 0;
		return;
	}

	curIdx = ls.currentMapIndex;
	if ( curIdx >= ls.numMaps ) { Cvar_Set( "ls_ghost_visible", "0" ); return; }
	if ( ls.splits[curIdx].cutscene ) { Cvar_Set( "ls_ghost_visible", "0" ); return; }

	mapIGT = ls.splits[curIdx].currentTimeMs;

	/* --- Load ghost data on map or category change --- */
	{
		char curCat[32];
		LS_GhostCategoryStr( curCat, sizeof( curCat ) );
		if ( Q_stricmp( ls_ghost.loadedMap, ls.splits[curIdx].mapname ) != 0 ||
			 Q_stricmp( ls_ghost.loadedCategory, curCat ) != 0 ) {
			LS_GhostLoad( ls.splits[curIdx].mapname );
			/* Mark map+category as loaded even if no file exists, so we don't
			   re-enter this block every frame and reset recCount.   */
			Q_strncpyz( ls_ghost.loadedMap, ls.splits[curIdx].mapname,
				sizeof( ls_ghost.loadedMap ) );
			Q_strncpyz( ls_ghost.loadedCategory, curCat,
				sizeof( ls_ghost.loadedCategory ) );
			ls_ghost.recCount  = 0;
			ls_ghost.lastRecMs = -GHOST_SAMPLE_MS;
		}
	}

	/* --- Detect timer rollback (savegame load) or restart (IL replay) --- */
	/* If mapIGT dropped far below our last recorded time, trim
	   recording back to the rollback point and rewind playback.   */
	if ( mapIGT < ls_ghost.lastRecMs - GHOST_SAMPLE_MS ) {
		int gi;
		for ( gi = ls_ghost.recCount - 1; gi >= 0; gi-- ) {
			if ( ls_ghost.rec[gi].timeMs <= mapIGT ) break;
		}
		ls_ghost.recCount  = gi + 1;
		ls_ghost.lastRecMs = ( gi >= 0 ) ? ls_ghost.rec[gi].timeMs
										 : -GHOST_SAMPLE_MS;
		ls_ghost.playSearchIdx = 0;
	}

	/* --- Recording: sample position at 20Hz --- */
	if ( cls.state >= CA_ACTIVE &&
		 mapIGT - ls_ghost.lastRecMs >= GHOST_SAMPLE_MS &&
		 ls_ghost.recCount < GHOST_MAX_FRAMES ) {
		ghostFrame_t *gf = &ls_ghost.rec[ls_ghost.recCount];
		gf->timeMs = mapIGT;
		gf->x   = cl.snap.ps.origin[0];
		gf->y   = cl.snap.ps.origin[1];
		gf->z   = cl.snap.ps.origin[2];
		gf->yaw = cl.snap.ps.viewangles[1];
		ls_ghost.recCount++;
		ls_ghost.lastRecMs = mapIGT;
	}

	/* --- Playback: interpolate ghost position for current mapIGT --- */
	if ( ls_ghost.playLoaded && ls_ghost.playCount > 0 ) {
		int   i;
		float gx, gy, gz, gyaw;
		float speed = 0.0f;

		if ( mapIGT <= 0 || mapIGT <= ls_ghost.play[0].timeMs ) {
			gx   = ls_ghost.play[0].x;
			gy   = ls_ghost.play[0].y;
			gz   = ls_ghost.play[0].z;
			gyaw = ls_ghost.play[0].yaw;
		} else if ( mapIGT >= ls_ghost.play[ls_ghost.playCount - 1].timeMs ) {
			/* Past end - freeze at last position */
			gx   = ls_ghost.play[ls_ghost.playCount - 1].x;
			gy   = ls_ghost.play[ls_ghost.playCount - 1].y;
			gz   = ls_ghost.play[ls_ghost.playCount - 1].z;
			gyaw = ls_ghost.play[ls_ghost.playCount - 1].yaw;
		} else {
			/* Advance cached search index */
			i = ls_ghost.playSearchIdx;
			if ( i >= ls_ghost.playCount - 1 ) i = 0;
			while ( i < ls_ghost.playCount - 1 && ls_ghost.play[i + 1].timeMs < mapIGT ) {
				i++;
			}
			ls_ghost.playSearchIdx = i;

			{
				ghostFrame_t *a = &ls_ghost.play[i];
				ghostFrame_t *b = &ls_ghost.play[i + 1];
				float dt, dx, dy;
				float frac = ( b->timeMs == a->timeMs ) ? 0.0f
					: (float)( mapIGT - a->timeMs ) / (float)( b->timeMs - a->timeMs );
				gx = a->x + frac * ( b->x - a->x );
				gy = a->y + frac * ( b->y - a->y );
				gz = a->z + frac * ( b->z - a->z );
				{
					float da = b->yaw - a->yaw;
					if ( da > 180.0f ) da -= 360.0f;
					if ( da < -180.0f ) da += 360.0f;
					gyaw = a->yaw + frac * da;
				}
				/* Compute horizontal speed (units/sec) */
				dt = (float)( b->timeMs - a->timeMs );
				dx = b->x - a->x;
				dy = b->y - a->y;
				if ( dt > 0.0f ) {
					speed = sqrtf( dx * dx + dy * dy ) / ( dt / 1000.0f );
				}
			}
		}

		Cvar_Set( "ls_ghost_visible", "1" );
		Cvar_Set( "ls_ghost_x", va( "%f", gx ) );
		Cvar_Set( "ls_ghost_y", va( "%f", gy ) );
		Cvar_Set( "ls_ghost_z", va( "%f", gz ) );
		Cvar_Set( "ls_ghost_yaw", va( "%f", gyaw ) );
		Cvar_Set( "ls_ghost_speed", va( "%f", speed ) );
	} else {
		Cvar_Set( "ls_ghost_visible", "0" );
	}
}

/* Called when a gold split is achieved - saves recording as new best */
static void LS_GhostOnGoldSplit( const char *mapname ) {
	if ( !ls_ghost.enableCvar || !ls_ghost.enableCvar->integer ) return;
	if ( ls_ghost.recCount <= 0 ) return;

	LS_GhostSave( mapname );

	/* Update playback buffer with new recording */
	memcpy( ls_ghost.play, ls_ghost.rec,
		sizeof( ghostFrame_t ) * ls_ghost.recCount );
	ls_ghost.playCount     = ls_ghost.recCount;
	ls_ghost.playLoaded    = qtrue;
	ls_ghost.playSearchIdx = 0;
	Q_strncpyz( ls_ghost.loadedMap, mapname, sizeof( ls_ghost.loadedMap ) );
	LS_GhostCategoryStr( ls_ghost.loadedCategory, sizeof( ls_ghost.loadedCategory ) );
}

/* =====================================================================
   Split Records Viewer  (UI tab "Records")
   Populates cvars that the menu displays.  Engine commands let the menu
   switch mode/difficulty/mission/page and reset individual golds.
   ===================================================================== */

#define SV_ROWS_PER_PAGE 15

/* Maps each visible display row -> split index in ls.splits[] */
static int  sv_rowMap[SV_ROWS_PER_PAGE];
static int  sv_numRows;           /* rows populated on current page */
static int  sv_viewMode   = 0;    /* LS_MODE_FULLGAME/MISSION/IL */
static int  sv_viewDiff   = 2;    /* g_gameskill 1-3 */
static int  sv_viewMs     = 1;    /* mission group 1-5 */
static int  sv_curPage    = 0;
static int  sv_totalPages = 1;

/* Compute diff slot: mode * 3 + (skill-1) */
static int SV_DiffSlot( void ) {
	int skill = sv_viewDiff;
	int mode  = sv_viewMode;
	if ( skill < 1 ) skill = 1;
	if ( skill > 3 ) skill = 3;
	if ( mode < 0 )  mode  = 0;
	if ( mode >= LS_NUM_MODES ) mode = LS_NUM_MODES - 1;
	return mode * LS_MAX_DIFFICULTIES + ( skill - 1 );
}

/* Gather visible (non-cutscene) split indices for the viewed category */
static int sv_allVis[LS_MAX_MAPS];
static int sv_allVisN;

static void SV_GatherVisible( void ) {
	int i, first = 0, last = ls.numMaps - 1;

	sv_allVisN = 0;

	switch ( sv_viewMode ) {
	case LS_MODE_MISSION: {
		int ms = sv_viewMs;
		int grpFirst, grpLast;
		if ( ms < 1 ) ms = 1;
		if ( ms > LS_NUM_MISSION_GROUPS ) ms = LS_NUM_MISSION_GROUPS;
		grpFirst = ls_missionGroups[ms - 1].firstMission;
		grpLast  = ls_missionGroups[ms - 1].lastMission;
		for ( i = 0; i < ls.numMaps; i++ ) {
			if ( ls.splits[i].mission >= grpFirst &&
				 ls.splits[i].mission <= grpLast &&
				 !ls.splits[i].cutscene ) {
				sv_allVis[sv_allVisN++] = i;
			}
		}
		return;
	}
	case LS_MODE_IL: {
		/* IL: show all non-cutscene maps so user can see all IL bests */
		for ( i = 0; i < ls.numMaps; i++ ) {
			if ( !ls.splits[i].cutscene )
				sv_allVis[sv_allVisN++] = i;
		}
		return;
	}
	default: /* FULLGAME */
		for ( i = first; i <= last && i < ls.numMaps; i++ ) {
			if ( !ls.splits[i].cutscene )
				sv_allVis[sv_allVisN++] = i;
		}
		return;
	}
}

static void SV_Refresh( void ) {
	int di, i, row, pageStart, pageEnd;
	char buf[256], tbuf[32];
	const char *modeName, *diffName;

	if ( !ls.initialized ) return;

	di = SV_DiffSlot();

	SV_GatherVisible();

	/* Pagination */
	sv_totalPages = ( sv_allVisN + SV_ROWS_PER_PAGE - 1 ) / SV_ROWS_PER_PAGE;
	if ( sv_totalPages < 1 ) sv_totalPages = 1;
	if ( sv_curPage >= sv_totalPages ) sv_curPage = sv_totalPages - 1;
	if ( sv_curPage < 0 ) sv_curPage = 0;

	pageStart = sv_curPage * SV_ROWS_PER_PAGE;
	pageEnd   = pageStart + SV_ROWS_PER_PAGE;
	if ( pageEnd > sv_allVisN ) pageEnd = sv_allVisN;

	/* Title */
	switch ( sv_viewMode ) {
	case LS_MODE_MISSION: {
		int gi = sv_viewMs - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		modeName = ls_missionGroups[gi].name;
		break;
	}
	case LS_MODE_IL: modeName = "Individual Level"; break;
	default:         modeName = "Full Game"; break;
	}

	switch ( sv_viewDiff ) {
	case 1:  diffName = "Don't hurt me."; break;
	case 3:  diffName = "I am Death incarnate!"; break;
	default: diffName = "Bring 'em on!"; break;
	}

	Com_sprintf( buf, sizeof( buf ), "%s - %s", modeName, diffName );
	Cvar_Set( "ls_sv_title", buf );

	/* Category stats */
	{
		int att = 0, comp = 0, pb = 0, pbRgt = 0;
		char pbBuf[32], rgtBuf[32];

		switch ( sv_viewMode ) {
		case LS_MODE_MISSION: {
			int gi = sv_viewMs - 1;
			int dbase = sv_viewDiff - 1;
			if ( gi < 0 ) gi = 0;
			if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
			if ( dbase < 0 ) dbase = 0;
			if ( dbase >= LS_MAX_DIFFICULTIES ) dbase = LS_MAX_DIFFICULTIES - 1;
			att  = ls.msAttempts[gi][dbase];
			comp = ls.msCompletions[gi][dbase];
			pb   = ls.msPB[gi][dbase];
			pbRgt = ls.msPBRgt[gi][dbase];
			break;
		}
		case LS_MODE_IL:
			/* IL PB is per-split; show aggregate attempts */
			att = 0; comp = 0; pb = 0; pbRgt = 0;
			break;
		default: {
			int dbase = sv_viewDiff - 1;
			if ( dbase < 0 ) dbase = 0;
			if ( dbase >= LS_MAX_DIFFICULTIES ) dbase = LS_MAX_DIFFICULTIES - 1;
			att   = ls.fgAttempts[dbase];
			comp  = ls.fgCompletions[dbase];
			pb    = ls.fgPB[dbase];
			pbRgt = ls.fgPBRgt[dbase];
			break;
		}
		}

		if ( pb > 0 ) LS_FormatTime( pb, pbBuf, sizeof( pbBuf ) );
		else Q_strncpyz( pbBuf, "---", sizeof( pbBuf ) );

		if ( pbRgt > 0 ) LS_FormatTime( pbRgt, rgtBuf, sizeof( rgtBuf ) );
		else Q_strncpyz( rgtBuf, "---", sizeof( rgtBuf ) );

		if ( sv_viewMode == LS_MODE_IL ) {
			Com_sprintf( buf, sizeof( buf ), "Per-split PBs shown below" );
		} else {
			int sobMs = 0;
			qboolean hasSob = qfalse;
			char sobBuf[32];

			/* Calculate Sum of Best for the viewed category */
			{
				int si, first = 0, last = ls.numMaps - 1;
				hasSob = qtrue;
				switch ( sv_viewMode ) {
				case LS_MODE_MISSION: {
					int ms = sv_viewMs;
					if ( ms < 1 ) ms = 1;
					if ( ms > LS_NUM_MISSION_GROUPS ) ms = LS_NUM_MISSION_GROUPS;
					for ( si = 0; si < ls.numMaps; si++ ) {
						int grpFirst = ls_missionGroups[ms - 1].firstMission;
						int grpLast  = ls_missionGroups[ms - 1].lastMission;
						if ( ls.splits[si].mission >= grpFirst &&
							 ls.splits[si].mission <= grpLast &&
							 !ls.splits[si].cutscene ) {
							int g = ls.splits[si].d[di].bestTimeMs;
							if ( g > 0 ) sobMs += g;
							else hasSob = qfalse;
						}
					}
					break;
				}
				default:
					for ( si = first; si <= last && si < ls.numMaps; si++ ) {
						if ( !ls.splits[si].cutscene ) {
							int g = ls.splits[si].d[di].bestTimeMs;
							if ( g > 0 ) sobMs += g;
							else hasSob = qfalse;
						}
					}
					break;
				}
			}

			if ( hasSob && sobMs > 0 ) LS_FormatTime( sobMs, sobBuf, sizeof( sobBuf ) );
			else Q_strncpyz( sobBuf, "---", sizeof( sobBuf ) );

			Com_sprintf( buf, sizeof( buf ),
				"PB: %s  SOB: %s  Att: %d  Comp: %d",
				pbBuf, sobBuf, att, comp );
		}
		Cvar_Set( "ls_sv_stats", buf );
	}

	/* Page indicator */
	Com_sprintf( buf, sizeof( buf ), "Page %d / %d", sv_curPage + 1, sv_totalPages );
	Cvar_Set( "ls_sv_pages", buf );

	/* Populate rows */
	sv_numRows = 0;
	for ( row = 0; row < SV_ROWS_PER_PAGE; row++ ) {
		char rowCvar[32];
		Com_sprintf( rowCvar, sizeof( rowCvar ), "ls_sv_r%d", row );

		i = pageStart + row;
		if ( i < pageEnd ) {
			int si    = sv_allVis[i];
			int gold  = ls.splits[si].d[di].bestTimeMs;
			int pbseg = ls.splits[si].d[di].pbSegmentMs;
			int att   = ls.splits[si].d[di].totalAttempts;
			int comp  = ls.splits[si].d[di].totalCompletions;
			const char *name = ls.splits[si].shortName
				? ls.splits[si].shortName
				: ls.splits[si].mapname;
			char goldBuf[24], pbBuf[24];

			if ( gold > 0 ) LS_FormatTime( gold, goldBuf, sizeof( goldBuf ) );
			else Q_strncpyz( goldBuf, "---", sizeof( goldBuf ) );

			if ( pbseg > 0 ) LS_FormatTime( pbseg, pbBuf, sizeof( pbBuf ) );
			else Q_strncpyz( pbBuf, "---", sizeof( pbBuf ) );

			Com_sprintf( buf, sizeof( buf ), "%-11s %-10s %-10s %3d/%-3d",
				name, goldBuf, pbBuf, att, comp );
			Cvar_Set( rowCvar, buf );
			sv_rowMap[row] = si;
			sv_numRows++;
		} else {
			Cvar_Set( rowCvar, "" );
			sv_rowMap[row] = -1;
		}
	}
}

/* Console commands for the Records viewer */
static void LS_SvRefresh_f( void ) {
	SV_Refresh();
}

static void LS_SvMode_f( void ) {
	if ( Cmd_Argc() < 2 ) return;
	sv_viewMode = atoi( Cmd_Argv( 1 ) );
	if ( sv_viewMode < 0 ) sv_viewMode = 0;
	if ( sv_viewMode >= LS_NUM_MODES ) sv_viewMode = LS_NUM_MODES - 1;
	sv_curPage = 0;
	SV_Refresh();
}

static void LS_SvDiff_f( void ) {
	if ( Cmd_Argc() < 2 ) return;
	sv_viewDiff = atoi( Cmd_Argv( 1 ) );
	if ( sv_viewDiff < 1 ) sv_viewDiff = 1;
	if ( sv_viewDiff > 3 ) sv_viewDiff = 3;
	sv_curPage = 0;
	SV_Refresh();
}

static void LS_SvMission_f( void ) {
	if ( Cmd_Argc() < 2 ) return;
	sv_viewMs = atoi( Cmd_Argv( 1 ) );
	if ( sv_viewMs < 1 ) sv_viewMs = 1;
	if ( sv_viewMs > LS_NUM_MISSION_GROUPS ) sv_viewMs = LS_NUM_MISSION_GROUPS;
	sv_curPage = 0;
	SV_Refresh();
}

static void LS_SvPageNext_f( void ) {
	if ( sv_curPage < sv_totalPages - 1 ) {
		sv_curPage++;
		SV_Refresh();
	}
}

static void LS_SvPagePrev_f( void ) {
	if ( sv_curPage > 0 ) {
		sv_curPage--;
		SV_Refresh();
	}
}

static void LS_SvResetGold_f( void ) {
	int row, si, di;
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: livesplit_sv_reset_gold <row>\n" );
		return;
	}
	row = atoi( Cmd_Argv( 1 ) );
	if ( row < 0 || row >= sv_numRows ) {
		Com_Printf( "^1Invalid row %d\n", row );
		return;
	}
	si = sv_rowMap[row];
	if ( si < 0 || si >= ls.numMaps ) return;
	di = SV_DiffSlot();

	LS_BackupFile( LS_SAVE_FILE );
	ls.splits[si].d[di].bestTimeMs = 0;
	Com_Printf( "^2LiveSplit: Gold reset for '%s'\n",
		ls.splits[si].displayName ? ls.splits[si].displayName : ls.splits[si].mapname );
	LS_Save();
	SV_Refresh();
}

static void LS_SvResetPBSeg_f( void ) {
	int row, si, di;
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: livesplit_sv_reset_pbseg <row>\n" );
		return;
	}
	row = atoi( Cmd_Argv( 1 ) );
	if ( row < 0 || row >= sv_numRows ) {
		Com_Printf( "^1Invalid row %d\n", row );
		return;
	}
	si = sv_rowMap[row];
	if ( si < 0 || si >= ls.numMaps ) return;
	di = SV_DiffSlot();

	LS_BackupFile( LS_SAVE_FILE );
	ls.splits[si].d[di].pbSegmentMs = 0;
	Com_Printf( "^2LiveSplit: PB segment reset for '%s'\n",
		ls.splits[si].displayName ? ls.splits[si].displayName : ls.splits[si].mapname );
	LS_Save();
	SV_Refresh();
}

static void LS_SvResetSplitAll_f( void ) {
	int row, si, di;
	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: livesplit_sv_reset_split <row>\n" );
		return;
	}
	row = atoi( Cmd_Argv( 1 ) );
	if ( row < 0 || row >= sv_numRows ) {
		Com_Printf( "^1Invalid row %d\n", row );
		return;
	}
	si = sv_rowMap[row];
	if ( si < 0 || si >= ls.numMaps ) return;
	di = SV_DiffSlot();

	LS_BackupFile( LS_SAVE_FILE );
	ls.splits[si].d[di].bestTimeMs      = 0;
	ls.splits[si].d[di].pbSegmentMs     = 0;
	ls.splits[si].d[di].totalAttempts   = 0;
	ls.splits[si].d[di].totalCompletions = 0;
	Com_Printf( "^2LiveSplit: All stats reset for '%s'\n",
		ls.splits[si].displayName ? ls.splits[si].displayName : ls.splits[si].mapname );
	LS_Save();
	SV_Refresh();
}

static void LS_SvResetCat_f( void ) {
	int di, dbase, i;
	LS_BackupFile( LS_SAVE_FILE );
	di    = SV_DiffSlot();
	dbase = sv_viewDiff - 1;
	if ( dbase < 0 ) dbase = 0;
	if ( dbase >= LS_MAX_DIFFICULTIES ) dbase = LS_MAX_DIFFICULTIES - 1;

	/* Clear per-split data for all visible splits in this category */
	SV_GatherVisible();
	for ( i = 0; i < sv_allVisN; i++ ) {
		int si = sv_allVis[i];
		ls.splits[si].d[di].bestTimeMs      = 0;
		ls.splits[si].d[di].pbSegmentMs     = 0;
		ls.splits[si].d[di].totalAttempts   = 0;
		ls.splits[si].d[di].totalCompletions = 0;
	}

	/* Clear category-level stats */
	switch ( sv_viewMode ) {
	case LS_MODE_MISSION: {
		int gi = sv_viewMs - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		ls.msAttempts[gi][dbase]    = 0;
		ls.msCompletions[gi][dbase] = 0;
		ls.msPB[gi][dbase]          = 0;
		ls.msPBRgt[gi][dbase]       = 0;
		break;
	}
	case LS_MODE_IL:
		/* IL has no category-level stats beyond per-split */
		break;
	default:
		ls.fgAttempts[dbase]    = 0;
		ls.fgCompletions[dbase] = 0;
		ls.fgPB[dbase]          = 0;
		ls.fgPBRgt[dbase]       = 0;
		break;
	}

	Com_Printf( "^2LiveSplit: Category stats reset\n" );
	LS_Save();
	SV_Refresh();
}

static void SV_InitCvars( void ) {
	int i;
	char name[32];
	Cvar_Get( "ls_sv_title", "", 0 );
	Cvar_Get( "ls_sv_stats", "", 0 );
	Cvar_Get( "ls_sv_pages", "", 0 );
	for ( i = 0; i < SV_ROWS_PER_PAGE; i++ ) {
		Com_sprintf( name, sizeof( name ), "ls_sv_r%d", i );
		Cvar_Get( name, "", 0 );
	}
}

static void SV_InitCommands( void ) {
	Cmd_AddCommand( "livesplit_sv_refresh",    LS_SvRefresh_f );
	Cmd_AddCommand( "livesplit_sv_mode",       LS_SvMode_f );
	Cmd_AddCommand( "livesplit_sv_diff",       LS_SvDiff_f );
	Cmd_AddCommand( "livesplit_sv_mission",    LS_SvMission_f );
	Cmd_AddCommand( "livesplit_sv_pgup",       LS_SvPagePrev_f );
	Cmd_AddCommand( "livesplit_sv_pgdn",       LS_SvPageNext_f );
	Cmd_AddCommand( "livesplit_sv_reset_gold", LS_SvResetGold_f );
	Cmd_AddCommand( "livesplit_sv_reset_pbseg", LS_SvResetPBSeg_f );
	Cmd_AddCommand( "livesplit_sv_reset_split", LS_SvResetSplitAll_f );
	Cmd_AddCommand( "livesplit_sv_reset_cat",  LS_SvResetCat_f );
}

void SCR_LiveSplitInit( void ) {
	cg_livesplit   = Cvar_Get( "cg_livesplit", "0",   CVAR_ARCHIVE );
	ls_modeCvar    = Cvar_Get( "ls_mode",      "0",   CVAR_ARCHIVE );
	ls_missionCvar = Cvar_Get( "ls_mission",   "1",   CVAR_ARCHIVE );
	ls_mapCvar     = Cvar_Get( "ls_map",       "",    CVAR_ARCHIVE );
	ls_xCvar       = Cvar_Get( "ls_x",         "8",   CVAR_ARCHIVE );
	ls_yCvar       = Cvar_Get( "ls_y",         "80",  CVAR_ARCHIVE );
	ls_wCvar       = Cvar_Get( "ls_w",         "178", CVAR_ARCHIVE );
	ls_scaleCvar   = Cvar_Get( "ls_scale",     "1.0", CVAR_ARCHIVE );
	ls_opacityCvar = Cvar_Get( "ls_opacity",   "1.0", CVAR_ARCHIVE );
	ls_opacityUiCvar = Cvar_Get( "ls_opacity_ui", "0.2", CVAR_ARCHIVE );
	ls_bgalphaCvar = Cvar_Get( "ls_bgalpha", "0.82", CVAR_ARCHIVE );
	ls_alignCvar   = Cvar_Get( "ls_align",   "0",    CVAR_ARCHIVE );
	ls_showheaderCvar = Cvar_Get( "ls_showheader", "1", CVAR_ARCHIVE );
	ls_showstatsCvar  = Cvar_Get( "ls_showstats",  "1", CVAR_ARCHIVE );
	ls_showsegCvar    = Cvar_Get( "ls_showseg",    "1", CVAR_ARCHIVE );
	ls_showrgtCvar    = Cvar_Get( "ls_showrgt",    "1", CVAR_ARCHIVE );
	ls_maxrowsCvar    = Cvar_Get( "ls_maxrows",    "0", CVAR_ARCHIVE ); /* 0 = auto */
	ls_showpbCvar     = Cvar_Get( "ls_showpb",     "1", CVAR_ARCHIVE );
	ls_showbestCvar   = Cvar_Get( "ls_showbest",   "1", CVAR_ARCHIVE );
	ls_showtimerCvar  = Cvar_Get( "ls_showtimer",  "1", CVAR_ARCHIVE );
	ls_showsepsCvar   = Cvar_Get( "ls_showseps",   "1", CVAR_ARCHIVE );
	ls_showdeltasCvar = Cvar_Get( "ls_showdeltas", "1", CVAR_ARCHIVE );
	ls_showbestdeltasCvar = Cvar_Get( "ls_showbestdeltas", "1", CVAR_ARCHIVE );
	ls_showattCvar    = Cvar_Get( "ls_showatt",    "1", CVAR_ARCHIVE );
	ls_drawCvar       = Cvar_Get( "ls_draw",       "1", CVAR_ARCHIVE );
	ls_100pctCvar     = Cvar_Get( "ls_100pct",     "0", CVAR_ARCHIVE );

	/* Color customization */
	ls_clr_aheadCvar  = Cvar_Get( "ls_clr_ahead",  "",  CVAR_ARCHIVE );
	ls_clr_behindCvar = Cvar_Get( "ls_clr_behind", "",  CVAR_ARCHIVE );
	ls_clr_goldCvar   = Cvar_Get( "ls_clr_gold",   "",  CVAR_ARCHIVE );
	ls_clr_headerCvar = Cvar_Get( "ls_clr_header", "",  CVAR_ARCHIVE );
	ls_clr_timerCvar  = Cvar_Get( "ls_clr_timer",  "",  CVAR_ARCHIVE );
	ls_clr_textCvar   = Cvar_Get( "ls_clr_text",   "",  CVAR_ARCHIVE );
	ls_clr_bgCvar     = Cvar_Get( "ls_clr_bg",     "",  CVAR_ARCHIVE );
	ls_clr_borderCvar = Cvar_Get( "ls_clr_border", "",  CVAR_ARCHIVE );
	ls_clr_mapnameCvar = Cvar_Get( "ls_clr_mapname", "", CVAR_ARCHIVE );
	ls_clr_currentCvar = Cvar_Get( "ls_clr_current", "", CVAR_ARCHIVE );
	ls_clr_completedCvar = Cvar_Get( "ls_clr_completed", "", CVAR_ARCHIVE );
	ls_clr_futureCvar  = Cvar_Get( "ls_clr_future",  "",  CVAR_ARCHIVE );
	ls_clr_dimCvar     = Cvar_Get( "ls_clr_dim",     "",  CVAR_ARCHIVE );
	ls_clr_segtimerCvar = Cvar_Get( "ls_clr_segtimer", "", CVAR_ARCHIVE );
	ls_clr_pausedCvar  = Cvar_Get( "ls_clr_paused",  "",  CVAR_ARCHIVE );
	ls_clr_sepCvar     = Cvar_Get( "ls_clr_sep",     "",  CVAR_ARCHIVE );
	ls_clr_highlightCvar = Cvar_Get( "ls_clr_highlight", "", CVAR_ARCHIVE );
	ls_clr_labelCvar   = Cvar_Get( "ls_clr_label",   "",  CVAR_ARCHIVE );

	ls_text_shadowCvar = Cvar_Get( "ls_text_shadow", "0", CVAR_ARCHIVE );

	/* External LiveSplit type */
	ls_typeCvar    = Cvar_Get( "ls_type",    "0",   CVAR_ARCHIVE );

	/* Standalone IGT timer overlay */
	ls_igttimerCvar       = Cvar_Get( "ls_igttimer",       "0",     CVAR_ARCHIVE );
	ls_igttimer_xCvar     = Cvar_Get( "ls_igttimer_x",     "280",   CVAR_ARCHIVE );
	ls_igttimer_yCvar     = Cvar_Get( "ls_igttimer_y",     "440",   CVAR_ARCHIVE );
	ls_igttimer_scaleCvar = Cvar_Get( "ls_igttimer_scale", "1.0",   CVAR_ARCHIVE );
	ls_igtsegtimer        = Cvar_Get( "ls_igtsegtimer",   "0",     CVAR_ARCHIVE );

	/* Keystroke overlay position/scale cvars */
	ks_xCvar       = Cvar_Get( "ks_x",       "0",   CVAR_ARCHIVE );  /* 0 = auto center */
	ks_yCvar       = Cvar_Get( "ks_y",       "0",   CVAR_ARCHIVE );  /* 0 = auto above statusbar */
	ks_scaleCvar   = Cvar_Get( "ks_scale",   "1.0", CVAR_ARCHIVE );
	ks_opacityCvar = Cvar_Get( "ks_opacity", "1.0", CVAR_ARCHIVE );
	ks_mouseCvar   = Cvar_Get( "ks_mouse",   "0",   CVAR_ARCHIVE );  /* 0=off, 1=clicks, 2=clicks+dir */

	/* Ghost system cvar */
	ls_ghost.enableCvar = Cvar_Get( "ls_ghost", "1", CVAR_ARCHIVE );

	/* Demo auto-record cvar */
	sp_autorecordCvar = Cvar_Get( "sp_autorecord", "0", CVAR_ARCHIVE );
	sp_demofpsCvar    = Cvar_Get( "sp_demofps",    "40", CVAR_ARCHIVE );

	LS_BuildSplitTable();
	LS_Load();
	LS_LoadHistory();

	/* Restore mode from save or use cvar defaults */
	if ( ls.active ) {
		/* active run: use saved mode/mission */
		LS_SetupMode( ls.runMode, ls.runMission );
	} else {
		LS_SetupMode( ls_modeCvar->integer, ls_missionCvar->integer );
	}

	if ( ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps ) {
		Q_strncpyz( ls.prevMapname, ls.splits[ls.currentMapIndex].mapname, LS_MAX_MAPNAME );
	}

	Cmd_AddCommand( "livesplit_start", LS_Start_f );
	Cmd_AddCommand( "livesplit_reset", LS_Reset_f );
	Cmd_AddCommand( "livesplit_reset_nosave", LS_ResetNoSave_f );
	Cmd_AddCommand( "livesplit_reset_save", LS_ResetConfirmSave_f );
	Cmd_AddCommand( "livesplit_reset_discard", LS_ResetConfirmDiscard_f );
	Cmd_AddCommand( "livesplit_reset_cancel", LS_ResetCancel_f );
	Cmd_AddCommand( "livesplit_reset_bests", LS_ResetBests_f );
	Cmd_AddCommand( "livesplit_reset_category", LS_ResetCategory_f );
	Cmd_AddCommand( "livesplit_pause", LS_Pause_f );
	Cmd_AddCommand( "livesplit_undo", LS_Undo_f );
	Cmd_AddCommand( "livesplit_skip", LS_Skip_f );
	Cmd_AddCommand( "livesplit_check", LS_CheckSettings_f );

	/* Split Records viewer */
	SV_InitCvars();
	SV_InitCommands();

	Com_Printf( "^2LiveSplit: Initialized (%d maps, %d visible)\n", ls.numMaps, ls.numVisible );


}

void SCR_LiveSplitShutdown( void ) {
	LS_ExtDisconnect();
	if ( ls.initialized ) {
		LS_Save();
		LS_SaveHistory();
	}
	Cmd_RemoveCommand( "livesplit_start" );
	Cmd_RemoveCommand( "livesplit_reset" );
	Cmd_RemoveCommand( "livesplit_reset_nosave" );
	Cmd_RemoveCommand( "livesplit_reset_save" );
	Cmd_RemoveCommand( "livesplit_reset_discard" );
	Cmd_RemoveCommand( "livesplit_reset_cancel" );
	Cmd_RemoveCommand( "livesplit_reset_bests" );
	Cmd_RemoveCommand( "livesplit_reset_category" );
	Cmd_RemoveCommand( "livesplit_pause" );
	Cmd_RemoveCommand( "livesplit_undo" );
	Cmd_RemoveCommand( "livesplit_skip" );
	Cmd_RemoveCommand( "livesplit_check" );

	/* Split Records viewer */
	Cmd_RemoveCommand( "livesplit_sv_refresh" );
	Cmd_RemoveCommand( "livesplit_sv_mode" );
	Cmd_RemoveCommand( "livesplit_sv_diff" );
	Cmd_RemoveCommand( "livesplit_sv_mission" );
	Cmd_RemoveCommand( "livesplit_sv_pgup" );
	Cmd_RemoveCommand( "livesplit_sv_pgdn" );
	Cmd_RemoveCommand( "livesplit_sv_reset_gold" );
	Cmd_RemoveCommand( "livesplit_sv_reset_pbseg" );
	Cmd_RemoveCommand( "livesplit_sv_reset_split" );
	Cmd_RemoveCommand( "livesplit_sv_reset_cat" );
}

/* =====================================================================
   Settings validation - checks all gameplay cvars against defaults
   ===================================================================== */

typedef struct {
	const char  *name;
	const char  *defaultVal;
	const char  *category;   /* "Movement", "Player Weapons", "AI Weapons", "Misc" */
} lsCvarCheck_t;

static const lsCvarCheck_t ls_settingsTable[] = {
	/* Movement / Physics */
	{ "g_speed",                       "320",    "Movement" },
	{ "g_gravity",                     "800",    "Movement" },
	{ "timescale",                     "1",      "Movement" },
	{ "g_knockback",                   "1000",   "Movement" },

	/* Player Weapons */
	{ "sk_plr_dmg_knife",              "5",      "Player Weapons" },
	{ "sk_plr_dmg_kick",               "15",     "Player Weapons" },
	{ "sk_plr_dmg_luger",              "6",      "Player Weapons" },
	{ "sk_plr_dmg_colt",               "8",      "Player Weapons" },
	{ "sk_plr_dmg_mp40",               "6",      "Player Weapons" },
	{ "sk_plr_dmg_thompson",           "8",      "Player Weapons" },
	{ "sk_plr_dmg_sten",               "10",     "Player Weapons" },
	{ "sk_plr_dmg_mauser",             "20",     "Player Weapons" },
	{ "sk_plr_dmg_sniperrifle",        "55",     "Player Weapons" },
	{ "sk_plr_dmg_garand",             "25",     "Player Weapons" },
	{ "sk_plr_dmg_snooperscope",       "25",     "Player Weapons" },
	{ "sk_plr_dmg_fg42",               "20",     "Player Weapons" },
	{ "sk_plr_dmg_fg42scope",          "35",     "Player Weapons" },
	{ "sk_plr_dmg_panzerfaust",        "200",    "Player Weapons" },
	{ "sk_plr_dmg_panzerfaust_splash", "200",    "Player Weapons" },
	{ "sk_plr_dmg_venom",              "12",     "Player Weapons" },
	{ "sk_plr_dmg_flamethrower",       "2",      "Player Weapons" },
	{ "sk_plr_dmg_tesla",              "8",      "Player Weapons" },
	{ "sk_plr_dmg_grenade",            "200",    "Player Weapons" },
	{ "sk_plr_dmg_grenade_radius",     "150",    "Player Weapons" },
	{ "sk_plr_dmg_pineapple",          "160",    "Player Weapons" },
	{ "sk_plr_dmg_pineapple_radius",   "300",    "Player Weapons" },
	{ "sk_plr_dmg_dynamite",           "800",    "Player Weapons" },
	{ "sk_plr_dmg_dynamite_radius",    "400",    "Player Weapons" },

	/* AI Weapons */
	{ "sk_ai_dmg_knife",               "5",      "AI Weapons" },
	{ "sk_ai_dmg_luger",               "6",      "AI Weapons" },
	{ "sk_ai_dmg_colt",                "8",      "AI Weapons" },
	{ "sk_ai_dmg_mp40",                "6",      "AI Weapons" },
	{ "sk_ai_dmg_thompson",            "8",      "AI Weapons" },
	{ "sk_ai_dmg_sten",                "8",      "AI Weapons" },
	{ "sk_ai_dmg_mauser",              "20",     "AI Weapons" },
	{ "sk_ai_dmg_sniperrifle",         "50",     "AI Weapons" },
	{ "sk_ai_dmg_garand",              "20",     "AI Weapons" },
	{ "sk_ai_dmg_snooperscope",        "25",     "AI Weapons" },
	{ "sk_ai_dmg_fg42",                "15",     "AI Weapons" },
	{ "sk_ai_dmg_fg42scope",           "15",     "AI Weapons" },
	{ "sk_ai_dmg_panzerfaust",         "100",    "AI Weapons" },
	{ "sk_ai_dmg_panzerfaust_splash",  "120",    "AI Weapons" },
	{ "sk_ai_dmg_venom",               "10",     "AI Weapons" },
	{ "sk_ai_dmg_flamethrower",        "1",      "AI Weapons" },
	{ "sk_ai_dmg_tesla",               "4",      "AI Weapons" },
	{ "sk_ai_dmg_grenade",             "100",    "AI Weapons" },
	{ "sk_ai_dmg_grenade_radius",      "150",    "AI Weapons" },
	{ "sk_ai_dmg_pineapple",           "80",     "AI Weapons" },
	{ "sk_ai_dmg_pineapple_radius",    "300",    "AI Weapons" },
	{ "sk_ai_dmg_dynamite",            "400",    "AI Weapons" },
	{ "sk_ai_dmg_dynamite_radius",     "400",    "AI Weapons" },

	/* Misc */
	{ "sk_dropped_weapon_min_ammo",    "0.25",   "Misc" },
	{ "g_debugDamage",                 "0",      "Misc" },
	{ "g_debugBullets",                "0",      "Misc" },
	{ "g_debugAlloc",                  "0",      "Misc" },

	{ NULL, NULL, NULL }
};

/*
===================
LS_CheckSettings

Checks all gameplay cvars against their default values and prints a report.
Returns the number of modified settings.
===================
*/
static int LS_CheckSettings( void ) {
	const lsCvarCheck_t *check;
	const char *lastCategory = "";
	int modified = 0;
	int total = 0;
	char val[256];

	Com_Printf( "\n^2============ SETTINGS CHECK ============\n" );

	for ( check = ls_settingsTable; check->name; check++ ) {
		float fDefault, fActual;
		qboolean differs;

		total++;
		Cvar_VariableStringBuffer( check->name, val, sizeof( val ) );

		/* Compare as floats for numeric values to handle "1.0" == "1" etc */
		fDefault = atof( check->defaultVal );
		fActual  = atof( val );

		/* For string values like "0.25", use float comparison with small epsilon */
		if ( fDefault == 0.0f && fActual == 0.0f ) {
			differs = ( Q_stricmp( val, check->defaultVal ) != 0 &&
						Q_stricmp( val, "0" ) != 0 &&
						Q_stricmp( val, "0.0" ) != 0 &&
						Q_stricmp( val, "" ) != 0 ) ? qtrue : qfalse;
		} else {
			float diff = fActual - fDefault;
			if ( diff < 0 ) diff = -diff;
			differs = ( diff > 0.001f ) ? qtrue : qfalse;
		}

		if ( differs ) {
			/* Print category header if new category */
			if ( Q_stricmp( lastCategory, check->category ) != 0 ) {
				Com_Printf( "^3--- %s ---\n", check->category );
				lastCategory = check->category;
			}
			Com_Printf( "  ^1MODIFIED ^7%-35s  ^1current: %-8s  ^2default: %s\n",
				check->name, val, check->defaultVal );
			modified++;
		}
	}

	if ( modified == 0 ) {
		Com_Printf( "^2All %d settings are at default values. ^7OK\n", total );
	} else {
		Com_Printf( "\n^1%d/%d settings modified!\n", modified, total );
	}

	/* Extra checks */
	if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
		Com_Printf( "^1WARNING: sv_cheats is enabled!\n" );
	}
	if ( ls.cheatsUsed ) {
		Com_Printf( "^1WARNING: sv_cheats was used during this run!\n" );
	}
	if ( ls.totalPauses || ls.totalUndos || ls.totalSkips ) {
		Com_Printf( "^3Anti-cheat: Pauses=%d  Undos=%d  Skips=%d\n",
			ls.totalPauses, ls.totalUndos, ls.totalSkips );
	}

	Com_Printf( "^2=========================================\n\n" );

	ls.settingsModified = ( modified > 0 ) ? qtrue : qfalse;
	ls.settingsModCount = modified;

	return modified;
}

static void LS_CheckSettings_f( void ) {
	LS_CheckSettings();
}

/* =====================================================================
   Run finish logic (shared between transition-finish and Heinrich-kill)
   ===================================================================== */

static void LS_FinishRun( int nowReal ) {
	int finalIGT, di;
	char tbuf[32];

	ls.runFinished         = qtrue;
	ls.runSavedRealMs      = nowReal - ls.runStartRealMs;
	di = LS_CurDiffIdx();

	LS_AutoRecordStop();

	finalIGT = ls.runTotalIGTMs;

	/* Send final game-time to external LiveSplit and trigger the final split */
	if ( LS_ExtEnabled() ) {
		LS_ExtSendGameTime( finalIGT );
		LS_ExtSend( "split" );
	}

	/* Per-category completions and PB */
	if ( ls.runMode == LS_MODE_IL ) {
		/* IL: completions already incremented in LS_CompleteSplit (per-split data).
		   bestTimeMs (IL PB) also already updated there. Nothing extra to do. */
	} else {
		int *comp, *pb;
		LS_GetCatCompletions( &comp );
		LS_GetCatPB( &pb );
		(*comp)++;
		if ( *pb == 0 || finalIGT < *pb ) {
			*pb = finalIGT;
			LS_SavePbSegments();
		}
	}

	/* RGT PB (per-category) */
	{
		int rgtMs = ls.runSavedRealMs;
		if ( ls.runMode == LS_MODE_FULLGAME ) {
			if ( ls.fgPBRgt[di] == 0 || rgtMs < ls.fgPBRgt[di] )
				ls.fgPBRgt[di] = rgtMs;
		} else if ( ls.runMode == LS_MODE_MISSION ) {
			int gi = ls.runMission - 1;
			if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS ) {
				if ( ls.msPBRgt[gi][di] == 0 || rgtMs < ls.msPBRgt[gi][di] )
					ls.msPBRgt[gi][di] = rgtMs;
			}
		}
	}

	LS_FormatTime( finalIGT, tbuf, sizeof( tbuf ) );
	Com_Printf( "^2LiveSplit: Run finished!  IGT: ^5%s\n", tbuf );

	/* Print full run statistics to console */
	{
		char rgtBuf[32], sobBuf[32], bptBuf[32];
		int i, sob, bpt;
		int *catPB;
		const char *modeName;
		const char *skillName;

		LS_FormatTime( ls.runSavedRealMs, rgtBuf, sizeof( rgtBuf ) );

		switch ( ls.runMode ) {
			case LS_MODE_MISSION: modeName = "Mission"; break;
			case LS_MODE_IL:      modeName = "IL"; break;
			default:              modeName = "Full Game"; break;
		}
		switch ( ls.currentDifficulty ) {
			case 1:  skillName = "Don't hurt me."; break;
			case 2:  skillName = "Bring 'em on!"; break;
			case 3:  skillName = "I am Death incarnate!"; break;
			default: skillName = "Normal"; break;
		}

		Com_Printf( "^2===== LiveSplit Run Summary =====\n" );
		Com_Printf( "^2Mode:       ^7%s\n", modeName );
		Com_Printf( "^2Difficulty:  ^7%s\n", skillName );
		if ( ls.runMode == LS_MODE_MISSION && ls.runMission >= 1 && ls.runMission <= LS_NUM_MISSION_GROUPS )
			Com_Printf( "^2Mission:     ^7%s\n", ls_missionGroups[ls.runMission - 1].name );
		Com_Printf( "^2Final IGT:   ^5%s\n", tbuf );
		Com_Printf( "^2Final RGT:   ^6%s\n", rgtBuf );

		LS_GetCatPB( &catPB );
		if ( catPB && *catPB > 0 ) {
			char pbBuf[32];
			LS_FormatTime( *catPB, pbBuf, sizeof( pbBuf ) );
			if ( finalIGT <= *catPB )
				Com_Printf( "^2Category PB: ^5%s ^2(NEW PB!)\n", pbBuf );
			else
				Com_Printf( "^2Category PB: ^7%s\n", pbBuf );
		}

		sob = LS_CalcSumOfBests();
		if ( sob >= 0 ) {
			LS_FormatTime( sob, sobBuf, sizeof( sobBuf ) );
			Com_Printf( "^2Sum of Best: ^7%s\n", sobBuf );
		}

		bpt = LS_CalcBestPossible();
		if ( bpt >= 0 ) {
			LS_FormatTime( bpt, bptBuf, sizeof( bptBuf ) );
			Com_Printf( "^2Best Poss.:  ^7%s\n", bptBuf );
		}

		/* Per-split breakdown */
		Com_Printf( "^2--- Split Breakdown ---\n" );
		Com_Printf( "^2%-20s %10s %10s %10s\n", "Map", "Segment", "Gold", "Delta" );
		for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
			char segBuf[32], goldBuf[32], dBuf[32];
			int gold;
			if ( ls.splits[i].cutscene ) continue;
			if ( ls.splits[i].splitSkipped ) {
				Q_strncpyz( segBuf, "---", sizeof( segBuf ) );
			} else {
				LS_FormatTime( ls.splits[i].currentTimeMs, segBuf, sizeof( segBuf ) );
			}
			gold = ls.splits[i].d[di].bestTimeMs;
			if ( gold > 0 ) {
				LS_FormatTime( gold, goldBuf, sizeof( goldBuf ) );
				LS_FormatDelta( ls.splits[i].currentTimeMs - gold, dBuf, sizeof( dBuf ) );
			} else {
				Q_strncpyz( goldBuf, "-----", sizeof( goldBuf ) );
				Q_strncpyz( dBuf, "-----", sizeof( dBuf ) );
			}
			Com_Printf( "^7%-20s %10s %10s %10s\n",
				ls.splits[i].displayName ? ls.splits[i].displayName : ls.splits[i].mapname,
				segBuf, goldBuf, dBuf );
		}

		if ( ls.totalPauses || ls.totalUndos || ls.totalSkips ) {
			Com_Printf( "^3Anti-cheat:  Pauses=%d  Undos=%d  Skips=%d\n",
				ls.totalPauses, ls.totalUndos, ls.totalSkips );
		}

		/* 100% category: check if all secrets and treasures found */
		if ( ls_100pctCvar && ls_100pctCvar->integer ) {
			int pctSecF = 0, pctSecT = 0, pctTresF = 0, pctTresT = 0;
			for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
				if ( ls.splits[i].cutscene ) continue;
				pctSecF  += ls.splits[i].secretsFound;
				pctSecT  += ls.splits[i].secretsTotal;
				pctTresF += ls.splits[i].treasureFound;
				pctTresT += ls.splits[i].treasureTotal;
			}
			Com_Printf( "^2Secrets:     ^7%d/%d\n", pctSecF, pctSecT );
			Com_Printf( "^2Treasure:    ^7%d/%d\n", pctTresF, pctTresT );
			if ( ( pctSecT == 0 || pctSecF >= pctSecT ) &&
				 ( pctTresT == 0 || pctTresF >= pctTresT ) ) {
				Com_Printf( "^5*** 100%% - ALL SECRETS & TREASURE FOUND! ***\n" );
			} else if ( ( pctSecT == 0 || pctSecF >= pctSecT ) &&
						pctTresT > 0 && pctTresF < pctTresT ) {
				Com_Printf( "^5*** ALL SECRETS FOUND! ***\n" );
			} else if ( ( pctTresT == 0 || pctTresF >= pctTresT ) &&
						pctSecT > 0 && pctSecF < pctSecT ) {
				Com_Printf( "^5*** ALL TREASURE FOUND! ***\n" );
			}
		}

		Com_Printf( "^2=================================\n" );
	}

	/* Auto-check settings on run finish */
	LS_CheckSettings();

	/* Record in history */
	if ( ls.numHistoryRuns < LS_MAX_HISTORY_RUNS ) {
		lsRunHistory_t *r = &ls.history[ls.numHistoryRuns];
		int i, n = 0;
		r->difficulty = ls.currentDifficulty;
		r->mode       = ls.runMode;
		r->missionNum = ls.runMission;
		r->totalIGTMs = finalIGT;
		r->totalRGTMs = ls.runSavedRealMs;
		for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
			if ( !ls.splits[i].cutscene ) {
				r->splitTimes[n] = ls.splits[i].currentTimeMs;
				n++;
			}
		}
		r->numSplits = n;
		ls.numHistoryRuns++;
	}

	LS_Save();
	LS_SaveHistory();
}

/* Complete a split: update gold, increment counters, add to runTotalIGTMs */
static void LS_CompleteSplit( int idx, int nowReal ) {
	int t, di;

	if ( idx < 0 || idx >= ls.numMaps ) return; /* bounds safety */
	if ( ls.splits[idx].splitDone ) return;      /* already completed - guard */
	t = ls.splits[idx].currentTimeMs;
	di = LS_CurDiffIdx();

	if ( t <= 0 ) return;

	ls.splits[idx].splitDone = qtrue;
	ls.splits[idx].d[di].totalCompletions++;
	ls.splits[idx].prevGoldMs = ls.splits[idx].d[di].bestTimeMs;

	/* 100% tracking: store PER-MAP secrets/treasures found.
	   player->numSecretsFound is cumulative across all maps (the game
	   never resets it), so we subtract the base (cumulative at the
	   start of this map) to get the per-map delta.
	   Totals come from the hardcoded ls_mapDefs table (set at init).
	   Use prevLive (from previous frame) because during map transitions
	   the configstring already shows the NEW map's values.
	   If found values were already pre-saved (deferral or undo+re-complete),
	   keep them but still advance the base for subsequent maps. */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		if ( ls.splits[idx].secretsFound == 0 && ls.splits[idx].treasureFound == 0 ) {
			int sf = ls.prevLiveSecretsFound  - ls.baseSecretsFound;
			int tf = ls.prevLiveTreasureFound - ls.baseTreasureFound;
			ls.splits[idx].secretsFound  = ( sf > 0 ) ? sf : 0;
			ls.splits[idx].treasureFound = ( tf > 0 ) ? tf : 0;
		}
		/* Always advance base for the next map */
		ls.baseSecretsFound  += ls.splits[idx].secretsFound;
		ls.baseTreasureFound += ls.splits[idx].treasureFound;
	}

	if ( ls.splits[idx].d[di].bestTimeMs == 0 ||
		 t < ls.splits[idx].d[di].bestTimeMs ) {
		ls.splits[idx].d[di].bestTimeMs = t;
		ls.splits[idx].goldFlashMs = Sys_Milliseconds(); /* trigger gold flash */
		LS_GhostOnGoldSplit( ls.splits[idx].mapname );
	}

	ls.runTotalIGTMs += t;

	/* Bump ASL split sequence number (monitored by external LiveSplit) */
	ls_aslState.splitSeqNum++;
	Com_Printf( "^5LiveSplit: splitSeqNum=%d (map=%s idx=%d t=%d)\n",
			ls_aslState.splitSeqNum, ls.splits[idx].mapname, idx, t );

	/* Notify external LiveSplit Server */
	{
		char gtBuf[64];
		Com_sprintf( gtBuf, sizeof( gtBuf ), "setgametime %.3f",
					 (float)ls.runTotalIGTMs / 1000.0f );
		LS_ExtSend( gtBuf );
		LS_ExtSend( "split" );
	}

	/* Check if this was the end map for the current mode */
	if ( idx == ls.modeEndMapIdx ) {
		LS_FinishRun( nowReal );
	}
}

/* =====================================================================
   Per-frame update
   ===================================================================== */

static void LS_Frame( void ) {
	char currentMap[LS_MAX_MAPNAME];
	int nowReal, newIdx;
	qboolean isPaused, isConnected;
	qboolean isSpTransition;
	int curMode, curMission;

	if ( !ls.initialized ) return;

	/* External LiveSplit connection / per-frame game-time sync */
	LS_ExtFrame();

	/* ---- IGT accumulation (runs BEFORE any early returns) ----
	   Timer ticks every frame so quickloads / death-reloads keep it
	   running.  Guards that prevent counting during loads/transitions:

	   1) Map-name mismatch - if cl.mapname has changed but
	      actualMapname hasn't been updated yet (map-change detection
	      runs later in LS_Frame), we're between loading and detection.
	   2) mapLoadFreeze flag - set by map-change detection when a new
	      map is entered.  Cleared only when the UI fires the
	      playerstart handler (player clicks continue arrow).
	   3) ls_loading cvar - set to 1 during loads, cleared to 0 by
	      the UI playerstart handler.
	   ALL must pass for time to accumulate.
	   NOTE: we do NOT check g_reloading here.  For Full Game the
	   timer must keep running during fade-to-black.  For IL/Mission
	   end maps the split is already marked done by end-detection so
	   no extra time accumulates on finished splits. */
	if ( ls.active && ls.currentMapIndex >= 0 && !ls.manualPause ) {
		int rNow = Sys_Milliseconds();
		int rDt  = rNow - ls.lastRealTimeMs;
		qboolean canAccumulate = qtrue;

		/* Guard 1: map-name mismatch - if cl.mapname already shows
		   the new map but actualMapname still has the old one, we're
		   in the gap between loading and map-change detection.
		   Also block when actualMapname is empty (first frame after
		   auto-start before afterMapChange runs). */
		if ( !ls.actualMapname[0] ) {
			canAccumulate = qfalse;
		} else {
			char igtMap[LS_MAX_MAPNAME];
			LS_ExtractMapname( cl.mapname, igtMap, sizeof( igtMap ) );
			if ( igtMap[0] && Q_stricmp( igtMap, ls.actualMapname ) ) {
				canAccumulate = qfalse;
			}
		}

		/* Guard 2 + 3: mapLoadFreeze and ls_loading */
		if ( canAccumulate && ( ls.mapLoadFreeze ||
			 Cvar_VariableIntegerValue( "ls_loading" ) ) ) {
			canAccumulate = qfalse;
		}

		/* Guard 2b: ESC menu open - pause IGT while UI is active */
		if ( canAccumulate && ( cls.keyCatchers & KEYCATCH_UI ) ) {
			canAccumulate = qfalse;
		}

		/* Guard 4: end-map cutscene detection (IL/Mission).
		   On the end map, after arming (1 s of gameplay with letterbox off),
		   the first letterbox 0>1 transition triggers an immediate run finish
		   at the exact moment the cutscene starts.  Intro cutscenes are
		   excluded because arming requires letterbox=0.  ls_changelevel
		   and g_reloading serve as fallbacks for maps without letterbox. */
		if ( ( ls.runMode == LS_MODE_IL || ls.runMode == LS_MODE_MISSION ) &&
			 ls.currentMapIndex == ls.modeEndMapIdx &&
			 ls.modeEndMapIdx >= 0 ) {
			int letterbox   = Cvar_VariableIntegerValue( "cg_letterbox" );
			int changelevel = Cvar_VariableIntegerValue( "ls_changelevel" );

			/* Arm once REAL gameplay starts: fully loaded, letterbox
			   off, and at least 1 s of play time accumulated. */
			if ( !ls.endCutsceneArmed && !letterbox &&
				 cls.state >= CA_ACTIVE && !ls.mapLoadFreeze &&
				 ls.splits[ls.modeEndMapIdx].currentTimeMs > 1000 ) {
				ls.endCutsceneArmed = qtrue;
			}

			if ( ls.endCutsceneArmed ) {
				/* Detect letterbox 0>1 (cutscene start): take snapshot. */
				if ( letterbox && !ls.endCutscenePrevLB ) {
					ls.endCutsceneSnapMs = ls.splits[ls.modeEndMapIdx].currentTimeMs;
				}
				/* Detect letterbox 1>0 (cutscene ended without changelevel >
				   was a mid-level cutscene): invalidate snapshot. */
				else if ( !letterbox && ls.endCutscenePrevLB ) {
					ls.endCutsceneSnapMs = -1;
				}
			}

			ls.endCutscenePrevLB = letterbox ? qtrue : qfalse;

			/* ls_changelevel detected - finish the run.
			   ls_changelevel is set by both:
			   (a) AICast_ScriptChange scanning the AI event for changelevel
			   (b) G_Script_ScriptChange scanning entity scripts for
			       "trigger <AI> <event>" where the AI event has changelevel
			   (c) AICast_ScriptAction_ChangeLevel as a redundant safety net
			   Case (b) fires at the START of the entity cutscene script,
			   so the client sees ls_changelevel on the same frame as the
			   letterbox 0>1 transition - giving an instant finish. */
			if ( ls.endCutsceneArmed && changelevel &&
				 !ls.splits[ls.modeEndMapIdx].splitDone ) {
				if ( ls.endCutsceneSnapMs >= 0 ) {
					ls.splits[ls.modeEndMapIdx].currentTimeMs = ls.endCutsceneSnapMs;
				}
				LS_CompleteSplit( ls.modeEndMapIdx, rNow );
				canAccumulate = qfalse;
			}

			/* Timer keeps running during cutscenes - no freeze.
			   When changelevel is detected, the time is rolled back
			   to the snapshot taken at the 0>1 transition. */
		}

		/* Clear the map-load freeze once we're fully loaded.
		   Non-cutscene maps: wait for the player to click 'continue'
		   on the briefing screen (UI sets ls_loading=0).
		   Cutscene maps: no briefing screen exists, so auto-clear
		   as soon as CA_ACTIVE is reached. */
		if ( ls.mapLoadFreeze && cls.state >= CA_ACTIVE ) {
			int ci = ls.currentMapIndex;
			if ( ci >= 0 && ls.splits[ci].cutscene ) {
				ls.mapLoadFreeze = qfalse;
				Cvar_Set( "ls_loading", "0" );
			} else if ( !Cvar_VariableIntegerValue( "ls_loading" ) ) {
				ls.mapLoadFreeze = qfalse;
			}
		}

		/* Accumulate time only when all guards pass. */
		if ( rDt > 0 && rDt < 1000 && canAccumulate ) {
			int ci = ls.currentMapIndex;
			if ( ls.splits[ci].cutscene ) {
				/* In Mission/IL mode cutscene time is not counted.
				   In Full Game mode cutscene time goes to the
				   adjacent real split. */
				if ( ls.runMode == LS_MODE_FULLGAME ) {
					int realIdx;
					if ( ci == ls.modeFirstIdx ) {
						realIdx = LS_NextRealSplit( ci );
					} else {
						realIdx = LS_PrevRealSplit( ci );
					}
					if ( realIdx >= 0 && !ls.splits[realIdx].splitDone &&
						 LS_InActiveSet( realIdx ) ) {
						ls.splits[realIdx].currentTimeMs += rDt;
					}
				}
			} else if ( !ls.splits[ci].splitDone && LS_InActiveSet( ci ) ) {
				ls.splits[ci].currentTimeMs += rDt;
			}
		}
		ls.lastRealTimeMs = rNow;
	} else {
		ls.lastRealTimeMs = Sys_Milliseconds();
	}

	/* Skip the rest of LS_Frame during savegame loading to avoid
	   unstable engine state.  IGT accumulation above already ran. */
	if ( Cvar_VariableIntegerValue( "savegame_loading" ) ) {
		return;
	}

	/* Deferred Full Game start: the run begins when the player clicks
	   the continue arrow on the briefing screen (playerstart fires,
	   ls_loading goes to 0), not during loading or the briefing. */
	if ( ls.fgPendingStart && cls.state >= CA_ACTIVE &&
		 !Cvar_VariableIntegerValue( "ls_loading" ) ) {
		int k, di;
		ls.fgPendingStart   = qfalse;
		ls.active           = qtrue;
		ls_aslState.runStartCount++;
		ls_aslState.splitSeqNum = 0;
		ls.runFinished      = qfalse;
		ls.runStartRealMs   = Sys_Milliseconds();
		ls.runSavedRealMs   = 0;
		ls.runTotalIGTMs    = 0;
		ls.heinrichDead     = qfalse;
		ls.endCutsceneArmed = qfalse;
		ls.endCutsceneSnapMs = -1;
		ls.endCutscenePrevLB = qfalse;
		Cvar_Set( "ls_changelevel", "0" );
		ls.manualPause      = qfalse;
		ls.cheatsUsed       = qfalse;
		ls.settingsModified = qfalse;
		ls.settingsModCount = 0;
		ls.totalPauses      = 0;
		ls.totalUndos       = 0;
		ls.totalSkips       = 0;
		ls.baseSecretsFound  = 0;
		ls.baseTreasureFound = 0;
		ls.currentDifficulty = LS_DetectDifficulty();
		di = LS_CurDiffIdx();
		ls.fgAttempts[di]++;

		LS_SavePreRunPBs();

		for ( k = 0; k < ls.numMaps; k++ ) {
			ls.splits[k].currentTimeMs = 0;
			ls.splits[k].splitDone     = qfalse;
			ls.splits[k].prevGoldMs    = 0;
			ls.splits[k].goldFlashMs   = 0;
			ls.splits[k].secretsFound  = 0;
			ls.splits[k].treasureFound = 0;
		}

		Cvar_Set( "g_heinrichDead", "0" );
		ls.currentMapIndex = 0;
		Q_strncpyz( ls.actualMapname, "cutscene1", LS_MAX_MAPNAME );
		ls.mapLoadFreeze = qfalse;
		Cvar_Set( "ls_loading", "0" );
		LS_UpdateCurVisRow();
		LS_Save();
		ls.lastRealTimeMs = Sys_Milliseconds();

		if ( LS_ExtEnabled() ) {
			lsext_recvLen = 0;
			LS_ExtSend( "initgametime" );
			LS_ExtSend( "starttimer" );
			LS_ExtSend( "pausegametime" );
			lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;
		}
		Com_Printf( "^2LiveSplit: Full Game run started (briefing dismissed)\n" );
	}

	/* Deferred auto-record: fire the record command once CA_ACTIVE */
	if ( ls_autoRecordPending && cls.state == CA_ACTIVE && !clc.demorecording ) {
		Cbuf_AddText( va( "record %s\n", ls_pendingRecordName ) );
		ls_autoRecordActive = qtrue;
		ls_autoRecordPending = qfalse;
		Com_Printf( "^2LiveSplit: Auto-recording demo '%s'\n", ls_pendingRecordName );
	}

	/* Update mode from cvars immediately - must happen before connection check
	   so changes in console are visible even when not fully in-game */
	{
		curMode    = ls_modeCvar    ? ls_modeCvar->integer    : 0;
		curMission = ls_missionCvar ? ls_missionCvar->integer : 1;
		if ( curMode != ls.runMode ||
			 ( curMode == LS_MODE_MISSION && curMission != ls.runMission ) ) {
			/* Full reset of transient run data so old times don't linger */
			{
				int k;
				ls.active          = qfalse;
				ls.runFinished     = qfalse;
				ls.runTotalIGTMs   = 0;
				ls.runStartRealMs  = 0;
				ls.runSavedRealMs  = 0;
				ls.heinrichDead    = qfalse;
				ls.endCutsceneArmed = qfalse;
				ls.endCutsceneSnapMs = -1;
				ls.endCutscenePrevLB = qfalse;
				Cvar_Set( "ls_changelevel", "0" );
				ls.fgPendingStart  = qfalse;
				ls.manualPause     = qfalse;
				ls.currentMapIndex = -1;
				ls.baseSecretsFound  = 0;
				ls.baseTreasureFound = 0;
				for ( k = 0; k < ls.numMaps; k++ ) {
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
					ls.splits[k].secretsFound  = 0;
					ls.splits[k].treasureFound = 0;
				}
			}
			LS_SetupMode( curMode, curMission );
			/* Clear prevMapname so auto-start can fire on the current map */
			ls.prevMapname[0] = '\0';
		}
		/* IL: detect ls_map cvar change (only when not actively running) */
		if ( curMode == LS_MODE_IL && !ls.active && ls_mapCvar && ls_mapCvar->string[0] ) {
			int wantIdx = LS_FindMapIndex( ls_mapCvar->string );
			if ( wantIdx >= 0 && !ls.splits[wantIdx].cutscene &&
				 wantIdx != ls.modeFirstIdx ) {
				{
					int k;
					ls.active          = qfalse;
					ls.runFinished     = qfalse;
					ls.runTotalIGTMs   = 0;
					ls.runStartRealMs  = 0;
					ls.runSavedRealMs  = 0;
					ls.heinrichDead    = qfalse;
					ls.endCutsceneArmed = qfalse;
					ls.endCutsceneSnapMs = -1;
					ls.endCutscenePrevLB = qfalse;
					Cvar_Set( "ls_changelevel", "0" );
					ls.fgPendingStart  = qfalse;
					ls.manualPause     = qfalse;
					ls.currentMapIndex = -1;
					ls.baseSecretsFound  = 0;
					ls.baseTreasureFound = 0;
					for ( k = 0; k < ls.numMaps; k++ ) {
						ls.splits[k].currentTimeMs = 0;
						ls.splits[k].splitDone     = qfalse;
						ls.splits[k].prevGoldMs    = 0;
						ls.splits[k].goldFlashMs   = 0;
						ls.splits[k].secretsFound  = 0;
						ls.splits[k].treasureFound = 0;
					}
				}
				LS_SetupMode( LS_MODE_IL, ls.runMission );
				ls.prevMapname[0] = '\0'; /* allow auto-start on current map */
			}
		}
	}

	nowReal     = Sys_Milliseconds();
	isPaused    = Cvar_VariableIntegerValue( "cl_paused" ) ? qtrue : qfalse;
	isConnected = ( cls.state >= CA_CONNECTED ) ? qtrue : qfalse;

	if ( !isConnected || cl.mapname[0] == '\0' ) {
		/* Clear prevMapname so the next real map load is detected as
		   a new map change, enabling auto-start after reset+reload */
		ls.prevMapname[0] = '\0';

		/* Auto-disable sv_cheats while in main menu so
		   the warning doesn't persist next run */
		if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
			Cvar_Set( "sv_cheats", "0" );
			Com_Printf( "^2LiveSplit: sv_cheats auto-disabled (main menu)\n" );
		}
		return;
	}

	/* Detect sv_cheats usage - flag persists until run reset/auto-start */
	if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
		ls.cheatsUsed = qtrue;
	}

	LS_ExtractMapname( cl.mapname, currentMap, sizeof( currentMap ) );
	if ( currentMap[0] == '\0' ) return;

	/* ---- 100% tracking: parse CS_MISSIONSTATS each frame ---- */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		/* Save previous-frame values before overwriting with new data.
		   During map transitions the configstring flips to the NEW map,
		   so prev values let LS_CompleteSplit use the OLD map's stats. */
		ls.prevLiveSecretsFound  = ls.liveSecretsFound;
		ls.prevLiveSecretsTotal  = ls.liveSecretsTotal;
		ls.prevLiveTreasureFound = ls.liveTreasureFound;
		ls.prevLiveTreasureTotal = ls.liveTreasureTotal;
		LS_ParseMissionStats();
	}

	/* For IL mode, auto-update target map only when ls_map cvar is empty */
	if ( ls.runMode == LS_MODE_IL &&
		 ( !ls_mapCvar || !ls_mapCvar->string[0] ) ) {
		int ilIdx = LS_FindMapIndex( currentMap );
		if ( ilIdx >= 0 && !ls.splits[ilIdx].cutscene &&
			 ilIdx != ls.modeFirstIdx ) {
			LS_SetupMode( LS_MODE_IL, ls.runMission );
		}
	}

	if ( ls.runFinished ) return;

	/* ---- End-of-game detection ---- */
	/* On the "end" map (Heinrich): two-phase detection.
	   Phase 1: g_heinrichDead flips to 1 when Heinrich is killed.
	   Phase 2: cg_letterbox flips to 1 when the victory cutscene
	   camera starts - THIS is the moment we finish the run.
	   For Mission/IL non-Heinrich end maps: use cg_letterbox to detect
	   the ending cutscene start rather than waiting for g_reloading
	   (which fires later, during the black screen / map change).
	   For Fullgame non-Heinrich end maps: fall back to g_reloading. */
	if ( ls.active && ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps &&
		 ls.currentMapIndex == ls.modeEndMapIdx &&
		 ls.modeEndMapIdx >= 0 && ls.modeEndMapIdx < ls.numMaps ) {
		/* Heinrich - "end" map only */
		if ( !Q_stricmp( ls.splits[ls.modeEndMapIdx].mapname, "end" ) ) {
			/* Phase 1: latch the kill */
			if ( !ls.heinrichDead &&
				 Cvar_VariableIntegerValue( "g_heinrichDead" ) ) {
				ls.heinrichDead = qtrue;
			}
			/* Phase 2: cutscene started after kill */
			if ( ls.heinrichDead &&
				 Cvar_VariableIntegerValue( "cg_letterbox" ) ) {
				if ( !ls.splits[ls.modeEndMapIdx].splitDone ) {
					LS_CompleteSplit( ls.modeEndMapIdx, nowReal );
				}
				if ( !ls.runFinished ) {
					LS_FinishRun( nowReal );
				}
				return;
			}
		} else if ( ls.runMode == LS_MODE_MISSION || ls.runMode == LS_MODE_IL ) {
			/* Mission/IL: fallback end detection via g_reloading.
			   Guard 4 in the IGT block handles the primary early finish
			   (via ls_changelevel or the freeze+snapshot mechanism).
			   This path fires only when Guard 4 didn't complete the run
			   (e.g. maps without cg_letterbox, or ls_changelevel missed).
			   The snapshot (endCutsceneSnapMs) is valid here because it is
			   cleared on every letterbox 1>0 transition, so it always
			   reflects the start of the LAST (ending) cutscene.
			   Check 0x02 (NEXTMAP), 0x04 (NEXTMAP_WAITING) and 0x10 (ENDGAME)
			   to cover both fade-based and direct spmap transitions.
			   Require at least 2 seconds of run time to prevent false triggers
			   from stale g_reloading values at map start. */
			int reloading = Cvar_VariableIntegerValue( "g_reloading" );
			if ( reloading == 0x02 || reloading == 0x04 || reloading == 0x10 ) {
				int runTime = ls.splits[ls.modeEndMapIdx].currentTimeMs;
				if ( runTime > 2000 ) {
					/* Roll back to cutscene-start snapshot if available
					   so end-cutscene time is excluded from the split. */
					if ( ls.endCutsceneSnapMs >= 0 ) {
						ls.splits[ls.modeEndMapIdx].currentTimeMs = ls.endCutsceneSnapMs;
					}
					if ( !ls.splits[ls.modeEndMapIdx].splitDone ) {
						LS_CompleteSplit( ls.modeEndMapIdx, nowReal );
					}
					if ( !ls.runFinished ) {
						LS_FinishRun( nowReal );
					}
					return;
				}
			}
		} else {
			/* Fullgame non-Heinrich end map: detect changelevel via g_reloading */
			int reloading = Cvar_VariableIntegerValue( "g_reloading" );
			if ( reloading == 0x02 || reloading == 0x04 || reloading == 0x10 ) {
				if ( !ls.splits[ls.modeEndMapIdx].splitDone ) {
					LS_CompleteSplit( ls.modeEndMapIdx, nowReal );
				}
				if ( !ls.runFinished ) {
					LS_FinishRun( nowReal );
				}
				return;
			}
		}
	}

	/* ---- detect map change ---- */
	if ( Q_stricmp( currentMap, ls.prevMapname ) ) {
		/* Quickload / death-reload on the same map clears prevMapname
		   (via the !isConnected early-out above) so it looks like a
		   "change" here, but actualMapname still holds the real map.
		   Compare against actualMapname to tell the two apart. */
		qboolean realMapChange = Q_stricmp( currentMap, ls.actualMapname ) ? qtrue : qfalse;

		/* After a reset the player may reload the same start map.
		   actualMapname still holds the old name, so realMapChange is
		   false.  Override it when no run is active so auto-start
		   can fire again (IL retry / Mission retry / FG retry). */
		if ( !realMapChange && !ls.active && !ls.runFinished ) {
			realMapChange = qtrue;
		}

		Q_strncpyz( ls.prevMapname, currentMap, LS_MAX_MAPNAME );
		ls.lastServerTime = cl.serverTime;
		ls.lastRealTimeMs = Sys_Milliseconds();

		if ( !realMapChange ) {
			/* Same-map reload (quickload, death).  Just update tracking
			   vars and fall through to normal IGT accumulation. */
			goto afterMapChange;
		}

		ls.mapLoadFreeze = qtrue;
		ls.mapLoadFreezeStartMs = Sys_Milliseconds();
		Cvar_Set( "ls_loading", "1" );

		newIdx = LS_FindMapIndex( currentMap );

		/* Reset live 100% counters on every real map change.
		   CS_MISSIONSTATS hasn't been set yet for the new map,
		   so without this reset the display shows stale values
		   from the previous map until the server sends the new data. */
		ls.liveSecretsFound  = 0;
		ls.liveSecretsTotal  = 0;
		ls.liveTreasureFound = 0;
		ls.liveTreasureTotal = 0;

		isSpTransition = Cvar_VariableIntegerValue( "sv_spTransition" ) ? qtrue : qfalse;
		Cvar_Set( "sv_spTransition", "0" );

		/* ---- Guard: keep timer running on backward map change ----
		   During an active (non-finished) run, if the player navigates
		   to a map that is NOT the next sequential split (e.g., they
		   accidentally typed devmap/spmap to an earlier stage), undo
		   the loading freeze and skip all auto-start / transition
		   logic so the timer keeps running on the current split.
		   Use ls_reset to intentionally restart the run. */
		if ( ls.active && !ls.runFinished && newIdx >= 0 &&
			 newIdx != ls.currentMapIndex + 1 ) {
			ls.mapLoadFreeze = qfalse;
			Cvar_Set( "ls_loading", "0" );
			goto afterMapChange;
		}

		/* IL mode: auto-start only on the configured target map (ls_map) */
		if ( ls.runMode == LS_MODE_IL &&
			 newIdx >= 0 && newIdx == ls.modeFirstIdx && !ls.splits[newIdx].cutscene ) {
			int k, di;

			ls.active           = qtrue;
			ls_aslState.runStartCount++;
			ls_aslState.splitSeqNum = 0;
			ls.runFinished      = qfalse;
			ls.runStartRealMs   = nowReal;
			ls.runSavedRealMs   = 0;
			ls.runTotalIGTMs    = 0;
			ls.heinrichDead     = qfalse;
			ls.endCutsceneArmed = qfalse;
			ls.endCutsceneSnapMs = -1;
			ls.endCutscenePrevLB = qfalse;
			Cvar_Set( "ls_changelevel", "0" );
			ls.manualPause      = qfalse;
			ls.cheatsUsed       = qfalse;
			ls.settingsModified = qfalse;
			ls.settingsModCount = 0;
			ls.baseSecretsFound  = 0;
			ls.baseTreasureFound = 0;
			ls.currentDifficulty = LS_DetectDifficulty();
			di = LS_CurDiffIdx();
			LS_SavePreRunPBs();

			for ( k = 0; k < ls.numMaps; k++ ) {
				ls.splits[k].currentTimeMs = 0;
				ls.splits[k].splitDone     = qfalse;
				ls.splits[k].prevGoldMs    = 0;
				ls.splits[k].goldFlashMs   = 0;
				ls.splits[k].secretsFound  = 0;
				ls.splits[k].treasureFound = 0;
			}

			ls.currentMapIndex = newIdx;
			/* IL: attempts tracked via per-split data only */
			ls.splits[newIdx].d[di].totalAttempts++;
			LS_UpdateCurVisRow();
			LS_Save();
			LS_AutoRecordStart();
			/* If already at CA_ACTIVE (reset without reload - player
			   is still in gameplay), clear the freeze immediately
			   so the timer starts right away.  On a real reload the
			   state is CA_LOADING/PRIMED so the freeze stays until
			   the player clicks 'continue' on the briefing screen. */
			if ( cls.state >= CA_ACTIVE ) {
				ls.mapLoadFreeze = qfalse;
				Cvar_Set( "ls_loading", "0" );
			}
			lsext_recvLen = 0; /* flush stale recv data */
			LS_ExtSend( "initgametime" );
			LS_ExtSend( "starttimer" );
			LS_ExtSend( "pausegametime" );
			lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;
			return;
		}

		/* ========== Mission auto-start (works with devmap too) ========== */
		if ( ls.runMode == LS_MODE_MISSION ) {
			qboolean isStart = qfalse;
			if ( newIdx == ls.modeFirstIdx ) {
				isStart = qtrue;
			} else {
				/* Also allow starting from the first non-cutscene split */
				int fr;
				for ( fr = ls.modeFirstIdx; fr <= ls.modeLastIdx; fr++ ) {
					if ( !ls.splits[fr].cutscene ) break;
				}
				if ( fr <= ls.modeLastIdx && newIdx == fr )
					isStart = qtrue;
			}
			if ( isStart ) {
				int k, di, gi;
				ls.active           = qtrue;
				ls_aslState.runStartCount++;
				ls_aslState.splitSeqNum = 0;
				ls.runFinished      = qfalse;
				ls.runStartRealMs   = nowReal;
				ls.runSavedRealMs   = 0;
				ls.runTotalIGTMs    = 0;
				ls.heinrichDead     = qfalse;
				ls.endCutsceneArmed = qfalse;
				ls.endCutsceneSnapMs = -1;
				ls.endCutscenePrevLB = qfalse;
				Cvar_Set( "ls_changelevel", "0" );
				ls.manualPause      = qfalse;
				ls.cheatsUsed       = qfalse;
				ls.settingsModified = qfalse;
				ls.settingsModCount = 0;
				ls.totalPauses      = 0;
				ls.totalUndos       = 0;
				ls.totalSkips       = 0;
				ls.baseSecretsFound  = 0;
				ls.baseTreasureFound = 0;
				ls.currentDifficulty = LS_DetectDifficulty();
				di = LS_CurDiffIdx();
				gi = ls.runMission - 1;
				if ( gi >= 0 && gi < LS_NUM_MISSION_GROUPS )
					ls.msAttempts[gi][di]++;

				LS_SavePreRunPBs();

				for ( k = 0; k < ls.numMaps; k++ ) {
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
					ls.splits[k].secretsFound  = 0;
					ls.splits[k].treasureFound = 0;
				}

				Cvar_Set( "g_heinrichDead", "0" );
				ls.currentMapIndex = newIdx;
				LS_UpdateCurVisRow();
				LS_Save();
				LS_AutoRecordStart();
				/* If already at CA_ACTIVE (reset without reload -
				   player still in gameplay), clear the freeze now.
				   On a real reload the state is CA_LOADING/PRIMED
				   so the freeze stays until playerstart fires
				   (non-cutscene) or CA_ACTIVE auto-clear (cutscene). */
				if ( cls.state >= CA_ACTIVE ) {
					ls.mapLoadFreeze = qfalse;
					Cvar_Set( "ls_loading", "0" );
				}
				lsext_recvLen = 0; /* flush stale recv data */
				LS_ExtSend( "initgametime" );
				LS_ExtSend( "starttimer" );
				LS_ExtSend( "pausegametime" );
				lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;
				return;
			}
		}

		/* ========== Full Game deferred start ========== */
		/* Don't start the run now - just set a pending flag.
		   The actual run init happens when cls.state >= CA_ACTIVE
		   (the cutscene is playing) so loading time is never counted. */
		if ( ls.runMode == LS_MODE_FULLGAME && newIdx == 0 ) {
			ls.fgPendingStart = qtrue;
			/* mapLoadFreeze + ls_loading are already set above */
			LS_AutoRecordStart();
			return;
		}

		/* ========== End-map departure (IL / Mission) ==========
		   If we just left the end map in IL or Mission mode,
		   complete the split and finish the run regardless of how
		   the transition happened (changelevel, spmap, devmap, etc).
		   This catches cases where g_reloading was too brief to detect
		   during the per-frame check above (e.g. maps that transition
		   directly to a cutscene without fade-to-black). */
		if ( ls.active && !ls.runFinished &&
			 ls.currentMapIndex == ls.modeEndMapIdx &&
			 ls.modeEndMapIdx >= 0 &&
			 ( ls.runMode == LS_MODE_IL || ls.runMode == LS_MODE_MISSION ) ) {
			/* Roll back to cutscene-start snapshot if available
			   so end-cutscene time is excluded from the split. */
			if ( ls.endCutsceneSnapMs >= 0 ) {
				ls.splits[ls.modeEndMapIdx].currentTimeMs = ls.endCutsceneSnapMs;
			}
			if ( !ls.splits[ls.modeEndMapIdx].splitDone ) {
				LS_CompleteSplit( ls.modeEndMapIdx, nowReal );
			}
			if ( !ls.runFinished ) {
				LS_FinishRun( nowReal );
			}
			return;
		}

		if ( !isSpTransition ) goto afterMapChange;

		/* ========== Natural sequential transition (active run) ========== */
		if ( ls.active && ls.currentMapIndex >= 0 &&
			 newIdx == ls.currentMapIndex + 1 ) {

			int prevIdx = ls.currentMapIndex;
			int di = LS_CurDiffIdx();

			/* ---- Complete previous split ---- */
			if ( !ls.splits[prevIdx].splitDone ) {
				if ( ls.splits[prevIdx].cutscene ) {
					/* Leaving a cutscene: mark done with 0 */
					ls.splits[prevIdx].splitDone     = qtrue;
					ls.splits[prevIdx].currentTimeMs = 0;

					if ( prevIdx != ls.modeFirstIdx ) {
						/* Non-first cutscene: complete the previous real split */
						int realIdx = LS_PrevRealSplit( prevIdx );
						if ( realIdx >= 0 && !ls.splits[realIdx].splitDone &&
							 LS_InActiveSet( realIdx ) ) {
							LS_CompleteSplit( realIdx, nowReal );
							if ( ls.runFinished ) return;
						}
					}
				} else if ( LS_InActiveSet( prevIdx ) ) {
					/* Leaving a real map */
					int t = ls.splits[prevIdx].currentTimeMs;
					qboolean deferCompletion = qfalse;

					/* Defer if next is a non-first cutscene and this isn't the end map */
					if ( newIdx < ls.numMaps &&
						 ls.splits[newIdx].cutscene && newIdx != ls.modeFirstIdx &&
						 prevIdx != ls.modeEndMapIdx ) {
						deferCompletion = qtrue;
					}

					if ( deferCompletion ) {
						/* Pre-save 100% found counts now while prevLive still
						   reflects this real map.  When LS_CompleteSplit
						   fires later (leaving the cutscene), prevLive
						   will hold the cutscene's values (0).
						   Totals already come from the hardcoded table. */
						if ( ls_100pctCvar && ls_100pctCvar->integer ) {
							int sf = ls.prevLiveSecretsFound  - ls.baseSecretsFound;
							int tf = ls.prevLiveTreasureFound - ls.baseTreasureFound;
							ls.splits[prevIdx].secretsFound  = ( sf > 0 ) ? sf : 0;
							ls.splits[prevIdx].treasureFound = ( tf > 0 ) ? tf : 0;
							ls.baseSecretsFound  += ls.splits[prevIdx].secretsFound;
							ls.baseTreasureFound += ls.splits[prevIdx].treasureFound;
						}
					}

					if ( !deferCompletion && t > 0 ) {
						LS_CompleteSplit( prevIdx, nowReal );
						if ( ls.runFinished ) return;
					}
				}
			}

			/* ---- IL mode: leaving the single map finishes the run ---- */
			if ( ls.runMode == LS_MODE_IL && !ls.runFinished ) {
				/* The split should have been completed above; if not, force it */
				if ( !ls.splits[ls.modeFirstIdx].splitDone ) {
					LS_CompleteSplit( ls.modeFirstIdx, nowReal );
				}
				if ( !ls.runFinished ) {
					LS_FinishRun( nowReal );
				}
				return;
			}

			/* ---- Mission mode: check if we left the mission boundary ---- */
			if ( ls.runMode == LS_MODE_MISSION && newIdx > ls.modeLastIdx ) {
				/* We've left the mission, run should already be finished via LS_CompleteSplit */
				if ( !ls.runFinished ) {
					LS_FinishRun( nowReal );
				}
				return;
			}

			/* ---- Advance to new map ---- */
			ls.currentMapIndex = newIdx;
			LS_UpdateCurVisRow();

			/* Keep mapLoadFreeze active for all maps.  Cutscenes
			   auto-clear when CA_ACTIVE; non-cutscene maps clear
			   when the playerstart handler fires (ls_loading>0). */

			if ( !ls.splits[newIdx].splitDone && LS_InActiveSet( newIdx ) ) {
				if ( ls.splits[newIdx].cutscene ) {
					ls.splits[newIdx].currentTimeMs = 0;
				}
				ls.splits[newIdx].d[di].totalAttempts++;
			}

			LS_Save();
		}
	}

afterMapChange:
	/* Update actualMapname AFTER map-change detection so the
	   realMapChange comparison above sees the old value. */
	Q_strncpyz( ls.actualMapname, currentMap, LS_MAX_MAPNAME );

	ls.lastServerTime = cl.serverTime;

	/* Ghost system: record + set playback cvars */
	LS_GhostFrame();

	/* --- Update ASL shared state for external LiveSplit --- */
	{
		int ai, doneN = 0, totalN = 0;
		for ( ai = ls.modeFirstIdx; ai <= ls.modeLastIdx && ai < ls.numMaps; ai++ ) {
			if ( ls.splits[ai].cutscene ) continue;
			totalN++;
			if ( ls.splits[ai].splitDone ) doneN++;
		}
		ls_aslState.active          = ls.active   ? 1 : 0;
		ls_aslState.finished        = ls.runFinished ? 1 : 0;
		ls_aslState.totalIGTMs      = LS_CumulativeTime( ls.modeLastIdx );
		ls_aslState.splitsDoneCount = doneN;
		ls_aslState.splitCount      = totalN;
		ls_aslState.runMode         = ls.runMode;
		ls_aslState.currentMapIndex = ls.currentMapIndex;
		ls_aslState.loading         = ( cls.state < CA_ACTIVE || isPaused
									  || ls.mapLoadFreeze ) ? 1 : 0;
		ls_aslState.runMission      = ls.runMission;
		Q_strncpyz( ls_aslState.currentMapName, ls.actualMapname,
					sizeof( ls_aslState.currentMapName ) );
	}
}

/* =====================================================================
   HUD Drawing  (LiveSplit-style layout)
   ===================================================================== */

/* Runtime layout values (computed from cvars each frame) */
static float _ls_hudX, _ls_hudY, _ls_panelW, _ls_scaleF;
static float _ls_rowH, _ls_charSz, _ls_bigSz, _ls_medSz, _ls_smallSz, _ls_statH;

/* Macro aliases so all drawing code uses runtime values transparently */
#define LS_HUD_X    _ls_hudX
#define LS_HUD_Y    _ls_hudY
#define LS_ROW_H    _ls_rowH
#define LS_CHAR_SZ  _ls_charSz
#define LS_BIG_SZ   _ls_bigSz
#define LS_MED_SZ   _ls_medSz
#define LS_SMALL_SZ _ls_smallSz
#define LS_STAT_H   _ls_statH
#define LS_PANEL_W  _ls_panelW
#define LS_SCALE    _ls_scaleF

/* Parse a color cvar string "R G B A" into vec4_t.
   RGB values are 0-255 integers, Alpha is 0.0-1.0 float.
   Example: "64 217 64 1.0"  ->  (0.25, 0.85, 0.25, 1.0)
   Falls back to defaultClr if the cvar string is empty or malformed. */
static void LS_ParseColorCvar( cvar_t *cv, vec4_t out, vec4_t defaultClr ) {
	if ( cv && cv->string[0] ) {
		float r, g, b, a;
		if ( sscanf( cv->string, "%f %f %f %f", &r, &g, &b, &a ) == 4 ) {
			out[0] = r / 255.0f; out[1] = g / 255.0f; out[2] = b / 255.0f; out[3] = a;
			return;
		}
		if ( sscanf( cv->string, "%f %f %f", &r, &g, &b ) == 3 ) {
			out[0] = r / 255.0f; out[1] = g / 255.0f; out[2] = b / 255.0f; out[3] = 1.0f;
			return;
		}
	}
	Vector4Copy( defaultClr, out );
}

static void LS_UpdateLayout( void ) {
	float s = ls_scaleCvar   ? ls_scaleCvar->value   : 1.0f;
	float op = ls_opacityCvar ? ls_opacityCvar->value : 1.0f;

	if ( s < 0.5f ) s = 0.5f;
	if ( s > 3.0f ) s = 3.0f;
	if ( op < 0.0f ) op = 0.0f;
	if ( op > 1.0f ) op = 1.0f;

	_ls_scaleF = s;
	_ls_hudX   = ls_xCvar ? ls_xCvar->value : 8.0f;
	_ls_hudY   = ls_yCvar ? ls_yCvar->value : 80.0f;
	_ls_panelW = ls_wCvar ? ls_wCvar->value : 178.0f;
	_ls_rowH   = 10.0f * s;
	_ls_charSz = 5.0f  * s;
	_ls_bigSz  = 9.0f  * s;
	_ls_medSz  = 6.0f  * s;
	_ls_smallSz= 4.5f  * s;
	_ls_statH  = 7.0f  * s;

	/* opacity: use cvar value, dim by ls_opacity_ui factor when console/UI is open */
	if ( cls.keyCatchers & ( KEYCATCH_CONSOLE | KEYCATCH_UI ) ) {
		float uiDim = ls_opacityUiCvar ? ls_opacityUiCvar->value : 0.2f;
		if ( uiDim < 0.0f ) uiDim = 0.0f;
		if ( uiDim > 1.0f ) uiDim = 1.0f;
		ls_opacity = op * uiDim;
	} else {
		ls_opacity = op;
	}

	/* background alpha: independent from text opacity */
	{
		float bg = ls_bgalphaCvar ? ls_bgalphaCvar->value : 0.82f;
		if ( bg < 0.0f ) bg = 0.0f;
		if ( bg > 1.0f ) bg = 1.0f;
		ls_bgalpha = bg;
	}
}

static void LS_DrawStringR( int rightEdge, int y, float charSize, const char *str, float *color ) {
	int w = (int)strlen( str ) * (int)charSize;
	LS_DrawString( rightEdge - w, y, charSize, str, color );
}

/* helper: draw a split row */
static void LS_DrawSplitRow( int splitIdx, float x, float y,
	float *currentMapC, float *completedC, float *futureMap,
	float *timerColor, float *timeWhite, float *timeDim,
	float *aheadColor, float *behindColor, float *goldColor,
	float *hlBg ) {

	float *nameClr;
	char timeBuf[32], deltaBuf[32];
	qboolean isCur  = qfalse;
	qboolean isDone = ls.splits[splitIdx].splitDone;
	int cumTime, cumBest, cumPB;
	int timeRight   = (int)( x + LS_PANEL_W - 4 );
	int pbDeltaR    = (int)( x + LS_PANEL_W - 40 );
	int goldDeltaR  = ( ls_showdeltasCvar && ls_showdeltasCvar->integer )
		? (int)( x + LS_PANEL_W - 72 )
		: (int)( x + LS_PANEL_W - 40 );
	int di = LS_CurDiffIdx();

	if ( splitIdx == ls.currentMapIndex ) {
		isCur = qtrue;
	} else if ( ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps &&
				ls.splits[ls.currentMapIndex].cutscene ) {
		int target = LS_ActiveRealSplit();
		if ( target == splitIdx ) isCur = qtrue;
	}

	if ( isCur ) {
		LS_FillRect( x + 1, y, LS_PANEL_W - 2, LS_ROW_H, hlBg );
	}

	if ( ls.splits[splitIdx].goldFlashMs > 0 ) {
		int elapsed = Sys_Milliseconds() - ls.splits[splitIdx].goldFlashMs;
		if ( elapsed >= 0 && elapsed < 1500 ) {
			float t = 1.0f - ( (float)elapsed / 1500.0f );
			vec4_t goldFlash = { 1.0f, 0.85f, 0.20f, 0.25f * t * t };
			LS_FillRect( x + 1, y, LS_PANEL_W - 2, LS_ROW_H, goldFlash );
		} else {
			ls.splits[splitIdx].goldFlashMs = 0; /* expired */
		}
	}

	if ( isCur )       nameClr = currentMapC;
	else if ( isDone ) nameClr = completedC;
	else               nameClr = futureMap;

	{
		/* Name column: available space = panel width minus visible columns */
		float nameMaxPx = LS_PANEL_W - 40; /* base: time column */
		if ( ls_showdeltasCvar && ls_showdeltasCvar->integer ) nameMaxPx -= 32;
		if ( ls_showbestdeltasCvar && ls_showbestdeltasCvar->integer ) nameMaxPx -= 32;
		const char *dispName = LS_SplitDisplayName( splitIdx, nameMaxPx, LS_CHAR_SZ );
		LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_CHAR_SZ, dispName, nameClr );
	}

	cumTime = LS_CumulativeTime( splitIdx );
	cumBest = LS_CumulativeBest( splitIdx );
	cumPB   = LS_CumulativePB( splitIdx );

	/* gold delta column (segment vs gold) */
	if ( ls_showbestdeltasCvar && ls_showbestdeltasCvar->integer && !ls.splits[splitIdx].splitSkipped ) {
		int gold = ls.splits[splitIdx].d[di].bestTimeMs;
		int compareGold = gold;

		if ( isDone && ls.splits[splitIdx].prevGoldMs > 0 ) {
			compareGold = ls.splits[splitIdx].prevGoldMs;
		}

		if ( compareGold > 0 ) {
			int segTime = ls.splits[splitIdx].currentTimeMs;
			qboolean showDelta = qfalse;

			if ( isDone ) {
				showDelta = qtrue;
			} else if ( isCur && segTime >= gold - 10000 ) {
				showDelta = qtrue;
			}

			if ( showDelta ) {
				int delta = segTime - compareGold;
				if ( delta == 0 ) {
					LS_DrawStringR( goldDeltaR, (int)( y + 2 ), LS_SMALL_SZ, "---", timeDim );
				} else {
					float *deltaClr;
					LS_FormatDelta( delta, deltaBuf, sizeof( deltaBuf ) );
					if ( delta < 0 ) deltaClr = ( isDone && ls.splits[splitIdx].prevGoldMs > 0 && gold < ls.splits[splitIdx].prevGoldMs ) ? goldColor : aheadColor;
					else             deltaClr = behindColor;
					LS_DrawStringR( goldDeltaR, (int)( y + 2 ), LS_SMALL_SZ, deltaBuf, deltaClr );
				}
			}
		}
	}

	/* PB delta column (cumulative actual vs cumulative PB segments) */
	if ( ( ls_showdeltasCvar && ls_showdeltasCvar->integer ) &&  /* separate cvar from gold */
		 !ls.splits[splitIdx].splitSkipped &&
		 cumPB > 0 && ( isDone || isCur ) ) {
		qboolean showPB = qfalse;
		if ( isDone ) {
			showPB = qtrue;
		} else if ( isCur ) {
			int pbSeg = ls.splits[splitIdx].d[di].pbSegmentMs;
			int segTime = ls.splits[splitIdx].currentTimeMs;
			if ( pbSeg > 0 && segTime >= pbSeg - 10000 ) {
				showPB = qtrue;
			}
		}
		if ( showPB ) {
			int pbDelta = cumTime - cumPB;
			if ( pbDelta == 0 ) {
				LS_DrawStringR( pbDeltaR, (int)( y + 2 ), LS_SMALL_SZ, "---", timeDim );
			} else {
				float *pbClr = pbDelta < 0 ? aheadColor : behindColor;
				LS_FormatDelta( pbDelta, deltaBuf, sizeof( deltaBuf ) );
				LS_DrawStringR( pbDeltaR, (int)( y + 2 ), LS_SMALL_SZ, deltaBuf, pbClr );
			}
		}
	}

	/* time column */
	if ( isDone ) {
		if ( ls.splits[splitIdx].splitSkipped ) {
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, "---", timeDim );
		} else if ( cumTime > 0 ) {
			LS_FormatTime( cumTime, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, timeWhite );
		}
	} else if ( isCur ) {
		if ( ls.splits[splitIdx].d[di].pbSegmentMs > 0 ) {
			int cumPrev = LS_CumulativeTime( splitIdx - 1 );
			int cumWithPb = cumPrev + ls.splits[splitIdx].d[di].pbSegmentMs;
			LS_FormatTime( cumWithPb, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, currentMapC );
		} else if ( cumBest > 0 ) {
			LS_FormatTime( cumBest, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, currentMapC );
		}
	} else if ( cumBest > 0 ) {
		LS_FormatTime( cumBest, timeBuf, sizeof( timeBuf ) );
		LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, timeDim );
	}
}

static void LS_Draw( void ) {
	int i;
	float x, y, panelH;
	int scrollStart, scrollEnd, visCount;
	int lastVisIdx;
	qboolean pinLast;
	char timeBuf[32], deltaBuf[32];
	int di = LS_CurDiffIdx();

	/* toggle flags from cvars */
	qboolean showHeader = ls_showheaderCvar ? ls_showheaderCvar->integer : 1;
	qboolean showStats  = ls_showstatsCvar  ? ls_showstatsCvar->integer  : 1;
	qboolean showSeg    = ls_showsegCvar    ? ls_showsegCvar->integer    : 1;
	qboolean showRgt    = ls_showrgtCvar    ? ls_showrgtCvar->integer    : 1;
	qboolean showPb     = ls_showpbCvar     ? ls_showpbCvar->integer     : 1;
	qboolean showBest   = ls_showbestCvar   ? ls_showbestCvar->integer   : 1;
	qboolean showTimer  = ls_showtimerCvar  ? ls_showtimerCvar->integer  : 1;
	qboolean showSeps   = ls_showsepsCvar   ? ls_showsepsCvar->integer   : 1;
	qboolean showDeltas     = ls_showdeltasCvar     ? ls_showdeltasCvar->integer     : 1;
	qboolean showBestDeltas = ls_showbestdeltasCvar ? ls_showbestdeltasCvar->integer : 1;
	qboolean showAtt        = ls_showattCvar        ? ls_showattCvar->integer        : 1;
	int      colAlign   = ls_alignCvar      ? ls_alignCvar->integer      : 0; /* 0=right, 1=left */
	int leftEdge, rightEdge, labelsX, labelsValR;

	/* colours */
	vec4_t panelBg     = { 0.04f, 0.04f, 0.06f, 0.0f }; /* alpha set below from ls_bgalpha */
	vec4_t panelBorder = { 0.20f, 0.35f, 0.15f, 0.08f };
	vec4_t headerColor = { 0.35f, 0.75f, 0.20f, 1.00f };
	vec4_t mapNameClr  = { 0.55f, 0.62f, 0.50f, 0.85f };
	vec4_t currentMapC = { 1.00f, 1.00f, 0.60f, 1.00f };
	vec4_t completedC  = { 0.72f, 0.72f, 0.72f, 0.80f };
	vec4_t futureMap   = { 0.36f, 0.36f, 0.40f, 0.48f };
	vec4_t aheadColor  = { 0.25f, 0.85f, 0.25f, 1.00f };
	vec4_t behindColor = { 0.85f, 0.25f, 0.25f, 1.00f };
	vec4_t goldColor   = { 1.00f, 0.85f, 0.20f, 1.00f };
	vec4_t timeWhite   = { 0.85f, 0.88f, 0.85f, 0.90f };
	vec4_t timeDim     = { 0.48f, 0.48f, 0.50f, 0.52f };
	vec4_t timerColor  = { 0.85f, 0.95f, 0.80f, 1.00f };
	vec4_t segTimerClr = { 0.62f, 0.65f, 0.62f, 0.82f };
	vec4_t finishedC   = { 0.25f, 0.85f, 0.25f, 1.00f };  /* default: green (PB) */
	vec4_t finishBad   = { 0.85f, 0.25f, 0.25f, 1.00f };  /* red (worse than PB) */
	vec4_t pausedColor = { 0.90f, 0.70f, 0.20f, 1.00f };
	vec4_t sepColor    = { 0.22f, 0.38f, 0.12f, 0.18f };
	vec4_t hlBg        = { 0.10f, 0.20f, 0.06f, 0.32f };
	vec4_t scrollInd   = { 0.42f, 0.42f, 0.46f, 0.32f };
	vec4_t labelColor  = { 0.42f, 0.48f, 0.38f, 0.62f };
	vec4_t noDataColor = { 0.32f, 0.32f, 0.32f, 0.42f };
	vec4_t statColor   = { 0.42f, 0.48f, 0.38f, 0.60f };
	vec4_t modeColor   = { 0.55f, 0.55f, 0.62f, 0.70f };

	/* Apply color customization cvars (override defaults when set) */
	LS_ParseColorCvar( ls_clr_aheadCvar,  aheadColor,  aheadColor );
	LS_ParseColorCvar( ls_clr_behindCvar, behindColor, behindColor );
	LS_ParseColorCvar( ls_clr_goldCvar,   goldColor,   goldColor );
	LS_ParseColorCvar( ls_clr_headerCvar, headerColor, headerColor );
	LS_ParseColorCvar( ls_clr_timerCvar,  timerColor,  timerColor );
	LS_ParseColorCvar( ls_clr_textCvar,   timeWhite,   timeWhite );
	LS_ParseColorCvar( ls_clr_bgCvar,     panelBg,     panelBg );
	LS_ParseColorCvar( ls_clr_borderCvar, panelBorder, panelBorder );
	LS_ParseColorCvar( ls_clr_mapnameCvar, mapNameClr, mapNameClr );
	LS_ParseColorCvar( ls_clr_currentCvar, currentMapC, currentMapC );
	LS_ParseColorCvar( ls_clr_completedCvar, completedC, completedC );
	LS_ParseColorCvar( ls_clr_futureCvar, futureMap,   futureMap );
	LS_ParseColorCvar( ls_clr_dimCvar,    timeDim,     timeDim );
	LS_ParseColorCvar( ls_clr_segtimerCvar, segTimerClr, segTimerClr );
	LS_ParseColorCvar( ls_clr_pausedCvar, pausedColor, pausedColor );
	LS_ParseColorCvar( ls_clr_sepCvar,    sepColor,    sepColor );
	LS_ParseColorCvar( ls_clr_highlightCvar, hlBg,     hlBg );
	LS_ParseColorCvar( ls_clr_labelCvar,  labelColor,  labelColor );
	/* Sync derived colors */
	Vector4Copy( labelColor, statColor );
	/* Sync finishedC with ahead color (PB color = ahead color) */
	Vector4Copy( aheadColor, finishedC );
	/* Sync finishBad with behind color */
	Vector4Copy( behindColor, finishBad );

	LS_UpdateLayout();

	/* Apply background alpha from cvar */
	panelBg[3] = ls_bgalpha;

	/* Determine finished color: compare final time to PB */
	if ( ls.runFinished ) {
		int *pb = NULL;
		LS_GetCatPB( &pb );
		/* If the final time equals PB it was set this run (new PB) > keep green.
		   If PB existed before and we're slower > red. */
		if ( pb && *pb > 0 && ls.runTotalIGTMs > *pb ) {
			Vector4Copy( finishBad, finishedC );
		}
	}

	x = LS_HUD_X;
	if ( colAlign == 1 ) {
		/* right-align: mirror panel to right side of screen */
		x = 640.0f - LS_HUD_X - LS_PANEL_W;
	}
	y = LS_HUD_Y;

	/* Content positioning helpers for alignment mode */
	leftEdge  = (int)( x + 3 );
	if ( colAlign == 1 ) {
		rightEdge  = (int)( x + LS_PANEL_W - 1 );
		labelsX    = leftEdge;
		labelsValR = (int)( x + 42 );
	} else {
		rightEdge  = (int)( x + LS_PANEL_W - 4 );
		labelsX    = (int)( x + LS_PANEL_W - 55 );
		labelsValR = rightEdge;
	}

	lastVisIdx = ls.numVisible - 1;
	pinLast = qfalse;

	{
		int maxRows = ls_maxrowsCvar ? ls_maxrowsCvar->integer : 0;
		int cap = ( maxRows >= 2 ) ? maxRows : 6;  /* 0 or 1 = auto (default 6) */

		if ( ls.numVisible <= cap ) {
			scrollStart = 0;
			scrollEnd   = lastVisIdx;
		} else {
			int scrollSize = cap - 1;

			if ( ls.curVisRow < 0 ) {
				scrollStart = 0;
			} else {
				scrollStart = ls.curVisRow - ( scrollSize / 2 );
				if ( scrollStart < 0 ) scrollStart = 0;
			}
			scrollEnd = scrollStart + scrollSize - 1;

			if ( scrollEnd >= lastVisIdx - 1 ) {
				scrollEnd   = lastVisIdx;
				scrollStart = scrollEnd - scrollSize;
				if ( scrollStart < 0 ) scrollStart = 0;
			} else {
				pinLast = qtrue;
			}
		}
	}

	visCount = scrollEnd - scrollStart + 1;
	if ( visCount < 0 ) visCount = 0;

	/* panel height (dynamic: skip areas whose elements are all hidden) */
	/* Uses a measuring pass that mirrors the drawing pass exactly.
	   Each section adds height only when its toggle is on. */
	{
		int totalRows = visCount + ( pinLast ? 1 : 0 );
		float s = LS_SCALE;
		float h = 0;
		qboolean hasBelow;

		h += 14*s + 2*s;  /* header + sep */
		if ( ls.cheatsUsed )       h += 8*s;
		if ( ls.settingsModified ) h += 8*s;
		if ( showHeader ) h += 6*s + 2*s;
		h += totalRows * LS_ROW_H;

		if ( visCount > 0 && scrollStart > 0 )                       h += 6*s;
		if ( visCount > 0 && !pinLast && scrollEnd < lastVisIdx )     h += 6*s;
		if ( pinLast )                                                h += 2*s;

		if ( ls.runMode == LS_MODE_IL ) {
			hasBelow = ( showTimer || showPb || showBest );
			if ( hasBelow ) h += 2*s;
			/* timer row */
			if ( showTimer ) h += 10*s;
			/* PB row */
			if ( showPb ) h += 6*s;
			/* Best row */
			if ( showBest ) h += 6*s;
			/* if only labels (no timer), ensure minimum height for them */
			if ( !showTimer && (showPb || showBest) ) {
				/* labels already counted above, nothing extra needed */
			}
		} else {
			hasBelow = ( showTimer || showPb || showBest || showSeg || showStats || showRgt );
			if ( hasBelow ) h += 2*s;
			/* big timer */
			if ( showTimer ) h += 8*s;
			/* PB label row */
			if ( showPb ) h += 5*s;
			/* Best label row (only in big area when seg hidden) */
			if ( showBest && !showSeg ) h += 5*s;
			/* segment timer area */
			if ( showSeg ) h += 7*s;
			/* stats section */
			if ( showStats ) {
				h += 2*s + 5 * LS_STAT_H;
			}
		}
		if ( showRgt ) h += 2*s + 9*s + 1;

		/* 100% category rows */
		if ( ls_100pctCvar && ls_100pctCvar->integer ) {
			h += 2*s;              /* separator */
			if ( ls.runFinished ) {
				h += 8*s;          /* TAG bar only (no detail rows) */
			} else {
				h += 2 * LS_STAT_H; /* secrets + treasure rows */
			}
		}

		panelH = h;
	}

	LS_FillRect( x, y, LS_PANEL_W, panelH, panelBg );
	LS_DrawRect( x, y, LS_PANEL_W, panelH, panelBorder );

	/* ---- header: difficulty/mode + completions/attempts ---- */
	{
		float *dotColor;
		const char *skillName;
		const char *modeStr = "";
		char headerBuf[96];
		char counterBuf[32];
		int skill;

		/* Status dot color */
		if ( ls.runFinished )                                        dotColor = finishedC;
		else if ( ls.manualPause )                                   dotColor = pausedColor;
		else if ( Cvar_VariableIntegerValue( "cl_paused" ) )         dotColor = pausedColor;
		else if ( !ls.active )                                       dotColor = statColor;
		else                                                         dotColor = headerColor;

		skill = Cvar_VariableIntegerValue( "g_gameskill" );
		if ( skill < 1 || skill > 3 ) skill = Cvar_VariableIntegerValue( "g_spSkill" );
		switch ( skill ) {
			case 1:  skillName = "Don't hurt me."; break;
			case 2:  skillName = "Bring 'em on!"; break;
			case 3:  skillName = "I am Death incarnate!"; break;
			default: skillName = "Normal"; break;
		}

		/* Build header string based on mode */
		switch ( ls.runMode ) {
		case LS_MODE_MISSION:
			if ( ls.runMission >= 1 && ls.runMission <= LS_NUM_MISSION_GROUPS ) {
				const char *opName;
				switch ( ls.runMission ) {
					case 1: opName = "Ominous Rumors & Dark Secret"; break;
					case 2: opName = "Weapons of Vengeance"; break;
					case 3: opName = "Deadly Designs"; break;
					case 4: opName = "Deathshead's Playground"; break;
					case 5: opName = "Return Engagement & Op. Resurrection"; break;
					default: opName = "Mission"; break;
				}
				Com_sprintf( headerBuf, sizeof( headerBuf ), "%s", opName );
			} else {
				Com_sprintf( headerBuf, sizeof( headerBuf ), "%s", skillName );
			}
			break;
		case LS_MODE_IL:
			Q_strncpyz( headerBuf, "Individual Level", sizeof( headerBuf ) );
			break;
		default:
			Q_strncpyz( headerBuf, skillName, sizeof( headerBuf ) );
			break;
		}

		/* Draw status dot + header in constant color */
		LS_DrawString( (int)( x + 3 ), (int)( y + 4 ), LS_CHAR_SZ, "\x07", dotColor );

		/* Populate counter first so we know its width for header truncation */
		if ( showAtt ) {
			int *catAtt, *catComp;
			LS_GetCatAttempts( &catAtt );
			LS_GetCatCompletions( &catComp );
			Com_sprintf( counterBuf, sizeof( counterBuf ), "%d/%d",
				*catComp, *catAtt );
		} else {
			counterBuf[0] = '\0';
		}

		{
			/* Truncate header if it doesn't fit: try full, then trim with ".." */
			int dotOffset = (int)( LS_CHAR_SZ * 1.4f );
			int headerX = (int)( x + 3 ) + dotOffset;
			int counterW = (int)strlen( counterBuf ) * (int)LS_CHAR_SZ + 6;
			int availW = (int)( x + LS_PANEL_W - 3 ) - headerX - counterW;
			int headerW = (int)strlen( headerBuf ) * (int)LS_CHAR_SZ;

			if ( headerW > availW && ls.runMode == LS_MODE_MISSION ) {
				/* Try short name first */
				const char *shortOp = "";
				switch ( ls.runMission ) {
					case 1: shortOp = "Ominous Rumors"; break;
					case 2: shortOp = "Vengeance"; break;
					case 3: shortOp = "Deadly Designs"; break;
					case 4: shortOp = "Deathshead"; break;
					case 5: shortOp = "Return & Resurrection"; break;
					default: shortOp = "Mission"; break;
				}
				Q_strncpyz( headerBuf, shortOp, sizeof( headerBuf ) );
				headerW = (int)strlen( headerBuf ) * (int)LS_CHAR_SZ;
				if ( headerW > availW ) {
					int maxCh = (int)( availW / LS_CHAR_SZ ) - 2;
					if ( maxCh < 1 ) maxCh = 1;
					headerBuf[maxCh] = '.';
					headerBuf[maxCh + 1] = '.';
					headerBuf[maxCh + 2] = '\0';
				}
			} else if ( headerW > availW ) {
				int maxCh = (int)( availW / LS_CHAR_SZ ) - 2;
				if ( maxCh < 1 ) maxCh = 1;
				headerBuf[maxCh] = '.';
				headerBuf[maxCh + 1] = '.';
				headerBuf[maxCh + 2] = '\0';
			}

			LS_DrawString( headerX, (int)( y + 4 ), LS_CHAR_SZ, headerBuf, headerColor );

			/* [100%] tag in header when 100% category is enabled */
			if ( ls_100pctCvar && ls_100pctCvar->integer ) {
				vec4_t pctTagClr = { 0.25f, 0.85f, 0.25f, 0.80f };
				int tagX = headerX + (int)strlen( headerBuf ) * (int)LS_CHAR_SZ + (int)( LS_CHAR_SZ * 0.6f );
				LS_DrawString( tagX, (int)( y + 4 ), LS_SMALL_SZ, "[100%]", pctTagClr );
			}
		}

		/* completions / attempts for current category + difficulty */
		if ( showAtt ) {
			int cntX = (int)( x + LS_PANEL_W - 3 - (int)strlen( counterBuf ) * (int)LS_CHAR_SZ );
			LS_DrawString( cntX, (int)( y + 4 ), LS_CHAR_SZ, counterBuf, mapNameClr );
		}
	}
	y += 14 * LS_SCALE;

	/* Cheats warning bar */
	if ( ls.cheatsUsed ) {
		float cheatsRed[] = { 1.0f, 0.2f, 0.2f, 1.0f };
		float cheatsBg[]  = { 0.5f, 0.0f, 0.0f, 0.35f };
		LS_FillRect( x + 2, y, LS_PANEL_W - 4, 7 * LS_SCALE, cheatsBg );
		LS_DrawString( (int)( x + 4 ), (int)( y + 1 ), LS_SMALL_SZ, "CHEATS - INVALID RUN", cheatsRed );
		y += 8 * LS_SCALE;
	}

	/* Settings modified warning bar */
	if ( ls.settingsModified ) {
		float warnYellow[] = { 1.0f, 0.8f, 0.2f, 1.0f };
		float warnBg[]     = { 0.4f, 0.3f, 0.0f, 0.35f };
		char settingsBuf[48];
		Com_sprintf( settingsBuf, sizeof( settingsBuf ), "SETTINGS MODIFIED (%d)", ls.settingsModCount );
		LS_FillRect( x + 2, y, LS_PANEL_W - 4, 7 * LS_SCALE, warnBg );
		LS_DrawString( (int)( x + 4 ), (int)( y + 1 ), LS_SMALL_SZ, settingsBuf, warnYellow );
		y += 8 * LS_SCALE;
	}

	if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
	y += 2 * LS_SCALE;

	/* Column header labels */
	if ( showHeader ) {
		int colPbR    = (int)( x + LS_PANEL_W - 40 );
		/* Best +/- header shifts right into PB column when PB deltas are hidden */
		int colGoldR  = showDeltas ? (int)( x + LS_PANEL_W - 72 ) : colPbR;
		int colTimeR  = (int)( x + LS_PANEL_W - 4 );
		int hdrTextY  = (int)( y + 1 );  /* nudge down to vertically center in header row */
		if ( showBestDeltas ) {
			LS_DrawStringR( colGoldR, hdrTextY, LS_SMALL_SZ, "Best +/-", labelColor );
		}
		if ( showDeltas ) {
			LS_DrawStringR( colPbR,   hdrTextY, LS_SMALL_SZ, "+/-", labelColor );
		}
		LS_DrawStringR( colTimeR, hdrTextY, LS_SMALL_SZ, "Time", labelColor );
		y += 6 * LS_SCALE;

		if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
		y += 2 * LS_SCALE;
	}

	if ( visCount > 0 ) {
		if ( scrollStart > 0 ) {
			LS_DrawString( (int)( x + LS_PANEL_W / 2 - 6 ), (int)y, LS_SMALL_SZ, "...", scrollInd );
			y += 6 * LS_SCALE;
		}

		for ( i = scrollStart; i <= scrollEnd; i++ ) {
			LS_DrawSplitRow( ls.visMap[i], x, y,
				currentMapC, completedC, futureMap,
				timerColor, timeWhite, timeDim,
				aheadColor, behindColor, goldColor, hlBg );
			y += LS_ROW_H;
		}

		if ( !pinLast && scrollEnd < lastVisIdx ) {
			LS_DrawString( (int)( x + LS_PANEL_W / 2 - 6 ), (int)y, LS_SMALL_SZ, "...", scrollInd );
			y += 6 * LS_SCALE;
		}

		if ( pinLast ) {
			if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
			y += 2 * LS_SCALE;

			LS_DrawSplitRow( ls.visMap[lastVisIdx], x, y,
				currentMapC, completedC, futureMap,
				timerColor, timeWhite, timeDim,
				aheadColor, behindColor, goldColor, hlBg );
			y += LS_ROW_H;
		}
	}

	/* Separator after splits: only if there is content below */
	{
		qboolean hasBelow;
		if ( ls.runMode == LS_MODE_IL ) {
			hasBelow = ( showTimer || showPb || showBest );
		} else {
			hasBelow = ( showTimer || showPb || showBest || showSeg || showStats || showRgt );
		}
		if ( hasBelow ) {
			if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
			y += 2 * LS_SCALE;
		}
	}

	if ( ls.runMode == LS_MODE_IL ) {
	/* ======== IL: flow layout – timer then labels, each advances y ======== */
	{
		int activeIdx = LS_ActiveRealSplit();

		/* Big timer */
		if ( showTimer ) {
			int totalIGT = LS_CumulativeTime( ls.modeLastIdx );
			LS_FormatTime( totalIGT, timeBuf, sizeof( timeBuf ) );
			if ( colAlign == 1 ) {
				LS_DrawStringR( rightEdge, (int)( y + 2 ), LS_BIG_SZ, timeBuf,
					ls.runFinished ? finishedC : timerColor );
			} else {
				LS_DrawString( leftEdge, (int)( y + 2 ), LS_BIG_SZ, timeBuf,
					ls.runFinished ? finishedC : timerColor );
			}
			y += 10 * LS_SCALE;
		}

		/* PB label row */
		if ( showPb ) {
			LS_DrawString( labelsX, (int)( y + 1 ), LS_SMALL_SZ, "PB:", labelColor );
			if ( activeIdx >= 0 && ls.splits[activeIdx].d[di].pbSegmentMs > 0 ) {
				LS_FormatTime( ls.splits[activeIdx].d[di].pbSegmentMs, timeBuf, sizeof( timeBuf ) );
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, timeWhite );
			} else {
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
			}
			y += 6 * LS_SCALE;
		}

		/* Best label row */
		if ( showBest ) {
			LS_DrawString( labelsX, (int)( y + 1 ), LS_SMALL_SZ, "Best:", labelColor );
			if ( activeIdx >= 0 && ls.splits[activeIdx].d[di].bestTimeMs > 0 ) {
				LS_FormatTime( ls.splits[activeIdx].d[di].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, goldColor );
			} else {
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
			}
			y += 6 * LS_SCALE;
		}
	}

	} else {
	/* ======== Non-IL: flow layout – each section advances y ======== */
	{
		int activeIdx = LS_ActiveRealSplit();

		/* --- Big timer --- */
		if ( showTimer ) {
			int totalIGT = LS_CumulativeTime( ls.modeLastIdx );
			LS_FormatTime( totalIGT, timeBuf, sizeof( timeBuf ) );
			if ( colAlign == 1 ) {
				LS_DrawStringR( rightEdge, (int)( y + 1 ), LS_BIG_SZ, timeBuf,
					ls.runFinished ? finishedC : timerColor );
			} else {
				LS_DrawString( leftEdge, (int)( y + 1 ), LS_BIG_SZ, timeBuf,
					ls.runFinished ? finishedC : timerColor );
			}
			y += 8 * LS_SCALE;
		}

		/* --- PB label row --- */
		if ( showPb ) {
			LS_DrawString( labelsX, (int)( y + 1 ), LS_SMALL_SZ, "PB:", labelColor );
			if ( activeIdx >= 0 && ls.splits[activeIdx].d[di].pbSegmentMs > 0 ) {
				LS_FormatTime( ls.splits[activeIdx].d[di].pbSegmentMs, timeBuf, sizeof( timeBuf ) );
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, timeWhite );
			} else {
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
			}
			y += 5 * LS_SCALE;
		}

		/* --- Best label row (only in big-timer area when segment timer is hidden) --- */
		if ( showBest && !showSeg ) {
			LS_DrawString( labelsX, (int)( y + 1 ), LS_SMALL_SZ, "Best:", labelColor );
			if ( activeIdx >= 0 && ls.splits[activeIdx].d[di].bestTimeMs > 0 ) {
				LS_FormatTime( ls.splits[activeIdx].d[di].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, goldColor );
			} else {
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
			}
			y += 5 * LS_SCALE;
		}
	}

	/* ================ Segment timer + Best ================ */
	if ( showSeg ) {
		int segTime = 0;
		int activeIdx = LS_ActiveRealSplit();
		float segFontSz = showTimer ? LS_MED_SZ : LS_BIG_SZ;
		float segAreaH  = showTimer ? 7 * LS_SCALE : 7 * LS_SCALE;
		int segTextOff = showTimer ? 1 : -3; /* move segment timer up when main timer hidden */

		if ( activeIdx >= 0 ) {
			segTime = ls.splits[activeIdx].currentTimeMs;
		}
		LS_FormatTime( segTime, timeBuf, sizeof( timeBuf ) );

		if ( colAlign == 1 ) {
			LS_DrawStringR( rightEdge, (int)( y + segTextOff ), segFontSz, timeBuf, segTimerClr );
		} else {
			LS_DrawString( leftEdge, (int)( y + segTextOff ), segFontSz, timeBuf, segTimerClr );
		}

		if ( showBest ) {
			LS_DrawString( labelsX, (int)( y + 1 ), LS_SMALL_SZ, "Best:", labelColor );
			if ( activeIdx >= 0 && ls.splits[activeIdx].d[di].bestTimeMs > 0 ) {
				LS_FormatTime( ls.splits[activeIdx].d[di].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, goldColor );
			} else {
				LS_DrawStringR( labelsValR, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
			}
		}
		y += segAreaH;
	}

	/* ================ Stat rows ================ */
	if ( showStats ) {

	if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
	y += 2 * LS_SCALE;

	/* Row 1: Previous Segment / Live Segment */
	{
		int valW, valX;
		int activeIdx = LS_ActiveRealSplit();
		qboolean showLive = qfalse;

		/* Show live segment when current split is near gold time */
		if ( ls.active && !ls.runFinished && activeIdx >= 0 ) {
			int gold = ls.splits[activeIdx].d[di].bestTimeMs;
			int seg  = ls.splits[activeIdx].currentTimeMs;
			if ( gold > 0 && seg >= gold - 10000 ) {
				showLive = qtrue;
			}
		}

		if ( showLive ) {
			int gold = ls.splits[activeIdx].d[di].bestTimeMs;
			int seg  = ls.splits[activeIdx].currentTimeMs;
			int liveDelta = seg - gold;

			LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Live Segment", currentMapC );
			if ( liveDelta == 0 ) {
				valW = 3 * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "---", timeDim );
			} else {
				LS_FormatDelta( liveDelta, deltaBuf, sizeof( deltaBuf ) );
				valW = (int)strlen( deltaBuf ) * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, deltaBuf,
					liveDelta < 0 ? aheadColor : behindColor );
			}
		} else {
			qboolean hasPrev;
			int prevSeg = LS_CalcPreviousSegment( &hasPrev );

			LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Previous Segment", labelColor );
			if ( hasPrev ) {
				if ( prevSeg == 0 ) {
					valW = 3 * (int)LS_SMALL_SZ;
					valX = (int)( x + LS_PANEL_W - 4 ) - valW;
					LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "---", timeDim );
				} else {
					LS_FormatDelta( prevSeg, deltaBuf, sizeof( deltaBuf ) );
					valW = (int)strlen( deltaBuf ) * (int)LS_SMALL_SZ;
					valX = (int)( x + LS_PANEL_W - 4 ) - valW;
					LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, deltaBuf,
						prevSeg < 0 ? aheadColor : behindColor );
				}
			} else {
				valW = 5 * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
			}
		}
		y += LS_STAT_H;
	}

	/* Row 2: Sum of Best Segments */
	{
		int sob = LS_CalcSumOfBests();
		int valW, valX;

		LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Sum of Best Segments", labelColor );
		if ( sob >= 0 ) {
			LS_FormatTime( sob, timeBuf, sizeof( timeBuf ) );
			valW = (int)strlen( timeBuf ) * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, timeWhite );
		} else {
			valW = 5 * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
		}
		y += LS_STAT_H;
	}

	/* Row 3: Best Possible Time */
	{
		int bpt = LS_CalcBestPossible();
		int valW, valX;

		LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Best Possible Time", labelColor );
		if ( bpt >= 0 ) {
			LS_FormatTime( bpt, timeBuf, sizeof( timeBuf ) );
			valW = (int)strlen( timeBuf ) * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, timeWhite );
		} else {
			valW = 5 * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
		}
		y += LS_STAT_H;
	}

	/* Row 4: Possible Time Save */
	{
		int activeIdx = LS_ActiveRealSplit();
		int valW, valX;

		LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Possible Time Save", labelColor );
		if ( activeIdx >= 0 && ls.splits[activeIdx].d[di].pbSegmentMs > 0 &&
			 ls.splits[activeIdx].d[di].bestTimeMs > 0 ) {
			int save = ls.splits[activeIdx].d[di].pbSegmentMs - ls.splits[activeIdx].d[di].bestTimeMs;
			if ( save > 0 ) {
				LS_FormatDelta( save, deltaBuf, sizeof( deltaBuf ) );
				valW = (int)strlen( deltaBuf ) * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, deltaBuf, aheadColor );
			} else {
				valW = 4 * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "0.00", goldColor );
			}
		} else {
			valW = 5 * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
		}
		y += LS_STAT_H;
	}

	/* Row 5: Best Segments */
	{
		qboolean hasBSD;
		int bsd = LS_CalcBestSegmentsDelta( &hasBSD );
		int valW, valX;

		LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Best Segments", labelColor );
		if ( hasBSD ) {
			LS_FormatDelta( bsd, deltaBuf, sizeof( deltaBuf ) );
			valW = (int)strlen( deltaBuf ) * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, deltaBuf,
				bsd <= 0 ? aheadColor : behindColor );
		} else {
			valW = 5 * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "-----", noDataColor );
		}
		y += LS_STAT_H;
	}

	} /* end showStats */

	} /* end if/else IL */

	/* ================ 100% Category: Secrets & Treasures ================ */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		char secBuf[32], tresBuf[32];
		int valW, valX;
		vec4_t pctLabelClr    = { 0.45f, 0.70f, 0.35f, 0.70f };
		vec4_t pctCompleteClr = { 0.25f, 0.85f, 0.25f, 1.00f };
		vec4_t pctPartialClr  = { 0.85f, 0.85f, 0.25f, 1.00f };
		vec4_t pctValueClr    = { 0.80f, 0.80f, 0.80f, 0.90f };

		if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
		y += 2 * LS_SCALE;

		if ( ls.runFinished ) {
			/* After run: sum all completed splits, show only TAG */
			int cumSecF = 0, cumSecT = 0, cumTresF = 0, cumTresT = 0;
			qboolean allFound;
			for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
				if ( ls.splits[i].cutscene ) continue;
				if ( ls.splits[i].splitDone ) {
					cumSecF  += ls.splits[i].secretsFound;
					cumSecT  += ls.splits[i].secretsTotal;
					cumTresF += ls.splits[i].treasureFound;
					cumTresT += ls.splits[i].treasureTotal;
				}
			}
			/* 0/0 counts as complete (no collectibles on that map) */
			allFound = ( cumSecF >= cumSecT && cumTresF >= cumTresT );

			if ( allFound ) {
				float tagGreen[] = { 0.20f, 0.85f, 0.20f, 1.0f };
				float tagBg[]    = { 0.05f, 0.30f, 0.05f, 0.35f };
				LS_FillRect( x + 2, y, LS_PANEL_W - 4, 7 * LS_SCALE, tagBg );
				LS_DrawString( (int)( x + 4 ), (int)( y + 1 ), LS_SMALL_SZ, "100% - ALL SECRETS FOUND", tagGreen );
				y += 8 * LS_SCALE;
			} else {
				float tagRed[] = { 0.90f, 0.45f, 0.25f, 0.90f };
				float tagBg[]  = { 0.30f, 0.10f, 0.05f, 0.30f };
				char pctBuf[64];
				LS_FillRect( x + 2, y, LS_PANEL_W - 4, 7 * LS_SCALE, tagBg );
				Com_sprintf( pctBuf, sizeof( pctBuf ), "INCOMPLETE: %d/%d sec  %d/%d tres",
					cumSecF, cumSecT, cumTresF, cumTresT );
				LS_DrawString( (int)( x + 4 ), (int)( y + 1 ), LS_SMALL_SZ, pctBuf, tagRed );
				y += 8 * LS_SCALE;
			}
		} else {
			/* During run: show only current segment's per-map values.
			   Subtract cumulative base (player->numSecretsFound is
			   cumulative across all maps) to get per-map found count.
			   Totals come from the hardcoded map table.
			   0/0 = green (no collectibles on this map). */
			int curIdx   = ls.currentMapIndex;
			int segSecF  = ls.liveSecretsFound  - ls.baseSecretsFound;
			int segSecT  = ( curIdx >= 0 && curIdx < ls.numMaps ) ? ls.splits[curIdx].secretsTotal : 0;
			int segTresF = ls.liveTreasureFound - ls.baseTreasureFound;
			int segTresT = ( curIdx >= 0 && curIdx < ls.numMaps ) ? ls.splits[curIdx].treasureTotal : 0;
			if ( segSecF  < 0 ) segSecF  = 0;
			if ( segTresF < 0 ) segTresF = 0;

			LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Secrets", pctLabelClr );
			Com_sprintf( secBuf, sizeof( secBuf ), "%d/%d", segSecF, segSecT );
			valW = (int)strlen( secBuf ) * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, secBuf,
				( segSecF >= segSecT ) ? pctCompleteClr :
				( segSecF > 0 ) ? pctPartialClr : pctValueClr );
			y += LS_STAT_H;

			LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_SMALL_SZ, "Treasure", pctLabelClr );
			Com_sprintf( tresBuf, sizeof( tresBuf ), "%d/%d", segTresF, segTresT );
			valW = (int)strlen( tresBuf ) * (int)LS_SMALL_SZ;
			valX = (int)( x + LS_PANEL_W - 4 ) - valW;
			LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, tresBuf,
				( segTresF >= segTresT ) ? pctCompleteClr :
				( segTresF > 0 ) ? pctPartialClr : pctValueClr );
			y += LS_STAT_H;
		}
	}

	/* ================ RGT (Real Game Time, bottom) ================ */
	if ( showRgt ) {
		int rgtMs = 0;

		if ( showSeps ) LS_FillRect( x + 2, y, LS_PANEL_W - 4, 1, sepColor );
		y += 2 * LS_SCALE;

		if ( ls.active ) {
			if ( ls.runFinished ) rgtMs = ls.runSavedRealMs;
			else                  rgtMs = Sys_Milliseconds() - ls.runStartRealMs;
		}
		LS_FormatTime( rgtMs, timeBuf, sizeof( timeBuf ) );

		if ( colAlign == 1 ) {
			LS_DrawStringR( rightEdge, (int)( y + 1 ), LS_MED_SZ, timeBuf, segTimerClr );
		} else {
			LS_DrawString( leftEdge, (int)( y + 1 ), LS_MED_SZ, timeBuf, segTimerClr );
		}
	}
}

/* =====================================================================
   Reset confirmation popup
   ===================================================================== */

static void LS_DrawResetConfirmPopup( void ) {
	static qboolean prevKeyY = qfalse;
	static qboolean prevKeyN = qfalse;
	static qboolean prevKeyEsc = qfalse;
	qboolean curY, curN, curEsc;

	if ( !ls_resetPending ) {
		prevKeyY = keys['y'].down;
		prevKeyN = keys['n'].down;
		prevKeyEsc = keys[K_ESCAPE].down;
		return;
	}

	/* --- detect key press edges --- */
	curY   = keys['y'].down;
	curN   = keys['n'].down;
	curEsc = keys[K_ESCAPE].down;

	if ( curY && !prevKeyY ) {
		prevKeyY = curY; prevKeyN = curN; prevKeyEsc = curEsc;
		Cbuf_AddText( "livesplit_reset_save\n" );
		return;
	}
	if ( curN && !prevKeyN ) {
		prevKeyY = curY; prevKeyN = curN; prevKeyEsc = curEsc;
		Cbuf_AddText( "livesplit_reset_discard\n" );
		return;
	}
	if ( curEsc && !prevKeyEsc ) {
		prevKeyY = curY; prevKeyN = curN; prevKeyEsc = curEsc;
		Cbuf_AddText( "livesplit_reset_cancel\n" );
		return;
	}

	prevKeyY = curY; prevKeyN = curN; prevKeyEsc = curEsc;

	/* --- draw popup overlay --- */
	{
		float popW = 220.0f, popH = 80.0f;
		float popX = ( 640.0f - popW ) * 0.5f;
		float popY = ( 480.0f - popH ) * 0.5f - 40.0f;
		float lineH = 12.0f;
		float charSz = 6.0f;

		vec4_t bgColor    = { 0.0f,  0.0f,  0.0f, 0.85f };
		vec4_t borderClr  = { 0.8f,  0.6f,  0.0f, 0.90f };
		vec4_t titleClr   = { 1.0f,  0.85f, 0.2f, 1.0f  };
		vec4_t textClr    = { 0.9f,  0.9f,  0.9f, 1.0f  };
		vec4_t yesClr     = { 0.3f,  1.0f,  0.3f, 1.0f  };
		vec4_t noClr      = { 1.0f,  0.35f, 0.3f, 1.0f  };
		vec4_t cancelClr  = { 0.6f,  0.6f,  0.6f, 1.0f  };

		/* Background */
		SCR_FillRect( popX - 1, popY - 1, popW + 2, popH + 2, borderClr );
		SCR_FillRect( popX, popY, popW, popH, bgColor );

		/* Title */
		SCR_DrawStringExt( (int)( popX + popW * 0.5f - 11 * charSz * 0.5f ),
			(int)( popY + 6 ), (int)charSz, "UNSAVED RUN!", titleClr, qtrue );

		/* Subtitle */
		SCR_DrawStringExt( (int)( popX + popW * 0.5f - 15 * charSz * 0.5f ),
			(int)( popY + 6 + lineH + 2 ), (int)charSz, "Save before reset?", textClr, qtrue );

		/* Options */
		SCR_DrawStringExt( (int)( popX + 20 ),
			(int)( popY + 6 + (lineH + 2) * 2 + 4 ), (int)charSz, "[Y] Save", yesClr, qtrue );

		SCR_DrawStringExt( (int)( popX + popW * 0.5f - 5 ),
			(int)( popY + 6 + (lineH + 2) * 2 + 4 ), (int)charSz, "[N] Discard", noClr, qtrue );

		SCR_DrawStringExt( (int)( popX + popW * 0.5f - 8 * charSz * 0.5f ),
			(int)( popY + 6 + (lineH + 2) * 3 + 6 ), (int)charSz, "[ESC] Cancel", cancelClr, qtrue );
	}
}

/* =====================================================================
   Standalone IGT timer overlay
   ===================================================================== */

static void LS_DrawIGTTimer( void ) {
	float timerX, timerY, timerScale, charSz;
	int   igtMs;
	char  timeBuf[32];
	vec4_t timerColor = { 1.0f, 1.0f, 1.0f, 0.95f };
	vec4_t shadowColor = { 0.0f, 0.0f, 0.0f, 0.65f };

	if ( !ls_igttimerCvar || !ls_igttimerCvar->integer ) {
		/* In external-only mode, force the IGT timer on even if ls_igttimer is 0 */
		if ( !LS_ExtEnabled() ) return;
	}
	if ( !ls.active && !ls.runFinished ) return;

	timerX     = ls_igttimer_xCvar ? ls_igttimer_xCvar->value : 280.0f;
	timerY     = ls_igttimer_yCvar ? ls_igttimer_yCvar->value : 440.0f;
	timerScale = ls_igttimer_scaleCvar ? ls_igttimer_scaleCvar->value : 1.0f;

	if ( timerScale < 0.3f ) timerScale = 0.3f;
	if ( timerScale > 4.0f ) timerScale = 4.0f;

	charSz = 8.0f * timerScale;

	/* Get current IGT - use cumulative time across all non-cutscene splits
	   (same method as the main timer display). This correctly includes
	   time accumulated on cutscene maps into adjacent real splits. */
	if ( ls.runFinished ) {
		igtMs = ls.runTotalIGTMs;
	} else {
		igtMs = LS_CumulativeTime( ls.modeLastIdx );
	}

	LS_FormatTime( igtMs, timeBuf, sizeof( timeBuf ) );

	/* Shadow */
	SCR_DrawStringExt( (int)( timerX + 1 ), (int)( timerY + 1 ), (int)charSz, timeBuf, shadowColor, qtrue );
	/* Timer text */
	SCR_DrawStringExt( (int)timerX, (int)timerY, (int)charSz, timeBuf, timerColor, qtrue );

	/* ---- Segment time (smaller, below main timer) ---- */
	if ( ls_igtsegtimer && ls_igtsegtimer->integer ) {
		int segMs = 0;
		float segSz = charSz * 0.6f;
		float segY  = timerY + charSz + 2.0f;
		char  segBuf[48];
		vec4_t segColor  = { 0.75f, 0.85f, 1.0f, 0.80f };
		vec4_t segShadow = { 0.0f,  0.0f,  0.0f, 0.50f };

		if ( segSz < 3.0f ) segSz = 3.0f;

		if ( ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps &&
			 !ls.splits[ls.currentMapIndex].cutscene ) {
			segMs = ls.splits[ls.currentMapIndex].currentTimeMs;
		} else {
			/* On a cutscene map, show the adjacent real split's time */
			int realIdx = LS_ActiveRealSplit();
			if ( realIdx >= 0 ) segMs = ls.splits[realIdx].currentTimeMs;
		}

		{
			char tmpBuf[32];
			LS_FormatTime( segMs, tmpBuf, sizeof( tmpBuf ) );
			Com_sprintf( segBuf, sizeof( segBuf ), "seg %s", tmpBuf );
		}

		SCR_DrawStringExt( (int)( timerX + 1 ), (int)( segY + 1 ), (int)segSz, segBuf, segShadow, qtrue );
		SCR_DrawStringExt( (int)timerX, (int)segY, (int)segSz, segBuf, segColor, qtrue );
	}
}

/* =====================================================================
   Main entry point
   ===================================================================== */

void SCR_LiveSplitDraw( void ) {
	qboolean extEnabled = ( ls_typeCvar && ( ls_typeCvar->integer == LS_TYPE_EXTERNAL ||
											 ls_typeCvar->integer == LS_TYPE_BOTH ) );

	if ( !cg_livesplit || !cg_livesplit->integer ) {
		/* Even with cg_livesplit off, run timer logic if external LS is enabled */
		if ( !extEnabled ) return;
		if ( !ls.initialized ) return;
		if ( !cls.rendererStarted ) return;
		if ( clc.demoplaying ) return;

		LS_Frame();
		LS_DrawIGTTimer();
		LS_DrawResetConfirmPopup();
		return;
	}
	if ( !ls.initialized ) return;
	if ( !cls.rendererStarted ) return;
	if ( clc.demoplaying ) return;  /* LiveSplit disabled during demo playback */

	LS_Frame();

	/* Always draw standalone IGT timer if enabled (independent of panel) */
	LS_DrawIGTTimer();

	/* Always process reset confirmation popup */
	LS_DrawResetConfirmPopup();

	/* External-only mode: skip panel, only IGT + reset popup */
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		return;
	}

	if ( ls_drawCvar && !ls_drawCvar->integer ) {
		/* Panel hidden but timers still running - show small indicator */
		if ( ls.active && !ls.runFinished ) {
			int ix, iy;
			vec4_t indColor  = { 0.35f, 0.75f, 0.20f, 0.70f };
			vec4_t indShadow = { 0.0f,  0.0f,  0.0f,  0.35f };

			ix = 6;
			/* Place below REC (y=4,h~12) and sv_cheats (y=16,h~12) */
			iy = 4;
			if ( clc.demorecording ) iy += 12;
			if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) iy += 12;

			SCR_DrawStringExt( ix + 1, iy + 1, 3, "LS RUNNING", indShadow, qtrue );
			SCR_DrawStringExt( ix, iy, 3, "LS RUNNING", indColor, qtrue );
		}
		return;
	}

	LS_Draw();
}
