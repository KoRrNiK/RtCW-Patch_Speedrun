#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "resource.h"

#define RACE_MAX_PLAYERS 8
#define RACE_PROTO_VERSION 2
#define RACE_NICK_MAX 32
#define RACE_MAP_MAX 64
#define RACE_STAGE_MAX 32
#define RACE_CHAT_MAX 192
#define RACE_PACKET_MAX 2048
#define RACE_MAX_TOKENS 64
#define RACE_TIMEOUT_MS 30000
#define RACE_LOAD_TIMEOUT_MS 60000
#define RACE_LOAD_RESEND_MS 500
#define RACE_COUNTDOWN_RESEND_MS 250
#define RACE_START_RESEND_MS 250
#define RACE_START_RESEND_WINDOW 5000
#define RACE_PLAYER_BROADCAST_MS 33
#define RACE_ROSTER_RESEND_MS 500
#define RACE_MAX_PACKETS_PER_POLL 128
#define RACE_LOG_MAX 512
#define RACE_LOG_TEXT_MAX 384
#define RACE_QUEUE_MAX 16
#define RACE_RUN_HISTORY_MAX 64
#define RACE_PASSWORD_MAX 32

#define RACE_MODE_FULL 0
#define RACE_MODE_CHAPTER 1
#define RACE_MODE_IL 2

#define RACE_STATE_OFFLINE 0
#define RACE_STATE_LOBBY 2
#define RACE_STATE_LOADING 3
#define RACE_STATE_COUNTDOWN 4
#define RACE_STATE_RACING 5
#define RACE_STATE_FINISHED 6

#define RACE_LOG_INFO 0
#define RACE_LOG_SERVER 1
#define RACE_LOG_COMMAND 2
#define RACE_LOG_CHAT 3
#define RACE_LOG_SUCCESS 4
#define RACE_LOG_WARN 5
#define RACE_LOG_ERROR 6

#ifdef __cplusplus
extern "C" {
#endif

typedef int qboolean;
#ifndef qtrue
#define qtrue 1
#define qfalse 0
#endif

typedef int raceLogKind_t;

typedef struct {
    raceLogKind_t kind;
    char text[RACE_LOG_TEXT_MAX];
} raceLogEntry_t;

typedef struct {
    int used;
    int slot;
    struct sockaddr_in address;
    char nick[RACE_NICK_MAX];
    int color[3];
    char map[RACE_MAP_MAX];
    int loaded;
    int started;
    int finished;
    int paused;
    int inMenu;
    int left;
    int timedOut;
    int kicked;
    int cheatFlags;
    int timeMs;
    int igtMs;
    int stageTimeMs;
    int stageProgress;
    char stageName[RACE_STAGE_MAX];
    int crouched;
    int health;
    int armor;
    int weapon;
    int ammo;
    int clip;
    int legsAnim;
    int torsoAnim;
    int movementDir;
    int eFlags;
    int groundEntityNum;
    int animMovetype;
    int objectivesFound;
    int objectivesTotal;
    int zoneProgress;
    int zoneTotal;
    float x, y, z, yaw, speed, pitch;
    float vx, vy, vz;
    DWORD lastHeardMs;
    DWORD lastBroadcastMs;
    DWORD lastKickRejectMs;
    DWORD pausedSinceMs;
    DWORD lastPauseAlertMs;
    int pauseAlerted;
    int mapMismatchAlerted;
} racePlayer_t;

typedef struct {
    int used;
    struct sockaddr_in address;
    char nick[RACE_NICK_MAX];
    int color[3];
    DWORD requestMs;
    DWORD lastNotifyMs;
} raceQueueEntry_t;

typedef struct {
    char nick[RACE_NICK_MAX];
    int color[3];
    char map[RACE_MAP_MAX];
    char stageName[RACE_STAGE_MAX];
    int slot;
    int finished;
    int timeMs;
    int igtMs;
    int stageProgress;
    int objectivesFound;
    int objectivesTotal;
    int zoneProgress;
    int zoneTotal;
    int cheatFlags;
    int timedOut;
    int left;
    int kicked;
} raceRunPlayerResult_t;

typedef struct {
    int used;
    int runId;
    int session;
    int completed;
    time_t startTime;
    time_t endTime;
    int durationMs;
    int mode;
    int mission;
    int percent100;
    int difficulty;
    int hl1Movement;
    int autoJump;
    int antiCheat;
    char targetMap[RACE_MAP_MAX];
    int playerCount;
    raceRunPlayerResult_t players[RACE_MAX_PLAYERS];
} raceRunHistory_t;

typedef struct {
    SOCKET socketId;
    char bindAddress[64];
    unsigned short port;
    int maxPlayers;
    int session;
    int state;
    int mode;
    int mission;
    int percent100;
    int difficulty;
    int hl1Movement;
    int autoJump;
    int antiCheat;
    int autoReadyCheck;
    int queueEnabled;
    int privateLobby;
    int pauseAlertMs;
    char password[RACE_PASSWORD_MAX];
    int countdownMs;
    DWORD countdownStartMs;
    DWORD raceStartMs;
    DWORD lastLoadBroadcastMs;
    DWORD lastCountdownBroadcastMs;
    DWORD lastStartBroadcastMs;
    DWORD lastRosterBroadcastMs;
    char ilMap[RACE_MAP_MAX];
    char targetMap[RACE_MAP_MAX];
    char hostName[RACE_NICK_MAX];
    racePlayer_t players[RACE_MAX_PLAYERS];
    raceQueueEntry_t queue[RACE_QUEUE_MAX];
    raceRunHistory_t runs[RACE_RUN_HISTORY_MAX];
    int runCount;
    int runHead;
    int nextRunId;
    int currentRunRecorded;
} raceHost_t;

typedef struct {
    int chapter;
    const char *displayName;
    const char *startMap;
} raceChapter_t;

extern raceHost_t raceHost;
extern const raceChapter_t raceChapters[];
extern const int raceChapterCount;
extern const char *raceILMaps[];
extern const int raceILMapCount;
extern const char *raceDifficulties[];

extern HINSTANCE gInstance;
extern HWND gMainWnd;
extern int gRunning;

extern char gUiHostName[RACE_NICK_MAX];
extern char gUiIlMap[RACE_MAP_MAX];
extern char gUiBindAddress[64];
extern char gUiChat[RACE_CHAT_MAX];
extern char gUiCommand[512];
extern char gUiLog[12000];
extern char gHostError[512];
extern char gConfigPath[MAX_PATH];
extern char gConfigNotice[512];
extern int gUiPort;
extern int gUiCountdownSec;
extern int gUiSelectedSlot;
extern int gUiScrollLog;
extern int gThemeDark;
extern float gThemeBlend;
extern float gThemeTarget;
extern float gUiAnimTime;
extern raceLogEntry_t gUiLogEntries[RACE_LOG_MAX];
extern int gUiLogCount;
extern int gConfigLoaded;

void Race_Copy(char *dst, size_t dstSize, const char *src);
void Race_SanitizeToken(const char *input, char *out, size_t outSize);
void Race_SanitizeOptionalToken(const char *input, char *out, size_t outSize);
char *Race_Trim(char *text);
int Race_ParseBoolValue(const char *value, int fallback);
int Race_ParseModeValue(const char *value, int fallback);
const char *Race_FileNameFromPath(const char *path);
const char *Race_AddrToString(const struct sockaddr_in *address);
int Race_PlayerCount(void);
int Race_QueueCount(void);
int Race_PlayerBlocksReady(const racePlayer_t *p);
void Race_LoadedCounts(int *loadedOut, int *totalOut);
int Race_AllLoaded(void);
int Race_AllFinished(void);
int Race_AnyRemoteNotStarted(void);
const char *Race_StateName(int state);
const char *Race_PlayerStateText(const racePlayer_t *p);
void Race_FormatTime(int ms, char *out, size_t outSize);
void Race_FormatCountdown(int ms, char *out, size_t outSize);
int Race_HostIsRunning(void);
void Race_ClearPlayers(void);
void Race_FormatWsaError(const char *op, int err, char *out, size_t outSize);
void Race_BuildConfigPath(void);
int Race_LoadServerConfig(int quiet);
int Race_SaveServerConfig(void);
int Race_NormalizeSettings(void);
int Race_ValidateHostSettings(char *out, size_t outSize);
int Race_ValidateRaceSettings(char *out, size_t outSize);

racePlayer_t *Race_FindPlayerBySlot(int slot);
void Race_SendText(const struct sockaddr_in *to, const char *fmt, ...);
void Race_BroadcastText(const char *fmt, ...);
void Race_BroadcastRoster(void);
void Race_BroadcastConfig(void);
int Race_CountdownRemaining(DWORD now);
int Race_Tokenize(char *text, char **tokens, int maxTokens);
void Race_ProcessPacket(char *packetText, int packetLen, const struct sockaddr_in *from);
void Race_PollNetwork(void);
const char *Race_ResolveStartMap(void);
void Race_ResetLobby(void);
int Race_StartRace(void);
void Race_StopRace(void);
void Race_Tick(void);
int Race_OpenSocket(const char *bindIp, int port, char *errorOut, size_t errorOutSize);
int Race_StartHost(void);
void Race_StopHost(void);
int Race_ExportRunsCsv(const char *path, char *status, size_t statusSize);
int Race_ExportRunsJson(const char *path, char *status, size_t statusSize);

void Race_InitUiState(void);
void Race_GuiLog(const char *fmt, ...);
void Race_GuiLogSrv(const char *fmt, ...);
void Race_GuiRefresh(void);
void Race_OnSettingsChanged(int writeBack);
LRESULT CALLBACK Race_WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
int Race_CreateOpenGLContext(HWND hwnd);
int Race_InitImGui(void);
void Race_ShutdownImGui(void);
void Race_BuildGui(void);
void Race_RenderFrame(void);

#ifdef __cplusplus
}
#endif
