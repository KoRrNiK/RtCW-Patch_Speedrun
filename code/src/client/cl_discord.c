// cl_discord.c -- lightweight Discord Rich Presence via local IPC

#include "client.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <time.h>

#define DISCORD_OPCODE_HANDSHAKE 0
#define DISCORD_OPCODE_FRAME     1
#define DISCORD_RECONNECT_MS     10000
#define DISCORD_MAX_JSON         4096
#define DISCORD_GAME_TITLE       "Return To Castle Wolfenstein"

typedef struct {
	HANDLE pipe;
	qboolean connected;
	qboolean announcedMissingId;
	int nextConnectTime;
	int nextUpdateTime;
	int lastUpdateMs;
	unsigned int nonce;
	time_t sessionStart;
	time_t activityStart;
	char lastKey[256];
} discordState_t;

typedef struct {
	char details[128];
	char state[128];
	char key[256];
	int partySize;
	int partyMax;
	qboolean timerActive;
	time_t timerStart;
} discordActivity_t;

static discordState_t discord;

static cvar_t *discord_presence;
static cvar_t *discord_presence_client_id;
static cvar_t *discord_presence_update_ms;
static cvar_t *discord_presence_large_image;
static cvar_t *discord_presence_large_text;
static cvar_t *discord_presence_timer;
static cvar_t *discord_presence_debug;
static cvar_t *discord_presence_private;

static void CL_DiscordClosePipe( void ) {
	if ( discord.pipe && discord.pipe != INVALID_HANDLE_VALUE ) {
		CloseHandle( discord.pipe );
	}

	discord.pipe = INVALID_HANDLE_VALUE;
	discord.connected = qfalse;
}

static void CL_DiscordAppend( char *out, int outSize, int *pos, const char *fmt, ... ) {
	va_list args;
	int written;

	if ( !out || outSize <= 0 || !pos || *pos >= outSize - 1 ) {
		return;
	}

	va_start( args, fmt );
	written = Q_vsnprintf( out + *pos, outSize - *pos, fmt, args );
	va_end( args );

	if ( written < 0 ) {
		*pos = outSize - 1;
		out[*pos] = '\0';
		return;
	}

	*pos += written;
	if ( *pos >= outSize ) {
		*pos = outSize - 1;
		out[*pos] = '\0';
	}
}

static void CL_DiscordAppendJsonString( char *out, int outSize, int *pos, const char *text ) {
	const unsigned char *in;
	char escaped[8];

	CL_DiscordAppend( out, outSize, pos, "\"" );

	if ( text ) {
		for ( in = (const unsigned char *)text; *in && *pos < outSize - 2; in++ ) {
			switch ( *in ) {
			case '\\':
				CL_DiscordAppend( out, outSize, pos, "\\\\" );
				break;
			case '"':
				CL_DiscordAppend( out, outSize, pos, "\\\"" );
				break;
			case '\n':
				CL_DiscordAppend( out, outSize, pos, "\\n" );
				break;
			case '\r':
				CL_DiscordAppend( out, outSize, pos, "\\r" );
				break;
			case '\t':
				CL_DiscordAppend( out, outSize, pos, "\\t" );
				break;
			default:
				if ( *in < 32 ) {
					Com_sprintf( escaped, sizeof( escaped ), "\\u%04x", *in );
					CL_DiscordAppend( out, outSize, pos, "%s", escaped );
				} else {
					CL_DiscordAppend( out, outSize, pos, "%c", *in );
				}
				break;
			}
		}
	}

	CL_DiscordAppend( out, outSize, pos, "\"" );
}

static qboolean CL_DiscordWritePacket( int opcode, const char *payload ) {
	uint32_t header[2];
	DWORD written;
	DWORD length;

	if ( !discord.connected || !discord.pipe || discord.pipe == INVALID_HANDLE_VALUE ) {
		return qfalse;
	}

	length = (DWORD)strlen( payload );
	header[0] = (uint32_t)opcode;
	header[1] = (uint32_t)length;

	if ( !WriteFile( discord.pipe, header, sizeof( header ), &written, NULL ) || written != sizeof( header ) ) {
		CL_DiscordClosePipe();
		return qfalse;
	}

	if ( length > 0 && ( !WriteFile( discord.pipe, payload, length, &written, NULL ) || written != length ) ) {
		CL_DiscordClosePipe();
		return qfalse;
	}

	return qtrue;
}

static void CL_DiscordDrainPipe( void ) {
	char buffer[1024];
	DWORD available;
	DWORD read;

	if ( !discord.connected || !discord.pipe || discord.pipe == INVALID_HANDLE_VALUE ) {
		return;
	}

	while ( PeekNamedPipe( discord.pipe, NULL, 0, NULL, &available, NULL ) && available > 0 ) {
		if ( !ReadFile( discord.pipe, buffer, available > sizeof( buffer ) ? sizeof( buffer ) : available, &read, NULL ) || read == 0 ) {
			break;
		}
	}
}

static qboolean CL_DiscordHasClientId( void ) {
	const char *clientId;

	clientId = discord_presence_client_id ? discord_presence_client_id->string : "";
	if ( !clientId[0] || !Q_stricmp( clientId, "0" ) ) {
		if ( !discord.announcedMissingId ) {
			Com_Printf( "Discord Rich Presence disabled: set cl_discord_presence_client_id to your Discord application ID.\n" );
			discord.announcedMissingId = qtrue;
		}
		return qfalse;
	}

	return qtrue;
}

static const char *CL_DiscordDefaultValue( const char *legacyName, const char *fallback ) {
	const char *legacy;

	legacy = Cvar_VariableString( legacyName );
	return legacy && legacy[0] ? legacy : fallback;
}

static qboolean CL_DiscordConnect( void ) {
	char pipeName[64];
	char payload[256];
	const char *clientId;
	int i;

	if ( discord.connected ) {
		return qtrue;
	}

	if ( !discord_presence || !discord_presence->integer || !CL_DiscordHasClientId() ) {
		return qfalse;
	}

	for ( i = 0; i < 10; i++ ) {
		Com_sprintf( pipeName, sizeof( pipeName ), "\\\\?\\pipe\\discord-ipc-%d", i );
		discord.pipe = CreateFileA( pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
		if ( discord.pipe != INVALID_HANDLE_VALUE ) {
			break;
		}
	}

	if ( discord.pipe == INVALID_HANDLE_VALUE ) {
		discord.nextConnectTime = cls.realtime + DISCORD_RECONNECT_MS;
		return qfalse;
	}

	discord.connected = qtrue;
	clientId = discord_presence_client_id->string;
	Com_sprintf( payload, sizeof( payload ), "{\"v\":1,\"client_id\":\"%s\"}", clientId );

	if ( !CL_DiscordWritePacket( DISCORD_OPCODE_HANDSHAKE, payload ) ) {
		discord.nextConnectTime = cls.realtime + DISCORD_RECONNECT_MS;
		return qfalse;
	}

	discord.nextUpdateTime = 0;
	if ( discord_presence_debug && discord_presence_debug->integer ) {
		Com_Printf( "Discord Rich Presence connected.\n" );
	}

	return qtrue;
}

static void CL_DiscordCleanMapName( char *out, int outSize ) {
	const char *map;
	int len;

	out[0] = '\0';

	if ( clc.demoplaying && clc.demoCurrentMapname[0] ) {
		map = clc.demoCurrentMapname;
	} else {
		map = cl.mapname;
	}

	if ( !map || !map[0] ) {
		return;
	}

	if ( !Q_stricmpn( map, "maps/", 5 ) ) {
		map += 5;
	}

	Q_strncpyz( out, map, outSize );
	len = strlen( out );
	if ( len > 4 && !Q_stricmp( out + len - 4, ".bsp" ) ) {
		out[len - 4] = '\0';
	}
}

static void CL_DiscordFormatDuration( int ms, char *out, int outSize ) {
	int hours;
	int mins;
	int secs;

	if ( !out || outSize <= 0 ) {
		return;
	}

	if ( ms < 0 ) {
		ms = 0;
	}

	hours = ms / 3600000;
	mins = ( ms / 60000 ) % 60;
	secs = ( ms / 1000 ) % 60;

	if ( hours > 0 ) {
		Com_sprintf( out, outSize, "%d:%02d:%02d", hours, mins, secs );
	} else {
		Com_sprintf( out, outSize, "%d:%02d", mins, secs );
	}
}

static void CL_DiscordBuildTimerText( const lsPresenceSnapshot_t *lsPresence, char *out, int outSize ) {
	int ms;
	char timeText[32];

	if ( !out || outSize <= 0 ) {
		return;
	}

	out[0] = '\0';
	if ( lsPresence && lsPresence->timerEnabled && ( lsPresence->active || lsPresence->finished ) ) {
		ms = lsPresence->igtMs;
		CL_DiscordFormatDuration( ms, timeText, sizeof( timeText ) );
		Com_sprintf( out, outSize, "IGT %s", timeText );
		return;
	}

	ms = Sys_Milliseconds();
	CL_DiscordFormatDuration( ms, timeText, sizeof( timeText ) );
	Com_sprintf( out, outSize, "Game %s", timeText );
}

static void CL_DiscordBuildActivity( discordActivity_t *activity ) {
	lsRaceUiSnapshot_t race;
	lsPresenceSnapshot_t lsPresence;
	char map[MAX_QPATH];
	char timerText[32];
	time_t now;

	if ( !activity ) {
		return;
	}

	memset( activity, 0, sizeof( *activity ) );
	now = time( NULL );
	CL_DiscordCleanMapName( map, sizeof( map ) );
	LS_RaceBuildSnapshot( &race );
	LS_BuildPresenceSnapshot( &lsPresence );
	CL_DiscordBuildTimerText( &lsPresence, timerText, sizeof( timerText ) );

	if ( discord_presence_private && discord_presence_private->integer ) {
		Q_strncpyz( activity->details, DISCORD_GAME_TITLE, sizeof( activity->details ) );
		Com_sprintf( activity->state, sizeof( activity->state ), "Speedrun build - %s", timerText );
		Com_sprintf( activity->key, sizeof( activity->key ), "private:%d:%d:%d", cls.state, lsPresence.timerEnabled, lsPresence.active );
		return;
	}

	if ( race.active ) {
		Q_strncpyz( activity->details, race.category[0] ? race.category : lsPresence.categoryText, sizeof( activity->details ) );
		Com_sprintf( activity->state, sizeof( activity->state ), "%s - %s - %s",
					  race.state[0] ? race.state : "RACE",
					  map[0] ? map : ( lsPresence.mapText[0] ? lsPresence.mapText : "-" ),
					  timerText );

		if ( race.playerCount > 0 ) {
			activity->partySize = race.playerCount;
			activity->partyMax = LS_RACE_UI_MAX_PLAYERS;
		}

		if ( lsPresence.active && lsPresence.igtMs >= 0 ) {
			activity->timerActive = qtrue;
			activity->timerStart = now - ( lsPresence.igtMs / 1000 );
		}

		Com_sprintf( activity->key, sizeof( activity->key ), "race:%s:%s:%s:%s:%d:%d",
					 race.category, race.state, map, lsPresence.timerEnabled ? "ls" : "nols", lsPresence.active, lsPresence.finished );
		return;
	}

	if ( lsPresence.timerEnabled && ( lsPresence.active || lsPresence.finished ) ) {
		Q_strncpyz( activity->details, lsPresence.categoryText, sizeof( activity->details ) );
		if ( lsPresence.finished ) {
			Com_sprintf( activity->state, sizeof( activity->state ), "FINISHED - %s - %s", lsPresence.stageText, timerText );
		} else {
			Com_sprintf( activity->state, sizeof( activity->state ), "RUN - %s - %s", lsPresence.stageText, timerText );
			activity->timerActive = qtrue;
			activity->timerStart = now - ( lsPresence.igtMs / 1000 );
		}
		Com_sprintf( activity->key, sizeof( activity->key ), "run:%s:%s:%d:%d:%d:%s",
					 lsPresence.categoryText, lsPresence.stageText, lsPresence.active, lsPresence.finished, lsPresence.timerEnabled, lsPresence.mapText );
		return;
	}

	if ( lsPresence.timerEnabled ) {
		Q_strncpyz( activity->details, lsPresence.categoryText, sizeof( activity->details ) );
		Com_sprintf( activity->state, sizeof( activity->state ), "Ready - %s", timerText );
		Com_sprintf( activity->key, sizeof( activity->key ), "ready:%s:%d:%d", lsPresence.categoryText, lsPresence.timerEnabled, cls.state );
		return;
	}

	if ( cls.state == CA_CINEMATIC ) {
		Q_strncpyz( activity->details, DISCORD_GAME_TITLE, sizeof( activity->details ) );
		Com_sprintf( activity->state, sizeof( activity->state ), "Watching a cinematic - %s", timerText );
		Com_sprintf( activity->key, sizeof( activity->key ), "cinematic:%d", lsPresence.timerEnabled );
		return;
	}

	if ( clc.demoplaying ) {
		Q_strncpyz( activity->details, DISCORD_GAME_TITLE, sizeof( activity->details ) );
		if ( map[0] ) {
			Com_sprintf( activity->state, sizeof( activity->state ), "Demo - %s - %s", map, timerText );
		} else {
			Com_sprintf( activity->state, sizeof( activity->state ), "Demo playback - %s", timerText );
		}
		Com_sprintf( activity->key, sizeof( activity->key ), "demo:%s:%d", map, lsPresence.timerEnabled );
		return;
	}

	if ( cls.state == CA_ACTIVE && map[0] ) {
		Q_strncpyz( activity->details, DISCORD_GAME_TITLE, sizeof( activity->details ) );
		Com_sprintf( activity->state, sizeof( activity->state ), "Single Player - %s - %s", map, timerText );
		Com_sprintf( activity->key, sizeof( activity->key ), "sp:%s:%d", map, lsPresence.timerEnabled );
		return;
	}

	if ( cls.state == CA_CONNECTING || cls.state == CA_CHALLENGING || cls.state == CA_CONNECTED || cls.state == CA_LOADING || cls.state == CA_PRIMED ) {
		Q_strncpyz( activity->details, DISCORD_GAME_TITLE, sizeof( activity->details ) );
		if ( map[0] ) {
			Com_sprintf( activity->state, sizeof( activity->state ), "Loading - %s - %s", map, timerText );
		} else {
			Com_sprintf( activity->state, sizeof( activity->state ), "Preparing game - %s", timerText );
		}
		Com_sprintf( activity->key, sizeof( activity->key ), "loading:%d:%s:%d", cls.state, map, lsPresence.timerEnabled );
		return;
	}

	Q_strncpyz( activity->details, DISCORD_GAME_TITLE, sizeof( activity->details ) );
	Com_sprintf( activity->state, sizeof( activity->state ), "In the menu - %s", timerText );
	Com_sprintf( activity->key, sizeof( activity->key ), "menu:%d:%d", cls.state, lsPresence.timerEnabled );
}

static qboolean CL_DiscordActivityChanged( void ) {
	discordActivity_t activity;

	CL_DiscordBuildActivity( &activity );
	return Q_stricmp( discord.lastKey, activity.key ) != 0;
}

static qboolean CL_DiscordSendActivity( void ) {
	char payload[DISCORD_MAX_JSON];
	discordActivity_t activity;
	const char *largeImage;
	const char *largeText;
	int pos;

	CL_DiscordBuildActivity( &activity );

	if ( Q_stricmp( discord.lastKey, activity.key ) ) {
		Q_strncpyz( discord.lastKey, activity.key, sizeof( discord.lastKey ) );
		discord.activityStart = time( NULL );
	}
	if ( !activity.timerActive ) {
		activity.timerActive = qtrue;
		activity.timerStart = discord.activityStart ? discord.activityStart : discord.sessionStart;
	}

	largeImage = discord_presence_large_image && discord_presence_large_image->string[0] ? discord_presence_large_image->string : "wolfsp";
	largeText = discord_presence_large_text && discord_presence_large_text->string[0] ? discord_presence_large_text->string : DISCORD_GAME_TITLE;

	pos = 0;
	payload[0] = '\0';
	CL_DiscordAppend( payload, sizeof( payload ), &pos, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{", (unsigned long)GetCurrentProcessId() );
	CL_DiscordAppend( payload, sizeof( payload ), &pos, "\"name\":" );
	CL_DiscordAppendJsonString( payload, sizeof( payload ), &pos, DISCORD_GAME_TITLE );
	CL_DiscordAppend( payload, sizeof( payload ), &pos, ",\"type\":0,\"details\":" );
	CL_DiscordAppendJsonString( payload, sizeof( payload ), &pos, activity.details );
	CL_DiscordAppend( payload, sizeof( payload ), &pos, ",\"state\":" );
	CL_DiscordAppendJsonString( payload, sizeof( payload ), &pos, activity.state );
	if ( ( !discord_presence_timer || discord_presence_timer->integer ) && activity.timerActive ) {
		CL_DiscordAppend( payload, sizeof( payload ), &pos, ",\"timestamps\":{\"start\":%lld}", (long long)activity.timerStart );
	}
	CL_DiscordAppend( payload, sizeof( payload ), &pos, ",\"assets\":{\"large_image\":" );
	CL_DiscordAppendJsonString( payload, sizeof( payload ), &pos, largeImage );
	CL_DiscordAppend( payload, sizeof( payload ), &pos, ",\"large_text\":" );
	CL_DiscordAppendJsonString( payload, sizeof( payload ), &pos, largeText );
	CL_DiscordAppend( payload, sizeof( payload ), &pos, "}" );
	if ( activity.partySize > 0 && activity.partyMax > 0 ) {
		CL_DiscordAppend( payload, sizeof( payload ), &pos, ",\"party\":{\"size\":[%d,%d]}", activity.partySize, activity.partyMax );
	}
	CL_DiscordAppend( payload, sizeof( payload ), &pos, "}},\"nonce\":\"rtcw-%u\"}", ++discord.nonce );

	if ( discord_presence_debug && discord_presence_debug->integer ) {
		Com_Printf( "Discord Rich Presence: %s | %s\n", activity.details, activity.state );
	}

	return CL_DiscordWritePacket( DISCORD_OPCODE_FRAME, payload );
}

static void CL_DiscordClearActivity( void ) {
	char payload[256];

	if ( !discord.connected ) {
		return;
	}

	Com_sprintf( payload, sizeof( payload ), "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":null},\"nonce\":\"rtcw-%u\"}",
				 (unsigned long)GetCurrentProcessId(), ++discord.nonce );
	CL_DiscordWritePacket( DISCORD_OPCODE_FRAME, payload );
}

static int CL_DiscordUpdateDelay( void ) {
	int delay;

	delay = discord_presence_update_ms ? discord_presence_update_ms->integer : 15000;
	if ( delay < 5000 ) {
		delay = 5000;
	} else if ( delay > 60000 ) {
		delay = 60000;
	}

	return delay;
}

static void CL_DiscordStatus_f( void ) {
	Com_Printf( "Discord Rich Presence: %s\n", discord_presence && discord_presence->integer ? "enabled" : "disabled" );
	Com_Printf( "  connection: %s\n", discord.connected ? "connected" : "offline" );
	Com_Printf( "  client id: %s\n", discord_presence_client_id && discord_presence_client_id->string[0] ? discord_presence_client_id->string : "<not set>" );
	Com_Printf( "  update ms: %d\n", CL_DiscordUpdateDelay() );
	Com_Printf( "  timer: %s\n", !discord_presence_timer || discord_presence_timer->integer ? "enabled" : "disabled" );
}

static void CL_DiscordReconnect_f( void ) {
	CL_DiscordClearActivity();
	CL_DiscordClosePipe();
	discord.nextConnectTime = 0;
	discord.nextUpdateTime = 0;
	discord.lastKey[0] = '\0';
	Com_Printf( "Discord Rich Presence reconnect queued.\n" );
}

void CL_DiscordInit( void ) {
	memset( &discord, 0, sizeof( discord ) );
	discord.pipe = INVALID_HANDLE_VALUE;
	discord.sessionStart = time( NULL );
	discord.activityStart = time( NULL );

	discord_presence = Cvar_Get( "cl_discord_presence", CL_DiscordDefaultValue( "discord_presence", "1" ), CVAR_ARCHIVE );
	discord_presence_client_id = Cvar_Get( "cl_discord_presence_client_id", CL_DiscordDefaultValue( "discord_presence_client_id", "1504825622088646786" ), CVAR_ARCHIVE );
	discord_presence_update_ms = Cvar_Get( "cl_discord_presence_update_ms", CL_DiscordDefaultValue( "discord_presence_update_ms", "15000" ), CVAR_ARCHIVE );
	discord_presence_large_image = Cvar_Get( "cl_discord_presence_large_image", CL_DiscordDefaultValue( "discord_presence_large_image", "wolfsp" ), CVAR_ARCHIVE );
	discord_presence_large_text = Cvar_Get( "cl_discord_presence_large_text", CL_DiscordDefaultValue( "discord_presence_large_text", DISCORD_GAME_TITLE ), CVAR_ARCHIVE );
	discord_presence_timer = Cvar_Get( "cl_discord_presence_timer", CL_DiscordDefaultValue( "discord_presence_timer", "1" ), CVAR_ARCHIVE );
	discord_presence_debug = Cvar_Get( "cl_discord_presence_debug", CL_DiscordDefaultValue( "discord_presence_debug", "0" ), 0 );
	discord_presence_private = Cvar_Get( "cl_discord_presence_private", CL_DiscordDefaultValue( "discord_presence_private", "0" ), CVAR_ARCHIVE );

	Cmd_AddCommand( "cl_discord_status", CL_DiscordStatus_f );
	Cmd_AddCommand( "cl_discord_reconnect", CL_DiscordReconnect_f );
}

void CL_DiscordFrame( void ) {
	if ( !discord_presence || !discord_presence->integer ) {
		if ( discord.connected ) {
			CL_DiscordClearActivity();
			CL_DiscordClosePipe();
		}
		return;
	}

	if ( !discord.connected ) {
		if ( cls.realtime >= discord.nextConnectTime ) {
			CL_DiscordConnect();
		}
		return;
	}

	CL_DiscordDrainPipe();

	if ( cls.realtime < discord.nextUpdateTime && !CL_DiscordActivityChanged() ) {
		return;
	}

	if ( !CL_DiscordSendActivity() ) {
		discord.nextConnectTime = cls.realtime + DISCORD_RECONNECT_MS;
		return;
	}

	discord.nextUpdateTime = cls.realtime + CL_DiscordUpdateDelay();
}

void CL_DiscordShutdown( void ) {
	Cmd_RemoveCommand( "cl_discord_status" );
	Cmd_RemoveCommand( "cl_discord_reconnect" );

	CL_DiscordClearActivity();
	CL_DiscordClosePipe();
}

#else

void CL_DiscordInit( void ) {
}

void CL_DiscordFrame( void ) {
}

void CL_DiscordShutdown( void ) {
}

#endif
