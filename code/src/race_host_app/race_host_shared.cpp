#include "race_host_shared.h"

raceHost_t raceHost;

const raceChapter_t raceChapters[] = {
    { 1, "Ominous Rumors & Dark Secret", "escape1" },
    { 2, "Weapons of Vengeance", "forest" },
    { 3, "Deadly Designs", "sfm" },
    { 4, "Deathshead's Playground", "norway" },
    { 5, "Return Eng. & Op. Resurrection", "dam" }
};
const int raceChapterCount = (int)(sizeof(raceChapters) / sizeof(raceChapters[0]));

const char *raceILMaps[] = {
    "escape1", "escape2", "tram", "village1", "crypt1", "crypt2", "church", "boss1",
    "forest", "rocket", "baseout", "assault", "sfm", "factory", "trainyard", "swf",
    "norway", "xlabs", "boss2", "dam", "village2", "chateau", "dark", "dig", "castle", "end"
};
const int raceILMapCount = (int)(sizeof(raceILMaps) / sizeof(raceILMaps[0]));

const char *raceDifficulties[] = { "Don't hurt me", "Bring 'em on!", "I am Death incarnate!" };
/* ---------- helpers ---------- */
void Race_Copy( char *dst, size_t dstSize, const char *src ) {
    if ( !dst || dstSize == 0 ) return;
    if ( !src ) src = "";
    strncpy( dst, src, dstSize - 1 );
    dst[dstSize - 1] = '\0';
}

void Race_SanitizeToken( const char *input, char *out, size_t outSize ) {
    size_t srcIndex;
    size_t dstIndex = 0;
    if ( !out || outSize == 0 ) return;
    if ( !input ) input = "";
    for ( srcIndex = 0; input[srcIndex] && dstIndex + 1 < outSize; ++srcIndex ) {
        unsigned char ch = (unsigned char)input[srcIndex];
        if ( ch <= ' ' || ch == '\\' || ch == '"' || ch == ';' ) out[dstIndex++] = '_';
        else out[dstIndex++] = (char)ch;
    }
    out[dstIndex] = '\0';
    if ( !out[0] ) Race_Copy( out, outSize, "Runner" );
}

void Race_SanitizeOptionalToken( const char *input, char *out, size_t outSize ) {
    size_t srcIndex;
    size_t dstIndex = 0;
    if ( !out || outSize == 0 ) return;
    if ( !input ) input = "";
    for ( srcIndex = 0; input[srcIndex] && dstIndex + 1 < outSize; ++srcIndex ) {
        unsigned char ch = (unsigned char)input[srcIndex];
        if ( ch <= ' ' || ch == '\\' || ch == '"' || ch == ';' ) out[dstIndex++] = '_';
        else out[dstIndex++] = (char)ch;
    }
    out[dstIndex] = '\0';
}

char *Race_Trim( char *text ) {
    char *end;
    if ( !text ) return text;
    while ( *text == ' ' || *text == '\t' || *text == '\r' || *text == '\n' ) text++;
    end = text + strlen( text );
    while ( end > text && ( end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n' ) ) {
        *--end = '\0';
    }
    return text;
}

int Race_ParseBoolValue( const char *value, int fallback ) {
    if ( !value || !value[0] ) return fallback;
    if ( !_stricmp( value, "1" ) || !_stricmp( value, "true" ) || !_stricmp( value, "yes" ) || !_stricmp( value, "on" ) ) return 1;
    if ( !_stricmp( value, "0" ) || !_stricmp( value, "false" ) || !_stricmp( value, "no" ) || !_stricmp( value, "off" ) ) return 0;
    return atoi( value ) ? 1 : 0;
}

int Race_ParseModeValue( const char *value, int fallback ) {
    if ( !value || !value[0] ) return fallback;
    if ( !_stricmp( value, "full" ) || !_stricmp( value, "fullgame" ) || !_stricmp( value, "full_game" ) ) return 0;
    if ( !_stricmp( value, "chapter" ) ) return 1;
    if ( !_stricmp( value, "il" ) || !_stricmp( value, "individual" ) || !_stricmp( value, "individual_level" ) ) return 2;
    return atoi( value );
}

const char *Race_FileNameFromPath( const char *path ) {
    const char *slash;
    const char *slash2;
    if ( !path || !path[0] ) return "";
    slash = strrchr( path, '\\' );
    slash2 = strrchr( path, '/' );
    if ( !slash || ( slash2 && slash2 > slash ) ) slash = slash2;
    return slash ? slash + 1 : path;
}

const char *Race_AddrToString( const struct sockaddr_in *address ) {
    static char text[64];
    unsigned char *ip = (unsigned char *)&address->sin_addr.s_addr;
    snprintf( text, sizeof( text ), "%u.%u.%u.%u:%d", ip[0], ip[1], ip[2], ip[3], ntohs( address->sin_port ) );
    return text;
}

int Race_PlayerCount( void ) {
    int i, count = 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        if ( raceHost.players[i].used && !raceHost.players[i].left && !raceHost.players[i].timedOut && !raceHost.players[i].kicked ) count++;
    }
    return count;
}

int Race_QueueCount( void ) {
    int i, p, count = 0;
    for ( i = 0; i < RACE_QUEUE_MAX; ++i ) {
        int alreadyConnected = 0;
        if ( !raceHost.queue[i].used ) continue;
        for ( p = 0; p < raceHost.maxPlayers; ++p ) {
            racePlayer_t *player = &raceHost.players[p];
            if ( player->used && !player->left && !player->timedOut && !player->kicked &&
                 player->address.sin_addr.s_addr == raceHost.queue[i].address.sin_addr.s_addr &&
                 player->address.sin_port == raceHost.queue[i].address.sin_port ) {
                alreadyConnected = 1;
                break;
            }
        }
        if ( !alreadyConnected ) count++;
    }
    return count;
}

int Race_PlayerBlocksReady( const racePlayer_t *p ) {
    if ( !p || !p->used ) return 0;
    if ( p->left || p->timedOut || p->kicked ) return 0;
    return 1;
}

void Race_LoadedCounts( int *loadedOut, int *totalOut ) {
    int i, loaded = 0, total = 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        if ( !Race_PlayerBlocksReady( &raceHost.players[i] ) ) continue;
        total++;
        if ( raceHost.players[i].loaded ) loaded++;
    }
    if ( loadedOut ) *loadedOut = loaded;
    if ( totalOut ) *totalOut = total;
}

int Race_AllLoaded( void ) {
    int i;
    int any = 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        if ( !Race_PlayerBlocksReady( &raceHost.players[i] ) ) continue;
        any = 1;
        if ( !raceHost.players[i].loaded ) return 0;
    }
    return any;
}

int Race_AllFinished( void ) {
    int i;
    int any = 0;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        if ( !Race_PlayerBlocksReady( &raceHost.players[i] ) ) continue;
        any = 1;
        if ( !raceHost.players[i].finished ) return 0;
    }
    return any;
}

int Race_AnyRemoteNotStarted( void ) {
    int i;
    for ( i = 0; i < raceHost.maxPlayers; ++i ) {
        if ( !Race_PlayerBlocksReady( &raceHost.players[i] ) ) continue;
        if ( !raceHost.players[i].started ) return 1;
    }
    return 0;
}

const char *Race_StateName( int state ) {
    switch ( state ) {
        case RACE_STATE_OFFLINE: return "OFFLINE";
        case RACE_STATE_LOBBY: return "LOBBY";
        case RACE_STATE_LOADING: return "LOADING";
        case RACE_STATE_COUNTDOWN: return "COUNTDOWN";
        case RACE_STATE_RACING: return "RACING";
        case RACE_STATE_FINISHED: return "FINISHED";
        default: return "?";
    }
}

const char *Race_PlayerStateText( const racePlayer_t *p ) {
    if ( !p ) return "LOAD";
    if ( p->kicked ) return "KICK";
    if ( p->left ) return "LEFT";
    if ( p->timedOut ) return "T/OUT";
    if ( p->finished ) return "FIN";
    if ( p->started ) return p->paused ? "PAUSE" : "RUN";
    if ( p->loaded ) return "READY";
    return "LOAD";
}

void Race_FormatTime( int ms, char *out, size_t outSize ) {
    int secs, min, frac;
    if ( ms <= 0 ) { Race_Copy( out, outSize, "--" ); return; }
    secs = ms / 1000;
    min = secs / 60;
    frac = ms % 1000;
    if ( min > 0 ) snprintf( out, outSize, "%d:%02d.%03d", min, secs % 60, frac );
    else snprintf( out, outSize, "%d.%03d", secs % 60, frac );
}

void Race_FormatCountdown( int ms, char *out, size_t outSize ) {
    if ( ms <= 0 ) { Race_Copy( out, outSize, "GO" ); return; }
    if ( ms >= 1000 ) snprintf( out, outSize, "%.1fs", ms / 1000.0f );
    else snprintf( out, outSize, "0.%03d", ms );
}

int Race_HostIsRunning( void ) {
    return raceHost.socketId != INVALID_SOCKET && raceHost.state != RACE_STATE_OFFLINE;
}

void Race_ClearPlayers( void ) {
    memset( raceHost.players, 0, sizeof( raceHost.players ) );
    gUiSelectedSlot = -1;
}

static const char *Race_WsaHint( int err ) {
    switch ( err ) {
        case WSAEADDRINUSE: return "port/address is already in use";
        case WSAEADDRNOTAVAIL: return "IP is not assigned to this computer";
        case WSAEACCES: return "access denied, firewall, or protected port";
        case WSAEINVAL: return "invalid socket argument or socket already bound";
        case WSAENETDOWN: return "network subsystem is unavailable";
        case WSAEAFNOSUPPORT: return "address family is not supported";
        case WSAENOBUFS: return "not enough socket buffer space";
        default: return "winsock error";
    }
}

void Race_FormatWsaError( const char *op, int err, char *out, size_t outSize ) {
    char systemText[256];
    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    systemText[0] = '\0';
    FormatMessageA( flags, NULL, (DWORD)err, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
                    systemText, sizeof( systemText ), NULL );
    while ( systemText[0] && ( systemText[strlen( systemText ) - 1] == '\r' ||
                               systemText[strlen( systemText ) - 1] == '\n' ||
                               systemText[strlen( systemText ) - 1] == '.' ) ) {
        systemText[strlen( systemText ) - 1] = '\0';
    }
    if ( systemText[0] ) {
        snprintf( out, outSize, "%s failed: WSA %d (%s) - %s", op, err, Race_WsaHint( err ), systemText );
    } else {
        snprintf( out, outSize, "%s failed: WSA %d (%s)", op, err, Race_WsaHint( err ) );
    }
    out[outSize - 1] = '\0';
}

void Race_BuildConfigPath( void ) {
    char path[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA( NULL, path, sizeof( path ) );
    if ( n == 0 || n >= sizeof( path ) ) {
        Race_Copy( gConfigPath, sizeof( gConfigPath ), "serverconfig.cfg" );
        return;
    }
    slash = strrchr( path, '\\' );
    if ( !slash ) slash = strrchr( path, '/' );
    if ( slash ) slash[1] = '\0';
    else path[0] = '\0';
    snprintf( gConfigPath, sizeof( gConfigPath ), "%sserverconfig.cfg", path );
    gConfigPath[sizeof( gConfigPath ) - 1] = '\0';
}

static int Race_NormalizeBool( int value ) {
    return value ? 1 : 0;
}

static int Race_CopyTrimmedSetting( char *dst, size_t dstSize, const char *src, const char *fallback ) {
    char temp[256];
    char *trimmed;
    if ( !dst || dstSize == 0 ) return 0;
    Race_Copy( temp, sizeof( temp ), src && src[0] ? src : fallback );
    trimmed = Race_Trim( temp );
    if ( !trimmed || !trimmed[0] ) trimmed = (char *)( fallback ? fallback : "" );
    if ( strcmp( dst, trimmed ) ) {
        Race_Copy( dst, dstSize, trimmed );
        return 1;
    }
    return 0;
}

int Race_NormalizeSettings( void ) {
    char sanitized[RACE_NICK_MAX];
    char map[RACE_MAP_MAX];
    char password[RACE_PASSWORD_MAX];
    int changed = 0;

    if ( raceHost.maxPlayers < 1 ) { raceHost.maxPlayers = 1; changed = 1; }
    if ( raceHost.maxPlayers > RACE_MAX_PLAYERS ) { raceHost.maxPlayers = RACE_MAX_PLAYERS; changed = 1; }
    if ( gUiPort < 1 ) { gUiPort = 1; changed = 1; }
    if ( gUiPort > 65535 ) { gUiPort = 65535; changed = 1; }
    if ( raceHost.port != gUiPort ) { raceHost.port = (unsigned short)gUiPort; changed = 1; }

    changed |= Race_CopyTrimmedSetting( gUiBindAddress, sizeof( gUiBindAddress ), gUiBindAddress, "0.0.0.0" );
    changed |= Race_CopyTrimmedSetting( raceHost.bindAddress, sizeof( raceHost.bindAddress ), gUiBindAddress, "0.0.0.0" );

    if ( raceHost.mode < RACE_MODE_FULL || raceHost.mode > RACE_MODE_IL ) { raceHost.mode = RACE_MODE_FULL; changed = 1; }
    if ( raceHost.mission < 1 ) { raceHost.mission = 1; changed = 1; }
    if ( raceHost.mission > raceChapterCount ) { raceHost.mission = raceChapterCount; changed = 1; }
    if ( raceHost.difficulty < 1 ) { raceHost.difficulty = 1; changed = 1; }
    if ( raceHost.difficulty > 3 ) { raceHost.difficulty = 3; changed = 1; }
    if ( gUiCountdownSec < 1 ) { gUiCountdownSec = 1; changed = 1; }
    if ( gUiCountdownSec > 30 ) { gUiCountdownSec = 30; changed = 1; }
    if ( raceHost.countdownMs != gUiCountdownSec * 1000 ) { raceHost.countdownMs = gUiCountdownSec * 1000; changed = 1; }
    if ( raceHost.pauseAlertMs < 0 ) { raceHost.pauseAlertMs = 0; changed = 1; }
    if ( raceHost.pauseAlertMs > 600000 ) { raceHost.pauseAlertMs = 600000; changed = 1; }

    if ( raceHost.percent100 != Race_NormalizeBool( raceHost.percent100 ) ) { raceHost.percent100 = Race_NormalizeBool( raceHost.percent100 ); changed = 1; }
    if ( raceHost.hl1Movement != Race_NormalizeBool( raceHost.hl1Movement ) ) { raceHost.hl1Movement = Race_NormalizeBool( raceHost.hl1Movement ); changed = 1; }
    if ( raceHost.autoJump != Race_NormalizeBool( raceHost.autoJump ) ) { raceHost.autoJump = Race_NormalizeBool( raceHost.autoJump ); changed = 1; }
    if ( raceHost.antiCheat != Race_NormalizeBool( raceHost.antiCheat ) ) { raceHost.antiCheat = Race_NormalizeBool( raceHost.antiCheat ); changed = 1; }
    if ( raceHost.autoReadyCheck != Race_NormalizeBool( raceHost.autoReadyCheck ) ) { raceHost.autoReadyCheck = Race_NormalizeBool( raceHost.autoReadyCheck ); changed = 1; }
    if ( raceHost.queueEnabled != Race_NormalizeBool( raceHost.queueEnabled ) ) { raceHost.queueEnabled = Race_NormalizeBool( raceHost.queueEnabled ); changed = 1; }
    if ( raceHost.privateLobby != Race_NormalizeBool( raceHost.privateLobby ) ) { raceHost.privateLobby = Race_NormalizeBool( raceHost.privateLobby ); changed = 1; }

    Race_SanitizeToken( gUiHostName, sanitized, sizeof( sanitized ) );
    if ( strcmp( raceHost.hostName, sanitized ) ) { Race_Copy( raceHost.hostName, sizeof( raceHost.hostName ), sanitized ); changed = 1; }
    if ( strcmp( gUiHostName, sanitized ) ) { Race_Copy( gUiHostName, sizeof( gUiHostName ), sanitized ); changed = 1; }

    Race_SanitizeToken( gUiIlMap, map, sizeof( map ) );
    if ( strcmp( raceHost.ilMap, map ) ) { Race_Copy( raceHost.ilMap, sizeof( raceHost.ilMap ), map ); changed = 1; }
    if ( strcmp( gUiIlMap, map ) ) { Race_Copy( gUiIlMap, sizeof( gUiIlMap ), map ); changed = 1; }

    Race_SanitizeOptionalToken( raceHost.password, password, sizeof( password ) );
    if ( strcmp( raceHost.password, password ) ) { Race_Copy( raceHost.password, sizeof( raceHost.password ), password ); changed = 1; }

    return changed;
}

static int Race_BindIpLooksValid( const char *bindIp ) {
    if ( !bindIp || !bindIp[0] ) return 0;
    if ( !_stricmp( bindIp, "any" ) || !_stricmp( bindIp, "all" ) || !strcmp( bindIp, "0.0.0.0" ) ) return 1;
    return inet_addr( bindIp ) != INADDR_NONE;
}

int Race_ValidateHostSettings( char *out, size_t outSize ) {
    const char *bindIp = gUiBindAddress[0] ? gUiBindAddress : raceHost.bindAddress;
    if ( out && outSize ) out[0] = '\0';
    if ( gUiPort < 1 || gUiPort > 65535 ) {
        if ( out && outSize ) snprintf( out, outSize, "UDP port must be between 1 and 65535." );
        return 0;
    }
    if ( !Race_BindIpLooksValid( bindIp ) ) {
        if ( out && outSize ) snprintf( out, outSize, "Bind IP must be IPv4, for example 0.0.0.0, 127.0.0.1, or your LAN IP." );
        return 0;
    }
    if ( raceHost.privateLobby && !raceHost.password[0] ) {
        if ( out && outSize ) snprintf( out, outSize, "Private lobby needs a password; otherwise it behaves like a public lobby." );
        return 0;
    }
    if ( !raceHost.hostName[0] && !gUiHostName[0] ) {
        if ( out && outSize ) snprintf( out, outSize, "Host name cannot be empty." );
        return 0;
    }
    if ( out && outSize ) snprintf( out, outSize, "Host settings are ready." );
    return 1;
}

int Race_ValidateRaceSettings( char *out, size_t outSize ) {
    DWORD now = GetTickCount();
    int i;
    if ( out && outSize ) out[0] = '\0';
    if ( !Race_HostIsRunning() ) {
        if ( out && outSize ) snprintf( out, outSize, "Start the host before starting a race." );
        return 0;
    }
    if ( raceHost.state != RACE_STATE_LOBBY ) {
        if ( out && outSize ) snprintf( out, outSize, "Race controls are locked while state is %s.", Race_StateName( raceHost.state ) );
        return 0;
    }
    if ( Race_PlayerCount() < 1 ) {
        if ( out && outSize ) snprintf( out, outSize, "At least one player must be connected." );
        return 0;
    }
    if ( raceHost.mode == RACE_MODE_IL && !raceHost.ilMap[0] ) {
        if ( out && outSize ) snprintf( out, outSize, "Individual Level mode needs a map name." );
        return 0;
    }
    if ( raceHost.autoReadyCheck ) {
        for ( i = 0; i < raceHost.maxPlayers; ++i ) {
            racePlayer_t *p = &raceHost.players[i];
            if ( !Race_PlayerBlocksReady( p ) ) continue;
            if ( p->cheatFlags ) {
                if ( out && outSize ) snprintf( out, outSize, "%s has anti-cheat flags; clear them before start.", p->nick[0] ? p->nick : "A player" );
                return 0;
            }
            if ( p->lastHeardMs && (DWORD)( now - p->lastHeardMs ) > 10000 ) {
                if ( out && outSize ) snprintf( out, outSize, "%s telemetry is stale; wait for a fresh update.", p->nick[0] ? p->nick : "A player" );
                return 0;
            }
        }
    }
    if ( out && outSize ) snprintf( out, outSize, "Race can start." );
    return 1;
}

static void Race_ApplyConfigKeyValue( const char *key, const char *value ) {
    if ( !key || !value ) return;
    if ( !_stricmp( key, "bind_ip" ) || !_stricmp( key, "bind" ) || !_stricmp( key, "ip" ) ) {
        Race_Copy( raceHost.bindAddress, sizeof( raceHost.bindAddress ), value );
        Race_Copy( gUiBindAddress, sizeof( gUiBindAddress ), value );
    } else if ( !_stricmp( key, "port" ) || !_stricmp( key, "udp_port" ) ) {
        gUiPort = atoi( value );
        if ( gUiPort < 1 ) gUiPort = 1;
        if ( gUiPort > 65535 ) gUiPort = 65535;
        raceHost.port = gUiPort;
    } else if ( !_stricmp( key, "host_name" ) || !_stricmp( key, "hostname" ) || !_stricmp( key, "name" ) ) {
        Race_SanitizeToken( value, raceHost.hostName, sizeof( raceHost.hostName ) );
        Race_Copy( gUiHostName, sizeof( gUiHostName ), raceHost.hostName );
    } else if ( !_stricmp( key, "mode" ) || !_stricmp( key, "run_mode" ) ) {
        raceHost.mode = Race_ParseModeValue( value, raceHost.mode );
    } else if ( !_stricmp( key, "mission" ) || !_stricmp( key, "chapter" ) ) {
        raceHost.mission = atoi( value );
    } else if ( !_stricmp( key, "difficulty" ) || !_stricmp( key, "diff" ) ) {
        raceHost.difficulty = atoi( value );
    } else if ( !_stricmp( key, "il_map" ) || !_stricmp( key, "ilmap" ) || !_stricmp( key, "map" ) ) {
        Race_SanitizeToken( value, raceHost.ilMap, sizeof( raceHost.ilMap ) );
        Race_Copy( gUiIlMap, sizeof( gUiIlMap ), raceHost.ilMap );
    } else if ( !_stricmp( key, "countdown" ) || !_stricmp( key, "countdown_sec" ) ) {
        gUiCountdownSec = atoi( value );
    } else if ( !_stricmp( key, "percent100" ) || !_stricmp( key, "100" ) || !_stricmp( key, "category_100" ) ) {
        raceHost.percent100 = Race_ParseBoolValue( value, raceHost.percent100 );
    } else if ( !_stricmp( key, "hl1" ) || !_stricmp( key, "hl1_movement" ) ) {
        raceHost.hl1Movement = Race_ParseBoolValue( value, raceHost.hl1Movement );
    } else if ( !_stricmp( key, "autojump" ) || !_stricmp( key, "auto_jump" ) ) {
        raceHost.autoJump = Race_ParseBoolValue( value, raceHost.autoJump );
    } else if ( !_stricmp( key, "anticheat" ) || !_stricmp( key, "anti_cheat" ) || !_stricmp( key, "ac" ) ) {
        raceHost.antiCheat = Race_ParseBoolValue( value, raceHost.antiCheat );
    } else if ( !_stricmp( key, "auto_ready_check" ) || !_stricmp( key, "ready_check" ) ) {
        raceHost.autoReadyCheck = Race_ParseBoolValue( value, raceHost.autoReadyCheck );
    } else if ( !_stricmp( key, "queue" ) || !_stricmp( key, "queue_enabled" ) || !_stricmp( key, "waiting_list" ) ) {
        raceHost.queueEnabled = Race_ParseBoolValue( value, raceHost.queueEnabled );
    } else if ( !_stricmp( key, "private" ) || !_stricmp( key, "private_lobby" ) || !_stricmp( key, "password_enabled" ) ) {
        raceHost.privateLobby = Race_ParseBoolValue( value, raceHost.privateLobby );
    } else if ( !_stricmp( key, "password" ) || !_stricmp( key, "lobby_password" ) ) {
        Race_SanitizeOptionalToken( value, raceHost.password, sizeof( raceHost.password ) );
    } else if ( !_stricmp( key, "pause_alert_ms" ) ) {
        raceHost.pauseAlertMs = atoi( value );
        if ( raceHost.pauseAlertMs < 0 ) raceHost.pauseAlertMs = 0;
    } else if ( !_stricmp( key, "pause_alert_sec" ) ) {
        raceHost.pauseAlertMs = atoi( value ) * 1000;
        if ( raceHost.pauseAlertMs < 0 ) raceHost.pauseAlertMs = 0;
    } else if ( !_stricmp( key, "theme" ) ) {
        if ( !_stricmp( value, "light" ) ) {
            gThemeDark = 0;
            gThemeBlend = gThemeTarget = 0.0f;
        } else if ( !_stricmp( value, "dark" ) ) {
            gThemeDark = 1;
            gThemeBlend = gThemeTarget = 1.0f;
        }
    }
}

int Race_LoadServerConfig( int quiet ) {
    FILE *f;
    char line[512];
    int loaded = 0;
    if ( !gConfigPath[0] ) Race_BuildConfigPath();
    f = fopen( gConfigPath, "r" );
    if ( !f ) {
        if ( !Race_SaveServerConfig() ) {
            gConfigLoaded = 0;
            return 0;
        }
        snprintf( gConfigNotice, sizeof( gConfigNotice ), "created default config: %s (missing file)", Race_FileNameFromPath( gConfigPath ) );
        Race_GuiLogSrv( "%s", gConfigNotice );
        gConfigLoaded = 1;
        return 1;
    }
    while ( fgets( line, sizeof( line ), f ) ) {
        char *comment;
        char *key;
        char *value;
        comment = strstr( line, "//" );
        if ( comment ) *comment = '\0';
        comment = strchr( line, '#' );
        if ( comment ) *comment = '\0';
        comment = strchr( line, ';' );
        if ( comment ) *comment = '\0';
        key = Race_Trim( line );
        if ( !key[0] ) continue;
        value = strchr( key, '=' );
        if ( !value ) value = strchr( key, ' ' );
        if ( !value ) value = strchr( key, '\t' );
        if ( !value ) continue;
        *value++ = '\0';
        key = Race_Trim( key );
        value = Race_Trim( value );
        if ( !key[0] || !value[0] ) continue;
        Race_ApplyConfigKeyValue( key, value );
        loaded++;
    }
    fclose( f );
    Race_OnSettingsChanged( 0 );
    gConfigLoaded = loaded > 0 ? 1 : 0;
    snprintf( gConfigNotice, sizeof( gConfigNotice ), "loaded %d config values from %s", loaded, Race_FileNameFromPath( gConfigPath ) );
    if ( !quiet ) Race_GuiLogSrv( "%s", gConfigNotice );
    return gConfigLoaded;
}

int Race_SaveServerConfig( void ) {
    FILE *f;
    if ( !gConfigPath[0] ) Race_BuildConfigPath();
    Race_OnSettingsChanged( 0 );
    f = fopen( gConfigPath, "w" );
    if ( !f ) {
        snprintf( gConfigNotice, sizeof( gConfigNotice ), "could not write config: %s", Race_FileNameFromPath( gConfigPath ) );
        Race_GuiLog( "! %s", gConfigNotice );
        return 0;
    }
    fprintf( f, "# RtCW Race Host server config\n" );
    fprintf( f, "# Host does not auto-start; press Start Host in the app.\n" );
    fprintf( f, "bind_ip=%s\n", gUiBindAddress[0] ? gUiBindAddress : "0.0.0.0" );
    fprintf( f, "port=%d\n", gUiPort );
    fprintf( f, "host_name=%s\n", raceHost.hostName );
    fprintf( f, "mode=%d\n", raceHost.mode );
    fprintf( f, "mission=%d\n", raceHost.mission );
    fprintf( f, "difficulty=%d\n", raceHost.difficulty );
    fprintf( f, "il_map=%s\n", raceHost.ilMap[0] ? raceHost.ilMap : "escape1" );
    fprintf( f, "countdown=%d\n", gUiCountdownSec );
    fprintf( f, "percent100=%d\n", raceHost.percent100 );
    fprintf( f, "hl1=%d\n", raceHost.hl1Movement );
    fprintf( f, "autojump=%d\n", raceHost.autoJump );
    fprintf( f, "anticheat=%d\n", raceHost.antiCheat );
    fprintf( f, "auto_ready_check=%d\n", raceHost.autoReadyCheck );
    fprintf( f, "queue_enabled=%d\n", raceHost.queueEnabled );
    fprintf( f, "private_lobby=%d\n", raceHost.privateLobby );
    fprintf( f, "password=%s\n", raceHost.password );
    fprintf( f, "pause_alert_sec=%d\n", raceHost.pauseAlertMs > 0 ? raceHost.pauseAlertMs / 1000 : 0 );
    fprintf( f, "theme=%s\n", gThemeDark ? "dark" : "light" );
    fclose( f );
    snprintf( gConfigNotice, sizeof( gConfigNotice ), "saved config: %s", Race_FileNameFromPath( gConfigPath ) );
    Race_GuiLogSrv( "%s", gConfigNotice );
    return 1;
}
