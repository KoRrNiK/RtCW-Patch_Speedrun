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
  ls_compare    0           Compare Against: 0=PB, 1=Best Segments, 2=Average
  ls_timing     0           Timing Method: 0=Game Time, 1=Real Time
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
#include "cl_speedrun_imgui.h"
#include "cl_livesplit_window.h"
#include "livesplit/ls_shared.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

/* =====================================================================
   Shared memory IPC  (game -> standalone LiveSplit window)
   ===================================================================== */
#ifdef _WIN32
static HANDLE       ls_shmHandle = NULL;
static lsSharedMem_t *ls_shmPtr = NULL;
static long         ls_shmSeq    = 0;

static void LS_ShmInit( void ) {
	ls_shmHandle = CreateFileMappingA(
		INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
		0, sizeof( lsSharedMem_t ), LS_SHM_NAME );
	if ( !ls_shmHandle ) return;
	ls_shmPtr = (lsSharedMem_t *)MapViewOfFile(
		ls_shmHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof( lsSharedMem_t ) );
	if ( !ls_shmPtr ) {
		CloseHandle( ls_shmHandle );
		ls_shmHandle = NULL;
		return;
	}
	memset( ls_shmPtr, 0, sizeof( *ls_shmPtr ) );
	ls_shmPtr->version    = LS_SHM_VERSION;
	ls_shmPtr->gameActive = 1;
	ls_shmSeq = 0;
	Com_Printf( "^2LiveSplit: Shared memory created for standalone window\n" );
}

static void LS_ShmUpdate( void ) {
	if ( !ls_shmPtr ) return;
	ls_shmPtr->state = lswnd_state;
	InterlockedIncrement( &ls_shmSeq );
	ls_shmPtr->sequence = ls_shmSeq;
}

static void LS_ShmShutdown( void ) {
	if ( ls_shmPtr ) {
		ls_shmPtr->gameActive = 0;
		UnmapViewOfFile( ls_shmPtr );
		ls_shmPtr = NULL;
	}
	if ( ls_shmHandle ) {
		CloseHandle( ls_shmHandle );
		ls_shmHandle = NULL;
	}
}
#else
static void LS_ShmInit( void ) {}
static void LS_ShmUpdate( void ) {}
static void LS_ShmShutdown( void ) {}
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
#define LS_NUM_CATEGORY_VARIANTS 4  /* any, 100%, HL1, HL1 100% */
#define LS_DIFF_SLOTS_PER_VARIANT (LS_NUM_MODES * LS_MAX_DIFFICULTIES)
#define LS_TOTAL_DIFF_SLOTS (LS_NUM_CATEGORY_VARIANTS * LS_DIFF_SLOTS_PER_VARIANT)  /* 36 */
#define LS_SAVE_FILE        "livesplit_stats.dat"    /* legacy - no longer used for saving */
#define LS_CS_MISSIONSTATS  23   /* configstring index for mission stats */
#define LS_HISTORY_FILE     "livesplit_history.dat"  /* legacy - no longer used for saving */
#define LS_MAX_VIS_ROWS     6

/* .lss file paths (LiveSplit XML format) */
#define LS_LSS_DIR          "livesplit"
#define LS_LSS_DIR_FG       "livesplit/fullgame"
#define LS_LSS_DIR_MS       "livesplit/mission"
#define LS_LSS_DIR_IL       "livesplit/il"
#define LS_STATE_FILE       "livesplit/state.dat"

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
	{ "boss2",       "Super Soldier",          			"S.Soldier",      5, qfalse,  0,  0 },
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
	const char *shortName;     /* abbreviated name for narrow header */
	int         firstMission;  /* first original mission number in group */
	int         lastMission;   /* last original mission number in group */
} lsMissionGroup_t;

static const lsMissionGroup_t ls_missionGroups[LS_NUM_MISSION_GROUPS] = {
	{ "Ominous Rumors & Dark Secret",   "Ominous & Dark",  1, 2 },  /* Group 1: Mission 1+2 */
	{ "Weapons of Vengeance",           "Vengeance",       3, 3 },  /* Group 2: Mission 3   */
	{ "Deadly Designs",                 "Deadly Designs",  4, 4 },  /* Group 3: Mission 4   */
	{ "Deathshead's Playground",        "Deathshead",      5, 5 },  /* Group 4: Mission 5   */
	{ "Return Eng. & Op. Resurrection", "Return & Res.",   6, 7 },  /* Group 5: Mission 6+7 */
};

/* Difficulty name arrays for .lss file paths and display */
static const char *ls_diffFileNames[] = { "easy", "medium", "hard" };
static const char *ls_diffDisplayNames[] = { "Don't hurt me", "Bring 'em on!", "I am Death incarnate!" };
static const char *ls_diffShortTags[] = { "DHM", "BEO", "IADI" };

/* =====================================================================
   Data structures
   ===================================================================== */

/* Per-difficulty persistent data per split */
typedef struct {
	int bestTimeMs;
	int bestRealTimeMs;
	int pbSegmentMs;
	int pbRealSegmentMs;
	int totalAttempts;
	int totalCompletions;
} lsDiffSplitData_t;

typedef struct {
	char                mapname[LS_MAX_MAPNAME];
	int                 mission;
	qboolean            cutscene;
	const char         *displayName;  /* human-readable full name */
	const char         *shortName;    /* abbreviated name for narrow UI */

	/* per-variant-per-mode-per-difficulty persistent data */
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
#define LS_HISTORY_INITIAL_CAP 256

typedef struct {
	int attemptId;                   /* original Attempt id from .lss */
	int difficulty;                  /* 1-3 */
	int mode;                        /* 0/1/2 */
	int categoryVariant;             /* 0 any, 1 100%, 2 HL1, 3 HL1 100% */
	int missionNum;                  /* for mission mode */
	int mapIdx;                      /* for IL mode */
	int totalIGTMs;
	int totalRGTMs;
	int numSplits;
	int splitTimes[LS_MAX_MAPS];
	int splitRealTimes[LS_MAX_MAPS];
	time_t startTime;                /* Unix timestamp of run start */
	time_t endTime;                  /* Unix timestamp of run end */
	qboolean isStartedSynced;
	qboolean isEndedSynced;
} lsRunHistory_t;

/* Reset attempt entry (incomplete run, no GameTime/RealTime) */
typedef struct {
	int attemptId;                   /* original Attempt id from .lss */
	int difficulty;                  /* 1-3 (diffIdx+1) */
	int mode;                        /* 0/1/2 */
	int categoryVariant;
	int missionNum;
	int mapIdx;                      /* for IL mode */
	time_t startTime;
	time_t endTime;
	qboolean isStartedSynced;
	qboolean isEndedSynced;
	int pauseTimeMs;                 /* >0 if attempt had <PauseTime> only (no RT/GT) */
} lsResetAttempt_t;

/* Orphan segment time: SegmentHistory entry from a partial run (reset)
   that doesn't match any completed history entry. */
typedef struct {
	int attemptId;       /* the Time id from SegmentHistory (= Attempt id) */
	int segIdx;          /* sequential segment index within the category */
	int timeMs;          /* GameTime in ms */
	int realTimeMs;      /* RealTime in ms */
	int mode;
	int diffIdx;
	int categoryVariant;
	int missionNum;
	int mapIdx;          /* IL: source map index; -1 for FG/Mission */
} lsOrphanSegTime_t;

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
	int         fgAttempts[LS_TOTAL_DIFF_SLOTS];
	int         fgCompletions[LS_TOTAL_DIFF_SLOTS];
	int         fgPB[LS_TOTAL_DIFF_SLOTS];
	/* mission: [group 0-4][difficulty 0-2] */
	int         msAttempts[LS_NUM_MISSION_GROUPS][LS_TOTAL_DIFF_SLOTS];
	int         msCompletions[LS_NUM_MISSION_GROUPS][LS_TOTAL_DIFF_SLOTS];
	int         msPB[LS_NUM_MISSION_GROUPS][LS_TOTAL_DIFF_SLOTS];
	/* IL: uses per-split d[di].totalAttempts/totalCompletions/bestTimeMs */

	/* run history (dynamically allocated, no fixed limit) */
	lsRunHistory_t *history;
	int            numHistoryRuns;
	int            historyCapacity;

	/* reset attempts (incomplete runs, for .lss round-trip) */
	lsResetAttempt_t *resetAttempts;
	int              numResetAttempts;
	int              resetAttemptsCapacity;

	/* orphan segment times (from partial runs, for .lss round-trip) */
	lsOrphanSegTime_t *orphanSegTimes;
	int               numOrphanSegTimes;
	int               orphanSegTimesCapacity;

	/* GameIcon (base64 string from .lss, for round-trip preservation) */
	char             *gameIcon;      /* Z_Malloc'd, NULL if none */

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
	qboolean    manualStartIgnoreUi; /* allow IL IGT to start even if start bind/menu leaves UI catcher set */

	/* Anti-cheat counters (lifetime) */
	int         totalPauses;
	int         totalUndos;
	int         totalSkips;

	/* sv_cheats detection (per-run) */
	qboolean    cheatsUsed;

	/* Settings validation (per-run) */
	qboolean    settingsModified;
	int         settingsModCount;
	int         settingsCheckInterval;  /* frame counter for periodic check */
	qboolean    settingsDetected[80];   /* per-cvar: already reported this run */

	/* Center-screen alert system */
	int         alertStartMs;       /* Sys_Milliseconds when alert was triggered */
	char        alertText[128];     /* text to display */
	float       alertColor[4];      /* RGBA color */

	/* Run-finish verification overlay */
	int         verifyStartMs;      /* Sys_Milliseconds when triggered (0 = inactive) */
	qboolean    verifyValid;        /* all checks passed? */
	int         verifyModCount;     /* number of modified settings */
	qboolean    verifyCheats;       /* sv_cheats was used during run */
	int         verifyPauses;
	int         verifyUndos;
	int         verifySkips;

	/* Pre-run PB snapshot (for nosave restore) */
	int         savedPBMs;
	int         savedRgtPBMs;
	int         savedPbSegMs[LS_MAX_MAPS]; /* per-split pbSegmentMs snapshot */

	/* Per-category PB realtime */
	int         fgPBRgt[LS_TOTAL_DIFF_SLOTS];
	int         msPBRgt[LS_NUM_MISSION_GROUPS][LS_TOTAL_DIFF_SLOTS];

	/* Map-load freeze: set on real map transition (changelevel), cleared
	   when the player clicks 'continue' on the pregame screen (ls_loading
	   cvar goes to 0).  Prevents IGT ticking during new map loading.
	   mapLoadFreezeStartMs is used as a failsafe timeout: if the briefing
	   screen never appears (cutscene maps), the freeze auto-clears. */
	qboolean    mapLoadFreeze;
	int         mapLoadFreezeStartMs;
	qboolean    backtrackPause;       /* legacy/off-route flag; kept false for current IGT rules */
	int         backtrackTargetIndex;  /* legacy resume target; kept for saved-state compatibility */

	/* Deferred Full Game start: the run is NOT activated during map-change
	   detection for cutscene1.  Instead we wait until cls.state >= CA_ACTIVE
	   (the cutscene is actually playing) to start the timer, so loading
	   screen time is never counted. */
	qboolean    fgPendingStart;

	/* 100% category: live stats from CS_MISSIONSTATS for current map */
	int         liveObjectivesFound;
	int         liveObjectivesTotal;
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

	/* 100% category: base for per-map found delta calculation.
	   player->numSecretsFound resets to 0 on each map load
	   (only health is persisted via gentityPersFields), so the
	   base is always 0 at the start of each map.  Kept for
	   structural compatibility but never accumulated. */
	int         baseSecretsFound;
	int         baseTreasureFound;
} lsState_t;

static lsState_t ls;

/* Ensure the dynamic history array can hold at least 'needed' entries.
   Grows by doubling.  Uses Z_Malloc + memcpy because the engine has
   no realloc. */
static void LS_HistoryEnsure( int needed ) {
	if ( needed <= ls.historyCapacity ) return;
	{
		int newCap = ls.historyCapacity ? ls.historyCapacity : LS_HISTORY_INITIAL_CAP;
		lsRunHistory_t *newBuf;
		while ( newCap < needed ) newCap *= 2;
		newBuf = (lsRunHistory_t *)Z_Malloc( newCap * sizeof( lsRunHistory_t ) );
		if ( ls.history ) {
			memcpy( newBuf, ls.history, ls.numHistoryRuns * sizeof( lsRunHistory_t ) );
			Z_Free( ls.history );
		}
		ls.history = newBuf;
		ls.historyCapacity = newCap;
	}
}

static void LS_HistoryFree( void ) {
	if ( ls.history ) {
		Z_Free( ls.history );
		ls.history = NULL;
	}
	ls.numHistoryRuns  = 0;
	ls.historyCapacity = 0;
	if ( ls.resetAttempts ) {
		Z_Free( ls.resetAttempts );
		ls.resetAttempts = NULL;
	}
	ls.numResetAttempts  = 0;
	ls.resetAttemptsCapacity = 0;
	if ( ls.orphanSegTimes ) {
		Z_Free( ls.orphanSegTimes );
		ls.orphanSegTimes = NULL;
	}
	ls.numOrphanSegTimes  = 0;
	ls.orphanSegTimesCapacity = 0;
	if ( ls.gameIcon ) {
		Z_Free( ls.gameIcon );
		ls.gameIcon = NULL;
	}
}

/* Ensure the reset attempts array can hold at least 'needed' entries. */
static void LS_ResetAttemptsEnsure( int needed ) {
	if ( needed <= ls.resetAttemptsCapacity ) return;
	{
		int newCap = ls.resetAttemptsCapacity ? ls.resetAttemptsCapacity : 256;
		lsResetAttempt_t *newBuf;
		while ( newCap < needed ) newCap *= 2;
		newBuf = (lsResetAttempt_t *)Z_Malloc( newCap * sizeof( lsResetAttempt_t ) );
		if ( ls.resetAttempts ) {
			memcpy( newBuf, ls.resetAttempts, ls.numResetAttempts * sizeof( lsResetAttempt_t ) );
			Z_Free( ls.resetAttempts );
		}
		ls.resetAttempts = newBuf;
		ls.resetAttemptsCapacity = newCap;
	}
}

/* Ensure the orphan segment times array can hold at least 'needed' entries. */
static void LS_OrphanSegTimesEnsure( int needed ) {
	if ( needed <= ls.orphanSegTimesCapacity ) return;
	{
		int newCap = ls.orphanSegTimesCapacity ? ls.orphanSegTimesCapacity : 256;
		lsOrphanSegTime_t *newBuf;
		while ( newCap < needed ) newCap *= 2;
		newBuf = (lsOrphanSegTime_t *)Z_Malloc( newCap * sizeof( lsOrphanSegTime_t ) );
		if ( ls.orphanSegTimes ) {
			memcpy( newBuf, ls.orphanSegTimes, ls.numOrphanSegTimes * sizeof( lsOrphanSegTime_t ) );
			Z_Free( ls.orphanSegTimes );
		}
		ls.orphanSegTimes = newBuf;
		ls.orphanSegTimesCapacity = newCap;
	}
}

static int LS_DiffIdx( int skill ); /* forward declaration */
static void LS_DemoWriteUpdate( void ); /* forward declaration */

/* Find the highest attemptId across all history entries and reset attempts
   for a given mode/difficulty/mission. Returns 0 if none exist. */
static int LS_MaxAttemptId( int mode, int diffIdx, int missionNum, int mapIdx, int categoryVariant ) {
	int maxId = 0, i, baseDiffIdx;
	baseDiffIdx = diffIdx;
	if ( baseDiffIdx >= LS_MAX_DIFFICULTIES ) {
		baseDiffIdx %= LS_MAX_DIFFICULTIES;
	}
	for ( i = 0; i < ls.numHistoryRuns; i++ ) {
		lsRunHistory_t *r = &ls.history[i];
		if ( r->mode != mode ) continue;
		if ( r->categoryVariant != categoryVariant ) continue;
		if ( LS_DiffIdx( r->difficulty ) != baseDiffIdx ) continue;
		if ( mode == LS_MODE_MISSION && r->missionNum != missionNum ) continue;
		if ( mode == LS_MODE_IL && mapIdx >= 0 && r->mapIdx != mapIdx ) continue;
		if ( r->attemptId > maxId ) maxId = r->attemptId;
	}
	for ( i = 0; i < ls.numResetAttempts; i++ ) {
		lsResetAttempt_t *ra = &ls.resetAttempts[i];
		if ( ra->mode != mode ) continue;
		if ( ra->categoryVariant != categoryVariant ) continue;
		if ( LS_DiffIdx( ra->difficulty ) != baseDiffIdx ) continue;
		if ( mode == LS_MODE_MISSION && ra->missionNum != missionNum ) continue;
		if ( mode == LS_MODE_IL && mapIdx >= 0 && ra->mapIdx != mapIdx ) continue;
		if ( ra->attemptId > maxId ) maxId = ra->attemptId;
	}
	return maxId;
}

static qboolean LS_HasAttemptId( int mode, int diffIdx, int missionNum, int mapIdx, int categoryVariant, int attemptId ) {
	int i;
	for ( i = 0; i < ls.numHistoryRuns; i++ ) {
		lsRunHistory_t *r = &ls.history[i];
		if ( r->attemptId != attemptId ) continue;
		if ( r->mode != mode ) continue;
		if ( r->categoryVariant != categoryVariant ) continue;
		if ( LS_DiffIdx( r->difficulty ) != diffIdx ) continue;
		if ( mode == LS_MODE_MISSION && r->missionNum != missionNum ) continue;
		if ( mode == LS_MODE_IL && r->mapIdx != mapIdx ) continue;
		return qtrue;
	}
	for ( i = 0; i < ls.numResetAttempts; i++ ) {
		lsResetAttempt_t *ra = &ls.resetAttempts[i];
		if ( ra->attemptId != attemptId ) continue;
		if ( ra->mode != mode ) continue;
		if ( ra->categoryVariant != categoryVariant ) continue;
		if ( LS_DiffIdx( ra->difficulty ) != diffIdx ) continue;
		if ( mode == LS_MODE_MISSION && ra->missionNum != missionNum ) continue;
		if ( mode == LS_MODE_IL && ra->mapIdx != mapIdx ) continue;
		return qtrue;
	}
	return qfalse;
}

static qboolean LS_HasOrphanSegTime( int mode, int diffIdx, int missionNum, int mapIdx, int categoryVariant, int attemptId, int segIdx ) {
	int i;
	for ( i = 0; i < ls.numOrphanSegTimes; i++ ) {
		lsOrphanSegTime_t *o = &ls.orphanSegTimes[i];
		if ( o->attemptId != attemptId || o->segIdx != segIdx ) continue;
		if ( o->mode != mode || o->diffIdx != diffIdx ) continue;
		if ( o->categoryVariant != categoryVariant ) continue;
		if ( mode == LS_MODE_MISSION && o->missionNum != missionNum ) continue;
		if ( mode == LS_MODE_IL && o->mapIdx != mapIdx ) continue;
		return qtrue;
	}
	return qfalse;
}

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
static cvar_t *sp_timerDecimalsCvar = NULL; /* show fractional timer values */

/* Compare Against mode (ls_compare):
   0 = Personal Best      - compare cumulative time vs PB splits
   1 = Best Segments       - compare cumulative time vs sum of best segments (golds)
   2 = Average Segments    - compare against average segment times from history
*/
#define LS_COMPARE_PB            0
#define LS_COMPARE_BEST          1
#define LS_COMPARE_AVERAGE       2
static cvar_t *ls_compareCvar = NULL;

/* Timing Method (ls_timing):
   0 = Game Time (IGT)     - default, uses in-game time
   1 = Real Time (RGT)     - uses real/wall-clock time
*/
#define LS_TIMING_GAMETIME  0
#define LS_TIMING_REALTIME  1
static cvar_t *ls_timingCvar = NULL;
static cvar_t *ls_showattCvar    = NULL; /* show attempt counter in header */
static cvar_t *ls_drawCvar       = NULL; /* show/hide LiveSplit panel (timers still run) */
static cvar_t *ls_100pctCvar     = NULL; /* 100% category: display secrets & treasures */
static int     ls_prev100pct    = 0;    /* track 100% cvar changes for file reload */
static int     ls_prevDifficulty = 2;   /* track difficulty cvar changes for file reload */
static int     ls_prevHL1Mode    = 0;   /* track bh_movement category changes */

static qboolean LS_HL1ModeActive( void ) {
	return Cvar_VariableIntegerValue( "bh_movement" ) ? qtrue : qfalse;
}

static int LS_CategoryVariant( qboolean pct100, qboolean hl1 ) {
	return ( pct100 ? 1 : 0 ) | ( hl1 ? 2 : 0 );
}

static int LS_CurCategoryVariant( void ) {
	qboolean p100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? qtrue : qfalse;
	return LS_CategoryVariant( p100, LS_HL1ModeActive() );
}

static int LS_DiffSlotFor( int mode, int diffIdx, qboolean pct100, qboolean hl1 ) {
	int variant = LS_CategoryVariant( pct100, hl1 );
	if ( mode < 0 ) mode = 0;
	if ( mode >= LS_NUM_MODES ) mode = LS_NUM_MODES - 1;
	if ( diffIdx < 0 ) diffIdx = 0;
	if ( diffIdx >= LS_MAX_DIFFICULTIES ) diffIdx = LS_MAX_DIFFICULTIES - 1;
	return variant * LS_DIFF_SLOTS_PER_VARIANT + mode * LS_MAX_DIFFICULTIES + diffIdx;
}

static const char *LS_HL1ModeNameSuffixFor( qboolean hl1 ) {
	return hl1 ? " HL1" : "";
}

static const char *LS_HL1ModeNameSuffix( void ) {
	return LS_HL1ModeNameSuffixFor( LS_HL1ModeActive() );
}

static const char *LS_HL1ModeFileSuffixFor( qboolean hl1 ) {
	return hl1 ? "_hl1" : "";
}

static const char *LS_HL1ModeFileSuffix( void ) {
	return LS_HL1ModeFileSuffixFor( LS_HL1ModeActive() );
}

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

static cvar_t *ls_igttimer_alignCvar = NULL;
/* Standalone segment timer overlay (below IGT timer) */
static cvar_t *ls_igtsegtimer = NULL; /* 0=off, 1=on */

/* Reset confirmation state */
static int ls_resetPending = 0; /* 0=none, 1=waiting for user confirm */
static int ls_resetPendingStartMs = 0;
static int ls_resetPromptKind = 0; /* 1=PB, 2=golds, 3=generic */
static int ls_resetPromptGoldCount = 0;
static qboolean ls_raceForceStart = qfalse;

#define LS_RESET_PROMPT_PB      1
#define LS_RESET_PROMPT_GOLDS   2
#define LS_RESET_PROMPT_GENERIC 3

/* ls_type cvar: 0=in-game only, 1=external LiveSplit only */
static cvar_t *ls_typeCvar = NULL;
#define LS_TYPE_INGAME   0
#define LS_TYPE_EXTERNAL 1

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
static void LS_DoResetSaveEx( qboolean fromExternal );
static void LS_DoResetSave( void );
static qboolean LS_RaceBlockTimerReset( const char *action );
static qboolean LS_RaceBlocksTimerStart( void );
static void LS_RaceSetStatus( const char *status );
static void LS_TriggerAlert( const char *text, float r, float g, float b );
static void LS_TriggerVerify( void );
static void LS_QuickCheckSettings( void );
static qboolean LS_Draw2DReady( void );

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

/* Track whether starttimer was sent on the current connection.
   If the connection drops and reconnects while ls.active is true,
   LS_ExtFrame will re-send initgametime/starttimer/pausegametime
   so the external timer stays in sync. */
static qboolean lsext_timerStarted = qfalse;

static qboolean LS_ExtEnabled( void ) {
	if ( !ls_typeCvar ) return qfalse;
	return ( ls_typeCvar->integer == LS_TYPE_EXTERNAL );
}

static qboolean LS_InGameEnabled( void ) {
	if ( !ls_typeCvar ) return qtrue;
	return ( ls_typeCvar->integer == LS_TYPE_INGAME );
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
	lsext_timerStarted = qfalse;
}

static void LS_ExtSend( const char *cmd ) {
	char buf[256];
	int len, sent;

	if ( !lsext_connected || lsext_socket == INVALID_SOCKET ) return;

	Com_sprintf( buf, sizeof( buf ), "%s\r\n", cmd );
	len = strlen( buf );
	sent = send( lsext_socket, buf, len, 0 );
	if ( sent == SOCKET_ERROR ) {
		int err = WSAGetLastError();
		if ( err == WSAEWOULDBLOCK ) {
			return; /* buffer full, skip this send - next frame will retry */
		}
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
				LS_DoResetSaveEx( qtrue );
			}
		}

		/* Remove processed line from buffer */
		if ( lsext_recvLen <= lineLen ) {
			lsext_recvLen = 0;
			lsext_recvBuf[0] = '\0';
		} else {
			memmove( lsext_recvBuf, lsext_recvBuf + lineLen, lsext_recvLen - lineLen + 1 );
			lsext_recvLen -= lineLen;
		}
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

	/* Reconnect mid-run: if the connection dropped and came back,
	   re-issue initgametime/starttimer/pausegametime so the external
	   timer stays in sync.  The actual per-frame setgametime is sent
	   later in LS_Frame, AFTER IGT accumulation, so the external timer
	   always has the same value as the in-game display. */
	if ( lsext_connected && ls.active && !ls.runFinished ) {
		if ( !lsext_timerStarted ) {
			lsext_recvLen = 0;
			LS_ExtSend( "initgametime" );
			LS_ExtSend( "starttimer" );
			LS_ExtSend( "pausegametime" );
			lsext_timerStarted = qtrue;
			lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;
		}
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

/* ls_window cvar removed - standalone exe handles the window now */

/* =====================================================================
   Difficulty accessor helpers  (skill index 0-2)
   ===================================================================== */

/* Clamp difficulty value 1-3 to array index 0-2 */
static int LS_DiffIdx( int skill ) {
	if ( skill < 1 ) skill = 1;
	if ( skill > 3 ) skill = 3;
	return skill - 1;
}

static int LS_CurDiffIdx( void ) {
	int base = LS_DiffIdx( ls.currentDifficulty );
	int mode = ls.runMode;
	qboolean p100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? qtrue : qfalse;
	return LS_DiffSlotFor( mode, base, p100, LS_HL1ModeActive() );
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
	int i, di = LS_CurDiffIdx();
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

	/* Snapshot per-split PB segments for current difficulty */
	for ( i = 0; i < ls.numMaps && i < LS_MAX_MAPS; i++ ) {
		ls.savedPbSegMs[i] = ls.splits[i].d[di].pbSegmentMs;
	}
}

/* Restore pre-run PB values (called from nosave reset) */
static void LS_RestorePreRunPBs( void ) {
	int i, di = LS_CurDiffIdx();
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

	/* Restore per-split PB segments for current difficulty */
	for ( i = 0; i < ls.numMaps && i < LS_MAX_MAPS; i++ ) {
		ls.splits[i].d[di].pbSegmentMs = ls.savedPbSegMs[i];
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

static const char *LS_SplitCustomName( int idx ) {
	static char customName[64];
	char cvarName[64];
	if ( idx < 0 || idx >= ls.numMaps ) return NULL;
	Com_sprintf( cvarName, sizeof( cvarName ), "ls_name_%s", ls.splits[idx].mapname );
	Cvar_VariableStringBuffer( cvarName, customName, sizeof( customName ) );
	if ( customName[0] ) {
		return customName;
	}
	return NULL;
}

static const char *LS_SplitMenuName( int idx ) {
	const char *custom;
	if ( idx < 0 || idx >= ls.numMaps ) return "---";
	custom = LS_SplitCustomName( idx );
	if ( custom && custom[0] ) return custom;
	if ( ls.splits[idx].shortName && ls.splits[idx].shortName[0] ) return ls.splits[idx].shortName;
	if ( ls.splits[idx].displayName && ls.splits[idx].displayName[0] ) return ls.splits[idx].displayName;
	return ls.splits[idx].mapname;
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
	full = LS_SplitCustomName( idx );
	if ( !full || !full[0] ) full = ls.splits[idx].displayName;
	shrt = ( full && full[0] && full != ls.splits[idx].displayName ) ? full : ls.splits[idx].shortName;
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

/* Detect current difficulty from cvars.
   g_gameskill is the authoritative difficulty cvar (1-3), set by the
   speedrun menu (play.menu / chapter.menu) and the game module.
   It's CVAR_LATCH, so when the menu changes it the new value sits in
   latchedString until the next server restart.  Check latchedString
   first so the timer uses the INTENDED difficulty before the map
   finishes loading. */
static int LS_DetectDifficulty( void ) {
	int skill;
	cvar_t *cv;
	/* g_gameskill: check latched value first (menu just set it but
	   map hasn't loaded yet), then current value. */
	cv = Cvar_Get( "g_gameskill", "2", 0 );
	if ( cv ) {
		if ( cv->latchedString && cv->latchedString[0] ) {
			skill = atoi( cv->latchedString );
			if ( skill >= 1 && skill <= 3 ) return skill;
		}
		skill = cv->integer;
		if ( skill >= 1 && skill <= 3 ) return skill;
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
	if ( ls.currentMapIndex < 0 || ls.currentMapIndex >= ls.numMaps ) return;

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
	if ( ls.currentMapIndex < 0 || ls.currentMapIndex >= ls.numMaps ) {
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
	int hours, mins, secs, hundredths;
	qboolean neg = qfalse;
	if ( ms < 0 ) { neg = qtrue; ms = -ms; }
	hours = ms / 3600000;
	mins = ( ms / 60000 ) % 60;
	secs = ( ms / 1000 ) % 60;
	if ( sp_timerDecimalsCvar && !sp_timerDecimalsCvar->integer ) {
		if ( hours > 0 ) {
			Com_sprintf( out, outSize, "%s%d:%02d:%02d", neg ? "-" : "", hours, mins, secs );
		} else {
			Com_sprintf( out, outSize, "%s%d:%02d", neg ? "-" : "", mins, secs );
		}
		return;
	}
	hundredths = ( ms % 1000 ) / 10;
	if ( hours > 0 ) {
		Com_sprintf( out, outSize, "%s%d:%02d:%02d.%02d", neg ? "-" : "", hours, mins, secs, hundredths );
	} else {
		Com_sprintf( out, outSize, "%s%d:%02d.%02d", neg ? "-" : "", mins, secs, hundredths );
	}
}

static void LS_FormatDelta( int deltaMs, char *out, int outSize ) {
	int ad, hours, mins, secs, hundredths;
	char sign;
	if ( deltaMs == 0 ) { Com_sprintf( out, outSize, "-" ); return; }
	ad = deltaMs < 0 ? -deltaMs : deltaMs;
	sign = deltaMs < 0 ? '-' : '+';
	hours = ad / 3600000;
	mins = ( ad / 60000 ) % 60;
	secs = ( ad / 1000 ) % 60;
	if ( sp_timerDecimalsCvar && !sp_timerDecimalsCvar->integer ) {
		if ( hours > 0 ) {
			Com_sprintf( out, outSize, "%c%d:%02d:%02d", sign, hours, mins, secs );
		} else if ( mins > 0 ) {
			Com_sprintf( out, outSize, "%c%d:%02d", sign, mins, secs );
		} else {
			Com_sprintf( out, outSize, "%c%d", sign, secs );
		}
		return;
	}
	hundredths = ( ad % 1000 ) / 10;
	if ( hours > 0 ) {
		Com_sprintf( out, outSize, "%c%d:%02d:%02d", sign, hours, mins, secs );
	} else if ( mins > 0 ) {
		Com_sprintf( out, outSize, "%c%d:%02d", sign, mins, secs );
	} else {
		Com_sprintf( out, outSize, "%c%d.%02d", sign, secs, hundredths );
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
		/* Mission stats are authoritative and can legitimately go
		   backwards after quickload, death reload, or loading an older
		   save.  Do not keep a high-watermark here: 100% tracking must
		   always show the exact current per-map state. */
		ls.liveObjectivesFound = vals[3];
		ls.liveObjectivesTotal = vals[4];
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
		} else if ( i == ls.currentMapIndex && ls.splits[i].currentTimeMs > 0 ) {
			/* Current split: use MAX(live time, gold) so BPT updates live */
			int gold = ls.splits[i].d[di].bestTimeMs;
			int live = ls.splits[i].currentTimeMs;
			if ( gold > 0 ) {
				bpt += ( live > gold ) ? live : gold;
			} else {
				bpt += live;
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

/* Sum of PB segments */
static int LS_CalcSumOfPB( void ) {
	int i, sum = 0, di = LS_CurDiffIdx();
	qboolean allValid = qtrue;
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].d[di].pbSegmentMs > 0 ) {
			sum += ls.splits[i].d[di].pbSegmentMs;
		} else {
			allValid = qfalse;
		}
	}
	return allValid ? sum : -1;
}

/* Sum of Average segments (from history) */
static int LS_CalcSumOfAvgs( void ) {
	int i, sum = 0, di = LS_CurDiffIdx();
	int segN = 0;
	int mode = ls.runMode, mission = ls.runMission;
	qboolean allValid = qtrue;

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		int j, total = 0, count = 0;
		if ( ls.splits[i].cutscene ) continue;
		for ( j = 0; j < ls.numHistoryRuns; j++ ) {
			lsRunHistory_t *r = &ls.history[j];
			if ( r->mode != mode ) continue;
			if ( LS_DiffIdx( r->difficulty ) != ( di % LS_MAX_DIFFICULTIES ) ) continue;
			if ( mode == LS_MODE_MISSION && r->missionNum != mission ) continue;
			if ( segN < r->numSplits && r->splitTimes[segN] > 0 ) {
				total += r->splitTimes[segN];
				count++;
			}
		}
		if ( count > 0 ) {
			sum += total / count;
		} else {
			allValid = qfalse;
		}
		segN++;
	}
	return allValid ? sum : -1;
}

/* Best Possible using PB segments for future splits */
static int LS_CalcBestPossiblePB( void ) {
	int i, bpt = 0, di = LS_CurDiffIdx();
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		if ( ls.splits[i].splitDone ) {
			bpt += ls.splits[i].currentTimeMs;
		} else if ( i == ls.currentMapIndex && ls.splits[i].currentTimeMs > 0 ) {
			int pb = ls.splits[i].d[di].pbSegmentMs;
			int live = ls.splits[i].currentTimeMs;
			if ( pb > 0 ) {
				bpt += ( live > pb ) ? live : pb;
			} else {
				bpt += live;
			}
		} else {
			if ( ls.splits[i].d[di].pbSegmentMs > 0 ) {
				bpt += ls.splits[i].d[di].pbSegmentMs;
			} else {
				return -1;
			}
		}
	}
	return bpt;
}

/* Best Possible using Average segments for future splits */
static int LS_CalcBestPossibleAvg( void ) {
	int i, bpt = 0, di = LS_CurDiffIdx();
	int segN = 0;
	int mode = ls.runMode, mission = ls.runMission;
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		int avgSeg = 0;
		if ( ls.splits[i].cutscene ) continue;
		/* compute average for this segment */
		{
			int j, total = 0, count = 0;
			for ( j = 0; j < ls.numHistoryRuns; j++ ) {
				lsRunHistory_t *r = &ls.history[j];
				if ( r->mode != mode ) continue;
				if ( LS_DiffIdx( r->difficulty ) != ( di % LS_MAX_DIFFICULTIES ) ) continue;
				if ( mode == LS_MODE_MISSION && r->missionNum != mission ) continue;
				if ( segN < r->numSplits && r->splitTimes[segN] > 0 ) {
					total += r->splitTimes[segN];
					count++;
				}
			}
			avgSeg = count > 0 ? total / count : 0;
		}
		if ( ls.splits[i].splitDone ) {
			bpt += ls.splits[i].currentTimeMs;
		} else if ( i == ls.currentMapIndex && ls.splits[i].currentTimeMs > 0 ) {
			int live = ls.splits[i].currentTimeMs;
			if ( avgSeg > 0 ) {
				bpt += ( live > avgSeg ) ? live : avgSeg;
			} else {
				bpt += live;
			}
		} else {
			if ( avgSeg > 0 ) {
				bpt += avgSeg;
			} else {
				segN++;
				return -1;
			}
		}
		segN++;
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
	if ( ls.currentMapIndex <= 0 || ls.currentMapIndex >= ls.numMaps ) return 0;
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

/* Cumulative average segment time up to and including splitIdx.
   Calculated from history entries matching current mode+difficulty.
   Returns -1 if no history data available. */
static int LS_CumulativeAverage( int upToIdx ) {
	int i, segN, di, mode, mission;
	int sum = 0;
	qboolean allValid = qtrue;

	di   = LS_CurDiffIdx();
	mode = ls.runMode;
	mission = ls.runMission;

	segN = 0;
	for ( i = ls.modeFirstIdx; i <= upToIdx && i < ls.numMaps; i++ ) {
		int j, total, count;
		if ( ls.splits[i].cutscene ) continue;

		total = 0; count = 0;
		for ( j = 0; j < ls.numHistoryRuns; j++ ) {
			lsRunHistory_t *r = &ls.history[j];
			if ( r->mode != mode ) continue;
			if ( LS_DiffIdx( r->difficulty ) != ( di % LS_MAX_DIFFICULTIES ) ) continue;
			if ( mode == LS_MODE_MISSION && r->missionNum != mission ) continue;
			if ( segN < r->numSplits && r->splitTimes[segN] > 0 ) {
				total += r->splitTimes[segN];
				count++;
			}
		}

		if ( count > 0 ) {
			sum += total / count;
		} else {
			allValid = qfalse;
		}
		segN++;
	}
	return allValid ? sum : ( sum > 0 ? sum : -1 );
}

/* forward declarations for cumulative helpers defined below */
static int LS_CumulativeBest( int upToIdx );
static int LS_CumulativePB( int upToIdx );

/* Get the comparison cumulative time for a given split index based on ls_compare mode.
   Returns -1 if no data available. */
static int LS_ComparisonCumulative( int upToIdx ) {
	int compare = ls_compareCvar ? ls_compareCvar->integer : LS_COMPARE_PB;
	switch ( compare ) {
	case LS_COMPARE_BEST:
		return LS_CumulativeBest( upToIdx );
	case LS_COMPARE_AVERAGE:
		return LS_CumulativeAverage( upToIdx );
	default: /* LS_COMPARE_PB */
		return LS_CumulativePB( upToIdx );
	}
}

/* Get the comparison segment time for a given split index.
   Returns 0 if no data available. */
static int LS_ComparisonSegment( int splitIdx ) {
	int di = LS_CurDiffIdx();
	int compare = ls_compareCvar ? ls_compareCvar->integer : LS_COMPARE_PB;
	switch ( compare ) {
	case LS_COMPARE_BEST:
		return ls.splits[splitIdx].d[di].bestTimeMs;
	case LS_COMPARE_AVERAGE: {
		/* Average from history */
		int j, segN, total = 0, count = 0;
		int mode = ls.runMode, mission = ls.runMission;
		/* Find segN for this split */
		segN = 0;
		for ( j = ls.modeFirstIdx; j < splitIdx && j < ls.numMaps; j++ ) {
			if ( !ls.splits[j].cutscene ) segN++;
		}
		for ( j = 0; j < ls.numHistoryRuns; j++ ) {
			lsRunHistory_t *r = &ls.history[j];
			if ( r->mode != mode ) continue;
			if ( LS_DiffIdx( r->difficulty ) != ( di % LS_MAX_DIFFICULTIES ) ) continue;
			if ( mode == LS_MODE_MISSION && r->missionNum != mission ) continue;
			if ( segN < r->numSplits && r->splitTimes[segN] > 0 ) {
				total += r->splitTimes[segN];
				count++;
			}
		}
		return count > 0 ? total / count : 0;
	}
	default: /* LS_COMPARE_PB */
		return ls.splits[splitIdx].d[di].pbSegmentMs;
	}
}

/* Check if any new golds were set during the current run.
   prevGoldMs stores the old best when the split was completed.
   If bestTimeMs differs from prevGoldMs, a new gold was set. */
static qboolean LS_HasNewGolds( void ) {
	int i, di = LS_CurDiffIdx();
	for ( i = 0; i < ls.numMaps; i++ ) {
		if ( ls.splits[i].splitDone &&
			 ls.splits[i].prevGoldMs > 0 &&
			 ls.splits[i].d[di].bestTimeMs != ls.splits[i].prevGoldMs ) {
			return qtrue;
		}
	}
	return qfalse;
}

/* Count how many splits have new golds this run */
static int LS_CountNewGolds( void ) {
	int i, count = 0, di = LS_CurDiffIdx();
	for ( i = 0; i < ls.numMaps; i++ ) {
		if ( ls.splits[i].splitDone &&
			 ls.splits[i].prevGoldMs > 0 &&
			 ls.splits[i].d[di].bestTimeMs != ls.splits[i].prevGoldMs ) {
			count++;
		}
	}
	return count;
}

/* Check if the finished run beat the previous PB.
   savedPBMs was stored at run start via LS_SavePreRunPBs. */
static qboolean LS_HasNewPB( void ) {
	int *pb;
	if ( !ls.runFinished ) return qfalse;
	LS_GetCatPB( &pb );
	/* New PB if either:
	   - savedPBMs was 0 (first run) and we have a time now, or
	   - current PB is lower than the saved one */
	if ( ls.savedPBMs == 0 && *pb > 0 ) return qtrue;
	if ( *pb > 0 && *pb < ls.savedPBMs ) return qtrue;
	return qfalse;
}

/* Combined check for backward compatibility */
static qboolean LS_HasUnsavedProgress( void ) {
	if ( LS_HasNewGolds() ) return qtrue;
	if ( LS_HasNewPB() ) return qtrue;
	return qfalse;
}

/* Cumulative time up to and including splitIdx (active set only, non-cutscene) */
static int LS_CumulativeTime( int upToIdx ) {
	int i, sum = 0;
	if ( ls.numMaps <= 0 ) return 0;
	if ( upToIdx < 0 ) return 0;
	if ( upToIdx >= ls.numMaps ) upToIdx = ls.numMaps - 1;
	i = ls.modeFirstIdx;
	if ( i < 0 ) i = 0;
	for ( ; i <= upToIdx && i < ls.numMaps; i++ ) {
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
	if ( ls.currentMapIndex < 0 || ls.currentMapIndex >= ls.numMaps ) return -1;
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
   LS_WindowUpdate - sync game state to the external window
   Runs on the game thread so it can safely access all ls.* data.
   ===================================================================== */
static void LS_SyncCategoryCvarsForDisplay( void );
static void LS_BuildCategoryText( char *out, int outSize );

static void LS_WindowUpdate( void ) {
	volatile lsWndState_t *st = &lswnd_state;
	int di, i, vi, igtMs, rtMs, timerMs, segMs;
	int compare;
	qboolean currentValid;

	memset( (void *)st, 0, sizeof( *st ) );
	st->curRow = -1;
	currentValid = ( ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps ) ? qtrue : qfalse;

	/* ---- title / header ---- */
	Q_strncpyz( (char *)st->gameName, "Return to Castle Wolfenstein", sizeof( st->gameName ) );
	LS_BuildCategoryText( (char *)st->categoryText, sizeof( st->categoryText ) );

	/* attempts / completions */
	{
		int *att;
		LS_GetCatAttempts( &att );
		st->attempts = *att;
	}
	{
		int *comp;
		LS_GetCatCompletions( &comp );
		st->completions = *comp;
	}

	/* comparison label */
	compare = ls_compareCvar ? ls_compareCvar->integer : LS_COMPARE_PB;
	switch ( compare ) {
	case LS_COMPARE_BEST:    Q_strncpyz( (char *)st->compareLabel, "Best Segments",    sizeof( st->compareLabel ) ); break;
	case LS_COMPARE_AVERAGE: Q_strncpyz( (char *)st->compareLabel, "Average Segments", sizeof( st->compareLabel ) ); break;
	default:                 Q_strncpyz( (char *)st->compareLabel, "Personal Best",    sizeof( st->compareLabel ) ); break;
	}

	/* Selected comparison total.  The field is named pbText for layout
	   compatibility, but it follows ls_compare for the overlay/window. */
	{
		int compareTotal = LS_ComparisonCumulative( ls.modeLastIdx );
		if ( compareTotal > 0 ) {
			LS_FormatTime( compareTotal, (char *)st->pbText, sizeof( st->pbText ) );
		} else {
			st->pbText[0] = '\0';
		}
	}

	/* ---- split rows ---- */
	di = LS_CurDiffIdx();
	vi = 0;
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps && vi < LSWND_MAX_ROWS; i++ ) {
		int cumTime, cumCmp;
		if ( ls.splits[i].cutscene ) continue;

		/* name */
		{
			const char *dn = ls.splits[i].displayName;
			if ( !dn || !dn[0] ) dn = ls.splits[i].mapname;
			Q_strncpyz( (char *)st->rows[vi].name, dn, sizeof( st->rows[vi].name ) );
			if ( ls.splits[i].shortName && ls.splits[i].shortName[0] )
				Q_strncpyz( (char *)st->rows[vi].shortName, ls.splits[i].shortName, sizeof( st->rows[vi].shortName ) );
			else
				( (char *)st->rows[vi].shortName )[0] = '\0';
		}

		/* state */
		if ( ls.splits[i].splitSkipped ) {
			st->rows[vi].state = 3;
		} else if ( ls.splits[i].splitDone ) {
			st->rows[vi].state = 2;
		} else if ( currentValid && i == ls.currentMapIndex ) {
			st->rows[vi].state = 1;
		} else {
			/* check if current cutscene maps to this split */
			if ( currentValid && ls.splits[ls.currentMapIndex].cutscene ) {
				int realIdx = LS_ActiveRealSplit();
				st->rows[vi].state = ( realIdx == i ) ? 1 : 0;
			} else {
				st->rows[vi].state = 0;
			}
		}

		/* cumulative time, segment time & delta (completed splits only) */
		st->rows[vi].splitTime[0] = '\0';
		st->rows[vi].segTime[0]   = '\0';
		st->rows[vi].delta[0]     = '\0';
		st->rows[vi].deltaBest[0] = '\0';
		st->rows[vi].bestSeg[0]   = '\0';
		st->rows[vi].pbSplitTime[0] = '\0';
		st->rows[vi].isGold       = 0;
		st->rows[vi].isBehind     = 0;
		st->rows[vi].liveDeltaMs  = 0x80000000;
		st->rows[vi].cumTimeMs    = 0;
		st->rows[vi].pbCumMs      = -1;
		st->rows[vi].bestCumMs    = -1;
		st->rows[vi].avgCumMs     = -1;
		st->rows[vi].segTimeMs    = 0;
		st->rows[vi].segCompareMs = 0;
		st->rows[vi].bestSegMs    = 0;
		st->rows[vi].pbSegMs      = 0;
		st->rows[vi].pbSegTime[0] = '\0';
		st->rows[vi].segSecretsFound  = 0;
		st->rows[vi].segSecretsTotal  = 0;
		st->rows[vi].segTreasureFound = 0;
		st->rows[vi].segTreasureTotal = 0;

		/* per-segment 100% data */
		st->rows[vi].segSecretsTotal  = ls.splits[i].secretsTotal;
		st->rows[vi].segTreasureTotal = ls.splits[i].treasureTotal;
		if ( ls.splits[i].splitDone ) {
			st->rows[vi].segSecretsFound  = ls.splits[i].secretsFound;
			st->rows[vi].segTreasureFound = ls.splits[i].treasureFound;
		} else if ( currentValid && i == ls.currentMapIndex ) {
			st->rows[vi].segSecretsFound  = ls.liveSecretsFound;
			st->rows[vi].segTreasureFound = ls.liveTreasureFound;
		}

		/* best segment time (always show if available) */
		{
			int bestMs = ls.splits[i].d[di].bestTimeMs;
			if ( bestMs > 0 ) {
				st->rows[vi].bestSegMs = bestMs;
				LS_FormatTime( bestMs, (char *)st->rows[vi].bestSeg, sizeof( st->rows[vi].bestSeg ) );
			}
		}

		/* Comparison cumulative split time (show for all splits).  The field
		   name is kept for renderer compatibility, but it follows ls_compare. */
		{
			int cmpCum = LS_ComparisonCumulative( i );
			int pbCum = LS_CumulativePB( i );
			if ( pbCum > 0 ) {
				st->rows[vi].pbCumMs = pbCum;
			}
			if ( cmpCum > 0 ) {
				LS_FormatTime( cmpCum, (char *)st->rows[vi].pbSplitTime, sizeof( st->rows[vi].pbSplitTime ) );
			}
		}

		/* Comparison segment time (for DetailedTimer).  Keep pbSegMs as raw PB
		   data, but format pbSegTime from the selected ls_compare mode. */
		{
			int pbSeg = ls.splits[i].d[di].pbSegmentMs;
			int cmpSeg = LS_ComparisonSegment( i );
			if ( pbSeg > 0 ) {
				st->rows[vi].pbSegMs = pbSeg;
			}
			if ( cmpSeg > 0 ) {
				LS_FormatTime( cmpSeg, (char *)st->rows[vi].pbSegTime, sizeof( st->rows[vi].pbSegTime ) );
			}
		}

		/* best-segments cumulative */
		{
			int bestCum = LS_CumulativeBest( i );
			if ( bestCum > 0 ) {
				st->rows[vi].bestCumMs = bestCum;
			}
		}

		/* average-segments cumulative */
		{
			int avgCum = LS_CumulativeAverage( i );
			if ( avgCum > 0 ) {
				st->rows[vi].avgCumMs = avgCum;
			}
		}

		/* comparison segment time (for countdown/current display) */
		{
			int cmpSeg = LS_ComparisonSegment( i );
			if ( cmpSeg > 0 ) st->rows[vi].segCompareMs = cmpSeg;
		}

		if ( ls.splits[i].splitDone && !ls.splits[i].splitSkipped ) {
			cumTime = LS_CumulativeTime( i );
			st->rows[vi].cumTimeMs = cumTime;
			LS_FormatTime( cumTime, (char *)st->rows[vi].splitTime, sizeof( st->rows[vi].splitTime ) );

			/* individual segment time */
			st->rows[vi].segTimeMs = ls.splits[i].currentTimeMs;
			LS_FormatTime( ls.splits[i].currentTimeMs, (char *)st->rows[vi].segTime, sizeof( st->rows[vi].segTime ) );

			/* delta vs comparison */
			cumCmp = LS_ComparisonCumulative( i );
			if ( cumCmp > 0 ) {
				int dt = cumTime - cumCmp;
				LS_FormatDelta( dt, (char *)st->rows[vi].delta, sizeof( st->rows[vi].delta ) );
				st->rows[vi].isBehind = ( dt > 0 ) ? 1 : 0;
			}

			/* gold check - use prevGoldMs (pre-update value) so a freshly-broken
			   gold still compares against the OLD best, not the updated one */
			{
				int gold = ls.splits[i].d[di].bestTimeMs;
				int compareGold = ( ls.splits[i].prevGoldMs > 0 )
					? ls.splits[i].prevGoldMs : gold;
				if ( compareGold > 0 && ls.splits[i].currentTimeMs <= compareGold ) {
					st->rows[vi].isGold = 1;
				}
			}

			/* delta from best segment (seg time - gold) - same prevGoldMs logic.
			   When prevGoldMs == 0 this is the first-ever completion, so there is
			   no meaningful delta to show (seg == gold > delta would be 0). */
			{
				int gold = ls.splits[i].d[di].bestTimeMs;
				if ( ls.splits[i].prevGoldMs > 0 && gold > 0 ) {
					int dbDelta = ls.splits[i].currentTimeMs - ls.splits[i].prevGoldMs;
					LS_FormatDelta( dbDelta, (char *)st->rows[vi].deltaBest, sizeof( st->rows[vi].deltaBest ) );
				} else {
					Q_strncpyz( (char *)st->rows[vi].deltaBest, "-", sizeof( st->rows[vi].deltaBest ) );
				}
			}
		} else if ( ls.splits[i].splitSkipped ) {
			Q_strncpyz( (char *)st->rows[vi].splitTime, "---", sizeof( st->rows[vi].splitTime ) );
		} else if ( st->rows[vi].state == 1 ) {
			/* Current split: keep the split-time column static on PB until
			   this split is actually completed.  Live time still feeds deltas
			   and the timer component, but the row time is updated only after
			   the player reaches the next split. */
			cumTime = LS_CumulativeTime( i );
			st->rows[vi].cumTimeMs = cumTime;
			st->rows[vi].segTimeMs = ls.splits[i].currentTimeMs;
		}

		/* Completed splits with no comparison data: show "-" placeholder.
		   Future / current splits intentionally left blank. */
		if ( ls.splits[i].splitDone && !ls.splits[i].splitSkipped ) {
			if ( !st->rows[vi].delta[0] )
				Q_strncpyz( (char *)st->rows[vi].delta, "-", sizeof( st->rows[vi].delta ) );
			if ( !st->rows[vi].deltaBest[0] )
				Q_strncpyz( (char *)st->rows[vi].deltaBest, "-", sizeof( st->rows[vi].deltaBest ) );
		}

		/* live cumulative delta for current split (for delta countdown + colors) */
		if ( st->rows[vi].state == 1 && !ls.splits[i].splitDone ) {
			int cumNow = LS_CumulativeTime( i );
			int cumCmpVal = LS_ComparisonCumulative( i );
			qboolean bestStarted = qfalse;
			if ( cumCmpVal > 0 ) {
				st->rows[vi].liveDeltaMs = cumNow - cumCmpVal;
				if ( cumNow >= cumCmpVal ) {
					LS_FormatDelta( st->rows[vi].liveDeltaMs, (char *)st->rows[vi].delta, sizeof( st->rows[vi].delta ) );
					st->rows[vi].isBehind = ( st->rows[vi].liveDeltaMs > 0 ) ? 1 : 0;
				}
			}
			if ( st->rows[vi].bestSegMs > 0 && st->rows[vi].segTimeMs >= st->rows[vi].bestSegMs ) {
				int bestDelta = st->rows[vi].segTimeMs - st->rows[vi].bestSegMs;
				bestStarted = qtrue;
				LS_FormatDelta( bestDelta, (char *)st->rows[vi].deltaBest, sizeof( st->rows[vi].deltaBest ) );
				if ( bestDelta <= 0 ) {
					st->rows[vi].isGold = 1;
				}
			}
			if ( bestStarted && cumCmpVal > 0 && !st->rows[vi].delta[0] ) {
				st->rows[vi].liveDeltaMs = cumNow - cumCmpVal;
				LS_FormatDelta( st->rows[vi].liveDeltaMs, (char *)st->rows[vi].delta, sizeof( st->rows[vi].delta ) );
				st->rows[vi].isBehind = ( st->rows[vi].liveDeltaMs > 0 ) ? 1 : 0;
			}
		}

		/* track current row index */
		if ( st->rows[vi].state == 1 ) {
			st->curRow = vi;
		}

		vi++;
	}
	st->numRows = vi;

	/* ---- best segments list (for Best Segments component) ---- */
	{
		int bsi = 0, j;
		for ( j = ls.modeFirstIdx; j <= ls.modeLastIdx && j < ls.numMaps && bsi < LSWND_MAX_ROWS; j++ ) {
			int bestMs;
			const char *dn;
			if ( ls.splits[j].cutscene ) continue;
			bestMs = ls.splits[j].d[di].bestTimeMs;
			dn = ls.splits[j].displayName;
			if ( !dn || !dn[0] ) dn = ls.splits[j].mapname;
			Q_strncpyz( (char *)st->bestSegs[bsi].name, dn, sizeof( st->bestSegs[bsi].name ) );
			if ( bestMs > 0 ) {
				LS_FormatTime( bestMs, (char *)st->bestSegs[bsi].time, sizeof( st->bestSegs[bsi].time ) );
			} else {
				Q_strncpyz( (char *)st->bestSegs[bsi].time, "-", sizeof( st->bestSegs[bsi].time ) );
			}

			/* state & delta for best segments component */
			st->bestSegs[bsi].delta[0] = '\0';
			st->bestSegs[bsi].deltaMs  = 0x80000000; /* INT_MIN = n/a */
			st->bestSegs[bsi].isGold   = 0;

			if ( ls.splits[j].splitSkipped ) {
				st->bestSegs[bsi].state = 2; /* treat as done */
			} else if ( ls.splits[j].splitDone ) {
				int segMs = ls.splits[j].currentTimeMs;
				st->bestSegs[bsi].state = 2;
				/* Only show delta when there was a previous gold to compare against.
				   On first-ever completion prevGoldMs == 0, so seg == gold > delta 0. */
				if ( ls.splits[j].prevGoldMs > 0 && segMs > 0 ) {
					int dt = segMs - ls.splits[j].prevGoldMs;
					st->bestSegs[bsi].deltaMs = dt;
					LS_FormatDelta( dt, (char *)st->bestSegs[bsi].delta,
						sizeof( st->bestSegs[bsi].delta ) );
					if ( dt <= 0 ) {
						st->bestSegs[bsi].isGold = 1;
					}
				} else if ( segMs > 0 && ls.splits[j].prevGoldMs == 0 ) {
					/* first-ever completion - mark as gold with no delta */
					st->bestSegs[bsi].isGold = 1;
					Q_strncpyz( (char *)st->bestSegs[bsi].delta, "-",
						sizeof( st->bestSegs[bsi].delta ) );
				}
			} else if ( currentValid && j == ls.currentMapIndex ) {
				st->bestSegs[bsi].state = 1;
				} else if ( currentValid && ls.splits[ls.currentMapIndex].cutscene ) {
				int realIdx = LS_ActiveRealSplit();
				st->bestSegs[bsi].state = ( realIdx == j ) ? 1 : 0;
			} else {
				st->bestSegs[bsi].state = 0;
			}

			bsi++;
		}
		st->numBestSegs = bsi;
	}

	/* ---- timer ---- */
	if ( ls.runFinished ) {
		igtMs = ls.runTotalIGTMs;
	} else if ( !ls.active ) {
		igtMs = 0;
	} else {
		igtMs = LS_CumulativeTime( ls.modeLastIdx );
	}
	if ( ls.active && !ls.runFinished ) {
		rtMs = Sys_Milliseconds() - ls.runStartRealMs;
	} else {
		rtMs = ls.runSavedRealMs;
	}
	timerMs = ( ls_timingCvar && ls_timingCvar->integer == LS_TIMING_REALTIME ) ? rtMs : igtMs;
	LS_FormatTime( timerMs, (char *)st->timerText, sizeof( st->timerText ) );
	st->timerFrac[0] = '\0'; /* fraction split done locally in DetailedTimer renderer */

	/* ---- real-time (RTA) timer ---- */
	LS_FormatTime( rtMs, (char *)st->rtTimerText, sizeof( st->rtTimerText ) );
	st->rtTimerFrac[0] = '\0';

	/* timerBehind: 1 if current cumulative delta > 0 */
	st->timerBehind = 0;
	if ( st->curRow >= 0 && st->curRow < st->numRows ) {
		if ( st->rows[st->curRow].liveDeltaMs != (int)0x80000000
			&& st->rows[st->curRow].liveDeltaMs > 0 ) {
			st->timerBehind = 1;
		} else if ( st->rows[st->curRow].isBehind ) {
			st->timerBehind = 1;
		}
	}

	/* segment timer */
	segMs = 0;
	if ( ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps ) {
		if ( !ls.splits[ls.currentMapIndex].cutscene ) {
			segMs = ls.splits[ls.currentMapIndex].currentTimeMs;
		} else {
			int realIdx = LS_ActiveRealSplit();
			if ( realIdx >= 0 ) segMs = ls.splits[realIdx].currentTimeMs;
		}
	}
	LS_FormatTime( segMs, (char *)st->segTimerText, sizeof( st->segTimerText ) );
	st->segTimerFrac[0] = '\0'; /* fraction split done locally in DetailedTimer renderer */

	/* ---- info rows ---- */

	/* Previous Segment / Live Segment */
	{
		int activeIdx = LS_ActiveRealSplit();
		qboolean showLive = qfalse;

		st->prevSegLabel[0] = '\0';
		st->prevSegValue[0] = '\0';
		st->prevSegBehind   = -1;
		st->prevSegGold     = 0;

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
			Q_strncpyz( (char *)st->prevSegLabel, "Live Segment", sizeof( st->prevSegLabel ) );
			if ( liveDelta == 0 ) {
				Q_strncpyz( (char *)st->prevSegValue, "---", sizeof( st->prevSegValue ) );
			} else {
				LS_FormatDelta( liveDelta, (char *)st->prevSegValue, sizeof( st->prevSegValue ) );
				st->prevSegBehind = ( liveDelta > 0 ) ? 1 : 0;
				st->prevSegGold   = ( liveDelta < 0 ) ? 1 : 0;
			}
		} else {
			qboolean hasPrev;
			int prevSeg = LS_CalcPreviousSegment( &hasPrev );
			Q_strncpyz( (char *)st->prevSegLabel, "Previous Segment", sizeof( st->prevSegLabel ) );
			if ( hasPrev ) {
				if ( prevSeg == 0 ) {
					Q_strncpyz( (char *)st->prevSegValue, "---", sizeof( st->prevSegValue ) );
				} else {
					LS_FormatDelta( prevSeg, (char *)st->prevSegValue, sizeof( st->prevSegValue ) );
					st->prevSegBehind = ( prevSeg > 0 ) ? 1 : 0;
					st->prevSegGold   = ( prevSeg < 0 ) ? 1 : 0;
				}
			} else {
				Q_strncpyz( (char *)st->prevSegValue, "-", sizeof( st->prevSegValue ) );
			}
		}
	}

	/* Sum of Best Segments (all variants) */
	{
		int sob = LS_CalcSumOfBests();
		st->sobBestMs = sob;
		if ( sob >= 0 ) {
			LS_FormatTime( sob, (char *)st->sobText, sizeof( st->sobText ) );
		} else {
			st->sobText[0] = '\0';
		}
		st->sobPbMs  = LS_CalcSumOfPB();
		st->sobAvgMs = LS_CalcSumOfAvgs();
	}

	/* Best Possible Time (all variants) */
	{
		int bpt = LS_CalcBestPossible();
		st->bptBestMs = bpt;
		if ( bpt >= 0 ) {
			LS_FormatTime( bpt, (char *)st->bptText, sizeof( st->bptText ) );
		} else {
			st->bptText[0] = '\0';
		}
		st->bptPbMs  = LS_CalcBestPossiblePB();
		st->bptAvgMs = LS_CalcBestPossibleAvg();
	}

	/* Possible Time Save  (static: comparison_seg - best_seg, plain time) */
	{
		int mi = LS_ActiveRealSplit();
		if ( mi < 0 ) mi = ls.modeFirstIdx;
		{
		int bestSeg = ( mi >= 0 && mi < ls.numMaps ) ? ls.splits[mi].d[di].bestTimeMs : 0;
		int cmpSeg  = ( mi >= 0 && mi < ls.numMaps ) ? LS_ComparisonSegment( mi ) : 0;
		st->ptsMs = 0x80000000;
		if ( bestSeg > 0 && cmpSeg > 0 ) {
			int pts = cmpSeg - bestSeg;
			if ( pts < 0 ) pts = 0;
			st->ptsMs = pts;
			LS_FormatTime( pts, (char *)st->ptsText, sizeof( st->ptsText ) );
		} else {
			st->ptsText[0] = '\0';
		}
		}
	}

	/* status */
	/* 100% tracking: accumulate secrets/treasures across active splits */
	{
		int i, sf = 0, st2 = 0, tf = 0, tt = 0;
		for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
			if ( ls.splits[i].cutscene ) continue;
			if ( ls.splits[i].splitDone ) {
				sf += ls.splits[i].secretsFound;
				tf += ls.splits[i].treasureFound;
			} else if ( currentValid && i == ls.currentMapIndex ) {
				/* Live values for the current map */
				sf += ls.liveSecretsFound;
				tf += ls.liveTreasureFound;
			}
			st2 += ls.splits[i].secretsTotal;
			tt  += ls.splits[i].treasureTotal;
		}
		st->pctSecretsFound  = sf;
		st->pctSecretsTotal  = st2;
		st->pctTreasureFound = tf;
		st->pctTreasureTotal = tt;
	}

	st->active   = ls.active   ? 1 : 0;
	st->finished = ls.runFinished ? 1 : 0;
	st->paused   = ls.manualPause ? 1 : 0;

	/* push to shared memory for standalone exe */
	LS_ShmUpdate();
}

static void LS_WindowUpdateMenu( void ) {
	volatile lsWndState_t *st = &lswnd_state;
	int di, i, vi, bsi;
	int igtMs, rtMs, timerMs, segMs;
	int pbCum, bestCum;
	qboolean pbComplete, bestComplete;

	LS_SyncCategoryCvarsForDisplay();

	memset( (void *)st, 0, sizeof( *st ) );

	st->curRow = -1;
	st->prevSegBehind = -1;
	st->sobBestMs = -1;
	st->sobPbMs = -1;
	st->sobAvgMs = -1;
	st->bptBestMs = -1;
	st->bptPbMs = -1;
	st->bptAvgMs = -1;
	st->ptsMs = 0x80000000;
	st->ghostSegMs = -1;

	Q_strncpyz( (char *)st->gameName, "Return to Castle Wolfenstein", sizeof( st->gameName ) );
	LS_BuildCategoryText( (char *)st->categoryText, sizeof( st->categoryText ) );
	Q_strncpyz( (char *)st->compareLabel, "Personal Best", sizeof( st->compareLabel ) );
	Q_strncpyz( (char *)st->prevSegLabel, "Previous Segment", sizeof( st->prevSegLabel ) );
	Q_strncpyz( (char *)st->prevSegValue, "-", sizeof( st->prevSegValue ) );
	if ( ls.runFinished ) {
		igtMs = ls.runTotalIGTMs;
	} else if ( ls.active ) {
		igtMs = LS_CumulativeTime( ls.modeLastIdx );
	} else {
		igtMs = 0;
	}
	if ( ls.active && !ls.runFinished && ls.runStartRealMs > 0 ) {
		rtMs = Sys_Milliseconds() - ls.runStartRealMs;
	} else {
		rtMs = ls.runSavedRealMs;
	}
	if ( igtMs < 0 ) igtMs = 0;
	if ( rtMs < 0 ) rtMs = 0;
	timerMs = ( ls_timingCvar && ls_timingCvar->integer == LS_TIMING_REALTIME ) ? rtMs : igtMs;
	LS_FormatTime( timerMs, (char *)st->timerText, sizeof( st->timerText ) );
	LS_FormatTime( rtMs, (char *)st->rtTimerText, sizeof( st->rtTimerText ) );
	segMs = 0;
	if ( ls.currentMapIndex >= 0 && ls.currentMapIndex < ls.numMaps ) {
		if ( !ls.splits[ls.currentMapIndex].cutscene ) {
			segMs = ls.splits[ls.currentMapIndex].currentTimeMs;
		} else {
			int realIdx = LS_ActiveRealSplit();
			if ( realIdx >= 0 ) segMs = ls.splits[realIdx].currentTimeMs;
		}
	}
	LS_FormatTime( segMs, (char *)st->segTimerText, sizeof( st->segTimerText ) );

	di = LS_CurDiffIdx();
	pbCum = 0;
	bestCum = 0;
	pbComplete = qtrue;
	bestComplete = qtrue;
	vi = 0;
	bsi = 0;

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps && vi < LSWND_MAX_ROWS; i++ ) {
		const char *dn;
		int pbSeg, bestSeg;

		if ( ls.splits[i].cutscene ) continue;

		dn = ls.splits[i].displayName;
		if ( !dn || !dn[0] ) dn = ls.splits[i].mapname;
		Q_strncpyz( (char *)st->rows[vi].name, dn, sizeof( st->rows[vi].name ) );
		if ( ls.splits[i].shortName && ls.splits[i].shortName[0] ) {
			Q_strncpyz( (char *)st->rows[vi].shortName, ls.splits[i].shortName, sizeof( st->rows[vi].shortName ) );
		}

		st->rows[vi].state = 0;
		st->rows[vi].liveDeltaMs = 0x80000000;
		st->rows[vi].pbCumMs = -1;
		st->rows[vi].bestCumMs = -1;
		st->rows[vi].avgCumMs = -1;

		pbSeg = ls.splits[i].d[di].pbSegmentMs;
		bestSeg = ls.splits[i].d[di].bestTimeMs;
		if ( pbSeg > 0 ) {
			st->rows[vi].pbSegMs = pbSeg;
			st->rows[vi].segCompareMs = pbSeg;
			LS_FormatTime( pbSeg, (char *)st->rows[vi].pbSegTime, sizeof( st->rows[vi].pbSegTime ) );
			if ( pbComplete ) {
				pbCum += pbSeg;
				st->rows[vi].pbCumMs = pbCum;
				LS_FormatTime( pbCum, (char *)st->rows[vi].pbSplitTime, sizeof( st->rows[vi].pbSplitTime ) );
			}
		} else {
			pbComplete = qfalse;
		}

		if ( bestSeg > 0 ) {
			st->rows[vi].bestSegMs = bestSeg;
			LS_FormatTime( bestSeg, (char *)st->rows[vi].bestSeg, sizeof( st->rows[vi].bestSeg ) );
			if ( bestComplete ) {
				bestCum += bestSeg;
				st->rows[vi].bestCumMs = bestCum;
			}
		} else {
			bestComplete = qfalse;
		}

		vi++;
	}
	st->numRows = vi;
	if ( pbComplete && pbCum > 0 ) {
		st->sobPbMs = pbCum;
		st->bptPbMs = pbCum;
		LS_FormatTime( pbCum, (char *)st->pbText, sizeof( st->pbText ) );
	}
	if ( bestComplete && bestCum > 0 ) {
		st->sobBestMs = bestCum;
		st->bptBestMs = bestCum;
		LS_FormatTime( bestCum, (char *)st->sobText, sizeof( st->sobText ) );
		LS_FormatTime( bestCum, (char *)st->bptText, sizeof( st->bptText ) );
	}

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps && bsi < LSWND_MAX_ROWS; i++ ) {
		const char *dn;
		int bestSeg;

		if ( ls.splits[i].cutscene ) continue;

		dn = ls.splits[i].displayName;
		if ( !dn || !dn[0] ) dn = ls.splits[i].mapname;
		Q_strncpyz( (char *)st->bestSegs[bsi].name, dn, sizeof( st->bestSegs[bsi].name ) );
		bestSeg = ls.splits[i].d[di].bestTimeMs;
		if ( bestSeg > 0 ) {
			LS_FormatTime( bestSeg, (char *)st->bestSegs[bsi].time, sizeof( st->bestSegs[bsi].time ) );
		} else {
			Q_strncpyz( (char *)st->bestSegs[bsi].time, "-", sizeof( st->bestSegs[bsi].time ) );
		}
		st->bestSegs[bsi].deltaMs = 0x80000000;
		bsi++;
	}
	st->numBestSegs = bsi;

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( ls.splits[i].cutscene ) continue;
		st->pctSecretsTotal += ls.splits[i].secretsTotal;
		st->pctTreasureTotal += ls.splits[i].treasureTotal;
	}
	st->active = ls.active ? 1 : 0;
	st->finished = ls.runFinished ? 1 : 0;
	st->paused = ls.manualPause ? 1 : 0;

	LS_ShmUpdate();
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
   File I/O  (.lss LiveSplit XML format - one file per category+difficulty)
   ===================================================================== */

/* --- LSS time format helpers --- */

/* Format milliseconds as LiveSplit time: "HH:MM:SS.NNNNNNN" */
static void LS_FormatLssTime( int ms, char *out, int outSize ) {
	int hours, mins, secs, frac;
	if ( ms < 0 ) ms = 0;
	hours = ms / 3600000;
	mins  = ( ms / 60000 ) % 60;
	secs  = ( ms / 1000 ) % 60;
	frac  = ( ms % 1000 ) * 10000; /* 3 significant digits -> 7 decimal places */
	Com_sprintf( out, outSize, "%02d:%02d:%02d.%07d", hours, mins, secs, frac );
}

/* Parse LiveSplit time "HH:MM:SS.NNNNNNN" back to milliseconds */
static int LS_ParseLssTime( const char *str ) {
	int h = 0, m = 0, s = 0, frac = 0;
	if ( !str || !str[0] ) return 0;
	if ( sscanf( str, "%d:%d:%d.%d", &h, &m, &s, &frac ) >= 3 ) {
		/* frac may have variable number of digits; normalize to ms */
		/* count digits after the dot */
		const char *dot = strchr( str, '.' );
		if ( dot ) {
			int digits = (int)strlen( dot + 1 );
			while ( digits > 3 ) { frac /= 10; digits--; }
			while ( digits < 3 ) { frac *= 10; digits++; }
		}
		return h * 3600000 + m * 60000 + s * 1000 + frac;
	}
	return 0;
}

/* Format time_t as LiveSplit date: "MM/DD/YYYY HH:MM:SS" */
static void LS_FormatLssDate( time_t t, char *out, int outSize ) {
	struct tm *ti;
	if ( t <= 0 ) {
		Com_sprintf( out, outSize, "01/01/2000 00:00:00" );
		return;
	}
	ti = localtime( &t );
	Com_sprintf( out, outSize, "%02d/%02d/%04d %02d:%02d:%02d",
		ti->tm_mon + 1, ti->tm_mday, ti->tm_year + 1900,
		ti->tm_hour, ti->tm_min, ti->tm_sec );
}

/* Parse LiveSplit date "MM/DD/YYYY HH:MM:SS" to time_t */
static time_t LS_ParseLssDate( const char *str ) {
	struct tm ti;
	if ( !str || !str[0] ) return 0;
	memset( &ti, 0, sizeof( ti ) );
	if ( sscanf( str, "%d/%d/%d %d:%d:%d",
		&ti.tm_mon, &ti.tm_mday, &ti.tm_year,
		&ti.tm_hour, &ti.tm_min, &ti.tm_sec ) == 6 ) {
		ti.tm_mon -= 1;
		ti.tm_year -= 1900;
		ti.tm_isdst = -1; /* let mktime determine DST */
		return mktime( &ti );
	}
	return 0;
}

/* --- LSS file path helpers --- */

static void LS_GetLssPath( char *out, int outSize, int mode, int diffIdx, int missionGroup, int mapIdx, qboolean pct100, qboolean hl1 ) {
	const char *suffix = pct100 ? "_100" : "";
	const char *moveSuffix = LS_HL1ModeFileSuffixFor( hl1 );
	if ( diffIdx < 0 ) diffIdx = 0;
	if ( diffIdx >= LS_MAX_DIFFICULTIES ) diffIdx = LS_MAX_DIFFICULTIES - 1;

	switch ( mode ) {
	case LS_MODE_FULLGAME:
		Com_sprintf( out, outSize, LS_LSS_DIR_FG "/%s%s%s.lss", ls_diffFileNames[diffIdx], suffix, moveSuffix );
		break;
	case LS_MODE_MISSION:
		Com_sprintf( out, outSize, LS_LSS_DIR_MS "/chapter%d_%s%s%s.lss",
			missionGroup + 1, ls_diffFileNames[diffIdx], suffix, moveSuffix );
		break;
	case LS_MODE_IL:
		if ( mapIdx >= 0 && mapIdx < ls.numMaps )
			Com_sprintf( out, outSize, LS_LSS_DIR_IL "/%s_%s%s%s.lss",
				ls.splits[mapIdx].mapname, ls_diffFileNames[diffIdx], suffix, moveSuffix );
		else
			Com_sprintf( out, outSize, LS_LSS_DIR_IL "/unknown_%s%s%s.lss",
				ls_diffFileNames[diffIdx], suffix, moveSuffix );
		break;
	default:
		Com_sprintf( out, outSize, LS_LSS_DIR "/unknown.lss" );
		break;
	}
}

/* Get the .lss path for the CURRENT run's category */
static void LS_GetCurrentLssPath( char *out, int outSize ) {
	int di = LS_DiffIdx( ls.currentDifficulty );
	qboolean p100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? qtrue : qfalse;
	qboolean hl1 = LS_HL1ModeActive();
	switch ( ls.runMode ) {
	case LS_MODE_MISSION:
		LS_GetLssPath( out, outSize, LS_MODE_MISSION, di, ls.runMission - 1, -1, p100, hl1 );
		break;
	case LS_MODE_IL:
		LS_GetLssPath( out, outSize, LS_MODE_IL, di, -1, ls.modeFirstIdx, p100, hl1 );
		break;
	default:
		LS_GetLssPath( out, outSize, LS_MODE_FULLGAME, di, -1, -1, p100, hl1 );
		break;
	}
}

/* Build category display name for the .lss CategoryName field */
static void LS_GetCategoryDisplayName( char *out, int outSize, int mode, int diffIdx, int missionGroup, int mapIdx, qboolean pct100, qboolean hl1 ) {
	const char *diff = ls_diffDisplayNames[diffIdx];
	const char *pctTag = pct100 ? " 100%" : " Any%";
	const char *moveTag = LS_HL1ModeNameSuffixFor( hl1 );
	switch ( mode ) {
	case LS_MODE_FULLGAME:
		Com_sprintf( out, outSize, "Full Game%s%s - %s", pctTag, moveTag, diff );
		break;
	case LS_MODE_MISSION:
		if ( missionGroup >= 0 && missionGroup < LS_NUM_MISSION_GROUPS )
			Com_sprintf( out, outSize, "%s%s%s - %s", ls_missionGroups[missionGroup].name, pctTag, moveTag, diff );
		else
			Com_sprintf( out, outSize, "Chapter%s%s - %s", pctTag, moveTag, diff );
		break;
	case LS_MODE_IL:
		if ( mapIdx >= 0 && mapIdx < ls.numMaps && ls.splits[mapIdx].displayName )
			Com_sprintf( out, outSize, "%s IL%s%s - %s", ls.splits[mapIdx].displayName, pctTag, moveTag, diff );
		else
			Com_sprintf( out, outSize, "IL%s%s - %s", pctTag, moveTag, diff );
		break;
	default:
		Com_sprintf( out, outSize, "Unknown - %s", diff );
		break;
	}
}

/* --- Buffered write system for .lss files ---
   Accumulates all output in memory, then writes once at file close.
   This avoids hundreds of small FS_Write syscalls per save. */

#define LS_WBUF_INIT_SIZE  (128 * 1024)  /* 128 KB initial buffer */

typedef struct {
	char *data;
	int   len;
	int   capacity;
} lsWriteBuf_t;

static lsWriteBuf_t ls_wbuf;

static void LS_WBufInit( void ) {
	ls_wbuf.capacity = LS_WBUF_INIT_SIZE;
	ls_wbuf.data = (char *)Z_Malloc( ls_wbuf.capacity );
	ls_wbuf.len = 0;
}

static void LS_WBufFree( void ) {
	if ( ls_wbuf.data ) {
		Z_Free( ls_wbuf.data );
		ls_wbuf.data = NULL;
	}
	ls_wbuf.len = 0;
	ls_wbuf.capacity = 0;
}

static void LS_WBufGrow( int needed ) {
	int newCap = ls_wbuf.capacity;
	char *newData;
	while ( newCap < needed ) newCap *= 2;
	newData = (char *)Z_Malloc( newCap );
	if ( ls_wbuf.data ) {
		memcpy( newData, ls_wbuf.data, ls_wbuf.len );
		Z_Free( ls_wbuf.data );
	}
	ls_wbuf.data = newData;
	ls_wbuf.capacity = newCap;
}

/* Append string to write buffer, converting \n to \r\n */
static void LS_WriteStr( fileHandle_t f, const char *s ) {
	const char *p = s;
	(void)f; /* buffer-based; fileHandle used at flush time */
	while ( *p ) {
		const char *nl = strchr( p, '\n' );
		if ( !nl ) {
			int slen = (int)strlen( p );
			if ( ls_wbuf.len + slen > ls_wbuf.capacity ) {
				LS_WBufGrow( ls_wbuf.len + slen );
			}
			memcpy( ls_wbuf.data + ls_wbuf.len, p, slen );
			ls_wbuf.len += slen;
			break;
		}
		if ( nl > p ) {
			int chunk = (int)( nl - p );
			if ( ls_wbuf.len + chunk + 2 > ls_wbuf.capacity ) {
				LS_WBufGrow( ls_wbuf.len + chunk + 2 );
			}
			memcpy( ls_wbuf.data + ls_wbuf.len, p, chunk );
			ls_wbuf.len += chunk;
		} else {
			if ( ls_wbuf.len + 2 > ls_wbuf.capacity ) {
				LS_WBufGrow( ls_wbuf.len + 2 );
			}
		}
		ls_wbuf.data[ls_wbuf.len]     = '\r';
		ls_wbuf.data[ls_wbuf.len + 1] = '\n';
		ls_wbuf.len += 2;
		p = nl + 1;
	}
}

/* Flush the entire buffer to disk in one write and free it */
static void LS_WBufFlush( fileHandle_t f ) {
	if ( ls_wbuf.data && ls_wbuf.len > 0 ) {
		FS_Write( ls_wbuf.data, ls_wbuf.len, f );
	}
	LS_WBufFree();
}

/* --- Get segment range for a given mode/mission --- */
static void LS_GetSegmentRange( int mode, int missionGroup, int mapIdx,
								int *outFirst, int *outLast ) {
	int i;
	switch ( mode ) {
	case LS_MODE_MISSION: {
		int grpFirst = ls_missionGroups[missionGroup].firstMission;
		int grpLast  = ls_missionGroups[missionGroup].lastMission;
		*outFirst = -1; *outLast = -1;
		for ( i = 0; i < ls.numMaps; i++ ) {
			if ( ls.splits[i].mission >= grpFirst && ls.splits[i].mission <= grpLast ) {
				if ( *outFirst < 0 ) *outFirst = i;
				*outLast = i;
			}
		}
		break;
	}
	case LS_MODE_IL:
		*outFirst = mapIdx;
		*outLast  = mapIdx;
		break;
	default: /* FULLGAME */
		*outFirst = 0;
		*outLast  = ls.numMaps - 1;
		break;
	}
}

/* =====================================================================
   LS_WriteLss - write a single .lss file for one category+difficulty
   ===================================================================== */
static void LS_WriteLssEx( int mode, int diffIdx, int missionGroup, int mapIdx, qboolean pct100, qboolean hl1, qboolean forceWrite ) {
	fileHandle_t f;
	char path[256], catName[256], buf[1024], timeBuf[64];
	int i, n, firstIdx, lastIdx, slot, variant;
	int attemptCount;
	int numFilteredRuns;
	int *filteredIndices;

	LS_GetLssPath( path, sizeof( path ), mode, diffIdx, missionGroup, mapIdx, pct100, hl1 );
	LS_GetCategoryDisplayName( catName, sizeof( catName ), mode, diffIdx, missionGroup, mapIdx, pct100, hl1 );

	variant = LS_CategoryVariant( pct100, hl1 );
	slot = LS_DiffSlotFor( mode, diffIdx, pct100, hl1 );
	LS_GetSegmentRange( mode, missionGroup, mapIdx, &firstIdx, &lastIdx );
	if ( firstIdx < 0 || lastIdx < 0 ) return;

	/* Determine attempt count from the appropriate counter */
	switch ( mode ) {
	case LS_MODE_FULLGAME:
		attemptCount = ls.fgAttempts[slot];
		break;
	case LS_MODE_MISSION:
		attemptCount = ls.msAttempts[missionGroup][slot];
		break;
	case LS_MODE_IL:
		attemptCount = ls.splits[mapIdx].d[slot].totalAttempts;
		break;
	default:
		attemptCount = 0;
		break;
	}

	/* Check if there is any data worth saving */
	{
		qboolean hasData = qfalse;
		if ( attemptCount > 0 ) hasData = qtrue;
		if ( !hasData ) {
			for ( i = firstIdx; i <= lastIdx; i++ ) {
				if ( ls.splits[i].cutscene ) continue;
				if ( ls.splits[i].d[slot].bestTimeMs > 0 ||
					 ls.splits[i].d[slot].totalAttempts > 0 ) {
					hasData = qtrue;
					break;
				}
			}
		}
		if ( !hasData && !forceWrite ) return; /* skip empty categories */
	}

	/* Filter history runs matching this category */
	filteredIndices = (int *)Z_Malloc( ( ls.numHistoryRuns > 0 ? ls.numHistoryRuns : 1 ) * sizeof( int ) );
	numFilteredRuns = 0;
	for ( i = 0; i < ls.numHistoryRuns; i++ ) {
		lsRunHistory_t *r = &ls.history[i];
		if ( r->mode != mode ) continue;
		if ( r->categoryVariant != variant ) continue;
		if ( LS_DiffIdx( r->difficulty ) != diffIdx ) continue;
		if ( mode == LS_MODE_MISSION && r->missionNum != missionGroup + 1 ) continue;
		if ( mode == LS_MODE_IL && r->mapIdx != mapIdx ) continue;
		filteredIndices[numFilteredRuns++] = i;
	}

	FS_FOpenFileByMode( path, &f, FS_WRITE );
	if ( !f ) {
		Z_Free( filteredIndices );
		Com_Printf( "^1LiveSplit: Could not write %s\n", path );
		return;
	}

	/* Initialize write buffer - all LS_WriteStr calls go here */
	LS_WBufInit();

	/* UTF-8 BOM (written directly to buffer) */
	if ( ls_wbuf.len + 3 > ls_wbuf.capacity ) {
		LS_WBufGrow( ls_wbuf.len + 3 );
	}
	memcpy( ls_wbuf.data + ls_wbuf.len, "\xEF\xBB\xBF", 3 );
	ls_wbuf.len += 3;

	/* XML header */
	LS_WriteStr( f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );
	LS_WriteStr( f, "<Run version=\"1.7.0\">\n" );
	if ( ls.gameIcon ) {
		LS_WriteStr( f, "  <GameIcon>" );
		LS_WriteStr( f, ls.gameIcon );
		LS_WriteStr( f, "</GameIcon>\n" );
	} else {
		LS_WriteStr( f, "  <GameIcon />\n" );
	}
	LS_WriteStr( f, "  <GameName>Return to Castle Wolfenstein</GameName>\n" );

	Com_sprintf( buf, sizeof( buf ), "  <CategoryName>%s</CategoryName>\n", catName );
	LS_WriteStr( f, buf );

	LS_WriteStr( f, "  <Metadata>\n" );
	LS_WriteStr( f, "    <Run id=\"\" />\n" );
	LS_WriteStr( f, "    <Platform usesEmulator=\"False\">\n" );
	LS_WriteStr( f, "    </Platform>\n" );
	LS_WriteStr( f, "    <Region>\n" );
	LS_WriteStr( f, "    </Region>\n" );
	LS_WriteStr( f, "    <Variables />\n" );
	LS_WriteStr( f, "  </Metadata>\n" );
	LS_WriteStr( f, "  <Offset>00:00:00</Offset>\n" );

	Com_sprintf( buf, sizeof( buf ), "  <AttemptCount>%d</AttemptCount>\n", attemptCount );
	LS_WriteStr( f, buf );

	/* AttemptHistory - write both completed runs and reset attempts merged by attemptId */
	LS_WriteStr( f, "  <AttemptHistory>\n" );
	{
		/* Filter reset attempts matching this category */
		int *filteredResets = NULL;
		int numFilteredResets = 0;
		int resetStartOfs = 0;  /* offset into filteredResets to skip old entries */
		int compIdx = 0, resetIdx = 0;

		if ( ls.numResetAttempts > 0 ) {
			filteredResets = (int *)Z_Malloc( ls.numResetAttempts * sizeof( int ) );
			for ( n = 0; n < ls.numResetAttempts; n++ ) {
				lsResetAttempt_t *ra = &ls.resetAttempts[n];
				if ( ra->mode != mode ) continue;
				if ( ra->categoryVariant != variant ) continue;
				if ( LS_DiffIdx( ra->difficulty ) != diffIdx ) continue;
				if ( mode == LS_MODE_MISSION && ra->missionNum != missionGroup + 1 ) continue;
				if ( mode == LS_MODE_IL && ra->mapIdx != mapIdx ) continue;
				filteredResets[numFilteredResets++] = n;
			}
		}

		/* Keep only the last 2000 reset attempts per category.
		   Completed runs (with GameTime/RealTime) are always kept in full
		   since they contain data used for PB/gold/history display.
		   Reset attempts only store start/end timestamps with no useful
		   timing data, so capping them prevents unbounded file growth. */
		if ( numFilteredResets > 2000 ) {
			resetStartOfs = numFilteredResets - 2000;
		}

		/* Merge completed runs and resets in ascending attemptId order.
		   Both filtered lists are already in load order (ascending id). */
		compIdx = 0;
		resetIdx = resetStartOfs;
		while ( compIdx < numFilteredRuns || resetIdx < numFilteredResets ) {
			int compId  = ( compIdx < numFilteredRuns )
				? ls.history[filteredIndices[compIdx]].attemptId : 0x7FFFFFFF;
			int resetId = ( resetIdx < numFilteredResets )
				? ls.resetAttempts[filteredResets[resetIdx]].attemptId : 0x7FFFFFFF;

			if ( compId <= resetId && compIdx < numFilteredRuns ) {
				/* Write completed run */
				lsRunHistory_t *r = &ls.history[filteredIndices[compIdx]];
				char startBuf[32], endBuf[32], rgtBuf[64], igtBuf[64];

				LS_FormatLssDate( r->startTime, startBuf, sizeof( startBuf ) );
				LS_FormatLssDate( r->endTime, endBuf, sizeof( endBuf ) );
				LS_FormatLssTime( r->totalRGTMs, rgtBuf, sizeof( rgtBuf ) );
				LS_FormatLssTime( r->totalIGTMs, igtBuf, sizeof( igtBuf ) );

				Com_sprintf( buf, sizeof( buf ),
					"    <Attempt id=\"%d\" started=\"%s\" isStartedSynced=\"%s\" ended=\"%s\" isEndedSynced=\"%s\">\n",
					r->attemptId, startBuf,
					r->isStartedSynced ? "True" : "False",
					endBuf,
					r->isEndedSynced ? "True" : "False" );
				LS_WriteStr( f, buf );

				Com_sprintf( buf, sizeof( buf ), "      <RealTime>%s</RealTime>\n", rgtBuf );
				LS_WriteStr( f, buf );
				Com_sprintf( buf, sizeof( buf ), "      <GameTime>%s</GameTime>\n", igtBuf );
				LS_WriteStr( f, buf );

				LS_WriteStr( f, "    </Attempt>\n" );
				compIdx++;
			} else {
				/* Write reset attempt */
				lsResetAttempt_t *ra = &ls.resetAttempts[filteredResets[resetIdx]];
				char startBuf[32], endBuf[32];

				LS_FormatLssDate( ra->startTime, startBuf, sizeof( startBuf ) );
				LS_FormatLssDate( ra->endTime, endBuf, sizeof( endBuf ) );

				if ( ra->pauseTimeMs > 0 ) {
					/* PauseTime-only attempt (non-self-closing) */
					char ptBuf[64];
					LS_FormatLssTime( ra->pauseTimeMs, ptBuf, sizeof( ptBuf ) );
					Com_sprintf( buf, sizeof( buf ),
						"    <Attempt id=\"%d\" started=\"%s\" isStartedSynced=\"%s\" ended=\"%s\" isEndedSynced=\"%s\">\n",
						ra->attemptId, startBuf,
						ra->isStartedSynced ? "True" : "False",
						endBuf,
						ra->isEndedSynced ? "True" : "False" );
					LS_WriteStr( f, buf );
					Com_sprintf( buf, sizeof( buf ), "      <PauseTime>%s</PauseTime>\n", ptBuf );
					LS_WriteStr( f, buf );
					LS_WriteStr( f, "    </Attempt>\n" );
				} else {
					/* Normal self-closing reset */
					Com_sprintf( buf, sizeof( buf ),
						"    <Attempt id=\"%d\" started=\"%s\" isStartedSynced=\"%s\" ended=\"%s\" isEndedSynced=\"%s\" />\n",
						ra->attemptId, startBuf,
						ra->isStartedSynced ? "True" : "False",
						endBuf,
						ra->isEndedSynced ? "True" : "False" );
					LS_WriteStr( f, buf );
				}
				resetIdx++;
			}
		}

		if ( filteredResets ) Z_Free( filteredResets );
	}
	LS_WriteStr( f, "  </AttemptHistory>\n" );

	/* Segments */
	LS_WriteStr( f, "  <Segments>\n" );
	{
		int segN = 0; /* sequential segment index within this category */
		int cumulativePB = 0;
		int cumulativeRealPB = 0;
		qboolean pbValid = qtrue;

		for ( i = firstIdx; i <= lastIdx; i++ ) {
			int j;
			const char *segName;
			if ( ls.splits[i].cutscene ) continue;

			segName = ls.splits[i].displayName ? ls.splits[i].displayName : ls.splits[i].mapname;

			LS_WriteStr( f, "    <Segment>\n" );
			Com_sprintf( buf, sizeof( buf ), "      <Name>%s</Name>\n", segName );
			LS_WriteStr( f, buf );
			LS_WriteStr( f, "      <Icon />\n" );

			/* SplitTimes - cumulative PB */
			LS_WriteStr( f, "      <SplitTimes>\n" );
			LS_WriteStr( f, "        <SplitTime name=\"Personal Best\">\n" );
			if ( pbValid && ls.splits[i].d[slot].pbSegmentMs > 0 ) {
				cumulativePB += ls.splits[i].d[slot].pbSegmentMs;
				if ( ls.splits[i].d[slot].pbRealSegmentMs > 0 ) {
					cumulativeRealPB += ls.splits[i].d[slot].pbRealSegmentMs;
					LS_FormatLssTime( cumulativeRealPB, timeBuf, sizeof( timeBuf ) );
					Com_sprintf( buf, sizeof( buf ), "          <RealTime>%s</RealTime>\n", timeBuf );
					LS_WriteStr( f, buf );
				}
				LS_FormatLssTime( cumulativePB, timeBuf, sizeof( timeBuf ) );
				Com_sprintf( buf, sizeof( buf ), "          <GameTime>%s</GameTime>\n", timeBuf );
				LS_WriteStr( f, buf );
			} else {
				pbValid = qfalse; /* once we hit a gap, remaining PB splits are invalid */
			}
			LS_WriteStr( f, "        </SplitTime>\n" );
			LS_WriteStr( f, "      </SplitTimes>\n" );

			/* BestSegmentTime (gold) */
			LS_WriteStr( f, "      <BestSegmentTime>\n" );
			if ( ls.splits[i].d[slot].bestRealTimeMs > 0 ) {
				LS_FormatLssTime( ls.splits[i].d[slot].bestRealTimeMs, timeBuf, sizeof( timeBuf ) );
				Com_sprintf( buf, sizeof( buf ), "        <RealTime>%s</RealTime>\n", timeBuf );
				LS_WriteStr( f, buf );
			}
			if ( ls.splits[i].d[slot].bestTimeMs > 0 ) {
				LS_FormatLssTime( ls.splits[i].d[slot].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				Com_sprintf( buf, sizeof( buf ), "        <GameTime>%s</GameTime>\n", timeBuf );
				LS_WriteStr( f, buf );
			}
			LS_WriteStr( f, "      </BestSegmentTime>\n" );

			/* SegmentHistory - per-attempt times for this segment */
			LS_WriteStr( f, "      <SegmentHistory>\n" );
			for ( j = 0; j < numFilteredRuns; j++ ) {
				lsRunHistory_t *r = &ls.history[filteredIndices[j]];

				if ( segN < r->numSplits && ( r->splitTimes[segN] > 0 || r->splitRealTimes[segN] > 0 ) ) {
					Com_sprintf( buf, sizeof( buf ), "        <Time id=\"%d\">\n", r->attemptId );
					LS_WriteStr( f, buf );
					if ( r->splitRealTimes[segN] > 0 ) {
						LS_FormatLssTime( r->splitRealTimes[segN], timeBuf, sizeof( timeBuf ) );
						Com_sprintf( buf, sizeof( buf ), "          <RealTime>%s</RealTime>\n", timeBuf );
						LS_WriteStr( f, buf );
					}
					if ( r->splitTimes[segN] > 0 ) {
						LS_FormatLssTime( r->splitTimes[segN], timeBuf, sizeof( timeBuf ) );
						Com_sprintf( buf, sizeof( buf ), "          <GameTime>%s</GameTime>\n", timeBuf );
						LS_WriteStr( f, buf );
					}
					LS_WriteStr( f, "        </Time>\n" );
				}
			}
			/* Also emit orphan segment times (from partial runs / resets) */
			{
				int oi;
				for ( oi = 0; oi < ls.numOrphanSegTimes; oi++ ) {
					lsOrphanSegTime_t *o = &ls.orphanSegTimes[oi];
					if ( o->mode != mode ) continue;
					if ( o->diffIdx != diffIdx ) continue;
					if ( o->categoryVariant != variant ) continue;
					if ( mode == LS_MODE_MISSION && o->missionNum != missionGroup + 1 ) continue;
					if ( mode == LS_MODE_IL && o->mapIdx != mapIdx ) continue;
					if ( o->segIdx != segN ) continue;
					Com_sprintf( buf, sizeof( buf ), "        <Time id=\"%d\">\n", o->attemptId );
					LS_WriteStr( f, buf );
					if ( o->realTimeMs > 0 ) {
						LS_FormatLssTime( o->realTimeMs, timeBuf, sizeof( timeBuf ) );
						Com_sprintf( buf, sizeof( buf ), "          <RealTime>%s</RealTime>\n", timeBuf );
						LS_WriteStr( f, buf );
					}
					if ( o->timeMs > 0 ) {
						LS_FormatLssTime( o->timeMs, timeBuf, sizeof( timeBuf ) );
						Com_sprintf( buf, sizeof( buf ), "          <GameTime>%s</GameTime>\n", timeBuf );
						LS_WriteStr( f, buf );
					}
					LS_WriteStr( f, "        </Time>\n" );
				}
			}
			LS_WriteStr( f, "      </SegmentHistory>\n" );

			LS_WriteStr( f, "    </Segment>\n" );
			segN++;
		}
	}
	LS_WriteStr( f, "  </Segments>\n" );

	/* AutoSplitterSettings (empty but valid) */
	LS_WriteStr( f, "  <AutoSplitterSettings />\n" );
	LS_WriteStr( f, "</Run>\n" );

	/* Flush buffered output to disk in one write */
	LS_WBufFlush( f );
	FS_FCloseFile( f );
	Z_Free( filteredIndices );
}

static void LS_WriteLss( int mode, int diffIdx, int missionGroup, int mapIdx, qboolean pct100, qboolean hl1 ) {
	LS_WriteLssEx( mode, diffIdx, missionGroup, mapIdx, pct100, hl1, qfalse );
}

/* =====================================================================
   LS_ReadLss - read a single .lss file and populate in-memory state
   ===================================================================== */

/* Simple XML content extractor: find <tag>content</tag> starting from pos.
   Returns content in out, returns pointer past </tag> or NULL if not found.
   If boundary is non-NULL, the search will not look past that pointer. */
static const char *LS_XmlTagContentBounded( const char *pos, const char *boundary, const char *tag, char *out, int outSize ) {
	char openTag[64], closeTag[64];
	const char *start, *end;

	Com_sprintf( openTag, sizeof( openTag ), "<%s>", tag );
	Com_sprintf( closeTag, sizeof( closeTag ), "</%s>", tag );

	start = strstr( pos, openTag );
	if ( !start || ( boundary && start >= boundary ) ) { out[0] = '\0'; return NULL; }
	start += strlen( openTag );

	end = strstr( start, closeTag );
	if ( !end || ( boundary && end >= boundary ) ) { out[0] = '\0'; return NULL; }

	{
		int len = (int)( end - start );
		if ( len >= outSize ) len = outSize - 1;
		memcpy( out, start, len );
		out[len] = '\0';
	}
	return end + strlen( closeTag );
}

/* Unbounded version for backward compatibility */
static const char *LS_XmlTagContent( const char *pos, const char *tag, char *out, int outSize ) {
	return LS_XmlTagContentBounded( pos, NULL, tag, out, outSize );
}

/* Extract an attribute value from a tag string like: id="42" started="..." */
static qboolean LS_XmlAttr( const char *tagStr, const char *attr, char *out, int outSize ) {
	char search[64];
	const char *pos, *end;
	Com_sprintf( search, sizeof( search ), "%s=\"", attr );
	pos = strstr( tagStr, search );
	if ( !pos ) { out[0] = '\0'; return qfalse; }
	pos += strlen( search );
	end = strchr( pos, '"' );
	if ( !end ) { out[0] = '\0'; return qfalse; }
	{
		int len = (int)( end - pos );
		if ( len >= outSize ) len = outSize - 1;
		memcpy( out, pos, len );
		out[len] = '\0';
	}
	return qtrue;
}

static void LS_ReadLss( int mode, int diffIdx, int missionGroup, int mapIdx, qboolean pct100, qboolean hl1 ) {
	fileHandle_t fh;
	int fileLen;
	char *bigbuf;
	char path[256], valBuf[256];
	int slot, firstIdx, lastIdx, variant;
	const char *pos, *segPos, *attemptEnd;

	LS_GetLssPath( path, sizeof( path ), mode, diffIdx, missionGroup, mapIdx, pct100, hl1 );
	variant = LS_CategoryVariant( pct100, hl1 );
	slot = LS_DiffSlotFor( mode, diffIdx, pct100, hl1 );
	LS_GetSegmentRange( mode, missionGroup, mapIdx, &firstIdx, &lastIdx );
	if ( firstIdx < 0 || lastIdx < 0 ) return;

	fileLen = FS_FOpenFileByMode( path, &fh, FS_READ );
	if ( fileLen <= 0 ) {
		if ( fh ) FS_FCloseFile( fh );
		return;
	}

	bigbuf = (char *)Z_Malloc( fileLen + 1 );
	FS_Read( bigbuf, fileLen, fh );
	bigbuf[fileLen] = '\0';
	FS_FCloseFile( fh );

	/* GameIcon - preserve base64 content for round-trip.
	   Only store the first one we encounter (all categories share the icon). */
	if ( !ls.gameIcon ) {
		const char *iconStart = strstr( bigbuf, "<GameIcon>" );
		const char *iconEnd   = strstr( bigbuf, "</GameIcon>" );
		if ( iconStart && iconEnd && iconEnd > iconStart ) {
			int iconLen;
			iconStart += 10; /* skip "<GameIcon>" */
			iconLen = (int)( iconEnd - iconStart );
			if ( iconLen > 0 ) {
				ls.gameIcon = (char *)Z_Malloc( iconLen + 1 );
				memcpy( ls.gameIcon, iconStart, iconLen );
				ls.gameIcon[iconLen] = '\0';
			}
		}
	}

	/* AttemptCount */
	if ( LS_XmlTagContent( bigbuf, "AttemptCount", valBuf, sizeof( valBuf ) ) ) {
		int att = atoi( valBuf );
		switch ( mode ) {
		case LS_MODE_FULLGAME:
			ls.fgAttempts[slot] = att;
			break;
		case LS_MODE_MISSION:
			ls.msAttempts[missionGroup][slot] = att;
			break;
		case LS_MODE_IL:
			ls.splits[mapIdx].d[slot].totalAttempts = att;
			break;
		}
	}

	/* AttemptHistory - read completed runs into history, preserve reset attempts */
	{
		int completions = 0;
		int bestIGT = 0, bestRGT = 0;

		pos = strstr( bigbuf, "<AttemptHistory>" );
		attemptEnd = strstr( bigbuf, "</AttemptHistory>" );
		if ( pos && attemptEnd ) {
			pos += 16; /* skip "<AttemptHistory>" */
			while ( pos < attemptEnd ) {
				const char *attStart = strstr( pos, "<Attempt " );
				const char *attClose;
				const char *tagEnd; /* end of the opening <Attempt ...> or <Attempt .../> tag */
				char idStr[32], igtStr[64], rgtStr[64], startStr[64], endStr[64];
				char syncStr[16];
				int attId, igtMs, rgtMs;
				qboolean isStartedSynced, isEndedSynced;

				if ( !attStart || attStart >= attemptEnd ) break;

				/* Parse attempt id */
				idStr[0] = '\0';
				LS_XmlAttr( attStart, "id", idStr, sizeof( idStr ) );
				attId = atoi( idStr );

				/* Parse isStartedSynced / isEndedSynced */
				syncStr[0] = '\0';
				LS_XmlAttr( attStart, "isStartedSynced", syncStr, sizeof( syncStr ) );
				isStartedSynced = ( Q_stricmp( syncStr, "True" ) == 0 ) ? qtrue : qfalse;
				syncStr[0] = '\0';
				LS_XmlAttr( attStart, "isEndedSynced", syncStr, sizeof( syncStr ) );
				isEndedSynced = ( Q_stricmp( syncStr, "True" ) == 0 ) ? qtrue : qfalse;

				/* Find end of the opening tag (first '>') to detect self-closing.
				   <Attempt ... />  > self-closing (tagEnd[-1] == '/')
				   <Attempt ... >   > has children, need </Attempt> */
				tagEnd = strchr( attStart + 9, '>' ); /* 9 = strlen("<Attempt ") */
				if ( !tagEnd || tagEnd >= attemptEnd ) break;

				if ( tagEnd > attStart && *( tagEnd - 1 ) == '/' ) {
					/* Self-closing attempt (no completion data) - store as reset */

					/* Preserve this reset attempt for round-trip */
					if ( !LS_HasAttemptId( mode, diffIdx, ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0, ( mode == LS_MODE_IL ) ? mapIdx : -1, variant, attId ) ) {
						LS_ResetAttemptsEnsure( ls.numResetAttempts + 1 );
						{
							lsResetAttempt_t *ra = &ls.resetAttempts[ls.numResetAttempts];
							ra->attemptId  = attId;
							ra->difficulty = diffIdx + 1;
							ra->mode       = mode;
							ra->categoryVariant = variant;
							ra->missionNum = ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0;
							ra->mapIdx     = ( mode == LS_MODE_IL ) ? mapIdx : -1;
							startStr[0] = '\0'; endStr[0] = '\0';
							LS_XmlAttr( attStart, "started", startStr, sizeof( startStr ) );
							LS_XmlAttr( attStart, "ended", endStr, sizeof( endStr ) );
							ra->startTime = LS_ParseLssDate( startStr );
							ra->endTime   = LS_ParseLssDate( endStr );
							ra->isStartedSynced = isStartedSynced;
							ra->isEndedSynced   = isEndedSynced;
							ra->pauseTimeMs     = 0;
							ls.numResetAttempts++;
						}
					}

					pos = tagEnd + 1;
					continue;
				}

				/* Non-self-closing: find </Attempt> */
				attClose = strstr( tagEnd, "</Attempt>" );
				if ( !attClose || attClose >= attemptEnd ) break;

				/* This attempt has child elements - check for GameTime/RealTime
				   (bounded search to stay within this Attempt block) */
				igtStr[0] = '\0'; rgtStr[0] = '\0';
				LS_XmlTagContentBounded( attStart, attClose, "GameTime", igtStr, sizeof( igtStr ) );
				LS_XmlTagContentBounded( attStart, attClose, "RealTime", rgtStr, sizeof( rgtStr ) );

				igtMs = LS_ParseLssTime( igtStr );
				rgtMs = LS_ParseLssTime( rgtStr );

				if ( igtMs > 0 || rgtMs > 0 ) {
					/* Completed run - add to history */
					completions++;

					/* Track PB */
					if ( igtMs > 0 && ( bestIGT == 0 || igtMs < bestIGT ) )
						bestIGT = igtMs;
					if ( rgtMs > 0 && ( bestRGT == 0 || rgtMs < bestRGT ) )
						bestRGT = rgtMs;

					/* Add to history array (dynamic, no fixed limit) */
					if ( !LS_HasAttemptId( mode, diffIdx, ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0, ( mode == LS_MODE_IL ) ? mapIdx : -1, variant, attId ) ) {
						LS_HistoryEnsure( ls.numHistoryRuns + 1 );
						{
						lsRunHistory_t *r = &ls.history[ls.numHistoryRuns];
						memset( r, 0, sizeof( *r ) );
						r->attemptId  = attId;
						r->difficulty = diffIdx + 1;
						r->mode       = mode;
						r->categoryVariant = variant;
						r->missionNum = ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0;
						r->mapIdx     = ( mode == LS_MODE_IL ) ? mapIdx : -1;
						r->totalIGTMs = igtMs;
						r->totalRGTMs = rgtMs;

						/* Parse dates */
						startStr[0] = '\0'; endStr[0] = '\0';
						LS_XmlAttr( attStart, "started", startStr, sizeof( startStr ) );
						LS_XmlAttr( attStart, "ended", endStr, sizeof( endStr ) );
						r->startTime = LS_ParseLssDate( startStr );
						r->endTime   = LS_ParseLssDate( endStr );
						r->isStartedSynced = isStartedSynced;
						r->isEndedSynced   = isEndedSynced;

						/* Split times will be filled from SegmentHistory below.
						   For now, set numSplits to 0; we'll fix it after. */
						r->numSplits = 0;
						ls.numHistoryRuns++;
						}
					}
				} else {
					/* Non-self-closing attempt with no RT/GT.
					   Check for <PauseTime> (paused but not completed runs). */
					char ptStr[64];
					int ptMs;
					ptStr[0] = '\0';
					LS_XmlTagContentBounded( attStart, attClose, "PauseTime", ptStr, sizeof( ptStr ) );
					ptMs = LS_ParseLssTime( ptStr );

					if ( !LS_HasAttemptId( mode, diffIdx, ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0, ( mode == LS_MODE_IL ) ? mapIdx : -1, variant, attId ) ) {
						LS_ResetAttemptsEnsure( ls.numResetAttempts + 1 );
						{
						lsResetAttempt_t *ra = &ls.resetAttempts[ls.numResetAttempts];
						ra->attemptId  = attId;
						ra->difficulty = diffIdx + 1;
						ra->mode       = mode;
						ra->categoryVariant = variant;
						ra->missionNum = ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0;
						ra->mapIdx     = ( mode == LS_MODE_IL ) ? mapIdx : -1;
						startStr[0] = '\0'; endStr[0] = '\0';
						LS_XmlAttr( attStart, "started", startStr, sizeof( startStr ) );
						LS_XmlAttr( attStart, "ended", endStr, sizeof( endStr ) );
						ra->startTime = LS_ParseLssDate( startStr );
						ra->endTime   = LS_ParseLssDate( endStr );
						ra->isStartedSynced = isStartedSynced;
						ra->isEndedSynced   = isEndedSynced;
						ra->pauseTimeMs     = ptMs > 0 ? ptMs : 0;
						ls.numResetAttempts++;
						}
					}
				}

				pos = attClose + 10; /* skip "</Attempt>" */
			}
		}

		/* Store completions and PB */
		switch ( mode ) {
		case LS_MODE_FULLGAME:
			ls.fgCompletions[slot] = completions;
			ls.fgPB[slot]    = bestIGT;
			ls.fgPBRgt[slot] = bestRGT;
			break;
		case LS_MODE_MISSION:
			ls.msCompletions[missionGroup][slot] = completions;
			ls.msPB[missionGroup][slot]    = bestIGT;
			ls.msPBRgt[missionGroup][slot] = bestRGT;
			break;
		case LS_MODE_IL:
			ls.splits[mapIdx].d[slot].totalCompletions = completions;
			if ( bestIGT > 0 ) ls.splits[mapIdx].d[slot].bestTimeMs = bestIGT;
			break;
		}
	}

	/* Segments - read BestSegmentTime, SplitTimes PB, and SegmentHistory */
	segPos = strstr( bigbuf, "<Segments>" );
	if ( segPos ) {
		const char *segsEnd = strstr( bigbuf, "</Segments>" );
		int segN = 0;
		int prevCumulativePB = 0;
		int prevCumulativeRealPB = 0;
		/* Build a fast attemptId -> history index lookup table to avoid O(n^2)
		   linear searches in SegmentHistory parsing. */
		int *idLookup = NULL;     /* idLookup[attemptId] = histIdx + 1 (0 = not found) */
		int  idLookupSize = 0;

		/* Build lookup over every already-loaded history entry for this category.
		   If this .lss is read more than once, duplicate attempts are skipped but
		   SegmentHistory still maps to the existing attempt instead of becoming
		   orphan history. */
		{
			int k, maxId = 0;
			for ( k = 0; k < ls.numHistoryRuns; k++ ) {
				if ( ls.history[k].mode != mode ) continue;
				if ( ls.history[k].categoryVariant != variant ) continue;
				if ( LS_DiffIdx( ls.history[k].difficulty ) != diffIdx ) continue;
				if ( mode == LS_MODE_MISSION && ls.history[k].missionNum != missionGroup + 1 ) continue;
				if ( mode == LS_MODE_IL && ls.history[k].mapIdx != mapIdx ) continue;
				if ( ls.history[k].attemptId > maxId )
					maxId = ls.history[k].attemptId;
			}
			if ( maxId > 0 && maxId < 1000000 ) {  /* sanity cap */
				idLookupSize = maxId + 1;
				idLookup = (int *)Z_Malloc( idLookupSize * sizeof( int ) );
				memset( idLookup, 0, idLookupSize * sizeof( int ) );
				for ( k = 0; k < ls.numHistoryRuns; k++ ) {
					int aid = ls.history[k].attemptId;
					if ( ls.history[k].mode != mode ) continue;
					if ( ls.history[k].categoryVariant != variant ) continue;
					if ( LS_DiffIdx( ls.history[k].difficulty ) != diffIdx ) continue;
					if ( mode == LS_MODE_MISSION && ls.history[k].missionNum != missionGroup + 1 ) continue;
					if ( mode == LS_MODE_IL && ls.history[k].mapIdx != mapIdx ) continue;
					if ( aid >= 0 && aid < idLookupSize ) {
						idLookup[aid] = k + 1;  /* +1 so 0 means "not found" */
					}
				}
			}
		}

		pos = segPos + 10; /* skip "<Segments>" */
		while ( pos && segsEnd && pos < segsEnd ) {
			const char *segStart = strstr( pos, "<Segment>" );
			const char *segEnd;
			int splitIdx;

			if ( !segStart || segStart >= segsEnd ) break;
			segEnd = strstr( segStart, "</Segment>" );
			if ( !segEnd ) break;

			/* Find the map index for this segment */
			splitIdx = -1;
			{
				int si;
				int n = 0;
				for ( si = firstIdx; si <= lastIdx; si++ ) {
					if ( ls.splits[si].cutscene ) continue;
					if ( n == segN ) { splitIdx = si; break; }
					n++;
				}
			}

			if ( splitIdx >= 0 ) {
				/* BestSegmentTime */
				{
					const char *bstStart = strstr( segStart, "<BestSegmentTime>" );
					if ( bstStart && bstStart < segEnd ) {
						char gtBuf[64];
						if ( LS_XmlTagContent( bstStart, "GameTime", gtBuf, sizeof( gtBuf ) ) ) {
							int ms = LS_ParseLssTime( gtBuf );
							if ( ms > 0 ) ls.splits[splitIdx].d[slot].bestTimeMs = ms;
						}
						if ( LS_XmlTagContent( bstStart, "RealTime", gtBuf, sizeof( gtBuf ) ) ) {
							int ms = LS_ParseLssTime( gtBuf );
							if ( ms > 0 ) ls.splits[splitIdx].d[slot].bestRealTimeMs = ms;
						}
					}
				}

				/* SplitTimes "Personal Best" - cumulative, de-cumulate for pbSegmentMs */
				{
					const char *stStart = strstr( segStart, "<SplitTime name=\"Personal Best\">" );
					if ( stStart && stStart < segEnd ) {
						char gtBuf[64];
						if ( LS_XmlTagContent( stStart, "GameTime", gtBuf, sizeof( gtBuf ) ) ) {
							int cumulMs = LS_ParseLssTime( gtBuf );
							int segMs = cumulMs - prevCumulativePB;
							if ( segMs > 0 ) ls.splits[splitIdx].d[slot].pbSegmentMs = segMs;
							prevCumulativePB = cumulMs;
						}
						if ( LS_XmlTagContent( stStart, "RealTime", gtBuf, sizeof( gtBuf ) ) ) {
							int cumulMs = LS_ParseLssTime( gtBuf );
							int segMs = cumulMs - prevCumulativeRealPB;
							if ( segMs > 0 ) ls.splits[splitIdx].d[slot].pbRealSegmentMs = segMs;
							prevCumulativeRealPB = cumulMs;
						}
					}
				}

				/* SegmentHistory - populate split times in matching history entries */
				{
					const char *shStart = strstr( segStart, "<SegmentHistory>" );
					const char *shEnd   = strstr( segStart, "</SegmentHistory>" );
					if ( shStart && shEnd && shStart < segEnd ) {
						const char *tp = shStart;

						while ( tp < shEnd ) {
							const char *timeStart = strstr( tp, "<Time " );
							const char *timeEnd;
							char idBuf[32], gtBuf[64];
							int timeId, histIdx;
							int gtMs = 0, rtMs = 0;
							qboolean hasGT = qfalse, hasRT = qfalse;

							if ( !timeStart || timeStart >= shEnd ) break;
							timeEnd = strstr( timeStart, "</Time>" );
							if ( !timeEnd || timeEnd >= shEnd ) break;

							LS_XmlAttr( timeStart, "id", idBuf, sizeof( idBuf ) );
							timeId = atoi( idBuf );

							if ( LS_XmlTagContentBounded( timeStart, timeEnd, "GameTime", gtBuf, sizeof( gtBuf ) ) ) {
								gtMs = LS_ParseLssTime( gtBuf );
								hasGT = qtrue;
							}
							if ( LS_XmlTagContentBounded( timeStart, timeEnd, "RealTime", gtBuf, sizeof( gtBuf ) ) ) {
								rtMs = LS_ParseLssTime( gtBuf );
								hasRT = qtrue;
							}

							/* Find history entry whose attemptId matches the
							   SegmentHistory Time id (which is the Attempt id
							   from AttemptHistory, NOT a sequential index).
							   Uses O(1) lookup table when available, falls back
							   to linear scan for very large attemptIds. */
							histIdx = -1;
							if ( idLookup && timeId >= 0 && timeId < idLookupSize ) {
								int v = idLookup[timeId];
								if ( v > 0 ) histIdx = v - 1;
							} else {
								int k;
								for ( k = 0; k < ls.numHistoryRuns; k++ ) {
									if ( ls.history[k].mode != mode ) continue;
									if ( ls.history[k].categoryVariant != variant ) continue;
									if ( LS_DiffIdx( ls.history[k].difficulty ) != diffIdx ) continue;
									if ( mode == LS_MODE_MISSION && ls.history[k].missionNum != missionGroup + 1 ) continue;
									if ( mode == LS_MODE_IL && ls.history[k].mapIdx != mapIdx ) continue;
									if ( ls.history[k].attemptId == timeId ) {
										histIdx = k;
										break;
									}
								}
							}

							if ( histIdx >= 0 ) {
								lsRunHistory_t *r = &ls.history[histIdx];
								if ( segN < LS_MAX_MAPS ) {
									if ( hasGT ) r->splitTimes[segN] = gtMs;
									if ( hasRT ) r->splitRealTimes[segN] = rtMs;
									if ( segN >= r->numSplits ) r->numSplits = segN + 1;
								}
							} else {
								/* Orphan: id matches a reset attempt (partial run).
								   Store for round-trip preservation. */
								if ( ( hasGT && gtMs > 0 ) || ( hasRT && rtMs > 0 ) ) {
									if ( LS_HasOrphanSegTime( mode, diffIdx, ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0, ( mode == LS_MODE_IL ) ? mapIdx : -1, variant, timeId, segN ) ) {
										tp = timeEnd + 7;
										continue;
									}
									LS_OrphanSegTimesEnsure( ls.numOrphanSegTimes + 1 );
									{
										lsOrphanSegTime_t *o = &ls.orphanSegTimes[ls.numOrphanSegTimes];
										o->attemptId  = timeId;
										o->segIdx     = segN;
										o->timeMs     = hasGT ? gtMs : 0;
										o->realTimeMs = hasRT ? rtMs : 0;
										o->mode       = mode;
										o->diffIdx    = diffIdx;
										o->categoryVariant = variant;
										o->missionNum = ( mode == LS_MODE_MISSION ) ? missionGroup + 1 : 0;
										o->mapIdx     = ( mode == LS_MODE_IL ) ? mapIdx : -1;
										ls.numOrphanSegTimes++;
									}
								}
							}
							tp = timeEnd + 7;
						}
					}
				}
			}

			segN++;
			pos = segEnd + 10; /* skip "</Segment>" */
		}

		if ( idLookup ) {
			Z_Free( idLookup );
		}
	}

	Z_Free( bigbuf );
}

/* =====================================================================
   LS_SaveState / LS_LoadState - current run transient state
   ===================================================================== */

static void LS_SaveState( void ) {
	fileHandle_t f;
	int i;
	char buf[512];
	int rgtElapsed;

	FS_FOpenFileByMode( LS_STATE_FILE, &f, FS_WRITE );
	if ( !f ) return;

	LS_WBufInit();

	if ( ls.active && !ls.runFinished ) {
		rgtElapsed = Sys_Milliseconds() - ls.runStartRealMs;
	} else {
		rgtElapsed = ls.runSavedRealMs;
	}

	LS_WriteStr( f, "LSSTATE_V2\n" );

	Com_sprintf( buf, sizeof( buf ), "RUN %d %d %d %d %d %d %d %d\n",
		(int)ls.active, (int)ls.runFinished,
		ls.runTotalIGTMs, rgtElapsed,
		ls.currentMapIndex,
		ls.currentDifficulty, ls.runMode, ls.runMission );
	LS_WriteStr( f, buf );

	Com_sprintf( buf, sizeof( buf ), "CHEATS %d %d %d\n",
		ls.totalPauses, ls.totalUndos, ls.totalSkips );
	LS_WriteStr( f, buf );

	for ( i = 0; i < ls.numMaps; i++ ) {
		Com_sprintf( buf, sizeof( buf ), "CMAP %s %d %d %d %d\n",
			ls.splits[i].mapname,
			ls.splits[i].currentTimeMs,
			(int)ls.splits[i].splitDone,
			(int)ls.splits[i].splitSkipped,
			ls.splits[i].prevGoldMs );
		LS_WriteStr( f, buf );
	}

	LS_WBufFlush( f );
	FS_FCloseFile( f );
}

static void LS_LoadState( void ) {
	fileHandle_t fh;
	int fileLen;
	int stateVersion = 1;
	char *bigbuf, *text, *token;

	fileLen = FS_FOpenFileByMode( LS_STATE_FILE, &fh, FS_READ );
	if ( fileLen <= 0 ) {
		if ( fh ) FS_FCloseFile( fh );
		return;
	}

	bigbuf = (char *)Z_Malloc( fileLen + 1 );
	FS_Read( bigbuf, fileLen, fh );
	bigbuf[fileLen] = '\0';
	FS_FCloseFile( fh );

	text = bigbuf;
	token = COM_Parse( &text );

	if ( !Q_stricmp( token, "LSSTATE_V2" ) ) {
		stateVersion = 2;
	} else if ( Q_stricmp( token, "LSSTATE_V1" ) ) {
		Z_Free( bigbuf );
		return;
	}

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
			if ( loadedIdx >= ls.numMaps ) loadedIdx = ls.numMaps - 1;
			if ( loadedIdx < -1 ) loadedIdx = -1;
			ls.currentMapIndex = loadedIdx;
			ls.runSavedRealMs = savedRgt;
			if ( ls.active && !ls.runFinished )
				ls.runStartRealMs = Sys_Milliseconds() - savedRgt;
		} else if ( !Q_stricmp( token, "CHEATS" ) ) {
			token = COM_Parse( &text ); ls.totalPauses = atoi( token );
			token = COM_Parse( &text ); ls.totalUndos  = atoi( token );
			token = COM_Parse( &text ); ls.totalSkips  = atoi( token );
		} else if ( !Q_stricmp( token, "CMAP" ) ) {
			int idx;
			token = COM_Parse( &text );
			idx = LS_FindMapIndex( token );
			if ( idx >= 0 ) {
				token = COM_Parse( &text ); ls.splits[idx].currentTimeMs = atoi( token );
				token = COM_Parse( &text ); ls.splits[idx].splitDone     = (qboolean)atoi( token );
				if ( stateVersion >= 2 ) {
					token = COM_Parse( &text ); ls.splits[idx].splitSkipped = (qboolean)atoi( token );
					token = COM_Parse( &text ); ls.splits[idx].prevGoldMs   = atoi( token );
				}
			} else {
				COM_Parse( &text ); COM_Parse( &text );
				if ( stateVersion >= 2 ) { COM_Parse( &text ); COM_Parse( &text ); }
			}
		}
	}

	LS_UpdateCurVisRow();
	Z_Free( bigbuf );
}

/* =====================================================================
   LS_Save / LS_Load - main save/load entry points
   ===================================================================== */

/* Save current category's .lss file + state */
static void LS_Save( void ) {
	int di = LS_DiffIdx( ls.currentDifficulty );
	qboolean p100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? qtrue : qfalse;
	qboolean hl1 = LS_HL1ModeActive();

	/* Save the .lss for the current category */
	switch ( ls.runMode ) {
	case LS_MODE_FULLGAME:
		LS_WriteLss( LS_MODE_FULLGAME, di, -1, -1, p100, hl1 );
		break;
	case LS_MODE_MISSION:
		LS_WriteLss( LS_MODE_MISSION, di, ls.runMission - 1, -1, p100, hl1 );
		break;
	case LS_MODE_IL:
		LS_WriteLss( LS_MODE_IL, di, -1, ls.modeFirstIdx, p100, hl1 );
		break;
	}

	/* Always save transient state */
	LS_SaveState();
}

/* Save ALL categories that have data (for reset-bests, shutdown, etc.) */
static void LS_SaveAll( void ) {
	int di, gi, mi, variant;
	qboolean p100, hl1;

	for ( variant = 0; variant < LS_NUM_CATEGORY_VARIANTS; variant++ ) {
		p100 = ( variant & 1 ) ? qtrue : qfalse;
		hl1 = ( variant & 2 ) ? qtrue : qfalse;

		/* Fullgame: 3 difficulties */
		for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
			LS_WriteLss( LS_MODE_FULLGAME, di, -1, -1, p100, hl1 );
		}

		/* Mission: 5 groups x 3 difficulties */
		for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
			for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
				LS_WriteLss( LS_MODE_MISSION, di, gi, -1, p100, hl1 );
			}
		}

		/* IL: each non-cutscene map x 3 difficulties */
		for ( mi = 0; mi < ls.numMaps; mi++ ) {
			if ( ls.splits[mi].cutscene ) continue;
			for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
				LS_WriteLss( LS_MODE_IL, di, -1, mi, p100, hl1 );
			}
		}
	}

	LS_SaveState();
}

/* LS_SaveHistory kept as wrapper for backward compatibility with callers */
static void LS_SaveHistory( void ) {
	/* History is now embedded in .lss files - LS_Save handles it */
}

/* =================================================================
   Lazy loading: only load .lss files for the active mode at startup;
   other modes are loaded on demand when the user switches to them.
   This avoids a 30+ second freeze when many large .lss files exist.
   ================================================================= */
static qboolean lssLoadedFg[LS_NUM_CATEGORY_VARIANTS];   /* fullgame files loaded */
static qboolean lssLoadedMs[LS_NUM_CATEGORY_VARIANTS];   /* mission files loaded */
static qboolean lssLoadedIl[LS_NUM_CATEGORY_VARIANTS];   /* IL files loaded */

/* Load .lss files for a specific mode (if not already loaded). */
static void LS_LoadModeVariant( int mode, qboolean p100, qboolean hl1 ) {
	int di, gi, mi;
	int variant = LS_CategoryVariant( p100, hl1 );

	switch ( mode ) {
	case LS_MODE_FULLGAME:
		if ( lssLoadedFg[variant] ) return;
		for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
			LS_ReadLss( LS_MODE_FULLGAME, di, -1, -1, p100, hl1 );
		}
		lssLoadedFg[variant] = qtrue;
		break;
	case LS_MODE_MISSION:
		if ( lssLoadedMs[variant] ) return;
		for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
			for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
				LS_ReadLss( LS_MODE_MISSION, di, gi, -1, p100, hl1 );
			}
		}
		lssLoadedMs[variant] = qtrue;
		break;
	case LS_MODE_IL:
		if ( lssLoadedIl[variant] ) return;
		for ( mi = 0; mi < ls.numMaps; mi++ ) {
			if ( ls.splits[mi].cutscene ) continue;
			for ( di = 0; di < LS_MAX_DIFFICULTIES; di++ ) {
				LS_ReadLss( LS_MODE_IL, di, -1, mi, p100, hl1 );
			}
		}
		lssLoadedIl[variant] = qtrue;
		break;
	}
}

static void LS_LoadMode( int mode ) {
	qboolean p100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? qtrue : qfalse;
	LS_LoadModeVariant( mode, p100, LS_HL1ModeActive() );
}

/* Reset lazy-loading flags (on 100% toggle or full reload). */
static void LS_ResetLoadedFlags( void ) {
	memset( lssLoadedFg, 0, sizeof( lssLoadedFg ) );
	memset( lssLoadedMs, 0, sizeof( lssLoadedMs ) );
	memset( lssLoadedIl, 0, sizeof( lssLoadedIl ) );
}

/* Load state + active mode's .lss files (lazy: others on demand). */
static void LS_Load( void ) {
	LS_ResetLoadedFlags();

	/* Load transient state first so we know the active mode */
	LS_LoadState();

	/* Only load the active mode's files; others load on demand */
	LS_LoadMode( ls.runMode );

	/* Restore transient per-run fields after .lss loading, because .lss files
	   can contain newly-written golds while an unfinished run is still pending
	   user confirmation.  prevGoldMs must survive restarts so reset can still
	   ask whether to keep/discard those golds and the in-run delta remains valid. */
	LS_LoadState();

	Com_Printf( "^2LiveSplit: .lss files loaded (%d history runs)\n", ls.numHistoryRuns );
}

/* LS_LoadHistory kept as no-op for backward compatibility */
static void LS_LoadHistory( void ) {
	/* History is now loaded from .lss files in LS_Load */
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

static void LS_ClearPreviewRunStateForCategoryChange( void ) {
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
	ls.backtrackPause  = qfalse;
	ls.backtrackTargetIndex = -1;
	ls.currentMapIndex = -1;
	ls.curVisRow       = -1;
	ls.baseSecretsFound  = 0;
	ls.baseTreasureFound = 0;
	for ( k = 0; k < ls.numMaps; k++ ) {
		ls.splits[k].currentTimeMs = 0;
		ls.splits[k].splitDone     = qfalse;
		ls.splits[k].splitSkipped  = qfalse;
		ls.splits[k].prevGoldMs    = 0;
		ls.splits[k].goldFlashMs   = 0;
		ls.splits[k].secretsFound  = 0;
		ls.splits[k].treasureFound = 0;
	}
}

static void LS_SyncCategoryCvarsForDisplay( void ) {
	int curMode, curMission, cur100, wantIdx;
	qboolean changed;

	if ( ls.active ) {
		ls.currentDifficulty = LS_DetectDifficulty();
		return;
	}

	curMode = ls_modeCvar ? ls_modeCvar->integer : LS_MODE_FULLGAME;
	if ( curMode < LS_MODE_FULLGAME || curMode > LS_MODE_IL ) curMode = LS_MODE_FULLGAME;
	curMission = ls_missionCvar ? ls_missionCvar->integer : 1;
	if ( curMission < 1 ) curMission = 1;
	if ( curMission > LS_NUM_MISSION_GROUPS ) curMission = LS_NUM_MISSION_GROUPS;
	cur100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? 1 : 0;
	changed = qfalse;

	if ( curMode != ls.runMode || ( curMode == LS_MODE_MISSION && curMission != ls.runMission ) ) changed = qtrue;
	if ( curMode == LS_MODE_IL && ls_mapCvar && ls_mapCvar->string[0] ) {
		wantIdx = LS_FindMapIndex( ls_mapCvar->string );
		if ( wantIdx >= 0 && !ls.splits[wantIdx].cutscene && ( ls.runMode != LS_MODE_IL || wantIdx != ls.modeFirstIdx ) ) {
			changed = qtrue;
		}
	}
	if ( cur100 != ls_prev100pct ) {
		ls_prev100pct = cur100;
		LS_ResetLoadedFlags();
		changed = qtrue;
	}

	ls.currentDifficulty = LS_DetectDifficulty();
	if ( !changed ) return;

	LS_ClearPreviewRunStateForCategoryChange();
	LS_SetupMode( curMode, curMission );
	LS_LoadMode( curMode );
	if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
		char tmp[LS_MAX_MAPNAME];
		LS_ExtractMapname( cl.mapname, tmp, sizeof( tmp ) );
		Q_strncpyz( ls.prevMapname, tmp, LS_MAX_MAPNAME );
	}
}

static void LS_BuildCategoryText( char *out, int outSize ) {
	char label[96];
	const char *diffTag;
	const char *name;
	int gi, idx;
	qboolean p100;

	if ( !out || outSize <= 0 ) return;
	label[0] = '\0';
	diffTag = ls_diffShortTags[LS_DiffIdx( ls.currentDifficulty )];
	p100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? qtrue : qfalse;

	switch ( ls.runMode ) {
	case LS_MODE_MISSION:
		gi = ls.runMission - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		Com_sprintf( label, sizeof( label ), "Chapter %d: %s", gi + 1, ls_missionGroups[gi].shortName );
		break;
	case LS_MODE_IL:
		idx = ls.modeFirstIdx;
		name = NULL;
		if ( ( idx < 0 || idx >= ls.numMaps || ls.splits[idx].cutscene ) && ls_mapCvar && ls_mapCvar->string[0] ) {
			idx = LS_FindMapIndex( ls_mapCvar->string );
		}
		if ( idx >= 0 && idx < ls.numMaps ) {
			name = ls.splits[idx].shortName && ls.splits[idx].shortName[0] ? ls.splits[idx].shortName : ls.splits[idx].displayName;
			if ( !name || !name[0] ) name = ls.splits[idx].mapname;
		}
		Com_sprintf( label, sizeof( label ), "IL %s", name && name[0] ? name : "Map" );
		break;
	default:
		Q_strncpyz( label, "Full Game", sizeof( label ) );
		break;
	}

	Com_sprintf( out, outSize, "%s%s%s [%s]", label, p100 ? " 100%" : "", LS_HL1ModeNameSuffix(), diffTag );
}

/* =====================================================================
   Init / Console commands
   ===================================================================== */

static void LS_BuildSplitTable( void ) {
	int i;
	LS_HistoryFree();  /* free dynamic history before zeroing struct */
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
	ls.backtrackPause = qfalse;
	ls.backtrackTargetIndex = -1;
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
	/* Stop immediately instead of queueing "stoprecord" behind a pending
	   map/cinematic transition.  Leaving demo close until after /spmap can
	   push extra work into CL_Shutdown/VM reload while memory is already
	   tight, which showed up as recursive Z_Malloc failures. */
	CL_StopRecord_f();
	ls_autoRecordActive = qfalse;
	Com_Printf( "^2LiveSplit: Auto-record stopped\n" );
}

static void LS_DoResetSaveEx( qboolean fromExternal ) {
	int i;
	qboolean wasFinished = ls.runFinished;
	Com_Printf( "^2LiveSplit: Run reset (saving golds)\n" );

	/* Record as a reset attempt if a run was active but not finished. */
	if ( ls.active && !ls.runFinished ) {
		int variant = LS_CurCategoryVariant();
		LS_ResetAttemptsEnsure( ls.numResetAttempts + 1 );
		{
			lsResetAttempt_t *ra = &ls.resetAttempts[ls.numResetAttempts];
			ra->mode       = ls.runMode;
			ra->difficulty = ls.currentDifficulty;
			ra->categoryVariant = variant;
			ra->missionNum = ls.runMission;
			ra->mapIdx     = ( ls.runMode == LS_MODE_IL ) ? ls.modeFirstIdx : -1;
			ra->endTime    = time( NULL );
			ra->startTime  = ra->endTime - ( ( Sys_Milliseconds() - ls.runStartRealMs ) / 1000 );
			ra->attemptId  = LS_MaxAttemptId( ls.runMode, LS_DiffIdx( ls.currentDifficulty ), ls.runMission, ra->mapIdx, variant ) + 1;
			ra->isStartedSynced = qtrue;
			ra->isEndedSynced   = qtrue;
			ra->pauseTimeMs     = 0;
			ls.numResetAttempts++;
		}
	}

	LS_AutoRecordStop();

	/* When the reset was triggered by external LS detection (got
	   "NotRunning" from polling), external LS is already reset.
	   Sending "reset" back would be redundant and could interfere
	   with the subsequent starttimer sent by auto-start. */
	if ( !fromExternal ) {
		LS_ExtSend( "reset" );
	}
	lsext_timerStarted = qfalse;
	lsext_recvLen = 0;  /* flush stale recv data */
	ls_resetPending = 0;
	ls_resetPendingStartMs = 0;
	ls_resetPromptKind = 0;
	ls_resetPromptGoldCount = 0;

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
	memset( ls.settingsDetected, 0, sizeof( ls.settingsDetected ) );
	ls.liveObjectivesFound = 0;
	ls.liveObjectivesTotal = 0;
	ls.liveSecretsFound = 0;
	ls.liveSecretsTotal = 0;
	ls.liveTreasureFound = 0;
	ls.liveTreasureTotal = 0;
	ls.baseSecretsFound  = 0;
	ls.baseTreasureFound = 0;

	/* Do not write Personal Best split times on an incomplete reset.
	   Golds (bestTimeMs) were already updated per segment in LS_CompleteSplit
	   and are saved as BestSegmentTime.  PB segments are only saved from a
	   completed category run in LS_FinishRun. */

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
	/* Keep prevMapname = current map so auto-start only fires on an
	   actual level load / changelevel, not while still on the same map.
	   The brief disconnect during map reload will clear prevMapname
	   via the !isConnected early-out in LS_Frame. */
	if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
		char tmp[LS_MAX_MAPNAME];
		LS_ExtractMapname( cl.mapname, tmp, sizeof( tmp ) );
		Q_strncpyz( ls.prevMapname, tmp, LS_MAX_MAPNAME );
	} else {
		ls.prevMapname[0] = '\0';
	}
	if ( ls_modeCvar ) {
		LS_SetupMode( ls_modeCvar->integer, ls_missionCvar ? ls_missionCvar->integer : 1 );
		LS_LoadMode( ls_modeCvar->integer );
	}

	LS_Save();
}

static void LS_DoResetSave( void ) {
	LS_DoResetSaveEx( qfalse );
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
	lsext_timerStarted = qfalse;
	lsext_recvLen = 0;  /* flush stale recv data */
	ls_resetPending = 0;
	ls_resetPendingStartMs = 0;
	ls_resetPromptKind = 0;
	ls_resetPromptGoldCount = 0;

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
	memset( ls.settingsDetected, 0, sizeof( ls.settingsDetected ) );
	ls.baseSecretsFound  = 0;
	ls.baseTreasureFound = 0;

	ls.currentMapIndex  = -1;
	ls.curVisRow        = -1;

	/* Keep prevMapname = current map so auto-start only fires on an
	   actual level load, not while still on the same map. */
	if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
		char tmp[LS_MAX_MAPNAME];
		LS_ExtractMapname( cl.mapname, tmp, sizeof( tmp ) );
		Q_strncpyz( ls.prevMapname, tmp, LS_MAX_MAPNAME );
	} else {
		ls.prevMapname[0] = '\0';
	}
	if ( ls_modeCvar ) {
		LS_SetupMode( ls_modeCvar->integer, ls_missionCvar ? ls_missionCvar->integer : 1 );
		LS_LoadMode( ls_modeCvar->integer );
	}

	LS_Save();
}

static void LS_Reset_f( void ) {
	if ( LS_RaceBlockTimerReset( "reset the timer" ) ) return;
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
	if ( ls.active || ls.runFinished ) {
		/* Finished run with new PB -> prompt */
		if ( ls.runFinished && LS_HasNewPB() ) {
			int gc = LS_CountNewGolds();
			ls_resetPending = 1;
			ls_resetPendingStartMs = Sys_Milliseconds();
			ls_resetPromptKind = LS_RESET_PROMPT_PB;
			ls_resetPromptGoldCount = gc;
			Com_Printf( "^3LiveSplit: New PB achieved! Press Y to save, N to discard.\n" );
			return;
		}
		/* Mid-run reset with new golds -> prompt with count */
		if ( !ls.runFinished && LS_HasNewGolds() ) {
			int gc = LS_CountNewGolds();
			ls_resetPending = 1;
			ls_resetPendingStartMs = Sys_Milliseconds();
			ls_resetPromptKind = LS_RESET_PROMPT_GOLDS;
			ls_resetPromptGoldCount = gc;
			Com_Printf( "^3LiveSplit: New golds from %d stage%s. Press Y to save, N to discard.\n",
				gc, gc > 1 ? "s" : "" );
			return;
		}
	}
	/* Everything else: save silently (history, golds, etc.) */
	LS_DoResetSave();
}

static void LS_ResetNoSave_f( void ) {
	if ( LS_RaceBlockTimerReset( "reset the timer" ) ) return;
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
	if ( ls.active || ls.runFinished ) {
		/* Finished run with new PB -> prompt */
		if ( ls.runFinished && LS_HasNewPB() ) {
			int gc = LS_CountNewGolds();
			ls_resetPending = 1;
			ls_resetPendingStartMs = Sys_Milliseconds();
			ls_resetPromptKind = LS_RESET_PROMPT_PB;
			ls_resetPromptGoldCount = gc;
			Com_Printf( "^3LiveSplit: New PB achieved! Press Y to save, N to discard.\n" );
			return;
		}
		/* Mid-run reset with new golds -> prompt with count */
		if ( !ls.runFinished && LS_HasNewGolds() ) {
			int gc = LS_CountNewGolds();
			ls_resetPending = 1;
			ls_resetPendingStartMs = Sys_Milliseconds();
			ls_resetPromptKind = LS_RESET_PROMPT_GOLDS;
			ls_resetPromptGoldCount = gc;
			Com_Printf( "^3LiveSplit: New golds from %d stage%s. Press Y to save, N to discard.\n",
				gc, gc > 1 ? "s" : "" );
			return;
		}
	}
	/* Everything else: reset silently (no save) */
	LS_DoResetNoSave();
}

/* Confirmation commands for reset popup */
static void LS_ResetConfirmSave_f( void ) {
	if ( !ls_resetPending ) return;
	if ( LS_RaceBlockTimerReset( "confirm a timer reset" ) ) return;
	LS_DoResetSave();
}

static void LS_ResetConfirmDiscard_f( void ) {
	if ( !ls_resetPending ) return;
	if ( LS_RaceBlockTimerReset( "confirm a timer reset" ) ) return;
	LS_DoResetNoSave();
}

static void LS_ResetCancel_f( void ) {
	if ( !ls_resetPending ) return;
	ls_resetPending = 0;
	ls_resetPendingStartMs = 0;
	ls_resetPromptKind = 0;
	ls_resetPromptGoldCount = 0;
	Com_Printf( "^2LiveSplit: Reset cancelled\n" );
}

static void LS_Start_f( void ) {
	int nowReal, newIdx;
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL && !ls_raceForceStart ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
	if ( LS_RaceBlocksTimerStart() && !ls_raceForceStart ) {
		Com_Printf( "^3Race: timer starts when the Ready countdown reaches GO\n" );
		LS_RaceSetStatus( "Timer starts at GO" );
		return;
	}
	if ( !ls.initialized ) return;

	nowReal = Sys_Milliseconds();

	/* If already active: manual split (advance to next split) */
	if ( ls.active && !ls.runFinished && !ls_raceForceStart ) {
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

	/* Full reset before starting - covers both "finished" and "cold" states */
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
		ls.manualPause     = qfalse;
		ls.manualStartIgnoreUi = qfalse;
		ls.cheatsUsed      = qfalse;
		ls.settingsModified = qfalse;
		ls.settingsModCount = 0;
		ls.totalPauses     = 0;
		ls.totalUndos      = 0;
		ls.totalSkips      = 0;
		ls.baseSecretsFound  = 0;
		ls.baseTreasureFound = 0;
		ls.mapLoadFreeze   = qfalse;
		ls.backtrackPause = qfalse;
		ls.backtrackTargetIndex = -1;
		Cvar_Set( "ls_loading", "0" );
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

		ls.active            = qtrue;
		ls_aslState.runStartCount++;
		ls_aslState.splitSeqNum = 0;
		ls.runStartRealMs    = nowReal;
		ls.lastServerTime    = cl.serverTime;
		ls.lastRealTimeMs    = nowReal;
		ls.currentDifficulty = LS_DetectDifficulty();
		ls.manualStartIgnoreUi = ( ls.runMode == LS_MODE_IL ) ? qtrue : qfalse;
		di = LS_CurDiffIdx();
		LS_SavePreRunPBs();

		if ( ls.runMode == LS_MODE_IL ) {
			/* IL mode: start on the current map */
			if ( newIdx >= 0 ) {
				ls.modeFirstIdx  = newIdx;
				ls.modeLastIdx   = newIdx;
				ls.modeEndMapIdx = newIdx;
				ls.currentMapIndex = newIdx;
			}
		} else {
			/* Fullgame / Mission: start on the current map if it's
			   within the active set (player already loaded past
			   cutscenes), otherwise default to first split. */
			if ( newIdx >= 0 && LS_InActiveSet( newIdx ) ) {
				ls.currentMapIndex = newIdx;
			} else {
				ls.currentMapIndex = ls.modeFirstIdx;
			}
		}
		LS_UpdateCurVisRow();

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
		Q_strncpyz( ls.actualMapname, currentMap, LS_MAX_MAPNAME );
		LS_Save();
		LS_AutoRecordStart();
		Com_Printf( "^2LiveSplit: Timer STARTED on '%s'\n", currentMap );
		lsext_recvLen = 0; /* flush stale recv data */
		LS_ExtSend( "initgametime" );
		LS_ExtSend( "starttimer" );
		LS_ExtSend( "pausegametime" );
		lsext_timerStarted = qtrue;
		lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;

		/* Immediate validation on manual start */
		ls.settingsCheckInterval = 0;
		LS_QuickCheckSettings();
		if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
			ls.cheatsUsed = qtrue;
			LS_TriggerAlert( "CHEATS ACTIVATED", 1.0f, 0.2f, 0.2f );
		}
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

	/* Strip extension and append backup suffix */
	{
		int baseLen = (int)strlen( filename );
		const char *ext = ".lss";
		if ( baseLen > 4 && !Q_stricmp( filename + baseLen - 4, ".dat" ) ) ext = ".dat";
		Com_sprintf( backupName, sizeof( backupName ),
			"%.*s_backup_%04d%02d%02d_%02d%02d%02d%s",
			baseLen - 4, filename,
			ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
			ti->tm_hour, ti->tm_min, ti->tm_sec, ext );
	}

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

	if ( LS_RaceBlockTimerReset( "reset LiveSplit stats" ) ) return;
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
	/* create backups of current category .lss before wiping */
	{
		char lssPath[256];
		LS_GetCurrentLssPath( lssPath, sizeof( lssPath ) );
		LS_BackupFile( lssPath );
	}

	Com_Printf( "^2LiveSplit: All stats & bests reset\n" );

	for ( i = 0; i < ls.numMaps; i++ ) {
		for ( di = 0; di < LS_TOTAL_DIFF_SLOTS; di++ ) {
			ls.splits[i].d[di].bestTimeMs       = 0;
			ls.splits[i].d[di].bestRealTimeMs   = 0;
			ls.splits[i].d[di].pbSegmentMs      = 0;
			ls.splits[i].d[di].pbRealSegmentMs  = 0;
			ls.splits[i].d[di].totalAttempts     = 0;
			ls.splits[i].d[di].totalCompletions  = 0;
		}
		ls.splits[i].currentTimeMs = 0;
		ls.splits[i].splitDone     = qfalse;
		ls.splits[i].splitSkipped  = qfalse;
		ls.splits[i].prevGoldMs    = 0;
		ls.splits[i].goldFlashMs   = 0;
	}

	for ( di = 0; di < LS_TOTAL_DIFF_SLOTS; di++ ) {
		ls.fgAttempts[di]    = 0;
		ls.fgCompletions[di] = 0;
		ls.fgPB[di]          = 0;
		ls.fgPBRgt[di]       = 0;
	}

	for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
		for ( di = 0; di < LS_TOTAL_DIFF_SLOTS; di++ ) {
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
	ls.backtrackPause      = qfalse;
	ls.backtrackTargetIndex = -1;
	if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
		char tmp[LS_MAX_MAPNAME];
		LS_ExtractMapname( cl.mapname, tmp, sizeof( tmp ) );
		Q_strncpyz( ls.prevMapname, tmp, LS_MAX_MAPNAME );
	} else {
		ls.prevMapname[0] = '\0';
	}
	ls.heinrichDead        = qfalse;
	ls.endCutsceneArmed    = qfalse;
	ls.endCutsceneSnapMs   = -1;
	ls.endCutscenePrevLB   = qfalse;
	Cvar_Set( "ls_changelevel", "0" );
	LS_HistoryFree();
	ls.manualPause         = qfalse;
	ls.pauseSavedRealMs    = 0;
	ls.totalPauses         = 0;
	ls.totalUndos          = 0;
	ls.totalSkips          = 0;
	LS_SaveAll();
}

static void LS_ResetCategory_f( void ) {
	int di, i;
	const char *modeName;
	const char *skillName;

	if ( LS_RaceBlockTimerReset( "reset the category" ) ) return;
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
	{
		char lssPath[256];
		LS_GetCurrentLssPath( lssPath, sizeof( lssPath ) );
		LS_BackupFile( lssPath );
	}

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

	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
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

	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
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

	/* 100%: nothing to roll back - base is always 0 since
	   liveSecretsFound is per-map (resets each map load).
	   Keep secretsFound/treasureFound intact so the snapshot
	   persists through the undo. */

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

/* Reset live 100% counters when a savegame/map load changes the current
   level state.  Mission stats in savegames can legitimately go backwards
   (loading an older save before a secret), so the high-watermark parser
   must start from zero again. */
static void LS_ResetLiveMissionStatsForMap( int idx ) {
	int secTotal = 0;
	int tresTotal = 0;

	if ( idx >= 0 && idx < ls.numMaps ) {
		secTotal = ls.splits[idx].secretsTotal;
		tresTotal = ls.splits[idx].treasureTotal;
	}

	ls.liveObjectivesFound = 0;
	ls.liveObjectivesTotal = 0;
	ls.liveSecretsFound = 0;
	ls.liveSecretsTotal = secTotal;
	ls.liveTreasureFound = 0;
	ls.liveTreasureTotal = tresTotal;
	ls.prevLiveSecretsFound = 0;
	ls.prevLiveSecretsTotal = secTotal;
	ls.prevLiveTreasureFound = 0;
	ls.prevLiveTreasureTotal = tresTotal;
}

/* =====================================================================
   livesplit_skip  - skip the current split (mark done with 0 time)
   ===================================================================== */
static void LS_Skip_f( void ) {
	int idx, nextIdx;

	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		Com_Printf( "^3LiveSplit: Binds disabled in External mode - use External LiveSplit controls\n" );
		return;
	}
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

	/* 100% tracking: save per-map secrets/treasure found.
	   liveSecretsFound is already per-map (resets each map load),
	   so no base subtraction or accumulation is needed. */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		if ( ls.splits[idx].secretsFound == 0 && ls.splits[idx].treasureFound == 0 ) {
			int sf = ls.liveSecretsFound;
			int tf = ls.liveTreasureFound;
			ls.splits[idx].secretsFound  = ( sf > 0 ) ? sf : 0;
			ls.splits[idx].treasureFound = ( tf > 0 ) ? tf : 0;
		}
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

static int LS_RaceSafePlayerStat( const int *values, int index );
static int LS_RaceAmmoIndexForWeapon( int weapon );
static int LS_RaceClipIndexForWeapon( int weapon );
static int LS_RaceNormalizeMovementDir( int movementDir );
static int LS_RaceAnimMovetypeForPlayerState( const playerState_t *ps );

#define GHOST_MAX_FRAMES   18000    /* 15 minutes at 20Hz */
#define GHOST_SAMPLE_MS    50       /* 20Hz                */
#define GHOST_PUBLISH_MS   8        /* cap cvar bridge updates at ~125Hz */
#define GHOST_FILE_VERSION 3
#define GHOST_FRAME_FLAG_CROUCH 1

typedef struct {
	int    timeMs;       /* ms of map IGT at this frame */
	float  x, y, z;     /* player origin               */
	float  yaw;          /* viewangles[YAW]             */
	float  pitch;
	float  vx, vy, vz;
	float  speed;
	int    flags;        /* GHOST_FRAME_FLAG_*          */
	int    health;
	int    armor;
	int    weapon;
	int    ammo;
	int    clip;
	int    legsAnim;
	int    torsoAnim;
	int    movementDir;
	int    eFlags;
	int    groundEntityNum;
	int    animMovetype;
} ghostFrame_t;

typedef struct {
	int    timeMs;
	float  x, y, z;
	float  yaw;
} ghostFrameV1_t;

typedef struct {
	int    timeMs;
	float  x, y, z;
	float  yaw;
	int    flags;
} ghostFrameV2_t;

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
	qboolean     isNewGold;       /* qtrue if playback data is from a gold just set this run */
	cvar_t       *enableCvar;     /* ls_ghost 0/1 */
} ls_ghost;

static struct {
	qboolean initialized;
	int      visible;
	int      color;
	int      lastPublishMs;
} ls_ghostPublish;

static void LS_GhostSetVisible( int visible ) {
	qboolean changed = ( !ls_ghostPublish.initialized || ls_ghostPublish.visible != visible ) ? qtrue : qfalse;
	if ( changed ) {
		Cvar_Set( "ls_ghost_visible", visible ? "1" : "0" );
		ls_ghostPublish.visible = visible;
		ls_ghostPublish.initialized = qtrue;
	}
	if ( !visible ) {
		if ( changed ) Cvar_Set( "ls_ghost_player", "" );
		ls_ghostPublish.lastPublishMs = 0;
	}
}

static int LS_GhostOpacity( void ) {
	int alpha = (int)Cvar_VariableValue( "ls_ghost_opacity" );
	if ( alpha <= 0 ) alpha = 60;
	if ( alpha > 255 ) alpha = 255;
	return alpha;
}

static int LS_GhostPackedColor( int color ) {
	return color ? ( ( 255 << 16 ) | ( 215 << 8 ) | 0 ) : ( ( 80 << 16 ) | ( 180 << 8 ) | 255 );
}

static void LS_GhostPublishFrame( const ghostFrame_t *gf, int color ) {
	int now = cls.realtime;
	qboolean forceColor = ( !ls_ghostPublish.initialized || ls_ghostPublish.color != color );
	char value[512];
	int alpha;
	int crouched;

	if ( !gf ) return;

	LS_GhostSetVisible( 1 );

	if ( ls_ghostPublish.lastPublishMs && !forceColor && now - ls_ghostPublish.lastPublishMs < GHOST_PUBLISH_MS ) {
		return;
	}

	crouched = ( gf->flags & GHOST_FRAME_FLAG_CROUCH ) ? 1 : 0;
	alpha = LS_GhostOpacity();

	Cvar_Set( "ls_ghost_x", va( "%.2f", gf->x ) );
	Cvar_Set( "ls_ghost_y", va( "%.2f", gf->y ) );
	Cvar_Set( "ls_ghost_z", va( "%.2f", gf->z ) );
	Cvar_Set( "ls_ghost_yaw", va( "%.2f", gf->yaw ) );
	Cvar_Set( "ls_ghost_speed", va( "%.1f", gf->speed ) );
	Cvar_Set( "ls_ghost_crouch", crouched ? "1" : "0" );
	Com_sprintf( value, sizeof( value ), "1 %d %d %.1f %.1f %.1f %.1f %.1f %d %d %d %d %d %d %d %d Ghost %d %d %.1f %.1f %.1f %.1f %d %d",
		LS_GhostPackedColor( color ), alpha,
		gf->x, gf->y, gf->z, gf->yaw, gf->speed, crouched,
		gf->health, gf->armor, gf->weapon, gf->ammo, gf->clip,
		gf->legsAnim, gf->torsoAnim, gf->movementDir, gf->eFlags,
		gf->pitch, gf->vx, gf->vy, gf->vz, gf->groundEntityNum, gf->animMovetype );
	Cvar_Set( "ls_ghost_player", value );
	if ( forceColor ) {
		Cvar_Set( "ls_ghost_color", color ? "1" : "0" );
		ls_ghostPublish.color = color;
	}
	ls_ghostPublish.lastPublishMs = now;
}

static void LS_GhostSetFrameDefaults( ghostFrame_t *gf ) {
	if ( !gf ) return;
	gf->flags = 0;
	gf->pitch = 0.0f;
	gf->vx = gf->vy = gf->vz = 0.0f;
	gf->speed = 0.0f;
	gf->health = 100;
	gf->armor = 0;
	gf->weapon = 0;
	gf->ammo = 0;
	gf->clip = 0;
	gf->legsAnim = -1;
	gf->torsoAnim = -1;
	gf->movementDir = 0;
	gf->eFlags = 0;
	gf->groundEntityNum = ENTITYNUM_WORLD;
	gf->animMovetype = 0;
}

static void LS_GhostSanitizeFrame( ghostFrame_t *gf ) {
	if ( !gf ) return;
	if ( gf->health < 0 ) gf->health = 0;
	if ( gf->armor < 0 ) gf->armor = 0;
	if ( gf->weapon < 0 || gf->weapon >= WP_NUM_WEAPONS ) gf->weapon = 0;
	if ( gf->ammo < 0 ) gf->ammo = 0;
	if ( gf->clip < 0 ) gf->clip = 0;
	if ( gf->groundEntityNum < 0 || gf->groundEntityNum > ENTITYNUM_NONE ) gf->groundEntityNum = ENTITYNUM_WORLD;
	if ( gf->animMovetype < 0 ) gf->animMovetype = 0;
	gf->movementDir = LS_RaceNormalizeMovementDir( gf->movementDir );
	if ( gf->flags & GHOST_FRAME_FLAG_CROUCH ) gf->eFlags |= EF_CROUCHING;
}

static qboolean LS_LocalPlayerCrouched( void );

static void LS_GhostSampleFrame( ghostFrame_t *gf, int timeMs ) {
	int weapon;
	int ammoIndex;
	int clipIndex;
	vec3_t hvel;

	if ( !gf ) return;
	memset( gf, 0, sizeof( *gf ) );
	LS_GhostSetFrameDefaults( gf );
	gf->timeMs = timeMs;
	gf->x = cl.snap.ps.origin[0];
	gf->y = cl.snap.ps.origin[1];
	gf->z = cl.snap.ps.origin[2];
	gf->yaw = cl.snap.ps.viewangles[1];
	gf->pitch = cl.snap.ps.viewangles[0];
	gf->vx = cl.snap.ps.velocity[0];
	gf->vy = cl.snap.ps.velocity[1];
	gf->vz = cl.snap.ps.velocity[2];
	hvel[0] = gf->vx;
	hvel[1] = gf->vy;
	hvel[2] = 0.0f;
	gf->speed = VectorLength( hvel );
	gf->flags = LS_LocalPlayerCrouched() ? GHOST_FRAME_FLAG_CROUCH : 0;
	gf->legsAnim = cl.snap.ps.legsAnim;
	gf->torsoAnim = cl.snap.ps.torsoAnim;
	gf->movementDir = LS_RaceNormalizeMovementDir( cl.snap.ps.movementDir );
	gf->eFlags = cl.snap.ps.eFlags;
	gf->groundEntityNum = cl.snap.ps.groundEntityNum;
	gf->animMovetype = LS_RaceAnimMovetypeForPlayerState( &cl.snap.ps );
	gf->health = cl.snap.ps.stats[STAT_HEALTH];
	gf->armor = cl.snap.ps.stats[STAT_ARMOR];
	weapon = cl.snap.ps.weapon;
	if ( weapon < 0 || weapon >= WP_NUM_WEAPONS ) weapon = 0;
	gf->weapon = weapon;
	ammoIndex = LS_RaceAmmoIndexForWeapon( weapon );
	clipIndex = LS_RaceClipIndexForWeapon( weapon );
	gf->ammo = LS_RaceSafePlayerStat( cl.snap.ps.ammo, ammoIndex );
	gf->clip = LS_RaceSafePlayerStat( cl.snap.ps.ammoclip, clipIndex );
	LS_GhostSanitizeFrame( gf );
}

static float LS_GhostLerpAngle( float a, float b, float frac ) {
	float da = b - a;
	if ( da > 180.0f ) da -= 360.0f;
	if ( da < -180.0f ) da += 360.0f;
	return a + frac * da;
}

static void LS_GhostInterpolateFrame( ghostFrame_t *out, const ghostFrame_t *a, const ghostFrame_t *b, int timeMs, float frac ) {
	float dt, dx, dy;
	if ( !out || !a || !b ) return;
	*out = ( frac < 0.5f ) ? *a : *b;
	out->timeMs = timeMs;
	out->x = a->x + frac * ( b->x - a->x );
	out->y = a->y + frac * ( b->y - a->y );
	out->z = a->z + frac * ( b->z - a->z );
	out->yaw = LS_GhostLerpAngle( a->yaw, b->yaw, frac );
	out->pitch = LS_GhostLerpAngle( a->pitch, b->pitch, frac );
	out->vx = a->vx + frac * ( b->vx - a->vx );
	out->vy = a->vy + frac * ( b->vy - a->vy );
	out->vz = a->vz + frac * ( b->vz - a->vz );
	out->flags = ( frac < 0.5f ) ? a->flags : b->flags;
	dt = (float)( b->timeMs - a->timeMs );
	dx = b->x - a->x;
	dy = b->y - a->y;
	if ( dt > 0.0f ) {
		out->speed = sqrtf( dx * dx + dy * dy ) / ( dt / 1000.0f );
	}
	LS_GhostSanitizeFrame( out );
}

static const char *LS_GhostDifficultyFolder( int diff ) {
	if ( diff <= 1 ) return "easy";
	if ( diff == 2 ) return "medium";
	return "hard";
}

static void LS_GhostBuildLegacyPath( const char *mapname, char *out, int outSize ) {
	int diff = ls.currentDifficulty;
	const char *suffix = ( ls_100pctCvar && ls_100pctCvar->integer ) ? "_100" : "";
	const char *moveSuffix = LS_HL1ModeFileSuffix();
	if ( diff < 1 ) diff = 1;
	if ( diff > 3 ) diff = 3;

	switch ( ls.runMode ) {
	case LS_MODE_FULLGAME:
		Com_sprintf( out, outSize, "ghostruns/fg/d%d/%s%s%s.ghost", diff, mapname, suffix, moveSuffix );
		break;
	case LS_MODE_MISSION:
		Com_sprintf( out, outSize, "ghostruns/m%d/d%d/%s%s%s.ghost", ls.runMission, diff, mapname, suffix, moveSuffix );
		break;
	case LS_MODE_IL:
		Com_sprintf( out, outSize, "ghostruns/il/d%d/%s%s%s.ghost", diff, mapname, suffix, moveSuffix );
		break;
	default:
		Com_sprintf( out, outSize, "ghostruns/fg/d%d/%s%s%s.ghost", diff, mapname, suffix, moveSuffix );
		break;
	}
}

/* Build a category-specific ghost file path.
   Fullgame:  ghostruns/fg/<easy|medium|hard>/<mapname>.ghost
   Mission N: ghostruns/chapter<N>/<easy|medium|hard>/<mapname>.ghost
   IL:        ghostruns/il/<easy|medium|hard>/<mapname>.ghost  */
static void LS_GhostBuildPath( const char *mapname, char *out, int outSize ) {
	int diff = ls.currentDifficulty;
	int mission = ls.runMission;
	const char *suffix = ( ls_100pctCvar && ls_100pctCvar->integer ) ? "_100" : "";
	const char *moveSuffix = LS_HL1ModeFileSuffix();
	const char *difficulty;
	if ( diff < 1 ) diff = 1;
	if ( diff > 3 ) diff = 3;
	if ( mission < 1 ) mission = 1;
	if ( mission > LS_NUM_MISSION_GROUPS ) mission = LS_NUM_MISSION_GROUPS;
	difficulty = LS_GhostDifficultyFolder( diff );

	switch ( ls.runMode ) {
	case LS_MODE_FULLGAME:
		Com_sprintf( out, outSize, "ghostruns/fg/%s/%s%s%s.ghost", difficulty, mapname, suffix, moveSuffix );
		break;
	case LS_MODE_MISSION:
		Com_sprintf( out, outSize, "ghostruns/chapter%d/%s/%s%s%s.ghost", mission, difficulty, mapname, suffix, moveSuffix );
		break;
	case LS_MODE_IL:
		Com_sprintf( out, outSize, "ghostruns/il/%s/%s%s%s.ghost", difficulty, mapname, suffix, moveSuffix );
		break;
	default:
		Com_sprintf( out, outSize, "ghostruns/fg/%s/%s%s%s.ghost", difficulty, mapname, suffix, moveSuffix );
		break;
	}
}

/* Build a string identifying the current category for cache invalidation */
static void LS_GhostCategoryStr( char *out, int outSize ) {
	int diff = ls.currentDifficulty;
	int pct = ( ls_100pctCvar && ls_100pctCvar->integer ) ? 1 : 0;
	if ( diff < 1 ) diff = 1;
	if ( diff > 3 ) diff = 3;
	Com_sprintf( out, outSize, "%d_%d_d%d_p%d_hl1%d", ls.runMode, ls.runMission, diff, pct, LS_HL1ModeActive() ? 1 : 0 );
}

/* Check if a ghost file exists for a given map */
static qboolean LS_GhostFileExists( const char *mapname ) {
	fileHandle_t f;
	char path[MAX_QPATH];
	int len;
	LS_GhostBuildPath( mapname, path, sizeof( path ) );
	len = FS_FOpenFileRead( path, &f, qtrue );
	if ( f ) {
		FS_FCloseFile( f );
		f = 0;
	}
	if ( len <= 0 ) {
		LS_GhostBuildLegacyPath( mapname, path, sizeof( path ) );
		len = FS_FOpenFileRead( path, &f, qtrue );
	}
	if ( f ) FS_FCloseFile( f );
	return ( len > 0 ) ? qtrue : qfalse;
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
	ls_ghost.isNewGold     = qfalse;
	ls_ghost.loadedMap[0]  = '\0';
	ls_ghost.loadedCategory[0] = '\0';

	LS_GhostBuildPath( mapname, path, sizeof( path ) );
	fileLen = FS_FOpenFileRead( path, &f, qtrue );
	if ( !f || fileLen <= 0 ) {
		if ( f ) {
			FS_FCloseFile( f );
			f = 0;
		}
		LS_GhostBuildLegacyPath( mapname, path, sizeof( path ) );
		fileLen = FS_FOpenFileRead( path, &f, qtrue );
	}
	if ( !f || fileLen <= 0 ) {
		if ( f ) FS_FCloseFile( f );
		return;
	}

	FS_Read( &version, sizeof( int ), f );
	if ( version != 1 && version != 2 && version != GHOST_FILE_VERSION ) {
		FS_FCloseFile( f );
		return;
	}

	FS_Read( &count, sizeof( int ), f );
	if ( count <= 0 || count > GHOST_MAX_FRAMES ) {
		FS_FCloseFile( f );
		return;
	}

	if ( version == 1 ) {
		int i;
		ghostFrameV1_t oldFrame;
		for ( i = 0; i < count; i++ ) {
			FS_Read( &oldFrame, sizeof( oldFrame ), f );
			LS_GhostSetFrameDefaults( &ls_ghost.play[i] );
			ls_ghost.play[i].timeMs = oldFrame.timeMs;
			ls_ghost.play[i].x = oldFrame.x;
			ls_ghost.play[i].y = oldFrame.y;
			ls_ghost.play[i].z = oldFrame.z;
			ls_ghost.play[i].yaw = oldFrame.yaw;
			LS_GhostSanitizeFrame( &ls_ghost.play[i] );
		}
	} else if ( version == 2 ) {
		int i;
		ghostFrameV2_t oldFrame;
		for ( i = 0; i < count; i++ ) {
			FS_Read( &oldFrame, sizeof( oldFrame ), f );
			LS_GhostSetFrameDefaults( &ls_ghost.play[i] );
			ls_ghost.play[i].timeMs = oldFrame.timeMs;
			ls_ghost.play[i].x = oldFrame.x;
			ls_ghost.play[i].y = oldFrame.y;
			ls_ghost.play[i].z = oldFrame.z;
			ls_ghost.play[i].yaw = oldFrame.yaw;
			ls_ghost.play[i].flags = oldFrame.flags;
			LS_GhostSanitizeFrame( &ls_ghost.play[i] );
		}
	} else {
		int i;
		FS_Read( ls_ghost.play, sizeof( ghostFrame_t ) * count, f );
		for ( i = 0; i < count; i++ ) {
			LS_GhostSanitizeFrame( &ls_ghost.play[i] );
		}
	}
	FS_FCloseFile( f );

	ls_ghost.playCount  = count;
	ls_ghost.playLoaded = qtrue;
	Q_strncpyz( ls_ghost.loadedMap, mapname, sizeof( ls_ghost.loadedMap ) );
	LS_GhostCategoryStr( ls_ghost.loadedCategory, sizeof( ls_ghost.loadedCategory ) );
	Com_Printf( "^2Ghost: loaded %d frames from %s\n", count, path );
}

/* Called each active frame from LS_Frame */
static qboolean LS_LocalPlayerCrouched( void ) {
	usercmd_t cmd;
	if ( cl.snap.ps.pm_flags & PMF_DUCKED ) return qtrue;
	if ( cl.snap.ps.eFlags & EF_CROUCHING ) return qtrue;
	if ( cl.snap.ps.viewheight > 0 && cl.snap.ps.viewheight <= CROUCH_VIEWHEIGHT + 2 ) return qtrue;
	if ( cl.cmdNumber > 0 ) {
		cmd = cl.cmds[cl.cmdNumber & CMD_MASK];
		if ( cmd.wbuttons & WBUTTON_CROUCH ) return qtrue;
		if ( cmd.upmove < 0 ) return qtrue;
	}
	return qfalse;
}

static void LS_GhostFrame( void ) {
	int    mapIGT, curIdx;

	if ( !ls_ghost.enableCvar || !ls_ghost.enableCvar->integer ) {
		LS_GhostSetVisible( 0 );
		return;
	}

	if ( !ls.active || ls.currentMapIndex < 0 ) {
		LS_GhostSetVisible( 0 );
		/* Clear loadedMap so ghost reloads when run re-activates */
		ls_ghost.loadedMap[0] = '\0';
		ls_ghost.recCount     = 0;
		ls_ghost.playSearchIdx = 0;
		ls_ghost.playLoaded    = qfalse;
		ls_ghost.playCount     = 0;
		return;
	}

	curIdx = ls.currentMapIndex;
	if ( curIdx >= ls.numMaps ) { LS_GhostSetVisible( 0 ); return; }
	if ( ls.splits[curIdx].cutscene ) { LS_GhostSetVisible( 0 ); return; }

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
		LS_GhostSampleFrame( gf, mapIGT );
		ls_ghost.recCount++;
		ls_ghost.lastRecMs = mapIGT;
	}

	/* --- Playback: interpolate ghost position for current mapIGT --- */
	if ( ls_ghost.playLoaded && ls_ghost.playCount > 0 ) {
		int   i;
		ghostFrame_t publishFrame;

		if ( mapIGT <= 0 || mapIGT <= ls_ghost.play[0].timeMs ) {
			publishFrame = ls_ghost.play[0];
		} else if ( mapIGT >= ls_ghost.play[ls_ghost.playCount - 1].timeMs ) {
			/* Past end - freeze at last position */
			publishFrame = ls_ghost.play[ls_ghost.playCount - 1];
			publishFrame.speed = 0.0f;
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
				float frac = ( b->timeMs == a->timeMs ) ? 0.0f
					: (float)( mapIGT - a->timeMs ) / (float)( b->timeMs - a->timeMs );
				LS_GhostInterpolateFrame( &publishFrame, a, b, mapIGT, frac );
			}
		}
		LS_GhostSanitizeFrame( &publishFrame );

		/* Ghost color: compare ghost's last frame time with best segment time.
		   Yellow (1) = ghost time ≈ best time (ghost IS the best run).
		   Blue   (0) = ghost time differs (e.g. LSS has a better BEST from import).
		   Use tolerance of GHOST_SAMPLE_MS because the last recorded frame
		   may not land exactly on the split time. */
		{
			int ghostLastMs = ls_ghost.play[ls_ghost.playCount - 1].timeMs;
			int di2 = LS_CurDiffIdx();
			int mi2 = ls.currentMapIndex;
			int bestMs2 = ( mi2 >= 0 && mi2 < ls.numMaps ) ? ls.splits[mi2].d[di2].bestTimeMs : 0;
			int diff2 = ( bestMs2 > ghostLastMs ) ? bestMs2 - ghostLastMs : ghostLastMs - bestMs2;
			LS_GhostPublishFrame( &publishFrame, ( bestMs2 > 0 && diff2 <= GHOST_SAMPLE_MS ) ? 1 : 0 );
		}
	} else {
		LS_GhostSetVisible( 0 );
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
	ls_ghost.isNewGold     = qtrue;  /* mark as freshly-set gold this run */
	Q_strncpyz( ls_ghost.loadedMap, mapname, sizeof( ls_ghost.loadedMap ) );
	LS_GhostCategoryStr( ls_ghost.loadedCategory, sizeof( ls_ghost.loadedCategory ) );
}

/* =====================================================================
   Split Records Viewer  (UI tab "Records")
   Populates cvars that the menu displays.  Engine commands let the menu
   switch mode/difficulty/mission/page and reset individual golds.
   ===================================================================== */

#define SV_ROWS_PER_PAGE LS_MAX_MAPS

/* Maps each visible display row -> split index in ls.splits[] */
static int  sv_rowMap[SV_ROWS_PER_PAGE];
static int  sv_numRows;           /* rows populated on current page */
static int  sv_viewMode   = 0;    /* LS_MODE_FULLGAME/MISSION/IL */
static int  sv_viewDiff   = 2;    /* g_gameskill 1-3 */
static int  sv_viewMs     = 1;    /* mission group 1-5 */
static int  sv_viewVariant = 0;   /* bit 0 = 100%, bit 1 = HL1 */
static int  sv_curPage    = 0;
static int  sv_totalPages = 1;

static qboolean SV_ViewPct100( void ) {
	return ( sv_viewVariant & 1 ) ? qtrue : qfalse;
}

static qboolean SV_ViewHL1( void ) {
	return ( sv_viewVariant & 2 ) ? qtrue : qfalse;
}

static const char *SV_ViewVariantName( void ) {
	switch ( sv_viewVariant & 3 ) {
	case 1: return "100%";
	case 2: return "HL1 Any%";
	case 3: return "HL1 100%";
	default: return "Any%";
	}
}

/* Compute variant-aware diff slot */
static int SV_DiffSlot( void ) {
	int skill = sv_viewDiff;
	int mode  = sv_viewMode;
	return LS_DiffSlotFor( mode, skill - 1, SV_ViewPct100(), SV_ViewHL1() );
}

#define SV_PB_CHART_MAX_POINTS 24

typedef struct {
	int attemptId;
	int timeMs;
} svPbChartRun_t;

static int SV_PbChartCompareRuns( const void *a, const void *b ) {
	const svPbChartRun_t *ra = (const svPbChartRun_t *)a;
	const svPbChartRun_t *rb = (const svPbChartRun_t *)b;
	if ( ra->attemptId < rb->attemptId ) return -1;
	if ( ra->attemptId > rb->attemptId ) return 1;
	if ( ra->timeMs < rb->timeMs ) return -1;
	if ( ra->timeMs > rb->timeMs ) return 1;
	return 0;
}

static qboolean SV_HistoryRunMatchesView( const lsRunHistory_t *r ) {
	int viewDiff = sv_viewDiff - 1;
	if ( !r ) return qfalse;
	if ( viewDiff < 0 ) viewDiff = 0;
	if ( viewDiff >= LS_MAX_DIFFICULTIES ) viewDiff = LS_MAX_DIFFICULTIES - 1;
	if ( r->mode != sv_viewMode ) return qfalse;
	if ( r->categoryVariant != ( sv_viewVariant & 3 ) ) return qfalse;
	if ( LS_DiffIdx( r->difficulty ) != viewDiff ) return qfalse;
	if ( sv_viewMode == LS_MODE_MISSION && r->missionNum != sv_viewMs ) return qfalse;
	return qtrue;
}

static void SV_BuildPbChart( void ) {
	svPbChartRun_t *runs;
	int *pbs;
	int runCount = 0;
	int pbCount = 0;
	int i, bestMs, outCount;
	char chart[256];
	int len = 0;

	Cvar_Set( "ls_sv_pb_chart", "" );
	Cvar_SetValue( "ls_sv_pb_chart_count", 0 );

	if ( ls.numHistoryRuns <= 0 || !ls.history ) return;

	runs = (svPbChartRun_t *)Z_Malloc( sizeof( *runs ) * ls.numHistoryRuns );
	pbs = (int *)Z_Malloc( sizeof( *pbs ) * ls.numHistoryRuns );
	if ( !runs || !pbs ) {
		if ( runs ) Z_Free( runs );
		if ( pbs ) Z_Free( pbs );
		return;
	}

	for ( i = 0; i < ls.numHistoryRuns; i++ ) {
		const lsRunHistory_t *r = &ls.history[i];
		if ( !SV_HistoryRunMatchesView( r ) ) continue;
		if ( r->totalIGTMs <= 0 ) continue;
		runs[runCount].attemptId = r->attemptId;
		runs[runCount].timeMs = r->totalIGTMs;
		runCount++;
	}

	if ( runCount <= 0 ) {
		Z_Free( runs );
		Z_Free( pbs );
		return;
	}

	qsort( runs, runCount, sizeof( runs[0] ), SV_PbChartCompareRuns );
	bestMs = 0;
	for ( i = 0; i < runCount; i++ ) {
		if ( bestMs <= 0 || runs[i].timeMs < bestMs ) {
			bestMs = runs[i].timeMs;
			pbs[pbCount++] = bestMs;
		}
	}

	if ( pbCount <= 0 ) {
		Z_Free( runs );
		Z_Free( pbs );
		return;
	}

	chart[0] = '\0';
	outCount = pbCount < SV_PB_CHART_MAX_POINTS ? pbCount : SV_PB_CHART_MAX_POINTS;
	for ( i = 0; i < outCount; i++ ) {
		int src = ( pbCount <= SV_PB_CHART_MAX_POINTS ) ? i : ( i * ( pbCount - 1 ) ) / ( SV_PB_CHART_MAX_POINTS - 1 );
		char entry[16];
		int entryLen;
		Com_sprintf( entry, sizeof( entry ), "%s%d", i > 0 ? "," : "", pbs[src] );
		entryLen = (int)strlen( entry );
		if ( len + entryLen >= (int)sizeof( chart ) - 1 ) break;
		Q_strcat( chart, sizeof( chart ), entry );
		len += entryLen;
	}

	Cvar_Set( "ls_sv_pb_chart", chart );
	Cvar_SetValue( "ls_sv_pb_chart_count", outCount );
	Z_Free( runs );
	Z_Free( pbs );
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
	char buf[256];
	const char *modeName, *diffName;

	if ( !ls.initialized ) return;

	/* Ensure the viewed mode/category variant's .lss files are loaded (lazy) */
	LS_LoadModeVariant( sv_viewMode, SV_ViewPct100(), SV_ViewHL1() );

	di = SV_DiffSlot();

	SV_GatherVisible();

	/* Show the whole viewed category; the ImGui page scrolls as one surface. */
	sv_totalPages = 1;
	sv_curPage = 0;
	pageStart = 0;
	pageEnd   = sv_allVisN;
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

	Com_sprintf( buf, sizeof( buf ), "%s - %s - %s", modeName, SV_ViewVariantName(), diffName );
	Cvar_Set( "ls_sv_title", buf );

	/* Category stats */
	{
		int att = 0, comp = 0, pb = 0, pbRgt = 0, goldCount = 0;
		char pbBuf[32], rgtBuf[32], sobBuf[32], rateBuf[32];
		int sobMs = 0;
		qboolean hasSob = qfalse;

		switch ( sv_viewMode ) {
		case LS_MODE_MISSION: {
			int gi = sv_viewMs - 1;
			if ( gi < 0 ) gi = 0;
			if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
			att  = ls.msAttempts[gi][di];
			comp = ls.msCompletions[gi][di];
			pb   = ls.msPB[gi][di];
			pbRgt = ls.msPBRgt[gi][di];
			break;
		}
		case LS_MODE_IL:
			att = 0; comp = 0; pb = 0; pbRgt = 0;
			for ( i = 0; i < sv_allVisN; i++ ) {
				int si = sv_allVis[i];
				att += ls.splits[si].d[di].totalAttempts;
				comp += ls.splits[si].d[di].totalCompletions;
			}
			break;
		default: {
			att   = ls.fgAttempts[di];
			comp  = ls.fgCompletions[di];
			pb    = ls.fgPB[di];
			pbRgt = ls.fgPBRgt[di];
			break;
		}
		}

		hasSob = qtrue;
		for ( i = 0; i < sv_allVisN; i++ ) {
			int si = sv_allVis[i];
			int g = ls.splits[si].d[di].bestTimeMs;
			if ( g > 0 ) {
				sobMs += g;
				goldCount++;
			} else {
				hasSob = qfalse;
			}
		}

		if ( pb > 0 ) LS_FormatTime( pb, pbBuf, sizeof( pbBuf ) );
		else Q_strncpyz( pbBuf, "---", sizeof( pbBuf ) );

		if ( pbRgt > 0 ) LS_FormatTime( pbRgt, rgtBuf, sizeof( rgtBuf ) );
		else Q_strncpyz( rgtBuf, "---", sizeof( rgtBuf ) );

		if ( hasSob && sobMs > 0 ) LS_FormatTime( sobMs, sobBuf, sizeof( sobBuf ) );
		else Q_strncpyz( sobBuf, "---", sizeof( sobBuf ) );

		if ( att > 0 ) {
			Com_sprintf( rateBuf, sizeof( rateBuf ), "%d%%", ( comp * 100 ) / att );
		} else {
			Q_strncpyz( rateBuf, "---", sizeof( rateBuf ) );
		}

		Com_sprintf( buf, sizeof( buf ),
			"PB: %s  RGT PB: %s  SOB: %s  Att: %d  Comp: %d  Rate: %s  Golds: %d/%d",
			pbBuf, rgtBuf, sobBuf, att, comp, rateBuf, goldCount, sv_allVisN );
		Cvar_Set( "ls_sv_stats", buf );
		Cvar_Set( "ls_sv_pb", pbBuf );
		Cvar_Set( "ls_sv_rgt_pb", rgtBuf );
		Cvar_Set( "ls_sv_sob", sobBuf );
		Cvar_SetValue( "ls_sv_attempts", att );
		Cvar_SetValue( "ls_sv_completions", comp );
		Cvar_Set( "ls_sv_completion_rate", rateBuf );
		Cvar_SetValue( "ls_sv_golds", goldCount );
		Cvar_SetValue( "ls_sv_total_rows", sv_allVisN );
	}
	SV_BuildPbChart();

	/* Row indicator */
	Com_sprintf( buf, sizeof( buf ), "%d rows", sv_allVisN );
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
			const char *name = LS_SplitMenuName( si );
			char goldBuf[24], pbBuf[24];

			if ( gold > 0 ) LS_FormatTime( gold, goldBuf, sizeof( goldBuf ) );
			else Q_strncpyz( goldBuf, "---", sizeof( goldBuf ) );

			if ( pbseg > 0 ) LS_FormatTime( pbseg, pbBuf, sizeof( pbBuf ) );
			else Q_strncpyz( pbBuf, "---", sizeof( pbBuf ) );

			Com_sprintf( buf, sizeof( buf ), "%s|%s|%s|%d|%d",
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
	Cvar_SetValue( "ls_sv_diff", sv_viewDiff );
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

static void LS_SvVariant_f( void ) {
	if ( Cmd_Argc() < 2 ) return;
	sv_viewVariant = atoi( Cmd_Argv( 1 ) ) & 3;
	Cvar_SetValue( "ls_sv_variant", sv_viewVariant );
	sv_curPage = 0;
	SV_Refresh();
}

static void LS_SvPageNext_f( void ) {
	SV_Refresh();
}

static void LS_SvPagePrev_f( void ) {
	SV_Refresh();
}

/* Build .lss path for the currently viewed SV category and back it up */
static void SV_BackupCurrentLss( int rowMapIdx ) {
	char lssPath[256];
	int  diffIdx = sv_viewDiff - 1;
	int  msGroup = sv_viewMs - 1;
	qboolean p100 = SV_ViewPct100();
	qboolean hl1 = SV_ViewHL1();
	int mapIdx = ( sv_viewMode == LS_MODE_IL ) ? rowMapIdx : -1;
	if ( diffIdx < 0 ) diffIdx = 0;
	if ( diffIdx >= LS_MAX_DIFFICULTIES ) diffIdx = LS_MAX_DIFFICULTIES - 1;
	if ( msGroup < 0 ) msGroup = 0;
	if ( msGroup >= LS_NUM_MISSION_GROUPS ) msGroup = LS_NUM_MISSION_GROUPS - 1;
	LS_GetLssPath( lssPath, sizeof( lssPath ), sv_viewMode, diffIdx, msGroup, mapIdx, p100, hl1 );
	LS_BackupFile( lssPath );
}

static void SV_SaveCurrentLss( int rowMapIdx ) {
	int diffIdx = sv_viewDiff - 1;
	int msGroup = sv_viewMs - 1;
	int mapIdx = ( sv_viewMode == LS_MODE_IL ) ? rowMapIdx : -1;
	if ( diffIdx < 0 ) diffIdx = 0;
	if ( diffIdx >= LS_MAX_DIFFICULTIES ) diffIdx = LS_MAX_DIFFICULTIES - 1;
	if ( msGroup < 0 ) msGroup = 0;
	if ( msGroup >= LS_NUM_MISSION_GROUPS ) msGroup = LS_NUM_MISSION_GROUPS - 1;
	LS_WriteLssEx( sv_viewMode, diffIdx, msGroup, mapIdx, SV_ViewPct100(), SV_ViewHL1(), qtrue );
	LS_SaveState();
}

static qboolean SV_MatchesCurrentCategory( int mode, int diffIdx, int missionNum, int mapIdx, int variant ) {
	int viewDiff = sv_viewDiff - 1;
	if ( viewDiff < 0 ) viewDiff = 0;
	if ( viewDiff >= LS_MAX_DIFFICULTIES ) viewDiff = LS_MAX_DIFFICULTIES - 1;
	if ( mode != sv_viewMode ) return qfalse;
	if ( diffIdx != viewDiff ) return qfalse;
	if ( variant != ( sv_viewVariant & 3 ) ) return qfalse;
	if ( sv_viewMode == LS_MODE_MISSION && missionNum != sv_viewMs ) return qfalse;
	if ( sv_viewMode == LS_MODE_IL && mapIdx < 0 ) return qfalse;
	return qtrue;
}

static void SV_PurgeCurrentCategoryHistory( void ) {
	int i, out;

	out = 0;
	for ( i = 0; i < ls.numHistoryRuns; i++ ) {
		lsRunHistory_t *r = &ls.history[i];
		if ( SV_MatchesCurrentCategory( r->mode, LS_DiffIdx( r->difficulty ), r->missionNum, r->mapIdx, r->categoryVariant ) ) continue;
		if ( out != i ) ls.history[out] = ls.history[i];
		out++;
	}
	ls.numHistoryRuns = out;

	out = 0;
	for ( i = 0; i < ls.numResetAttempts; i++ ) {
		lsResetAttempt_t *ra = &ls.resetAttempts[i];
		if ( SV_MatchesCurrentCategory( ra->mode, LS_DiffIdx( ra->difficulty ), ra->missionNum, ra->mapIdx, ra->categoryVariant ) ) continue;
		if ( out != i ) ls.resetAttempts[out] = ls.resetAttempts[i];
		out++;
	}
	ls.numResetAttempts = out;

	out = 0;
	for ( i = 0; i < ls.numOrphanSegTimes; i++ ) {
		lsOrphanSegTime_t *o = &ls.orphanSegTimes[i];
		if ( SV_MatchesCurrentCategory( o->mode, o->diffIdx, o->missionNum, o->mapIdx, o->categoryVariant ) ) continue;
		if ( out != i ) ls.orphanSegTimes[out] = ls.orphanSegTimes[i];
		out++;
	}
	ls.numOrphanSegTimes = out;
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

	SV_BackupCurrentLss( si );
	ls.splits[si].d[di].bestTimeMs = 0;
	ls.splits[si].d[di].bestRealTimeMs = 0;
	Com_Printf( "^2LiveSplit: Gold reset for '%s'\n",
		ls.splits[si].displayName ? ls.splits[si].displayName : ls.splits[si].mapname );
	SV_SaveCurrentLss( si );
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

	SV_BackupCurrentLss( si );
	ls.splits[si].d[di].pbSegmentMs = 0;
	ls.splits[si].d[di].pbRealSegmentMs = 0;
	Com_Printf( "^2LiveSplit: PB segment reset for '%s'\n",
		ls.splits[si].displayName ? ls.splits[si].displayName : ls.splits[si].mapname );
	SV_SaveCurrentLss( si );
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

	SV_BackupCurrentLss( si );
	ls.splits[si].d[di].bestTimeMs      = 0;
	ls.splits[si].d[di].bestRealTimeMs  = 0;
	ls.splits[si].d[di].pbSegmentMs     = 0;
	ls.splits[si].d[di].pbRealSegmentMs = 0;
	ls.splits[si].d[di].totalAttempts   = 0;
	ls.splits[si].d[di].totalCompletions = 0;
	Com_Printf( "^2LiveSplit: All stats reset for '%s'\n",
		ls.splits[si].displayName ? ls.splits[si].displayName : ls.splits[si].mapname );
	SV_SaveCurrentLss( si );
	SV_Refresh();
}

static void LS_SvResetCat_f( void ) {
	int di, i;
	di    = SV_DiffSlot();

	/* Clear per-split data for all visible splits in this category */
	SV_GatherVisible();
	if ( sv_viewMode == LS_MODE_IL ) {
		for ( i = 0; i < sv_allVisN; i++ ) {
			SV_BackupCurrentLss( sv_allVis[i] );
		}
	} else {
		SV_BackupCurrentLss( -1 );
	}
	for ( i = 0; i < sv_allVisN; i++ ) {
		int si = sv_allVis[i];
		ls.splits[si].d[di].bestTimeMs      = 0;
		ls.splits[si].d[di].bestRealTimeMs  = 0;
		ls.splits[si].d[di].pbSegmentMs     = 0;
		ls.splits[si].d[di].pbRealSegmentMs = 0;
		ls.splits[si].d[di].totalAttempts   = 0;
		ls.splits[si].d[di].totalCompletions = 0;
	}

	/* Clear category-level stats */
	switch ( sv_viewMode ) {
	case LS_MODE_MISSION: {
		int gi = sv_viewMs - 1;
		if ( gi < 0 ) gi = 0;
		if ( gi >= LS_NUM_MISSION_GROUPS ) gi = LS_NUM_MISSION_GROUPS - 1;
		ls.msAttempts[gi][di]    = 0;
		ls.msCompletions[gi][di] = 0;
		ls.msPB[gi][di]          = 0;
		ls.msPBRgt[gi][di]       = 0;
		break;
	}
	case LS_MODE_IL:
		/* IL has no category-level stats beyond per-split */
		break;
	default:
		ls.fgAttempts[di]    = 0;
		ls.fgCompletions[di] = 0;
		ls.fgPB[di]          = 0;
		ls.fgPBRgt[di]       = 0;
		break;
	}
	SV_PurgeCurrentCategoryHistory();

	Com_Printf( "^2LiveSplit: Category stats reset\n" );
	if ( sv_viewMode == LS_MODE_IL ) {
		for ( i = 0; i < sv_allVisN; i++ ) {
			SV_SaveCurrentLss( sv_allVis[i] );
		}
	} else {
		SV_SaveCurrentLss( -1 );
	}
	SV_Refresh();
}

/* ---- Debug dump ---- */
static void LS_Debug_f( void ) {
	int i, di;
	const char *modeName;

	if ( !ls.initialized ) {
		Com_Printf( "^1LiveSplit: Not initialized\n" );
		return;
	}

	di = LS_CurDiffIdx();

	switch ( ls.runMode ) {
	case LS_MODE_MISSION: modeName = "Mission"; break;
	case LS_MODE_IL:      modeName = "IL"; break;
	default:              modeName = "Full Game"; break;
	}

	Com_Printf( "^2===== LiveSplit Debug Dump =====\n" );
	Com_Printf( "^2Mode: ^7%s  ^2Diff slot: ^7%d  ^2Active: ^7%s  ^2Finished: ^7%s\n",
			modeName, di, ls.active ? "yes" : "no", ls.runFinished ? "yes" : "no" );
	Com_Printf( "^2Range: ^7%d-%d  ^2EndMap: ^7%d  ^2CurMap: ^7%d\n",
			ls.modeFirstIdx, ls.modeLastIdx, ls.modeEndMapIdx, ls.currentMapIndex );

	{
		int *catPB;
		LS_GetCatPB( &catPB );
		Com_Printf( "^2Category PB: ^7%d ms  ^2savedPBMs: ^7%d ms\n",
				catPB ? *catPB : -1, ls.savedPBMs );
	}

	Com_Printf( "^2ls_compare: ^7%d\n",
			ls_compareCvar ? ls_compareCvar->integer : -1 );

	Com_Printf( "^2%-4s %-20s %10s %10s %10s %10s\n",
			"Idx", "Map", "bestTimeMs", "pbSegMs", "curTimeMs", "cmpSeg" );

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		const char *name;
		int cmpSeg;
		if ( ls.splits[i].cutscene ) continue;
		name = ls.splits[i].displayName ? ls.splits[i].displayName : ls.splits[i].mapname;

		cmpSeg = LS_ComparisonSegment( i );

		Com_Printf( "^7%-4d %-20s %10d %10d %10d %10d%s\n",
				i, name,
				ls.splits[i].d[di].bestTimeMs,
				ls.splits[i].d[di].pbSegmentMs,
				ls.splits[i].currentTimeMs,
				cmpSeg,
				ls.splits[i].splitDone ? " ^2DONE" : "" );
	}
	Com_Printf( "^2================================\n" );
}

static void SV_InitCvars( void ) {
	int i;
	char name[32];
	cvar_t *diffCvar;
	Cvar_Get( "ls_sv_title", "", 0 );
	Cvar_Get( "ls_sv_stats", "", 0 );
	Cvar_Get( "ls_sv_pages", "", 0 );
	diffCvar = Cvar_Get( "ls_sv_diff", "3", CVAR_ARCHIVE );
	sv_viewDiff = diffCvar ? diffCvar->integer : 3;
	if ( sv_viewDiff < 1 ) sv_viewDiff = 1;
	if ( sv_viewDiff > 3 ) sv_viewDiff = 3;
	Cvar_SetValue( "ls_sv_diff", sv_viewDiff );
	sv_viewVariant = Cvar_Get( "ls_sv_variant", "0", CVAR_ARCHIVE )->integer & 3;
	Cvar_Get( "ls_sv_pb", "---", 0 );
	Cvar_Get( "ls_sv_rgt_pb", "---", 0 );
	Cvar_Get( "ls_sv_sob", "---", 0 );
	Cvar_Get( "ls_sv_attempts", "0", 0 );
	Cvar_Get( "ls_sv_completions", "0", 0 );
	Cvar_Get( "ls_sv_completion_rate", "---", 0 );
	Cvar_Get( "ls_sv_golds", "0", 0 );
	Cvar_Get( "ls_sv_total_rows", "0", 0 );
	Cvar_Get( "ls_sv_pb_chart", "", 0 );
	Cvar_Get( "ls_sv_pb_chart_count", "0", 0 );
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
	Cmd_AddCommand( "livesplit_sv_variant",    LS_SvVariant_f );
	Cmd_AddCommand( "livesplit_sv_pgup",       LS_SvPagePrev_f );
	Cmd_AddCommand( "livesplit_sv_pgdn",       LS_SvPageNext_f );
	Cmd_AddCommand( "livesplit_sv_reset_gold", LS_SvResetGold_f );
	Cmd_AddCommand( "livesplit_sv_reset_pbseg", LS_SvResetPBSeg_f );
	Cmd_AddCommand( "livesplit_sv_reset_split", LS_SvResetSplitAll_f );
	Cmd_AddCommand( "livesplit_sv_reset_cat",  LS_SvResetCat_f );
}

/* =====================================================================
   Race / competition system
   ===================================================================== */
#define LS_RACE_MAX_PLAYERS       8
#define LS_RACE_PROTO_VERSION     2
#define LS_RACE_PACKET_MS         33
#define LS_RACE_ROSTER_MS         500
#define LS_RACE_TIMEOUT_MS        30000
#define LS_RACE_LOAD_TIMEOUT_MS   60000
#define LS_RACE_LOAD_RESEND_MS    500
#define LS_RACE_COUNTDOWN_RESEND_MS 250
#define LS_RACE_RESYNC_MS         1000
#define LS_RACE_PORT_SCAN_SPAN    10
#define LS_RACE_START_RESEND_MS   250
#define LS_RACE_START_RESEND_WINDOW_MS 5000
#define LS_RACE_GHOST_ALPHA       120
#define LS_RACE_GHOST_SNAP_DIST   256.0f
#define LS_RACE_GHOST_EXTRAPOLATE_MS 100
#define LS_RACE_GHOST_PUBLISH_MS  16
#define LS_RACE_CHAT_LINES        6
#define LS_RACE_CHAT_TEXT         128
#define LS_RACE_FOUND_MAX         4
#define LS_RACE_CHEAT_SV_CHEATS   1
#define LS_RACE_CHEAT_GOD         2
#define LS_RACE_CHEAT_NOCLIP      4

typedef enum {
	LS_RACE_ROLE_NONE = 0,
	LS_RACE_ROLE_HOST,
	LS_RACE_ROLE_CLIENT
} lsRaceRole_t;

typedef enum {
	LS_RACE_STATE_IDLE = 0,
	LS_RACE_STATE_CONNECTING,
	LS_RACE_STATE_LOBBY,
	LS_RACE_STATE_LOADING,
	LS_RACE_STATE_COUNTDOWN,
	LS_RACE_STATE_RACING,
	LS_RACE_STATE_FINISHED
} lsRaceState_t;

typedef struct {
	qboolean used;
	qboolean local;
	int      slot;
	netadr_t adr;
	int      basePort;
	char     nick[32];
	int      color[3];
	char     map[LS_MAX_MAPNAME];
	qboolean loaded;
	qboolean started;
	qboolean finished;
	qboolean paused;
	qboolean inMenu;
	qboolean left;
	qboolean timedOut;
	int      cheatFlags;
	int      timeMs;
	int      igtMs;
	int      stageTimeMs;
	int      stageProgress;
	char     stageName[32];
	int      crouched;
	int      health;
	int      armor;
	int      weapon;
	int      ammo;
	int      clip;
	int      legsAnim;
	int      torsoAnim;
	int      movementDir;
	int      eFlags;
	int      groundEntityNum;
	int      animMovetype;
	int      objectivesFound;
	int      objectivesTotal;
	int      zoneProgress;
	int      zoneTotal;
	float    x, y, z, yaw, speed, pitch;
	float    vx, vy, vz;
	float    renderX, renderY, renderZ, renderYaw, renderSpeed, renderPitch;
	float    smoothStartX, smoothStartY, smoothStartZ, smoothStartYaw, smoothStartSpeed, smoothStartPitch;
	int      smoothStartMs;
	int      smoothEndMs;
	int      lastSampleMs;
	int      lastHeardMs;
} lsRacePlayer_t;

typedef struct {
	int  timeMs;
	int  color[3];
	char nick[32];
	char text[LS_RACE_CHAT_TEXT];
} lsRaceChatLine_t;

typedef struct {
	qboolean used;
	netadr_t adr;
	int      session;
	int      state;
	int      players;
	int      maxPlayers;
	int      mode;
	int      mission;
	int      percent100;
	int      difficulty;
	int      hl1Movement;
	int      autoJump;
	int      antiCheat;
	int      privateLobby;
	int      queueCount;
	int      lastHeardMs;
	char     nick[32];
	char     ilMap[LS_MAX_MAPNAME];
} lsRaceFoundLobby_t;

typedef struct {
	qboolean initialized;
	lsRaceRole_t role;
	lsRaceState_t state;
	int session;
	int localSlot;
	netadr_t hostAdr;
	int hostBasePort;
	int lastSendMs;
	int lastRosterMs;
	int lastHelloMs;
	int lastHostPacketMs;
	int lastLoadBroadcastMs;
	int lastCountdownBroadcastMs;
	int lastStartBroadcastMs;
	int lastGhostPublishMs;
	int countdownStartMs;
	int countdownMs;
	int raceStartMs;
	qboolean playerStartIssued;
	qboolean playerStartProcessed;
	int antiCheat;
	int lastNoclipDisableMs;
	qboolean startIssued;
	qboolean pendingStart;
	qboolean loadIssued;
	int loadIssuedMs;
	int loadRetryCount;
	char loadIssuedMap[LS_MAX_MAPNAME];
	int mode;
	int mission;
	int percent100;
	int difficulty;
	int hl1Movement;
	int autoJump;
	char ilMap[LS_MAX_MAPNAME];
	char targetMap[LS_MAX_MAPNAME];
	lsRacePlayer_t players[LS_RACE_MAX_PLAYERS];
	lsRaceFoundLobby_t found[LS_RACE_FOUND_MAX];
	cvar_t *nickCvar;
	cvar_t *ipCvar;
	cvar_t *hostIpCvar;
	cvar_t *portCvar;
	cvar_t *passwordCvar;
	cvar_t *hideIpCvar;
	cvar_t *colorCvar;
	cvar_t *overlayCvar;
	cvar_t *countdownCvar;
	cvar_t *packetMsCvar;
	cvar_t *rosterMsCvar;
	cvar_t *antiCheatCvar;
	cvar_t *ghostsCvar;
	cvar_t *ghostAlphaCvar;
	cvar_t *zoneScoringCvar;
	cvar_t *activeCvar;
	cvar_t *roleCvar;
	cvar_t *stateCvar;
	cvar_t *statusCvar;
	cvar_t *playersCvar;
	cvar_t *timerCvar;
	cvar_t *stageCvar;
	cvar_t *stageIgtCvar;
	cvar_t *readyCvar;
	cvar_t *flagsCvar;
	cvar_t *countdownTextCvar;
	cvar_t *foundCountCvar;
	cvar_t *foundStatusCvar;
	cvar_t *foundCvars[LS_RACE_FOUND_MAX];
	cvar_t *ghostCvars[LS_RACE_MAX_PLAYERS];
	char     status[128];
	lsRaceChatLine_t chat[LS_RACE_CHAT_LINES];
	char     chatLines[LS_RACE_CHAT_LINES][LS_RACE_CHAT_TEXT];
	int      lastChatSlot;
	int      lastChatMs;
	char     lastChatNick[32];
	char     lastChatText[LS_RACE_CHAT_TEXT];
} lsRaceStateData_t;

typedef struct {
	qboolean valid;
	int mode;
	int mission;
	int percent100;
	int difficulty;
	int hl1Movement;
	int autoJump;
	int antiCheat;
	char ilMap[LS_MAX_MAPNAME];
} lsRaceSavedSettings_t;

static lsRaceStateData_t ls_race;
static lsRaceSavedSettings_t ls_raceSavedSettings;

static lsRacePlayer_t *LS_RaceFindPlayerBySlot( int slot );
static void LS_RaceLoadedCounts( int *loaded, int *total );
static int LS_RaceCountdownRemaining( int now );
static int LS_RaceAdrPort( netadr_t adr );
static void LS_RaceFormatCountdown( int remainMs, char *out, int outSize );
static void LS_RaceSendEventToHostScan( const char *kind, const char *detail );

static void LS_RaceFormatRejectReason( const char *reason, char *out, int outSize ) {
	int i;
	if ( !out || outSize <= 0 ) return;
	Q_strncpyz( out, reason && reason[0] ? reason : "Rejected", outSize );
	for ( i = 0; out[i]; ++i ) {
		if ( out[i] == '_' ) out[i] = ' ';
	}
}

static void LS_RaceSetCvarString( cvar_t *cv, const char *value ) {
	const char *safeValue = value ? value : "";
	if ( !cv ) return;
	if ( !cv->string || strcmp( cv->string, safeValue ) ) {
		Cvar_Set( cv->name, safeValue );
	}
}

static void LS_RaceSetCvarInt( cvar_t *cv, int value ) {
	char text[32];
	if ( !cv ) return;
	if ( cv->integer == value ) return;
	Com_sprintf( text, sizeof( text ), "%d", value );
	Cvar_Set( cv->name, text );
}

static void LS_RaceInitPlayerDefaults( lsRacePlayer_t *p ) {
	if ( !p ) return;
	p->legsAnim = -1;
	p->torsoAnim = -1;
	p->health = 100;
	p->groundEntityNum = ENTITYNUM_WORLD;
	p->animMovetype = 0;
}

static const char *LS_RaceRoleName( lsRaceRole_t role ) {
	switch ( role ) {
	case LS_RACE_ROLE_HOST: return "Host";
	case LS_RACE_ROLE_CLIENT: return "Client";
	default: return "Idle";
	}
}

static const char *LS_RaceStateName( lsRaceState_t state ) {
	switch ( state ) {
	case LS_RACE_STATE_CONNECTING: return "Connecting";
	case LS_RACE_STATE_LOBBY: return "Lobby";
	case LS_RACE_STATE_LOADING: return "Loading";
	case LS_RACE_STATE_COUNTDOWN: return "Countdown";
	case LS_RACE_STATE_RACING: return "Racing";
	case LS_RACE_STATE_FINISHED: return "Finished";
	default: return "Idle";
	}
}

static void LS_RaceSanitizeToken( const char *in, char *out, int outSize ) {
	int i, o = 0;
	if ( outSize <= 0 ) return;
	if ( !in || !in[0] ) in = "Player";
	for ( i = 0; in[i] && o < outSize - 1; i++ ) {
		unsigned char ch = (unsigned char)in[i];
		if ( ch <= ' ' || ch == '|' || ch == '\\' || ch == '"' || ch == ';' ) {
			out[o++] = '_';
		} else if ( ch >= 33 && ch <= 126 ) {
			out[o++] = (char)ch;
		}
	}
	out[o] = '\0';
	if ( !out[0] ) Q_strncpyz( out, "Player", outSize );
}

static void LS_RaceSanitizeOptionalToken( const char *in, char *out, int outSize ) {
	int i, o = 0;
	if ( outSize <= 0 ) return;
	if ( !in ) in = "";
	for ( i = 0; in[i] && o < outSize - 1; i++ ) {
		unsigned char ch = (unsigned char)in[i];
		if ( ch <= ' ' || ch == '|' || ch == '\\' || ch == '"' || ch == ';' ) {
			out[o++] = '_';
		} else if ( ch >= 33 && ch <= 126 ) {
			out[o++] = (char)ch;
		}
	}
	out[o] = '\0';
}

static void LS_RaceReadColorCvar( int color[3] ) {
	int r = 80, g = 180, b = 255;
	float a;
	const char *s = ls_race.colorCvar ? ls_race.colorCvar->string : NULL;
	if ( s && s[0] ) {
		if ( sscanf( s, "%d %d %d %f", &r, &g, &b, &a ) < 3 ) {
			r = 80; g = 180; b = 255;
		}
	}
	color[0] = (int)Com_Clamp( 0.0f, 255.0f, (float)r );
	color[1] = (int)Com_Clamp( 0.0f, 255.0f, (float)g );
	color[2] = (int)Com_Clamp( 0.0f, 255.0f, (float)b );
}

static void LS_RaceGetLocalIdentity( char *nick, int nickSize, int color[3] ) {
	LS_RaceSanitizeToken( ls_race.nickCvar ? ls_race.nickCvar->string : "Player", nick, nickSize );
	LS_RaceReadColorCvar( color );
}

static void LS_RaceAdrToHostString( netadr_t adr, char *out, int outSize ) {
	const char *text;
	char *portSep;
	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	if ( adr.type == NA_IP ) {
		Com_sprintf( out, outSize, "%i.%i.%i.%i", adr.ip[0], adr.ip[1], adr.ip[2], adr.ip[3] );
		return;
	}
	text = NET_AdrToString( adr );
	Q_strncpyz( out, text, outSize );
	portSep = strrchr( out, ':' );
	if ( portSep ) *portSep = '\0';
}

static int LS_RaceClampDifficulty( int difficulty ) {
	if ( difficulty < 1 ) difficulty = 1;
	if ( difficulty > 3 ) difficulty = 3;
	return difficulty;
}

static void LS_RaceBuildCategoryLabelFor( int mode, int mission, int percent100, int difficulty, int hl1Movement, int autoJump, const char *ilMap, char *out, int outSize ) {
	int mapIdx;
	const char *diffTag;
	const char *moveTag;
	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	diffTag = ls_diffShortTags[LS_DiffIdx( LS_RaceClampDifficulty( difficulty ) )];
	moveTag = hl1Movement ? ( autoJump ? " HL1+AJ" : " HL1" ) : "";
	if ( mode == LS_MODE_IL ) {
		mapIdx = LS_FindMapIndex( ilMap && ilMap[0] ? ilMap : "escape1" );
		Com_sprintf( out, outSize, "%s %s [%s]%s", ( mapIdx >= 0 ) ? ls.splits[mapIdx].shortName : "IL", percent100 ? "100%" : "Any%", diffTag, moveTag );
	} else if ( mode == LS_MODE_MISSION ) {
		Com_sprintf( out, outSize, "Mission %d %s [%s]%s", mission, percent100 ? "100%" : "Any%", diffTag, moveTag );
	} else {
		Com_sprintf( out, outSize, "Full Game %s [%s]%s", percent100 ? "100%" : "Any%", diffTag, moveTag );
	}
}

static void LS_RaceUpdateFoundCvars( void ) {
	int i, count = 0;
	qboolean hideIp = ( ls_race.hideIpCvar && ls_race.hideIpCvar->integer ) ? qtrue : qfalse;
	char display[128];
	char address[64];
	char category[64];
	char access[32];

	for ( i = 0; i < LS_RACE_FOUND_MAX; i++ ) {
		if ( !ls_race.found[i].used ) {
			LS_RaceSetCvarString( ls_race.foundCvars[i], "" );
			continue;
		}
		count++;
		LS_RaceAdrToHostString( ls_race.found[i].adr, address, sizeof( address ) );
		LS_RaceBuildCategoryLabelFor( ls_race.found[i].mode, ls_race.found[i].mission, ls_race.found[i].percent100,
			ls_race.found[i].difficulty, ls_race.found[i].hl1Movement, ls_race.found[i].autoJump,
			ls_race.found[i].ilMap, category, sizeof( category ) );
		Com_sprintf( access, sizeof( access ), "%s%s%d/%d",
			ls_race.found[i].privateLobby ? "private | " : "",
			ls_race.found[i].queueCount > 0 ? va( "+%dq | ", ls_race.found[i].queueCount ) : "",
			ls_race.found[i].players, ls_race.found[i].maxPlayers );
		if ( hideIp ) {
			Com_sprintf( display, sizeof( display ), "%d. Address hidden | %s | %s | %s", i + 1,
				ls_race.found[i].nick[0] ? ls_race.found[i].nick : "Host",
				category,
				access );
		} else {
			Com_sprintf( display, sizeof( display ), "%d. %s:%d | %s | %s | %s", i + 1, address, LS_RaceAdrPort( ls_race.found[i].adr ),
				ls_race.found[i].nick[0] ? ls_race.found[i].nick : "Host",
				category,
				access );
		}
		LS_RaceSetCvarString( ls_race.foundCvars[i], display );
	}
	LS_RaceSetCvarInt( ls_race.foundCountCvar, count );
	if ( ls_race.foundStatusCvar ) {
		if ( count > 0 ) LS_RaceSetCvarString( ls_race.foundStatusCvar, va( "Found %d Race lobby%s", count, count == 1 ? "" : "s" ) );
		else LS_RaceSetCvarString( ls_race.foundStatusCvar, "No Race lobbies found yet" );
	}
}

static void LS_RaceClearFoundLobbies( const char *status ) {
	memset( ls_race.found, 0, sizeof( ls_race.found ) );
	LS_RaceUpdateFoundCvars();
	if ( status ) LS_RaceSetCvarString( ls_race.foundStatusCvar, status );
}

static void LS_RaceSetStatus( const char *status ) {
	Q_strncpyz( ls_race.status, status ? status : "", sizeof( ls_race.status ) );
	LS_RaceSetCvarString( ls_race.statusCvar, ls_race.status );
}

static int LS_RaceLocalCheatFlags( void ) {
	int flags = 0;
	if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) flags |= LS_RACE_CHEAT_SV_CHEATS;
	if ( Cvar_VariableIntegerValue( "ls_godmode" ) ) flags |= LS_RACE_CHEAT_GOD;
	if ( cls.state >= CA_ACTIVE && cl.snap.ps.pm_type == PM_NOCLIP ) flags |= LS_RACE_CHEAT_NOCLIP;
	return flags;
}

static void LS_RaceCheatFlagsText( int flags, char *out, int outSize ) {
	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	if ( flags & LS_RACE_CHEAT_SV_CHEATS ) { if ( out[0] ) Q_strcat( out, outSize, "," ); Q_strcat( out, outSize, "sv_cheats" ); }
	if ( flags & LS_RACE_CHEAT_GOD ) { if ( out[0] ) Q_strcat( out, outSize, "," ); Q_strcat( out, outSize, "god" ); }
	if ( flags & LS_RACE_CHEAT_NOCLIP ) { if ( out[0] ) Q_strcat( out, outSize, "," ); Q_strcat( out, outSize, "noclip" ); }
	if ( !out[0] ) Q_strncpyz( out, "none", outSize );
}

static void LS_RaceBuildFlags( char *out, int outSize ) {
	int flags;
	if ( !out || outSize <= 0 ) return;
	flags = LS_RaceLocalCheatFlags();
	Com_sprintf( out, outSize, "%s | sv_cheats %d | god %s | noclip %s",
		ls_race.antiCheat ? "anti-cheat on" : "anti-cheat off",
		( flags & LS_RACE_CHEAT_SV_CHEATS ) ? 1 : 0,
		( flags & LS_RACE_CHEAT_GOD ) ? "ON" : "off",
		( flags & LS_RACE_CHEAT_NOCLIP ) ? "ON" : "off" );
}

static void LS_RaceSetZeroIfEnabled( const char *name ) {
	if ( name && name[0] && Cvar_VariableIntegerValue( name ) ) {
		Cvar_Set( name, "0" );
	}
}

static void LS_RaceSetNamedCvarString( const char *name, const char *value ) {
	char current[MAX_CVAR_VALUE_STRING];
	const char *safeValue = value ? value : "";
	if ( !name || !name[0] ) return;
	Cvar_VariableStringBuffer( name, current, sizeof( current ) );
	if ( strcmp( current, safeValue ) ) {
		Cvar_Set( name, safeValue );
	}
}

static void LS_RaceRestoreGameplayRenderCvars( void ) {
	LS_RaceSetNamedCvarString( "cl_freecamActive", "0" );
	LS_RaceSetNamedCvarString( "cg_thirdPerson", "0" );
	LS_RaceSetNamedCvarString( "r_drawworld", "1" );
	LS_RaceSetNamedCvarString( "r_novis", "0" );
	LS_RaceSetNamedCvarString( "r_zfar", "0" );
	LS_RaceSetNamedCvarString( "r_wolffog", "1" );
}

static void LS_RaceDisableProtectedOptions( void ) {
	static const char *protectedCvars[] = {
		"cg_drawTriggers", "cg_drawEnemies", "cg_drawItems", "cg_drawEnemySight", "cg_drawAIPath",
		"cg_explosiveTimers", "r_drawClips", "g_triggerLog", "sp_zone_draw", "sp_zone_edit", "sp_zone_race_debug",
		"ls_godmode"
	};
	static int lastReportedFlags = 0;
	static int lastReportMs = 0;
	int i, now, flags, newlyFlagged;
	char cheatText[64];
	if ( !ls_race.antiCheat ) {
		ls_race.lastNoclipDisableMs = 0;
		lastReportedFlags = 0;
		return;
	}
	now = Sys_Milliseconds();
	flags = LS_RaceLocalCheatFlags();
	newlyFlagged = flags & ~lastReportedFlags;
	if ( flags && ( newlyFlagged || now - lastReportMs > 5000 ) ) {
		LS_RaceCheatFlagsText( flags, cheatText, sizeof( cheatText ) );
		LS_RaceSendEventToHostScan( "cheat", cheatText );
		lastReportedFlags = flags;
		lastReportMs = now;
	} else if ( !flags ) {
		lastReportedFlags = 0;
	}
	if ( cls.state >= CA_ACTIVE && cl.snap.ps.pm_type == PM_NOCLIP ) {
		if ( !ls_race.lastNoclipDisableMs ) {
			Cbuf_AddText( "noclip\n" );
			ls_race.lastNoclipDisableMs = Sys_Milliseconds();
		}
	} else {
		ls_race.lastNoclipDisableMs = 0;
	}
	for ( i = 0; i < (int)( sizeof( protectedCvars ) / sizeof( protectedCvars[0] ) ); i++ ) {
		LS_RaceSetZeroIfEnabled( protectedCvars[i] );
	}
	LS_RaceRestoreGameplayRenderCvars();
	LS_RaceSetZeroIfEnabled( "sv_cheats" );
}

static void LS_RaceSanitizeChatText( const char *in, char *out, int outSize ) {
	int i, o = 0;
	if ( !out || outSize <= 0 ) return;
	out[0] = '\0';
	if ( !in ) return;
	while ( *in == ' ' ) in++;
	for ( i = 0; in[i] && o < outSize - 1; i++ ) {
		unsigned char ch = (unsigned char)in[i];
		if ( ch == '\r' || ch == '\n' || ch == ';' || ch == '"' || ch == '\\' ) continue;
		if ( ch == '|' ) ch = '/';
		if ( ch >= 32 && ch <= 126 ) out[o++] = (char)ch;
	}
	while ( o > 0 && out[o - 1] == ' ' ) o--;
	out[o] = '\0';
}

static void LS_RaceClearChat( void ) {
	memset( ls_race.chat, 0, sizeof( ls_race.chat ) );
	memset( ls_race.chatLines, 0, sizeof( ls_race.chatLines ) );
	ls_race.lastChatSlot = -1;
	ls_race.lastChatMs = 0;
	ls_race.lastChatNick[0] = '\0';
	ls_race.lastChatText[0] = '\0';
}

static qboolean LS_RaceDropDuplicateChatLine( int slot, const char *nick, const char *text ) {
	int now;
	char cleanNick[32];
	char cleanText[LS_RACE_CHAT_TEXT];

	LS_RaceSanitizeToken( nick && nick[0] ? nick : "Runner", cleanNick, sizeof( cleanNick ) );
	LS_RaceSanitizeChatText( text, cleanText, sizeof( cleanText ) );
	if ( !cleanText[0] ) return qtrue;
	now = Sys_Milliseconds();
	if ( ls_race.lastChatMs > 0 && now - ls_race.lastChatMs < 600 &&
		 ls_race.lastChatSlot == slot &&
		 !Q_stricmp( ls_race.lastChatNick, cleanNick ) &&
		 !Q_stricmp( ls_race.lastChatText, cleanText ) ) {
		return qtrue;
	}
	ls_race.lastChatSlot = slot;
	ls_race.lastChatMs = now;
	Q_strncpyz( ls_race.lastChatNick, cleanNick, sizeof( ls_race.lastChatNick ) );
	Q_strncpyz( ls_race.lastChatText, cleanText, sizeof( ls_race.lastChatText ) );
	return qfalse;
}

static void LS_RaceChatColorForSender( int slot, const char *nick, int color[3] ) {
	lsRacePlayer_t *p;
	int i;
	color[0] = 188;
	color[1] = 226;
	color[2] = 168;
	p = LS_RaceFindPlayerBySlot( slot );
	if ( p ) {
		color[0] = p->color[0];
		color[1] = p->color[1];
		color[2] = p->color[2];
		return;
	}
	if ( !nick || !nick[0] ) return;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		p = &ls_race.players[i];
		if ( !p->used ) continue;
		if ( Q_stricmp( p->nick, nick ) ) continue;
		color[0] = p->color[0];
		color[1] = p->color[1];
		color[2] = p->color[2];
		return;
	}
}

static void LS_RaceAddChatLine( int slot, const char *nick, const char *text ) {
	int i;
	char cleanNick[32];
	char cleanText[LS_RACE_CHAT_TEXT];
	char line[LS_RACE_CHAT_TEXT];
	int color[3];
	LS_RaceSanitizeToken( nick && nick[0] ? nick : "Runner", cleanNick, sizeof( cleanNick ) );
	LS_RaceSanitizeChatText( text, cleanText, sizeof( cleanText ) );
	if ( !cleanText[0] ) return;
	LS_RaceChatColorForSender( slot, cleanNick, color );
	Com_sprintf( line, sizeof( line ), "%s: %s", cleanNick, cleanText );
	for ( i = LS_RACE_CHAT_LINES - 1; i > 0; i-- ) {
		ls_race.chat[i] = ls_race.chat[i - 1];
		Q_strncpyz( ls_race.chatLines[i], ls_race.chatLines[i - 1], sizeof( ls_race.chatLines[i] ) );
	}
	memset( &ls_race.chat[0], 0, sizeof( ls_race.chat[0] ) );
	ls_race.chat[0].timeMs = Sys_Milliseconds();
	ls_race.chat[0].color[0] = color[0];
	ls_race.chat[0].color[1] = color[1];
	ls_race.chat[0].color[2] = color[2];
	Q_strncpyz( ls_race.chat[0].nick, cleanNick, sizeof( ls_race.chat[0].nick ) );
	Q_strncpyz( ls_race.chat[0].text, cleanText, sizeof( ls_race.chat[0].text ) );
	Q_strncpyz( ls_race.chatLines[0], line, sizeof( ls_race.chatLines[0] ) );
}

static qboolean LS_RaceBlockTimerReset( const char *action ) {
	if ( ls_race.initialized && ls_race.role != LS_RACE_ROLE_NONE ) {
		const char *verb = action && action[0] ? action : "reset the timer";
		Com_Printf( "^3Race: leave the race before you %s. Use /ls_race_leave first.\n", verb );
		LS_RaceSetStatus( "Leave race before resetting timer" );
		return qtrue;
	}
	return qfalse;
}

static qboolean LS_RaceBlocksTimerStart( void ) {
	return ( ls_race.initialized && ls_race.role != LS_RACE_ROLE_NONE &&
		ls_race.state >= LS_RACE_STATE_LOADING && ls_race.state < LS_RACE_STATE_RACING ) ? qtrue : qfalse;
}

static int LS_RaceClampedCvarInt( cvar_t *cv, int fallback, int minValue, int maxValue ) {
	int value = cv ? cv->integer : fallback;
	if ( value < minValue ) value = minValue;
	if ( value > maxValue ) value = maxValue;
	return value;
}

static int LS_RacePacketIntervalMs( void ) {
	return LS_RaceClampedCvarInt( ls_race.packetMsCvar, LS_RACE_PACKET_MS, 16, 250 );
}

static int LS_RaceRosterIntervalMs( void ) {
	return LS_RaceClampedCvarInt( ls_race.rosterMsCvar, LS_RACE_ROSTER_MS, 33, 1000 );
}

static int LS_RaceGhostAlpha( void ) {
	return LS_RaceClampedCvarInt( ls_race.ghostAlphaCvar, LS_RACE_GHOST_ALPHA, 0, 255 );
}

static float LS_RaceClampFloat( float value, float minValue, float maxValue ) {
	if ( value < minValue ) return minValue;
	if ( value > maxValue ) return maxValue;
	return value;
}

static float LS_RaceAngleDelta( float from, float to ) {
	float delta = to - from;
	while ( delta > 180.0f ) delta -= 360.0f;
	while ( delta < -180.0f ) delta += 360.0f;
	return delta;
}

static void LS_RaceSnapPlayerRender( lsRacePlayer_t *p, int now ) {
	if ( !p ) return;
	p->renderX = p->smoothStartX = p->x;
	p->renderY = p->smoothStartY = p->y;
	p->renderZ = p->smoothStartZ = p->z;
	p->renderYaw = p->smoothStartYaw = p->yaw;
	p->renderPitch = p->smoothStartPitch = p->pitch;
	p->renderSpeed = p->smoothStartSpeed = p->speed;
	p->smoothStartMs = now;
	p->smoothEndMs = now;
	p->lastSampleMs = now;
}

static void LS_RaceUpdatePlayerRender( lsRacePlayer_t *p, int now ) {
	float frac;
	int duration;
	if ( !p ) return;
	if ( p->local || !p->lastSampleMs || p->smoothEndMs <= p->smoothStartMs ) {
		p->renderX = p->x;
		p->renderY = p->y;
		p->renderZ = p->z;
		p->renderYaw = p->yaw;
		p->renderPitch = p->pitch;
		p->renderSpeed = p->speed;
		return;
	}
	duration = p->smoothEndMs - p->smoothStartMs;
	frac = (float)( now - p->smoothStartMs ) / (float)duration;
	frac = LS_RaceClampFloat( frac, 0.0f, 1.0f );
	p->renderX = p->smoothStartX + ( p->x - p->smoothStartX ) * frac;
	p->renderY = p->smoothStartY + ( p->y - p->smoothStartY ) * frac;
	p->renderZ = p->smoothStartZ + ( p->z - p->smoothStartZ ) * frac;
	p->renderYaw = p->smoothStartYaw + LS_RaceAngleDelta( p->smoothStartYaw, p->yaw ) * frac;
	p->renderPitch = p->smoothStartPitch + LS_RaceAngleDelta( p->smoothStartPitch, p->pitch ) * frac;
	p->renderSpeed = p->smoothStartSpeed + ( p->speed - p->smoothStartSpeed ) * frac;
	if ( now > p->smoothEndMs && now - p->smoothEndMs <= LS_RACE_GHOST_EXTRAPOLATE_MS ) {
		float extra = (float)( now - p->smoothEndMs ) * 0.001f;
		p->renderX += p->vx * extra;
		p->renderY += p->vy * extra;
		p->renderZ += p->vz * extra;
	}
}

static void LS_RaceStartPlayerSmooth( lsRacePlayer_t *p, int now ) {
	float dx, dy, dz;
	int interval;
	if ( !p || p->local ) return;
	if ( !p->lastSampleMs ) {
		LS_RaceSnapPlayerRender( p, now );
		return;
	}
	dx = p->x - p->renderX;
	dy = p->y - p->renderY;
	dz = p->z - p->renderZ;
	if ( dx * dx + dy * dy + dz * dz > LS_RACE_GHOST_SNAP_DIST * LS_RACE_GHOST_SNAP_DIST ) {
		LS_RaceSnapPlayerRender( p, now );
		return;
	}
	interval = now - p->lastSampleMs;
	if ( interval < 33 ) interval = 33;
	if ( interval > 200 ) interval = 200;
	p->smoothStartX = p->renderX;
	p->smoothStartY = p->renderY;
	p->smoothStartZ = p->renderZ;
	p->smoothStartYaw = p->renderYaw;
	p->smoothStartPitch = p->renderPitch;
	p->smoothStartSpeed = p->renderSpeed;
	p->smoothStartMs = now;
	p->smoothEndMs = now + interval;
	p->lastSampleMs = now;
}

static int LS_RaceSafePlayerStat( const int *values, int index ) {
	if ( index < 0 || index >= MAX_WEAPONS ) return 0;
	return values[index];
}

static int LS_RaceAmmoIndexForWeapon( int weapon ) {
	if ( weapon < 0 || weapon >= WP_NUM_WEAPONS ) return 0;
	switch ( weapon ) {
	case WP_MP40:
	case WP_STEN:
	case WP_SILENCER:
		return WP_LUGER;
	case WP_THOMPSON:
	case WP_AKIMBO:
		return WP_COLT;
	case WP_FG42:
	case WP_SNIPERRIFLE:
	case WP_FG42SCOPE:
		return WP_MAUSER;
	case WP_SNOOPERSCOPE:
		return WP_GARAND;
	default:
		return weapon;
	}
}

static int LS_RaceClipIndexForWeapon( int weapon ) {
	if ( weapon < 0 || weapon >= WP_NUM_WEAPONS ) return 0;
	switch ( weapon ) {
	case WP_SILENCER:
		return WP_LUGER;
	case WP_SNIPERRIFLE:
		return WP_MAUSER;
	case WP_SNOOPERSCOPE:
		return WP_GARAND;
	case WP_FG42SCOPE:
		return WP_FG42;
	default:
		return weapon;
	}
}

static int LS_RaceNormalizeMovementDir( int movementDir ) {
	if ( movementDir > 128 && movementDir <= 255 ) movementDir -= 256;
	if ( movementDir < -128 || movementDir > 128 ) movementDir = 0;
	return movementDir;
}

static int LS_RaceAnimMovetypeForPlayerState( const playerState_t *ps ) {
	float speed;
	vec3_t hvel;
	if ( !ps ) return ANIM_MT_IDLE;
	hvel[0] = ps->velocity[0];
	hvel[1] = ps->velocity[1];
	hvel[2] = 0.0f;
	speed = VectorLength( hvel );
	if ( ps->pm_flags & PMF_DUCKED ) return speed > 5.0f ? ANIM_MT_WALKCR : ANIM_MT_IDLECR;
	if ( ps->pm_flags & PMF_LADDER ) return speed > 0.0f ? ANIM_MT_CLIMBUP : ANIM_MT_IDLE;
	if ( speed > 127.0f ) return ( ps->pm_flags & PMF_BACKWARDS_RUN ) ? ANIM_MT_RUNBK : ANIM_MT_RUN;
	if ( speed > 5.0f ) return ( ps->pm_flags & PMF_BACKWARDS_RUN ) ? ANIM_MT_WALKBK : ANIM_MT_WALK;
	return ANIM_MT_IDLE;
}

static int LS_RaceCombinedPoints( const lsRacePlayer_t *p ) {
	if ( !p ) return 0;
	return p->objectivesFound + p->zoneProgress;
}

static int LS_RaceElapsedMs( int now ) {
	if ( !ls_race.raceStartMs || ls_race.state < LS_RACE_STATE_RACING ) return 0;
	if ( now < ls_race.raceStartMs ) return 0;
	return now - ls_race.raceStartMs;
}

static int LS_RaceStageProgressForIndex( int stageIndex ) {
	int mapIndex, progress = 0;
	if ( stageIndex < 0 || stageIndex >= ls.numMaps ) return 0;
	for ( mapIndex = ls.modeFirstIdx; mapIndex <= ls.modeLastIdx && mapIndex < ls.numMaps; mapIndex++ ) {
		if ( ls.splits[mapIndex].cutscene ) continue;
		progress++;
		if ( mapIndex == stageIndex ) return progress;
	}
	return 0;
}

static int LS_RaceStageCount( void ) {
	int mapIndex, count = 0;
	for ( mapIndex = ls.modeFirstIdx; mapIndex <= ls.modeLastIdx && mapIndex < ls.numMaps; mapIndex++ ) {
		if ( !ls.splits[mapIndex].cutscene ) count++;
	}
	return count;
}

static void LS_RaceStageNameForIndex( int stageIndex, char *out, int outSize ) {
	const char *name;
	char clean[32];
	if ( !out || outSize <= 0 ) return;
	if ( stageIndex < 0 || stageIndex >= ls.numMaps ) {
		Q_strncpyz( out, "-", outSize );
		return;
	}
	name = ls.splits[stageIndex].shortName && ls.splits[stageIndex].shortName[0] ? ls.splits[stageIndex].shortName : ls.splits[stageIndex].mapname;
	LS_RaceSanitizeToken( name, clean, sizeof( clean ) );
	Q_strncpyz( out, clean, outSize );
}

static void LS_RaceBuildCategoryLabel( char *out, int outSize ) {
	const char *pctTag;
	const char *moveTag;
	const char *diffTag;
	int mapIndex;
	int missionGroup;
	if ( !out || outSize <= 0 ) return;
	pctTag = ls_race.percent100 ? "100%" : "Any%";
	diffTag = ls_diffShortTags[LS_DiffIdx( LS_RaceClampDifficulty( ls_race.difficulty ) )];
	moveTag = ls_race.hl1Movement ? ( ls_race.autoJump ? " HL1+AJ" : " HL1" ) : "";
	switch ( ls_race.mode ) {
	case LS_MODE_MISSION:
		missionGroup = ls_race.mission - 1;
		if ( missionGroup >= 0 && missionGroup < LS_NUM_MISSION_GROUPS ) {
			Com_sprintf( out, outSize, "Chapter: %s %s [%s]%s", ls_missionGroups[missionGroup].name, pctTag, diffTag, moveTag );
		} else {
			Com_sprintf( out, outSize, "Chapter %s [%s]%s", pctTag, diffTag, moveTag );
		}
		break;
	case LS_MODE_IL:
		mapIndex = LS_FindMapIndex( ls_race.ilMap );
		if ( mapIndex >= 0 && mapIndex < ls.numMaps ) {
			const char *name = ls.splits[mapIndex].displayName && ls.splits[mapIndex].displayName[0] ? ls.splits[mapIndex].displayName : ls.splits[mapIndex].mapname;
			Com_sprintf( out, outSize, "IL: %s %s [%s]%s", name, pctTag, diffTag, moveTag );
		} else {
			Com_sprintf( out, outSize, "IL %s [%s]%s", pctTag, diffTag, moveTag );
		}
		break;
	default:
		Com_sprintf( out, outSize, "Full Game %s [%s]%s", pctTag, diffTag, moveTag );
		break;
	}
}

static int LS_RaceCurrentIgtMs( void ) {
	if ( ls.runFinished ) return ls.runTotalIGTMs;
	if ( ls.active ) return LS_CumulativeTime( ls.modeLastIdx );
	return 0;
}

static void LS_RaceUpdateStageFields( lsRacePlayer_t *p ) {
	int stageIndex = -1;
	if ( !p ) return;
	if ( ls.active || ls.runFinished ) stageIndex = LS_ActiveRealSplit();
	if ( stageIndex < 0 && p->map[0] ) stageIndex = LS_FindMapIndex( p->map );
	if ( stageIndex >= 0 ) {
		LS_RaceStageNameForIndex( stageIndex, p->stageName, sizeof( p->stageName ) );
		p->stageProgress = LS_RaceStageProgressForIndex( stageIndex );
		p->stageTimeMs = ls.active || ls.runFinished ? ls.splits[stageIndex].currentTimeMs : 0;
	} else {
		Q_strncpyz( p->stageName, p->map[0] ? p->map : "-", sizeof( p->stageName ) );
		p->stageProgress = 0;
		p->stageTimeMs = 0;
	}
	p->igtMs = LS_RaceCurrentIgtMs();
}

static int LS_RaceComparePlayers( const lsRacePlayer_t *a, const lsRacePlayer_t *b ) {
	int timeDelta;
	if ( a->finished != b->finished ) return a->finished ? -1 : 1;
	if ( a->finished && b->finished && a->timeMs != b->timeMs ) return a->timeMs - b->timeMs;
	if ( a->stageProgress != b->stageProgress ) return b->stageProgress - a->stageProgress;
	if ( a->objectivesFound != b->objectivesFound ) return b->objectivesFound - a->objectivesFound;
	if ( a->zoneProgress != b->zoneProgress ) return b->zoneProgress - a->zoneProgress;
	if ( Q_stricmp( a->map, b->map ) ) return Q_stricmp( a->map, b->map );
	timeDelta = a->timeMs - b->timeMs;
	if ( timeDelta <= -1500 || timeDelta >= 1500 ) return timeDelta;
	return a->slot - b->slot;
}

static void LS_RaceBuildSortedPlayers( int sorted[LS_RACE_MAX_PLAYERS], int *count ) {
	int playerIndex, sortIndex, nextIndex;
	*count = 0;
	for ( playerIndex = 0; playerIndex < LS_RACE_MAX_PLAYERS; playerIndex++ ) {
		if ( ls_race.players[playerIndex].used ) sorted[( *count )++] = playerIndex;
	}
	for ( sortIndex = 0; sortIndex < *count - 1; sortIndex++ ) {
		for ( nextIndex = sortIndex + 1; nextIndex < *count; nextIndex++ ) {
			lsRacePlayer_t *left = &ls_race.players[sorted[sortIndex]];
			lsRacePlayer_t *right = &ls_race.players[sorted[nextIndex]];
			if ( LS_RaceComparePlayers( left, right ) > 0 ) {
				int tmp = sorted[sortIndex];
				sorted[sortIndex] = sorted[nextIndex];
				sorted[nextIndex] = tmp;
			}
		}
	}
}

static qboolean LS_RacePlayerBlocksReady( const lsRacePlayer_t *p ) {
	if ( !p || !p->used ) return qfalse;
	if ( p->left || p->timedOut ) return qfalse;
	return qtrue;
}

static const char *LS_RacePlayerStateText( const lsRacePlayer_t *p ) {
	if ( !p ) return "LOAD";
	if ( p->left ) return "LEFT";
	if ( p->timedOut ) return "TIMEOUT";
	if ( p->finished ) return "FINISH";
	if ( p->started && p->health <= 0 ) return "DEAD";
	if ( p->inMenu ) return "MENU";
	if ( p->started && p->paused ) return "PAUSE";
	if ( p->started ) return "RUN";
	if ( p->loaded ) return "READY";
	return "LOAD";
}

static void LS_RaceUpdateRuntimeCvars( void ) {
	int count = 0, sorted[LS_RACE_MAX_PLAYERS];
	char status[128];
	char flags[96];
	if ( !ls_race.initialized ) return;
	LS_RaceSetCvarString( ls_race.activeCvar, ls_race.role == LS_RACE_ROLE_NONE ? "0" : "1" );
	LS_RaceSetCvarString( ls_race.roleCvar, LS_RaceRoleName( ls_race.role ) );
	LS_RaceSetCvarString( ls_race.stateCvar, LS_RaceStateName( ls_race.state ) );
	LS_RaceBuildFlags( flags, sizeof( flags ) );
	LS_RaceSetCvarString( ls_race.flagsCvar, flags );
	LS_RaceBuildSortedPlayers( sorted, &count );
	LS_RaceSetCvarInt( ls_race.playersCvar, count );
	if ( ls_race.countdownTextCvar ) {
		char countdownText[32];
		countdownText[0] = '\0';
		if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
			LS_RaceFormatCountdown( LS_RaceCountdownRemaining( Sys_Milliseconds() ), countdownText, sizeof( countdownText ) );
		}
		LS_RaceSetCvarString( ls_race.countdownTextCvar, countdownText );
	}
	if ( ls_race.statusCvar ) {
		if ( ls_race.status[0] ) Q_strncpyz( status, ls_race.status, sizeof( status ) );
		else Com_sprintf( status, sizeof( status ), "%s / %s", LS_RaceRoleName( ls_race.role ), LS_RaceStateName( ls_race.state ) );
		LS_RaceSetCvarString( ls_race.statusCvar, status );
	}
	{
		lsRacePlayer_t *local = LS_RaceFindPlayerBySlot( ls_race.localSlot );
		int loaded = 0, total = 0;
		char value[64];
		LS_RaceLoadedCounts( &loaded, &total );
		if ( ls_race.readyCvar ) {
			Com_sprintf( value, sizeof( value ), "%d/%d", loaded, total );
			LS_RaceSetCvarString( ls_race.readyCvar, value );
		}
		if ( local ) {
			if ( local->timeMs > 0 ) LS_FormatTime( local->timeMs, value, sizeof( value ) );
			else Q_strncpyz( value, "0.00", sizeof( value ) );
			LS_RaceSetCvarString( ls_race.timerCvar, value );
			LS_RaceSetCvarString( ls_race.stageCvar, local->stageName[0] ? local->stageName : "-" );
			if ( local->stageTimeMs > 0 ) LS_FormatTime( local->stageTimeMs, value, sizeof( value ) );
			else Q_strncpyz( value, "--", sizeof( value ) );
			LS_RaceSetCvarString( ls_race.stageIgtCvar, value );
		} else {
			LS_RaceSetCvarString( ls_race.timerCvar, "0.00" );
			LS_RaceSetCvarString( ls_race.stageCvar, "-" );
			LS_RaceSetCvarString( ls_race.stageIgtCvar, "--" );
		}
	}
}

void LS_RaceBuildSnapshot( lsRaceUiSnapshot_t *out ) {
	int i, count = 0, sorted[LS_RACE_MAX_PLAYERS];
	int loaded = 0, total = 0;
	char value[64];
	lsRacePlayer_t *local;
	if ( !out ) return;
	memset( out, 0, sizeof( *out ) );
	Q_strncpyz( out->role, "Idle", sizeof( out->role ) );
	Q_strncpyz( out->state, "Idle", sizeof( out->state ) );
	Q_strncpyz( out->category, "Full Game", sizeof( out->category ) );
	Q_strncpyz( out->status, "", sizeof( out->status ) );
	Q_strncpyz( out->timer, "0.00", sizeof( out->timer ) );
	Q_strncpyz( out->stage, "-", sizeof( out->stage ) );
	Q_strncpyz( out->stageIgt, "--", sizeof( out->stageIgt ) );
	Q_strncpyz( out->ready, "0/0", sizeof( out->ready ) );
	if ( !ls_race.initialized ) return;
	out->active = ls_race.role == LS_RACE_ROLE_NONE ? 0 : 1;
	out->localCheatFlags = LS_RaceLocalCheatFlags();
	Q_strncpyz( out->role, LS_RaceRoleName( ls_race.role ), sizeof( out->role ) );
	Q_strncpyz( out->state, LS_RaceStateName( ls_race.state ), sizeof( out->state ) );
	LS_RaceBuildCategoryLabel( out->category, sizeof( out->category ) );
	if ( ls_race.status[0] ) Q_strncpyz( out->status, ls_race.status, sizeof( out->status ) );
	else Com_sprintf( out->status, sizeof( out->status ), "%s / %s", out->role, out->state );
	LS_RaceBuildFlags( out->flags, sizeof( out->flags ) );
	LS_RaceLoadedCounts( &loaded, &total );
	Com_sprintf( out->ready, sizeof( out->ready ), "%d/%d", loaded, total );
	if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		LS_RaceFormatCountdown( LS_RaceCountdownRemaining( Sys_Milliseconds() ), out->countdownText, sizeof( out->countdownText ) );
	}
	LS_RaceBuildSortedPlayers( sorted, &count );
	out->playerCount = count;
	for ( i = 0; i < count && i < LS_RACE_UI_MAX_PLAYERS; i++ ) {
		lsRacePlayer_t *p = &ls_race.players[sorted[i]];
		lsRaceUiPlayer_t *row = &out->players[i];
		char timeText[32], stageTimeText[32];
		if ( p->timeMs > 0 ) LS_FormatTime( p->timeMs, timeText, sizeof( timeText ) );
		else Q_strncpyz( timeText, "--", sizeof( timeText ) );
		if ( p->stageTimeMs > 0 ) LS_FormatTime( p->stageTimeMs, stageTimeText, sizeof( stageTimeText ) );
		else Q_strncpyz( stageTimeText, "--", sizeof( stageTimeText ) );
		row->rank = i + 1;
		row->slot = p->slot + 1;
		row->progress = p->stageProgress;
		row->finished = p->finished ? 1 : 0;
		row->local = p->local ? 1 : 0;
		row->red = p->color[0];
		row->green = p->color[1];
		row->blue = p->color[2];
		row->objectivesFound = p->objectivesFound;
		row->objectivesTotal = p->objectivesTotal;
		row->zonesFound = p->zoneProgress;
		row->zonesTotal = p->zoneTotal;
		row->score = LS_RaceCombinedPoints( p );
		row->cheatFlags = p->cheatFlags;
		row->inMenu = p->inMenu ? 1 : 0;
		row->left = p->left ? 1 : 0;
		row->timedOut = p->timedOut ? 1 : 0;
		Q_strncpyz( row->nick, p->nick, sizeof( row->nick ) );
		Q_strncpyz( row->map, p->map[0] ? p->map : "-", sizeof( row->map ) );
		Q_strncpyz( row->stage, p->stageName[0] ? p->stageName : "-", sizeof( row->stage ) );
		Q_strncpyz( row->state, LS_RacePlayerStateText( p ), sizeof( row->state ) );
		Q_strncpyz( row->rgt, timeText, sizeof( row->rgt ) );
		Q_strncpyz( row->stageIgt, stageTimeText, sizeof( row->stageIgt ) );
	}
	local = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( local ) {
		if ( local->timeMs > 0 ) LS_FormatTime( local->timeMs, value, sizeof( value ) );
		else Q_strncpyz( value, "0.00", sizeof( value ) );
		Q_strncpyz( out->timer, value, sizeof( out->timer ) );
		Q_strncpyz( out->stage, local->stageName[0] ? local->stageName : "-", sizeof( out->stage ) );
		if ( local->stageTimeMs > 0 ) LS_FormatTime( local->stageTimeMs, value, sizeof( value ) );
		else Q_strncpyz( value, "--", sizeof( value ) );
		Q_strncpyz( out->stageIgt, value, sizeof( out->stageIgt ) );
	}
	for ( i = 0; i < LS_RACE_CHAT_LINES && i < LS_RACE_UI_CHAT_LINES; i++ ) {
		out->chat[i].timeMs = ls_race.chat[i].timeMs;
		out->chat[i].red = ls_race.chat[i].color[0];
		out->chat[i].green = ls_race.chat[i].color[1];
		out->chat[i].blue = ls_race.chat[i].color[2];
		Q_strncpyz( out->chat[i].nick, ls_race.chat[i].nick, sizeof( out->chat[i].nick ) );
		Q_strncpyz( out->chat[i].text, ls_race.chat[i].text, sizeof( out->chat[i].text ) );
		Q_strncpyz( out->chatLines[i], ls_race.chatLines[i], sizeof( out->chatLines[i] ) );
		if ( out->chat[i].text[0] || out->chatLines[i][0] ) out->chatCount++;
	}
}

static void LS_RaceClearGhostCvars( void ) {
	int i;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		LS_RaceSetCvarString( ls_race.ghostCvars[i], "" );
	}
}

static void LS_RacePublishGhostCvars( void ) {
	int i, ghostSlot = 0, now = Sys_Milliseconds();
	char localMap[LS_MAX_MAPNAME];
	char value[512];
	if ( ls_race.role == LS_RACE_ROLE_NONE || ( ls_race.ghostsCvar && !ls_race.ghostsCvar->integer ) ) {
		LS_RaceClearGhostCvars();
		ls_race.lastGhostPublishMs = 0;
		return;
	}
	if ( ls_race.lastGhostPublishMs && now - ls_race.lastGhostPublishMs < LS_RACE_GHOST_PUBLISH_MS ) return;
	ls_race.lastGhostPublishMs = now;
	localMap[0] = '\0';
	if ( cl.mapname[0] ) LS_ExtractMapname( cl.mapname, localMap, sizeof( localMap ) );
	for ( i = 0; i < LS_RACE_MAX_PLAYERS && ghostSlot < LS_RACE_MAX_PLAYERS; i++ ) {
		lsRacePlayer_t *p = &ls_race.players[i];
		if ( !p->used || p->local || !p->started || p->finished ) continue;
		if ( now - p->lastHeardMs > 2500 ) continue;
		if ( localMap[0] && p->map[0] && Q_stricmp( localMap, p->map ) ) continue;
		LS_RaceUpdatePlayerRender( p, now );
		Com_sprintf( value, sizeof( value ), "1 %d %d %.1f %.1f %.1f %.1f %.1f %d %d %d %d %d %d %d %d %s %d %d %.1f %.1f %.1f %.1f %d %d",
			( p->color[0] << 16 ) | ( p->color[1] << 8 ) | p->color[2],
			LS_RaceGhostAlpha(), p->renderX, p->renderY, p->renderZ, p->renderYaw, p->renderSpeed,
			p->crouched, p->health, p->armor, p->weapon, p->ammo, p->clip,
			p->legsAnim, p->torsoAnim, p->nick, p->movementDir, p->eFlags,
			p->renderPitch, p->vx, p->vy, p->vz, p->groundEntityNum, p->animMovetype );
		LS_RaceSetCvarString( ls_race.ghostCvars[ghostSlot], value );
		ghostSlot++;
	}
	for ( ; ghostSlot < LS_RACE_MAX_PLAYERS; ghostSlot++ ) {
		LS_RaceSetCvarString( ls_race.ghostCvars[ghostSlot], "" );
	}
}

static void LS_RaceClearPlayers( void ) {
	memset( ls_race.players, 0, sizeof( ls_race.players ) );
}

static int LS_RacePlayerCount( void ) {
	int i, count = 0;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( ls_race.players[i].used ) count++;
	}
	return count;
}

static void LS_RaceLoadedCounts( int *loaded, int *total ) {
	int i, l = 0, t = 0;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !LS_RacePlayerBlocksReady( &ls_race.players[i] ) ) continue;
		t++;
		if ( ls_race.players[i].loaded ) l++;
	}
	if ( loaded ) *loaded = l;
	if ( total ) *total = t;
}

static int LS_RaceCountdownRemaining( int now ) {
	int remain;
	if ( ls_race.countdownMs <= 0 ) return 0;
	if ( ls_race.state != LS_RACE_STATE_COUNTDOWN || !ls_race.countdownStartMs ) return ls_race.countdownMs;
	remain = ls_race.countdownMs - ( now - ls_race.countdownStartMs );
	if ( remain < 0 ) remain = 0;
	if ( remain > ls_race.countdownMs ) remain = ls_race.countdownMs;
	return remain;
}

static void LS_RaceFormatCountdown( int remainMs, char *out, int outSize ) {
	if ( !out || outSize <= 0 ) return;
	if ( remainMs <= 0 ) {
		Q_strncpyz( out, "GO", outSize );
		return;
	}
	if ( remainMs < 10000 ) {
		Com_sprintf( out, outSize, "%d.%d", remainMs / 1000, ( remainMs % 1000 ) / 100 );
		return;
	}
	Com_sprintf( out, outSize, "%d", ( remainMs + 999 ) / 1000 );
}

static int LS_RaceCurrentTimerMs( void ) {
	if ( ls.runFinished ) return ls.runTotalIGTMs;
	if ( ls.active ) return LS_CumulativeTime( ls.modeLastIdx );
	return 0;
}

static qboolean LS_RaceAnyRemoteNotStarted( void ) {
	int i;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( LS_RacePlayerBlocksReady( &ls_race.players[i] ) && !ls_race.players[i].local && !ls_race.players[i].started ) return qtrue;
	}
	return qfalse;
}

static void LS_RaceDrawCenteredString( int y, float size, const char *text, float *color ) {
	int x;
	if ( !text || !text[0] ) return;
	x = 320 - (int)( strlen( text ) * size * 0.5f );
	SCR_DrawStringExt( x, y, size, text, color, qtrue );
}

static lsRacePlayer_t *LS_RaceFindPlayerByAdr( netadr_t adr ) {
	int i;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( ls_race.players[i].used && !ls_race.players[i].local && NET_CompareAdr( ls_race.players[i].adr, adr ) ) return &ls_race.players[i];
	}
	return NULL;
}

static lsRacePlayer_t *LS_RaceFindPlayerForReconnect( netadr_t adr, const char *nick ) {
	int i;
	lsRacePlayer_t *match = NULL;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		lsRacePlayer_t *p = &ls_race.players[i];
		if ( !p->used || p->local ) continue;
		if ( NET_CompareAdr( p->adr, adr ) ) return p;
	}
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		lsRacePlayer_t *p = &ls_race.players[i];
		if ( !p->used || p->local ) continue;
		if ( !NET_CompareBaseAdr( p->adr, adr ) ) continue;
		if ( nick && nick[0] && !Q_stricmp( p->nick, nick ) ) return p;
		if ( match ) return NULL;
		match = p;
	}
	return match;
}

static lsRacePlayer_t *LS_RaceFindPlayerBySlot( int slot ) {
	if ( slot < 0 || slot >= LS_RACE_MAX_PLAYERS ) return NULL;
	if ( !ls_race.players[slot].used ) return NULL;
	return &ls_race.players[slot];
}

static lsRacePlayer_t *LS_RaceAllocPlayer( netadr_t adr ) {
	int i;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used ) {
			memset( &ls_race.players[i], 0, sizeof( ls_race.players[i] ) );
			LS_RaceInitPlayerDefaults( &ls_race.players[i] );
			ls_race.players[i].used = qtrue;
			ls_race.players[i].slot = i;
			ls_race.players[i].adr = adr;
			ls_race.players[i].basePort = BigShort( adr.port );
			return &ls_race.players[i];
		}
	}
	return NULL;
}

static void LS_RaceInitLocalPlayer( int slot ) {
	char nick[32];
	int color[3];
	lsRacePlayer_t *p;
	if ( slot < 0 || slot >= LS_RACE_MAX_PLAYERS ) return;
	p = &ls_race.players[slot];
	LS_RaceGetLocalIdentity( nick, sizeof( nick ), color );
	memset( p, 0, sizeof( *p ) );
	LS_RaceInitPlayerDefaults( p );
	p->used = qtrue;
	p->local = qtrue;
	p->slot = slot;
	Q_strncpyz( p->nick, nick, sizeof( p->nick ) );
	p->color[0] = color[0];
	p->color[1] = color[1];
	p->color[2] = color[2];
	p->lastHeardMs = Sys_Milliseconds();
	ls_race.localSlot = slot;
}

static void LS_RaceNormalizeSettings( int *mode, int *mission, int *percent100, int *difficulty, int *hl1Movement, int *autoJump, char *ilMap, int ilMapSize ) {
	if ( mode ) {
		if ( *mode < LS_MODE_FULLGAME || *mode > LS_MODE_IL ) *mode = LS_MODE_FULLGAME;
	}
	if ( mission ) {
		if ( *mission < 1 ) *mission = 1;
		if ( *mission > LS_NUM_MISSION_GROUPS ) *mission = LS_NUM_MISSION_GROUPS;
	}
	if ( percent100 ) *percent100 = *percent100 ? 1 : 0;
	if ( difficulty ) *difficulty = LS_RaceClampDifficulty( *difficulty );
	if ( hl1Movement ) *hl1Movement = *hl1Movement ? 1 : 0;
	if ( autoJump ) {
		*autoJump = ( hl1Movement && *hl1Movement && *autoJump ) ? 1 : 0;
	}
	if ( ilMap && ilMapSize > 0 && !ilMap[0] ) Q_strncpyz( ilMap, "escape1", ilMapSize );
}

static qboolean LS_RaceSettingsChanged( int mode, int mission, int percent100, int difficulty, int hl1Movement, int autoJump, int antiCheat, const char *ilMap ) {
	if ( ls_race.mode != mode || ls_race.mission != mission || ls_race.percent100 != percent100 ) return qtrue;
	if ( ls_race.difficulty != difficulty || ls_race.hl1Movement != hl1Movement || ls_race.autoJump != autoJump ) return qtrue;
	if ( ls_race.antiCheat != ( antiCheat ? 1 : 0 ) ) return qtrue;
	if ( Q_stricmp( ls_race.ilMap, ilMap && ilMap[0] ? ilMap : "escape1" ) ) return qtrue;
	return qfalse;
}

static void LS_RaceCaptureSavedSettings( void ) {
	if ( ls_raceSavedSettings.valid ) return;
	ls_raceSavedSettings.mode = ls_modeCvar ? ls_modeCvar->integer : LS_MODE_FULLGAME;
	ls_raceSavedSettings.mission = ls_missionCvar ? ls_missionCvar->integer : 1;
	ls_raceSavedSettings.percent100 = ls_100pctCvar && ls_100pctCvar->integer ? 1 : 0;
	ls_raceSavedSettings.difficulty = LS_DetectDifficulty();
	ls_raceSavedSettings.hl1Movement = LS_HL1ModeActive() ? 1 : 0;
	ls_raceSavedSettings.autoJump = Cvar_VariableIntegerValue( "bh_autojump" ) ? 1 : 0;
	ls_raceSavedSettings.antiCheat = Cvar_VariableIntegerValue( "ls_race_anticheat" ) ? 1 : 0;
	Q_strncpyz( ls_raceSavedSettings.ilMap, ( ls_mapCvar && ls_mapCvar->string[0] ) ? ls_mapCvar->string : "escape1", sizeof( ls_raceSavedSettings.ilMap ) );
	LS_RaceNormalizeSettings( &ls_raceSavedSettings.mode, &ls_raceSavedSettings.mission, &ls_raceSavedSettings.percent100,
		&ls_raceSavedSettings.difficulty, &ls_raceSavedSettings.hl1Movement, &ls_raceSavedSettings.autoJump,
		ls_raceSavedSettings.ilMap, sizeof( ls_raceSavedSettings.ilMap ) );
	ls_raceSavedSettings.valid = qtrue;
}

static void LS_RaceRestoreSavedSettings( void ) {
	char buf[16];
	if ( !ls_raceSavedSettings.valid ) return;
	Com_sprintf( buf, sizeof( buf ), "%d", ls_raceSavedSettings.mode );
	Cvar_Set( "ls_mode", buf );
	Com_sprintf( buf, sizeof( buf ), "%d", ls_raceSavedSettings.mission );
	Cvar_Set( "ls_mission", buf );
	Cvar_Set( "ls_100pct", ls_raceSavedSettings.percent100 ? "1" : "0" );
	Com_sprintf( buf, sizeof( buf ), "%d", ls_raceSavedSettings.difficulty );
	Cvar_Set( "g_gameskill", buf );
	Cvar_Set( "bh_movement", ls_raceSavedSettings.hl1Movement ? "1" : "0" );
	Cvar_Set( "bh_autojump", ls_raceSavedSettings.autoJump ? "1" : "0" );
	Cvar_Set( "ls_race_anticheat", ls_raceSavedSettings.antiCheat ? "1" : "0" );
	Cvar_Set( "ls_map", ls_raceSavedSettings.ilMap[0] ? ls_raceSavedSettings.ilMap : "escape1" );
	LS_SetupMode( ls_raceSavedSettings.mode, ls_raceSavedSettings.mission );
	LS_LoadMode( ls_raceSavedSettings.mode );
	memset( &ls_raceSavedSettings, 0, sizeof( ls_raceSavedSettings ) );
}

static qboolean LS_RaceSyncSettingsFromCvars( void ) {
	int mode = ls_modeCvar ? ls_modeCvar->integer : LS_MODE_FULLGAME;
	int mission = ls_missionCvar ? ls_missionCvar->integer : 1;
	int percent100 = ls_100pctCvar && ls_100pctCvar->integer ? 1 : 0;
	int difficulty = LS_DetectDifficulty();
	int hl1Movement = LS_HL1ModeActive() ? 1 : 0;
	int autoJump = Cvar_VariableIntegerValue( "bh_autojump" ) ? 1 : 0;
	int antiCheat = Cvar_VariableIntegerValue( "ls_race_anticheat" ) ? 1 : 0;
	char ilMap[LS_MAX_MAPNAME];
	qboolean changed;
	Q_strncpyz( ilMap, ( ls_mapCvar && ls_mapCvar->string[0] ) ? ls_mapCvar->string : "escape1", sizeof( ilMap ) );
	LS_RaceNormalizeSettings( &mode, &mission, &percent100, &difficulty, &hl1Movement, &autoJump, ilMap, sizeof( ilMap ) );
	changed = LS_RaceSettingsChanged( mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, ilMap );
	ls_race.mode = mode;
	ls_race.mission = mission;
	ls_race.percent100 = percent100;
	ls_race.difficulty = difficulty;
	ls_race.hl1Movement = hl1Movement;
	ls_race.autoJump = autoJump;
	ls_race.antiCheat = antiCheat;
	Q_strncpyz( ls_race.ilMap, ilMap, sizeof( ls_race.ilMap ) );
	if ( !hl1Movement && Cvar_VariableIntegerValue( "bh_autojump" ) ) {
		Cvar_Set( "bh_autojump", "0" );
	}
	LS_SetupMode( ls_race.mode, ls_race.mission );
	return changed;
}

static qboolean LS_RaceCvarsMatchSettings( void ) {
	int mode = ls_modeCvar ? ls_modeCvar->integer : LS_MODE_FULLGAME;
	int mission = ls_missionCvar ? ls_missionCvar->integer : 1;
	int percent100 = ls_100pctCvar && ls_100pctCvar->integer ? 1 : 0;
	int difficulty = LS_DetectDifficulty();
	int hl1Movement = LS_HL1ModeActive() ? 1 : 0;
	int autoJump = Cvar_VariableIntegerValue( "bh_autojump" ) ? 1 : 0;
	int antiCheat = Cvar_VariableIntegerValue( "ls_race_anticheat" ) ? 1 : 0;
	char ilMap[LS_MAX_MAPNAME];
	Q_strncpyz( ilMap, ( ls_mapCvar && ls_mapCvar->string[0] ) ? ls_mapCvar->string : "escape1", sizeof( ilMap ) );
	LS_RaceNormalizeSettings( &mode, &mission, &percent100, &difficulty, &hl1Movement, &autoJump, ilMap, sizeof( ilMap ) );
	return !LS_RaceSettingsChanged( mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, ilMap );
}

static const char *LS_RaceSelectStartMap( void ) {
	int i;
	LS_RaceSyncSettingsFromCvars();
	if ( ls_race.mode == LS_MODE_IL ) {
		int idx = LS_FindMapIndex( ls_race.ilMap );
		if ( idx >= 0 && !ls.splits[idx].cutscene ) return ls.splits[idx].mapname;
	}
	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		if ( !ls.splits[i].cutscene ) return ls.splits[i].mapname;
	}
	return "escape1";
}

static void LS_RaceApplySettings( int mode, int mission, int percent100, int difficulty, int hl1Movement, int autoJump, int antiCheat, const char *ilMap ) {
	char buf[16];
	char safeIlMap[LS_MAX_MAPNAME];
	Q_strncpyz( safeIlMap, ilMap && ilMap[0] ? ilMap : "escape1", sizeof( safeIlMap ) );
	LS_RaceNormalizeSettings( &mode, &mission, &percent100, &difficulty, &hl1Movement, &autoJump, safeIlMap, sizeof( safeIlMap ) );
	ls_race.mode = mode;
	ls_race.mission = mission;
	ls_race.percent100 = percent100;
	ls_race.difficulty = difficulty;
	ls_race.hl1Movement = hl1Movement;
	ls_race.autoJump = autoJump;
	ls_race.antiCheat = antiCheat ? 1 : 0;
	Q_strncpyz( ls_race.ilMap, safeIlMap, sizeof( ls_race.ilMap ) );
	Com_sprintf( buf, sizeof( buf ), "%d", ls_race.mode );
	Cvar_Set( "ls_mode", buf );
	Com_sprintf( buf, sizeof( buf ), "%d", ls_race.mission );
	Cvar_Set( "ls_mission", buf );
	Cvar_Set( "ls_100pct", ls_race.percent100 ? "1" : "0" );
	Com_sprintf( buf, sizeof( buf ), "%d", ls_race.difficulty );
	Cvar_Set( "g_gameskill", buf );
	Cvar_Set( "bh_movement", ls_race.hl1Movement ? "1" : "0" );
	Cvar_Set( "bh_autojump", ls_race.autoJump ? "1" : "0" );
	Cvar_Set( "ls_race_anticheat", ls_race.antiCheat ? "1" : "0" );
	if ( ls_race.mode == LS_MODE_IL ) Cvar_Set( "ls_map", ls_race.ilMap );
	LS_SetupMode( ls_race.mode, ls_race.mission );
}

static void LS_RaceEnforceLockedSettings( void ) {
	if ( ls_race.role == LS_RACE_ROLE_NONE ) return;
	if ( ls_race.role == LS_RACE_ROLE_HOST && ls_race.state == LS_RACE_STATE_LOBBY ) return;
	if ( !LS_RaceCvarsMatchSettings() ) {
		LS_RaceApplySettings( ls_race.mode, ls_race.mission, ls_race.percent100, ls_race.difficulty,
			ls_race.hl1Movement, ls_race.autoJump, ls_race.antiCheat, ls_race.ilMap );
	}
}

static void LS_RaceSendPlayerTo( netadr_t to, lsRacePlayer_t *p ) {
	if ( !p || !p->used ) return;
	NET_OutOfBandPrint( NS_CLIENT, to,
		"srace player %d %d %s %d %d %d %s %d %d %d %d %.1f %.1f %.1f %.1f %.1f %d %d %d %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %.1f %.1f %.1f %.1f %d %d",
		ls_race.session, p->slot, p->nick,
		p->color[0], p->color[1], p->color[2], p->map[0] ? p->map : "-",
		p->loaded ? 1 : 0, p->started ? 1 : 0, p->finished ? 1 : 0, p->timeMs,
		p->x, p->y, p->z, p->yaw, p->speed,
		p->igtMs, p->stageTimeMs, p->stageProgress, p->stageName[0] ? p->stageName : "-",
		p->crouched, p->health, p->armor, p->weapon, p->ammo, p->clip,
		p->objectivesFound, p->objectivesTotal, p->zoneProgress, p->zoneTotal, p->paused ? 1 : 0,
		p->cheatFlags, p->inMenu ? 1 : 0, p->left ? 1 : 0, p->timedOut ? 1 : 0,
		p->legsAnim, p->torsoAnim, p->movementDir, p->eFlags,
		p->pitch, p->vx, p->vy, p->vz, p->groundEntityNum, p->animMovetype );
}

static void LS_RaceBroadcastPlayer( lsRacePlayer_t *p ) {
	int i;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( ls_race.players[i].used && !ls_race.players[i].local ) LS_RaceSendPlayerTo( ls_race.players[i].adr, p );
	}
}

static void LS_RaceSendRosterTo( netadr_t to ) {
	int i;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( ls_race.players[i].used ) LS_RaceSendPlayerTo( to, &ls_race.players[i] );
	}
}

static void LS_RaceBroadcastRoster( void ) {
	int i, j;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used || ls_race.players[i].local ) continue;
		for ( j = 0; j < LS_RACE_MAX_PLAYERS; j++ ) {
			if ( ls_race.players[j].used ) LS_RaceSendPlayerTo( ls_race.players[i].adr, &ls_race.players[j] );
		}
	}
}

static int LS_RaceAdrPort( netadr_t adr ) {
	return BigShort( adr.port );
}

static void LS_RaceSetAdrPort( netadr_t *adr, int port ) {
	if ( !adr ) return;
	adr->port = BigShort( (short)port );
}

static int LS_RacePortScanBase( netadr_t adr, int rememberedPort ) {
	int currentPort = LS_RaceAdrPort( adr );
	return rememberedPort > 0 ? rememberedPort : currentPort;
}

static qboolean LS_RaceShouldScanHostPorts( void ) {
	if ( ls_race.role != LS_RACE_ROLE_CLIENT || ls_race.hostAdr.type != NA_IP ) return qfalse;
	return ls_race.state == LS_RACE_STATE_CONNECTING ? qtrue : qfalse;
}

static qboolean LS_RaceAllowPortScan( void ) {
	return ls_race.role == LS_RACE_ROLE_CLIENT &&
		ls_race.state == LS_RACE_STATE_CONNECTING &&
		ls_race.hostAdr.type == NA_IP ? qtrue : qfalse;
}

static qboolean LS_RaceHostAdrMatches( netadr_t from ) {
	int requestedPort, fromPort;
	if ( NET_CompareAdr( from, ls_race.hostAdr ) ) {
		if ( !ls_race.hostBasePort ) ls_race.hostBasePort = LS_RaceAdrPort( from );
		return qtrue;
	}
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return qfalse;
	if ( !NET_CompareBaseAdr( from, ls_race.hostAdr ) ) {
		if ( ls_race.session && Cmd_Argc() > 2 && atoi( Cmd_Argv( 2 ) ) == ls_race.session ) {
			ls_race.hostAdr = from;
			if ( !ls_race.hostBasePort ) ls_race.hostBasePort = LS_RaceAdrPort( from );
			if ( ls_race.portCvar ) Cvar_SetValue( ls_race.portCvar->name, LS_RaceAdrPort( from ) );
			return qtrue;
		}
		return qfalse;
	}
	requestedPort = LS_RaceAdrPort( ls_race.hostAdr );
	fromPort = LS_RaceAdrPort( from );
	if ( LS_RaceAllowPortScan() ) {
		if ( fromPort < requestedPort || fromPort >= requestedPort + LS_RACE_PORT_SCAN_SPAN ) return qfalse;
	} else if ( !ls_race.session ) {
		return qfalse;
	}
	ls_race.hostAdr = from;
	if ( !ls_race.hostBasePort ) ls_race.hostBasePort = fromPort;
	if ( ls_race.portCvar ) Cvar_SetValue( ls_race.portCvar->name, fromPort );
	return qtrue;
}

static qboolean LS_RacePlayerAdrMatches( lsRacePlayer_t *p, netadr_t from ) {
	if ( !p || p->local ) return qfalse;
	if ( NET_CompareAdr( p->adr, from ) ) {
		if ( !p->basePort ) p->basePort = LS_RaceAdrPort( from );
		return qtrue;
	}
	if ( !NET_CompareBaseAdr( p->adr, from ) && !ls_race.session ) return qfalse;
	p->adr = from;
	if ( !p->basePort ) p->basePort = LS_RaceAdrPort( from );
	return qtrue;
}

static void LS_RaceSendHelloTo( netadr_t to ) {
	char nick[32];
	char pass[32];
	int color[3];
	LS_RaceGetLocalIdentity( nick, sizeof( nick ), color );
	LS_RaceSanitizeOptionalToken( ls_race.passwordCvar ? ls_race.passwordCvar->string : "", pass, sizeof( pass ) );
	NET_OutOfBandPrint( NS_CLIENT, to, "srace hello %d %s %d %d %d %d %d %s",
		LS_RACE_PROTO_VERSION, nick, color[0], color[1], color[2], ls_race.session, ls_race.localSlot, pass );
}

static void LS_RaceSendHello( void ) {
	int basePort, scanPort;
	netadr_t probeAdr;
	if ( LS_RaceShouldScanHostPorts() ) {
		int exactPort = LS_RaceAdrPort( ls_race.hostAdr );
		LS_RaceSendHelloTo( ls_race.hostAdr );
		basePort = LS_RacePortScanBase( ls_race.hostAdr, ls_race.hostBasePort );
		for ( scanPort = basePort; scanPort < basePort + LS_RACE_PORT_SCAN_SPAN; scanPort++ ) {
			if ( scanPort == exactPort ) continue;
			probeAdr = ls_race.hostAdr;
			LS_RaceSetAdrPort( &probeAdr, scanPort );
			LS_RaceSendHelloTo( probeAdr );
		}
		return;
	}
	LS_RaceSendHelloTo( ls_race.hostAdr );
}

static const char *LS_RaceHostBindIp( void ) {
	const char *bindIp = ls_race.hostIpCvar ? ls_race.hostIpCvar->string : "localhost";
	return bindIp && bindIp[0] ? bindIp : "localhost";
}

static qboolean LS_RaceValidateBindIp( const char *bindIp ) {
	netadr_t adr;
	if ( !bindIp || !bindIp[0] ) return qtrue;
	if ( !Q_stricmp( bindIp, "localhost" ) || !Q_stricmp( bindIp, "0.0.0.0" ) || !Q_stricmp( bindIp, "*" ) ) return qtrue;
	if ( strchr( bindIp, ':' ) ) return qfalse;
	if ( !NET_StringToAdr( bindIp, &adr ) || adr.type != NA_IP ) {
		return qfalse;
	}
	return qtrue;
}

static void LS_RaceHostSocketAddress( const char *socketIp, int port, struct sockaddr_in *address ) {
	netadr_t adr;
	memset( address, 0, sizeof( *address ) );
	address->sin_family = AF_INET;
	address->sin_port = htons( (short)port );
	if ( !socketIp || !socketIp[0] || !Q_stricmp( socketIp, "localhost" ) || !Q_stricmp( socketIp, "0.0.0.0" ) ) {
		address->sin_addr.s_addr = INADDR_ANY;
		return;
	}
	if ( NET_StringToAdr( socketIp, &adr ) && adr.type == NA_IP ) {
		*(int *)&address->sin_addr = *(int *)&adr.ip;
	}
}

static qboolean LS_RaceCanBindHostSocket( const char *socketIp, int port, char *error, int errorSize ) {
	struct sockaddr_in address;
	int yes = 1;
#ifdef _WIN32
	SOCKET testSocket;
	int err;
#else
	int testSocket;
	int err;
#endif
	if ( error && errorSize > 0 ) error[0] = '\0';
	LS_RaceHostSocketAddress( socketIp, port, &address );
#ifdef _WIN32
	testSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( testSocket == INVALID_SOCKET ) {
		err = WSAGetLastError();
		if ( error && errorSize > 0 ) Com_sprintf( error, errorSize, "socket error %d", err );
		return qfalse;
	}
	setsockopt( testSocket, SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof( yes ) );
	if ( bind( testSocket, (struct sockaddr *)&address, sizeof( address ) ) == SOCKET_ERROR ) {
		err = WSAGetLastError();
		if ( error && errorSize > 0 ) Com_sprintf( error, errorSize, "bind error %d", err );
		closesocket( testSocket );
		return qfalse;
	}
	closesocket( testSocket );
#else
	testSocket = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( testSocket < 0 ) {
		err = errno;
		if ( error && errorSize > 0 ) Com_sprintf( error, errorSize, "socket error %d", err );
		return qfalse;
	}
	setsockopt( testSocket, SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof( yes ) );
	if ( bind( testSocket, (struct sockaddr *)&address, sizeof( address ) ) < 0 ) {
		err = errno;
		if ( error && errorSize > 0 ) Com_sprintf( error, errorSize, "bind error %d", err );
		close( testSocket );
		return qfalse;
	}
	close( testSocket );
#endif
	return qtrue;
}

static void LS_RaceRestoreHostNetwork( const char *oldIp, int oldPort ) {
	Cvar_Set( "net_ip", oldIp && oldIp[0] ? oldIp : "localhost" );
	Cvar_SetValue( "net_port", oldPort > 0 ? oldPort : 27960 );
	NET_Config( qfalse );
	NET_Config( qtrue );
}

static qboolean LS_RaceApplyHostNetwork( int *actualPortOut ) {
	char currentIp[128];
	char bindError[64];
	const char *bindIp = LS_RaceHostBindIp();
	const char *socketIp;
	int requestedPort, currentPort, actualPort;
	qboolean restartNeeded;
	if ( !LS_RaceValidateBindIp( bindIp ) ) {
		LS_RaceSetStatus( "Invalid host bind IP" );
		Com_Printf( "^1Race: invalid host bind IP '%s'\n", bindIp );
		return qfalse;
	}
	socketIp = !Q_stricmp( bindIp, "*" ) ? "localhost" : bindIp;
	requestedPort = ls_race.portCvar ? ls_race.portCvar->integer : Cvar_VariableIntegerValue( "net_port" );
	if ( requestedPort < 1 || requestedPort > 65535 ) {
		LS_RaceSetStatus( "Invalid host UDP port" );
		Com_Printf( "^1Race: invalid host UDP port %d\n", requestedPort );
		return qfalse;
	}
	Cvar_VariableStringBuffer( "net_ip", currentIp, sizeof( currentIp ) );
	currentPort = Cvar_VariableIntegerValue( "net_port" );
	if ( !LS_RaceCanBindHostSocket( socketIp, 0, bindError, sizeof( bindError ) ) ) {
		LS_RaceSetStatus( va( "Cannot bind host IP %s (%s)", socketIp, bindError[0] ? bindError : "not local" ) );
		Com_Printf( "^1Race: cannot bind host IP '%s': %s\n", socketIp, bindError[0] ? bindError : "not local" );
		return qfalse;
	}
	if ( requestedPort != currentPort && !LS_RaceCanBindHostSocket( socketIp, requestedPort, bindError, sizeof( bindError ) ) ) {
		LS_RaceSetStatus( va( "Cannot use UDP port %d (%s)", requestedPort, bindError[0] ? bindError : "busy" ) );
		Com_Printf( "^1Race: cannot bind %s:%d: %s\n", socketIp, requestedPort, bindError[0] ? bindError : "busy" );
		return qfalse;
	}
	restartNeeded = ( Q_stricmp( currentIp, socketIp ) || currentPort != requestedPort ) ? qtrue : qfalse;
	Cvar_Set( "net_ip", socketIp );
	Cvar_SetValue( "net_port", requestedPort );
	if ( restartNeeded ) {
		Com_Printf( "^3Race: binding host socket to %s:%d\n", socketIp, requestedPort );
		NET_Config( qfalse );
		NET_Config( qtrue );
	}
	if ( !NET_IsIPSocketOpen() ) {
		LS_RaceSetStatus( va( "Cannot open UDP socket on %s:%d", socketIp, requestedPort ) );
		Com_Printf( "^1Race: failed to open UDP socket on %s:%d\n", socketIp, requestedPort );
		LS_RaceRestoreHostNetwork( currentIp, currentPort );
		return qfalse;
	}
	actualPort = Cvar_VariableIntegerValue( "net_port" );
	if ( actualPort != requestedPort ) {
		LS_RaceSetStatus( va( "UDP port %d is unavailable", requestedPort ) );
		Com_Printf( "^1Race: requested UDP port %d unavailable, engine opened %d instead\n", requestedPort, actualPort );
		LS_RaceRestoreHostNetwork( currentIp, currentPort );
		return qfalse;
	}
	if ( actualPortOut ) *actualPortOut = actualPort;
	if ( ls_race.hostIpCvar && Q_stricmp( ls_race.hostIpCvar->string, socketIp ) ) Cvar_Set( ls_race.hostIpCvar->name, socketIp );
	if ( ls_race.portCvar ) Cvar_SetValue( ls_race.portCvar->name, actualPort );
	return qtrue;
}

static qboolean LS_RaceResolveHostAddress( const char *ip, int port, netadr_t *out ) {
	char addrText[128];
	if ( !ip || !ip[0] || !out ) return qfalse;
	if ( port <= 0 ) port = 27960;
	Com_sprintf( addrText, sizeof( addrText ), "%s:%d", ip, port );
	if ( !NET_StringToAdr( addrText, out ) ) {
		return qfalse;
	}
	return out->type == NA_IP ? qtrue : qfalse;
}

static qboolean LS_RaceSendDiscoverScanTo( const char *ip, int basePort ) {
	netadr_t baseAdr, probeAdr;
	int scanPort;
	if ( !LS_RaceResolveHostAddress( ip, basePort, &baseAdr ) ) return qfalse;
	for ( scanPort = basePort; scanPort < basePort + LS_RACE_PORT_SCAN_SPAN; scanPort++ ) {
		probeAdr = baseAdr;
		LS_RaceSetAdrPort( &probeAdr, scanPort );
		NET_OutOfBandPrint( NS_CLIENT, probeAdr, "srace discover %d", LS_RACE_PROTO_VERSION );
	}
	return qtrue;
}

static void LS_RaceAddFoundLobby( netadr_t adr, int session, const char *nick, int state, int players, int maxPlayers, int mode, int mission, int percent100, int difficulty, int hl1Movement, int autoJump, int antiCheat, const char *ilMap, int privateLobby, int queueCount ) {
	int i, slot = -1;
	char safeIlMap[LS_MAX_MAPNAME];
	Q_strncpyz( safeIlMap, ilMap && ilMap[0] ? ilMap : "escape1", sizeof( safeIlMap ) );
	LS_RaceNormalizeSettings( &mode, &mission, &percent100, &difficulty, &hl1Movement, &autoJump, safeIlMap, sizeof( safeIlMap ) );
	for ( i = 0; i < LS_RACE_FOUND_MAX; i++ ) {
		if ( ls_race.found[i].used && NET_CompareAdr( ls_race.found[i].adr, adr ) ) {
			slot = i;
			break;
		}
		if ( slot < 0 && !ls_race.found[i].used ) slot = i;
	}
	if ( slot < 0 ) slot = 0;
	ls_race.found[slot].used = qtrue;
	ls_race.found[slot].adr = adr;
	ls_race.found[slot].session = session;
	ls_race.found[slot].state = (int)Com_Clamp( (float)LS_RACE_STATE_LOBBY, (float)LS_RACE_STATE_FINISHED, (float)state );
	ls_race.found[slot].players = (int)Com_Clamp( 0.0f, (float)LS_RACE_MAX_PLAYERS, (float)players );
	ls_race.found[slot].maxPlayers = maxPlayers > 0 ? maxPlayers : LS_RACE_MAX_PLAYERS;
	ls_race.found[slot].mode = mode;
	ls_race.found[slot].mission = mission;
	ls_race.found[slot].percent100 = percent100 ? 1 : 0;
	ls_race.found[slot].difficulty = difficulty;
	ls_race.found[slot].hl1Movement = hl1Movement;
	ls_race.found[slot].autoJump = autoJump;
	ls_race.found[slot].antiCheat = antiCheat ? 1 : 0;
	ls_race.found[slot].privateLobby = privateLobby ? 1 : 0;
	ls_race.found[slot].queueCount = queueCount < 0 ? 0 : queueCount;
	ls_race.found[slot].lastHeardMs = Sys_Milliseconds();
	LS_RaceSanitizeToken( nick && nick[0] ? nick : "Host", ls_race.found[slot].nick, sizeof( ls_race.found[slot].nick ) );
	Q_strncpyz( ls_race.found[slot].ilMap, safeIlMap, sizeof( ls_race.found[slot].ilMap ) );
	LS_RaceUpdateFoundCvars();
}

static void LS_RaceRefresh_f( void ) {
	int basePort, directBasePort, scanPort;
	netadr_t to;
	const char *ip;
	qboolean directScan;
	basePort = ls_race.portCvar ? ls_race.portCvar->integer : 27960;
	if ( basePort <= 0 ) basePort = 27960;
	ip = ls_race.ipCvar ? ls_race.ipCvar->string : "127.0.0.1";
	directScan = LS_RaceResolveHostAddress( ip, basePort, &to );
	directBasePort = directScan ? LS_RaceAdrPort( to ) : basePort;
	if ( directBasePort <= 0 ) directBasePort = basePort;
	LS_RaceClearFoundLobbies( directScan ? "Scanning LAN and host IP..." : "Scanning local network..." );
	if ( directScan ) {
		LS_RaceSendDiscoverScanTo( ip, directBasePort );
		Com_Printf( "^3Race: scanning host %s on UDP ports %d-%d\n", ip, directBasePort, directBasePort + LS_RACE_PORT_SCAN_SPAN - 1 );
	}
	Com_Printf( "^3Race: scanning local network on UDP ports %d-%d\n", basePort, basePort + LS_RACE_PORT_SCAN_SPAN - 1 );
	for ( scanPort = basePort; scanPort < basePort + LS_RACE_PORT_SCAN_SPAN; scanPort++ ) {
		memset( &to, 0, sizeof( to ) );
		to.type = NA_BROADCAST;
		to.port = BigShort( (short)scanPort );
		NET_OutOfBandPrint( NS_CLIENT, to, "srace discover %d", LS_RACE_PROTO_VERSION );
		to.type = NA_BROADCAST_IPX;
		NET_OutOfBandPrint( NS_CLIENT, to, "srace discover %d", LS_RACE_PROTO_VERSION );
	}
}

static void LS_RaceJoinFound_f( void ) {
	int index, port;
	char host[64];
	if ( Cmd_Argc() < 2 ) index = 0;
	else index = atoi( Cmd_Argv( 1 ) );
	if ( index < 0 || index >= LS_RACE_FOUND_MAX || !ls_race.found[index].used ) {
		LS_RaceSetStatus( "Select a discovered lobby first" );
		return;
	}
	LS_RaceAdrToHostString( ls_race.found[index].adr, host, sizeof( host ) );
	port = LS_RaceAdrPort( ls_race.found[index].adr );
	if ( ls_race.ipCvar ) Cvar_Set( ls_race.ipCvar->name, host );
	if ( ls_race.portCvar ) Cvar_SetValue( ls_race.portCvar->name, port );
	Cbuf_AddText( va( "ls_race_join %s %d\n", host, port ) );
}

static void LS_RaceHandleDiscover( netadr_t from ) {
	char nick[32];
	int color[3];
	int version, port;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	version = Cmd_Argc() > 2 ? atoi( Cmd_Argv( 2 ) ) : 0;
	if ( version != LS_RACE_PROTO_VERSION ) return;
	LS_RaceGetLocalIdentity( nick, sizeof( nick ), color );
	port = Cvar_VariableIntegerValue( "net_port" );
	if ( port <= 0 ) port = ls_race.portCvar ? ls_race.portCvar->integer : 27960;
	NET_OutOfBandPrint( NS_CLIENT, from, "srace found %d %d %s %d %d %d %d %d %d %d %d %d %d %s %d",
		LS_RACE_PROTO_VERSION, ls_race.session, nick, port, ls_race.state,
		LS_RacePlayerCount(), LS_RACE_MAX_PLAYERS, ls_race.mode, ls_race.mission,
		ls_race.percent100, ls_race.difficulty, ls_race.hl1Movement, ls_race.autoJump,
		ls_race.ilMap[0] ? ls_race.ilMap : "escape1", ls_race.antiCheat );
}

static void LS_RaceHandleFound( netadr_t from ) {
	int version, session, port, state, players, maxPlayers, mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, privateLobby, queueCount;
	char nick[32];
	if ( Cmd_Argc() < 16 ) return;
	version = atoi( Cmd_Argv( 2 ) );
	if ( version != LS_RACE_PROTO_VERSION ) return;
	session = atoi( Cmd_Argv( 3 ) );
	LS_RaceSanitizeToken( Cmd_Argv( 4 ), nick, sizeof( nick ) );
	port = atoi( Cmd_Argv( 5 ) );
	state = atoi( Cmd_Argv( 6 ) );
	players = atoi( Cmd_Argv( 7 ) );
	maxPlayers = atoi( Cmd_Argv( 8 ) );
	mode = atoi( Cmd_Argv( 9 ) );
	mission = atoi( Cmd_Argv( 10 ) );
	percent100 = atoi( Cmd_Argv( 11 ) );
	difficulty = atoi( Cmd_Argv( 12 ) );
	hl1Movement = atoi( Cmd_Argv( 13 ) );
	autoJump = atoi( Cmd_Argv( 14 ) );
	antiCheat = Cmd_Argc() > 16 ? atoi( Cmd_Argv( 16 ) ) : 1;
	privateLobby = Cmd_Argc() > 17 ? atoi( Cmd_Argv( 17 ) ) : 0;
	queueCount = Cmd_Argc() > 18 ? atoi( Cmd_Argv( 18 ) ) : 0;
	if ( port > 0 && from.type != NA_IP ) LS_RaceSetAdrPort( &from, port );
	LS_RaceAddFoundLobby( from, session, nick, state, players, maxPlayers, mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, Cmd_Argv( 15 ), privateLobby, queueCount );
}

static void LS_RaceHandleQueued( netadr_t from ) {
	int position = Cmd_Argc() > 2 ? atoi( Cmd_Argv( 2 ) ) : 0;
	int hostState = Cmd_Argc() > 3 ? atoi( Cmd_Argv( 3 ) ) : LS_RACE_STATE_RACING;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	if ( !LS_RaceHostAdrMatches( from ) ) return;
	ls_race.lastHostPacketMs = Sys_Milliseconds();
	if ( position < 1 ) position = 1;
	LS_RaceSetStatus( va( "Queued #%d - %s in progress", position, LS_RaceStateName( (lsRaceState_t)hostState ) ) );
	Com_Printf( "^3Race: queued for next lobby at %s (position %d)\n", NET_AdrToString( from ), position );
}

static void LS_RaceSendStateToHostAdr( netadr_t to, lsRacePlayer_t *p ) {
	if ( !p ) return;
	NET_OutOfBandPrint( NS_CLIENT, to,
		"srace state %d %d %s %d %d %d %s %d %d %d %d %.1f %.1f %.1f %.1f %.1f %d %d %d %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %.1f %.1f %.1f %.1f %d %d",
		ls_race.session, p->slot, p->nick, p->color[0], p->color[1], p->color[2],
		p->map[0] ? p->map : "-", p->loaded ? 1 : 0, p->started ? 1 : 0,
		p->finished ? 1 : 0, p->timeMs, p->x, p->y, p->z, p->yaw, p->speed,
		p->igtMs, p->stageTimeMs, p->stageProgress, p->stageName[0] ? p->stageName : "-",
		p->crouched, p->health, p->armor, p->weapon, p->ammo, p->clip,
		p->objectivesFound, p->objectivesTotal, p->zoneProgress, p->zoneTotal, p->paused ? 1 : 0,
		p->cheatFlags, p->inMenu ? 1 : 0, p->left ? 1 : 0, p->timedOut ? 1 : 0,
		p->legsAnim, p->torsoAnim, p->movementDir, p->eFlags,
		p->pitch, p->vx, p->vy, p->vz, p->groundEntityNum, p->animMovetype );
}

static void LS_RaceSendStateToHost( void ) {
	lsRacePlayer_t *p;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	p = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( !p ) return;
	LS_RaceSendStateToHostAdr( ls_race.hostAdr, p );
}

static void LS_RaceSendStateToHostScan( void ) {
	lsRacePlayer_t *p;
	int basePort, scanPort, exactPort;
	netadr_t probeAdr;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	p = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( !p ) return;
	LS_RaceSendStateToHostAdr( ls_race.hostAdr, p );
	if ( !LS_RaceShouldScanHostPorts() ) return;
	exactPort = LS_RaceAdrPort( ls_race.hostAdr );
	basePort = LS_RacePortScanBase( ls_race.hostAdr, ls_race.hostBasePort );
	for ( scanPort = basePort; scanPort < basePort + LS_RACE_PORT_SCAN_SPAN; scanPort++ ) {
		if ( scanPort == exactPort ) continue;
		probeAdr = ls_race.hostAdr;
		LS_RaceSetAdrPort( &probeAdr, scanPort );
		LS_RaceSendStateToHostAdr( probeAdr, p );
	}
}

static void LS_RaceSendChatTo( netadr_t to, int slot, const char *nick, const char *text ) {
	char cleanNick[32];
	char cleanText[LS_RACE_CHAT_TEXT];
	if ( ls_race.role == LS_RACE_ROLE_NONE || !ls_race.session ) return;
	LS_RaceSanitizeToken( nick && nick[0] ? nick : "Runner", cleanNick, sizeof( cleanNick ) );
	LS_RaceSanitizeChatText( text, cleanText, sizeof( cleanText ) );
	if ( !cleanText[0] ) return;
	NET_OutOfBandPrint( NS_CLIENT, to, "srace chat %d %d %s %s", ls_race.session, slot, cleanNick, cleanText );
}

static void LS_RaceBroadcastChat( int slot, const char *nick, const char *text, int skipSlot ) {
	int i;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used || ls_race.players[i].local || ls_race.players[i].slot == skipSlot ) continue;
		if ( ls_race.players[i].left || ls_race.players[i].timedOut ) continue;
		LS_RaceSendChatTo( ls_race.players[i].adr, slot, nick, text );
	}
}

static void LS_RaceSendChatToHostScan( const char *text ) {
	lsRacePlayer_t *p;
	int basePort, scanPort, exactPort;
	netadr_t probeAdr;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	p = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( !p ) return;
	LS_RaceSendChatTo( ls_race.hostAdr, p->slot, p->nick, text );
	if ( !LS_RaceShouldScanHostPorts() ) return;
	exactPort = LS_RaceAdrPort( ls_race.hostAdr );
	basePort = LS_RacePortScanBase( ls_race.hostAdr, ls_race.hostBasePort );
	for ( scanPort = basePort; scanPort < basePort + LS_RACE_PORT_SCAN_SPAN; scanPort++ ) {
		if ( scanPort == exactPort ) continue;
		probeAdr = ls_race.hostAdr;
		LS_RaceSetAdrPort( &probeAdr, scanPort );
		LS_RaceSendChatTo( probeAdr, p->slot, p->nick, text );
	}
}

static void LS_RaceSendEventTo( netadr_t to, int slot, const char *kind, const char *detail ) {
	char cleanKind[32];
	char cleanDetail[LS_RACE_CHAT_TEXT];
	if ( ls_race.role != LS_RACE_ROLE_CLIENT || !ls_race.session ) return;
	LS_RaceSanitizeToken( kind && kind[0] ? kind : "event", cleanKind, sizeof( cleanKind ) );
	LS_RaceSanitizeChatText( detail && detail[0] ? detail : "none", cleanDetail, sizeof( cleanDetail ) );
	if ( !cleanDetail[0] ) Q_strncpyz( cleanDetail, "none", sizeof( cleanDetail ) );
	NET_OutOfBandPrint( NS_CLIENT, to, "srace event %d %d %s %s", ls_race.session, slot, cleanKind, cleanDetail );
}

static void LS_RaceSendEventToHostScan( const char *kind, const char *detail ) {
	lsRacePlayer_t *p;
	int basePort, scanPort, exactPort;
	netadr_t probeAdr;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	p = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( !p ) return;
	LS_RaceSendEventTo( ls_race.hostAdr, p->slot, kind, detail );
	if ( !LS_RaceShouldScanHostPorts() ) return;
	exactPort = LS_RaceAdrPort( ls_race.hostAdr );
	basePort = LS_RacePortScanBase( ls_race.hostAdr, ls_race.hostBasePort );
	for ( scanPort = basePort; scanPort < basePort + LS_RACE_PORT_SCAN_SPAN; scanPort++ ) {
		if ( scanPort == exactPort ) continue;
		probeAdr = ls_race.hostAdr;
		LS_RaceSetAdrPort( &probeAdr, scanPort );
		LS_RaceSendEventTo( probeAdr, p->slot, kind, detail );
	}
}

static void LS_RaceSendLoadTo( netadr_t to ) {
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	NET_OutOfBandPrint( NS_CLIENT, to, "srace load %d %d %d %d %d %d %d %s %s %d",
		ls_race.session, ls_race.mode, ls_race.mission, ls_race.percent100,
		ls_race.difficulty, ls_race.hl1Movement, ls_race.autoJump, ls_race.ilMap, ls_race.targetMap, ls_race.antiCheat );
}

static void LS_RaceBroadcastLoad( void ) {
	int i;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used || ls_race.players[i].local ) continue;
		LS_RaceSendLoadTo( ls_race.players[i].adr );
	}
}

static void LS_RaceSendConfigTo( netadr_t to ) {
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	NET_OutOfBandPrint( NS_CLIENT, to, "srace config %d %d %d %d %d %d %d %s %d",
		ls_race.session, ls_race.mode, ls_race.mission, ls_race.percent100,
		ls_race.difficulty, ls_race.hl1Movement, ls_race.autoJump,
		ls_race.ilMap[0] ? ls_race.ilMap : "escape1", ls_race.antiCheat );
}

static void LS_RaceBroadcastConfig( void ) {
	int i;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used || ls_race.players[i].local ) continue;
		LS_RaceSendConfigTo( ls_race.players[i].adr );
	}
}

static void LS_RaceSendCountdownTo( netadr_t to ) {
	int now, remainingMs;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	now = Sys_Milliseconds();
	remainingMs = LS_RaceCountdownRemaining( now );
	NET_OutOfBandPrint( NS_CLIENT, to, "srace countdown %d %d %d", ls_race.session, ls_race.countdownMs, remainingMs );
}

static void LS_RaceBroadcastCountdown( void ) {
	int i;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used || ls_race.players[i].local ) continue;
		LS_RaceSendCountdownTo( ls_race.players[i].adr );
	}
}

static void LS_RaceSendStartTo( netadr_t to ) {
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	NET_OutOfBandPrint( NS_CLIENT, to, "srace start %d", ls_race.session );
}

static void LS_RaceBroadcastStart( void ) {
	int i;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !ls_race.players[i].used || ls_race.players[i].local ) continue;
		LS_RaceSendStartTo( ls_race.players[i].adr );
	}
}

static void LS_RaceSendControlStateTo( netadr_t to ) {
	if ( ls_race.state == LS_RACE_STATE_LOBBY ) {
		LS_RaceSendConfigTo( to );
	} else if ( ls_race.state == LS_RACE_STATE_LOADING ) {
		LS_RaceSendLoadTo( to );
	} else if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		LS_RaceSendLoadTo( to );
		LS_RaceSendCountdownTo( to );
	} else if ( ls_race.state == LS_RACE_STATE_RACING || ls_race.state == LS_RACE_STATE_FINISHED ) {
		LS_RaceSendStartTo( to );
	}
}

static qboolean LS_RaceStartMapLoad( qboolean force ) {
	char cmd[128];
	if ( !ls_race.targetMap[0] ) return qfalse;
	if ( !force && ls_race.loadIssued && !Q_stricmp( ls_race.loadIssuedMap, ls_race.targetMap ) ) {
		return qfalse;
	}
	if ( CL_SpeedrunImGui_IsOpen() ) {
		CL_SpeedrunImGui_CloseAllForGameplay();
	}
	LS_RaceDisableProtectedOptions();
	LS_DoResetNoSave();
	Cvar_Set( "savegame_loading", "0" );
	Cvar_Set( "savegame_filename", "" );
	Cvar_Set( "g_reloading", "0" );
	Cvar_Set( "cl_paused", "0" );
	Cvar_Set( "g_playerstart", "0" );
	LS_RaceRestoreGameplayRenderCvars();
	ls_race.playerStartIssued = qfalse;
	ls_race.playerStartProcessed = qfalse;
	/* If a previous mission ended these cvars stick at non-default values
	   and CG_DrawActive / cg_view skip world rendering entirely, leaving
	   the joiner staring at an empty map with no walls. */
	Cvar_Set( "g_missionStats", "0" );
	Cvar_Set( "cg_norender", "0" );
	Com_sprintf( cmd, sizeof( cmd ), "spmap %s\n", ls_race.targetMap );
	Cbuf_AddText( cmd );
	ls_race.loadIssued = qtrue;
	ls_race.loadIssuedMs = Sys_Milliseconds();
	Q_strncpyz( ls_race.loadIssuedMap, ls_race.targetMap, sizeof( ls_race.loadIssuedMap ) );
	return qtrue;
}

static qboolean LS_RaceStillOnWrongActiveMap( void ) {
	char currentMap[LS_MAX_MAPNAME];
	if ( !ls_race.targetMap[0] ) return qfalse;
	if ( cls.state < CA_ACTIVE || !cl.mapname[0] ) return qfalse;
	LS_ExtractMapname( cl.mapname, currentMap, sizeof( currentMap ) );
	return Q_stricmp( currentMap, ls_race.targetMap ) ? qtrue : qfalse;
}

static qboolean LS_RaceUserInterfaceActive( void ) {
	if ( cls.keyCatchers & ( KEYCATCH_UI | KEYCATCH_CONSOLE ) ) return qtrue;
	if ( CL_SpeedrunImGui_IsOpen() ) return qtrue;
	return qfalse;
}

static void LS_RaceReleaseGate( void ) {
	if ( CL_SpeedrunImGui_IsOpen() ) {
		CL_SpeedrunImGui_CloseAllForGameplay();
	}
	if ( cls.keyCatchers & KEYCATCH_UI ) {
		Key_SetCatcher( cls.keyCatchers & ~KEYCATCH_UI );
	}
	Cvar_Set( "cl_paused", "0" );
}

static qboolean LS_RaceEnsurePlayerStart( void ) {
	if ( ls_race.playerStartProcessed ) return qtrue;

	if ( ls_race.playerStartIssued ) {
		if ( Cvar_VariableIntegerValue( "g_playerstart" ) == 0 ) {
			ls_race.playerStartProcessed = qtrue;
			return qtrue;
		}
		return qfalse;
	}

	/* Race bypasses the stock pregame menu, so manually fire the same
	   playerstart hook that the Start button would trigger. This lets
	   SP scripts/cameras leave their pregame state before countdown GO. */
	Cbuf_AddText( "fade 0 0 0 0 3\n" );
	Cvar_Set( "g_playerstart", "1" );
	Cvar_Set( "ls_loading", "0" );
	ls_race.playerStartIssued = qtrue;
	return qfalse;
}

static void LS_RaceBeginRun( void ) {
	lsRacePlayer_t *p;
	if ( ls_race.startIssued ) {
		ls_race.state = LS_RACE_STATE_RACING;
		return;
	}
	if ( CL_SpeedrunImGui_IsOpen() ) {
		CL_SpeedrunImGui_CloseAllForGameplay();
	}
	ls_race.startIssued = qtrue;
	ls_race.state = LS_RACE_STATE_RACING;
	ls_race.raceStartMs = Sys_Milliseconds();
	/* Re-clear any pregame render blockers right at GO. The stock SP flow
	   would already have done this via the pregame Start button. */
	Cvar_Set( "cg_norender", "0" );
	Cvar_Set( "g_missionStats", "0" );
	LS_RaceRestoreGameplayRenderCvars();
	LS_RaceReleaseGate();
	ls_raceForceStart = qtrue;
	LS_Start_f();
	ls_raceForceStart = qfalse;
	p = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( p ) {
		p->started = qtrue;
		p->loaded = qtrue;
	}
	LS_RaceSetStatus( "Race started" );
}

static qboolean LS_RaceLocalLoaded( void ) {
	char currentMap[LS_MAX_MAPNAME];
	if ( !ls_race.targetMap[0] ) return qfalse;
	if ( cls.state != CA_ACTIVE ) return qfalse;
	if ( !cl.mapname[0] ) return qfalse;
	if ( !cl.snap.valid || cl.snap.serverTime <= 0 ) return qfalse;
	if ( !LS_Draw2DReady() ) return qfalse;
	LS_ExtractMapname( cl.mapname, currentMap, sizeof( currentMap ) );
	if ( !currentMap[0] || Q_stricmp( currentMap, ls_race.targetMap ) ) return qfalse;
	if ( Cvar_VariableIntegerValue( "savegame_loading" ) ) return qfalse;
	if ( Cvar_VariableIntegerValue( "g_reloading" ) ) return qfalse;
	/* A stale g_missionStats or cg_norender from the previous mission/save
	   keeps the world from rendering ("empty map" bug on race joiners).
	   The map IS loaded and the snap is valid, so it's safe to clear them. */
	if ( Cvar_VariableIntegerValue( "cg_norender" ) ) Cvar_Set( "cg_norender", "0" );
	{
		char missionStats[16];
		Cvar_VariableStringBuffer( "g_missionStats", missionStats, sizeof( missionStats ) );
		if ( missionStats[0] && strlen( missionStats ) > 1 ) Cvar_Set( "g_missionStats", "0" );
	}
	if ( !LS_RaceEnsurePlayerStart() ) return qfalse;
	if ( LS_RaceUserInterfaceActive() ) return qfalse;
	if ( ls.mapLoadFreeze ) ls.mapLoadFreeze = qfalse;
	if ( Cvar_VariableIntegerValue( "ls_loading" ) ) Cvar_Set( "ls_loading", "0" );
	return qtrue;
}

static qboolean LS_RaceAllLoaded( void ) {
	int i;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( LS_RacePlayerBlocksReady( &ls_race.players[i] ) && !ls_race.players[i].loaded ) return qfalse;
	}
	return qtrue;
}

static qboolean LS_RaceAllFinished( void ) {
	int i, count = 0;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		if ( !LS_RacePlayerBlocksReady( &ls_race.players[i] ) ) continue;
		count++;
		if ( !ls_race.players[i].finished ) return qfalse;
	}
	return count > 0 ? qtrue : qfalse;
}

static void LS_RaceUpdateLocalPlayer( void ) {
	lsRacePlayer_t *p;
	char nick[32];
	int color[3];
	int now = Sys_Milliseconds();
	p = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( !p ) return;
	LS_RaceGetLocalIdentity( nick, sizeof( nick ), color );
	Q_strncpyz( p->nick, nick, sizeof( p->nick ) );
	p->color[0] = color[0]; p->color[1] = color[1]; p->color[2] = color[2];
	if ( cl.mapname[0] ) LS_ExtractMapname( cl.mapname, p->map, sizeof( p->map ) );
	else p->map[0] = '\0';
	if ( cls.state >= CA_ACTIVE ) {
		int weapon;
		int ammoIndex;
		int clipIndex;
		vec3_t hvel;
		p->x = cl.snap.ps.origin[0];
		p->y = cl.snap.ps.origin[1];
		p->z = cl.snap.ps.origin[2];
		p->yaw = cl.snap.ps.viewangles[1];
		p->pitch = cl.snap.ps.viewangles[0];
		p->vx = cl.snap.ps.velocity[0];
		p->vy = cl.snap.ps.velocity[1];
		p->vz = cl.snap.ps.velocity[2];
		hvel[0] = p->vx;
		hvel[1] = p->vy;
		hvel[2] = 0.0f;
		p->speed = VectorLength( hvel );
		p->crouched = LS_LocalPlayerCrouched() ? 1 : 0;
		p->legsAnim = cl.snap.ps.legsAnim;
		p->torsoAnim = cl.snap.ps.torsoAnim;
		p->movementDir = LS_RaceNormalizeMovementDir( cl.snap.ps.movementDir );
		p->eFlags = cl.snap.ps.eFlags;
		p->groundEntityNum = cl.snap.ps.groundEntityNum;
		p->animMovetype = LS_RaceAnimMovetypeForPlayerState( &cl.snap.ps );
		p->health = cl.snap.ps.stats[STAT_HEALTH];
		p->armor = cl.snap.ps.stats[STAT_ARMOR];
		weapon = cl.snap.ps.weapon;
		if ( weapon < 0 || weapon >= WP_NUM_WEAPONS ) weapon = 0;
		p->weapon = weapon;
		ammoIndex = LS_RaceAmmoIndexForWeapon( weapon );
		clipIndex = LS_RaceClipIndexForWeapon( weapon );
		p->ammo = LS_RaceSafePlayerStat( cl.snap.ps.ammo, ammoIndex );
		p->clip = LS_RaceSafePlayerStat( cl.snap.ps.ammoclip, clipIndex );
	}
	p->objectivesFound = ls.liveObjectivesFound;
	p->objectivesTotal = ls.liveObjectivesTotal;
	p->zoneProgress = Cvar_VariableIntegerValue( "sp_zone_race_points" );
	p->zoneTotal = Cvar_VariableIntegerValue( "sp_zone_race_total" );
	if ( p->zoneTotal <= 0 ) {
		p->zoneProgress = Cvar_VariableIntegerValue( "sp_zone_completed_index" );
		p->zoneTotal = Cvar_VariableIntegerValue( "sp_zone_progress_count" );
	}
	if ( ls_race.state == LS_RACE_STATE_LOADING || ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		p->loaded = LS_RaceLocalLoaded();
	}
	LS_RaceUpdateStageFields( p );
	if ( ls.runFinished ) {
		if ( !p->finished ) p->timeMs = LS_RaceElapsedMs( now );
		p->finished = qtrue;
	} else if ( ls_race.state == LS_RACE_STATE_RACING || p->started ) {
		p->timeMs = LS_RaceElapsedMs( now );
	} else if ( ls_race.state < LS_RACE_STATE_RACING ) {
		p->timeMs = 0;
	}
	p->paused = ( p->started && ( ls.manualPause || ( cl_paused && cl_paused->integer ) || LS_RaceUserInterfaceActive() ) ) ? qtrue : qfalse;
	p->inMenu = ( cls.state < CA_ACTIVE || LS_RaceUserInterfaceActive() ) ? qtrue : qfalse;
	p->left = qfalse;
	p->timedOut = qfalse;
	p->cheatFlags = LS_RaceLocalCheatFlags();
	p->lastHeardMs = now;
}

static void LS_RaceResetSession( void ) {
	ls_race.role = LS_RACE_ROLE_NONE;
	ls_race.state = LS_RACE_STATE_IDLE;
	ls_race.session = 0;
	ls_race.localSlot = -1;
	ls_race.hostBasePort = 0;
	ls_race.lastSendMs = 0;
	ls_race.lastRosterMs = 0;
	ls_race.lastHelloMs = 0;
	ls_race.lastHostPacketMs = 0;
	ls_race.lastLoadBroadcastMs = 0;
	ls_race.lastCountdownBroadcastMs = 0;
	ls_race.lastStartBroadcastMs = 0;
	ls_race.countdownStartMs = 0;
	ls_race.countdownMs = 5000;
	ls_race.raceStartMs = 0;
	ls_race.playerStartIssued = qfalse;
	ls_race.playerStartProcessed = qfalse;
	ls_race.startIssued = qfalse;
	ls_race.pendingStart = qfalse;
	ls_race.loadIssued = qfalse;
	ls_race.loadIssuedMs = 0;
	ls_race.loadRetryCount = 0;
	ls_race.loadIssuedMap[0] = '\0';
	ls_race.targetMap[0] = '\0';
	ls_race.ilMap[0] = '\0';
	ls_race.mode = LS_MODE_FULLGAME;
	ls_race.mission = 1;
	ls_race.percent100 = 0;
	ls_race.difficulty = LS_DetectDifficulty();
	ls_race.hl1Movement = LS_HL1ModeActive() ? 1 : 0;
	ls_race.autoJump = ( ls_race.hl1Movement && Cvar_VariableIntegerValue( "bh_autojump" ) ) ? 1 : 0;
	ls_race.antiCheat = Cvar_VariableIntegerValue( "ls_race_anticheat" ) ? 1 : 0;
	ls_race.lastNoclipDisableMs = 0;
	memset( &ls_race.hostAdr, 0, sizeof( ls_race.hostAdr ) );
	LS_RaceClearPlayers();
	LS_RaceClearGhostCvars();
	LS_RaceClearChat();
	LS_RaceSetStatus( "" );
	LS_RaceUpdateRuntimeCvars();
}

static void LS_RaceHost_f( void ) {
	int port;
	const char *bindIp;
	if ( !LS_RaceApplyHostNetwork( &port ) ) return;
	bindIp = LS_RaceHostBindIp();
	LS_RaceCaptureSavedSettings();
	LS_RaceResetSession();
	ls_race.role = LS_RACE_ROLE_HOST;
	ls_race.state = LS_RACE_STATE_LOBBY;
	ls_race.session = Sys_Milliseconds() & 0x7fffffff;
	LS_RaceInitLocalPlayer( 0 );
	LS_RaceSyncSettingsFromCvars();
	LS_RaceSetStatus( va( "Lobby hosted on %s:%d", bindIp, port ) );
	Com_Printf( "^2Race: hosting lobby on %s:%d. Join with LAN IP, or public IP when this UDP port is forwarded/open.\n", bindIp, port );
	Sys_ShowIP();
	LS_RaceUpdateRuntimeCvars();
}

static void LS_RaceJoin_f( void ) {
	char addrText[128];
	const char *ip;
	int port;
	int oldSession = ls_race.session;
	int oldSlot = ls_race.localSlot;
	netadr_t adr;
	if ( Cmd_Argc() >= 2 ) ip = Cmd_Argv( 1 );
	else ip = ls_race.ipCvar ? ls_race.ipCvar->string : "127.0.0.1";
	if ( Cmd_Argc() >= 3 ) port = atoi( Cmd_Argv( 2 ) );
	else port = ls_race.portCvar ? ls_race.portCvar->integer : 27960;
	if ( port <= 0 ) port = 27960;
	Com_sprintf( addrText, sizeof( addrText ), "%s:%d", ip, port );
	if ( !NET_StringToAdr( addrText, &adr ) ) {
		LS_RaceSetStatus( "Invalid host address" );
		Com_Printf( "^1Race: invalid address '%s'\n", addrText );
		return;
	}
	port = LS_RaceAdrPort( adr );
	if ( port <= 0 ) port = 27960;
	LS_RaceCaptureSavedSettings();
	LS_RaceResetSession();
	ls_race.role = LS_RACE_ROLE_CLIENT;
	ls_race.state = LS_RACE_STATE_CONNECTING;
	ls_race.session = oldSession;
	ls_race.hostAdr = adr;
	ls_race.hostBasePort = port;
	ls_race.localSlot = oldSlot;
	ls_race.lastHostPacketMs = Sys_Milliseconds();
	if ( ls_race.ipCvar ) Cvar_Set( ls_race.ipCvar->name, ip );
	if ( ls_race.portCvar ) Cvar_SetValue( ls_race.portCvar->name, port );
	LS_RaceSetStatus( va( "Connecting to %s, probing ports %d-%d", ip, port, port + LS_RACE_PORT_SCAN_SPAN - 1 ) );
	Com_Printf( "^3Race: sending hello to %s, probing UDP ports %d-%d\n", ip, port, port + LS_RACE_PORT_SCAN_SPAN - 1 );
	LS_RaceSendHello();
	LS_RaceUpdateRuntimeCvars();
}

static void LS_RaceLeave_f( void ) {
	if ( ls_race.role == LS_RACE_ROLE_CLIENT && ls_race.session ) {
		NET_OutOfBandPrint( NS_CLIENT, ls_race.hostAdr, "srace leave %d %d", ls_race.session, ls_race.localSlot );
	}
	LS_RaceResetSession();
	LS_RaceRestoreSavedSettings();
	Cvar_Set( "cl_paused", "0" );
	Com_Printf( "^3Race: left lobby\n" );
}

static void LS_RaceStart_f( void ) {
	lsRacePlayer_t *p;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) {
		LS_RaceSetStatus( "Only the host can start the race" );
		return;
	}
	if ( ls_race.state != LS_RACE_STATE_LOBBY ) {
		LS_RaceSetStatus( "Race already started" );
		return;
	}
	Q_strncpyz( ls_race.targetMap, LS_RaceSelectStartMap(), sizeof( ls_race.targetMap ) );
	ls_race.countdownMs = ( ls_race.countdownCvar ? ls_race.countdownCvar->integer : 5 ) * 1000;
	if ( ls_race.countdownMs < 1000 ) ls_race.countdownMs = 1000;
	if ( ls_race.countdownMs > 30000 ) ls_race.countdownMs = 30000;
	LS_RaceDisableProtectedOptions();
	LS_RaceApplySettings( ls_race.mode, ls_race.mission, ls_race.percent100, ls_race.difficulty,
		ls_race.hl1Movement, ls_race.autoJump, ls_race.antiCheat, ls_race.ilMap );
	for ( p = ls_race.players; p < ls_race.players + LS_RACE_MAX_PLAYERS; p++ ) {
		if ( p->used ) {
			p->loaded = qfalse;
			p->started = qfalse;
			p->finished = qfalse;
			p->timeMs = 0;
		}
	}
	ls_race.state = LS_RACE_STATE_LOADING;
	ls_race.startIssued = qfalse;
	ls_race.pendingStart = qfalse;
	ls_race.loadIssued = qfalse;
	ls_race.loadIssuedMs = 0;
	ls_race.loadRetryCount = 0;
	ls_race.loadIssuedMap[0] = '\0';
	ls_race.lastLoadBroadcastMs = 0;
	ls_race.lastCountdownBroadcastMs = 0;
	ls_race.lastStartBroadcastMs = 0;
	ls_race.raceStartMs = 0;
	LS_RaceBroadcastLoad();
	ls_race.lastLoadBroadcastMs = Sys_Milliseconds();
	LS_RaceStartMapLoad( qtrue );
	LS_RaceSetStatus( va( "Loading %s", ls_race.targetMap ) );
	Com_Printf( "^2Race: loading %s for all players\n", ls_race.targetMap );
}

static void LS_RaceHandleHello( netadr_t from ) {
	lsRacePlayer_t *p;
	int version, r, g, b, helloSession, helloSlot;
	qboolean newPlayer = qfalse;
	char nick[32];
	char welcomeMap[LS_MAX_MAPNAME];
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	version = atoi( Cmd_Argv( 2 ) );
	if ( version != LS_RACE_PROTO_VERSION ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "srace reject Version_mismatch" );
		return;
	}
	LS_RaceSanitizeToken( Cmd_Argv( 3 ), nick, sizeof( nick ) );
	r = atoi( Cmd_Argv( 4 ) ); g = atoi( Cmd_Argv( 5 ) ); b = atoi( Cmd_Argv( 6 ) );
	helloSession = Cmd_Argc() > 7 ? atoi( Cmd_Argv( 7 ) ) : 0;
	helloSlot = Cmd_Argc() > 8 ? atoi( Cmd_Argv( 8 ) ) : -1;
	p = LS_RaceFindPlayerByAdr( from );
	if ( !p && helloSession == ls_race.session ) {
		p = LS_RaceFindPlayerBySlot( helloSlot );
		if ( p && p->local ) p = NULL;
	}
	if ( !p && ls_race.state != LS_RACE_STATE_LOBBY ) p = LS_RaceFindPlayerForReconnect( from, nick );
	if ( ls_race.state != LS_RACE_STATE_LOBBY && !p ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "srace reject Run_in_progress_wait_for_next_lobby" );
		return;
	}
	if ( !p ) {
		p = LS_RaceAllocPlayer( from );
		newPlayer = p ? qtrue : qfalse;
	}
	if ( !p ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "srace reject Lobby_full" );
		return;
	}
	p->adr = from;
	if ( !p->basePort ) p->basePort = LS_RaceAdrPort( from );
	Q_strncpyz( welcomeMap, ( ls_race.state >= LS_RACE_STATE_RACING && p->map[0] ) ? p->map : ( ls_race.targetMap[0] ? ls_race.targetMap : "-" ), sizeof( welcomeMap ) );
	p->left = qfalse;
	p->timedOut = qfalse;
	p->inMenu = qfalse;
	Q_strncpyz( p->nick, nick, sizeof( p->nick ) );
	p->color[0] = (int)Com_Clamp( 0.0f, 255.0f, (float)r );
	p->color[1] = (int)Com_Clamp( 0.0f, 255.0f, (float)g );
	p->color[2] = (int)Com_Clamp( 0.0f, 255.0f, (float)b );
	p->lastHeardMs = Sys_Milliseconds();
	NET_OutOfBandPrint( NS_CLIENT, from, "srace welcome %d %d %d %d %d %d %d %d %s %s %d %d",
		ls_race.session, p->slot, ls_race.mode, ls_race.mission, ls_race.percent100,
		ls_race.difficulty, ls_race.hl1Movement, ls_race.autoJump,
		ls_race.ilMap[0] ? ls_race.ilMap : "escape1", welcomeMap, ls_race.state, ls_race.antiCheat );
	if ( newPlayer ) {
		Com_Printf( "^2Race: player joined from %s, slot %d (%s)\n", NET_AdrToString( from ), p->slot, p->nick );
	}
	LS_RaceSendRosterTo( from );
	LS_RaceSendControlStateTo( from );
	LS_RaceBroadcastRoster();
	if ( ls_race.state == LS_RACE_STATE_LOBBY ) {
		LS_RaceSetStatus( va( "Lobby: %d player(s)", LS_RacePlayerCount() ) );
	}
}

static void LS_RaceHandleWelcome( netadr_t from ) {
	lsRacePlayer_t *p;
	int session, slot, mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, hostState, now;
	lsRaceState_t previousState;
	qboolean sameSession, keepLocalState, announceWelcome, needsMapLoad;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	if ( !LS_RaceHostAdrMatches( from ) ) return;
	if ( Cmd_Argc() < 12 ) return;
	session = atoi( Cmd_Argv( 2 ) );
	slot = atoi( Cmd_Argv( 3 ) );
	mode = atoi( Cmd_Argv( 4 ) );
	mission = atoi( Cmd_Argv( 5 ) );
	percent100 = atoi( Cmd_Argv( 6 ) );
	difficulty = Cmd_Argc() > 7 ? atoi( Cmd_Argv( 7 ) ) : LS_DetectDifficulty();
	hl1Movement = Cmd_Argc() > 8 ? atoi( Cmd_Argv( 8 ) ) : ( LS_HL1ModeActive() ? 1 : 0 );
	autoJump = Cmd_Argc() > 9 ? atoi( Cmd_Argv( 9 ) ) : Cvar_VariableIntegerValue( "bh_autojump" );
	hostState = Cmd_Argc() > 12 ? atoi( Cmd_Argv( 12 ) ) : LS_RACE_STATE_LOBBY;
	antiCheat = Cmd_Argc() > 13 ? atoi( Cmd_Argv( 13 ) ) : 1;
	if ( hostState < LS_RACE_STATE_LOBBY || hostState > LS_RACE_STATE_FINISHED ) hostState = LS_RACE_STATE_LOBBY;
	sameSession = ( ls_race.session == session && session != 0 ) ? qtrue : qfalse;
	keepLocalState = ( sameSession && slot == ls_race.localSlot && slot >= 0 && slot < LS_RACE_MAX_PLAYERS && ls_race.players[slot].used ) ? qtrue : qfalse;
	now = Sys_Milliseconds();
	previousState = ls_race.state;
	ls_race.session = session;
	ls_race.hostAdr = from;
	if ( !ls_race.hostBasePort ) ls_race.hostBasePort = LS_RaceAdrPort( from );
	ls_race.lastHostPacketMs = now;
	if ( ls_race.portCvar ) Cvar_SetValue( ls_race.portCvar->name, LS_RaceAdrPort( from ) );
	LS_RaceApplySettings( mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, Cmd_Argv( 10 ) );
	Q_strncpyz( ls_race.targetMap, Cmd_Argv( 11 ), sizeof( ls_race.targetMap ) );
	if ( !Q_stricmp( ls_race.targetMap, "-" ) ) ls_race.targetMap[0] = '\0';
	if ( keepLocalState ) {
		p = &ls_race.players[slot];
		p->local = qtrue;
		p->lastHeardMs = now;
	} else {
		LS_RaceInitLocalPlayer( slot );
	}
	if ( !sameSession || ls_race.state == LS_RACE_STATE_CONNECTING || hostState > ls_race.state ) {
		ls_race.state = (lsRaceState_t)hostState;
	}
	needsMapLoad = ( ls_race.targetMap[0] && ls_race.state >= LS_RACE_STATE_LOADING &&
		( cls.state < CA_ACTIVE || LS_RaceStillOnWrongActiveMap() ) ) ? qtrue : qfalse;
	if ( needsMapLoad && sameSession && keepLocalState && hostState >= LS_RACE_STATE_RACING &&
		 ( previousState == LS_RACE_STATE_RACING || previousState == LS_RACE_STATE_FINISHED || ls_race.startIssued ) ) {
		needsMapLoad = qfalse;
	}
	if ( needsMapLoad ) {
		if ( hostState >= LS_RACE_STATE_RACING ) {
			ls_race.pendingStart = qtrue;
			ls_race.startIssued = qfalse;
			ls_race.state = LS_RACE_STATE_LOADING;
		}
		LS_RaceStartMapLoad( qfalse );
	} else if ( ls_race.state >= LS_RACE_STATE_RACING ) {
		LS_RaceBeginRun();
	}
	announceWelcome = ( !sameSession || !keepLocalState || previousState == LS_RACE_STATE_CONNECTING ) ? qtrue : qfalse;
	if ( ls_race.state == LS_RACE_STATE_LOBBY ) LS_RaceSetStatus( va( "Joined lobby at %s", NET_AdrToString( from ) ) );
	else LS_RaceSetStatus( LS_RaceStateName( ls_race.state ) );
	if ( announceWelcome ) {
		Com_Printf( "^2Race: joined lobby at %s as slot %d\n", NET_AdrToString( from ), slot );
	}
}

static void LS_RaceParsePlayerTelemetryArgs( lsRacePlayer_t *p ) {
	if ( !p ) return;
	if ( Cmd_Argc() > 22 ) p->crouched = atoi( Cmd_Argv( 22 ) );
	if ( Cmd_Argc() > 23 ) p->health = atoi( Cmd_Argv( 23 ) );
	if ( Cmd_Argc() > 24 ) p->armor = atoi( Cmd_Argv( 24 ) );
	if ( Cmd_Argc() > 25 ) p->weapon = atoi( Cmd_Argv( 25 ) );
	if ( Cmd_Argc() > 26 ) p->ammo = atoi( Cmd_Argv( 26 ) );
	if ( Cmd_Argc() > 27 ) p->clip = atoi( Cmd_Argv( 27 ) );
	if ( Cmd_Argc() > 28 ) p->objectivesFound = atoi( Cmd_Argv( 28 ) );
	if ( Cmd_Argc() > 29 ) p->objectivesTotal = atoi( Cmd_Argv( 29 ) );
	if ( Cmd_Argc() > 30 ) p->zoneProgress = atoi( Cmd_Argv( 30 ) );
	if ( Cmd_Argc() > 31 ) p->zoneTotal = atoi( Cmd_Argv( 31 ) );
	if ( Cmd_Argc() > 32 ) p->paused = atoi( Cmd_Argv( 32 ) ) ? qtrue : qfalse;
	else p->paused = qfalse;
	if ( Cmd_Argc() > 33 ) p->cheatFlags = atoi( Cmd_Argv( 33 ) );
	else p->cheatFlags = 0;
	if ( Cmd_Argc() > 34 ) p->inMenu = atoi( Cmd_Argv( 34 ) ) ? qtrue : qfalse;
	else p->inMenu = qfalse;
	if ( Cmd_Argc() > 35 ) p->left = atoi( Cmd_Argv( 35 ) ) ? qtrue : qfalse;
	else p->left = qfalse;
	if ( Cmd_Argc() > 36 ) p->timedOut = atoi( Cmd_Argv( 36 ) ) ? qtrue : qfalse;
	else p->timedOut = qfalse;
	if ( Cmd_Argc() > 37 ) p->legsAnim = atoi( Cmd_Argv( 37 ) );
	else p->legsAnim = -1;
	if ( Cmd_Argc() > 38 ) p->torsoAnim = atoi( Cmd_Argv( 38 ) );
	else p->torsoAnim = -1;
	if ( Cmd_Argc() > 39 ) p->movementDir = atoi( Cmd_Argv( 39 ) );
	else p->movementDir = 0;
	if ( Cmd_Argc() > 40 ) p->eFlags = atoi( Cmd_Argv( 40 ) );
	else p->eFlags = 0;
	if ( Cmd_Argc() > 41 ) p->pitch = (float)atof( Cmd_Argv( 41 ) );
	else p->pitch = 0.0f;
	if ( Cmd_Argc() > 42 ) p->vx = (float)atof( Cmd_Argv( 42 ) );
	else p->vx = 0.0f;
	if ( Cmd_Argc() > 43 ) p->vy = (float)atof( Cmd_Argv( 43 ) );
	else p->vy = 0.0f;
	if ( Cmd_Argc() > 44 ) p->vz = (float)atof( Cmd_Argv( 44 ) );
	else p->vz = 0.0f;
	if ( Cmd_Argc() > 45 ) p->groundEntityNum = atoi( Cmd_Argv( 45 ) );
	else p->groundEntityNum = ENTITYNUM_WORLD;
	if ( Cmd_Argc() > 46 ) p->animMovetype = atoi( Cmd_Argv( 46 ) );
	else p->animMovetype = 0;
	if ( p->health < 0 ) p->health = 0;
	if ( p->cheatFlags < 0 ) p->cheatFlags = 0;
	if ( p->armor < 0 ) p->armor = 0;
	if ( p->weapon < 0 || p->weapon >= WP_NUM_WEAPONS ) p->weapon = 0;
	if ( p->ammo < 0 ) p->ammo = 0;
	if ( p->clip < 0 ) p->clip = 0;
	if ( p->groundEntityNum < 0 || p->groundEntityNum > ENTITYNUM_NONE ) p->groundEntityNum = ENTITYNUM_WORLD;
	if ( p->animMovetype < 0 ) p->animMovetype = 0;
	p->movementDir = LS_RaceNormalizeMovementDir( p->movementDir );
	if ( p->objectivesFound < 0 ) p->objectivesFound = 0;
	if ( p->objectivesTotal < 0 ) p->objectivesTotal = 0;
	if ( p->zoneProgress < 0 ) p->zoneProgress = 0;
	if ( p->zoneTotal < 0 ) p->zoneTotal = 0;
}

static void LS_RaceHandlePlayer( netadr_t from ) {
	lsRacePlayer_t *p;
	int session, slot, now;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	if ( !LS_RaceHostAdrMatches( from ) ) return;
	now = Sys_Milliseconds();
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	slot = atoi( Cmd_Argv( 3 ) );
	if ( slot < 0 || slot >= LS_RACE_MAX_PLAYERS ) return;
	if ( slot == ls_race.localSlot ) {
		p = &ls_race.players[slot];
		if ( !p->used ) LS_RaceInitLocalPlayer( slot );
		else p->local = qtrue;
		ls_race.lastHostPacketMs = now;
		return;
	}
	p = &ls_race.players[slot];
	if ( !p->used ) {
		memset( p, 0, sizeof( *p ) );
		LS_RaceInitPlayerDefaults( p );
		p->used = qtrue;
		p->slot = slot;
	}
	p->local = qfalse;
	if ( p->lastSampleMs ) LS_RaceUpdatePlayerRender( p, now );
	LS_RaceSanitizeToken( Cmd_Argv( 4 ), p->nick, sizeof( p->nick ) );
	p->color[0] = atoi( Cmd_Argv( 5 ) );
	p->color[1] = atoi( Cmd_Argv( 6 ) );
	p->color[2] = atoi( Cmd_Argv( 7 ) );
	Q_strncpyz( p->map, Cmd_Argv( 8 ), sizeof( p->map ) );
	if ( !Q_stricmp( p->map, "-" ) ) p->map[0] = '\0';
	p->loaded = atoi( Cmd_Argv( 9 ) ) ? qtrue : qfalse;
	p->started = atoi( Cmd_Argv( 10 ) ) ? qtrue : qfalse;
	p->finished = atoi( Cmd_Argv( 11 ) ) ? qtrue : qfalse;
	p->timeMs = atoi( Cmd_Argv( 12 ) );
	p->x = (float)atof( Cmd_Argv( 13 ) );
	p->y = (float)atof( Cmd_Argv( 14 ) );
	p->z = (float)atof( Cmd_Argv( 15 ) );
	p->yaw = (float)atof( Cmd_Argv( 16 ) );
	p->speed = (float)atof( Cmd_Argv( 17 ) );
	p->igtMs = Cmd_Argc() > 18 ? atoi( Cmd_Argv( 18 ) ) : p->timeMs;
	p->stageTimeMs = Cmd_Argc() > 19 ? atoi( Cmd_Argv( 19 ) ) : 0;
	p->stageProgress = Cmd_Argc() > 20 ? atoi( Cmd_Argv( 20 ) ) : LS_RaceStageProgressForIndex( LS_FindMapIndex( p->map ) );
	if ( Cmd_Argc() > 21 ) LS_RaceSanitizeToken( Cmd_Argv( 21 ), p->stageName, sizeof( p->stageName ) );
	else Q_strncpyz( p->stageName, p->map[0] ? p->map : "-", sizeof( p->stageName ) );
	LS_RaceParsePlayerTelemetryArgs( p );
	p->lastHeardMs = now;
	LS_RaceStartPlayerSmooth( p, now );
	if ( ls_race.role == LS_RACE_ROLE_CLIENT ) ls_race.lastHostPacketMs = p->lastHeardMs;
}

static void LS_RaceHandleState( netadr_t from ) {
	lsRacePlayer_t *p;
	int session, slot, stageProgress, now;
	qboolean loaded, started, finished;
	qboolean importantChange;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	now = Sys_Milliseconds();
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	slot = atoi( Cmd_Argv( 3 ) );
	p = LS_RaceFindPlayerBySlot( slot );
	if ( !LS_RacePlayerAdrMatches( p, from ) ) return;
	if ( p->lastSampleMs ) LS_RaceUpdatePlayerRender( p, now );
	loaded = atoi( Cmd_Argv( 9 ) ) ? qtrue : qfalse;
	started = atoi( Cmd_Argv( 10 ) ) ? qtrue : qfalse;
	finished = atoi( Cmd_Argv( 11 ) ) ? qtrue : qfalse;
	stageProgress = Cmd_Argc() > 20 ? atoi( Cmd_Argv( 20 ) ) : p->stageProgress;
	importantChange = ( p->loaded != loaded || p->started != started || p->finished != finished || p->stageProgress != stageProgress ) ? qtrue : qfalse;
	LS_RaceSanitizeToken( Cmd_Argv( 4 ), p->nick, sizeof( p->nick ) );
	p->color[0] = atoi( Cmd_Argv( 5 ) );
	p->color[1] = atoi( Cmd_Argv( 6 ) );
	p->color[2] = atoi( Cmd_Argv( 7 ) );
	Q_strncpyz( p->map, Cmd_Argv( 8 ), sizeof( p->map ) );
	if ( !Q_stricmp( p->map, "-" ) ) p->map[0] = '\0';
	p->loaded = loaded;
	p->started = started;
	p->finished = finished;
	p->timeMs = atoi( Cmd_Argv( 12 ) );
	p->x = (float)atof( Cmd_Argv( 13 ) );
	p->y = (float)atof( Cmd_Argv( 14 ) );
	p->z = (float)atof( Cmd_Argv( 15 ) );
	p->yaw = (float)atof( Cmd_Argv( 16 ) );
	p->speed = (float)atof( Cmd_Argv( 17 ) );
	p->igtMs = Cmd_Argc() > 18 ? atoi( Cmd_Argv( 18 ) ) : p->timeMs;
	p->stageTimeMs = Cmd_Argc() > 19 ? atoi( Cmd_Argv( 19 ) ) : 0;
	p->stageProgress = stageProgress;
	if ( Cmd_Argc() > 21 ) LS_RaceSanitizeToken( Cmd_Argv( 21 ), p->stageName, sizeof( p->stageName ) );
	else Q_strncpyz( p->stageName, p->map[0] ? p->map : "-", sizeof( p->stageName ) );
	{
		int oldObjectives = p->objectivesFound;
		int oldZones = p->zoneProgress;
		LS_RaceParsePlayerTelemetryArgs( p );
		if ( oldObjectives != p->objectivesFound || oldZones != p->zoneProgress ) importantChange = qtrue;
	}
	p->lastHeardMs = now;
	LS_RaceStartPlayerSmooth( p, now );
	if ( importantChange ) {
		LS_RaceBroadcastRoster();
		ls_race.lastRosterMs = p->lastHeardMs;
	} else {
		LS_RaceBroadcastPlayer( p );
	}
}

static void LS_RaceHandleLoad( netadr_t from ) {
	int session, mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, now;
	char newTarget[LS_MAX_MAPNAME];
	qboolean duplicateLoad;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT || !LS_RaceHostAdrMatches( from ) ) return;
	if ( Cmd_Argc() < 11 ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	now = Sys_Milliseconds();
	if ( ls_race.state == LS_RACE_STATE_COUNTDOWN || ls_race.state == LS_RACE_STATE_RACING || ls_race.state == LS_RACE_STATE_FINISHED ) {
		ls_race.lastHostPacketMs = now;
		return;
	}
	mode = atoi( Cmd_Argv( 3 ) );
	mission = atoi( Cmd_Argv( 4 ) );
	percent100 = atoi( Cmd_Argv( 5 ) );
	difficulty = Cmd_Argc() > 6 ? atoi( Cmd_Argv( 6 ) ) : LS_DetectDifficulty();
	hl1Movement = Cmd_Argc() > 7 ? atoi( Cmd_Argv( 7 ) ) : ( LS_HL1ModeActive() ? 1 : 0 );
	autoJump = Cmd_Argc() > 8 ? atoi( Cmd_Argv( 8 ) ) : Cvar_VariableIntegerValue( "bh_autojump" );
	antiCheat = Cmd_Argc() > 11 ? atoi( Cmd_Argv( 11 ) ) : 1;
	Q_strncpyz( newTarget, Cmd_Argv( 10 ), sizeof( newTarget ) );
	if ( !newTarget[0] || !Q_stricmp( newTarget, "-" ) ) return;
	duplicateLoad = ( ls_race.state == LS_RACE_STATE_LOADING && ls_race.loadIssued && !Q_stricmp( ls_race.targetMap, newTarget ) ) ? qtrue : qfalse;
	LS_RaceApplySettings( mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, Cmd_Argv( 9 ) );
	Q_strncpyz( ls_race.targetMap, newTarget, sizeof( ls_race.targetMap ) );
	ls_race.state = LS_RACE_STATE_LOADING;
	ls_race.lastHostPacketMs = now;
	if ( !duplicateLoad ) {
		ls_race.startIssued = qfalse;
		ls_race.loadIssued = qfalse;
		ls_race.loadIssuedMs = 0;
		ls_race.loadRetryCount = 0;
		ls_race.loadIssuedMap[0] = '\0';
		ls_race.raceStartMs = 0;
		LS_RaceStartMapLoad( qfalse );
	}
	LS_RaceSendStateToHostScan();
	LS_RaceSetStatus( va( "Loading %s", ls_race.targetMap ) );
}

static void LS_RaceHandleConfig( netadr_t from ) {
	int session, mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT || !LS_RaceHostAdrMatches( from ) ) return;
	if ( Cmd_Argc() < 10 ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	if ( ls_race.state != LS_RACE_STATE_LOBBY && ls_race.state != LS_RACE_STATE_CONNECTING ) return;
	mode = atoi( Cmd_Argv( 3 ) );
	mission = atoi( Cmd_Argv( 4 ) );
	percent100 = atoi( Cmd_Argv( 5 ) );
	difficulty = Cmd_Argc() > 6 ? atoi( Cmd_Argv( 6 ) ) : LS_DetectDifficulty();
	hl1Movement = Cmd_Argc() > 7 ? atoi( Cmd_Argv( 7 ) ) : ( LS_HL1ModeActive() ? 1 : 0 );
	autoJump = Cmd_Argc() > 8 ? atoi( Cmd_Argv( 8 ) ) : Cvar_VariableIntegerValue( "bh_autojump" );
	antiCheat = Cmd_Argc() > 10 ? atoi( Cmd_Argv( 10 ) ) : 1;
	LS_RaceApplySettings( mode, mission, percent100, difficulty, hl1Movement, autoJump, antiCheat, Cmd_Argv( 9 ) );
	ls_race.lastHostPacketMs = Sys_Milliseconds();
	LS_RaceSetStatus( "Host settings synced" );
}

static void LS_RaceHandleCountdown( netadr_t from ) {
	int session, now, fullMs, remainingMs, localRemaining;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT || !LS_RaceHostAdrMatches( from ) ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	now = Sys_Milliseconds();
	ls_race.lastHostPacketMs = now;
	if ( ls_race.startIssued || ls_race.state == LS_RACE_STATE_RACING || ls_race.state == LS_RACE_STATE_FINISHED ) return;
	fullMs = atoi( Cmd_Argv( 3 ) );
	if ( fullMs < 1000 ) fullMs = 5000;
	if ( fullMs > 30000 ) fullMs = 30000;
	remainingMs = Cmd_Argc() > 4 ? atoi( Cmd_Argv( 4 ) ) : fullMs;
	if ( remainingMs < 0 ) remainingMs = 0;
	if ( remainingMs > fullMs ) remainingMs = fullMs;
	if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		localRemaining = LS_RaceCountdownRemaining( now );
		if ( remainingMs > localRemaining + 250 ) return;
	}
	ls_race.countdownMs = fullMs;
	ls_race.countdownStartMs = now - ( fullMs - remainingMs );
	ls_race.state = LS_RACE_STATE_COUNTDOWN;
	LS_RaceSendStateToHostScan();
	LS_RaceSetStatus( "Countdown" );
	if ( remainingMs <= 0 ) LS_RaceBeginRun();
}

static void LS_RaceHandleStart( netadr_t from ) {
	int session;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT || !LS_RaceHostAdrMatches( from ) ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	ls_race.lastHostPacketMs = Sys_Milliseconds();
	if ( ls_race.state == LS_RACE_STATE_LOADING && !LS_RaceLocalLoaded() ) {
		ls_race.pendingStart = qtrue;
		return;
	}
	ls_race.pendingStart = qfalse;
	LS_RaceBeginRun();
}

static void LS_RaceHandleLeave( netadr_t from ) {
	int session, slot;
	lsRacePlayer_t *p;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	slot = atoi( Cmd_Argv( 3 ) );
	p = LS_RaceFindPlayerBySlot( slot );
	if ( !LS_RacePlayerAdrMatches( p, from ) ) return;
	p->left = qtrue;
	p->inMenu = qtrue;
	p->timedOut = qfalse;
	p->loaded = qtrue;
	p->started = qfalse;
	p->paused = qfalse;
	p->lastHeardMs = Sys_Milliseconds();
	LS_RaceBroadcastRoster();
	LS_RaceSetStatus( va( "Lobby: %d player(s)", LS_RacePlayerCount() ) );
}

static void LS_RaceHandleChat( netadr_t from ) {
	int session, slot;
	char nick[32];
	char text[LS_RACE_CHAT_TEXT];
	lsRacePlayer_t *p;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	slot = atoi( Cmd_Argv( 3 ) );
	LS_RaceSanitizeToken( Cmd_Argv( 4 ), nick, sizeof( nick ) );
	LS_RaceSanitizeChatText( Cmd_ArgsFrom( 5 ), text, sizeof( text ) );
	if ( !text[0] ) return;
	if ( ls_race.role == LS_RACE_ROLE_HOST ) {
		p = LS_RaceFindPlayerBySlot( slot );
		if ( !LS_RacePlayerAdrMatches( p, from ) ) return;
		p->lastHeardMs = Sys_Milliseconds();
		p->left = qfalse;
		p->timedOut = qfalse;
		Q_strncpyz( nick, p->nick, sizeof( nick ) );
		if ( LS_RaceDropDuplicateChatLine( slot, nick, text ) ) return;
		LS_RaceAddChatLine( slot, nick, text );
		LS_RaceBroadcastChat( slot, nick, text, slot );
	} else if ( ls_race.role == LS_RACE_ROLE_CLIENT ) {
		if ( !LS_RaceHostAdrMatches( from ) ) return;
		ls_race.lastHostPacketMs = Sys_Milliseconds();
		if ( LS_RaceDropDuplicateChatLine( slot, nick, text ) ) return;
		LS_RaceAddChatLine( slot, nick, text );
	}
}

static void LS_RaceHandleEvent( netadr_t from ) {
	int session, slot;
	char kind[32];
	char detail[LS_RACE_CHAT_TEXT];
	lsRacePlayer_t *p;
	if ( ls_race.role != LS_RACE_ROLE_HOST ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	slot = atoi( Cmd_Argv( 3 ) );
	p = LS_RaceFindPlayerBySlot( slot );
	if ( !LS_RacePlayerAdrMatches( p, from ) ) return;
	LS_RaceSanitizeToken( Cmd_Argv( 4 ), kind, sizeof( kind ) );
	LS_RaceSanitizeChatText( Cmd_ArgsFrom( 5 ), detail, sizeof( detail ) );
	p->lastHeardMs = Sys_Milliseconds();
	if ( !Q_stricmp( kind, "cheat" ) ) {
		Com_Printf( "^1Race anti-cheat: %s tried/triggered %s\n", p->nick, detail[0] ? detail : "protected command" );
	} else {
		Com_Printf( "^3Race event: %s %s %s\n", p->nick, kind, detail );
	}
}

static void LS_RaceHandleStop( netadr_t from ) {
	int session, i;
	if ( ls_race.role != LS_RACE_ROLE_CLIENT ) return;
	if ( !LS_RaceHostAdrMatches( from ) ) return;
	if ( Cmd_Argc() < 3 ) return;
	session = atoi( Cmd_Argv( 2 ) );
	if ( session != ls_race.session ) return;
	ls_race.state = LS_RACE_STATE_LOBBY;
	ls_race.countdownStartMs = 0;
	ls_race.raceStartMs = 0;
	ls_race.playerStartIssued = qfalse;
	ls_race.playerStartProcessed = qfalse;
	ls_race.pendingStart = qfalse;
	ls_race.loadIssued = qfalse;
	ls_race.loadIssuedMs = 0;
	ls_race.loadRetryCount = 0;
	ls_race.loadIssuedMap[0] = '\0';
	ls_race.lastHostPacketMs = Sys_Milliseconds();
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; ++i ) {
		if ( !ls_race.players[i].used ) continue;
		ls_race.players[i].loaded = qfalse;
		ls_race.players[i].started = qfalse;
		ls_race.players[i].finished = qfalse;
		ls_race.players[i].timeMs = 0;
		ls_race.players[i].stageTimeMs = 0;
		ls_race.players[i].paused = qfalse;
		ls_race.players[i].inMenu = qfalse;
	}
	Cvar_Set( "cl_paused", "0" );
	LS_RaceSetStatus( "Race stopped - back to lobby" );
	Com_Printf( "^3Race: stopped by host, back to lobby\n" );
	LS_RaceUpdateRuntimeCvars();
}

static void LS_RaceSay_f( void ) {
	char text[LS_RACE_CHAT_TEXT];
	lsRacePlayer_t *local;
	if ( ls_race.role == LS_RACE_ROLE_NONE || !ls_race.session ) {
		LS_RaceSetStatus( "Join or host a race before chatting" );
		return;
	}
	LS_RaceSanitizeChatText( Cmd_Args(), text, sizeof( text ) );
	if ( !text[0] ) return;
	local = LS_RaceFindPlayerBySlot( ls_race.localSlot );
	if ( !local ) return;
	if ( LS_RaceDropDuplicateChatLine( local->slot, local->nick, text ) ) return;
	LS_RaceAddChatLine( local->slot, local->nick, text );
	if ( ls_race.role == LS_RACE_ROLE_HOST ) {
		LS_RaceBroadcastChat( local->slot, local->nick, text, -1 );
	} else {
		LS_RaceSendChatToHostScan( text );
	}
}

static void LS_RaceChat_f( void ) {
	if ( ls_race.role == LS_RACE_ROLE_NONE || !ls_race.session ) {
		LS_RaceSetStatus( "Join or host a race before chatting" );
		return;
	}
	CL_SpeedrunImGui_OpenRaceChat();
}

void LS_RaceConnectionlessPacket( netadr_t from ) {
	const char *sub = Cmd_Argv( 1 );
	if ( !sub || !sub[0] ) return;
	if ( !Q_stricmp( sub, "discover" ) ) LS_RaceHandleDiscover( from );
	else if ( !Q_stricmp( sub, "found" ) ) LS_RaceHandleFound( from );
	else if ( !Q_stricmp( sub, "hello" ) ) LS_RaceHandleHello( from );
	else if ( !Q_stricmp( sub, "queued" ) ) LS_RaceHandleQueued( from );
	else if ( !Q_stricmp( sub, "welcome" ) ) LS_RaceHandleWelcome( from );
	else if ( !Q_stricmp( sub, "player" ) ) LS_RaceHandlePlayer( from );
	else if ( !Q_stricmp( sub, "state" ) ) LS_RaceHandleState( from );
	else if ( !Q_stricmp( sub, "config" ) ) LS_RaceHandleConfig( from );
	else if ( !Q_stricmp( sub, "load" ) ) LS_RaceHandleLoad( from );
	else if ( !Q_stricmp( sub, "countdown" ) ) LS_RaceHandleCountdown( from );
	else if ( !Q_stricmp( sub, "start" ) ) LS_RaceHandleStart( from );
	else if ( !Q_stricmp( sub, "stop" ) ) LS_RaceHandleStop( from );
	else if ( !Q_stricmp( sub, "leave" ) ) LS_RaceHandleLeave( from );
	else if ( !Q_stricmp( sub, "chat" ) ) LS_RaceHandleChat( from );
	else if ( !Q_stricmp( sub, "event" ) ) LS_RaceHandleEvent( from );
	else if ( !Q_stricmp( sub, "reject" ) ) {
		const char *reason = Cmd_Argv( 2 );
		char cleanReason[128];
		LS_RaceFormatRejectReason( reason, cleanReason, sizeof( cleanReason ) );
		LS_RaceSetStatus( cleanReason );
		Com_Printf( "^1Race: join rejected: %s\n", cleanReason );
		/* The host has explicitly refused us (kicked, lobby full, race
		   already started, race stopped, ...). Tear down the local race
		   session so the client returns to idle and can join a fresh
		   lobby - without this, the kicked client keeps spamming state
		   packets with a now-stale session and can never reconnect. */
		if ( ls_race.role == LS_RACE_ROLE_CLIENT && LS_RaceHostAdrMatches( from ) ) {
			LS_RaceResetSession();
			LS_RaceRestoreSavedSettings();
			Cvar_Set( "cl_paused", "0" );
		}
	}
}

qboolean LS_RaceShouldBlockInput( void ) {
	if ( ls_race.role == LS_RACE_ROLE_NONE ) return qfalse;
	if ( LS_RaceUserInterfaceActive() ) return qfalse;
	if ( ls_race.startIssued || ls_race.state == LS_RACE_STATE_RACING ) return qfalse;
	if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) return qtrue;
	if ( ls_race.state == LS_RACE_STATE_LOADING && LS_RaceLocalLoaded() ) return qtrue;
	return qfalse;
}

void LS_RaceFrame( void ) {
	int now, i, packetMs, rosterMs;
	if ( !ls_race.initialized ) return;
	LS_RaceUpdateFoundCvars();
	if ( ls_race.role == LS_RACE_ROLE_NONE ) return;
	now = Sys_Milliseconds();
	packetMs = LS_RacePacketIntervalMs();
	rosterMs = LS_RaceRosterIntervalMs();
	if ( ls_race.role == LS_RACE_ROLE_HOST && ls_race.state == LS_RACE_STATE_LOBBY ) {
		if ( LS_RaceSyncSettingsFromCvars() ) {
			LS_RaceBroadcastConfig();
			LS_RaceBroadcastRoster();
		}
	} else {
		LS_RaceEnforceLockedSettings();
	}
	if ( ls_race.state >= LS_RACE_STATE_LOADING ) {
		LS_RaceDisableProtectedOptions();
	}
	LS_RaceUpdateLocalPlayer();
	if ( ls_race.state == LS_RACE_STATE_LOADING && ls_race.loadIssued && ls_race.loadRetryCount < 2 &&
		 now - ls_race.loadIssuedMs > 3500 && LS_RaceStillOnWrongActiveMap() ) {
		ls_race.loadRetryCount++;
		LS_RaceSetStatus( va( "Retry loading %s", ls_race.targetMap ) );
		LS_RaceStartMapLoad( qtrue );
	}
	if ( ( ls_race.state == LS_RACE_STATE_LOADING && LS_RaceLocalLoaded() ) || ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		Cvar_Set( "cl_paused", "1" );
	} else if ( ls_race.state == LS_RACE_STATE_RACING && cl_paused && cl_paused->integer && !LS_RaceUserInterfaceActive() ) {
		Cvar_Set( "cl_paused", "0" );
	}
	if ( ls_race.role == LS_RACE_ROLE_CLIENT && ls_race.pendingStart && ls_race.state == LS_RACE_STATE_LOADING && LS_RaceLocalLoaded() ) {
		ls_race.pendingStart = qfalse;
		LS_RaceBeginRun();
	}
	if ( ls_race.role == LS_RACE_ROLE_CLIENT ) {
		if ( now - ls_race.lastSendMs >= packetMs ) {
			if ( ls_race.state == LS_RACE_STATE_CONNECTING ) LS_RaceSendHello();
			else LS_RaceSendStateToHost();
			ls_race.lastSendMs = now;
		}
		if ( ( ls_race.state == LS_RACE_STATE_LOADING || ls_race.state == LS_RACE_STATE_COUNTDOWN || ls_race.state == LS_RACE_STATE_RACING ) && now - ls_race.lastHelloMs >= LS_RACE_RESYNC_MS ) {
			LS_RaceSendHello();
			LS_RaceSendStateToHostScan();
			ls_race.lastHelloMs = now;
		}
		if ( ls_race.lastHostPacketMs && now - ls_race.lastHostPacketMs > ( ( ls_race.state == LS_RACE_STATE_LOADING || ls_race.state == LS_RACE_STATE_COUNTDOWN ) ? LS_RACE_LOAD_TIMEOUT_MS : LS_RACE_TIMEOUT_MS * 2 ) ) {
			LS_RaceSetStatus( "Host timeout" );
		}
	} else if ( ls_race.role == LS_RACE_ROLE_HOST ) {
		lsRacePlayer_t *local = LS_RaceFindPlayerBySlot( ls_race.localSlot );
		for ( i = 1; i < LS_RACE_MAX_PLAYERS; i++ ) {
			int timeoutMs = ( ls_race.state == LS_RACE_STATE_LOADING || ls_race.state == LS_RACE_STATE_COUNTDOWN ) ? LS_RACE_LOAD_TIMEOUT_MS : LS_RACE_TIMEOUT_MS;
			if ( ls_race.players[i].used && now - ls_race.players[i].lastHeardMs > timeoutMs ) {
				if ( ls_race.state == LS_RACE_STATE_LOADING || ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
					continue;
				}
				if ( ls_race.players[i].timedOut ) continue;
				Com_Printf( "^3Race: player %s timed out\n", ls_race.players[i].nick );
				ls_race.players[i].timedOut = qtrue;
				ls_race.players[i].inMenu = qtrue;
				ls_race.players[i].loaded = qtrue;
				ls_race.players[i].started = qfalse;
				ls_race.players[i].paused = qfalse;
				LS_RaceBroadcastRoster();
			}
		}
		if ( local && now - ls_race.lastSendMs >= packetMs &&
			 ( ls_race.state == LS_RACE_STATE_LOADING || ls_race.state == LS_RACE_STATE_COUNTDOWN || ls_race.state == LS_RACE_STATE_RACING || ls_race.state == LS_RACE_STATE_FINISHED ) ) {
			LS_RaceBroadcastPlayer( local );
			ls_race.lastSendMs = now;
		}
		if ( now - ls_race.lastRosterMs >= rosterMs ) {
			LS_RaceBroadcastRoster();
			ls_race.lastRosterMs = now;
		}
		if ( ls_race.state == LS_RACE_STATE_LOADING && now - ls_race.lastLoadBroadcastMs >= LS_RACE_LOAD_RESEND_MS ) {
			LS_RaceBroadcastLoad();
			ls_race.lastLoadBroadcastMs = now;
		}
		if ( ls_race.state == LS_RACE_STATE_LOADING && LS_RaceAllLoaded() ) {
			ls_race.state = LS_RACE_STATE_COUNTDOWN;
			ls_race.countdownStartMs = now;
			LS_RaceBroadcastCountdown();
			ls_race.lastCountdownBroadcastMs = now;
			LS_RaceSetStatus( "Countdown" );
		}
		if ( ls_race.state == LS_RACE_STATE_COUNTDOWN && now - ls_race.lastCountdownBroadcastMs >= LS_RACE_COUNTDOWN_RESEND_MS ) {
			LS_RaceBroadcastCountdown();
			ls_race.lastCountdownBroadcastMs = now;
		}
		if ( ls_race.state == LS_RACE_STATE_RACING && LS_RaceAnyRemoteNotStarted() && ls_race.raceStartMs && now - ls_race.raceStartMs < LS_RACE_START_RESEND_WINDOW_MS && now - ls_race.lastStartBroadcastMs >= LS_RACE_START_RESEND_MS ) {
			LS_RaceBroadcastStart();
			ls_race.lastStartBroadcastMs = now;
		}
		if ( ls_race.state == LS_RACE_STATE_RACING && LS_RaceAllFinished() ) {
			ls_race.state = LS_RACE_STATE_FINISHED;
			LS_RaceSetStatus( "Race finished" );
		}
	}
	if ( ls_race.state == LS_RACE_STATE_COUNTDOWN && now - ls_race.countdownStartMs >= ls_race.countdownMs ) {
		LS_RaceBeginRun();
		if ( ls_race.role == LS_RACE_ROLE_HOST ) {
			LS_RaceBroadcastStart();
			ls_race.lastStartBroadcastMs = now;
		}
	}
	LS_RacePublishGhostCvars();
	LS_RaceUpdateRuntimeCvars();
}

void LS_RaceDrawOverlay( void ) {
	float panelBg[4] = { 0.02f, 0.03f, 0.025f, 0.78f };
	float panelBgSoft[4] = { 0.02f, 0.03f, 0.025f, 0.52f };
	float border[4] = { 0.35f, 0.80f, 0.22f, 0.62f };
	float text[4] = { 0.85f, 0.91f, 0.82f, 1.0f };
	float muted[4] = { 0.48f, 0.56f, 0.46f, 1.0f };
	float gold[4] = { 1.0f, 0.78f, 0.25f, 1.0f };
	float green[4] = { 0.48f, 0.92f, 0.30f, 1.0f };
	float white[4] = { 0.92f, 0.98f, 0.88f, 1.0f };
	int i, y, rowCount, loadedCount, totalCount, panelH;
	char category[64];
	char headerRight[64];
	char centerText[64];
	char timerText[32];
	if ( !ls_race.initialized || ls_race.role == LS_RACE_ROLE_NONE ) return;
	if ( ls_race.overlayCvar && !ls_race.overlayCvar->integer ) return;
	LS_RaceLoadedCounts( &loadedCount, &totalCount );
	LS_RaceBuildCategoryLabel( category, sizeof( category ) );
	rowCount = LS_RacePlayerCount();
	if ( rowCount < 1 ) rowCount = 1;
	panelH = 43 + rowCount * 13;
	headerRight[0] = '\0';
	timerText[0] = '\0';

	if ( ls_race.state == LS_RACE_STATE_LOADING ) {
		Com_sprintf( centerText, sizeof( centerText ), "READY %d/%d", loadedCount, totalCount );
		SCR_FillRect( 218, 168, 204, 48, panelBgSoft );
		SCR_FillRect( 218, 168, 204, 1, border );
		LS_RaceDrawCenteredString( 176, 6, "WAITING FOR PLAYERS", muted );
		LS_RaceDrawCenteredString( 190, 10, centerText, loadedCount == totalCount && totalCount > 0 ? green : gold );
	} else if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		int remain = LS_RaceCountdownRemaining( Sys_Milliseconds() );
		LS_RaceFormatCountdown( remain, centerText, sizeof( centerText ) );
		SCR_FillRect( 230, 148, 180, 72, panelBgSoft );
		SCR_FillRect( 230, 148, 180, 1, border );
		LS_RaceDrawCenteredString( 158, 6, "START IN", muted );
		LS_RaceDrawCenteredString( 174, remain > 0 ? 22 : 20, centerText, remain > 0 ? gold : green );
	} else if ( ls_race.state == LS_RACE_STATE_RACING || ls_race.state == LS_RACE_STATE_FINISHED ) {
		int timerMs = LS_RaceCurrentTimerMs();
		if ( timerMs > 0 ) LS_FormatTime( timerMs, timerText, sizeof( timerText ) );
		else Q_strncpyz( timerText, "0.00", sizeof( timerText ) );
		SCR_FillRect( 246, 28, 148, 40, panelBgSoft );
		SCR_FillRect( 246, 28, 148, 1, border );
		LS_RaceDrawCenteredString( 34, 5, ls_race.state == LS_RACE_STATE_FINISHED ? "FINISHED" : "RACE TIMER", muted );
		LS_RaceDrawCenteredString( 47, 10, timerText, ls_race.state == LS_RACE_STATE_FINISHED ? gold : white );
	}

	if ( ls_race.state == LS_RACE_STATE_LOADING ) {
		Com_sprintf( headerRight, sizeof( headerRight ), "Ready %d/%d", loadedCount, totalCount );
	} else if ( ls_race.state == LS_RACE_STATE_COUNTDOWN ) {
		LS_RaceFormatCountdown( LS_RaceCountdownRemaining( Sys_Milliseconds() ), headerRight, sizeof( headerRight ) );
	} else if ( ls_race.state == LS_RACE_STATE_RACING || ls_race.state == LS_RACE_STATE_FINISHED ) {
		int timerMs = LS_RaceCurrentTimerMs();
		if ( timerMs > 0 ) LS_FormatTime( timerMs, headerRight, sizeof( headerRight ) );
		else Q_strncpyz( headerRight, "0.00", sizeof( headerRight ) );
	} else {
		Com_sprintf( headerRight, sizeof( headerRight ), "%d player%s", LS_RacePlayerCount(), LS_RacePlayerCount() == 1 ? "" : "s" );
	}
	SCR_FillRect( 8, 72, 260, panelH, panelBg );
	SCR_FillRect( 8, 72, 260, 1, border );
	SCR_FillRect( 8, 96, 260, 1, border );
	SCR_DrawStringExt( 14, 79, 6, "Race", green, qtrue );
	SCR_DrawStringExt( 56, 79, 5, LS_RaceStateName( ls_race.state ), muted, qtrue );
	SCR_DrawStringExt( 14, 89, 4, category, muted, qtrue );
	SCR_DrawStringExt( 178, 79, 5, headerRight, gold, qtrue );
	SCR_DrawStringExt( 14, 101, 4, "#  Player        St       Time", muted, qtrue );
	y = 115;
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		lsRacePlayer_t *p = &ls_race.players[i];
		char line[128], timeText[32];
		const char *stateText;
		if ( !p->used ) continue;
		if ( p->finished ) stateText = "FIN";
		else if ( p->started ) stateText = "RUN";
		else if ( p->loaded ) stateText = "RDY";
		else stateText = "LOD";
		if ( p->timeMs > 0 ) LS_FormatTime( p->timeMs, timeText, sizeof( timeText ) );
		else Q_strncpyz( timeText, "--", sizeof( timeText ) );
		Com_sprintf( line, sizeof( line ), "%d  %-12.12s %-5s %s", p->slot, p->nick, stateText, timeText );
		SCR_DrawStringExt( 14, y, 5, line, p->local ? gold : text, qtrue );
		y += 13;
	}
	if ( LS_RacePlayerCount() == 0 ) {
		SCR_DrawStringExt( 14, y, 5, "No players", muted, qtrue );
	}
}

static void LS_RaceInit( void ) {
	int i;
	char name[32];
	if ( ls_race.initialized ) return;
	memset( &ls_race, 0, sizeof( ls_race ) );
	ls_race.localSlot = -1;
	ls_race.antiCheat = 1;
	ls_race.nickCvar = Cvar_Get( "name", "Player", CVAR_ARCHIVE );
	ls_race.ipCvar = Cvar_Get( "ls_race_ip", "127.0.0.1", CVAR_ARCHIVE );
	ls_race.hostIpCvar = Cvar_Get( "ls_race_host_ip", "localhost", CVAR_ARCHIVE );
	ls_race.portCvar = Cvar_Get( "ls_race_port", "27960", CVAR_ARCHIVE );
	ls_race.hideIpCvar = Cvar_Get( "ls_race_hide_ip", "1", CVAR_ARCHIVE );
	ls_race.colorCvar = Cvar_Get( "ls_race_color", "80 180 255 1.00", CVAR_ARCHIVE );
	ls_race.overlayCvar = Cvar_Get( "ls_race_overlay", "1", CVAR_ARCHIVE );
	ls_race.countdownCvar = Cvar_Get( "ls_race_countdown", "5", CVAR_ARCHIVE );
	ls_race.packetMsCvar = Cvar_Get( "ls_race_packet_ms", "33", CVAR_ARCHIVE );
	ls_race.rosterMsCvar = Cvar_Get( "ls_race_roster_ms", "500", CVAR_ARCHIVE );
	ls_race.antiCheatCvar = Cvar_Get( "ls_race_anticheat", "1", CVAR_ARCHIVE );
	ls_race.antiCheat = ls_race.antiCheatCvar && ls_race.antiCheatCvar->integer ? 1 : 0;
	ls_race.ghostsCvar = Cvar_Get( "ls_race_ghosts", "1", CVAR_ARCHIVE );
	ls_race.ghostAlphaCvar = Cvar_Get( "ls_race_ghost_alpha", "120", CVAR_ARCHIVE );
	ls_race.zoneScoringCvar = Cvar_Get( "ls_race_zone_scoring", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_x", "8", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_y", "72", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_w", "110", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_scale", "0.5", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_opacity", "0.90", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_progress", "0", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_overlay_compact", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_countdown_center", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_x", "12", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_y", "300", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_w", "285", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_scale", "1.0", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_bg", "8 10 8 0.50", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_text", "226 238 218 0.95", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_input_x", "12", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_input_y", "444", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_input_w", "285", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_input_scale", "1.0", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_input_bg", "8 10 8 0.82", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_chat_input_text", "224 240 214 1.00", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_countdown_scale", "1.0", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_nametag", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_nametag_stats", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_nametag_icons", "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_nametag_scale", "1.0", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_nametag_opacity", "1.0", CVAR_ARCHIVE );
	Cvar_Get( "ls_race_ghost_render", "0", CVAR_ARCHIVE );
	ls_race.passwordCvar = Cvar_Get( "ls_race_password", "", CVAR_ARCHIVE );
	ls_race.activeCvar = Cvar_Get( "ls_race_active", "0", 0 );
	ls_race.roleCvar = Cvar_Get( "ls_race_role", "Idle", 0 );
	ls_race.stateCvar = Cvar_Get( "ls_race_state", "Idle", 0 );
	ls_race.statusCvar = Cvar_Get( "ls_race_status", "", 0 );
	ls_race.playersCvar = Cvar_Get( "ls_race_players", "0", 0 );
	ls_race.timerCvar = Cvar_Get( "ls_race_timer", "0.00", 0 );
	ls_race.stageCvar = Cvar_Get( "ls_race_stage", "-", 0 );
	ls_race.stageIgtCvar = Cvar_Get( "ls_race_stage_igt", "--", 0 );
	ls_race.readyCvar = Cvar_Get( "ls_race_ready", "0/0", 0 );
	ls_race.flagsCvar = Cvar_Get( "ls_race_flags", "anti-cheat on | sv_cheats 0 | god off | noclip off", 0 );
	ls_race.countdownTextCvar = Cvar_Get( "ls_race_countdown_text", "", 0 );
	ls_race.foundCountCvar = Cvar_Get( "ls_race_found_count", "0", 0 );
	ls_race.foundStatusCvar = Cvar_Get( "ls_race_found_status", "No Race lobbies found yet", 0 );
	Cvar_Get( "ls_godmode", "0", 0 );
	for ( i = 0; i < LS_RACE_FOUND_MAX; i++ ) {
		Com_sprintf( name, sizeof( name ), "ls_race_found_%d", i );
		ls_race.foundCvars[i] = Cvar_Get( name, "", 0 );
	}
	for ( i = 0; i < LS_RACE_MAX_PLAYERS; i++ ) {
		Com_sprintf( name, sizeof( name ), "ls_race_ghost%d", i );
		ls_race.ghostCvars[i] = Cvar_Get( name, "", 0 );
	}
	ls_race.initialized = qtrue;
	LS_RaceResetSession();
	Cmd_AddCommand( "ls_race_host", LS_RaceHost_f );
	Cmd_AddCommand( "ls_race_join", LS_RaceJoin_f );
	Cmd_AddCommand( "ls_race_connect", LS_RaceJoin_f );
	Cmd_AddCommand( "ls_race_refresh", LS_RaceRefresh_f );
	Cmd_AddCommand( "ls_race_join_found", LS_RaceJoinFound_f );
	Cmd_AddCommand( "ls_race_leave", LS_RaceLeave_f );
	Cmd_AddCommand( "ls_race_start", LS_RaceStart_f );
	Cmd_AddCommand( "ls_race_open_chat", LS_RaceChat_f );
	Cmd_AddCommand( "ls_race_say", LS_RaceSay_f );
}

static void LS_RaceShutdown( void ) {
	if ( !ls_race.initialized ) return;
	Cmd_RemoveCommand( "ls_race_host" );
	Cmd_RemoveCommand( "ls_race_join" );
	Cmd_RemoveCommand( "ls_race_connect" );
	Cmd_RemoveCommand( "ls_race_refresh" );
	Cmd_RemoveCommand( "ls_race_join_found" );
	Cmd_RemoveCommand( "ls_race_leave" );
	Cmd_RemoveCommand( "ls_race_start" );
	Cmd_RemoveCommand( "ls_race_open_chat" );
	Cmd_RemoveCommand( "ls_race_say" );
	LS_RaceResetSession();
	LS_RaceRestoreSavedSettings();
	ls_race.initialized = qfalse;
}

void SCR_LiveSplitInit( void ) {
	cg_livesplit   = Cvar_Get( "cg_livesplit", "0",   CVAR_ARCHIVE );
	ls_modeCvar    = Cvar_Get( "ls_mode",      "0",   CVAR_ARCHIVE );
	ls_missionCvar = Cvar_Get( "ls_mission",   "1",   CVAR_ARCHIVE );
	ls_mapCvar     = Cvar_Get( "ls_map",       "",    CVAR_ARCHIVE );
	ls_xCvar       = Cvar_Get( "ls_x",         "6.666843",   CVAR_ARCHIVE );
	ls_yCvar       = Cvar_Get( "ls_y",         "92.444427",  CVAR_ARCHIVE );
	ls_wCvar       = Cvar_Get( "ls_w",         "215", CVAR_ARCHIVE );
	ls_scaleCvar   = Cvar_Get( "ls_scale",     "0.550000", CVAR_ARCHIVE );
	ls_opacityCvar = Cvar_Get( "ls_opacity",   "0.900000", CVAR_ARCHIVE );
	ls_opacityUiCvar = Cvar_Get( "ls_opacity_ui", "0.900000", CVAR_ARCHIVE );
	ls_bgalphaCvar = Cvar_Get( "ls_bgalpha", "0.78", CVAR_ARCHIVE );
	ls_alignCvar   = Cvar_Get( "ls_align",   "0",    CVAR_ARCHIVE );
	ls_showheaderCvar = Cvar_Get( "ls_showheader", "1", CVAR_ARCHIVE );
	ls_showstatsCvar  = Cvar_Get( "ls_showstats",  "1", CVAR_ARCHIVE );
	ls_showsegCvar    = Cvar_Get( "ls_showseg",    "1", CVAR_ARCHIVE );
	ls_showrgtCvar    = Cvar_Get( "ls_showrgt",    "1", CVAR_ARCHIVE );
	ls_maxrowsCvar    = Cvar_Get( "ls_maxrows",    "6", CVAR_ARCHIVE ); /* 0 = auto */
	ls_showpbCvar     = Cvar_Get( "ls_showpb",     "1", CVAR_ARCHIVE );
	ls_showbestCvar   = Cvar_Get( "ls_showbest",   "1", CVAR_ARCHIVE );
	ls_showtimerCvar  = Cvar_Get( "ls_showtimer",  "1", CVAR_ARCHIVE );
	ls_showsepsCvar   = Cvar_Get( "ls_showseps",   "1", CVAR_ARCHIVE );
	ls_showdeltasCvar = Cvar_Get( "ls_showdeltas", "1", CVAR_ARCHIVE );
	ls_showbestdeltasCvar = Cvar_Get( "ls_showbestdeltas", "1", CVAR_ARCHIVE );
	sp_timerDecimalsCvar = Cvar_Get( "sp_timer_decimals", "1", CVAR_ARCHIVE );
	ls_showattCvar    = Cvar_Get( "ls_showatt",    "1", CVAR_ARCHIVE );
	ls_drawCvar       = Cvar_Get( "ls_draw",       "1", CVAR_ARCHIVE );
	Cvar_Get( "ls_split_countdown_lead", "10", CVAR_ARCHIVE );
	Cvar_Get( "ls_imgui", "1", CVAR_ARCHIVE );
	ls_compareCvar    = Cvar_Get( "ls_compare",    "0", CVAR_ARCHIVE ); /* 0=PB, 1=Best Segments, 2=Average */
	ls_timingCvar     = Cvar_Get( "ls_timing",     "0", CVAR_ARCHIVE ); /* 0=Game Time, 1=Real Time */
	ls_100pctCvar     = Cvar_Get( "ls_100pct",     "0", CVAR_ARCHIVE );

	/* UI state indicator: 1 when a run is active (used by menu disableCvar) */
	Cvar_Get( "ls_running", "0", 0 );

	/* Color customization */
	ls_clr_aheadCvar  = Cvar_Get( "ls_clr_ahead",  "72 220 80 1.00",  CVAR_ARCHIVE );
	ls_clr_behindCvar = Cvar_Get( "ls_clr_behind", "220 72 72 1.00",  CVAR_ARCHIVE );
	ls_clr_goldCvar   = Cvar_Get( "ls_clr_gold",   "255 220 50 1.00",  CVAR_ARCHIVE );
	ls_clr_headerCvar = Cvar_Get( "ls_clr_header", "",  CVAR_ARCHIVE );
	ls_clr_timerCvar  = Cvar_Get( "ls_clr_timer",  "224 246 214 1.00",  CVAR_ARCHIVE );
	ls_clr_textCvar   = Cvar_Get( "ls_clr_text",   "214 224 210 0.92",  CVAR_ARCHIVE );
	ls_clr_bgCvar     = Cvar_Get( "ls_clr_bg",     "10 10 15 0.63",  CVAR_ARCHIVE );
	ls_clr_borderCvar = Cvar_Get( "ls_clr_border", "29 52 24 0.58",  CVAR_ARCHIVE );
	ls_clr_mapnameCvar = Cvar_Get( "ls_clr_mapname", "", CVAR_ARCHIVE );
	ls_clr_currentCvar = Cvar_Get( "ls_clr_current", "86 210 55 1.00", CVAR_ARCHIVE );
	ls_clr_completedCvar = Cvar_Get( "ls_clr_completed", "120 130 118 0.62", CVAR_ARCHIVE );
	ls_clr_futureCvar  = Cvar_Get( "ls_clr_future",  "124 124 124 1.00",  CVAR_ARCHIVE );
	ls_clr_dimCvar     = Cvar_Get( "ls_clr_dim",     "",  CVAR_ARCHIVE );
	ls_clr_segtimerCvar = Cvar_Get( "ls_clr_segtimer", "", CVAR_ARCHIVE );
	ls_clr_pausedCvar  = Cvar_Get( "ls_clr_paused",  "",  CVAR_ARCHIVE );
	ls_clr_sepCvar     = Cvar_Get( "ls_clr_sep",     "22 36 18 0.34",  CVAR_ARCHIVE );
	ls_clr_highlightCvar = Cvar_Get( "ls_clr_highlight", "13 28 12 0.27", CVAR_ARCHIVE );
	ls_clr_labelCvar   = Cvar_Get( "ls_clr_label",   "",  CVAR_ARCHIVE );

	ls_text_shadowCvar = Cvar_Get( "ls_text_shadow", "1", CVAR_ARCHIVE );

	/* External LiveSplit type */
	ls_typeCvar    = Cvar_Get( "ls_type",    "0",   CVAR_ARCHIVE );
	/* Clamp old "Both" (2) values to "In-Game Only" (0) */
	if ( ls_typeCvar->integer > LS_TYPE_EXTERNAL ) {
		Cvar_Set( "ls_type", "0" );
	}

	/* External window geometry is now managed by the standalone exe */

	/* Standalone IGT timer overlay */
	ls_igttimerCvar       = Cvar_Get( "ls_igttimer",       "0",     CVAR_ARCHIVE );
	ls_igttimer_xCvar     = Cvar_Get( "ls_igttimer_x",     "638",   CVAR_ARCHIVE );
	ls_igttimer_yCvar     = Cvar_Get( "ls_igttimer_y",     "240",   CVAR_ARCHIVE );
	ls_igttimer_scaleCvar = Cvar_Get( "ls_igttimer_scale", "1.0",   CVAR_ARCHIVE );
	ls_igttimer_alignCvar = Cvar_Get( "ls_igttimer_align", "2",     CVAR_ARCHIVE );
	ls_igtsegtimer        = Cvar_Get( "ls_igtsegtimer",   "0",     CVAR_ARCHIVE );

	/* Keystroke overlay position/scale cvars */
	Cvar_Get( "ks_ingame_only", "1", CVAR_ARCHIVE );
	ks_xCvar       = Cvar_Get( "ks_x",       "297", CVAR_ARCHIVE );
	ks_yCvar       = Cvar_Get( "ks_y",       "375", CVAR_ARCHIVE );
	ks_scaleCvar   = Cvar_Get( "ks_scale",   "0.750000", CVAR_ARCHIVE );
	ks_opacityCvar = Cvar_Get( "ks_opacity", "1.0", CVAR_ARCHIVE );
	ks_mouseCvar   = Cvar_Get( "ks_mouse",   "2",   CVAR_ARCHIVE );  /* 0=off, 1=clicks, 2=clicks+dir */
	Cvar_Get( "ks_imgui", "1", CVAR_ARCHIVE );
	Cvar_Get( "ks_layout", "0", CVAR_ARCHIVE );
	Cvar_Get( "ks_effect", "1", CVAR_ARCHIVE );
	Cvar_Get( "ks_box_w", "22", CVAR_ARCHIVE );
	Cvar_Get( "ks_box_h", "18", CVAR_ARCHIVE );
	Cvar_Get( "ks_gap", "2", CVAR_ARCHIVE );
	Cvar_Get( "ks_rounding", "0", CVAR_ARCHIVE );
	Cvar_Get( "ks_border_size", "1", CVAR_ARCHIVE );
	Cvar_Get( "ks_mouse_grid_size", "92", CVAR_ARCHIVE );
	Cvar_Get( "ks_mouse_grid_cells", "5", CVAR_ARCHIVE );
	Cvar_Get( "ks_mouse_grid_cm", "1", CVAR_ARCHIVE );
	Cvar_Get( "ks_mouse_grid_run_cm", "1", CVAR_ARCHIVE );
	Cvar_Get( "ks_mouse_grid_total_cm_value", "232.626083", CVAR_ARCHIVE );
	Cvar_Get( "ks_active_anchor", "0", CVAR_ARCHIVE );
	Cvar_Get( "ks_active_max", "3", CVAR_ARCHIVE );
	Cvar_Get( "ks_grid_keys", "0", CVAR_ARCHIVE );
	Cvar_Get( "ks_grid_keys_dir", "1", CVAR_ARCHIVE );
	Cvar_Get( "ks_grid_keys_x", "-35", CVAR_ARCHIVE );
	Cvar_Get( "ks_grid_keys_y", "-15", CVAR_ARCHIVE );
	Cvar_Get( "ks_grid_keys_font_scale", "0.72", CVAR_ARCHIVE );
	Cvar_Get( "ks_font_scale", "0.620000", CVAR_ARCHIVE );
	Cvar_Get( "ks_show_use", "0", CVAR_ARCHIVE );
	Cvar_Get( "ks_show_reload", "0", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_bg", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_active", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_border", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_active_border", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_text", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_active_text", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_grid_checker", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_grid_cross", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_grid_trail", "", CVAR_ARCHIVE );
	Cvar_Get( "ks_clr_grid_cm", "", CVAR_ARCHIVE );

	/* Ghost system cvar */
	ls_ghost.enableCvar = Cvar_Get( "ls_ghost", "1", CVAR_ARCHIVE );

	/* Demo auto-record cvar */
	sp_autorecordCvar = Cvar_Get( "sp_autorecord", "0", CVAR_ARCHIVE );
	sp_demofpsCvar    = Cvar_Get( "sp_demofps",    "40", CVAR_ARCHIVE );

	LS_BuildSplitTable();
	ls_prev100pct = ( ls_100pctCvar && ls_100pctCvar->integer ) ? 1 : 0;
	ls_prevDifficulty = LS_DetectDifficulty();
	ls_prevHL1Mode = LS_HL1ModeActive() ? 1 : 0;
	LS_Load();

	/* Restore mode from save or use cvar defaults */
	if ( ls.active ) {
		/* active run: use saved mode/mission */
		LS_SetupMode( ls.runMode, ls.runMission );
	} else {
		int initMode = ls_modeCvar ? ls_modeCvar->integer : 0;
		LS_SetupMode( initMode, ls_missionCvar ? ls_missionCvar->integer : 1 );
		/* Ensure the cvar-selected mode is loaded (may differ from state file) */
		LS_LoadMode( initMode );
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
	Cmd_AddCommand( "livesplit_debug", LS_Debug_f );

	/* Split Records viewer */
	SV_InitCvars();
	SV_InitCommands();

	/* Race / competition lobby */
	LS_RaceInit();

	/* Shared memory for standalone LiveSplit window */
	LS_ShmInit();

	Com_Printf( "^2LiveSplit: Initialized (%d maps, %d visible)\n", ls.numMaps, ls.numVisible );


}

void SCR_LiveSplitShutdown( void ) {
	LS_RaceShutdown();
	LS_ShmShutdown();
	LS_ExtDisconnect();
	if ( ls.initialized ) {
		LS_SaveAll();
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

/*
===================
LS_TriggerAlert

Shows a centered alert with bounce+fade animation.
===================
*/
static void LS_TriggerAlert( const char *text, float r, float g, float b ) {
	Q_strncpyz( ls.alertText, text, sizeof( ls.alertText ) );
	ls.alertColor[0] = r;
	ls.alertColor[1] = g;
	ls.alertColor[2] = b;
	ls.alertColor[3] = 1.0f;
	ls.alertStartMs = Sys_Milliseconds();
}

/*
===================
LS_TriggerVerify

Triggers the run-finish verification overlay showing config check results.
===================
*/
static void LS_TriggerVerify( void ) {
	ls.verifyStartMs  = Sys_Milliseconds();
	ls.verifyValid    = ( !ls.settingsModified && !ls.cheatsUsed ) ? qtrue : qfalse;
	ls.verifyModCount = ls.settingsModCount;
	ls.verifyCheats   = ls.cheatsUsed;
	ls.verifyPauses   = ls.totalPauses;
	ls.verifyUndos    = ls.totalUndos;
	ls.verifySkips    = ls.totalSkips;
}

/*
===================
LS_QuickCheckSettings

Lightweight per-frame cvar check (no console spam).
Sets settingsModified/settingsModCount and triggers alert on first detection.
===================
*/
static void LS_QuickCheckSettings( void ) {
	const lsCvarCheck_t *check;
	int idx, newDetections = 0;
	char val[256];

	for ( idx = 0, check = ls_settingsTable; check->name; check++, idx++ ) {
		float fDefault, fActual, diff;
		qboolean differs = qfalse;

		Cvar_VariableStringBuffer( check->name, val, sizeof( val ) );
		fDefault = atof( check->defaultVal );
		fActual  = atof( val );

		if ( fDefault == 0.0f && fActual == 0.0f ) {
			if ( Q_stricmp( val, check->defaultVal ) != 0 &&
				 Q_stricmp( val, "0" ) != 0 &&
				 Q_stricmp( val, "0.0" ) != 0 &&
				 Q_stricmp( val, "" ) != 0 ) {
				differs = qtrue;
			}
		} else {
			diff = fActual - fDefault;
			if ( diff < 0 ) diff = -diff;
			if ( diff > 0.001f ) differs = qtrue;
		}

		if ( differs && idx < 80 && !ls.settingsDetected[idx] ) {
			/* New cvar modification detected - log it */
			ls.settingsDetected[idx] = qtrue;
			ls.settingsModCount++;
			newDetections++;
			Com_Printf( "^1LiveSplit: ^7%s ^1changed to ^7%s ^1(default: ^2%s^1)\n",
				check->name, val, check->defaultVal );
		}
	}

	if ( newDetections > 0 ) {
		ls.settingsModified = qtrue;
		LS_TriggerAlert( "MODIFIED SETTINGS DETECTED", 1.0f, 0.8f, 0.2f );
	}
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

	/* Send final game-time to external LiveSplit and trigger the final split.
	   Use LS_CumulativeTime for the send (same source as in-game HUD). */
	if ( LS_ExtEnabled() ) {
		LS_ExtSendGameTime( LS_CumulativeTime( ls.modeLastIdx ) );
		LS_ExtSend( "split" );
	}

	/* Per-category completions and PB */
	if ( ls.runMode == LS_MODE_IL ) {
		/* IL: completions already incremented in LS_CompleteSplit.
		   bestTimeMs is used as the IL PB (LS_GetCatPB returns &bestTimeMs)
		   and was already updated there.  However, LS_SavePbSegments was
		   NOT called because the condition `finalIGT < *pb` always fails
		   (*pb == bestTimeMs == finalIGT at this point).  Use the pre-run
		   snapshot savedPBMs for comparison so pbSegmentMs gets set. */
		if ( ls.savedPBMs == 0 || finalIGT < ls.savedPBMs ) {
			LS_SavePbSegments();
		}
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
	LS_TriggerVerify();

	/* Record in history (dynamic, no fixed limit) */
	LS_HistoryEnsure( ls.numHistoryRuns + 1 );
	{
		lsRunHistory_t *r = &ls.history[ls.numHistoryRuns];
		int i, n = 0;
		memset( r, 0, sizeof( *r ) );
		r->difficulty = ls.currentDifficulty;
		r->mode       = ls.runMode;
		r->categoryVariant = LS_CurCategoryVariant();
		r->missionNum = ls.runMission;
		r->mapIdx     = ( ls.runMode == LS_MODE_IL ) ? ls.modeFirstIdx : -1;
		r->totalIGTMs = finalIGT;
		r->totalRGTMs = ls.runSavedRealMs;
		r->attemptId  = LS_MaxAttemptId( ls.runMode, LS_DiffIdx( ls.currentDifficulty ), ls.runMission, r->mapIdx, r->categoryVariant ) + 1;
		for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
			if ( !ls.splits[i].cutscene ) {
				r->splitTimes[n] = ls.splits[i].currentTimeMs;
				n++;
			}
		}
		r->numSplits = n;
		r->endTime   = time( NULL );
		/* Estimate start time from end time and RGT duration */
		r->startTime = r->endTime - ( ls.runSavedRealMs / 1000 );
		r->isStartedSynced = qtrue;
		r->isEndedSynced   = qtrue;
		ls.numHistoryRuns++;
	}

	LS_Save();
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
	   player->numSecretsFound resets to 0 on each map load
	   (only health is persisted via gentityPersFields), so
	   prevLiveSecretsFound already IS the per-map found count.
	   Use prevLive (from previous frame) because during map
	   transitions the configstring already shows the NEW map.
	   If found values were already pre-saved (deferral or
	   undo+re-complete), keep them. */
	if ( ls_100pctCvar && ls_100pctCvar->integer ) {
		if ( ls.splits[idx].secretsFound == 0 && ls.splits[idx].treasureFound == 0 ) {
			int sf = ls.prevLiveSecretsFound;
			int tf = ls.prevLiveTreasureFound;
			ls.splits[idx].secretsFound  = ( sf > 0 ) ? sf : 0;
			ls.splits[idx].treasureFound = ( tf > 0 ) ? tf : 0;
		}
	}

	/* Inject a final ghost frame at the exact split time so the ghost's
	   total time matches the split time 1:1 (recording samples at 20Hz
	   so the last frame can be up to 50ms early). */
	if ( ls_ghost.recCount > 0 && ls_ghost.recCount < GHOST_MAX_FRAMES ) {
		ghostFrame_t *last = &ls_ghost.rec[ls_ghost.recCount - 1];
		if ( last->timeMs != t ) {
			ghostFrame_t *gf = &ls_ghost.rec[ls_ghost.recCount];
			*gf = *last;
			gf->timeMs = t;
			ls_ghost.recCount++;
		}
	}

	if ( ls.splits[idx].d[di].bestTimeMs == 0 ||
		 t < ls.splits[idx].d[di].bestTimeMs ) {
		ls.splits[idx].d[di].bestTimeMs = t;
		ls.splits[idx].goldFlashMs = Sys_Milliseconds(); /* trigger gold flash */
		LS_GhostOnGoldSplit( ls.splits[idx].mapname );
	} else {
		/* Not a gold - save ghost if time is better than previous ghost.
		   This way the ghost file always tracks the best recorded run,
		   even when it wasn't fast enough for a new gold/best segment. */
		int ghostTimeMs = 0;
		if ( ls_ghost.playLoaded && ls_ghost.playCount > 0 ) {
			ghostTimeMs = ls_ghost.play[ls_ghost.playCount - 1].timeMs;
		}
		if ( ls_ghost.recCount > 0 && ( ghostTimeMs <= 0 || t < ghostTimeMs ) ) {
			LS_GhostSave( ls.splits[idx].mapname );
			/* Load into playback so ghost is visible on next attempt */
			memcpy( ls_ghost.play, ls_ghost.rec,
				sizeof( ghostFrame_t ) * ls_ghost.recCount );
			ls_ghost.playCount     = ls_ghost.recCount;
			ls_ghost.playLoaded    = qtrue;
			ls_ghost.playSearchIdx = 0;
			ls_ghost.isNewGold     = qfalse; /* not a gold - stays blue */
			Com_Printf( "^3Ghost: saved better run (%d ms < ghost %d ms)\n", t, ghostTimeMs );
		}
	}

	ls.runTotalIGTMs += t;

	/* Bump ASL split sequence number (monitored by external LiveSplit) */
	ls_aslState.splitSeqNum++;
	Com_Printf( "^5LiveSplit: splitSeqNum=%d (map=%s idx=%d t=%d)\n",
			ls_aslState.splitSeqNum, ls.splits[idx].mapname, idx, t );

	/* Notify external LiveSplit Server.
	   Use LS_CumulativeTime (same source as in-game HUD) so the split
	   records the exact value the player sees on screen. */
	LS_ExtSendGameTime( LS_CumulativeTime( ls.modeLastIdx ) );
	LS_ExtSend( "split" );

	/* Write updated state to demo file if recording */
	LS_DemoWriteUpdate();

	/* Check if this was the end map for the current mode */
	if ( idx == ls.modeEndMapIdx ) {
		LS_FinishRun( nowReal );
		/* Write final "finished" state to demo so playback shows the
		   completed run (runFinished=true, final PB data, etc.) */
		LS_DemoWriteUpdate();
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
	qboolean preserveActualMapname = qfalse;
	int curMode, curMission;

	if ( !ls.initialized ) return;

	/* Keep ls_running cvar in sync for UI disableCvar checks */
	{
		static int prevRunning = -1;
		int curRunning = ( ls.active && !ls.runFinished ) ? 1 : 0;
		if ( curRunning != prevRunning ) {
			Cvar_Set( "ls_running", curRunning ? "1" : "0" );
			prevRunning = curRunning;
		}
	}

	/* External LiveSplit connection / per-frame game-time sync */
	LS_ExtFrame();

	/* ---- Transitional state guard ----
	   During map loading, the renderer fires updatescreen callbacks
	   which invoke LS_Frame() while cl.mapname is still empty (zeroed
	   by CL_ClearState, not yet set by CL_InitCGame).  Running the
	   full LS_Frame in this state causes:
	   (a) IGT accumulation of loading time (Guard 1 lets it through
	       because empty igtMap[0] doesn't trigger the mismatch check)
	   (b) prevMapname gets cleared by the cl.mapname=='' early return
	       later in this function, which then triggers spurious map-
	       change detection on the first valid frame
	   (c) Mode/difficulty/settings checks may misfire during the
	       unstable transitional state
	   Fix: bail immediately, only updating lastRealTimeMs to avoid a
	   time spike when the next valid frame arrives.  This does NOT
	   affect quickloads (SV_MapRestart_f never clears cl.mapname). */
	if ( cls.state >= CA_CONNECTED && cl.mapname[0] == '\0' ) {
		ls.lastRealTimeMs = Sys_Milliseconds();
		return;
	}

	/* ---- IGT accumulation (runs BEFORE any early returns) ----
	   Timer ticks every frame so quickloads / death-reloads keep it
	   running.  Guards that prevent counting during loads/transitions:

	   0) Client is fully active - never count while CA_LOADING/PRIMED.
	   1) Map-name mismatch - if cl.mapname has changed but
	      actualMapname hasn't been updated yet (map-change detection
	      runs later in LS_Frame), we're between loading and detection.
	   2) mapLoadFreeze flag - set by map-change detection when a new
	      map is entered.  Cleared only when the UI fires the
	      playerstart handler (player clicks continue arrow).
	   3) ls_loading cvar - set to 1 during loads, cleared to 0 by
	      the UI playerstart handler.
	   ALL must pass for time to accumulate.  Do not block only because
	   g_reloading is set: finish/changelevel triggers can set it before
	   the actual loading screen, and IGT must still count until the
	   screen/map transition begins. */
	if ( ls.active && ls.currentMapIndex >= 0 && !ls.manualPause ) {
		int rNow = Sys_Milliseconds();
		int rDt  = rNow - ls.lastRealTimeMs;
		qboolean canAccumulate = qtrue;

		/* Guard 0: never count loading/intermediate client states. */
		if ( cls.state < CA_ACTIVE ) {
			canAccumulate = qfalse;
		}

		/* Guard 1: map-name mismatch - if cl.mapname already shows
		   the new map but actualMapname still has the old one, we're
		   in the gap between loading and map-change detection.
		   Also block when actualMapname is empty (first frame after
		   auto-start before afterMapChange runs). */
		if ( canAccumulate && !ls.actualMapname[0] ) {
			canAccumulate = qfalse;
		} else if ( canAccumulate ) {
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

		/* Guard 2b: ESC menu open - pause IGT while UI is active.
		   Manual IL start can be invoked while the UI catcher is still set;
		   allow it to start immediately, then restore normal UI pause rules
		   once gameplay has a frame with no UI catcher. */
		if ( ls.manualStartIgnoreUi && !( cls.keyCatchers & KEYCATCH_UI ) ) {
			ls.manualStartIgnoreUi = qfalse;
		}
		if ( canAccumulate && ( cls.keyCatchers & KEYCATCH_UI ) && !ls.manualStartIgnoreUi ) {
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
		   on the briefing screen (UI sets ls_loading=0).  CA_ACTIVE can
		   happen while the briefing/arrow is still on screen, so never use
		   it alone to resume IGT for normal maps.
		   Cutscene maps: no briefing screen exists, so auto-clear
		   as soon as CA_ACTIVE is reached. */
		if ( ls.mapLoadFreeze && cls.state >= CA_ACTIVE ) {
			char freezeMap[LS_MAX_MAPNAME];
			int ci;
			LS_ExtractMapname( cl.mapname, freezeMap, sizeof( freezeMap ) );
			ci = freezeMap[0] ? LS_FindMapIndex( freezeMap ) : ls.currentMapIndex;
			if ( ci < 0 ) {
				ci = ls.currentMapIndex;
			}
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

	/* ---- Per-frame external LiveSplit game-time sync ----
	   Sent here (AFTER IGT accumulation) so the external timer receives
	   the exact same cumulative value that the in-game HUD displays.
	   Previously this was in LS_ExtFrame (before accumulation), causing
	   the external timer to lag one frame behind (~8-16 ms) and visibly
	   "stop and add a few ms" during map changes. */
	if ( lsext_connected && ls.active && !ls.runFinished ) {
		LS_ExtSendGameTime( LS_CumulativeTime( ls.modeLastIdx ) );
	}

	/* ---- Periodic demo state update ----
	   Write the current LS state to the demo file every ~100 ms so that
	   playback-side timer interpolation stays tightly synchronised with
	   the actual recorded IGT.  Without this, the demo timer drifts
	   between infrequent state snapshots because the recording uses
	   Sys_Milliseconds() (wall-clock) for IGT while playback uses
	   cl.serverTime for interpolation. */
	if ( clc.demorecording && ls.active && !ls.runFinished ) {
		static int lsDemoLastWriteMs = 0;
		int demoNow = Sys_Milliseconds();
		if ( demoNow - lsDemoLastWriteMs >= 100 ) {
			LS_DemoWriteUpdate();
			lsDemoLastWriteMs = demoNow;
		}
	}

	/* Skip the rest of LS_Frame during savegame loading to avoid
	   unstable engine state.  IGT accumulation above already ran. */
	{
		static int prevSavegameLoading = 0;
		int curSavegameLoading = Cvar_VariableIntegerValue( "savegame_loading" );
		if ( curSavegameLoading ) {
			prevSavegameLoading = curSavegameLoading;
			return;
		}
		/* Savegame load completed: savegame_loading transitioned to 0.
		   Clear mapLoadFreeze and ls_loading that may have been set by
		   a previous/interrupted map transition.  Without this, the
		   timer stays frozen permanently after quickload/load because
		   playerstart (which normally clears ls_loading) never fires
		   during a savegame restore.
		   NOTE: savegame_loading can go to 0 before cls.state reaches
		   CA_ACTIVE (it's cleared in game init, before first snapshot).
		   Keep prevSavegameLoading alive until CA_ACTIVE so the fix
		   fires on the right frame. */
		if ( prevSavegameLoading && ls.active ) {
			if ( cls.state >= CA_ACTIVE ) {
				char sgMap[LS_MAX_MAPNAME];
				int sgIdx;
				int oldIdx = ls.currentMapIndex;
				if ( ls.mapLoadFreeze || Cvar_VariableIntegerValue( "ls_loading" ) ) {
					ls.mapLoadFreeze = qfalse;
					Cvar_Set( "ls_loading", "0" );
				}
				/* Sync prevMapname/actualMapname to the loaded map so
				   map-change detection (later in this function) does NOT
				   fire.  Backward loads no longer pause IGT: keep the
				   latest reached split pointer, but allow time to keep
				   accumulating once the save is fully active. */
				LS_ExtractMapname( cl.mapname, sgMap, sizeof( sgMap ) );
				if ( sgMap[0] ) {
					sgIdx = LS_FindMapIndex( sgMap );
					if ( sgIdx >= 0 ) {
						if ( !ls.runFinished && sgIdx != oldIdx ) {
							ls.backtrackPause = qfalse;
							ls.backtrackTargetIndex = -1;
							LS_UpdateCurVisRow();
							Com_Printf( "^3LiveSplit: loaded off-route map '%s' - IGT keeps running on current split\n",
								ls.splits[sgIdx].displayName ? ls.splits[sgIdx].displayName : ls.splits[sgIdx].mapname );
						} else if ( sgIdx != ls.currentMapIndex ) {
							ls.backtrackPause = qfalse;
							ls.backtrackTargetIndex = -1;
							ls.currentMapIndex = sgIdx;
							LS_UpdateCurVisRow();

							/* Force live 100% counters to be re-read from the loaded save. */
							LS_ResetLiveMissionStatsForMap( sgIdx );
						} else {
							ls.backtrackPause = qfalse;
							ls.backtrackTargetIndex = -1;
							/* Same tracked map: savegame stats can go backwards. */
							LS_ResetLiveMissionStatsForMap( sgIdx );
						}

						LS_UpdateCurVisRow();
						/* In IL mode, move the target so the IL
						   auto-update code doesn't misfire and
						   restart the timer on this map. */
						if ( ls.runMode == LS_MODE_IL && !( !ls.runFinished && sgIdx != oldIdx ) ) {
							ls.modeFirstIdx  = sgIdx;
							ls.modeLastIdx   = sgIdx;
							ls.modeEndMapIdx = sgIdx;
						}
					}
					Q_strncpyz( ls.prevMapname, sgMap, LS_MAX_MAPNAME );
					Q_strncpyz( ls.actualMapname, sgMap, LS_MAX_MAPNAME );
				}
				ls.lastRealTimeMs = Sys_Milliseconds();
				prevSavegameLoading = 0;
			}
			/* else: CA_ACTIVE not reached yet - keep prevSavegameLoading
			   so we retry next frame.  Return early like savegame_loading
			   was still active to avoid running map-change detection in
			   an intermediate state.  Update lastRealTimeMs to avoid
			   a large IGT jump when the fix finally fires. */
			else {
				ls.lastRealTimeMs = Sys_Milliseconds();
				return;
			}
		} else {
			prevSavegameLoading = 0;
		}
	}

	/* Deferred Full Game start: the run begins when the player clicks
	   the continue arrow on the briefing screen (playerstart fires,
	   ls_loading goes to 0), not during loading or the briefing. */
	if ( ls.fgPendingStart && cls.state >= CA_ACTIVE &&
		 !Cvar_VariableIntegerValue( "ls_loading" ) && !LS_RaceBlocksTimerStart() ) {
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
		ls.backtrackPause   = qfalse;
		ls.backtrackTargetIndex = -1;
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
			lsext_timerStarted = qtrue;
			lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;
		}
		Com_Printf( "^2LiveSplit: Full Game run started (briefing dismissed)\n" );

		/* Immediate validation on run start */
		ls.settingsCheckInterval = 0;
		LS_QuickCheckSettings();
		if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
			ls.cheatsUsed = qtrue;
			LS_TriggerAlert( "CHEATS ACTIVATED", 1.0f, 0.2f, 0.2f );
		}
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
			/* Ensure the new mode's .lss files are loaded (lazy) */
			LS_LoadMode( curMode );
			/* Keep prevMapname = current map so auto-start only fires
			   on an actual level load, not while already on the map.
			   NOTE: currentMap isn't populated yet at this point, so
			   extract directly from cl.mapname. */
			if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
				char tmp[LS_MAX_MAPNAME];
				LS_ExtractMapname( cl.mapname, tmp, sizeof( tmp ) );
				Q_strncpyz( ls.prevMapname, tmp, LS_MAX_MAPNAME );
			}
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
				/* Keep prevMapname = current map so auto-start only
				   fires on actual level load, not while standing here.
				   NOTE: currentMap isn't populated yet, use cl.mapname. */
				if ( cls.state >= CA_CONNECTED && cl.mapname[0] ) {
					char tmp[LS_MAX_MAPNAME];
					LS_ExtractMapname( cl.mapname, tmp, sizeof( tmp ) );
					Q_strncpyz( ls.prevMapname, tmp, LS_MAX_MAPNAME );
				}
			}
		}
	}

	/* Auto Jump is only valid with HL1 movement.  Also treat changing
	   bh_movement as a category change: if a timer is active/finished,
	   reset transient run data just like a difficulty change. */
	{
		int curHL1 = LS_HL1ModeActive() ? 1 : 0;

		if ( !curHL1 && Cvar_VariableIntegerValue( "bh_autojump" ) ) {
			Cvar_Set( "bh_autojump", "0" );
		}

		if ( curHL1 != ls_prevHL1Mode ) {
			if ( ls.active || ls.runFinished ) {
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
				ls.backtrackPause  = qfalse;
				ls.backtrackTargetIndex = -1;
				ls.currentMapIndex = -1;
				ls.curVisRow       = -1;
				ls.baseSecretsFound  = 0;
				ls.baseTreasureFound = 0;
				for ( k = 0; k < ls.numMaps; k++ ) {
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].splitSkipped  = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
					ls.splits[k].secretsFound  = 0;
					ls.splits[k].treasureFound = 0;
				}
				Com_Printf( "^2LiveSplit: Run reset due to HL1 movement change\n" );
			}

			LS_SaveAll();
			LS_Load();
			LS_SetupMode( ls.runMode, ls.runMission );
			ls_prevHL1Mode = curHL1;
			Com_Printf( "^2LiveSplit: switched to %s movement category\n", curHL1 ? "HL1" : "RTCW" );
		}
	}

	/* Detect ls_100pct toggle: save current data and reload from new file set */
	{
		int cur100 = ( ls_100pctCvar && ls_100pctCvar->integer ) ? 1 : 0;
		if ( cur100 != ls_prev100pct ) {
			/* If a run is active, force-reset it first */
			if ( ls.active || ls.runFinished ) {
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
				ls.curVisRow       = -1;
				for ( k = 0; k < ls.numMaps; k++ ) {
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].splitSkipped  = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
					ls.splits[k].secretsFound  = 0;
					ls.splits[k].treasureFound = 0;
				}
				Com_Printf( "^2LiveSplit: Run reset due to 100%% category change\n" );
			}
			/* Save current data to the OLD file set before switching */
			{
				int old100 = ls_prev100pct;
				/* Temporarily restore old value for SaveAll path generation */
				if ( ls_100pctCvar ) Cvar_Set( "ls_100pct", va( "%d", old100 ) );
				LS_SaveAll();
				if ( ls_100pctCvar ) Cvar_Set( "ls_100pct", va( "%d", cur100 ) );
			}
			/* Clear in-memory data before loading new set */
			{
				int k, di, gi;
				for ( k = 0; k < ls.numMaps; k++ ) {
					int s;
					for ( s = 0; s < LS_TOTAL_DIFF_SLOTS; s++ ) {
						ls.splits[k].d[s].bestTimeMs       = 0;
						ls.splits[k].d[s].pbSegmentMs      = 0;
						ls.splits[k].d[s].totalAttempts     = 0;
						ls.splits[k].d[s].totalCompletions  = 0;
					}
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
				}
				for ( di = 0; di < LS_TOTAL_DIFF_SLOTS; di++ ) {
					ls.fgAttempts[di]    = 0;
					ls.fgCompletions[di] = 0;
					ls.fgPB[di]          = 0;
					ls.fgPBRgt[di]       = 0;
					for ( gi = 0; gi < LS_NUM_MISSION_GROUPS; gi++ ) {
						ls.msAttempts[gi][di]    = 0;
						ls.msCompletions[gi][di] = 0;
						ls.msPB[gi][di]          = 0;
						ls.msPBRgt[gi][di]       = 0;
					}
				}
				LS_HistoryFree();
			}
			LS_Load();
			ls_prev100pct = cur100;
			Com_Printf( "^2LiveSplit: switched to %s .lss files\n", cur100 ? "100%%" : "Any%%" );
		}
	}

	/* Detect difficulty change: update displayed splits immediately */
	{
		int curDiff = LS_DetectDifficulty();
		if ( curDiff != ls_prevDifficulty ) {
			/* If a run is active, force-reset it first */
			if ( ls.active || ls.runFinished ) {
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
				ls.curVisRow       = -1;
				for ( k = 0; k < ls.numMaps; k++ ) {
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].splitSkipped  = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
					ls.splits[k].secretsFound  = 0;
					ls.splits[k].treasureFound = 0;
				}
				Com_Printf( "^2LiveSplit: Run reset due to difficulty change\n" );
			}
			ls.currentDifficulty = curDiff;
			ls_prevDifficulty    = curDiff;
			Com_Printf( "^2LiveSplit: difficulty changed to %s\n", ls_diffShortTags[LS_DiffIdx( curDiff )] );
		}
	}

	nowReal     = Sys_Milliseconds();
	isPaused    = Cvar_VariableIntegerValue( "cl_paused" ) ? qtrue : qfalse;
	isConnected = ( cls.state >= CA_CONNECTED ) ? qtrue : qfalse;

	if ( !isConnected || cl.mapname[0] == '\0' ) {
		/* Clear prevMapname so the next real map load is detected as
		   a new map change, enabling auto-start after reset+reload */
		ls.prevMapname[0] = '\0';

		/* Auto-disable sv_cheats on disconnect when the timer
		   is off / reset (no active run in progress) */
		if ( Cvar_VariableIntegerValue( "sv_cheats" ) && !ls.active ) {
			Cvar_Set( "sv_cheats", "0" );
			Com_Printf( "^2LiveSplit: sv_cheats auto-disabled (disconnect)\n" );
		}
		/* Clear cheats flag when no run is active (reset already happened) */
		if ( !ls.active && !ls.runFinished ) {
			ls.cheatsUsed = qfalse;
		}
		return;
	}

	/* Detect sv_cheats usage - only while the timer is actively counting.
	   After a run has finished, practice commands/settings may be used before
	   reset without retroactively invalidating the already-finished run.
	   Flag persists until run reset/auto-start. */
	if ( ls.active && !ls.runFinished && Cvar_VariableIntegerValue( "sv_cheats" ) ) {
		if ( !ls.cheatsUsed ) {
			LS_TriggerAlert( "CHEATS ACTIVATED", 1.0f, 0.2f, 0.2f );
			Com_Printf( "^1LiveSplit: sv_cheats detected!\n" );
		}
		ls.cheatsUsed = qtrue;
	}

	/* Periodic settings check every ~60 frames (about once per second) */
	if ( ls.active && !ls.runFinished ) {
		ls.settingsCheckInterval++;
		if ( ls.settingsCheckInterval >= 60 ) {
			ls.settingsCheckInterval = 0;
			LS_QuickCheckSettings();
		}
	}

	LS_ExtractMapname( cl.mapname, currentMap, sizeof( currentMap ) );
	if ( currentMap[0] == '\0' ) return;

	/* ---- Mission objective tracking: parse CS_MISSIONSTATS each frame ---- */
	/* Save previous-frame values before overwriting with new data. During map
	   transitions the configstring flips to the NEW map, so prev values let
	   LS_CompleteSplit use the OLD map's stats. Race also uses these objective
	   counts outside 100% mode, so the parser must not be gated by ls_100pct. */
	ls.prevLiveSecretsFound  = ls.liveSecretsFound;
	ls.prevLiveSecretsTotal  = ls.liveSecretsTotal;
	ls.prevLiveTreasureFound = ls.liveTreasureFound;
	ls.prevLiveTreasureTotal = ls.liveTreasureTotal;
	LS_ParseMissionStats();

	/* For IL mode (AUTO), auto-update target map only when ls_map cvar is empty.
	   If the run is active but not finished, reset and restart on the new map. */
	if ( ls.runMode == LS_MODE_IL &&
		 ( !ls_mapCvar || !ls_mapCvar->string[0] ) ) {
		int ilIdx = LS_FindMapIndex( currentMap );
		if ( ilIdx >= 0 && !ls.splits[ilIdx].cutscene &&
			 ilIdx != ls.modeFirstIdx &&
			 !( ls.active && !ls.runFinished && ilIdx < ls.currentMapIndex ) ) {
			/* If a run is active (not finished), reset and auto-restart on the new map */
			if ( ls.active && !ls.runFinished ) {
				int k, di;
				int restartNow = Sys_Milliseconds();

				/* Reset all split data */
				ls.active          = qfalse;
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
				ls.curVisRow       = -1;
				ls.baseSecretsFound  = 0;
				ls.baseTreasureFound = 0;
				for ( k = 0; k < ls.numMaps; k++ ) {
					ls.splits[k].currentTimeMs = 0;
					ls.splits[k].splitDone     = qfalse;
					ls.splits[k].splitSkipped  = qfalse;
					ls.splits[k].prevGoldMs    = 0;
					ls.splits[k].goldFlashMs   = 0;
					ls.splits[k].secretsFound  = 0;
					ls.splits[k].treasureFound = 0;
				}

				/* Switch to the new IL map */
				LS_SetupMode( LS_MODE_IL, ls.runMission );

				/* Auto-restart timer on the new map */
				ls.active            = qtrue;
				ls_aslState.runStartCount++;
				ls_aslState.splitSeqNum = 0;
				ls.runStartRealMs    = restartNow;
				ls.lastServerTime    = cl.serverTime;
				ls.lastRealTimeMs    = restartNow;
				ls.currentDifficulty = LS_DetectDifficulty();
				di = LS_CurDiffIdx();
				LS_SavePreRunPBs();
				ls.currentMapIndex   = ilIdx;
				LS_UpdateCurVisRow();
				ls.splits[ilIdx].d[di].totalAttempts++;
				Q_strncpyz( ls.prevMapname, currentMap, LS_MAX_MAPNAME );

				/* Reset validation state for the fresh run */
				ls.cheatsUsed       = qfalse;
				ls.settingsModified = qfalse;
				ls.settingsModCount = 0;

				/* Re-check cheats on the new map */
				ls.settingsCheckInterval = 0;
				LS_QuickCheckSettings();
				if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
					ls.cheatsUsed = qtrue;
					LS_TriggerAlert( "CHEATS ACTIVATED", 1.0f, 0.2f, 0.2f );
				}

				/* Freeze IGT until loading is done (player clicks continue) */
				ls.mapLoadFreeze = qtrue;
				ls.mapLoadFreezeStartMs = restartNow;
				Cvar_Set( "ls_loading", "1" );

				LS_Save();

				lsext_recvLen = 0;
				LS_ExtSend( "initgametime" );
				LS_ExtSend( "starttimer" );
				LS_ExtSend( "pausegametime" );
				lsext_timerStarted = qtrue;
				lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;

				Com_Printf( "^2LiveSplit: IL run auto-restarted on '%s'\n",
					ls.splits[ilIdx].displayName ? ls.splits[ilIdx].displayName : ls.splits[ilIdx].mapname );
			} else {
				/* Not active: directly update target to the new map.
				   LS_SetupMode would use stale ls.actualMapname (updated
				   at end of frame) so set the indices directly. */
				ls.modeFirstIdx = ilIdx;
				ls.modeLastIdx  = ilIdx;
			}
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
	/* Check sv_spTransition early: spmap to the SAME map while the
	   disconnect is too brief for LS_Frame to clear prevMapname means
	   prevMapname == currentMap and the normal diff check below won't
	   fire.  Force-clear prevMapname so the detection triggers. */
	if ( Cvar_VariableIntegerValue( "sv_spTransition" ) &&
		 !Q_stricmp( currentMap, ls.prevMapname ) && !ls.active ) {
		ls.prevMapname[0] = '\0';
	}

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
		if ( !realMapChange && !ls.active && !ls.runFinished && !LS_RaceBlocksTimerStart() ) {
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

		/* Reset live 100% counters on every real forward/current map change.
		   CS_MISSIONSTATS hasn't been set yet for the new map, so without
		   this reset the display shows stale values from the previous map
		   until the server sends the new data.  Do not clear them for
		   backwards/off-route movement: the timer remains on the latest
		   reached split and waits for the player to return there. */
		if ( !( ls.active && !ls.runFinished && newIdx >= 0 &&
			   newIdx < ls.currentMapIndex ) ) {
			ls.liveObjectivesFound = 0;
			ls.liveObjectivesTotal = 0;
			ls.liveSecretsFound  = 0;
			ls.liveSecretsTotal  = 0;
			ls.liveTreasureFound = 0;
			ls.liveTreasureTotal = 0;
		}

		isSpTransition = Cvar_VariableIntegerValue( "sv_spTransition" ) ? qtrue : qfalse;
		Cvar_Set( "sv_spTransition", "0" );

		/* ---- Guard: non-sequential map change during active run ----
		   If the player goes to any non-sequential map, keep the split
		   pointer on the latest reached stage, but do not pause IGT.
		   actualMapname is updated to the loaded map so accumulation can
		   continue after the loading freeze clears. */
		if ( ls.active && !ls.runFinished && newIdx >= 0 &&
			 newIdx != ls.currentMapIndex + 1 ) {
			preserveActualMapname = qfalse;
			ls.backtrackPause = qfalse;
			ls.backtrackTargetIndex = -1;
			LS_UpdateCurVisRow();
			Com_Printf( "^3LiveSplit: moved off-route to '%s' - IGT keeps running on current split\n",
				ls.splits[newIdx].displayName ? ls.splits[newIdx].displayName : ls.splits[newIdx].mapname );
			/* Keep ls_loading=1 for normal maps.  The client can already be
			   CA_ACTIVE while the pregame briefing arrow is still waiting, and
			   clearing here lets a few frames of transition time leak into IGT.
			   The UI playerstart handler clears ls_loading when gameplay starts. */
			if ( newIdx >= 0 && ls.splits[newIdx].cutscene ) {
				Cvar_Set( "ls_loading", "0" );
			}
			goto afterMapChange;
		}

		/* IL mode: auto-start on target map, or any non-cutscene map in AUTO (empty ls_map) */
		if ( ls.runMode == LS_MODE_IL && !ls.active && !LS_RaceBlocksTimerStart() &&
			 newIdx >= 0 && !ls.splits[newIdx].cutscene &&
			 ( newIdx == ls.modeFirstIdx ||
			   ( !ls_mapCvar || !ls_mapCvar->string[0] ) ) ) {
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
			ls.backtrackPause   = qfalse;
			ls.backtrackTargetIndex = -1;
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
			Q_strncpyz( ls.prevMapname, currentMap, LS_MAX_MAPNAME );
			Q_strncpyz( ls.actualMapname, currentMap, LS_MAX_MAPNAME );
			ls.mapLoadFreeze = qfalse;
			Cvar_Set( "ls_loading", "0" );
			ls.manualStartIgnoreUi = qtrue;
			/* AUTO mode: update IL target to this map */
			if ( !ls_mapCvar || !ls_mapCvar->string[0] ) {
				ls.modeFirstIdx  = newIdx;
				ls.modeLastIdx   = newIdx;
				ls.modeEndMapIdx = newIdx;
				LS_RebuildVisMap();
			}
			/* IL: attempts tracked via per-split data only */
			ls.splits[newIdx].d[di].totalAttempts++;
			LS_UpdateCurVisRow();
			LS_Save();
			LS_AutoRecordStart();
			/* IL starts are single-map attempts: start IGT as soon as the map is
			   active.  Stale mapLoadFreeze/ls_loading would otherwise keep IGT at
			   0 until a quickload refreshes actualMapname/loading state. */
			lsext_recvLen = 0; /* flush stale recv data */
			LS_ExtSend( "initgametime" );
			LS_ExtSend( "starttimer" );
			LS_ExtSend( "pausegametime" );
			lsext_timerStarted = qtrue;
			lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;

			/* Immediate validation on IL run start */
			ls.settingsCheckInterval = 0;
			LS_QuickCheckSettings();
			if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
				ls.cheatsUsed = qtrue;
				LS_TriggerAlert( "CHEATS ACTIVATED", 1.0f, 0.2f, 0.2f );
			}
			return;
		}

		/* ========== Mission auto-start (works with devmap too) ========== */
		if ( ls.runMode == LS_MODE_MISSION && !ls.active && !LS_RaceBlocksTimerStart() ) {
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
				/* Keep the freeze until playerstart clears ls_loading.  CA_ACTIVE
				   can be reached before the briefing arrow is clicked. */
				lsext_recvLen = 0; /* flush stale recv data */
				LS_ExtSend( "initgametime" );
				LS_ExtSend( "starttimer" );
				LS_ExtSend( "pausegametime" );
				lsext_timerStarted = qtrue;
				lsext_lastPollMs = Sys_Milliseconds() + LSEXT_POLL_GRACE;

				/* Immediate validation on Mission run start */
				ls.settingsCheckInterval = 0;
				LS_QuickCheckSettings();
				if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) {
					ls.cheatsUsed = qtrue;
					LS_TriggerAlert( "CHEATS ACTIVATED", 1.0f, 0.2f, 0.2f );
				}
				return;
			}
		}

		/* ========== Full Game deferred start ========== */
		/* Don't start the run now - just set a pending flag.
		   The actual run init happens when cls.state >= CA_ACTIVE
		   (the cutscene is playing) so loading time is never counted. */
		if ( ls.runMode == LS_MODE_FULLGAME && newIdx == 0 && !LS_RaceBlocksTimerStart() ) {
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

		if ( !isSpTransition ) {
			/* Non-spTransition during active run (devmap, console map,
			   chapter select).  Do not clear ls_loading for normal maps here:
			   if the briefing screen appears, the client may already be
			   CA_ACTIVE before the continue arrow is clicked.  Cutscenes have no
			   briefing/playerstart, so allow their freeze to auto-clear. */
			if ( ls.active && !ls.runFinished && newIdx >= 0 && ls.splits[newIdx].cutscene ) {
				Cvar_Set( "ls_loading", "0" );
			}
			goto afterMapChange;
		}

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
						   liveSecretsFound is per-map (resets each map load),
						   so no base subtraction needed. */
						if ( ls_100pctCvar && ls_100pctCvar->integer ) {
							int sf = ls.prevLiveSecretsFound;
							int tf = ls.prevLiveTreasureFound;
							ls.splits[prevIdx].secretsFound  = ( sf > 0 ) ? sf : 0;
							ls.splits[prevIdx].treasureFound = ( tf > 0 ) ? tf : 0;
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

			/* Write updated LS state to demo now that currentMapIndex
			   has advanced.  The earlier CL_ParseGamestate injection
			   had the OLD index; this mid-stream update gives the demo
			   the correct current-map and segment-time snapshot. */
			LS_DemoWriteUpdate();
		}
	}

afterMapChange:
	/* Update actualMapname AFTER map-change detection so the
	   realMapChange comparison above sees the old value. */
	if ( !preserveActualMapname ) {
		Q_strncpyz( ls.actualMapname, currentMap, LS_MAX_MAPNAME );
	}

	ls.lastServerTime = cl.serverTime;

	/* Ghost system: record + set playback cvars */
	LS_GhostFrame();

	/* --- Update ASL shared state for external LiveSplit --- */
	{
		int ai, doneN = 0, totalN = 0;
		qboolean aslMapMismatch = qfalse;
		if ( ls.active && !ls.runFinished && ls.actualMapname[0] ) {
			char aslMap[LS_MAX_MAPNAME];
			LS_ExtractMapname( cl.mapname, aslMap, sizeof( aslMap ) );
			if ( aslMap[0] && Q_stricmp( aslMap, ls.actualMapname ) ) {
				aslMapMismatch = qtrue;
			}
		}
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
									  || ls.mapLoadFreeze
									  || aslMapMismatch ) ? 1 : 0;
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

	/* opacity: use cvar value, dim by ls_opacity_ui factor when normal console/UI is open.
	   Keep full opacity under the Speedrun ImGui so settings changes can be previewed live. */
	if ( ( cls.keyCatchers & ( KEYCATCH_CONSOLE | KEYCATCH_UI ) ) && !CL_SpeedrunImGui_IsOpen() ) {
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

/* =====================================================================
   Column layout  (shared between column headers and split rows)
   ===================================================================== */

/* Cached column positions recomputed each frame by LS_UpdateLayout */
static struct {
	int timeRight;    /* right edge of time column (panel right - 4) */
	int timeColW;     /* width of the time column */
	int deltaColW;    /* width of a single delta column (+/- or Best +/-) */
	int pbDeltaR;     /* right edge of PB delta column */
	int goldDeltaR;   /* right edge of gold delta column */
} ls_cols;

/* Recompute column layout.  Must be called after LS_UpdateLayout()
   and after the panel x position is known. */
static void LS_ComputeColumns( float panelX ) {
	qboolean showDeltas     = ls_showdeltasCvar     ? ls_showdeltasCvar->integer     : 1;
	qboolean showBestDeltas = ls_showbestdeltasCvar ? ls_showbestdeltasCvar->integer : 1;

	ls_cols.timeRight = (int)( panelX + LS_PANEL_W - 4 );
	ls_cols.timeColW  = (int)( 8 * LS_CHAR_SZ + 4 );
	ls_cols.deltaColW = (int)( 7 * LS_SMALL_SZ + 4 );

	/* Cap columns to prevent overflow at high scale or narrow width */
	if ( ls_cols.timeColW > (int)( LS_PANEL_W * 0.40f ) )
		ls_cols.timeColW = (int)( LS_PANEL_W * 0.40f );
	if ( ls_cols.deltaColW > (int)( LS_PANEL_W * 0.22f ) )
		ls_cols.deltaColW = (int)( LS_PANEL_W * 0.22f );

	ls_cols.pbDeltaR   = ls_cols.timeRight - ls_cols.timeColW;
	ls_cols.goldDeltaR = showDeltas
		? ls_cols.pbDeltaR - ls_cols.deltaColW
		: ls_cols.pbDeltaR;
}

/* =====================================================================
   Multi-color string helper
   ===================================================================== */

/*
LS_DrawStringMulti  –  draw multiple text segments on one line, each
with its own color.  Segments are drawn left-to-right starting at (x,y).
Returns the final x position (useful for chaining).

Usage:
    LS_DrawStringMulti( x, y, sz, 3,
        "PB:", labelClr,  "  ", labelClr,  "12:34.56", valClr );

The TAB character '\t' inside a segment string advances x to the next
multiple of (4 * charSize) relative to the starting x, providing simple
column-alignment.
*/
static int LS_DrawStringMulti( int startX, int yy, float charSize,
							   int numSegments, ... ) {
	va_list ap;
	int cx = startX;
	int seg;

	va_start( ap, numSegments );
	for ( seg = 0; seg < numSegments; seg++ ) {
		const char *text  = va_arg( ap, const char * );
		float      *color = va_arg( ap, float * );
		const char *p;
		int tabStop = (int)( 4 * charSize );
		if ( tabStop < 1 ) tabStop = 1;

		for ( p = text; *p; p++ ) {
			if ( *p == '\t' ) {
				/* advance to next tab stop relative to startX */
				int rel = cx - startX;
				cx = startX + ( ( rel / tabStop ) + 1 ) * tabStop;
			} else {
				char tmp[2] = { *p, '\0' };
				LS_DrawString( cx, yy, charSize, tmp, color );
				cx += (int)charSize;
			}
		}
	}
	va_end( ap );
	return cx;
}

/* LS_DrawStringMultiR  –  right-aligned version: measures total width
   first, then draws ending at rightEdge. */
static void LS_DrawStringMultiR( int rightEdge, int yy, float charSize,
								 int numSegments, ... ) {
	va_list ap;
	int totalW = 0, seg;
	const char *texts[8];
	float *colors[8];

	if ( numSegments > 8 ) numSegments = 8;

	/* First pass: measure total width */
	va_start( ap, numSegments );
	for ( seg = 0; seg < numSegments; seg++ ) {
		texts[seg]  = va_arg( ap, const char * );
		colors[seg] = va_arg( ap, float * );
		totalW += (int)strlen( texts[seg] ) * (int)charSize;
	}
	va_end( ap );

	/* Second pass: draw from computed startX */
	{
		int cx = rightEdge - totalW;
		for ( seg = 0; seg < numSegments; seg++ ) {
			LS_DrawString( cx, yy, charSize, texts[seg], colors[seg] );
			cx += (int)strlen( texts[seg] ) * (int)charSize;
		}
	}
}

static void LS_DrawStringR( int rightEdge, int y, float charSize, const char *str, float *color ) {
	int w = (int)strlen( str ) * (int)charSize;
	LS_DrawString( rightEdge - w, y, charSize, str, color );
}

/* Draw "Label: Value" pair with tight spacing.
   colAlign 0 = right-aligned to rightE, 1 = left-aligned from leftE.
   Label and value get separate colors; a 1-char gap separates them. */
static void LS_DrawLabelValue( int leftE, int rightE, int colAl, int yy,
							   const char *label, float *labelClr,
							   const char *value, float *valueClr ) {
	int labelLen = (int)strlen( label );
	int valueLen = (int)strlen( value );
	int totalW   = ( labelLen + 1 + valueLen ) * (int)LS_SMALL_SZ;  /* label + space + value */

	if ( colAl == 1 ) {
		/* left-aligned */
		int cx = leftE;
		LS_DrawString( cx, yy, LS_SMALL_SZ, label, labelClr );
		cx += ( labelLen + 1 ) * (int)LS_SMALL_SZ;
		LS_DrawString( cx, yy, LS_SMALL_SZ, value, valueClr );
	} else {
		/* right-aligned */
		int cx = rightE - totalW;
		LS_DrawString( cx, yy, LS_SMALL_SZ, label, labelClr );
		cx += ( labelLen + 1 ) * (int)LS_SMALL_SZ;
		LS_DrawString( cx, yy, LS_SMALL_SZ, value, valueClr );
	}
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
	int timeRight  = ls_cols.timeRight;
	int timeColW   = ls_cols.timeColW;
	int deltaColW  = ls_cols.deltaColW;
	int pbDeltaR   = ls_cols.pbDeltaR;
	int goldDeltaR = ls_cols.goldDeltaR;
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
		float nameMaxPx = LS_PANEL_W - (float)timeColW - 8; /* base: time column */
		if ( ls_showdeltasCvar && ls_showdeltasCvar->integer ) nameMaxPx -= (float)deltaColW;
		if ( ls_showbestdeltasCvar && ls_showbestdeltasCvar->integer ) nameMaxPx -= (float)deltaColW;
		const char *dispName = LS_SplitDisplayName( splitIdx, nameMaxPx, LS_CHAR_SZ );
		LS_DrawString( (int)( x + 3 ), (int)( y + 1 ), LS_CHAR_SZ, dispName, nameClr );
	}

	cumTime = LS_CumulativeTime( splitIdx );
	cumBest = LS_CumulativeBest( splitIdx );
	cumPB   = LS_ComparisonCumulative( splitIdx );

	/* In IL, before the attempt starts, the highlighted row is only a
	   selection/current-map marker. Do not fill the Time column with PB/best
	   comparison data, because it looks like the live run has already started. */
	if ( ls.runMode == LS_MODE_IL && !ls.active && !ls.runFinished ) {
		if ( isCur ) {
			LS_FormatTime( 0, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, currentMapC );
		}
		return;
	}

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

	/* Comparison delta column (cumulative actual vs comparison target) */
	if ( ( ls_showdeltasCvar && ls_showdeltasCvar->integer ) &&
		 !ls.splits[splitIdx].splitSkipped &&
		 cumPB > 0 && ( isDone || isCur ) ) {
		qboolean showPB = qfalse;
		if ( isDone ) {
			showPB = qtrue;
		} else if ( isCur ) {
			int cmpSeg = LS_ComparisonSegment( splitIdx );
			int segTime = ls.splits[splitIdx].currentTimeMs;
			if ( cmpSeg > 0 && segTime >= cmpSeg - 10000 ) {
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
		int cumCmpCur = LS_ComparisonCumulative( splitIdx );
		if ( cumCmpCur > 0 ) {
			LS_FormatTime( cumCmpCur, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, currentMapC );
		} else if ( cumBest > 0 ) {
			LS_FormatTime( cumBest, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, currentMapC );
		}
	} else {
		/* Future splits: show comparison cumulative if available, else best */
		int cumCmp = LS_ComparisonCumulative( splitIdx );
		if ( cumCmp > 0 ) {
			LS_FormatTime( cumCmp, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, timeDim );
		} else if ( cumBest > 0 ) {
			LS_FormatTime( cumBest, timeBuf, sizeof( timeBuf ) );
			LS_DrawStringR( timeRight, (int)( y + 1 ), LS_CHAR_SZ, timeBuf, timeDim );
		}
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
	int leftEdge, rightEdge;

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

	/* Compute shared column layout for split rows and headers */
	LS_ComputeColumns( x );

	/* Content positioning helpers for alignment mode */
	leftEdge  = (int)( x + 3 );
	if ( colAlign == 1 ) {
		rightEdge = (int)( x + LS_PANEL_W - 3 );
	} else {
		rightEdge = (int)( x + LS_PANEL_W - 4 );
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
		if ( skill < 1 || skill > 3 ) skill = 2;
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
				const char *opName = ls_missionGroups[ls.runMission - 1].name;
				Com_sprintf( headerBuf, sizeof( headerBuf ), "%s%s", opName, LS_HL1ModeNameSuffix() );
			} else {
				Com_sprintf( headerBuf, sizeof( headerBuf ), "%s%s", skillName, LS_HL1ModeNameSuffix() );
			}
			break;
		case LS_MODE_IL:
			Com_sprintf( headerBuf, sizeof( headerBuf ), "Individual Level%s", LS_HL1ModeNameSuffix() );
			break;
		default:
			Com_sprintf( headerBuf, sizeof( headerBuf ), "Full Game%s", LS_HL1ModeNameSuffix() );
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
			/* Reserve space for difficulty tag [IADI] + optional [100%] tag */
			const char *diffTag = ls_diffShortTags[LS_DiffIdx( skill )];
			int diffTagW = (int)( ( strlen( diffTag ) + 2 ) * LS_SMALL_SZ + LS_CHAR_SZ * 0.6f ); /* [TAG] + gap */
			int pctTagW  = ( ls_100pctCvar && ls_100pctCvar->integer )
				? (int)( 6 * LS_SMALL_SZ + LS_SMALL_SZ * 0.4f ) : 0;  /* [100%] + gap */
			int dotOffset = (int)( LS_CHAR_SZ * 1.4f );
			int headerX = (int)( x + 3 ) + dotOffset;
			int counterW = (int)strlen( counterBuf ) * (int)LS_CHAR_SZ + 6;
			int availW = (int)( x + LS_PANEL_W - 3 ) - headerX - counterW - diffTagW - pctTagW;
			int headerW = (int)strlen( headerBuf ) * (int)LS_CHAR_SZ;

			if ( headerW > availW && ls.runMode == LS_MODE_MISSION ) {
				/* Try short name from mission group table */
				const char *shortOp = "Mission";
				if ( ls.runMission >= 1 && ls.runMission <= LS_NUM_MISSION_GROUPS )
					shortOp = ls_missionGroups[ls.runMission - 1].shortName;
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

			/* [DHM]/[BEO]/[IADI] difficulty tag in header */
			{
				const char *diffTag = ls_diffShortTags[LS_DiffIdx( skill )];
				char diffBuf[12];
				int tagX = headerX + (int)strlen( headerBuf ) * (int)LS_CHAR_SZ + (int)( LS_CHAR_SZ * 0.6f );
				Com_sprintf( diffBuf, sizeof( diffBuf ), "[%s]", diffTag );
				LS_DrawString( tagX, (int)( y + 4 ), LS_SMALL_SZ, diffBuf, headerColor );

				/* [100%] tag after difficulty tag when 100% category is enabled */
				if ( ls_100pctCvar && ls_100pctCvar->integer ) {
					vec4_t pctTagClr = { 0.25f, 0.85f, 0.25f, 0.80f };
					int pctX = tagX + (int)strlen( diffBuf ) * (int)LS_SMALL_SZ + (int)( LS_SMALL_SZ * 0.4f );
					LS_DrawString( pctX, (int)( y + 4 ), LS_SMALL_SZ, "[100%]", pctTagClr );
				}
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

	/* Column header labels - use shared column positions (ls_cols) */
	if ( showHeader ) {
		int hdrTextY  = (int)( y + 1 );
		if ( showBestDeltas ) {
			LS_DrawStringR( ls_cols.goldDeltaR, hdrTextY, LS_SMALL_SZ, "Best +/-", labelColor );
		}
		if ( showDeltas ) {
			LS_DrawStringR( ls_cols.pbDeltaR,   hdrTextY, LS_SMALL_SZ, "+/-", labelColor );
		}
		LS_DrawStringR( ls_cols.timeRight, hdrTextY, LS_SMALL_SZ, "Time", labelColor );
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
		int fallbackIdx = ( activeIdx >= 0 ) ? activeIdx : LS_NextRealSplit( ls.modeFirstIdx - 1 );

		/* Big timer */
		if ( showTimer ) {
			int timerMs;
			if ( ls_timingCvar && ls_timingCvar->integer == LS_TIMING_REALTIME ) {
				timerMs = ls.runFinished ? ls.runSavedRealMs :
						  ( ls.active ? ( Sys_Milliseconds() - ls.runStartRealMs ) : 0 );
			} else if ( !ls.active && !ls.runFinished ) {
				timerMs = 0;
			} else {
				timerMs = LS_CumulativeTime( ls.modeLastIdx );
			}
			LS_FormatTime( timerMs, timeBuf, sizeof( timeBuf ) );
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
			int cmpSeg = ( fallbackIdx >= 0 ) ? LS_ComparisonSegment( fallbackIdx ) : 0;
			const char *cmpLabel = "PB:";
			const char *valStr;
			float *valClr;
			if ( ls_compareCvar ) {
				switch ( ls_compareCvar->integer ) {
				case LS_COMPARE_BEST:    cmpLabel = "Best:"; break;
				case LS_COMPARE_AVERAGE: cmpLabel = "Avg:"; break;
				}
			}
			if ( cmpSeg > 0 ) {
				LS_FormatTime( cmpSeg, timeBuf, sizeof( timeBuf ) );
				valStr = timeBuf; valClr = timeWhite;
			} else {
				valStr = "-----"; valClr = noDataColor;
			}
			LS_DrawLabelValue( leftEdge, rightEdge, colAlign, (int)( y + 1 ), cmpLabel, labelColor, valStr, valClr );
			y += 6 * LS_SCALE;
		}

		/* Best label row */
		if ( showBest ) {
			const char *valStr;
			float *valClr;
			if ( fallbackIdx >= 0 && ls.splits[fallbackIdx].d[di].bestTimeMs > 0 ) {
				LS_FormatTime( ls.splits[fallbackIdx].d[di].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				valStr = timeBuf; valClr = goldColor;
			} else {
				valStr = "-----"; valClr = noDataColor;
			}
			LS_DrawLabelValue( leftEdge, rightEdge, colAlign, (int)( y + 1 ), "Best:", labelColor, valStr, valClr );
			y += 6 * LS_SCALE;
		}
	}

	} else {
	/* ======== Non-IL: flow layout – each section advances y ======== */
	{
		int activeIdx = LS_ActiveRealSplit();
		int fallbackIdx = ( activeIdx >= 0 ) ? activeIdx : LS_NextRealSplit( ls.modeFirstIdx - 1 );

		/* --- Big timer --- */
		if ( showTimer ) {
			int timerMs;
			if ( ls_timingCvar && ls_timingCvar->integer == LS_TIMING_REALTIME ) {
				timerMs = ls.runFinished ? ls.runSavedRealMs :
						  ( ls.active ? ( Sys_Milliseconds() - ls.runStartRealMs ) : 0 );
			} else if ( !ls.active && !ls.runFinished ) {
				timerMs = 0;
			} else {
				timerMs = LS_CumulativeTime( ls.modeLastIdx );
			}
			LS_FormatTime( timerMs, timeBuf, sizeof( timeBuf ) );
			if ( colAlign == 1 ) {
				LS_DrawStringR( rightEdge, (int)( y + 1 ), LS_BIG_SZ, timeBuf,
					ls.runFinished ? finishedC : timerColor );
			} else {
				LS_DrawString( leftEdge, (int)( y + 1 ), LS_BIG_SZ, timeBuf,
					ls.runFinished ? finishedC : timerColor );
			}
			y += 8 * LS_SCALE;
		}

		/* --- Comparison label row --- */
		if ( showPb ) {
			int cmpSeg = ( fallbackIdx >= 0 ) ? LS_ComparisonSegment( fallbackIdx ) : 0;
			const char *cmpLabel = "PB:";
			const char *valStr;
			float *valClr;
			if ( ls_compareCvar ) {
				switch ( ls_compareCvar->integer ) {
				case LS_COMPARE_BEST:    cmpLabel = "Best:"; break;
				case LS_COMPARE_AVERAGE: cmpLabel = "Avg:"; break;
				}
			}
			if ( cmpSeg > 0 ) {
				LS_FormatTime( cmpSeg, timeBuf, sizeof( timeBuf ) );
				valStr = timeBuf; valClr = timeWhite;
			} else {
				valStr = "-----"; valClr = noDataColor;
			}
			LS_DrawLabelValue( leftEdge, rightEdge, colAlign, (int)( y + 1 ), cmpLabel, labelColor, valStr, valClr );
			y += 5 * LS_SCALE;
		}

		/* --- Best label row (only in big-timer area when segment timer is hidden) --- */
		if ( showBest && !showSeg ) {
			const char *valStr;
			float *valClr;
			if ( fallbackIdx >= 0 && ls.splits[fallbackIdx].d[di].bestTimeMs > 0 ) {
				LS_FormatTime( ls.splits[fallbackIdx].d[di].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				valStr = timeBuf; valClr = goldColor;
			} else {
				valStr = "-----"; valClr = noDataColor;
			}
			LS_DrawLabelValue( leftEdge, rightEdge, colAlign, (int)( y + 1 ), "Best:", labelColor, valStr, valClr );
			y += 5 * LS_SCALE;
		}
	}

	/* ================ Segment timer + Best ================ */
	if ( showSeg ) {
		int segTime = 0;
		int activeIdx = LS_ActiveRealSplit();
		int segFallback = ( activeIdx >= 0 ) ? activeIdx : LS_NextRealSplit( ls.modeFirstIdx - 1 );
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
			const char *valStr;
			float *valClr;
			if ( segFallback >= 0 && ls.splits[segFallback].d[di].bestTimeMs > 0 ) {
				LS_FormatTime( ls.splits[segFallback].d[di].bestTimeMs, timeBuf, sizeof( timeBuf ) );
				valStr = timeBuf; valClr = goldColor;
			} else {
				valStr = "-----"; valClr = noDataColor;
			}
			LS_DrawLabelValue( leftEdge, rightEdge, colAlign, (int)( y + 1 ), "Best:", labelColor, valStr, valClr );
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
				LS_FormatTime( save, timeBuf, sizeof( timeBuf ) );
				valW = (int)strlen( timeBuf ) * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, timeBuf, timeWhite );
			} else {
				valW = 4 * (int)LS_SMALL_SZ;
				valX = (int)( x + LS_PANEL_W - 4 ) - valW;
				LS_DrawString( valX, (int)( y + 1 ), LS_SMALL_SZ, "0.00", timeWhite );
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
			   liveSecretsFound is already per-map (resets each map
			   load), so no base subtraction needed.
			   Totals come from the hardcoded map table.
			   0/0 = green (no collectibles on this map). */
			int segSecF  = ls.liveSecretsFound;
			int segSecT  = ls.liveSecretsTotal;
			int segTresF = ls.liveTreasureFound;
			int segTresT = ls.liveTreasureTotal;
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
		int now = Sys_Milliseconds();
		int elapsed = ls_resetPendingStartMs ? now - ls_resetPendingStartMs : 999;
		float intro = elapsed < 180 ? (float)elapsed / 180.0f : 1.0f;
		float pulse = ( ( now / 350 ) & 1 ) ? 1.0f : 0.84f;
		float popW = 348.0f, popH = 132.0f;
		float popX = ( 640.0f - popW ) * 0.5f;
		float popY = ( 480.0f - popH ) * 0.5f - 32.0f + ( 1.0f - intro ) * 10.0f;
		float charSz = 6.0f;
		const char *titleStr = "SAVE RUN PROGRESS?";
		const char *statusStr = "UNSAVED RUN";
		const char *detailStr = "Save run history before reset.";
		char statusBuf[80];
		char detailBuf[96];
		char timeBuf[32];
		char saveBuf[48];
		char discardBuf[64];
		int titleLen;
		int finalTime = ls.runFinished ? ls.runTotalIGTMs : LS_CumulativeTime( ls.modeLastIdx );

		vec4_t dimClr       = { 0.0f, 0.0f, 0.0f, 0.46f * intro };
		vec4_t shadowClr    = { 0.0f, 0.0f, 0.0f, 0.48f * intro };
		vec4_t cardClr      = { 0.035f, 0.040f, 0.050f, 0.94f * intro };
		vec4_t card2Clr     = { 0.085f, 0.095f, 0.115f, 0.84f * intro };
		vec4_t goldClr      = { 1.0f, 0.74f, 0.16f, 1.0f * intro };
		vec4_t goldSoftClr  = { 1.0f, 0.74f, 0.16f, 0.22f * intro * pulse };
		vec4_t titleClr     = { 1.0f, 0.92f, 0.62f, 1.0f * intro };
		vec4_t textClr      = { 0.84f, 0.86f, 0.82f, 0.95f * intro };
		vec4_t dimTextClr   = { 0.58f, 0.62f, 0.62f, 0.88f * intro };
		vec4_t saveBg       = { 0.08f, 0.34f, 0.13f, ( curY ? 0.86f : 0.62f ) * intro };
		vec4_t discardBg    = { 0.42f, 0.09f, 0.08f, ( curN ? 0.86f : 0.62f ) * intro };
		vec4_t cancelBg     = { 0.22f, 0.22f, 0.24f, ( curEsc ? 0.80f : 0.54f ) * intro };
		vec4_t btnBorder    = { 1.0f, 1.0f, 1.0f, 0.10f * intro };
		vec4_t saveTxt      = { 0.68f, 1.0f, 0.64f, 1.0f * intro };
		vec4_t discardTxt   = { 1.0f, 0.58f, 0.50f, 1.0f * intro };
		vec4_t cancelTxt    = { 0.82f, 0.84f, 0.84f, 1.0f * intro };

		if ( ls_resetPromptKind == LS_RESET_PROMPT_PB || ( ls.runFinished && LS_HasNewPB() ) ) {
			int gc = ls_resetPromptGoldCount > 0 ? ls_resetPromptGoldCount : LS_CountNewGolds();
			statusStr = "NEW PERSONAL BEST";
			if ( gc > 0 ) {
				Com_sprintf( detailBuf, sizeof( detailBuf ), "PB + %d new gold%s are ready to save.", gc, gc > 1 ? "s" : "" );
			} else {
				Q_strncpyz( detailBuf, "Your finished run is faster than the saved PB.", sizeof( detailBuf ) );
			}
			detailStr = detailBuf;
		} else if ( ls_resetPromptKind == LS_RESET_PROMPT_GOLDS || LS_HasNewGolds() ) {
			int gc = ls_resetPromptGoldCount > 0 ? ls_resetPromptGoldCount : LS_CountNewGolds();
			Com_sprintf( statusBuf, sizeof( statusBuf ), "%d NEW GOLD%s", gc, gc == 1 ? "" : "S" );
			statusStr = statusBuf;
			Q_strncpyz( detailBuf, "Keep these best segment times before resetting.", sizeof( detailBuf ) );
			detailStr = detailBuf;
		}

		LS_FormatTime( finalTime, timeBuf, sizeof( timeBuf ) );
		Com_sprintf( saveBuf, sizeof( saveBuf ), "[Y]  SAVE" );
		Com_sprintf( discardBuf, sizeof( discardBuf ), "[N]  DISCARD" );
		titleLen = (int)strlen( titleStr );

		/* Dim the game view and draw a soft drop shadow. */
		SCR_FillRect( 0, 0, 640, 480, dimClr );
		SCR_FillRect( popX + 5, popY + 6, popW, popH, shadowClr );

		/* Card body + accent border. */
		SCR_FillRect( popX - 1, popY - 1, popW + 2, popH + 2, goldSoftClr );
		SCR_FillRect( popX, popY, popW, popH, cardClr );
		SCR_FillRect( popX, popY, popW, 3, goldClr );
		SCR_FillRect( popX + 10, popY + 34, popW - 20, 1, goldSoftClr );

		/* Header. */
		SCR_DrawStringExt( (int)( popX + popW * 0.5f - titleLen * charSz * 0.5f ),
			(int)( popY + 11 ), (int)charSz, titleStr, titleClr, qtrue );

		/* Status pill. */
		SCR_FillRect( popX + 18, popY + 43, popW - 36, 22, card2Clr );
		SCR_FillRect( popX + 18, popY + 43, 3, 22, goldClr );
		SCR_DrawStringExt( (int)( popX + 29 ), (int)( popY + 47 ), 6, statusStr, goldClr, qtrue );
		SCR_DrawStringExt( (int)( popX + popW - 18 - (int)strlen( timeBuf ) * 6 ),
			(int)( popY + 47 ), 6, timeBuf, textClr, qtrue );

		/* Detail text. */
		SCR_DrawStringExt( (int)( popX + 22 ), (int)( popY + 72 ), 5, detailStr, textClr, qtrue );
		SCR_DrawStringExt( (int)( popX + 22 ), (int)( popY + 84 ), 5,
			"Save keeps PB/golds + history. Discard reverts this attempt.", dimTextClr, qtrue );

		/* Action buttons. */
		SCR_FillRect( popX + 18, popY + 104, 92, 18, btnBorder );
		SCR_FillRect( popX + 19, popY + 105, 90, 16, saveBg );
		SCR_DrawStringExt( (int)( popX + 37 ), (int)( popY + 109 ), 5, saveBuf, saveTxt, qtrue );

		SCR_FillRect( popX + 128, popY + 104, 92, 18, btnBorder );
		SCR_FillRect( popX + 129, popY + 105, 90, 16, discardBg );
		SCR_DrawStringExt( (int)( popX + 143 ), (int)( popY + 109 ), 5, discardBuf, discardTxt, qtrue );

		SCR_FillRect( popX + 238, popY + 104, 92, 18, btnBorder );
		SCR_FillRect( popX + 239, popY + 105, 90, 16, cancelBg );
		SCR_DrawStringExt( (int)( popX + 252 ), (int)( popY + 109 ), 5, "[ESC] CANCEL", cancelTxt, qtrue );
	}
}

/* =====================================================================
   Standalone IGT timer overlay
   ===================================================================== */

static void LS_DrawIGTTimer( void ) {
	float timerX, timerY, timerScale, charSz, drawX, segX;
	int   align, timeW;
	int   igtMs;
	char  timeBuf[32];
	vec4_t timerColor = { 1.0f, 1.0f, 1.0f, 0.95f };
	vec4_t shadowColor = { 0.0f, 0.0f, 0.0f, 0.65f };

	if ( !ls_igttimerCvar || !ls_igttimerCvar->integer ) {
		/* In external mode, always show IGT timer when a run is active
		   (the panel is hidden, so this is the only on-screen time). */
		if ( !( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL &&
				ls.active && !ls.runFinished ) ) {
			return;
		}
	}

	timerX     = ls_igttimer_xCvar ? ls_igttimer_xCvar->value : 638.0f;
	timerY     = ls_igttimer_yCvar ? ls_igttimer_yCvar->value : 240.0f;
	timerScale = ls_igttimer_scaleCvar ? ls_igttimer_scaleCvar->value : 1.0f;
	align      = ls_igttimer_alignCvar ? ls_igttimer_alignCvar->integer : 2;

	if ( timerScale < 0.3f ) timerScale = 0.3f;
	if ( timerScale > 4.0f ) timerScale = 4.0f;
	if ( align < 0 ) align = 0;
	if ( align > 2 ) align = 2;

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
	timeW = (int)strlen( timeBuf ) * (int)charSz;
	drawX = timerX;
	if ( align == 1 ) {
		drawX -= timeW * 0.5f;
	} else if ( align == 2 ) {
		drawX -= timeW;
	}

	/* Shadow */
	SCR_DrawStringExt( (int)( drawX + 1 ), (int)( timerY + 1 ), (int)charSz, timeBuf, shadowColor, qtrue );
	/* Timer text */
	SCR_DrawStringExt( (int)drawX, (int)timerY, (int)charSz, timeBuf, timerColor, qtrue );

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

		segX = timerX;
		if ( align == 1 ) {
			segX -= (float)( (int)strlen( segBuf ) * (int)segSz ) * 0.5f;
		} else if ( align == 2 ) {
			segX -= (float)( (int)strlen( segBuf ) * (int)segSz );
		}
		SCR_DrawStringExt( (int)( segX + 1 ), (int)( segY + 1 ), (int)segSz, segBuf, segShadow, qtrue );
		SCR_DrawStringExt( (int)segX, (int)segY, (int)segSz, segBuf, segColor, qtrue );
	}
}

/*
===================
LS_DrawCenterAlert

Draws the centered alert with bounce-in + fade-out animation.
Total duration: ~2.5s (0.3s bounce in, 1.5s hold, 0.7s fade out)
===================
*/
static void LS_DrawCenterAlert( void ) {
	int now, elapsed;
	float alpha, scale, yOff, pulse;
	float t;
	int titleLen, detailLen;
	float cardW, cardH, cardX, cardY;
	const char *titleStr;
	const char *detailStr;
	vec4_t dimClr, shadowClr, cardClr, accentClr, accentSoftClr;
	vec4_t titleClr, detailClr, chipClr, chipTxtClr;
	qboolean isCheatAlert, isCvarAlert;

	if ( !ls.alertStartMs ) return;

	now = Sys_Milliseconds();
	elapsed = now - ls.alertStartMs;

	if ( elapsed > 2500 ) {
		ls.alertStartMs = 0;
		return;
	}

	/* Phase 1: Bounce in (0-300ms) */
	if ( elapsed < 300 ) {
		t = (float)elapsed / 300.0f;
		/* Overshoot bounce: goes to 1.15 then settles to 1.0 */
		if ( t < 0.6f ) {
			scale = t / 0.6f * 1.15f;
		} else {
			scale = 1.15f - 0.15f * ( ( t - 0.6f ) / 0.4f );
		}
		alpha = t;
	}
	/* Phase 2: Hold (300-1800ms) */
	else if ( elapsed < 1800 ) {
		scale = 1.0f;
		alpha = 1.0f;
	}
	/* Phase 3: Fade out (1800-2500ms) */
	else {
		t = (float)( elapsed - 1800 ) / 700.0f;
		scale = 1.0f;
		alpha = 1.0f - t;
	}

	if ( alpha <= 0.0f ) return;

	isCheatAlert = ( strstr( ls.alertText, "CHEATS" ) != NULL ) ? qtrue : qfalse;
	isCvarAlert = ( strstr( ls.alertText, "SETTINGS" ) != NULL || strstr( ls.alertText, "CVAR" ) != NULL ) ? qtrue : qfalse;

	if ( isCheatAlert ) {
		titleStr = "SV_CHEATS DETECTED";
		detailStr = "Practice/dev mode was used - this run is marked invalid.";
	} else if ( isCvarAlert ) {
		titleStr = "MODIFIED CVARS DETECTED";
		detailStr = "Speedrun-relevant settings changed from their defaults.";
	} else {
		titleStr = ls.alertText;
		detailStr = "LiveSplit status updated.";
	}

	titleLen = (int)strlen( titleStr );
	detailLen = (int)strlen( detailStr );
	cardW = 360.0f * scale;
	cardH = 66.0f * scale;
	cardX = ( 640.0f - cardW ) * 0.5f;

	/* Vertical position: slight slide up during bounce */
	yOff = ( elapsed < 300 ) ? ( 1.0f - (float)elapsed / 300.0f ) * 8.0f : 0.0f;
	cardY = 86.0f + yOff;
	pulse = ( ( now / 300 ) & 1 ) ? 1.0f : 0.82f;

	dimClr[0] = 0.0f; dimClr[1] = 0.0f; dimClr[2] = 0.0f; dimClr[3] = 0.16f * alpha;
	shadowClr[0] = 0.0f; shadowClr[1] = 0.0f; shadowClr[2] = 0.0f; shadowClr[3] = 0.42f * alpha;
	cardClr[0] = 0.035f; cardClr[1] = 0.040f; cardClr[2] = 0.050f; cardClr[3] = 0.92f * alpha;
	accentClr[0] = ls.alertColor[0]; accentClr[1] = ls.alertColor[1]; accentClr[2] = ls.alertColor[2]; accentClr[3] = 0.95f * alpha;
	accentSoftClr[0] = ls.alertColor[0]; accentSoftClr[1] = ls.alertColor[1]; accentSoftClr[2] = ls.alertColor[2]; accentSoftClr[3] = 0.20f * alpha * pulse;
	titleClr[0] = ls.alertColor[0]; titleClr[1] = ls.alertColor[1]; titleClr[2] = ls.alertColor[2]; titleClr[3] = 1.0f * alpha;
	detailClr[0] = 0.82f; detailClr[1] = 0.84f; detailClr[2] = 0.82f; detailClr[3] = 0.92f * alpha;
	chipClr[0] = isCheatAlert ? 0.44f : 0.34f; chipClr[1] = isCheatAlert ? 0.07f : 0.27f; chipClr[2] = isCheatAlert ? 0.06f : 0.04f; chipClr[3] = 0.62f * alpha;
	chipTxtClr[0] = 1.0f; chipTxtClr[1] = isCheatAlert ? 0.58f : 0.84f; chipTxtClr[2] = isCheatAlert ? 0.50f : 0.24f; chipTxtClr[3] = 1.0f * alpha;

	SCR_FillRect( 0, 0, 640, 480, dimClr );
	SCR_FillRect( cardX + 5, cardY + 6, cardW, cardH, shadowClr );
	SCR_FillRect( cardX - 1, cardY - 1, cardW + 2, cardH + 2, accentSoftClr );
	SCR_FillRect( cardX, cardY, cardW, cardH, cardClr );
	SCR_FillRect( cardX, cardY, cardW, 3 * scale, accentClr );
	SCR_FillRect( cardX + 16 * scale, cardY + 37 * scale, cardW - 32 * scale, 1, accentSoftClr );

	/* Left status chip. */
	SCR_FillRect( cardX + 16 * scale, cardY + 16 * scale, 64 * scale, 18 * scale, chipClr );
	SCR_DrawStringExt( (int)( cardX + 29 * scale ), (int)( cardY + 20 * scale ), (int)( 5 * scale ),
		isCheatAlert ? "INVALID" : ( isCvarAlert ? "WARNING" : "NOTICE" ), chipTxtClr, qtrue );

	/* Title + detail. */
	{
		vec4_t textShadow = { 0.0f, 0.0f, 0.0f, 0.48f * alpha };
		float titleX = cardX + cardW * 0.5f - titleLen * 7.0f * scale * 0.5f;
		float detailX = cardX + cardW * 0.5f - detailLen * 5.0f * scale * 0.5f;
		SCR_DrawStringExt( (int)( titleX + 1 ), (int)( cardY + 17 * scale + 1 ), (int)( 7 * scale ), titleStr, textShadow, qtrue );
		SCR_DrawStringExt( (int)titleX, (int)( cardY + 17 * scale ), (int)( 7 * scale ), titleStr, titleClr, qtrue );
		SCR_DrawStringExt( (int)( detailX + 1 ), (int)( cardY + 43 * scale + 1 ), (int)( 5 * scale ), detailStr, textShadow, qtrue );
		SCR_DrawStringExt( (int)detailX, (int)( cardY + 43 * scale ), (int)( 5 * scale ), detailStr, detailClr, qtrue );
	}
}

/*
===================
LS_DrawVerifyOverlay

Draws the run-finish verification overlay (multi-line config check results).
===================
*/
static void LS_DrawVerifyOverlay( void ) {
	int now, elapsed;
	float alpha, t, intro, pulse;
	float boxW, boxH, boxX, boxY;
	char titleBuf[64], subtitleBuf[96], statsBuf[96], cvarBuf[64];
	const char *statusLabel;
	vec4_t dimClr, shadowClr, cardClr, card2Clr, accentClr, accentSoftClr;
	vec4_t titleClr, subtitleClr, textClr, dimTextClr;
	vec4_t passBg, warnBg, failBg, neutralBg;
	vec4_t passClr, warnClr, failClr, neutralClr;

	if ( !ls.verifyStartMs ) return;

	now = Sys_Milliseconds();
	elapsed = now - ls.verifyStartMs;

	/* Total 5000ms: 400ms fade-in, 3600ms hold, 1000ms fade-out */
	if ( elapsed > 5000 ) {
		ls.verifyStartMs = 0;
		return;
	}

	if ( elapsed < 400 ) {
		t = (float)elapsed / 400.0f;
		alpha = t * t; /* ease-in */
	} else if ( elapsed < 4000 ) {
		alpha = 1.0f;
	} else {
		t = (float)( elapsed - 4000 ) / 1000.0f;
		alpha = 1.0f - t * t; /* ease-out */
	}

	if ( alpha <= 0.0f ) return;

	intro = elapsed < 400 ? (float)elapsed / 400.0f : 1.0f;
	pulse = ( ( now / 360 ) & 1 ) ? 1.0f : 0.84f;

	if ( ls.verifyValid ) {
		Q_strncpyz( titleBuf, "RUN VERIFIED", sizeof( titleBuf ) );
		Q_strncpyz( subtitleBuf, "Settings are clean and sv_cheats was not used.", sizeof( subtitleBuf ) );
		statusLabel = "VALID";
	} else {
		Q_strncpyz( titleBuf, "RUN NOT VERIFIED", sizeof( titleBuf ) );
		if ( ls.verifyCheats && ls.verifyModCount > 0 ) {
			Q_strncpyz( subtitleBuf, "sv_cheats and modified CVARs were detected.", sizeof( subtitleBuf ) );
		} else if ( ls.verifyCheats ) {
			Q_strncpyz( subtitleBuf, "sv_cheats was used during this run.", sizeof( subtitleBuf ) );
		} else {
			Q_strncpyz( subtitleBuf, "Speedrun-relevant CVARs were modified.", sizeof( subtitleBuf ) );
		}
		statusLabel = "INVALID";
	}

	Com_sprintf( statsBuf, sizeof( statsBuf ), "Pauses %d   Undos %d   Skips %d",
		ls.verifyPauses, ls.verifyUndos, ls.verifySkips );
	if ( ls.verifyModCount > 0 ) {
		Com_sprintf( cvarBuf, sizeof( cvarBuf ), "%d modified CVAR%s",
			ls.verifyModCount, ls.verifyModCount == 1 ? "" : "s" );
	} else {
		Q_strncpyz( cvarBuf, "CVARs clean", sizeof( cvarBuf ) );
	}

	boxW   = 360.0f;
	boxH   = 142.0f;
	boxX   = ( 640.0f - boxW ) * 0.5f;
	boxY   = 116.0f + ( 1.0f - intro ) * 10.0f;

	dimClr[0] = 0.0f; dimClr[1] = 0.0f; dimClr[2] = 0.0f; dimClr[3] = 0.28f * alpha;
	shadowClr[0] = 0.0f; shadowClr[1] = 0.0f; shadowClr[2] = 0.0f; shadowClr[3] = 0.46f * alpha;
	cardClr[0] = 0.035f; cardClr[1] = 0.040f; cardClr[2] = 0.050f; cardClr[3] = 0.94f * alpha;
	card2Clr[0] = 0.085f; card2Clr[1] = 0.095f; card2Clr[2] = 0.115f; card2Clr[3] = 0.84f * alpha;
	if ( ls.verifyValid ) {
		accentClr[0] = 0.24f; accentClr[1] = 0.92f; accentClr[2] = 0.34f;
	} else {
		accentClr[0] = 1.0f; accentClr[1] = 0.28f; accentClr[2] = 0.20f;
	}
	accentClr[3] = 0.96f * alpha;
	accentSoftClr[0] = accentClr[0]; accentSoftClr[1] = accentClr[1]; accentSoftClr[2] = accentClr[2]; accentSoftClr[3] = 0.20f * alpha * pulse;
	titleClr[0] = accentClr[0]; titleClr[1] = accentClr[1]; titleClr[2] = accentClr[2]; titleClr[3] = 1.0f * alpha;
	subtitleClr[0] = 0.86f; subtitleClr[1] = 0.88f; subtitleClr[2] = 0.84f; subtitleClr[3] = 0.95f * alpha;
	textClr[0] = 0.84f; textClr[1] = 0.86f; textClr[2] = 0.84f; textClr[3] = 0.92f * alpha;
	dimTextClr[0] = 0.58f; dimTextClr[1] = 0.62f; dimTextClr[2] = 0.62f; dimTextClr[3] = 0.86f * alpha;
	passBg[0] = 0.05f; passBg[1] = 0.28f; passBg[2] = 0.08f; passBg[3] = 0.62f * alpha;
	warnBg[0] = 0.34f; warnBg[1] = 0.25f; warnBg[2] = 0.04f; warnBg[3] = 0.62f * alpha;
	failBg[0] = 0.42f; failBg[1] = 0.07f; failBg[2] = 0.05f; failBg[3] = 0.62f * alpha;
	neutralBg[0] = 0.16f; neutralBg[1] = 0.17f; neutralBg[2] = 0.20f; neutralBg[3] = 0.58f * alpha;
	passClr[0] = 0.52f; passClr[1] = 1.0f; passClr[2] = 0.48f; passClr[3] = 1.0f * alpha;
	warnClr[0] = 1.0f; warnClr[1] = 0.84f; warnClr[2] = 0.28f; warnClr[3] = 1.0f * alpha;
	failClr[0] = 1.0f; failClr[1] = 0.50f; failClr[2] = 0.43f; failClr[3] = 1.0f * alpha;
	neutralClr[0] = 0.78f; neutralClr[1] = 0.82f; neutralClr[2] = 0.84f; neutralClr[3] = 1.0f * alpha;

	SCR_FillRect( 0, 0, 640, 480, dimClr );
	SCR_FillRect( boxX + 5, boxY + 6, boxW, boxH, shadowClr );
	SCR_FillRect( boxX - 1, boxY - 1, boxW + 2, boxH + 2, accentSoftClr );
	SCR_FillRect( boxX, boxY, boxW, boxH, cardClr );
	SCR_FillRect( boxX, boxY, boxW, 3, accentClr );
	SCR_FillRect( boxX + 14, boxY + 43, boxW - 28, 1, accentSoftClr );

	/* Status chip + title. */
	SCR_FillRect( boxX + 18, boxY + 16, 72, 18, ls.verifyValid ? passBg : failBg );
	SCR_DrawStringExt( (int)( boxX + ( ls.verifyValid ? 39 : 33 ) ), (int)( boxY + 20 ), 5,
		statusLabel, ls.verifyValid ? passClr : failClr, qtrue );
	{
		int titleLen = (int)strlen( titleBuf );
		vec4_t textShadow = { 0.0f, 0.0f, 0.0f, 0.48f * alpha };
		float titleX = boxX + boxW * 0.5f - titleLen * 7.0f * 0.5f;
		SCR_DrawStringExt( (int)( titleX + 1 ), (int)( boxY + 18 + 1 ), 7, titleBuf, textShadow, qtrue );
		SCR_DrawStringExt( (int)titleX, (int)( boxY + 18 ), 7, titleBuf, titleClr, qtrue );
	}

	/* Subtitle. */
	{
		int subLen = (int)strlen( subtitleBuf );
		float subX = boxX + boxW * 0.5f - subLen * 5.0f * 0.5f;
		SCR_DrawStringExt( (int)subX, (int)( boxY + 51 ), 5, subtitleBuf, subtitleClr, qtrue );
	}

	/* Result rows. */
	SCR_FillRect( boxX + 18, boxY + 70, 104, 22, ls.verifyCheats ? failBg : passBg );
	SCR_DrawStringExt( (int)( boxX + 27 ), (int)( boxY + 74 ), 5, "SV_CHEATS", dimTextClr, qtrue );
	SCR_DrawStringExt( (int)( boxX + 76 ), (int)( boxY + 82 ), 5,
		ls.verifyCheats ? "USED" : "CLEAN", ls.verifyCheats ? failClr : passClr, qtrue );

	SCR_FillRect( boxX + 128, boxY + 70, 214, 22, ls.verifyModCount > 0 ? warnBg : passBg );
	SCR_DrawStringExt( (int)( boxX + 137 ), (int)( boxY + 74 ), 5, "CVAR CHECK", dimTextClr, qtrue );
	SCR_DrawStringExt( (int)( boxX + 226 ), (int)( boxY + 82 ), 5,
		cvarBuf, ls.verifyModCount > 0 ? warnClr : passClr, qtrue );

	SCR_FillRect( boxX + 18, boxY + 100, 324, 24, neutralBg );
	SCR_DrawStringExt( (int)( boxX + 27 ), (int)( boxY + 105 ), 5, "RUN ACTIONS", dimTextClr, qtrue );
	SCR_DrawStringExt( (int)( boxX + 176 ), (int)( boxY + 105 ), 5, statsBuf,
		( ls.verifyPauses || ls.verifyUndos || ls.verifySkips ) ? warnClr : neutralClr, qtrue );

	SCR_DrawStringExt( (int)( boxX + 21 ), (int)( boxY + 129 ), 4,
		ls.verifyValid ? "This completion is clean for speedrun timing." : "Review these flags before submitting the run.",
		textClr, qtrue );
}

/* =====================================================================
   Main entry point
   ===================================================================== */

/*
===================
LS_GetModifiedSettingsCount

Returns the number of currently modified gameplay cvars (0 = clean).
Called from cl_scrn.c to display the indicator.
===================
*/
int LS_GetModifiedSettingsCount( void ) {
	if ( !ls.initialized ) return 0;
	return ls.settingsModCount;
}

/* =====================================================================
   Demo LiveSplit recording & playback
   ===================================================================== */

/*
===================
LS_DemoBuildState

Serialize the current LiveSplit state into an Info-string for
embedding in demos via CS_DEMO_LIVESPLIT.

Keys:
  a  = active (0/1)
  f  = finished (0/1)
  m  = mode (0=FG, 1=MS, 2=IL)
  d  = difficulty (1-3)
  ms = mission group (1-5)
  igt= total IGT so far (ms) - sum of completed splits
  sg = current segment time (ms) on the active split
  si = current split index
  st = cl.serverTime at capture
  nm = numMaps
  nv = numVisible
  ci = currentMapIndex
  mf = modeFirstIdx
  ml = modeLastIdx
  me = modeEndMapIdx
  fp = com_maxfps at capture (recording FPS)
===================
*/
void LS_DemoBuildState( char *out, int outSize ) {
	char tmp[32];

	out[0] = '\0';

	if ( !ls.initialized || !ls.active ) return;

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.active ? 1 : 0 );
	Info_SetValueForKey( out, "a", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.runFinished ? 1 : 0 );
	Info_SetValueForKey( out, "f", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ( ls.manualPause || ( cl_paused && cl_paused->integer ) || ( cls.keyCatchers & KEYCATCH_UI ) ) ? 1 : 0 );
	Info_SetValueForKey( out, "pa", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.runMode );
	Info_SetValueForKey( out, "m", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.currentDifficulty );
	Info_SetValueForKey( out, "d", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.runMission );
	Info_SetValueForKey( out, "ms", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.runTotalIGTMs );
	Info_SetValueForKey( out, "igt", tmp );

	/* Current segment time on the active (non-done) split */
	{
		int segMs = 0;
		int ci = ls.currentMapIndex;
		if ( ci >= 0 && ci < ls.numMaps && !ls.splits[ci].splitDone ) {
			if ( ls.splits[ci].cutscene ) {
				/* Cutscene: attribute time to adjacent real split */
				int ri = ( ci == ls.modeFirstIdx ) ? LS_NextRealSplit( ci ) : LS_PrevRealSplit( ci );
				if ( ri >= 0 && !ls.splits[ri].splitDone )
					segMs = ls.splits[ri].currentTimeMs;
			} else {
				segMs = ls.splits[ci].currentTimeMs;
			}
		}
		Com_sprintf( tmp, sizeof( tmp ), "%d", segMs );
		Info_SetValueForKey( out, "sg", tmp );
	}

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.currentMapIndex );
	Info_SetValueForKey( out, "si", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", cl.serverTime );
	Info_SetValueForKey( out, "st", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.numMaps );
	Info_SetValueForKey( out, "nm", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.numVisible );
	Info_SetValueForKey( out, "nv", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.currentMapIndex );
	Info_SetValueForKey( out, "ci", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.modeFirstIdx );
	Info_SetValueForKey( out, "mf", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.modeLastIdx );
	Info_SetValueForKey( out, "ml", tmp );

	Com_sprintf( tmp, sizeof( tmp ), "%d", ls.modeEndMapIdx );
	Info_SetValueForKey( out, "me", tmp );

	/* Recording FPS */
	Com_sprintf( tmp, sizeof( tmp ), "%d", Cvar_VariableIntegerValue( "com_maxfps" ) );
	Info_SetValueForKey( out, "fp", tmp );
}

/*
===================
LS_DemoBuildTimes

Serialize completed split times and PB/gold comparison data into
a compact string for CS_DEMO_LIVESPLIT_TIMES.

Format: semicolon-separated entries, one per split in the active range:
  <splitIdx>:<done>:<igtMs>:<pbMs>:<goldMs>

where:
  splitIdx = index in splits[] (global map index)
  done     = 1 if split completed, 0 if not
  igtMs    = segment IGT in milliseconds (0 if not done)
  pbMs     = PB cumulative time for comparison (0 if no PB)
  goldMs   = best segment time (gold) for this split (0 if none)

Example: "1:1:12345:11000:10500;2:1:8000:19000:7500;3:0:0:25000:5000"
===================
*/
void LS_DemoBuildTimes( char *out, int outSize ) {
	int i, di, len;
	char entry[64];

	out[0] = '\0';
	len = 0;

	if ( !ls.initialized || !ls.active ) return;

	di = LS_CurDiffIdx();

	for ( i = ls.modeFirstIdx; i <= ls.modeLastIdx && i < ls.numMaps; i++ ) {
		int done, igtMs, pbCumMs, goldMs;
		int entryLen;

		if ( ls.splits[i].cutscene ) continue;

		done   = ls.splits[i].splitDone ? 1 : 0;
		igtMs  = ls.splits[i].currentTimeMs;
		goldMs = ls.splits[i].d[di].bestTimeMs;

		/* PB cumulative: sum of PB segments up to and including this split */
		pbCumMs = LS_CumulativePB( i );

		Com_sprintf( entry, sizeof( entry ), "%d:%d:%d:%d:%d;",
			i, done, igtMs, pbCumMs, goldMs );

		entryLen = (int)strlen( entry );
		if ( len + entryLen >= outSize - 1 ) break;

		Q_strcat( out, outSize, entry );
		len += entryLen;
	}

	/* Remove trailing semicolon */
	if ( len > 0 && out[len - 1] == ';' ) {
		out[len - 1] = '\0';
	}
}

/*
===================
LS_DemoWriteUpdate

Called during demo recording when split state changes (e.g. split
completed). Writes updated CS_DEMO_LIVESPLIT and CS_DEMO_LIVESPLIT_TIMES
configstrings into the demo file mid-stream.
===================
*/
static void LS_DemoWriteUpdate( void ) {
	char lsState[MAX_INFO_STRING];
	char lsTimes[MAX_INFO_STRING];

	if ( !clc.demorecording ) return;

	LS_DemoBuildState( lsState, sizeof( lsState ) );
	LS_DemoBuildTimes( lsTimes, sizeof( lsTimes ) );

	if ( lsState[0] ) {
		CL_DemoWriteConfigstring( CS_DEMO_LIVESPLIT, lsState );
	}
	if ( lsTimes[0] ) {
		CL_DemoWriteConfigstring( CS_DEMO_LIVESPLIT_TIMES, lsTimes );
	}
}

/* =====================================================================
   Demo LiveSplit playback renderer
   ===================================================================== */

/* Parsed demo LS state (rebuilt from configstrings each frame) */
typedef struct {
	qboolean valid;
	int      active;
	int      finished;
	int      paused;
	int      mode;          /* 0=FG 1=MS 2=IL */
	int      difficulty;    /* 1-3 */
	int      mission;       /* 1-5 */
	int      totalIGTMs;    /* accumulated IGT at capture */
	int      segTimeMs;     /* current segment time at capture */
	int      captureServerTime; /* cl.serverTime when state was captured */
	int      currentMapIndex;
	int      numMaps;
	int      numVisible;
	int      modeFirstIdx;
	int      modeLastIdx;
	int      modeEndMapIdx;
	int      recordFps;     /* com_maxfps during recording */
} lsDemoState_t;

typedef struct {
	int splitIdx;
	int done;
	int igtMs;          /* segment time */
	int pbCumMs;        /* PB cumulative */
	int goldMs;         /* best segment (gold) */
} lsDemoSplit_t;

#define LS_DEMO_MAX_SPLITS 40

/*
===================
LS_DemoParseTimes

Parse the CS_DEMO_LIVESPLIT_TIMES configstring into an array
of lsDemoSplit_t entries.
Returns the number of entries parsed.
===================
*/
static int LS_DemoParseTimes( const char *str, lsDemoSplit_t *splits, int maxSplits ) {
	int count = 0;
	const char *p = str;
	char token[64];
	int  tlen;

	while ( *p && count < maxSplits ) {
		int idx, done, igt, pb, gold;

		/* Read one entry up to ';' or end */
		tlen = 0;
		while ( *p && *p != ';' ) {
			if ( tlen < (int)sizeof( token ) - 1 ) token[tlen++] = *p;
			p++;
		}
		token[tlen] = '\0';
		if ( *p == ';' ) p++;

		if ( tlen == 0 ) continue;

		/* Parse idx:done:igt:pb:gold */
		if ( sscanf( token, "%d:%d:%d:%d:%d", &idx, &done, &igt, &pb, &gold ) == 5 ) {
			splits[count].splitIdx = idx;
			splits[count].done     = done;
			splits[count].igtMs    = igt;
			splits[count].pbCumMs  = pb;
			splits[count].goldMs   = gold;
			count++;
		}
	}
	return count;
}

/*
===================
LS_DemoParseState

Parse the CS_DEMO_LIVESPLIT configstring into an lsDemoState_t.
===================
*/
static void LS_DemoParseState( const char *str, lsDemoState_t *st ) {
	memset( st, 0, sizeof( *st ) );

	if ( !str || !str[0] ) return;

	st->valid              = qtrue;
	st->active             = atoi( Info_ValueForKey( str, "a" ) );
	st->finished           = atoi( Info_ValueForKey( str, "f" ) );
	st->paused             = atoi( Info_ValueForKey( str, "pa" ) );
	st->mode               = atoi( Info_ValueForKey( str, "m" ) );
	st->difficulty         = atoi( Info_ValueForKey( str, "d" ) );
	st->mission            = atoi( Info_ValueForKey( str, "ms" ) );
	st->totalIGTMs         = atoi( Info_ValueForKey( str, "igt" ) );
	st->segTimeMs          = atoi( Info_ValueForKey( str, "sg" ) );
	st->currentMapIndex    = atoi( Info_ValueForKey( str, "si" ) );
	st->captureServerTime  = atoi( Info_ValueForKey( str, "st" ) );
	st->numMaps            = atoi( Info_ValueForKey( str, "nm" ) );
	st->numVisible         = atoi( Info_ValueForKey( str, "nv" ) );
	st->modeFirstIdx       = atoi( Info_ValueForKey( str, "mf" ) );
	st->modeLastIdx        = atoi( Info_ValueForKey( str, "ml" ) );
	st->modeEndMapIdx      = atoi( Info_ValueForKey( str, "me" ) );
	st->recordFps          = atoi( Info_ValueForKey( str, "fp" ) );
}

/*
===================
LS_DemoCumulativeTime

Compute cumulative IGT for a given split from the parsed demo splits.
Sums up segment times for all done splits from first to upTo (inclusive).
===================
*/
static int LS_DemoCumulativeTime( const lsDemoSplit_t *splits, int numSplits,
								  int modeFirst, int upToIdx ) {
	int i, sum = 0;
	for ( i = 0; i < numSplits; i++ ) {
		if ( splits[i].splitIdx < modeFirst ) continue;
		if ( splits[i].splitIdx > upToIdx ) break;
		if ( splits[i].done ) sum += splits[i].igtMs;
	}
	return sum;
}

/*
===================
LS_DemoFindSplit

Find a demo split entry by split index. Returns NULL if not found.
===================
*/
static const lsDemoSplit_t *LS_DemoFindSplit( const lsDemoSplit_t *splits,
											  int numSplits, int splitIdx ) {
	int i;
	for ( i = 0; i < numSplits; i++ ) {
		if ( splits[i].splitIdx == splitIdx ) return &splits[i];
	}
	return NULL;
}

static void LS_DemoBuildCategoryText( const lsDemoState_t *dst, char *out, int outSize ) {
	const char *modeName = "Full Game";
	const char *skillName = "BEO";
	if ( !out || outSize <= 0 ) return;
	switch ( dst->mode ) {
	case LS_MODE_MISSION:
		modeName = "Mission";
		break;
	case LS_MODE_IL:
		modeName = "Individual Level";
		break;
	default:
		modeName = "Full Game";
		break;
	}
	switch ( dst->difficulty ) {
	case 1:
		skillName = "DHM";
		break;
	case 3:
		skillName = "IADI";
		break;
	default:
		skillName = "BEO";
		break;
	}
	if ( dst->mode == LS_MODE_MISSION ) {
		Com_sprintf( out, outSize, "Demo - %s %d - %s", modeName, dst->mission, skillName );
	} else {
		Com_sprintf( out, outSize, "Demo - %s - %s", modeName, skillName );
	}
}

static void LS_DemoClearWindowState( void ) {
	memset( (void *)&lswnd_state, 0, sizeof( lswnd_state ) );
	lswnd_state.curRow = -1;
	lswnd_state.prevSegBehind = -1;
	lswnd_state.sobBestMs = -1;
	lswnd_state.sobPbMs = -1;
	lswnd_state.sobAvgMs = -1;
	lswnd_state.bptBestMs = -1;
	lswnd_state.bptPbMs = -1;
	lswnd_state.bptAvgMs = -1;
	lswnd_state.ptsMs = 0x80000000;
	lswnd_state.ghostSegMs = -1;
	LS_ShmUpdate();
}

static void LS_DemoUpdateWindowState( void ) {
	const char *stateStr;
	const char *timesStr;
	lsDemoState_t dst;
	lsDemoSplit_t dsplits[LS_MAX_MAPS];
	int numDSplits;
	int mapDefCount;
	int liveIGT, segTime;
	int vi, i, bsi;
	int pbCum, bestCum;
	int prevPbCum;
	qboolean pbComplete, bestComplete;
	volatile lsWndState_t *st = &lswnd_state;

	if ( !clc.demoplaying || !cls.rendererStarted ) {
		LS_DemoClearWindowState();
		return;
	}

	stateStr = cl.gameState.stringData + cl.gameState.stringOffsets[CS_DEMO_LIVESPLIT];
	timesStr = cl.gameState.stringData + cl.gameState.stringOffsets[CS_DEMO_LIVESPLIT_TIMES];
	if ( !stateStr || !stateStr[0] ) {
		LS_DemoClearWindowState();
		return;
	}

	LS_DemoParseState( stateStr, &dst );
	if ( !dst.valid ) {
		LS_DemoClearWindowState();
		return;
	}

	numDSplits = LS_DemoParseTimes( timesStr, dsplits, LS_MAX_MAPS );
	mapDefCount = (int)( sizeof( ls_mapDefs ) / sizeof( ls_mapDefs[0] ) ) - 1;

	if ( dst.modeFirstIdx < 0 ) dst.modeFirstIdx = 0;
	if ( dst.modeLastIdx < dst.modeFirstIdx ) dst.modeLastIdx = dst.modeFirstIdx;
	if ( dst.modeLastIdx >= mapDefCount ) dst.modeLastIdx = mapDefCount - 1;
	if ( dst.currentMapIndex < dst.modeFirstIdx ) dst.currentMapIndex = dst.modeFirstIdx;
	if ( dst.currentMapIndex > dst.modeLastIdx ) dst.currentMapIndex = dst.modeLastIdx;

	if ( dst.finished ) {
		liveIGT = dst.totalIGTMs;
		segTime = 0;
	} else if ( dst.active ) {
		int elapsed = cl.serverTime - dst.captureServerTime;
		if ( dst.paused ) elapsed = 0;
		if ( elapsed < 0 ) elapsed = 0;
		if ( elapsed > 2000 ) elapsed = 0;
		segTime = dst.segTimeMs + elapsed;
		liveIGT = dst.totalIGTMs + segTime;
	} else {
		segTime = dst.segTimeMs;
		liveIGT = dst.totalIGTMs + segTime;
	}
	if ( liveIGT < 0 ) liveIGT = 0;
	if ( segTime < 0 ) segTime = 0;

	memset( (void *)st, 0, sizeof( *st ) );
	st->curRow = -1;
	st->prevSegBehind = -1;
	st->sobBestMs = -1;
	st->sobPbMs = -1;
	st->sobAvgMs = -1;
	st->bptBestMs = -1;
	st->bptPbMs = -1;
	st->bptAvgMs = -1;
	st->ptsMs = 0x80000000;
	st->ghostSegMs = -1;

	Q_strncpyz( (char *)st->gameName, "Return to Castle Wolfenstein", sizeof( st->gameName ) );
	LS_DemoBuildCategoryText( &dst, (char *)st->categoryText, sizeof( st->categoryText ) );
	Q_strncpyz( (char *)st->compareLabel, "Personal Best", sizeof( st->compareLabel ) );
	Q_strncpyz( (char *)st->prevSegLabel, "Previous Segment", sizeof( st->prevSegLabel ) );
	Q_strncpyz( (char *)st->prevSegValue, "-", sizeof( st->prevSegValue ) );

	pbCum = 0;
	bestCum = 0;
	prevPbCum = 0;
	pbComplete = qtrue;
	bestComplete = qtrue;
	vi = 0;
	for ( i = dst.modeFirstIdx; i <= dst.modeLastIdx && i < mapDefCount && vi < LSWND_MAX_ROWS; i++ ) {
		const lsDemoSplit_t *ds;
		const char *name;
		int pbSeg = 0;
		int cumTime = 0;
		qboolean isCur;

		if ( ls_mapDefs[i].cutscene ) continue;

		ds = LS_DemoFindSplit( dsplits, numDSplits, i );
		isCur = ( i == dst.currentMapIndex ) ? qtrue : qfalse;
		name = ls_mapDefs[i].displayName;
		if ( !name || !name[0] ) name = ls_mapDefs[i].name;
		if ( !name || !name[0] ) name = "???";
		Q_strncpyz( (char *)st->rows[vi].name, name, sizeof( st->rows[vi].name ) );
		if ( ls_mapDefs[i].shortName && ls_mapDefs[i].shortName[0] ) {
			Q_strncpyz( (char *)st->rows[vi].shortName, ls_mapDefs[i].shortName, sizeof( st->rows[vi].shortName ) );
		}
		st->rows[vi].state = ( ds && ds->done ) ? 2 : ( isCur ? 1 : 0 );
		st->rows[vi].liveDeltaMs = 0x80000000;
		st->rows[vi].pbCumMs = -1;
		st->rows[vi].bestCumMs = -1;
		st->rows[vi].avgCumMs = -1;

		if ( ds && ds->pbCumMs > 0 ) {
			st->rows[vi].pbCumMs = ds->pbCumMs;
			LS_FormatTime( ds->pbCumMs, (char *)st->rows[vi].pbSplitTime, sizeof( st->rows[vi].pbSplitTime ) );
			if ( ds->pbCumMs > prevPbCum ) {
				pbSeg = ds->pbCumMs - prevPbCum;
				st->rows[vi].pbSegMs = pbSeg;
				st->rows[vi].segCompareMs = pbSeg;
				LS_FormatTime( pbSeg, (char *)st->rows[vi].pbSegTime, sizeof( st->rows[vi].pbSegTime ) );
			}
			prevPbCum = ds->pbCumMs;
		} else {
			pbComplete = qfalse;
		}

		if ( ds && ds->goldMs > 0 ) {
			st->rows[vi].bestSegMs = ds->goldMs;
			LS_FormatTime( ds->goldMs, (char *)st->rows[vi].bestSeg, sizeof( st->rows[vi].bestSeg ) );
			if ( bestComplete ) {
				bestCum += ds->goldMs;
				st->rows[vi].bestCumMs = bestCum;
			}
		} else {
			bestComplete = qfalse;
		}

		if ( ds && ds->done ) {
			cumTime = LS_DemoCumulativeTime( dsplits, numDSplits, dst.modeFirstIdx, i );
			st->rows[vi].cumTimeMs = cumTime;
			st->rows[vi].segTimeMs = ds->igtMs;
			LS_FormatTime( cumTime, (char *)st->rows[vi].splitTime, sizeof( st->rows[vi].splitTime ) );
			LS_FormatTime( ds->igtMs, (char *)st->rows[vi].segTime, sizeof( st->rows[vi].segTime ) );
			if ( ds->pbCumMs > 0 ) {
				int delta = cumTime - ds->pbCumMs;
				if ( delta != 0 ) LS_FormatDelta( delta, (char *)st->rows[vi].delta, sizeof( st->rows[vi].delta ) );
				else Q_strncpyz( (char *)st->rows[vi].delta, "---", sizeof( st->rows[vi].delta ) );
				st->rows[vi].isBehind = delta > 0 ? 1 : 0;
			}
			if ( ds->goldMs > 0 ) {
				int goldDelta = ds->igtMs - ds->goldMs;
				if ( goldDelta != 0 ) LS_FormatDelta( goldDelta, (char *)st->rows[vi].deltaBest, sizeof( st->rows[vi].deltaBest ) );
				else Q_strncpyz( (char *)st->rows[vi].deltaBest, "---", sizeof( st->rows[vi].deltaBest ) );
				st->rows[vi].isGold = goldDelta <= 0 ? 1 : 0;
			}
		} else if ( isCur ) {
			st->curRow = vi;
			st->rows[vi].segTimeMs = segTime;
			LS_FormatTime( segTime, (char *)st->rows[vi].segTime, sizeof( st->rows[vi].segTime ) );
			if ( ds && ds->pbCumMs > 0 ) {
				int liveDelta = liveIGT - ds->pbCumMs;
				st->rows[vi].liveDeltaMs = liveDelta;
				if ( liveDelta != 0 ) LS_FormatDelta( liveDelta, (char *)st->rows[vi].delta, sizeof( st->rows[vi].delta ) );
				else Q_strncpyz( (char *)st->rows[vi].delta, "---", sizeof( st->rows[vi].delta ) );
				st->rows[vi].isBehind = liveDelta > 0 ? 1 : 0;
				st->timerBehind = liveDelta > 0 ? 1 : 0;
			}
		}

		if ( pbComplete && ds && ds->pbCumMs > 0 ) pbCum = ds->pbCumMs;
		vi++;
	}
	st->numRows = vi;

	if ( pbComplete && pbCum > 0 ) {
		st->sobPbMs = pbCum;
		st->bptPbMs = pbCum;
		LS_FormatTime( pbCum, (char *)st->pbText, sizeof( st->pbText ) );
	}
	if ( bestComplete && bestCum > 0 ) {
		st->sobBestMs = bestCum;
		st->bptBestMs = bestCum;
		LS_FormatTime( bestCum, (char *)st->sobText, sizeof( st->sobText ) );
		LS_FormatTime( bestCum, (char *)st->bptText, sizeof( st->bptText ) );
	}

	bsi = 0;
	for ( i = dst.modeFirstIdx; i <= dst.modeLastIdx && i < mapDefCount && bsi < LSWND_MAX_ROWS; i++ ) {
		const lsDemoSplit_t *ds;
		const char *name;
		if ( ls_mapDefs[i].cutscene ) continue;
		ds = LS_DemoFindSplit( dsplits, numDSplits, i );
		name = ls_mapDefs[i].displayName;
		if ( !name || !name[0] ) name = ls_mapDefs[i].name;
		if ( !name || !name[0] ) name = "???";
		Q_strncpyz( (char *)st->bestSegs[bsi].name, name, sizeof( st->bestSegs[bsi].name ) );
		if ( ds && ds->goldMs > 0 ) LS_FormatTime( ds->goldMs, (char *)st->bestSegs[bsi].time, sizeof( st->bestSegs[bsi].time ) );
		else Q_strncpyz( (char *)st->bestSegs[bsi].time, "-", sizeof( st->bestSegs[bsi].time ) );
		st->bestSegs[bsi].deltaMs = 0x80000000;
		st->bestSegs[bsi].state = ( ds && ds->done ) ? 2 : ( i == dst.currentMapIndex ? 1 : 0 );
		bsi++;
	}
	st->numBestSegs = bsi;

	LS_FormatTime( liveIGT, (char *)st->timerText, sizeof( st->timerText ) );
	LS_FormatTime( liveIGT, (char *)st->rtTimerText, sizeof( st->rtTimerText ) );
	LS_FormatTime( segTime, (char *)st->segTimerText, sizeof( st->segTimerText ) );
	st->active = dst.active ? 1 : 0;
	st->finished = dst.finished ? 1 : 0;
	st->paused = ( dst.paused || CL_DemoPaused() ) ? 1 : 0;

	LS_ShmUpdate();
}

/*
===================
LS_DemoDrawPlayback

Main demo playback renderer. Parses CS_DEMO_LIVESPLIT and
CS_DEMO_LIVESPLIT_TIMES from cl.gameState, calculates running
timers, and draws a simplified LiveSplit panel.
===================
*/
static void LS_DemoDrawPlayback( void ) {
	const char *stateStr;
	const char *timesStr;
	lsDemoState_t dst;
	lsDemoSplit_t dsplits[LS_DEMO_MAX_SPLITS];
	int numDSplits;
	int stateOfs, timesOfs;
	int liveIGT, segTime;
	int i, vi;
	char timeBuf[32], deltaBuf[32];

	/* Layout variables */
	float x, y, panelW, charSz, smallSz, rowH, scaleF;
	int timeRight;

	/* Colours */
	vec4_t panelBg     = { 0.04f, 0.04f, 0.06f, 0.82f };
	vec4_t panelBorder = { 0.20f, 0.35f, 0.15f, 0.08f };
	vec4_t headerColor = { 0.35f, 0.75f, 0.20f, 1.00f };
	vec4_t currentMapC = { 1.00f, 1.00f, 0.60f, 1.00f };
	vec4_t completedC  = { 0.72f, 0.72f, 0.72f, 0.80f };
	vec4_t futureMap   = { 0.36f, 0.36f, 0.40f, 0.48f };
	vec4_t aheadColor  = { 0.25f, 0.85f, 0.25f, 1.00f };
	vec4_t behindColor = { 0.85f, 0.25f, 0.25f, 1.00f };
	vec4_t goldColor   = { 1.00f, 0.85f, 0.20f, 1.00f };
	vec4_t timeWhite   = { 0.85f, 0.88f, 0.85f, 0.90f };
	vec4_t timeDim     = { 0.48f, 0.48f, 0.50f, 0.52f };
	vec4_t timerColor  = { 0.85f, 0.95f, 0.80f, 1.00f };
	vec4_t finishedC   = { 0.25f, 0.85f, 0.25f, 1.00f };
	vec4_t finishBad   = { 0.85f, 0.25f, 0.25f, 1.00f };
	vec4_t sepColor    = { 0.22f, 0.38f, 0.12f, 0.18f };
	vec4_t hlBg        = { 0.10f, 0.20f, 0.06f, 0.32f };
	vec4_t labelColor  = { 0.42f, 0.48f, 0.38f, 0.62f };
	vec4_t segTimerClr = { 0.62f, 0.65f, 0.62f, 0.82f };
	vec4_t demoTag     = { 0.90f, 0.65f, 0.15f, 0.80f };
	vec4_t mapNameClr  = { 0.55f, 0.62f, 0.50f, 0.85f };

	/* Read configstrings */
	stateOfs = cl.gameState.stringOffsets[CS_DEMO_LIVESPLIT];
	timesOfs = cl.gameState.stringOffsets[CS_DEMO_LIVESPLIT_TIMES];

	stateStr = stateOfs ? ( cl.gameState.stringData + stateOfs ) : "";
	timesStr = timesOfs ? ( cl.gameState.stringData + timesOfs ) : "";

	if ( !stateStr[0] ) return; /* no LS data in this demo */

	LS_DemoParseState( stateStr, &dst );
	if ( !dst.valid || !dst.active ) return;

	numDSplits = LS_DemoParseTimes( timesStr, dsplits, LS_DEMO_MAX_SPLITS );

	/* ---- Compute live IGT ---- */
	/* totalIGTMs = sum of completed splits at capture.
	   segTimeMs  = segment time on the active split at capture.
	   captureServerTime = cl.serverTime at capture.
	   elapsed    = time since capture (from demo's server time).
	   Running timer = totalIGT + segTime + elapsed.

	   After a map change the gamestate is rebuilt.  CS_DEMO_LIVESPLIT
	   may carry stale data (captureServerTime from the old map) until
	   the first LS_DemoWriteUpdate arrives on the new map (~500ms).
	   During this gap, elapsed would be huge and the timer would show
	   a wrong value.  Detect this by capping elapsed at 2 seconds
	   (well above the normal 500ms capture interval).  When capped,
	   show the snapshot's totalIGTMs without adding elapsed so the
	   timer stays at the last valid total until fresh data arrives. */
	if ( dst.finished ) {
		liveIGT = dst.totalIGTMs;
		segTime = 0;
	} else {
		int elapsed = cl.serverTime - dst.captureServerTime;
		if ( elapsed < 0 ) elapsed = 0;
		if ( elapsed > 2000 ) {
			/* Stale capture - show frozen total, don't add elapsed */
			segTime = dst.segTimeMs;
			liveIGT = dst.totalIGTMs + segTime;
		} else {
			segTime = dst.segTimeMs + elapsed;
			liveIGT = dst.totalIGTMs + segTime;
		}
	}

	/* ---- Layout ---- */
	scaleF = 1.0f;
	charSz  = 5.0f * scaleF;
	smallSz = 4.5f * scaleF;
	rowH    = 10.0f * scaleF;
	panelW  = 178.0f;
	x       = 8.0f;
	y       = 80.0f;
	timeRight = (int)( x + panelW - 4 );

	/* Use layout cvars if available */
	if ( ls_scaleCvar && ls_scaleCvar->value > 0 ) {
		scaleF = ls_scaleCvar->value;
		if ( scaleF < 0.5f ) scaleF = 0.5f;
		if ( scaleF > 3.0f ) scaleF = 3.0f;
		charSz  = 5.0f * scaleF;
		smallSz = 4.5f * scaleF;
		rowH    = 10.0f * scaleF;
	}
	if ( ls_xCvar ) x = ls_xCvar->value;
	if ( ls_yCvar ) y = ls_yCvar->value;
	if ( ls_wCvar ) panelW = ls_wCvar->value;
	timeRight = (int)( x + panelW - 4 );

	/* Alignment: mirror to right if ls_align 1 */
	if ( ls_alignCvar && ls_alignCvar->integer == 1 ) {
		x = 640.0f - x - panelW;
		timeRight = (int)( x + panelW - 4 );
	}

	/* ---- Count rows ---- */
	{
		float panelH;
		int visRows = 0;
		int scrollStart, scrollEnd;
		int maxRows = 6;
		qboolean pinLast = qfalse;

		/* Build visible row list from map defs (non-cutscene in mode range) */
		int visMap[LS_DEMO_MAX_SPLITS];
		int curVisRow = -1;

		for ( i = dst.modeFirstIdx; i <= dst.modeLastIdx && i < (int)( sizeof(ls_mapDefs)/sizeof(ls_mapDefs[0]) - 1 ); i++ ) {
			if ( ls_mapDefs[i].cutscene ) continue;
			if ( i == dst.currentMapIndex ) curVisRow = visRows;
			visMap[visRows++] = i;
		}

		if ( visRows <= 0 ) return;

		/* ---- Scrolling ---- */
		if ( ls_maxrowsCvar && ls_maxrowsCvar->integer >= 2 )
			maxRows = ls_maxrowsCvar->integer;

		if ( visRows <= maxRows ) {
			scrollStart = 0;
			scrollEnd   = visRows - 1;
		} else {
			int scrollSize = maxRows - 1;
			scrollStart = curVisRow - ( scrollSize / 2 );
			if ( scrollStart < 0 ) scrollStart = 0;
			scrollEnd = scrollStart + scrollSize - 1;
			if ( scrollEnd >= visRows - 2 ) {
				scrollEnd = visRows - 1;
				scrollStart = scrollEnd - scrollSize;
				if ( scrollStart < 0 ) scrollStart = 0;
			} else {
				pinLast = qtrue;
			}
		}

		{
			int totalRows = ( scrollEnd - scrollStart + 1 ) + ( pinLast ? 1 : 0 );
			panelH = 14 * scaleF + 2 * scaleF;     /* header + sep */
			panelH += 6 * scaleF + 2 * scaleF;      /* column header + sep */
			panelH += totalRows * rowH;              /* split rows */
			if ( visRows > maxRows && scrollStart > 0 ) panelH += 6 * scaleF;
			if ( pinLast ) panelH += 2 * scaleF;
			panelH += 2 * scaleF;                    /* sep before timer */
			panelH += 8 * scaleF;                    /* timer */
			panelH += 7 * scaleF;                    /* seg timer */
			panelH += 6 * scaleF;                    /* DEMO tag */
		}

		/* ---- Draw background ---- */
		SCR_FillRect( x, y, panelW, panelH, panelBg );
		/* Border */
		SCR_FillRect( x, y, panelW, 1, panelBorder );
		SCR_FillRect( x, y + panelH - 1, panelW, 1, panelBorder );
		SCR_FillRect( x, y, 1, panelH, panelBorder );
		SCR_FillRect( x + panelW - 1, y, 1, panelH, panelBorder );

		/* ---- Header ---- */
		{
			const char *modeStr;
			const char *skillName;
			char headerBuf[64];
			const char *diffTag;
			char diffBuf[12];
			int headerX, tagX;

			switch ( dst.mode ) {
			case LS_MODE_MISSION:
				if ( dst.mission >= 1 && dst.mission <= LS_NUM_MISSION_GROUPS )
					modeStr = ls_missionGroups[dst.mission - 1].shortName;
				else
					modeStr = "Mission";
				break;
			case LS_MODE_IL:
				modeStr = "Individual Level";
				break;
			default:
				modeStr = "Full Game";
				break;
			}

			switch ( dst.difficulty ) {
			case 1: skillName = "DHM"; break;
			case 3: skillName = "IADI"; break;
			default: skillName = "BEO"; break;
			}

			Q_strncpyz( headerBuf, modeStr, sizeof( headerBuf ) );
			diffTag = skillName;
			Com_sprintf( diffBuf, sizeof( diffBuf ), "[%s]", diffTag );

			/* Status dot */
			{
				float *dotClr = dst.finished ? finishedC : headerColor;
				SCR_DrawStringExt( (int)( x + 3 ), (int)( y + 4 ), charSz, "\x07", dotClr, qtrue );
			}

			headerX = (int)( x + 3 + charSz * 1.4f );
			SCR_DrawStringExt( headerX, (int)( y + 4 ), charSz, headerBuf, headerColor, qtrue );

			tagX = headerX + (int)strlen( headerBuf ) * (int)charSz + (int)( charSz * 0.6f );
			SCR_DrawStringExt( tagX, (int)( y + 4 ), smallSz, diffBuf, headerColor, qtrue );
		}
		y += 14 * scaleF;

		/* Separator */
		SCR_FillRect( x + 2, y, panelW - 4, 1, sepColor );
		y += 2 * scaleF;

		/* Column header */
		{
			int hdrY = (int)( y + 1 );
			int trRight = timeRight;
			int wDelta = (int)( 7 * smallSz + 4 );
			int drRight = trRight - (int)( 8 * charSz + 4 ) - 2;
			(void)wDelta;
			(void)drRight;
			SCR_DrawStringExt( trRight - (int)( 4 * smallSz ), hdrY, smallSz, "Time", labelColor, qtrue );
		}
		y += 6 * scaleF;
		SCR_FillRect( x + 2, y, panelW - 4, 1, sepColor );
		y += 2 * scaleF;

		/* ---- Split rows ---- */
		if ( scrollStart > 0 ) {
			SCR_DrawStringExt( (int)( x + panelW / 2 - 6 ), (int)y, smallSz, "...", timeDim, qtrue );
			y += 6 * scaleF;
		}

		for ( vi = scrollStart; vi <= scrollEnd; vi++ ) {
			int splitIdx = visMap[vi];
			const lsDemoSplit_t *ds = LS_DemoFindSplit( dsplits, numDSplits, splitIdx );
			qboolean isCur  = ( splitIdx == dst.currentMapIndex );
			qboolean isDone = ( ds && ds->done );
			float *nameClr;
			const char *name;

			if ( isCur ) {
				SCR_FillRect( x + 1, y, panelW - 2, rowH, hlBg );
			}

			if ( isCur )       nameClr = currentMapC;
			else if ( isDone ) nameClr = completedC;
			else               nameClr = futureMap;

			/* Get display name from mapDefs */
			name = ls_mapDefs[splitIdx].shortName;
			if ( !name ) name = ls_mapDefs[splitIdx].displayName;
			if ( !name ) name = ls_mapDefs[splitIdx].name;
			if ( !name ) name = "???";

			SCR_DrawStringExt( (int)( x + 3 ), (int)( y + 1 ), charSz, name, nameClr, qtrue );

			/* Time column */
			if ( isDone && ds ) {
				int cumTime = LS_DemoCumulativeTime( dsplits, numDSplits, dst.modeFirstIdx, splitIdx );
				if ( cumTime > 0 ) {
					/* Delta vs PB */
					if ( ds->pbCumMs > 0 ) {
						int delta = cumTime - ds->pbCumMs;
						float *deltaClr;
						if ( delta < 0 )      deltaClr = aheadColor;
						else if ( delta > 0 ) deltaClr = behindColor;
						else                  deltaClr = timeDim;

						if ( delta != 0 ) {
							LS_FormatDelta( delta, deltaBuf, sizeof( deltaBuf ) );
						} else {
							Q_strncpyz( deltaBuf, "---", sizeof( deltaBuf ) );
						}
						{
							int deltaRight = timeRight - (int)( 8 * charSz + 4 ) - 2;
							SCR_DrawStringExt( deltaRight - (int)strlen( deltaBuf ) * (int)smallSz,
								(int)( y + 2 ), smallSz, deltaBuf, deltaClr, qtrue );
						}
					}

					LS_FormatTime( cumTime, timeBuf, sizeof( timeBuf ) );
					SCR_DrawStringExt( timeRight - (int)strlen( timeBuf ) * (int)charSz,
						(int)( y + 1 ), charSz, timeBuf, timeWhite, qtrue );
				}
			} else if ( isCur && ds && ds->pbCumMs > 0 ) {
				/* Show PB target time for current map */
				LS_FormatTime( ds->pbCumMs, timeBuf, sizeof( timeBuf ) );
				SCR_DrawStringExt( timeRight - (int)strlen( timeBuf ) * (int)charSz,
					(int)( y + 1 ), charSz, timeBuf, currentMapC, qtrue );
			} else if ( ds && ds->pbCumMs > 0 ) {
				/* Future: show PB cumulative dimmed */
				LS_FormatTime( ds->pbCumMs, timeBuf, sizeof( timeBuf ) );
				SCR_DrawStringExt( timeRight - (int)strlen( timeBuf ) * (int)charSz,
					(int)( y + 1 ), charSz, timeBuf, timeDim, qtrue );
			}
			y += rowH;
		}

		/* Pinned last row */
		if ( pinLast ) {
			int splitIdx = visMap[visRows - 1];
			const lsDemoSplit_t *ds = LS_DemoFindSplit( dsplits, numDSplits, splitIdx );
			qboolean isDone = ( ds && ds->done );
			const char *name;

			SCR_FillRect( x + 2, y, panelW - 4, 1, sepColor );
			y += 2 * scaleF;

			name = ls_mapDefs[splitIdx].shortName;
			if ( !name ) name = ls_mapDefs[splitIdx].displayName;
			if ( !name ) name = ls_mapDefs[splitIdx].name;
			if ( !name ) name = "???";

			SCR_DrawStringExt( (int)( x + 3 ), (int)( y + 1 ), charSz, name,
				isDone ? completedC : futureMap, qtrue );

			if ( isDone && ds ) {
				int cumTime = LS_DemoCumulativeTime( dsplits, numDSplits, dst.modeFirstIdx, splitIdx );
				if ( cumTime > 0 ) {
					LS_FormatTime( cumTime, timeBuf, sizeof( timeBuf ) );
					SCR_DrawStringExt( timeRight - (int)strlen( timeBuf ) * (int)charSz,
						(int)( y + 1 ), charSz, timeBuf, timeWhite, qtrue );
				}
			} else if ( ds && ds->pbCumMs > 0 ) {
				LS_FormatTime( ds->pbCumMs, timeBuf, sizeof( timeBuf ) );
				SCR_DrawStringExt( timeRight - (int)strlen( timeBuf ) * (int)charSz,
					(int)( y + 1 ), charSz, timeBuf, timeDim, qtrue );
			}
			y += rowH;
		}

		/* ---- Separator before timers ---- */
		SCR_FillRect( x + 2, y, panelW - 4, 1, sepColor );
		y += 2 * scaleF;

		/* ---- Main Timer ---- */
		{
			float *tClr;
			if ( dst.finished ) {
				/* Check if final time is worse than PB for colour */
				/* Find last split's PB cumulative */
				const lsDemoSplit_t *lastSplit = NULL;
				int lastIdx = visMap[visRows - 1];
				lastSplit = LS_DemoFindSplit( dsplits, numDSplits, lastIdx );
				if ( lastSplit && lastSplit->pbCumMs > 0 && liveIGT > lastSplit->pbCumMs ) {
					tClr = finishBad;
				} else {
					tClr = finishedC;
				}
			} else {
				tClr = timerColor;
			}

			LS_FormatTime( liveIGT, timeBuf, sizeof( timeBuf ) );
			SCR_DrawStringExt( timeRight - (int)strlen( timeBuf ) * (int)( 8 * scaleF ),
				(int)y, 8 * scaleF, timeBuf, tClr, qtrue );
			y += 8 * scaleF;
		}

		/* ---- Segment Timer ---- */
		if ( !dst.finished ) {
			char segBuf[32];
			LS_FormatTime( segTime, segBuf, sizeof( segBuf ) );
			SCR_DrawStringExt( timeRight - (int)strlen( segBuf ) * (int)( 5 * scaleF ),
				(int)( y + 1 ), 5 * scaleF, segBuf, segTimerClr, qtrue );
		}
		y += 7 * scaleF;

		/* ---- DEMO playback tag + recording FPS ---- */
		{
			const char *tag = "DEMO";
			SCR_DrawStringExt( (int)( x + 3 ), (int)( y + 1 ), smallSz, tag, demoTag, qtrue );

			/* Show recording FPS on the right side */
			if ( dst.recordFps > 0 ) {
				char fpsBuf[32];
				Com_sprintf( fpsBuf, sizeof( fpsBuf ), "REC %d fps", dst.recordFps );
				SCR_DrawStringExt( timeRight - (int)strlen( fpsBuf ) * (int)smallSz,
					(int)( y + 1 ), smallSz, fpsBuf, demoTag, qtrue );
			}
		}
	}
}

/*
===================
LS_BuildDemoCvarString

Builds an Info-string containing all tracked gameplay cvars that
differ from their default values, plus sv_cheats.
Used by the demo recording system to embed cvar state into the
demo file (configstring CS_DEMO_CVARS).

Format: \sv_cheats\<0|1>\<name>\<value>\...

Returns the number of modified cvars written (excluding sv_cheats).
===================
*/
int LS_BuildDemoCvarString( char *out, int outSize ) {
	const lsCvarCheck_t *check;
	char val[256];
	int modified = 0;

	out[0] = '\0';

	/* Always include sv_cheats */
	{
		char svCheats[16];
		Cvar_VariableStringBuffer( "sv_cheats", svCheats, sizeof( svCheats ) );
		Info_SetValueForKey( out, "sv_cheats", svCheats );
	}

	/* Include all tracked cvars that differ from defaults */
	for ( check = ls_settingsTable; check->name; check++ ) {
		float fDefault, fActual, diff;
		qboolean differs = qfalse;

		Cvar_VariableStringBuffer( check->name, val, sizeof( val ) );
		fDefault = atof( check->defaultVal );
		fActual  = atof( val );

		if ( fDefault == 0.0f && fActual == 0.0f ) {
			if ( Q_stricmp( val, check->defaultVal ) != 0 &&
				 Q_stricmp( val, "0" ) != 0 &&
				 Q_stricmp( val, "0.0" ) != 0 &&
				 Q_stricmp( val, "" ) != 0 ) {
				differs = qtrue;
			}
		} else {
			diff = fActual - fDefault;
			if ( diff < 0 ) diff = -diff;
			if ( diff > 0.001f ) differs = qtrue;
		}

		if ( differs ) {
			Info_SetValueForKey( out, check->name, val );
			modified++;
		}
	}

	return modified;
}

/* Called synchronously from CL_Disconnect so that
   disconnect;loadgame in the same frame still triggers
   map-change detection (LS_Frame may never see the
   disconnected state). */
void SCR_LiveSplitNotifyDisconnect( void ) {
	if ( !ls.initialized ) return;
	ls.prevMapname[0] = '\0';
}

static qboolean LS_WindowStateReady( void ) {
	if ( !ls.initialized ) return qfalse;
	if ( !cls.rendererStarted ) return qfalse;
	if ( ls.numMaps <= 0 || ls.numMaps > LS_MAX_MAPS ) return qfalse;
	if ( ls.modeFirstIdx < 0 || ls.modeFirstIdx >= ls.numMaps ) return qfalse;
	if ( ls.modeLastIdx < ls.modeFirstIdx || ls.modeLastIdx >= ls.numMaps ) return qfalse;
	return qtrue;
}

static qboolean LS_WindowActiveUpdateReady( void ) {
	if ( !LS_WindowStateReady() ) return qfalse;
	if ( cls.state != CA_ACTIVE ) return qfalse;
	if ( !cl.mapname[0] ) return qfalse;
	if ( ( ls.active || ls.runFinished ) && ( ls.currentMapIndex < 0 || ls.currentMapIndex >= ls.numMaps ) ) return qfalse;
	return qtrue;
}

static qboolean LS_WindowHasCachedState( void ) {
	return ( lswnd_state.gameName[0] || lswnd_state.numRows > 0 ) ? qtrue : qfalse;
}

static qboolean LS_WindowShouldHoldCachedState( void ) {
	if ( !LS_WindowHasCachedState() ) return qfalse;
	if ( !( ls.active || ls.runFinished ) ) return qfalse;
	if ( cls.state < CA_ACTIVE ) return qtrue;
	if ( !cl.mapname[0] ) return qtrue;
	if ( ls.mapLoadFreeze ) return qtrue;
	if ( Cvar_VariableIntegerValue( "ls_loading" ) ) return qtrue;
	return qfalse;
}

static void LS_WindowHoldCachedState( void ) {
	LS_ShmUpdate();
}

static qboolean LS_Draw2DReady( void ) {
	if ( !cls.rendererStarted ) return qfalse;
	if ( cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 ) return qfalse;
	if ( !cls.whiteShader || !cls.charSetShader ) return qfalse;
	return qtrue;
}

void SCR_LiveSplitDraw( void ) {
	qboolean extEnabled = ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL );
	qboolean activeUpdateReady;
	qboolean draw2DReady;

	/* ---- Demo playback: feed recorded LS data into the main overlay state. */
	if ( clc.demoplaying ) {
		if ( cls.state == CA_ACTIVE && ( !cg_livesplit || cg_livesplit->integer ) ) {
			LS_DemoUpdateWindowState();
		} else {
			LS_DemoClearWindowState();
		}
		return;
	}

	if ( !LS_WindowStateReady() ) return;
	draw2DReady = LS_Draw2DReady();
	if ( cls.state != CA_ACTIVE || !cl.mapname[0] ) {
		if ( LS_WindowShouldHoldCachedState() ) {
			LS_WindowHoldCachedState();
		} else {
			LS_WindowUpdateMenu();
		}
		if ( draw2DReady ) {
			LS_DrawResetConfirmPopup();
		}
		return;
	}

	if ( !cg_livesplit || !cg_livesplit->integer ) {
		/* Even with cg_livesplit off, run timer logic if external LS is enabled */
		if ( !extEnabled ) return;

		LS_Frame();
		activeUpdateReady = LS_WindowActiveUpdateReady();
		if ( activeUpdateReady && ( ls.active || ls.runFinished ) ) {
			LS_WindowUpdate();
		} else if ( LS_WindowShouldHoldCachedState() ) {
			LS_WindowHoldCachedState();
		} else if ( LS_WindowStateReady() ) {
			LS_WindowUpdateMenu();
		}
		if ( draw2DReady ) {
			LS_DrawIGTTimer();
			LS_DrawResetConfirmPopup();
			LS_DrawCenterAlert();
			LS_DrawVerifyOverlay();
		}
		return;
	}

	LS_Frame();
	if ( !LS_WindowStateReady() ) return;
	activeUpdateReady = LS_WindowActiveUpdateReady();
	if ( !activeUpdateReady || ( !ls.active && !ls.runFinished ) ) {
		if ( LS_WindowShouldHoldCachedState() ) {
			LS_WindowHoldCachedState();
		} else {
			LS_WindowUpdateMenu();
		}
		if ( draw2DReady ) {
			LS_DrawIGTTimer();
			LS_DrawResetConfirmPopup();
			LS_DrawCenterAlert();
			LS_DrawVerifyOverlay();
		}
		return;
	}

	/* ---- Update shared memory for standalone LiveSplit exe ---- */
	LS_WindowUpdate();

	/* Ghost segment time (total time the ghost took on the current map) */
	{
		volatile lsWndState_t *st = &lswnd_state;
		st->ghostSegMs = -1;
		st->ghostSegText[0] = '\0';
		st->ghostSegFrac[0] = '\0';
		if ( ls_ghost.playLoaded && ls_ghost.playCount > 0 ) {
			int ghostTotal = ls_ghost.play[ls_ghost.playCount - 1].timeMs;
			if ( ghostTotal > 0 ) {
				st->ghostSegMs = ghostTotal;
				LS_FormatTime( ghostTotal, (char *)st->ghostSegText, sizeof( st->ghostSegText ) );
				{
					char *dot = strchr( (char *)st->ghostSegText, '.' );
					if ( dot ) {
						Q_strncpyz( (char *)st->ghostSegFrac, dot, sizeof( st->ghostSegFrac ) );
					}
				}
			}
		}
	}

	/* Always draw standalone IGT timer if enabled (independent of panel) */
	if ( draw2DReady ) {
		LS_DrawIGTTimer();

		/* Always process reset confirmation popup */
		LS_DrawResetConfirmPopup();

		/* Centered alert (cheats / modified settings) */
		LS_DrawCenterAlert();

		/* Run-finish verification overlay */
		LS_DrawVerifyOverlay();
	}

	/* External-only mode: skip panel, only IGT + reset popup */
	if ( ls_typeCvar && ls_typeCvar->integer == LS_TYPE_EXTERNAL ) {
		return;
	}

	if ( ls_drawCvar && !ls_drawCvar->integer ) {
		/* Panel hidden but timers still running - show small indicator */
		if ( draw2DReady && ls.active && !ls.runFinished ) {
			int ix, iy;
			vec4_t indColor  = { 0.35f, 0.75f, 0.20f, 0.70f };
			vec4_t indShadow = { 0.0f,  0.0f,  0.0f,  0.35f };

			ix = 6;
			/* Place below REC, sv_cheats, and CVAR indicators */
			iy = 4;
			if ( clc.demorecording ) iy += 12;
			if ( Cvar_VariableIntegerValue( "sv_cheats" ) ) iy += 12;
			if ( ls.settingsModCount > 0 ) iy += 12;

			SCR_DrawStringExt( ix + 1, iy + 1, 3, "LS RUNNING", indShadow, qtrue );
			SCR_DrawStringExt( ix, iy, 3, "LS RUNNING", indColor, qtrue );
		}
		return;
	}

	/* The ImGui LiveSplit panel is now the only in-game panel renderer. */
	return;
}
