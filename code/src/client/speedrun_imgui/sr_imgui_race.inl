// Race-specific ImGui panels, overlays, and controls.

static char s_raceControlChatInput[128] = "";

static void CL_ImGuiRaceCommandButton( const char *label, const char *command, float width = 118.0f, bool enabled = true, const char *disabledHint = NULL ) {
	if ( !enabled ) {
		ImGui::BeginDisabled();
	}
	if ( ImGui::Button( label, ImVec2( width > 0.0f ? width : ImGui::GetContentRegionAvail().x, 0.0f ) ) ) {
		Cbuf_AddText( command );
		Cbuf_AddText( "\n" );
		if ( !Q_stricmp( command, "ls_race_start" ) ) {
			CL_SpeedrunImGui_CloseSettingsForRaceStart();
		}
	}
	CL_ImGuiOptionTooltip( label, command, enabled ? "Runs this Race command." : ( disabledHint ? disabledHint : "This Race command is not available right now." ) );
	if ( !enabled ) {
		ImGui::EndDisabled();
	}
}

static ImVec4 CL_ImGuiRaceAccentVec4( float alphaMul = 1.0f ) {
	return ImGui::ColorConvertU32ToFloat4( CL_ImGuiGuiAccentU32( alphaMul ) );
}

static void CL_ImGuiRaceControlStylePush( void ) {
	ImVec4 accent = CL_ImGuiRaceAccentVec4();
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 6.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 5.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_GrabRounding, 3.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_TabRounding, 4.0f );
	ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.020f, 0.026f, 0.024f, 0.97f ) );
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.035f, 0.044f, 0.040f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( accent.x * 0.42f, accent.y * 0.42f, accent.z * 0.42f, 0.55f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.065f, 0.082f, 0.074f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( accent.x * 0.16f + 0.05f, accent.y * 0.16f + 0.07f, accent.z * 0.16f + 0.05f, 0.96f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( accent.x * 0.24f + 0.05f, accent.y * 0.24f + 0.07f, accent.z * 0.24f + 0.05f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( accent.x * 0.20f, accent.y * 0.20f, accent.z * 0.20f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( accent.x * 0.34f, accent.y * 0.34f, accent.z * 0.34f, 0.98f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( accent.x * 0.46f, accent.y * 0.46f, accent.z * 0.46f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( accent.x * 0.18f, accent.y * 0.18f, accent.z * 0.18f, 0.78f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( accent.x * 0.28f, accent.y * 0.28f, accent.z * 0.28f, 0.92f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( accent.x * 0.38f, accent.y * 0.38f, accent.z * 0.38f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_Tab, ImVec4( 0.065f, 0.082f, 0.074f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_TabHovered, ImVec4( accent.x * 0.28f, accent.y * 0.28f, accent.z * 0.28f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_TabActive, ImVec4( accent.x * 0.22f, accent.y * 0.22f, accent.z * 0.22f, 0.96f ) );
	ImGui::PushStyleColor( ImGuiCol_TitleBg, ImVec4( 0.030f, 0.045f, 0.035f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_TitleBgActive, ImVec4( 0.055f, 0.085f, 0.050f, 1.00f ) );
}

static void CL_ImGuiRaceControlStylePop( void ) {
	ImGui::PopStyleColor( 17 );
	ImGui::PopStyleVar( 5 );
}

static void CL_ImGuiRaceControlHeader( const lsRaceUiSnapshot_t *race, int rowCount ) {
	char playersText[16];
	int statCols = ImGui::GetContentRegionAvail().x < 720.0f ? 3 : 6;
	Com_sprintf( playersText, sizeof( playersText ), "%d", rowCount );
	CL_ImGuiBeginAutoBox( "race_control_header" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Race Control" );
	CL_ImGuiSameLineIfFits( 180.0f );
	ImGui::TextDisabled( "%s", race->status[0] ? race->status : "Idle" );
	if ( ImGui::BeginTable( "race_control_stats", statCols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Role", race->role[0] ? race->role : "Idle" );
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "State", race->state[0] ? race->state : "Idle" );
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Category", race->category[0] ? race->category : "Full Game" );
		if ( statCols == 3 ) {
			ImGui::TableNextRow();
		}
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Players", playersText );
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Ready", race->ready[0] ? race->ready : "0/0" );
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Timer", race->timer[0] ? race->timer : "0.00" );
		ImGui::EndTable();
	}
	ImGui::EndChild();
}

static void CL_ImGuiRaceFoundLobbyRow( int index, bool canJoin ) {
	char cvarName[32];
	char lobbyText[192];
	char command[64];
	Com_sprintf( cvarName, sizeof( cvarName ), "ls_race_found_%d", index );
	Cvar_VariableStringBuffer( cvarName, lobbyText, sizeof( lobbyText ) );
	if ( !lobbyText[0] ) {
		return;
	}
	ImGui::PushID( index );
	ImGui::AlignTextToFramePadding();
	ImGui::TextWrapped( "%s", lobbyText );
	CL_ImGuiSameLineIfFits( 66.0f );
	Com_sprintf( command, sizeof( command ), "ls_race_join_found %d", index );
	CL_ImGuiRaceCommandButton( "Join", command, 58.0f, canJoin, "Leave the current race before joining a discovered lobby." );
	ImGui::PopID();
}

static void CL_ImGuiRaceDrawChatLog( const lsRaceUiSnapshot_t *race, float height ) {
	int i;
	ImGui::BeginChild( "race_control_chat_log", ImVec2( 0, height ), true, ImGuiWindowFlags_HorizontalScrollbar );
	for ( i = 0; i < race->chatCount && i < LS_RACE_UI_CHAT_LINES; ++i ) {
		if ( race->chat[i].text[0] ) {
			ImVec4 nickColor = ImVec4( race->chat[i].red / 255.0f, race->chat[i].green / 255.0f, race->chat[i].blue / 255.0f, 1.0f );
			ImGui::TextColored( nickColor, "%s:", race->chat[i].nick[0] ? race->chat[i].nick : "Runner" );
			ImGui::SameLine( 0.0f, 4.0f );
			ImGui::PushTextWrapPos( 0.0f );
			ImGui::TextUnformatted( race->chat[i].text );
			ImGui::PopTextWrapPos();
		} else if ( race->chatLines[i][0] ) {
			ImGui::TextWrapped( "%s", race->chatLines[i] );
		}
	}
	if ( race->chatCount <= 0 ) {
		ImGui::TextDisabled( "No Race chat yet." );
	}
	ImGui::EndChild();
}

static void CL_ImGuiRaceDrawChatComposer( bool canChat ) {
	ImGuiStyle &style = ImGui::GetStyle();
	float sendWidth = 72.0f;
	float avail = ImGui::GetContentRegionAvail().x;
	bool inlineButton = avail > 230.0f;
	bool sendNow = false;
	ImGui::BeginDisabled( !canChat );
	if ( inlineButton ) {
		ImGui::SetNextItemWidth( avail - sendWidth - style.ItemSpacing.x );
		sendNow = ImGui::InputTextWithHint( "##race_control_chat_input", "message...", s_raceControlChatInput, sizeof( s_raceControlChatInput ), ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		sendNow = ImGui::Button( "Send", ImVec2( sendWidth, 0.0f ) ) || sendNow;
	} else {
		ImGui::SetNextItemWidth( -1.0f );
		sendNow = ImGui::InputTextWithHint( "##race_control_chat_input", "message...", s_raceControlChatInput, sizeof( s_raceControlChatInput ), ImGuiInputTextFlags_EnterReturnsTrue );
		sendNow = ImGui::Button( "Send", ImVec2( sendWidth, 0.0f ) ) || sendNow;
	}
	if ( sendNow ) {
		CL_ImGuiRaceSubmitChatInput( s_raceControlChatInput, sizeof( s_raceControlChatInput ) );
	}
	ImGui::EndDisabled();
	CL_ImGuiOptionTooltip( "Race chat", "ls_race_say", canChat ? "Sends a message to the active Race lobby." : "Join or host a race before chatting." );
}

static void CL_ImGuiRaceDrawHostBox( const float raceColorFallback[4], bool canHost, bool canStart, bool canLeave, bool canChat, bool canEditSettings ) {
	static const char *modeLabels[] = { "Full Game", "Chapter", "Individual Level" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *missionLabels[] = { "1: Ominous Rumors", "2: Vengeance", "3: Deadly Designs", "4: Deathshead", "5: Resurrection" };
	static const int missionValues[] = { 1, 2, 3, 4, 5 };
	static const char *diffLabels[] = { "Don't hurt me.", "Bring 'em on!", "I am Death incarnate!" };
	static const int diffValues[] = { 1, 2, 3 };
	int mode = Cvar_VariableIntegerValue( "ls_mode" );
	bool hl1 = Cvar_VariableIntegerValue( "bh_movement" ) != 0;
	CL_ImGuiBeginAutoBox( "race_control_host" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Host" );
	CL_ImGuiInputCvarName( "Name", "name", "Player", ImGuiInputTextFlags_CharsNoBlank );
	CL_ImGuiColorCvarName( "Color", "ls_race_color", raceColorFallback );
	if ( !canEditSettings ) {
		ImGui::TextDisabled( "Race settings are locked after start." );
	}
	ImGui::BeginDisabled( !canEditSettings );
	CL_ImGuiComboCvarName( "Run Mode", "ls_mode", "0", modeLabels, modeValues, IM_ARRAYSIZE( modeValues ) );
	if ( mode == 1 ) {
		CL_ImGuiComboCvarName( "Chapter", "ls_mission", "1", missionLabels, missionValues, IM_ARRAYSIZE( missionValues ) );
	}
	if ( mode == 2 ) {
		CL_ImGuiStringComboCvarName( "IL Map", "ls_map", "escape1", s_mapLabels, s_mapValues, IM_ARRAYSIZE( s_mapValues ) );
	}
	CL_ImGuiBoolCvarName( "100% Category", "ls_100pct", "0" );
	CL_ImGuiCommandComboCvarName( "Difficulty", "g_gameskill", "2", diffLabels, diffValues, IM_ARRAYSIZE( diffValues ), "livesplit_sv_diff" );
	CL_ImGuiBoolCvarName( "HL1 Bhop Physics", "bh_movement", "0" );
	ImGui::BeginDisabled( !hl1 );
	CL_ImGuiBoolCvarName( "Auto Jump", "bh_autojump", "0" );
	ImGui::EndDisabled();
	CL_ImGuiBoolCvarName( "Anti-Cheat", "ls_race_anticheat", "1" );
	ImGui::EndDisabled();
	CL_ImGuiIntSliderCvarName( "Countdown", "ls_race_countdown", "5", 1, 30 );
	CL_ImGuiBoolCvarName( "Hide Address", "ls_race_hide_ip", "1" );
	if ( ImGui::BeginTable( "race_host_buttons", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Host", "ls_race_host", 0.0f, canHost, "Leave the current race before hosting a new lobby." );
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Start", "ls_race_start", 0.0f, canStart, "Only the host can start while the lobby is waiting." );
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Leave", "ls_race_leave", 0.0f, canLeave, "You are not in a race lobby." );
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Chat", "ls_race_open_chat", 0.0f, canChat, "Join or host a race before opening Race chat." );
		ImGui::EndTable();
	}
	ImGui::EndChild();
}

static void CL_ImGuiRaceDrawJoinBox( bool hideIp, bool isHost, bool isClient, bool canJoinManual, bool canConnect, bool canRefresh ) {
	CL_ImGuiBeginAutoBox( "race_control_join" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Join" );
	CL_ImGuiInputCvarName( "Host IP", "ls_race_ip", "127.0.0.1", ImGuiInputTextFlags_CharsNoBlank | ( hideIp ? ImGuiInputTextFlags_Password : 0 ) );
	CL_ImGuiIntInputCvarName( "UDP Port", "ls_race_port", "27960", 1, 65535 );
	if ( ImGui::BeginTable( "race_join_buttons", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Join Manual", "ls_race_join", 0.0f, canJoinManual, isHost ? "Leave hosted lobby before joining another race." : "Leave the current race before joining another lobby." );
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( isClient ? "Reconnect" : "Connect", "ls_race_connect", 0.0f, canConnect, "Hosts cannot connect to themselves." );
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Refresh LAN", "ls_race_refresh", 0.0f, canRefresh, "LAN scan is unavailable while hosting." );
		ImGui::EndTable();
	}
	ImGui::TextDisabled( "%s", Cvar_VariableString( "ls_race_found_status" ) );
	ImGui::EndChild();
}

static void CL_ImGuiRaceDrawBackgroundDots( void ) {
	ImDrawList *draw;
	ImVec2 winPos, winSize;
	float t;
	int i;
	if ( !s_imguiAnimations || s_imguiAnimations->integer == 0 ) return;
	draw = ImGui::GetWindowDrawList();
	winPos = ImGui::GetWindowPos();
	winSize = ImGui::GetWindowSize();
	t = (float)ImGui::GetTime();
	draw->PushClipRect( winPos, ImVec2( winPos.x + winSize.x, winPos.y + winSize.y ), true );
	for ( i = 0; i < 18; ++i ) {
		float lane = (float)( ( i * 37 ) % 100 ) * 0.01f;
		float x = winPos.x + fmodf( winSize.x + 96.0f + (float)i * 61.0f - t * 26.0f, winSize.x + 128.0f ) - 64.0f;
		float y = winPos.y + 58.0f + lane * ( winSize.y - 94.0f );
		float pulse = 0.5f + 0.5f * sinf( t * 1.8f + (float)i * 0.53f );
		int alpha = 24 + (int)( pulse * 34.0f );
		draw->AddCircleFilled( ImVec2( x, y ), 1.9f + pulse * 1.2f, CL_ImGuiGuiAccentU32( alpha / 255.0f ), 12 );
	}
	draw->PopClipRect();
}

static void CL_ImGuiRaceDrawLobbyList( int foundCount, bool canJoinFound ) {
	int rowIndex;
	CL_ImGuiBeginAutoBox( "race_control_lobbies" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Local lobbies" );
	if ( foundCount <= 0 ) {
		ImGui::TextDisabled( "No local Race lobbies found." );
	} else {
		for ( rowIndex = 0; rowIndex < 4; ++rowIndex ) {
			CL_ImGuiRaceFoundLobbyRow( rowIndex, canJoinFound );
		}
	}
	ImGui::EndChild();
}

static void CL_ImGuiRaceDrawSnapshotBox( const lsRaceUiSnapshot_t *race, bool canStart, bool canLeave, bool canChat ) {
	CL_ImGuiBeginAutoBox( "race_control_state" );
	CL_ImGuiMiniStat( "Stage", race->stage[0] ? race->stage : "-" );
	CL_ImGuiMiniStat( "Stage IGT", race->stageIgt[0] ? race->stageIgt : "--" );
	CL_ImGuiMiniStat( "Checks", race->flags[0] ? race->flags : "sv_cheats 0 | god off | noclip off" );
	if ( race->countdownText[0] ) CL_ImGuiMiniStat( "Countdown", race->countdownText );
	ImGui::Separator();
	CL_ImGuiRaceCommandButton( "Start", "ls_race_start", 104.0f, canStart, "Only the host can start while the lobby is waiting." );
	CL_ImGuiSameLineIfFits( 112.0f );
	CL_ImGuiRaceCommandButton( "Open Chat", "ls_race_open_chat", 104.0f, canChat, "Join or host a race before opening Race chat." );
	CL_ImGuiSameLineIfFits( 108.0f );
	CL_ImGuiRaceCommandButton( "Leave", "ls_race_leave", 96.0f, canLeave, "You are not in a race lobby." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawRaceControlWindow( void ) {
	static const float raceColorFallback[4] = { 0.31f, 0.70f, 1.00f, 1.00f };
	static int selectedTab = 0;
	lsRaceUiSnapshot_t race;
	srRaceRow_t rows[8];
	bool isHost;
	bool isClient;
	bool isLobby;
	bool canHost;
	bool canStart;
	bool canLeave;
	bool canChat;
	bool canJoinManual;
	bool canConnect;
	bool canRefresh;
	bool canJoinFound;
	bool hideIp;
	int rowIndex;
	int rowCount;
	int foundCount;
	float alpha;
	ImGuiWindowFlags flags;

	LS_RaceBuildSnapshot( &race );
	rowCount = race.playerCount;
	if ( rowCount > IM_ARRAYSIZE( rows ) ) rowCount = IM_ARRAYSIZE( rows );
	for ( rowIndex = 0; rowIndex < rowCount; ++rowIndex ) {
		rows[rowIndex] = race.players[rowIndex];
		CL_ImGuiRacePrettyToken( rows[rowIndex].stage );
	}
	isHost = !Q_stricmp( race.role, "Host" );
	isClient = !Q_stricmp( race.role, "Client" );
	isLobby = !Q_stricmp( race.state, "Lobby" );
	hideIp = Cvar_VariableIntegerValue( "ls_race_hide_ip" ) != 0;
	foundCount = Cvar_VariableIntegerValue( "ls_race_found_count" );
	canHost = race.active == 0;
	canStart = isHost && isLobby;
	canLeave = race.active != 0;
	canChat = race.active != 0;
	canJoinManual = !isHost && race.active == 0;
	canConnect = !isHost;
	canRefresh = !isHost;
	canJoinFound = !isHost && race.active == 0;
	if ( selectedTab > 2 ) selectedTab = 0;
	alpha = s_imguiAlpha ? Com_Clamp( 0.72f, 1.0f, s_imguiAlpha->value ) : 0.96f;
	flags = ImGuiWindowFlags_NoCollapse | ( s_raceGuiPinned ? ImGuiWindowFlags_NoMove : 0 );

	CL_ImGuiRaceControlStylePush();
	ImGui::SetNextWindowPos( ImVec2( s_raceGuiWindowX ? s_raceGuiWindowX->value : 42.0f, s_raceGuiWindowY ? s_raceGuiWindowY->value : 34.0f ), ImGuiCond_Appearing );
	ImGui::SetNextWindowSize( ImVec2( s_raceGuiWindowW ? s_raceGuiWindowW->value : 900.0f, s_raceGuiWindowH ? s_raceGuiWindowH->value : 640.0f ), ImGuiCond_Appearing );
	ImGui::SetNextWindowSizeConstraints( ImVec2( 460, 430 ), ImVec2( (float)cls.glconfig.vidWidth, (float)cls.glconfig.vidHeight ) );
	ImGui::SetNextWindowBgAlpha( alpha );
	if ( !ImGui::Begin( "Race Control", &s_raceGuiOpen, flags ) ) {
		ImGui::End();
		CL_ImGuiRaceControlStylePop();
		return;
	}
	CL_ImGuiRaceDrawBackgroundDots();
	{
		ImVec2 winPos = ImGui::GetWindowPos();
		ImVec2 winSize = ImGui::GetWindowSize();
		if ( s_raceGuiWindowX ) Cvar_SetValue( s_raceGuiWindowX->name, winPos.x );
		if ( s_raceGuiWindowY ) Cvar_SetValue( s_raceGuiWindowY->name, winPos.y );
		if ( s_raceGuiWindowW ) Cvar_SetValue( s_raceGuiWindowW->name, winSize.x );
		if ( s_raceGuiWindowH ) Cvar_SetValue( s_raceGuiWindowH->name, winSize.y );
	}
	CL_ImGuiRaceControlHeader( &race, rowCount );
	if ( ImGui::SmallButton( s_raceGuiPinned ? "Unpin" : "Pin" ) ) {
		s_raceGuiPinned = !s_raceGuiPinned;
	}
	CL_ImGuiSameLineIfFits( 128.0f );
	if ( ImGui::SmallButton( "Speedrun Settings" ) ) {
		s_raceGuiOpen = false;
		s_imguiCategory = 11;
		CL_SpeedrunImGui_Open();
	}
	CL_ImGuiSameLineIfFits( 58.0f );
	if ( ImGui::SmallButton( "Close" ) ) {
		s_raceGuiOpen = false;
	}
	ImGui::Separator();

	if ( ImGui::BeginTabBar( "race_control_tabs" ) ) {
		if ( ImGui::BeginTabItem( "Lobby" ) ) { selectedTab = 0; ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Roster" ) ) { selectedTab = 1; ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Status" ) ) { selectedTab = 2; ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}

	if ( selectedTab == 0 ) {
		CL_ImGuiRaceDrawHostBox( raceColorFallback, canHost, canStart, canLeave, canChat, race.active == 0 || ( isHost && isLobby ) );
		ImGui::Spacing();
		CL_ImGuiRaceDrawJoinBox( hideIp, isHost, isClient, canJoinManual, canConnect, canRefresh );
		ImGui::Spacing();
		CL_ImGuiRaceDrawLobbyList( foundCount, canJoinFound );
	} else if ( selectedTab == 1 ) {
		float tableHeight = ImGui::GetContentRegionAvail().y * 0.45f;
		if ( tableHeight < 260.0f ) tableHeight = 260.0f;
		CL_ImGuiDrawRaceRowsTable( "race_control_roster", rows, rowCount, tableHeight, false );
		ImGui::Separator();
		CL_ImGuiRaceDrawChatLog( &race, 132.0f );
		CL_ImGuiRaceDrawChatComposer( canChat );
		ImGui::Spacing();
		CL_ImGuiRaceDrawSnapshotBox( &race, canStart, canLeave, canChat );
	} else if ( selectedTab == 2 ) {
		CL_ImGuiRaceDrawSnapshotBox( &race, canStart, canLeave, canChat );
		ImGui::Spacing();
		CL_ImGuiBeginAutoBox( "race_control_network" );
		CL_ImGuiMiniStat( "LAN Status", Cvar_VariableString( "ls_race_found_status" ) );
		CL_ImGuiMiniStat( "Found", va( "%d", foundCount ) );
		CL_ImGuiMiniStat( "Ready", race.ready[0] ? race.ready : "0/0" );
		CL_ImGuiRaceCommandButton( "Refresh LAN", "ls_race_refresh", 124.0f, canRefresh, "LAN scan is unavailable while hosting." );
		CL_ImGuiSameLineIfFits( 132.0f );
		CL_ImGuiRaceCommandButton( isClient ? "Reconnect" : "Connect", "ls_race_connect", 124.0f, canConnect, "Hosts cannot connect to themselves." );
		ImGui::EndChild();
	}

	ImGui::End();
	CL_ImGuiRaceControlStylePop();
}

static void CL_ImGuiDrawRacePage( void ) {
	static const float raceColorFallback[4] = { 0.31f, 0.70f, 1.00f, 1.00f };
	static const float chatBgFallback[4] = { 0.03f, 0.04f, 0.03f, 0.50f };
	static const float chatTextFallback[4] = { 0.89f, 0.94f, 0.86f, 0.95f };
	static const float chatInputBgFallback[4] = { 0.03f, 0.04f, 0.03f, 0.82f };
	static const float chatInputTextFallback[4] = { 0.88f, 0.94f, 0.84f, 1.00f };
	static const char *ghostRenderLabels[] = { "Transparent", "Textured Tint", "Normal", "X-Ray" };
	static const int ghostRenderValues[] = { 0, 2, 3, 4 };
	ImDrawList *draw;
	ImVec2 p;
	float avail;
	float t;
	int dot;

	CL_ImGuiSectionHeader( "Race Styling", "Visual-only options for the Race overlay, remote ghosts and nametags." );

	CL_ImGuiBeginAutoBox( "race_style_overlay" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Overlay" );
	CL_ImGuiBoolCvarName( "Race HUD", "ls_race_overlay", "1" );
	CL_ImGuiBoolCvarName( "Race Chat", "ls_race_chat", "1" );
	CL_ImGuiBoolCvarName( "Roster Progress", "ls_race_overlay_progress", "0" );
	CL_ImGuiBoolCvarName( "Center Countdown", "ls_race_countdown_center", "1" );
	CL_ImGuiSliderCvarName( "Overlay X", "ls_race_overlay_x", "8", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Overlay Y", "ls_race_overlay_y", "72", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Overlay Width", "ls_race_overlay_w", "110", 110.0f, 500.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Overlay Scale", "ls_race_overlay_scale", "0.5", 0.35f, 1.80f, "%.2f" );
	CL_ImGuiSliderCvarName( "Overlay Opacity", "ls_race_overlay_opacity", "0.90", 0.20f, 1.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Countdown Scale", "ls_race_countdown_scale", "1.0", 0.50f, 2.50f, "%.2f" );
	ImGui::EndChild();

	ImGui::Spacing();
	CL_ImGuiBeginAutoBox( "race_style_chat" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Chat" );
	CL_ImGuiSliderCvarName( "Chat X", "ls_race_chat_x", "12", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Chat Y", "ls_race_chat_y", "300", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Chat Width", "ls_race_chat_w", "285", 160.0f, 620.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Chat Scale", "ls_race_chat_scale", "1.0", 0.50f, 2.00f, "%.2f" );
	CL_ImGuiColorCvarName( "Chat Background", "ls_race_chat_bg", chatBgFallback );
	CL_ImGuiColorCvarName( "Chat Text", "ls_race_chat_text", chatTextFallback );
	ImGui::Separator();
	CL_ImGuiSliderCvarName( "Input X", "ls_race_chat_input_x", "12", 0.0f, 640.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Input Y", "ls_race_chat_input_y", "444", 0.0f, 480.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Input Width", "ls_race_chat_input_w", "285", 160.0f, 620.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Input Scale", "ls_race_chat_input_scale", "1.0", 0.50f, 2.00f, "%.2f" );
	CL_ImGuiColorCvarName( "Input Background", "ls_race_chat_input_bg", chatInputBgFallback );
	CL_ImGuiColorCvarName( "Input Text", "ls_race_chat_input_text", chatInputTextFallback );
	ImGui::EndChild();

	ImGui::Spacing();
	CL_ImGuiBeginAutoBox( "race_style_identity" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Identity" );
	CL_ImGuiColorCvarName( "Player Color", "ls_race_color", raceColorFallback );
	ImGui::EndChild();

	ImGui::Spacing();
	CL_ImGuiBeginAutoBox( "race_style_ghosts" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Remote ghosts" );
	CL_ImGuiBoolCvarName( "Remote Ghosts", "ls_race_ghosts", "1" );
	CL_ImGuiIntSliderCvarName( "Model Opacity", "ls_race_ghost_alpha", "120", 0, 255 );
	CL_ImGuiComboCvarName( "Render Mode", "ls_race_ghost_render", "0", ghostRenderLabels, ghostRenderValues, IM_ARRAYSIZE( ghostRenderValues ) );
	ImGui::EndChild();

	ImGui::Spacing();
	CL_ImGuiBeginAutoBox( "race_style_nametags" );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "Nametags" );
	CL_ImGuiBoolCvarName( "Nametags", "ls_race_nametag", "1" );
	CL_ImGuiBoolCvarName( "HP / Armor / Ammo", "ls_race_nametag_stats", "1" );
	CL_ImGuiBoolCvarName( "Weapon Icons", "ls_race_nametag_icons", "1" );
	CL_ImGuiSliderCvarName( "Nametag Scale", "ls_race_nametag_scale", "1.0", 0.50f, 2.00f, "%.2f" );
	CL_ImGuiSliderCvarName( "Nametag Opacity", "ls_race_nametag_opacity", "1.0", 0.15f, 1.00f, "%.2f" );
	ImGui::EndChild();

	if ( s_imguiAnimations && s_imguiAnimations->integer != 0 ) {
		ImGui::Dummy( ImVec2( 1.0f, 18.0f ) );
		draw = ImGui::GetWindowDrawList();
		p = ImGui::GetCursorScreenPos();
		avail = ImGui::GetContentRegionAvail().x;
		t = (float)ImGui::GetTime();
		for ( dot = 0; dot < 5; ++dot ) {
			float wave = sinf( t * 2.6f - (float)dot * 0.75f );
			float x = p.x + avail - 18.0f - (float)dot * 18.0f;
			float r = 3.2f + 1.6f * ( 0.5f + 0.5f * wave );
			int alpha = 74 + (int)( 62.0f * ( 0.5f + 0.5f * wave ) );
			draw->AddCircleFilled( ImVec2( x, p.y + 5.0f ), r + 2.0f, CL_ImGuiGuiAccentU32( 0.10f ), 18 );
			draw->AddCircleFilled( ImVec2( x, p.y + 5.0f ), r, IM_COL32( 126, 232, 84, alpha ), 18 );
		}
		ImGui::Dummy( ImVec2( 1.0f, 12.0f ) );
	}
}