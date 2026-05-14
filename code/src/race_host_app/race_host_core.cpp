#include "race_host_shared.h"
/* ---------- networking ---------- */
static int Race_SameAddress( const struct sockaddr_in *l, const struct sockaddr_in *r ) {
    return l->sin_addr.s_addr == r->sin_addr.s_addr && l->sin_port == r->sin_port;
}

static racePlayer_t *Race_FindPlayerByAddress( const struct sockaddr_in *address ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( p->used && Race_SameAddress( &p->address, address ) ) return p;
    }
    return NULL;
}

racePlayer_t *Race_FindPlayerBySlot( int slot ) {
    if ( slot < 0 || slot >= raceHost.maxPlayers ) return NULL;
    if ( !raceHost.players[slot].used ) return NULL;
    return &raceHost.players[slot];
}

static racePlayer_t *Race_AllocPlayer( const struct sockaddr_in *address ) {
    int i;
    /* Re-use a previously vacated slot (left/timedOut/kicked) before
       picking a fresh one; this is the path the in-game host takes too. */
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( p->used && ( p->left || p->kicked || p->timedOut ) ) {
            memset( p, 0, sizeof( *p ) );
            p->used = 1;
            p->slot = i;
            p->address = *address;
            p->color[0] = 120; p->color[1] = 210; p->color[2] = 90;
            p->health = 100;
            p->legsAnim = -1;
            p->torsoAnim = -1;
            Race_Copy( p->map, sizeof( p->map ), "-" );
            Race_Copy( p->stageName, sizeof( p->stageName ), "-" );
            p->lastHeardMs = GetTickCount();
            return p;
        }
    }
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( !p->used ) {
            memset( p, 0, sizeof( *p ) );
            p->used = 1;
            p->slot = i;
            p->address = *address;
            p->color[0] = 120; p->color[1] = 210; p->color[2] = 90;
            p->health = 100;
            p->legsAnim = -1;
            p->torsoAnim = -1;
            Race_Copy( p->map, sizeof( p->map ), "-" );
            Race_Copy( p->stageName, sizeof( p->stageName ), "-" );
            p->lastHeardMs = GetTickCount();
            return p;
        }
    }
    return NULL;
}

void Race_SendText( const struct sockaddr_in *to, const char *fmt, ... ) {
    char text[RACE_PACKET_MAX];
    char packet[RACE_PACKET_MAX + 4];
    va_list args;
    int textLen;
    if ( !to ) return;
    if ( raceHost.socketId == INVALID_SOCKET ) return;
    va_start( args, fmt );
    vsnprintf( text, sizeof( text ), fmt, args );
    va_end( args );
    text[sizeof( text ) - 1] = '\0';
    textLen = (int)strlen( text );
    if ( textLen + 4 > (int)sizeof( packet ) ) textLen = (int)sizeof( packet ) - 4;
    packet[0] = (char)0xff; packet[1] = (char)0xff; packet[2] = (char)0xff; packet[3] = (char)0xff;
    memcpy( packet + 4, text, textLen );
    sendto( raceHost.socketId, packet, textLen + 4, 0, (const struct sockaddr *)to, sizeof( *to ) );
}

void Race_BroadcastText( const char *fmt, ... ) {
    char text[RACE_PACKET_MAX];
    va_list args;
    int i;
    va_start( args, fmt );
    vsnprintf( text, sizeof( text ), fmt, args );
    va_end( args );
    text[sizeof( text ) - 1] = '\0';
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( Race_PlayerBlocksReady( p ) ) Race_SendText( &p->address, "%s", text );
    }
}

static void Race_SendPlayerTo( const struct sockaddr_in *to, const racePlayer_t *p ) {
    if ( !p || !p->used ) return;
    Race_SendText( to,
        "srace player %d %d %s %d %d %d %s %d %d %d %d %.1f %.1f %.1f %.1f %.1f %d %d %d %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %.1f %.1f %.1f %.1f %d %d",
        raceHost.session, p->slot, p->nick,
        p->color[0], p->color[1], p->color[2], p->map[0] ? p->map : "-",
        p->loaded, p->started, p->finished, p->timeMs,
        p->x, p->y, p->z, p->yaw, p->speed,
        p->igtMs, p->stageTimeMs, p->stageProgress, p->stageName[0] ? p->stageName : "-",
        p->crouched, p->health, p->armor, p->weapon, p->ammo, p->clip,
        p->objectivesFound, p->objectivesTotal, p->zoneProgress, p->zoneTotal, p->paused,
        p->cheatFlags, p->inMenu, p->left, p->timedOut,
        p->legsAnim, p->torsoAnim, p->movementDir, p->eFlags,
        p->pitch, p->vx, p->vy, p->vz, p->groundEntityNum, p->animMovetype );
}

static void Race_SendRosterTo( const struct sockaddr_in *to ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        if ( raceHost.players[i].used ) Race_SendPlayerTo( to, &raceHost.players[i] );
    }
}

static void Race_BroadcastPlayer( const racePlayer_t *p ) {
    int target;
    if ( !p || !p->used ) return;
    for ( target = 0; target < raceHost.maxPlayers; ++target ) {
        racePlayer_t *to = &raceHost.players[target];
        if ( !Race_PlayerBlocksReady( to ) ) continue;
        if ( Race_SameAddress( &to->address, &p->address ) ) continue;
        Race_SendPlayerTo( &to->address, p );
    }
}

void Race_BroadcastRoster( void ) {
    int target, src;
    for ( target = 0; target < raceHost.maxPlayers; ++target ) {
        racePlayer_t *to = &raceHost.players[target];
        if ( !Race_PlayerBlocksReady( to ) ) continue;
        for ( src = 0; src < raceHost.maxPlayers; ++src ) {
            if ( raceHost.players[src].used ) Race_SendPlayerTo( &to->address, &raceHost.players[src] );
        }
    }
}

void Race_SendConfigTo( const struct sockaddr_in *to ) {
    Race_SendText( to, "srace config %d %d %d %d %d %d %d %s %d",
        raceHost.session, raceHost.mode, raceHost.mission, raceHost.percent100,
        raceHost.difficulty, raceHost.hl1Movement, raceHost.autoJump,
        raceHost.ilMap[0] ? raceHost.ilMap : "escape1", raceHost.antiCheat );
}

void Race_BroadcastConfig( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( Race_PlayerBlocksReady( p ) ) Race_SendConfigTo( &p->address );
    }
}

void Race_SendLoadTo( const struct sockaddr_in *to ) {
    Race_SendText( to, "srace load %d %d %d %d %d %d %d %s %s %d",
        raceHost.session, raceHost.mode, raceHost.mission, raceHost.percent100,
        raceHost.difficulty, raceHost.hl1Movement, raceHost.autoJump,
        raceHost.ilMap[0] ? raceHost.ilMap : "escape1",
        raceHost.targetMap[0] ? raceHost.targetMap : "escape1", raceHost.antiCheat );
}

static void Race_BroadcastLoad( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( Race_PlayerBlocksReady( p ) ) Race_SendLoadTo( &p->address );
    }
    raceHost.lastLoadBroadcastMs = GetTickCount();
}

int Race_CountdownRemaining( DWORD now ) {
    int remain;
    if ( raceHost.countdownMs <= 0 ) return 0;
    if ( raceHost.state != RACE_STATE_COUNTDOWN || !raceHost.countdownStartMs ) return raceHost.countdownMs;
    remain = raceHost.countdownMs - (int)( now - raceHost.countdownStartMs );
    if ( remain < 0 ) remain = 0;
    if ( remain > raceHost.countdownMs ) remain = raceHost.countdownMs;
    return remain;
}

static void Race_SendCountdownTo( const struct sockaddr_in *to ) {
    DWORD now = GetTickCount();
    int remaining = Race_CountdownRemaining( now );
    Race_SendText( to, "srace countdown %d %d %d", raceHost.session, raceHost.countdownMs, remaining );
}

static void Race_BroadcastCountdown( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( Race_PlayerBlocksReady( p ) ) Race_SendCountdownTo( &p->address );
    }
    raceHost.lastCountdownBroadcastMs = GetTickCount();
}

void Race_SendStartTo( const struct sockaddr_in *to ) {
    Race_SendText( to, "srace start %d", raceHost.session );
}

static void Race_SendStopTo( const struct sockaddr_in *to ) {
    Race_SendText( to, "srace stop %d", raceHost.session );
}

static void Race_BroadcastStart( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( Race_PlayerBlocksReady( p ) ) Race_SendStartTo( &p->address );
    }
    raceHost.lastStartBroadcastMs = GetTickCount();
}

static void Race_BroadcastStop( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( Race_PlayerBlocksReady( p ) ) Race_SendStopTo( &p->address );
    }
}

static void Race_SendControlStateTo( const struct sockaddr_in *to ) {
    Race_SendConfigTo( to );
    if ( raceHost.state == RACE_STATE_LOADING || raceHost.state == RACE_STATE_COUNTDOWN ) {
        Race_SendLoadTo( to );
    }
    if ( raceHost.state == RACE_STATE_COUNTDOWN ) {
        Race_SendCountdownTo( to );
    }
    if ( raceHost.state == RACE_STATE_RACING || raceHost.state == RACE_STATE_FINISHED ) {
        Race_SendStartTo( to );
    }
}

/* ---------- tokenize ---------- */
int Race_Tokenize( char *text, char **tokens, int maxTokens ) {
    int count = 0;
    char *c = text;
    while ( *c && count < maxTokens ) {
        while ( *c == ' ' || *c == '\t' || *c == '\r' || *c == '\n' ) c++;
        if ( !*c ) break;
        tokens[count++] = c;
        while ( *c && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n' ) c++;
        if ( *c ) *c++ = '\0';
    }
    return count;
}

static int Race_ArgInt( char **tokens, int n, int idx, int fallback ) {
    return ( idx < n ) ? atoi( tokens[idx] ) : fallback;
}

static float Race_ArgFloat( char **tokens, int n, int idx, float fallback ) {
    return ( idx < n ) ? (float)atof( tokens[idx] ) : fallback;
}

static int Race_ClampInt( int value, int minValue, int maxValue ) {
    if ( value < minValue ) return minValue;
    if ( value > maxValue ) return maxValue;
    return value;
}

static const char *Race_ArgText( char **tokens, int n, int idx, const char *fallback ) {
    return ( idx < n && tokens[idx][0] ) ? tokens[idx] : fallback;
}

static int Race_QueuePositionByAddress( const struct sockaddr_in *address ) {
    int i, pos = 0;
    for ( i = 0; i < RACE_QUEUE_MAX; ++i ) {
        raceQueueEntry_t *q = &raceHost.queue[i];
        if ( !q->used ) continue;
        pos++;
        if ( Race_SameAddress( &q->address, address ) ) return pos;
    }
    return 0;
}

static raceQueueEntry_t *Race_FindQueueByAddress( const struct sockaddr_in *address ) {
    int i;
    for ( i = 0; i < RACE_QUEUE_MAX; ++i ) {
        if ( raceHost.queue[i].used && Race_SameAddress( &raceHost.queue[i].address, address ) ) return &raceHost.queue[i];
    }
    return NULL;
}

static raceQueueEntry_t *Race_AddQueueEntry( const struct sockaddr_in *address, const char *nick, int r, int g, int b ) {
    int i;
    raceQueueEntry_t *q = Race_FindQueueByAddress( address );
    if ( !q ) {
        for ( i = 0; i < RACE_QUEUE_MAX; ++i ) {
            if ( !raceHost.queue[i].used ) {
                q = &raceHost.queue[i];
                memset( q, 0, sizeof( *q ) );
                q->used = 1;
                q->requestMs = GetTickCount();
                break;
            }
        }
    }
    if ( !q ) return NULL;
    q->address = *address;
    Race_Copy( q->nick, sizeof( q->nick ), nick && nick[0] ? nick : "Runner" );
    q->color[0] = Race_ClampInt( r, 0, 255 );
    q->color[1] = Race_ClampInt( g, 0, 255 );
    q->color[2] = Race_ClampInt( b, 0, 255 );
    return q;
}

static void Race_RemoveQueueEntry( int index ) {
    int i;
    if ( index < 0 || index >= RACE_QUEUE_MAX ) return;
    for ( i = index; i < RACE_QUEUE_MAX - 1; ++i ) raceHost.queue[i] = raceHost.queue[i + 1];
    memset( &raceHost.queue[RACE_QUEUE_MAX - 1], 0, sizeof( raceHost.queue[RACE_QUEUE_MAX - 1] ) );
}

static void Race_RemoveQueueEntriesByAddress( const struct sockaddr_in *address ) {
    int i = 0;
    if ( !address ) return;
    while ( i < RACE_QUEUE_MAX ) {
        raceQueueEntry_t *q = &raceHost.queue[i];
        if ( q->used && Race_SameAddress( &q->address, address ) ) Race_RemoveQueueEntry( i );
        else i++;
    }
}

static void Race_SendQueued( const struct sockaddr_in *to ) {
    int pos = Race_QueuePositionByAddress( to );
    Race_SendText( to, "srace queued %d %d", pos > 0 ? pos : Race_QueueCount(), raceHost.state );
}

static void Race_CheatFlagsText( int flags, char *out, size_t outSize ) {
    int wrote = 0;
    if ( !out || outSize == 0 ) return;
    out[0] = '\0';
    if ( flags & 1 ) wrote += snprintf( out + wrote, outSize - wrote, "%ssv_cheats", wrote ? ", " : "" );
    if ( flags & 2 ) wrote += snprintf( out + wrote, outSize - wrote, "%sgod", wrote ? ", " : "" );
    if ( flags & 4 ) wrote += snprintf( out + wrote, outSize - wrote, "%snoclip", wrote ? ", " : "" );
    if ( !out[0] ) Race_Copy( out, outSize, "none" );
    out[outSize - 1] = '\0';
}

static void Race_RecordRunHistory( int completed ) {
    raceRunHistory_t *run;
    DWORD now = GetTickCount();
    int slot, out = 0;
    if ( raceHost.currentRunRecorded ) return;
    if ( !raceHost.raceStartMs && raceHost.state != RACE_STATE_FINISHED ) return;
    run = &raceHost.runs[raceHost.runHead];
    memset( run, 0, sizeof( *run ) );
    run->used = 1;
    run->runId = raceHost.nextRunId++;
    run->session = raceHost.session;
    run->startTime = time( NULL );
    run->endTime = run->startTime;
    run->completed = completed ? 1 : 0;
    if ( raceHost.raceStartMs ) {
        run->durationMs = (int)( now - raceHost.raceStartMs );
        run->startTime = run->endTime - ( run->durationMs / 1000 );
    }
    run->mode = raceHost.mode;
    run->mission = raceHost.mission;
    run->percent100 = raceHost.percent100;
    run->difficulty = raceHost.difficulty;
    run->hl1Movement = raceHost.hl1Movement;
    run->autoJump = raceHost.autoJump;
    run->antiCheat = raceHost.antiCheat;
    Race_Copy( run->targetMap, sizeof( run->targetMap ), raceHost.targetMap[0] ? raceHost.targetMap : Race_ResolveStartMap() );
    for ( slot = 0; slot < raceHost.maxPlayers && out < RACE_MAX_PLAYERS; ++slot ) {
        racePlayer_t *p = &raceHost.players[slot];
        raceRunPlayerResult_t *r;
        if ( !p->used ) continue;
        r = &run->players[out++];
        Race_Copy( r->nick, sizeof( r->nick ), p->nick[0] ? p->nick : "Runner" );
        r->color[0] = p->color[0]; r->color[1] = p->color[1]; r->color[2] = p->color[2];
        Race_Copy( r->map, sizeof( r->map ), p->map[0] ? p->map : "-" );
        Race_Copy( r->stageName, sizeof( r->stageName ), p->stageName[0] ? p->stageName : "-" );
        r->slot = p->slot;
        r->finished = p->finished;
        r->timeMs = p->timeMs;
        r->igtMs = p->igtMs;
        r->stageProgress = p->stageProgress;
        r->objectivesFound = p->objectivesFound;
        r->objectivesTotal = p->objectivesTotal;
        r->zoneProgress = p->zoneProgress;
        r->zoneTotal = p->zoneTotal;
        r->cheatFlags = p->cheatFlags;
        r->timedOut = p->timedOut;
        r->left = p->left;
        r->kicked = p->kicked;
    }
    run->playerCount = out;
    raceHost.runHead = ( raceHost.runHead + 1 ) % RACE_RUN_HISTORY_MAX;
    if ( raceHost.runCount < RACE_RUN_HISTORY_MAX ) raceHost.runCount++;
    raceHost.currentRunRecorded = 1;
    Race_GuiLogSrv( "saved run #%d to history (%d player%s)", run->runId, run->playerCount, run->playerCount == 1 ? "" : "s" );
}

static void Race_WriteCsvText( FILE *f, const char *text ) {
    const char *p = text ? text : "";
    fputc( '"', f );
    while ( *p ) {
        if ( *p == '"' ) fputc( '"', f );
        fputc( *p++, f );
    }
    fputc( '"', f );
}

int Race_ExportRunsCsv( const char *path, char *status, size_t statusSize ) {
    FILE *f;
    int i, p, wrote = 0;
    if ( status && statusSize ) status[0] = '\0';
    if ( !path || !path[0] ) return 0;
    f = fopen( path, "w" );
    if ( !f ) {
        if ( status && statusSize ) snprintf( status, statusSize, "could not write %s", path );
        return 0;
    }
    fprintf( f, "run_id,session,completed,target_map,mode,mission,difficulty,percent100,hl1,autojump,player_slot,nick,finished,rgt_ms,igt_ms,map,stage,stage_progress,objectives,zones,cheat_flags,timed_out,left,kicked\n" );
    for ( i = 0; i < RACE_RUN_HISTORY_MAX; ++i ) {
        raceRunHistory_t *run = &raceHost.runs[i];
        if ( !run->used ) continue;
        for ( p = 0; p < run->playerCount && p < RACE_MAX_PLAYERS; ++p ) {
            raceRunPlayerResult_t *r = &run->players[p];
            fprintf( f, "%d,%d,%d,", run->runId, run->session, run->completed );
            Race_WriteCsvText( f, run->targetMap );
            fprintf( f, ",%d,%d,%d,%d,%d,%d,%d,", run->mode, run->mission, run->difficulty, run->percent100, run->hl1Movement, run->autoJump, r->slot );
            Race_WriteCsvText( f, r->nick );
            fprintf( f, ",%d,%d,%d,", r->finished, r->timeMs, r->igtMs );
            Race_WriteCsvText( f, r->map );
            fputc( ',', f );
            Race_WriteCsvText( f, r->stageName );
            fprintf( f, ",%d,%d/%d,%d/%d,%d,%d,%d,%d\n",
                r->stageProgress, r->objectivesFound, r->objectivesTotal, r->zoneProgress, r->zoneTotal,
                r->cheatFlags, r->timedOut, r->left, r->kicked );
            wrote++;
        }
    }
    fclose( f );
    if ( status && statusSize ) snprintf( status, statusSize, "exported %d rows to %s", wrote, path );
    return 1;
}

static void Race_WriteJsonText( FILE *f, const char *text ) {
    const unsigned char *p = (const unsigned char *)( text ? text : "" );
    fputc( '"', f );
    while ( *p ) {
        if ( *p == '"' || *p == '\\' ) {
            fputc( '\\', f );
            fputc( *p, f );
        } else if ( *p == '\n' ) {
            fputs( "\\n", f );
        } else if ( *p == '\r' ) {
            fputs( "\\r", f );
        } else if ( *p == '\t' ) {
            fputs( "\\t", f );
        } else if ( *p < 32 ) {
            fprintf( f, "\\u%04x", *p );
        } else {
            fputc( *p, f );
        }
        p++;
    }
    fputc( '"', f );
}

int Race_ExportRunsJson( const char *path, char *status, size_t statusSize ) {
    FILE *f;
    int i, p, wroteRuns = 0;
    if ( status && statusSize ) status[0] = '\0';
    if ( !path || !path[0] ) return 0;
    f = fopen( path, "w" );
    if ( !f ) {
        if ( status && statusSize ) snprintf( status, statusSize, "could not write %s", path );
        return 0;
    }
    fprintf( f, "{\n  \"runs\": [\n" );
    for ( i = 0; i < RACE_RUN_HISTORY_MAX; ++i ) {
        raceRunHistory_t *run = &raceHost.runs[i];
        if ( !run->used ) continue;
        if ( wroteRuns ) fprintf( f, ",\n" );
        fprintf( f, "    {\"run_id\":%d,\"session\":%d,\"completed\":%d,\"target_map\":", run->runId, run->session, run->completed );
        Race_WriteJsonText( f, run->targetMap );
        fprintf( f, ",\"mode\":%d,\"mission\":%d,\"difficulty\":%d,\"percent100\":%d,\"hl1\":%d,\"autojump\":%d,\"players\":[",
            run->mode, run->mission, run->difficulty, run->percent100, run->hl1Movement, run->autoJump );
        for ( p = 0; p < run->playerCount && p < RACE_MAX_PLAYERS; ++p ) {
            raceRunPlayerResult_t *r = &run->players[p];
            if ( p ) fprintf( f, "," );
            fprintf( f, "{\"slot\":%d,\"nick\":", r->slot );
            Race_WriteJsonText( f, r->nick );
            fprintf( f, ",\"finished\":%d,\"rgt_ms\":%d,\"igt_ms\":%d,\"map\":", r->finished, r->timeMs, r->igtMs );
            Race_WriteJsonText( f, r->map );
            fprintf( f, ",\"stage\":" );
            Race_WriteJsonText( f, r->stageName );
            fprintf( f, ",\"stage_progress\":%d,\"objectives_found\":%d,\"objectives_total\":%d,\"zone_progress\":%d,\"zone_total\":%d,\"cheat_flags\":%d,\"timed_out\":%d,\"left\":%d,\"kicked\":%d}",
                r->stageProgress, r->objectivesFound, r->objectivesTotal, r->zoneProgress, r->zoneTotal,
                r->cheatFlags, r->timedOut, r->left, r->kicked );
        }
        fprintf( f, "]}" );
        wroteRuns++;
    }
    fprintf( f, "\n  ]\n}\n" );
    fclose( f );
    if ( status && statusSize ) snprintf( status, statusSize, "exported %d runs to %s", wroteRuns, path );
    return 1;
}

/* ---------- protocol handlers ---------- */
static void Race_HandleDiscover( const struct sockaddr_in *from, char **t, int n ) {
    int version = Race_ArgInt( t, n, 2, 0 );
    if ( version != RACE_PROTO_VERSION ) return;
    Race_SendText( from, "srace found %d %d %s %d %d %d %d %d %d %d %d %d %d %s %d %d %d",
        RACE_PROTO_VERSION, raceHost.session, raceHost.hostName, raceHost.port, raceHost.state,
        Race_PlayerCount(), raceHost.maxPlayers, raceHost.mode, raceHost.mission, raceHost.percent100,
        raceHost.difficulty, raceHost.hl1Movement, raceHost.autoJump,
        raceHost.ilMap[0] ? raceHost.ilMap : "escape1", raceHost.antiCheat,
        raceHost.privateLobby && raceHost.password[0] ? 1 : 0, Race_QueueCount() );
}

static void Race_HandleHello( const struct sockaddr_in *from, char **t, int n ) {
    racePlayer_t *p;
    raceQueueEntry_t *q;
    char nick[RACE_NICK_MAX];
    char oldNick[RACE_NICK_MAX];
    char pass[RACE_PASSWORD_MAX];
    int wasKnown, wasGone, announceJoin;
    int version = Race_ArgInt( t, n, 2, 0 );
    if ( version != RACE_PROTO_VERSION ) {
        Race_SendText( from, "srace reject Version_mismatch" );
        return;
    }
    p = Race_FindPlayerByAddress( from );
    wasKnown = p ? 1 : 0;
    wasGone = p ? ( p->left || p->kicked || p->timedOut ) : 0;
    if ( p ) Race_Copy( oldNick, sizeof( oldNick ), p->nick );
    else oldNick[0] = '\0';
    /* If the same address re-hellos after a kick, only accept it in lobby. */
    if ( p && p->kicked && raceHost.state != RACE_STATE_LOBBY ) {
        Race_SendText( from, "srace reject You_were_kicked" );
        return;
    }
    Race_SanitizeToken( Race_ArgText( t, n, 3, "Runner" ), nick, sizeof( nick ) );
    Race_SanitizeOptionalToken( Race_ArgText( t, n, 9, "" ), pass, sizeof( pass ) );
    if ( ( !p || wasGone ) && raceHost.privateLobby && raceHost.password[0] && ( !pass[0] || strcmp( pass, raceHost.password ) ) ) {
        Race_SendText( from, "srace reject Wrong_password" );
        Race_GuiLog( "! rejected wrong password from %s (%s)", Race_AddrToString( from ), nick );
        return;
    }
    if ( ( !p || wasGone ) && raceHost.state != RACE_STATE_LOBBY ) {
        if ( raceHost.queueEnabled ) {
            q = Race_AddQueueEntry( from, nick, Race_ArgInt( t, n, 4, 120 ), Race_ArgInt( t, n, 5, 210 ), Race_ArgInt( t, n, 6, 90 ) );
            if ( q ) {
                DWORD now = GetTickCount();
                Race_SendQueued( from );
                if ( !q->lastNotifyMs || (DWORD)( now - q->lastNotifyMs ) > 5000 ) {
                    Race_GuiLogSrv( "queued %s at position %d during %s", q->nick, Race_QueuePositionByAddress( from ), Race_StateName( raceHost.state ) );
                }
                q->lastNotifyMs = now;
            } else {
                Race_SendText( from, "srace reject Queue_full" );
            }
        } else {
            Race_SendText( from, "srace reject Run_in_progress_wait_for_next_lobby" );
            Race_GuiLogSrv( "join blocked during %s: %s", Race_StateName( raceHost.state ), Race_AddrToString( from ) );
        }
        return;
    }
    /* Recycle a previously-vacated slot for this address, or alloc fresh. */
    if ( p && ( p->left || p->kicked || p->timedOut ) ) {
        int slot = p->slot;
        memset( p, 0, sizeof( *p ) );
        p->used = 1;
        p->slot = slot;
        p->color[0] = 120; p->color[1] = 210; p->color[2] = 90;
        p->health = 100;
        p->legsAnim = -1;
        p->torsoAnim = -1;
        Race_Copy( p->map, sizeof( p->map ), "-" );
        Race_Copy( p->stageName, sizeof( p->stageName ), "-" );
    }
    if ( !p ) p = Race_AllocPlayer( from );
    if ( !p ) {
        Race_SendText( from, "srace reject Lobby_full" );
        return;
    }
    Race_Copy( p->nick, sizeof( p->nick ), nick );
    p->color[0] = Race_ClampInt( Race_ArgInt( t, n, 4, p->color[0] ), 0, 255 );
    p->color[1] = Race_ClampInt( Race_ArgInt( t, n, 5, p->color[1] ), 0, 255 );
    p->color[2] = Race_ClampInt( Race_ArgInt( t, n, 6, p->color[2] ), 0, 255 );
    p->address = *from;
    p->left = 0;
    p->kicked = 0;
    p->timedOut = 0;
    p->lastHeardMs = GetTickCount();
    Race_RemoveQueueEntriesByAddress( from );

    Race_SendText( from, "srace welcome %d %d %d %d %d %d %d %d %s %s %d %d",
        raceHost.session, p->slot, raceHost.mode, raceHost.mission, raceHost.percent100,
        raceHost.difficulty, raceHost.hl1Movement, raceHost.autoJump,
        raceHost.ilMap[0] ? raceHost.ilMap : "escape1",
        raceHost.targetMap[0] ? raceHost.targetMap : "-", raceHost.state, raceHost.antiCheat );
    Race_SendRosterTo( from );
    Race_SendControlStateTo( from );
    Race_BroadcastRoster();

    announceJoin = ( !wasKnown || wasGone || _stricmp( oldNick, p->nick ) != 0 ) ? 1 : 0;
    if ( announceJoin ) Race_GuiLog( "+ joined slot %d  %s  (%s)", p->slot, p->nick, Race_AddrToString( from ) );
    Race_GuiRefresh();
}

static void Race_PromoteQueuedPlayers( void ) {
    int i = 0;
    int promoted = 0;
    while ( i < RACE_QUEUE_MAX ) {
        raceQueueEntry_t *q = &raceHost.queue[i];
        racePlayer_t *p;
        if ( !q->used ) {
            i++;
            continue;
        }
        if ( Race_FindPlayerByAddress( &q->address ) ) {
            Race_RemoveQueueEntry( i );
            continue;
        }
        p = Race_AllocPlayer( &q->address );
        if ( !p ) break;
        Race_Copy( p->nick, sizeof( p->nick ), q->nick[0] ? q->nick : "Runner" );
        p->color[0] = q->color[0];
        p->color[1] = q->color[1];
        p->color[2] = q->color[2];
        p->left = 0;
        p->kicked = 0;
        p->timedOut = 0;
        p->lastHeardMs = GetTickCount();
        Race_SendText( &p->address, "srace welcome %d %d %d %d %d %d %d %d %s %s %d %d",
            raceHost.session, p->slot, raceHost.mode, raceHost.mission, raceHost.percent100,
            raceHost.difficulty, raceHost.hl1Movement, raceHost.autoJump,
            raceHost.ilMap[0] ? raceHost.ilMap : "escape1",
            raceHost.targetMap[0] ? raceHost.targetMap : "-", raceHost.state, raceHost.antiCheat );
        Race_SendRosterTo( &p->address );
        Race_SendControlStateTo( &p->address );
        Race_GuiLog( "+ promoted queued slot %d  %s", p->slot, p->nick );
        Race_RemoveQueueEntriesByAddress( &p->address );
        promoted++;
    }
    if ( promoted ) {
        Race_BroadcastRoster();
        Race_GuiLogSrv( "promoted %d queued player%s", promoted, promoted == 1 ? "" : "s" );
    }
}

static void Race_HandleState( const struct sockaddr_in *from, char **t, int n ) {
    racePlayer_t *p;
    char oldMap[RACE_MAP_MAX];
    char cheatText[96];
    int oldCheatFlags;
    int oldInMenu;
    int session = Race_ArgInt( t, n, 2, 0 );
    int slot = Race_ArgInt( t, n, 3, -1 );
    if ( session != raceHost.session ) return;
    p = Race_FindPlayerBySlot( slot );
    if ( !p || !Race_SameAddress( &p->address, from ) ) return;
    /* If the player was kicked, ignore further state, but nudge them with
       another reject every second so the client's reject handler can fire. */
    if ( p->kicked ) {
        DWORD now = GetTickCount();
        if ( (DWORD)( now - p->lastKickRejectMs ) > 1000 ) {
            Race_SendText( from, "srace reject You_have_been_kicked_from_the_race" );
            p->lastKickRejectMs = now;
        }
        return;
    }
    if ( p->left || p->timedOut ) return;
    Race_Copy( oldMap, sizeof( oldMap ), p->map );
    oldCheatFlags = p->cheatFlags;
    oldInMenu = p->inMenu;
    Race_SanitizeToken( Race_ArgText( t, n, 4, p->nick ), p->nick, sizeof( p->nick ) );
    p->color[0] = Race_ClampInt( Race_ArgInt( t, n, 5, p->color[0] ), 0, 255 );
    p->color[1] = Race_ClampInt( Race_ArgInt( t, n, 6, p->color[1] ), 0, 255 );
    p->color[2] = Race_ClampInt( Race_ArgInt( t, n, 7, p->color[2] ), 0, 255 );
    Race_SanitizeToken( Race_ArgText( t, n, 8, "-" ), p->map, sizeof( p->map ) );
    p->loaded = Race_ArgInt( t, n, 9, p->loaded ) ? 1 : 0;
    p->started = Race_ArgInt( t, n, 10, p->started ) ? 1 : 0;
    p->finished = Race_ArgInt( t, n, 11, p->finished ) ? 1 : 0;
    p->timeMs = Race_ClampInt( Race_ArgInt( t, n, 12, p->timeMs ), 0, 24 * 60 * 60 * 1000 );
    p->x = Race_ArgFloat( t, n, 13, p->x );
    p->y = Race_ArgFloat( t, n, 14, p->y );
    p->z = Race_ArgFloat( t, n, 15, p->z );
    p->yaw = Race_ArgFloat( t, n, 16, p->yaw );
    p->speed = Race_ArgFloat( t, n, 17, p->speed );
    p->igtMs = Race_ClampInt( Race_ArgInt( t, n, 18, p->igtMs ), 0, 24 * 60 * 60 * 1000 );
    p->stageTimeMs = Race_ClampInt( Race_ArgInt( t, n, 19, p->stageTimeMs ), 0, 24 * 60 * 60 * 1000 );
    p->stageProgress = Race_ClampInt( Race_ArgInt( t, n, 20, p->stageProgress ), 0, 1000 );
    Race_SanitizeToken( Race_ArgText( t, n, 21, "-" ), p->stageName, sizeof( p->stageName ) );
    p->crouched = Race_ArgInt( t, n, 22, p->crouched ) ? 1 : 0;
    p->health = Race_ClampInt( Race_ArgInt( t, n, 23, p->health ), 0, 999 );
    p->armor = Race_ClampInt( Race_ArgInt( t, n, 24, p->armor ), 0, 999 );
    p->weapon = Race_ClampInt( Race_ArgInt( t, n, 25, p->weapon ), 0, 255 );
    p->ammo = Race_ClampInt( Race_ArgInt( t, n, 26, p->ammo ), 0, 9999 );
    p->clip = Race_ClampInt( Race_ArgInt( t, n, 27, p->clip ), 0, 9999 );
    p->objectivesFound = Race_ClampInt( Race_ArgInt( t, n, 28, p->objectivesFound ), 0, 999 );
    p->objectivesTotal = Race_ClampInt( Race_ArgInt( t, n, 29, p->objectivesTotal ), 0, 999 );
    if ( p->objectivesTotal > 0 && p->objectivesFound > p->objectivesTotal ) p->objectivesFound = p->objectivesTotal;
    p->zoneProgress = Race_ClampInt( Race_ArgInt( t, n, 30, p->zoneProgress ), 0, 9999 );
    p->zoneTotal = Race_ClampInt( Race_ArgInt( t, n, 31, p->zoneTotal ), 0, 9999 );
    if ( p->zoneTotal > 0 && p->zoneProgress > p->zoneTotal ) p->zoneProgress = p->zoneTotal;
    p->paused = Race_ArgInt( t, n, 32, p->paused ) ? 1 : 0;
    p->cheatFlags = Race_ArgInt( t, n, 33, p->cheatFlags ) & 7;
    p->inMenu = Race_ArgInt( t, n, 34, p->inMenu ) ? 1 : 0;
    /* IMPORTANT: do NOT trust client-supplied left/timedOut. Host owns these. */
    p->legsAnim = Race_ArgInt( t, n, 37, p->legsAnim );
    p->torsoAnim = Race_ArgInt( t, n, 38, p->torsoAnim );
    p->movementDir = Race_ArgInt( t, n, 39, p->movementDir );
    p->eFlags = Race_ArgInt( t, n, 40, p->eFlags );
    p->pitch = Race_ArgFloat( t, n, 41, p->pitch );
    p->vx = Race_ArgFloat( t, n, 42, p->vx );
    p->vy = Race_ArgFloat( t, n, 43, p->vy );
    p->vz = Race_ArgFloat( t, n, 44, p->vz );
    p->groundEntityNum = Race_ArgInt( t, n, 45, p->groundEntityNum );
    p->animMovetype = Race_ArgInt( t, n, 46, p->animMovetype );
    p->lastHeardMs = GetTickCount();

    if ( raceHost.state == RACE_STATE_RACING && raceHost.pauseAlertMs > 0 && ( p->paused || p->inMenu ) ) {
        if ( !p->pausedSinceMs ) p->pausedSinceMs = p->lastHeardMs;
        if ( !p->pauseAlerted && (DWORD)( p->lastHeardMs - p->pausedSinceMs ) >= (DWORD)raceHost.pauseAlertMs ) {
            Race_GuiLog( "! pause alert: %s paused/in menu for %ds", p->nick, raceHost.pauseAlertMs / 1000 );
            p->pauseAlerted = 1;
            p->lastPauseAlertMs = p->lastHeardMs;
        }
    } else {
        p->pausedSinceMs = 0;
        p->pauseAlerted = 0;
    }

    if ( ( raceHost.state == RACE_STATE_LOADING || raceHost.state == RACE_STATE_COUNTDOWN ) &&
         p->loaded && raceHost.targetMap[0] && p->map[0] && strcmp( p->map, "-" ) &&
         _stricmp( p->map, raceHost.targetMap ) && !p->mapMismatchAlerted ) {
        Race_GuiLog( "! map mismatch: %s loaded %s, expected %s", p->nick, p->map, raceHost.targetMap );
        p->mapMismatchAlerted = 1;
    }

    if ( oldMap[0] && p->map[0] && strcmp( oldMap, "-" ) && strcmp( p->map, "-" ) && _stricmp( oldMap, p->map ) ) {
        Race_GuiLogSrv( "%s changed map: %s -> %s", p->nick, oldMap, p->map );
    }
    if ( oldCheatFlags != p->cheatFlags ) {
        Race_CheatFlagsText( p->cheatFlags, cheatText, sizeof( cheatText ) );
        if ( p->cheatFlags ) Race_GuiLog( "! anti-cheat: %s flagged %s", p->nick, cheatText );
        else Race_GuiLogSrv( "%s cheat flags cleared", p->nick );
    }
    if ( oldInMenu != p->inMenu ) {
        Race_GuiLogSrv( "%s %s menu/pause", p->nick, p->inMenu ? "entered" : "left" );
    }
    if ( (DWORD)( p->lastHeardMs - p->lastBroadcastMs ) >= RACE_PLAYER_BROADCAST_MS ) {
        Race_BroadcastPlayer( p );
        p->lastBroadcastMs = p->lastHeardMs;
    }
}

static void Race_HandleLeave( const struct sockaddr_in *from, char **t, int n ) {
    racePlayer_t *p;
    int session = Race_ArgInt( t, n, 2, 0 );
    int slot = Race_ArgInt( t, n, 3, -1 );
    if ( session != raceHost.session ) return;
    p = Race_FindPlayerBySlot( slot );
    if ( !p || !Race_SameAddress( &p->address, from ) ) return;
    p->left = 1;
    p->inMenu = 1;
    p->lastHeardMs = GetTickCount();
    Race_GuiLog( "- left slot %d  %s", p->slot, p->nick );
    Race_BroadcastRoster();
    Race_GuiRefresh();
}

static void Race_HandleChat( const struct sockaddr_in *from, char **t, int n ) {
    racePlayer_t *p;
    int session = Race_ArgInt( t, n, 2, 0 );
    int slot = Race_ArgInt( t, n, 3, -1 );
    char restBuf[RACE_PACKET_MAX];
    int i;
    if ( session != raceHost.session ) return;
    p = Race_FindPlayerBySlot( slot );
    if ( !p || !Race_SameAddress( &p->address, from ) ) return;
    if ( !Race_PlayerBlocksReady( p ) ) return;
    restBuf[0] = '\0';
    for ( i = 5; i < n; ++i ) {
        if ( restBuf[0] ) strncat( restBuf, " ", sizeof( restBuf ) - strlen( restBuf ) - 1 );
        strncat( restBuf, t[i], sizeof( restBuf ) - strlen( restBuf ) - 1 );
    }
    Race_GuiLog( "<%s> %s", p->nick, restBuf );
    Race_BroadcastText( "srace chat %d %d %s %s", raceHost.session, slot, p->nick, restBuf );
}

static void Race_HandleEvent( const struct sockaddr_in *from, char **t, int n ) {
    racePlayer_t *p;
    char detail[RACE_PACKET_MAX];
    int i;
    int session = Race_ArgInt( t, n, 2, 0 );
    int slot = Race_ArgInt( t, n, 3, -1 );
    const char *kind = Race_ArgText( t, n, 4, "event" );
    if ( session != raceHost.session ) return;
    p = Race_FindPlayerBySlot( slot );
    if ( !p || !Race_SameAddress( &p->address, from ) ) return;
    if ( !Race_PlayerBlocksReady( p ) ) return;
    detail[0] = '\0';
    for ( i = 5; i < n; ++i ) {
        if ( detail[0] ) strncat( detail, " ", sizeof( detail ) - strlen( detail ) - 1 );
        strncat( detail, t[i], sizeof( detail ) - strlen( detail ) - 1 );
    }
    detail[sizeof( detail ) - 1] = '\0';
    p->lastHeardMs = GetTickCount();
    if ( !_stricmp( kind, "cheat" ) ) {
        Race_GuiLog( "! anti-cheat event: %s tried/triggered %s", p->nick, detail[0] ? detail : "protected command" );
    } else {
        Race_GuiLogSrv( "event from %s: %s %s", p->nick, kind, detail );
    }
}

void Race_ProcessPacket( char *packetText, int packetLen, const struct sockaddr_in *from ) {
    char *tokens[RACE_MAX_TOKENS];
    int n;
    if ( packetLen >= 4 && (unsigned char)packetText[0] == 0xff && (unsigned char)packetText[1] == 0xff &&
         (unsigned char)packetText[2] == 0xff && (unsigned char)packetText[3] == 0xff ) {
        packetText += 4;
        packetLen -= 4;
    }
    packetText[packetLen] = '\0';
    n = Race_Tokenize( packetText, tokens, RACE_MAX_TOKENS );
    if ( n < 2 ) return;
    if ( _stricmp( tokens[0], "srace" ) ) return;
    if ( !_stricmp( tokens[1], "discover" ) ) Race_HandleDiscover( from, tokens, n );
    else if ( !_stricmp( tokens[1], "hello" ) ) Race_HandleHello( from, tokens, n );
    else if ( !_stricmp( tokens[1], "state" ) ) Race_HandleState( from, tokens, n );
    else if ( !_stricmp( tokens[1], "leave" ) ) Race_HandleLeave( from, tokens, n );
    else if ( !_stricmp( tokens[1], "chat" ) ) Race_HandleChat( from, tokens, n );
    else if ( !_stricmp( tokens[1], "event" ) ) Race_HandleEvent( from, tokens, n );
}

void Race_PollNetwork( void ) {
    int packets = 0;
    if ( !Race_HostIsRunning() ) return;
    while ( packets < RACE_MAX_PACKETS_PER_POLL ) {
        fd_set readSet;
        struct timeval tv;
        char packet[RACE_PACKET_MAX + 1];
        struct sockaddr_in from;
        int fromLen = sizeof( from );
        FD_ZERO( &readSet );
        FD_SET( raceHost.socketId, &readSet );
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if ( select( 0, &readSet, NULL, NULL, &tv ) <= 0 ) break;
        int packetLen = recvfrom( raceHost.socketId, packet, RACE_PACKET_MAX, 0, (struct sockaddr *)&from, &fromLen );
        if ( packetLen <= 0 ) break;
        if ( packetLen >= RACE_PACKET_MAX ) packetLen = RACE_PACKET_MAX - 1;
        Race_ProcessPacket( packet, packetLen, &from );
        packets++;
    }
}

/* ---------- state machine ---------- */
const char *Race_ResolveStartMap( void ) {
    int i;
    if ( raceHost.mode == 2 ) {
        return raceHost.ilMap[0] ? raceHost.ilMap : "escape1";
    }
    if ( raceHost.mode == 1 ) {
        for ( i = 0; i < raceChapterCount; ++i ) {
            if ( raceChapters[i].chapter == raceHost.mission ) return raceChapters[i].startMap;
        }
    }
    return "escape1";
}

static void Race_BeginRun( DWORD now ) {
    raceHost.state = RACE_STATE_RACING;
    raceHost.raceStartMs = now;
    raceHost.currentRunRecorded = 0;
    Race_BroadcastStart();
    Race_GuiLogSrv( "race STARTED" );
}

void Race_ResetLobby( void ) {
    int i;
    if ( !Race_HostIsRunning() ) {
        Race_GuiLogSrv( "cannot reset: host is offline" );
        return;
    }
    raceHost.state = RACE_STATE_LOBBY;
    raceHost.session = (int)( time( NULL ) & 0x7fffffff );
    raceHost.countdownStartMs = 0;
    raceHost.raceStartMs = 0;
    raceHost.currentRunRecorded = 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( !p->used ) continue;
        /* Truly forget kicked/left/timedOut slots so the address frees up. */
        if ( p->kicked || p->left || p->timedOut ) {
            memset( p, 0, sizeof( *p ) );
            continue;
        }
        p->loaded = 0;
        p->started = 0;
        p->finished = 0;
        p->timeMs = 0;
    }
    Race_PromoteQueuedPlayers();
    Race_BroadcastConfig();
    Race_BroadcastRoster();
    Race_GuiLogSrv( "lobby reset (session %d)", raceHost.session );
}

int Race_StartRace( void ) {
    int countdownSec;
    const char *targetMap;
    int i;
    if ( !Race_HostIsRunning() ) {
        Race_GuiLogSrv( "cannot start race: host is offline" );
        return 0;
    }
    if ( raceHost.state != RACE_STATE_LOBBY ) {
        Race_GuiLogSrv( "cannot start: already in %s", Race_StateName( raceHost.state ) );
        return 0;
    }
    if ( Race_PlayerCount() < 1 ) {
        Race_GuiLogSrv( "cannot start: no players in lobby" );
        return 0;
    }
    if ( raceHost.autoReadyCheck ) {
        DWORD now = GetTickCount();
        for ( i = 0; i < raceHost.maxPlayers; ++i ) {
            racePlayer_t *p = &raceHost.players[i];
            if ( !Race_PlayerBlocksReady( p ) ) continue;
            if ( p->cheatFlags ) {
                Race_GuiLog( "! ready check blocked start: %s has cheat flags", p->nick );
                return 0;
            }
            if ( p->lastHeardMs && (DWORD)( now - p->lastHeardMs ) > 10000 ) {
                Race_GuiLog( "! ready check blocked start: %s telemetry is stale", p->nick );
                return 0;
            }
        }
    }
    targetMap = Race_ResolveStartMap();
    Race_Copy( raceHost.targetMap, sizeof( raceHost.targetMap ), targetMap );

    countdownSec = gUiCountdownSec;
    if ( countdownSec < 1 ) countdownSec = 5;
    if ( countdownSec > 30 ) countdownSec = 30;
    raceHost.countdownMs = countdownSec * 1000;
    raceHost.countdownStartMs = 0;
    raceHost.raceStartMs = 0;
    raceHost.lastLoadBroadcastMs = 0;
    raceHost.lastCountdownBroadcastMs = 0;
    raceHost.lastStartBroadcastMs = 0;

    {
      for ( i = 0; i < raceHost.maxPlayers; ++i ) {
          if ( !raceHost.players[i].used ) continue;
          raceHost.players[i].loaded = 0;
          raceHost.players[i].started = 0;
          raceHost.players[i].finished = 0;
          raceHost.players[i].timeMs = 0;
          raceHost.players[i].pausedSinceMs = 0;
          raceHost.players[i].lastPauseAlertMs = 0;
          raceHost.players[i].pauseAlerted = 0;
          raceHost.players[i].mapMismatchAlerted = 0;
      }
    }
    raceHost.currentRunRecorded = 0;
    raceHost.state = RACE_STATE_LOADING;
    Race_BroadcastLoad();
    Race_GuiLogSrv( "loading %s (countdown %ds)", raceHost.targetMap, countdownSec );
    return 1;
}

void Race_StopRace( void ) {
    int i;
    DWORD now = GetTickCount();
    if ( !Race_HostIsRunning() ) {
        Race_GuiLogSrv( "already offline" );
        return;
    }
    if ( raceHost.state == RACE_STATE_LOBBY ) {
        Race_GuiLogSrv( "already in lobby" );
        return;
    }
    if ( raceHost.state == RACE_STATE_RACING || raceHost.state == RACE_STATE_FINISHED ) {
        Race_RecordRunHistory( raceHost.state == RACE_STATE_FINISHED );
    }
    raceHost.state = RACE_STATE_LOBBY;
    raceHost.countdownStartMs = 0;
    raceHost.raceStartMs = 0;
    raceHost.lastLoadBroadcastMs = 0;
    raceHost.lastCountdownBroadcastMs = 0;
    raceHost.lastStartBroadcastMs = 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        if ( !Race_PlayerBlocksReady( p ) ) continue;
        p->loaded = 0;
        p->started = 0;
        p->finished = 0;
        p->timeMs = 0;
        p->igtMs = 0;
        p->stageTimeMs = 0;
        p->paused = 0;
        p->inMenu = 0;
        p->pausedSinceMs = 0;
        p->pauseAlerted = 0;
        p->mapMismatchAlerted = 0;
        p->lastHeardMs = now;
    }
    Race_BroadcastStop();
    Race_BroadcastConfig();
    Race_BroadcastRoster();
    Race_BroadcastText( "srace chat %d -1 %s Race stopped - back to lobby", raceHost.session, raceHost.hostName );
    Race_GuiLogSrv( "race STOPPED -> lobby" );
}

void Race_Tick( void ) {
    DWORD now = GetTickCount();
    int i;
    if ( !Race_HostIsRunning() ) return;

    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        DWORD timeout = ( raceHost.state == RACE_STATE_LOADING || raceHost.state == RACE_STATE_COUNTDOWN )
                        ? RACE_LOAD_TIMEOUT_MS : RACE_TIMEOUT_MS;
        if ( !p->used || p->left || p->timedOut || p->kicked ) continue;
        if ( (DWORD)( now - p->lastHeardMs ) > timeout ) {
            p->timedOut = 1;
            p->inMenu = 1;
            p->loaded = 1;
            p->started = 0;
            p->paused = 0;
            Race_GuiLog( "! timed out slot %d %s", p->slot, p->nick );
            Race_BroadcastRoster();
        }
    }

    if ( raceHost.state == RACE_STATE_LOADING ) {
        if ( (DWORD)( now - raceHost.lastLoadBroadcastMs ) >= RACE_LOAD_RESEND_MS ) {
            Race_BroadcastLoad();
        }
        if ( Race_AllLoaded() ) {
            raceHost.state = RACE_STATE_COUNTDOWN;
            raceHost.countdownStartMs = now;
            Race_BroadcastCountdown();
            Race_GuiLogSrv( "all loaded -> countdown %d ms", raceHost.countdownMs );
        }
    } else if ( raceHost.state == RACE_STATE_COUNTDOWN ) {
        if ( (DWORD)( now - raceHost.lastCountdownBroadcastMs ) >= RACE_COUNTDOWN_RESEND_MS ) {
            Race_BroadcastCountdown();
        }
        if ( raceHost.countdownStartMs && (DWORD)( now - raceHost.countdownStartMs ) >= (DWORD)raceHost.countdownMs ) {
            Race_BeginRun( now );
        }
    } else if ( raceHost.state == RACE_STATE_RACING ) {
        if ( Race_AnyRemoteNotStarted() && raceHost.raceStartMs &&
             (DWORD)( now - raceHost.raceStartMs ) < RACE_START_RESEND_WINDOW &&
             (DWORD)( now - raceHost.lastStartBroadcastMs ) >= RACE_START_RESEND_MS ) {
            Race_BroadcastStart();
        }
        if ( Race_AllFinished() ) {
            Race_RecordRunHistory( 1 );
            raceHost.state = RACE_STATE_FINISHED;
            Race_GuiLogSrv( "all FINISHED" );
        }
    }

    if ( (DWORD)( now - raceHost.lastRosterBroadcastMs ) >= RACE_ROSTER_RESEND_MS ) {
        if ( raceHost.state != RACE_STATE_LOBBY ) Race_BroadcastRoster();
        raceHost.lastRosterBroadcastMs = now;
    }
}


/* ---------- socket setup ---------- */
int Race_OpenSocket( const char *bindIp, int port, char *errorOut, size_t errorOutSize ) {
    struct sockaddr_in address;
    u_long nb = 1;
    int yes = 1;
    unsigned long bindAddr = INADDR_ANY;
    if ( errorOut && errorOutSize ) errorOut[0] = '\0';
    if ( bindIp && bindIp[0] && strcmp( bindIp, "0.0.0.0" ) && _stricmp( bindIp, "any" ) && _stricmp( bindIp, "all" ) ) {
        bindAddr = inet_addr( bindIp );
        if ( bindAddr == INADDR_NONE ) {
            if ( errorOut && errorOutSize ) errorOut[errorOutSize - 1] = '\0';
            if ( errorOut && errorOutSize ) {
                snprintf( errorOut, errorOutSize, "invalid bind IP '%s' - use IPv4 like 0.0.0.0, 127.0.0.1, or your LAN IP", bindIp );
                errorOut[errorOutSize - 1] = '\0';
            }
            return 0;
        }
    }
    raceHost.socketId = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if ( raceHost.socketId == INVALID_SOCKET ) {
        Race_FormatWsaError( "socket", WSAGetLastError(), errorOut, errorOutSize );
        return 0;
    }
    if ( setsockopt( raceHost.socketId, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof( yes ) ) == SOCKET_ERROR ) {
        Race_FormatWsaError( "setsockopt SO_BROADCAST", WSAGetLastError(), errorOut, errorOutSize );
        closesocket( raceHost.socketId );
        raceHost.socketId = INVALID_SOCKET;
        return 0;
    }
    if ( ioctlsocket( raceHost.socketId, FIONBIO, &nb ) == SOCKET_ERROR ) {
        Race_FormatWsaError( "ioctlsocket FIONBIO", WSAGetLastError(), errorOut, errorOutSize );
        closesocket( raceHost.socketId );
        raceHost.socketId = INVALID_SOCKET;
        return 0;
    }
    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = bindAddr;
    address.sin_port = htons( (u_short)port );
    if ( bind( raceHost.socketId, (const struct sockaddr *)&address, sizeof( address ) ) == SOCKET_ERROR ) {
        Race_FormatWsaError( "bind", WSAGetLastError(), errorOut, errorOutSize );
        closesocket( raceHost.socketId );
        raceHost.socketId = INVALID_SOCKET;
        return 0;
    }
    return 1;
}

int Race_StartHost( void ) {
    char errorText[512];
    if ( Race_HostIsRunning() ) {
        Race_GuiLogSrv( "host already running on %s:%d", raceHost.bindAddress, raceHost.port );
        return 1;
    }
    Race_OnSettingsChanged( 0 );
    if ( gUiPort < 1 || gUiPort > 65535 ) {
        snprintf( gHostError, sizeof( gHostError ), "invalid UDP port %d - use 1..65535", gUiPort );
        Race_GuiLogSrv( "%s", gHostError );
        return 0;
    }
    Race_Copy( raceHost.bindAddress, sizeof( raceHost.bindAddress ), gUiBindAddress[0] ? gUiBindAddress : "0.0.0.0" );
    Race_Copy( gUiBindAddress, sizeof( gUiBindAddress ), raceHost.bindAddress );
    raceHost.port = gUiPort;
    errorText[0] = '\0';
    if ( !Race_OpenSocket( raceHost.bindAddress, raceHost.port, errorText, sizeof( errorText ) ) ) {
        Race_Copy( gHostError, sizeof( gHostError ), errorText[0] ? errorText : "failed to open UDP socket" );
        Race_GuiLog( "! host start failed: %s", gHostError );
        return 0;
    }
    gHostError[0] = '\0';
    Race_ClearPlayers();
    raceHost.session = (int)( time( NULL ) & 0x7fffffff );
    raceHost.state = RACE_STATE_LOBBY;
    raceHost.countdownStartMs = 0;
    raceHost.raceStartMs = 0;
    raceHost.lastLoadBroadcastMs = 0;
    raceHost.lastCountdownBroadcastMs = 0;
    raceHost.lastStartBroadcastMs = 0;
    raceHost.lastRosterBroadcastMs = 0;
    Race_GuiLogSrv( "lobby online on %s:%d (session %d)",
        raceHost.bindAddress[0] ? raceHost.bindAddress : "0.0.0.0", raceHost.port, raceHost.session );
    return 1;
}

void Race_StopHost( void ) {
    if ( !Race_HostIsRunning() ) {
        Race_GuiLogSrv( "host already offline" );
        return;
    }
    Race_BroadcastText( "srace chat %d -1 %s Race host stopped - lobby closed", raceHost.session, raceHost.hostName );
    Race_BroadcastText( "srace reject Host_stopped" );
    Sleep( 35 );
    Race_BroadcastText( "srace chat %d -1 %s Race host stopped - lobby closed", raceHost.session, raceHost.hostName );
    Race_BroadcastText( "srace reject Host_stopped" );
    closesocket( raceHost.socketId );
    raceHost.socketId = INVALID_SOCKET;
    raceHost.state = RACE_STATE_OFFLINE;
    raceHost.countdownStartMs = 0;
    raceHost.raceStartMs = 0;
    Race_ClearPlayers();
    Race_GuiLogSrv( "host stopped" );
}

/* ---------- entry point (WinMain - no console window) ---------- */
