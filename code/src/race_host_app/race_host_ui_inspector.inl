static void Race_DrawHealthCheck( const char *label, raceLogKind_t kind, const char *text, const char *tooltip ) {
    ImVec4 color = Race_LogColor( kind );
    ImVec4 bg = color;
    ImVec2 p0;
    ImVec2 p1;
    bg.w = gThemeBlend > 0.5f ? 0.10f : 0.07f;
    ImGui::PushID( label );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, bg );
    ImGui::BeginChild( "health_row", ImVec2( 0.0f, 34.0f ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    p0 = ImGui::GetWindowPos();
    p1 = ImVec2( p0.x + 3.0f, p0.y + ImGui::GetWindowSize().y );
    ImGui::GetWindowDrawList()->AddRectFilled( p0, p1, ImGui::ColorConvertFloat4ToU32( color ), 4.0f, ImDrawFlags_RoundCornersLeft );
    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 6.0f );
    ImGui::PushStyleColor( ImGuiCol_Text, color );
    ImGui::TextUnformatted( "*" );
    ImGui::PopStyleColor();
    ImGui::SameLine( 0.0f, 8.0f );
    ImGui::TextUnformatted( label );
    ImGui::SameLine();
    ImGui::TextDisabled( "%s", text && text[0] ? text : "-" );
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Race_DrawTooltip( tooltip );
    ImGui::PopID();
}

static void Race_DrawStatValue( const char *label, const char *value, ImVec4 color, const char *tooltip ) {
    ImVec4 bg = color;
    bg.w = gThemeBlend > 0.5f ? 0.09f : 0.06f;
    ImGui::PushID( label );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, bg );
    ImGui::BeginChild( "stat_value", ImVec2( 0.0f, 58.0f ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    ImGui::TextDisabled( "%s", label );
    ImGui::PushStyleColor( ImGuiCol_Text, color );
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::TextUnformatted( value && value[0] ? value : "-" );
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    Race_DrawTooltip( tooltip );
    ImGui::PopID();
}

static raceLogKind_t Race_PlayerHealthKind( const racePlayer_t *p ) {
    if ( !p ) return RACE_LOG_INFO;
    if ( p->health <= 0 ) return RACE_LOG_ERROR;
    if ( p->health < 35 ) return RACE_LOG_WARN;
    return RACE_LOG_SUCCESS;
}

static void Race_DrawLobbyHealthPanel( float width, float height ) {
    char hostReason[256];
    char raceReason[256];
    int hostOk = Race_ValidateHostSettings( hostReason, sizeof( hostReason ) );
    int raceOk = Race_ValidateRaceSettings( raceReason, sizeof( raceReason ) );
    int playerCount = Race_PlayerCount();
    int queueCount = Race_QueueCount();
    int loaded = 0;
    int total = 0;
    int issueCount = 0;
    DWORD now = GetTickCount();
    int i;

    Race_LoadedCounts( &loaded, &total );
    ImGui::BeginChild( "lobby_health", ImVec2( width, height ), true );
    Race_DrawSectionTitle( "Lobby Health" );
    Race_DrawHealthCheck( "Host", hostOk ? RACE_LOG_SUCCESS : RACE_LOG_WARN,
        Race_HostIsRunning() ? "online" : ( hostOk ? "ready" : hostReason ),
        "Socket/config state. If this is not green, the host cannot accept runners yet." );
    if ( Race_StateIsActiveRun() ) {
        Race_DrawHealthCheck( "Race", RACE_LOG_SUCCESS, Race_StateName( raceHost.state ),
            "Current run state from the host state machine." );
    } else if ( raceHost.state == RACE_STATE_FINISHED ) {
        Race_DrawHealthCheck( "Race", RACE_LOG_INFO, "finished",
            "The previous race is finished. Reset/start another lobby run when ready." );
    } else {
        Race_DrawHealthCheck( "Race", raceOk ? RACE_LOG_SUCCESS : RACE_LOG_WARN,
            raceOk ? "can start" : raceReason,
            "Start validation. It checks lobby state, connected players, load state, stale telemetry, and rule blockers." );
    }
    Race_DrawHealthCheck( "Players", playerCount > 0 ? RACE_LOG_SUCCESS : RACE_LOG_INFO,
        Race_HostIsRunning() ? "connected" : "offline",
        "Connected runners that are currently known by the host." );
    if ( raceHost.state == RACE_STATE_LOADING ) {
        char loadText[64];
        snprintf( loadText, sizeof( loadText ), "%d/%d loaded", loaded, total );
        Race_DrawHealthCheck( "Loading", Race_AllLoaded() ? RACE_LOG_SUCCESS : RACE_LOG_WARN, loadText,
            "Load readiness from players. Race should not start until everyone reported loaded." );
    }
    Race_DrawHealthCheck( "Queue", queueCount > 0 ? RACE_LOG_WARN : RACE_LOG_SUCCESS,
        queueCount > 0 ? "waiting runners" : "clear",
        "Players waiting because the lobby is full or a run is currently active." );

    ImGui::Separator();
    for ( i = 0; i < raceHost.maxPlayers && issueCount < 4; ++i ) {
        racePlayer_t *p = &raceHost.players[i];
        char riskText[48];
        raceLogKind_t kind;
        if ( !p->used ) continue;
        kind = Race_PlayerRiskText( p, now, riskText, sizeof( riskText ) );
        if ( kind == RACE_LOG_SUCCESS || kind == RACE_LOG_INFO ) continue;
        ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( p->color[0], p->color[1], p->color[2], 255 ) );
        ImGui::TextUnformatted( p->nick[0] ? p->nick : "Player" );
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor( ImGuiCol_Text, Race_LogColor( kind ) );
        ImGui::TextUnformatted( riskText );
        ImGui::PopStyleColor();
        issueCount++;
    }
    if ( issueCount == 0 ) ImGui::TextDisabled( "No player blockers right now." );
    ImGui::EndChild();
}

static void Race_DrawSelectedPlayerStatsPanel( float width, float height ) {
    racePlayer_t *p = Race_FindPlayerBySlot( gUiSelectedSlot );
    DWORD now = GetTickCount();
    char healthText[32];
    char armorText[32];
    char ammoText[32];
    char clipText[32];
    char speedText[32];
    char posText[96];
    char velText[96];
    char rgtText[32];
    char igtText[32];
    char stageText[64];
    char progressText[64];
    char guardText[48];
    raceLogKind_t guardKind;
    float progress;

    ImGui::BeginChild( "player_stats", ImVec2( width, height ), true );
    Race_DrawSectionTitle( "Player Stats" );
    if ( !p || !p->used ) {
        ImGui::TextDisabled( "Select a player from the table to inspect live telemetry." );
        ImGui::EndChild();
        return;
    }

    progress = Race_PlayerProgressPercent( p );
    snprintf( healthText, sizeof( healthText ), "%d", p->health );
    snprintf( armorText, sizeof( armorText ), "%d", p->armor );
    snprintf( ammoText, sizeof( ammoText ), "%d", p->ammo );
    snprintf( clipText, sizeof( clipText ), "%d", p->clip );
    snprintf( speedText, sizeof( speedText ), "%.1f", p->speed );
    snprintf( posText, sizeof( posText ), "%.0f  %.0f  %.0f", p->x, p->y, p->z );
    snprintf( velText, sizeof( velText ), "%.0f  %.0f  %.0f", p->vx, p->vy, p->vz );
    snprintf( stageText, sizeof( stageText ), "%s  %d", p->stageName[0] ? p->stageName : "stage", p->stageProgress );
    snprintf( progressText, sizeof( progressText ), "obj %d/%d  zones %d/%d",
        p->objectivesFound, p->objectivesTotal, p->zoneProgress, p->zoneTotal );
    if ( p->timeMs > 0 ) Race_FormatTime( p->timeMs, rgtText, sizeof( rgtText ) ); else Race_Copy( rgtText, sizeof( rgtText ), "-" );
    if ( p->igtMs > 0 ) Race_FormatTime( p->igtMs, igtText, sizeof( igtText ) ); else Race_Copy( igtText, sizeof( igtText ), "-" );
    guardKind = Race_PlayerRiskText( p, now, guardText, sizeof( guardText ) );

    ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( p->color[0], p->color[1], p->color[2], 255 ) );
    ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
    ImGui::Text( "#%d  %s", p->slot, p->nick[0] ? p->nick : "Player" );
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled( "%s / %s", Race_PlayerStateText( p ), p->map[0] ? p->map : "-" );

    if ( ImGui::BeginTable( "player_stat_tiles", 5, ImGuiTableFlags_SizingStretchSame ) ) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); Race_DrawStatValue( "HP", healthText, Race_LogColor( Race_PlayerHealthKind( p ) ), "Current player health from live playerstate telemetry." );
        ImGui::TableNextColumn(); Race_DrawStatValue( "Armor", armorText, Race_AccentColor(), "Current armor value." );
        ImGui::TableNextColumn(); Race_DrawStatValue( "Ammo", ammoText, Race_LogColor( RACE_LOG_INFO ), "Reserve ammo for the active weapon." );
        ImGui::TableNextColumn(); Race_DrawStatValue( "Clip", clipText, Race_LogColor( RACE_LOG_INFO ), "Ammo currently loaded in the active weapon clip." );
        ImGui::TableNextColumn(); Race_DrawStatValue( "Speed", speedText, Race_LogColor( RACE_LOG_SUCCESS ), "Horizontal movement speed calculated from velocity X/Y." );
        ImGui::EndTable();
    }

    ImGui::ProgressBar( progress / 100.0f, ImVec2( -1.0f, 18.0f ), progressText );
    Race_DrawTooltip( "Combined objective/zone progress used by the live progress graph." );
    ImGui::TextDisabled( "Stage: %s", stageText );
    ImGui::TextDisabled( "RGT %s   IGT %s   stage %dms", rgtText, igtText, p->stageTimeMs );
    ImGui::TextDisabled( "Pos %s   Vel %s", posText, velText );
    ImGui::TextDisabled( "Yaw %.1f  Pitch %.1f  Weapon %d  Ground %d", p->yaw, p->pitch, p->weapon, p->groundEntityNum );
    ImGui::PushStyleColor( ImGuiCol_Text, Race_LogColor( guardKind ) );
    ImGui::Text( "Guard: %s", guardText );
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

static void Race_DrawInspectorPanel( float height ) {
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float avail = ImGui::GetContentRegionAvail().x;
    float leftWidth = ( avail - gap ) * 0.42f;
    if ( avail < 640.0f ) {
        float halfHeight = ( height - ImGui::GetStyle().ItemSpacing.y ) * 0.5f;
        Race_DrawLobbyHealthPanel( 0.0f, halfHeight );
        Race_DrawSelectedPlayerStatsPanel( 0.0f, halfHeight );
        return;
    }
    Race_DrawLobbyHealthPanel( leftWidth, height );
    if ( leftWidth < avail - 260.0f ) {
        ImGui::SameLine();
        Race_DrawSelectedPlayerStatsPanel( 0.0f, height );
    }
}
