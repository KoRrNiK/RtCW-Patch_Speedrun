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
	ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 8.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 7.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 5.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_GrabRounding, 4.0f );
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 14.0f, 12.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f, 6.0f ) );
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 9.0f, 8.0f ) );
	ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.022f, 0.027f, 0.026f, 0.98f ) );
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.040f, 0.047f, 0.045f, 0.94f ) );
	ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( accent.x * 0.34f + 0.08f, accent.y * 0.34f + 0.08f, accent.z * 0.34f + 0.08f, 0.50f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.075f, 0.086f, 0.083f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( accent.x * 0.13f + 0.07f, accent.y * 0.13f + 0.08f, accent.z * 0.13f + 0.07f, 0.98f ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( accent.x * 0.22f + 0.06f, accent.y * 0.22f + 0.08f, accent.z * 0.22f + 0.06f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.090f, 0.105f, 0.100f, 0.96f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( accent.x * 0.24f + 0.06f, accent.y * 0.24f + 0.07f, accent.z * 0.24f + 0.06f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( accent.x * 0.36f + 0.05f, accent.y * 0.36f + 0.06f, accent.z * 0.36f + 0.05f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( accent.x * 0.14f + 0.04f, accent.y * 0.14f + 0.05f, accent.z * 0.14f + 0.04f, 0.84f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( accent.x * 0.24f + 0.05f, accent.y * 0.24f + 0.06f, accent.z * 0.24f + 0.05f, 0.95f ) );
	ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( accent.x * 0.34f + 0.05f, accent.y * 0.34f + 0.06f, accent.z * 0.34f + 0.05f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_TitleBg, ImVec4( 0.030f, 0.038f, 0.036f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_TitleBgActive, ImVec4( 0.052f, 0.072f, 0.060f, 1.00f ) );
}

static void CL_ImGuiRaceControlStylePop( void ) {
	ImGui::PopStyleColor( 14 );
	ImGui::PopStyleVar( 7 );
}

static bool CL_ImGuiRaceStatusWarning( const char *status ) {
	if ( !status || !status[0] ) return false;
	return strstr( status, "Cannot" ) || strstr( status, "Invalid" ) || strstr( status, "rejected" ) ||
		   strstr( status, "timeout" ) || strstr( status, "unavailable" ) || strstr( status, "blocked" );
}

static ImU32 CL_ImGuiRaceStatusDotColor( const lsRaceUiSnapshot_t *race ) {
	const char *state = race && race->state[0] ? race->state : "Idle";
	if ( race && CL_ImGuiRaceStatusWarning( race->status ) ) return IM_COL32( 232, 82, 58, 255 );
	if ( !Q_stricmp( state, "Racing" ) ) return IM_COL32( 86, 210, 76, 255 );
	if ( !Q_stricmp( state, "Countdown" ) ) return IM_COL32( 255, 204, 82, 255 );
	if ( !Q_stricmp( state, "Loading" ) ) return IM_COL32( 80, 170, 255, 255 );
	if ( !Q_stricmp( state, "Lobby" ) ) return IM_COL32( 112, 216, 128, 255 );
	if ( !Q_stricmp( state, "Finished" ) ) return IM_COL32( 255, 210, 80, 255 );
	return IM_COL32( 124, 136, 128, 255 );
}

static void CL_ImGuiRaceInlinePill( const char *label, ImU32 bg, ImU32 fg = IM_COL32( 230, 240, 224, 255 ) ) {
	ImDrawList *draw;
	ImVec2 pos;
	ImVec2 textSize;
	ImVec2 size;
	if ( !label || !label[0] ) return;
	draw = ImGui::GetWindowDrawList();
	pos = ImGui::GetCursorScreenPos();
	textSize = ImGui::CalcTextSize( label );
	size = ImVec2( textSize.x + 14.0f, textSize.y + 6.0f );
	draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ), bg, 4.0f );
	draw->AddText( ImVec2( pos.x + 7.0f, pos.y + 3.0f ), fg, label );
	ImGui::Dummy( size );
}

static void CL_ImGuiRaceSectionTitle( const char *title, const char *right = NULL ) {
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "%s", title );
	if ( right && right[0] && CL_ImGuiSameLineIfFits( ImGui::CalcTextSize( right ).x + 8.0f ) ) {
		ImGui::TextDisabled( "%s", right );
	}
	ImGui::Separator();
}

static void CL_ImGuiRaceActionButton( const char *label, const char *command, bool enabled, const char *disabledHint, const ImVec4 &accent, float width = 0.0f ) {
	ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( accent.x * 0.52f, accent.y * 0.52f, accent.z * 0.52f, 0.96f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( accent.x * 0.68f, accent.y * 0.68f, accent.z * 0.68f, 1.00f ) );
	ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, 1.00f ) );
	CL_ImGuiRaceCommandButton( label, command, width, enabled, disabledHint );
	ImGui::PopStyleColor( 3 );
}

static void CL_ImGuiRaceDrawTopBar( const lsRaceUiSnapshot_t *race, int rowCount ) {
	ImDrawList *draw = ImGui::GetWindowDrawList();
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImU32 dot = CL_ImGuiRaceStatusDotColor( race );
	const char *status = race->status[0] ? race->status : "Idle";
	bool warning = CL_ImGuiRaceStatusWarning( status );
	char countText[32];
	char readyText[48];

	Com_sprintf( countText, sizeof( countText ), "%d runner%s", rowCount, rowCount == 1 ? "" : "s" );
	Com_sprintf( readyText, sizeof( readyText ), "Ready %s", race->ready[0] ? race->ready : "0/0" );

	draw->AddCircleFilled( ImVec2( pos.x + 8.0f, pos.y + 10.0f ), 5.0f, dot, 16 );
	ImGui::Indent( 22.0f );
	ImGui::TextColored( CL_ImGuiRaceAccentVec4(), "RtCW Race" );
	ImGui::SameLine();
	CL_ImGuiRaceInlinePill( race->state[0] ? race->state : "Idle", CL_ImGuiRaceStateBgU32( race->state ) );
	ImGui::SameLine();
	ImGui::TextDisabled( "%s", race->role[0] ? race->role : "Idle" );
	ImGui::Unindent( 22.0f );

	ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - 214.0f );
	if ( ImGui::SmallButton( s_raceGuiPinned ? "Unpin" : "Pin" ) ) {
		s_raceGuiPinned = !s_raceGuiPinned;
	}
	ImGui::SameLine();
	if ( ImGui::SmallButton( "Settings" ) ) {
		s_raceGuiOpen = false;
		s_imguiCategory = 11;
		CL_SpeedrunImGui_Open();
	}
	ImGui::SameLine();
	if ( ImGui::SmallButton( "Close" ) ) {
		s_raceGuiOpen = false;
	}

	ImGui::TextColored( warning ? ImVec4( 1.0f, 0.34f, 0.25f, 1.0f ) : ImVec4( 0.70f, 0.78f, 0.70f, 1.0f ), "%s", status );
	ImGui::SameLine();
	ImGui::TextDisabled( "| %s | %s | %s", race->category[0] ? race->category : "Full Game", countText, readyText );
	ImGui::Separator();
}

static void CL_ImGuiRaceFoundLobbyRow( int index, bool canJoin ) {
	char cvarName[32];
	char lobbyText[192];
	char command[64];
	float buttonW = 58.0f;
	Com_sprintf( cvarName, sizeof( cvarName ), "ls_race_found_%d", index );
	Cvar_VariableStringBuffer( cvarName, lobbyText, sizeof( lobbyText ) );
	if ( !lobbyText[0] ) return;
	Com_sprintf( command, sizeof( command ), "ls_race_join_found %d", index );
	ImGui::PushID( index );
	ImGui::AlignTextToFramePadding();
	ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonW - ImGui::GetStyle().ItemSpacing.x );
	ImGui::TextUnformatted( lobbyText );
	ImGui::PopTextWrapPos();
	ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - buttonW );
	CL_ImGuiRaceCommandButton( "Join", command, buttonW, canJoin, "Leave the current race before joining another lobby." );
	ImGui::PopID();
}

static void CL_ImGuiRaceDrawChatLog( const lsRaceUiSnapshot_t *race, float height ) {
	int i;
	ImGui::BeginChild( "race_control_chat_log", ImVec2( 0, height ), true, ImGuiWindowFlags_HorizontalScrollbar );
	for ( i = 0; i < race->chatCount && i < LS_RACE_UI_CHAT_LINES; ++i ) {
		if ( race->chat[i].text[0] ) {
			ImVec4 nickColor = ImVec4( race->chat[i].red / 255.0f, race->chat[i].green / 255.0f, race->chat[i].blue / 255.0f, 1.0f );
			ImGui::TextColored( nickColor, "%s", race->chat[i].nick[0] ? race->chat[i].nick : "Runner" );
			ImGui::SameLine( 0.0f, 6.0f );
			ImGui::PushTextWrapPos( 0.0f );
			ImGui::TextUnformatted( race->chat[i].text );
			ImGui::PopTextWrapPos();
		} else if ( race->chatLines[i][0] ) {
			ImGui::TextWrapped( "%s", race->chatLines[i] );
		}
	}
	if ( race->chatCount <= 0 ) ImGui::TextDisabled( "No Race chat yet." );
	ImGui::EndChild();
}

static void CL_ImGuiRaceDrawChatComposer( bool canChat ) {
	ImGuiStyle &style = ImGui::GetStyle();
	float sendWidth = 82.0f;
	float avail = ImGui::GetContentRegionAvail().x;
	bool sendNow;
	ImGui::BeginDisabled( !canChat );
	ImGui::SetNextItemWidth( avail - sendWidth - style.ItemSpacing.x );
	sendNow = ImGui::InputTextWithHint( "##race_control_chat_input", "message...", s_raceControlChatInput, sizeof( s_raceControlChatInput ), ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	sendNow = ImGui::Button( "Send", ImVec2( sendWidth, 0.0f ) ) || sendNow;
	if ( sendNow ) CL_ImGuiRaceSubmitChatInput( s_raceControlChatInput, sizeof( s_raceControlChatInput ) );
	ImGui::EndDisabled();
	CL_ImGuiOptionTooltip( "Race chat", "ls_race_say", canChat ? "Sends a message to the active Race lobby." : "Join a race before chatting." );
}

static void CL_ImGuiRaceDrawConnectionPanel( bool hideIp, bool isHost, bool canJoin, bool canConnect, bool canRefresh ) {
	static const float raceColorFallback[4] = { 0.31f, 0.70f, 1.00f, 1.00f };
	cvar_t *hostIp;
	char hostIpBuf[128];
	bool showIp = !hideIp;
	CL_ImGuiRaceSectionTitle( "Connect" );
	CL_ImGuiInputCvarName( "Name", "name", "Player", ImGuiInputTextFlags_None );
	CL_ImGuiColorCvarName( "Color", "ls_race_color", raceColorFallback );

	hostIp = CL_ImGuiCvar( "ls_race_ip", "127.0.0.1" );
	Q_strncpyz( hostIpBuf, hostIp && hostIp->string ? hostIp->string : "127.0.0.1", sizeof( hostIpBuf ) );
	ImGui::PushID( "race_host_ip_inline" );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( "Host IP" );
	CL_ImGuiOptionTooltip( "Host IP", "ls_race_ip", "Race host address." );
	{
		float avail = ImGui::GetContentRegionAvail().x;
		float checkboxW = ImGui::GetFrameHeight();
		bool inlineCheckbox = avail >= checkboxW + ImGui::GetStyle().ItemSpacing.x + 88.0f;
		float inputW = inlineCheckbox ? avail - checkboxW - ImGui::GetStyle().ItemSpacing.x : avail;
		ImGui::SetNextItemWidth( inputW );
		if ( ImGui::InputText( "##host_ip", hostIpBuf, sizeof( hostIpBuf ), ImGuiInputTextFlags_CharsNoBlank | ( hideIp ? ImGuiInputTextFlags_Password : 0 ) ) && hostIp ) {
			Cvar_Set( hostIp->name, hostIpBuf );
		}
		CL_ImGuiOptionTooltip( "Host IP", "ls_race_ip", "Race host address." );
		if ( inlineCheckbox ) {
			ImGui::SameLine();
			if ( ImGui::Checkbox( "##show_ip", &showIp ) ) {
				Cvar_SetValue( "ls_race_hide_ip", showIp ? 0.0f : 1.0f );
			}
			CL_ImGuiOptionTooltip( "Show IP", "ls_race_hide_ip", "Shows or hides the Host IP field." );
		}
	}
	ImGui::PopID();

	CL_ImGuiIntInputCvarName( "Port", "ls_race_port", "27960", 1, 65535 );
	CL_ImGuiInputCvarName( "Password", "ls_race_password", "", ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_Password );
	ImGui::Spacing();
	CL_ImGuiRaceActionButton( "Join", "ls_race_connect", canConnect, isHost ? "Leave hosted lobby before joining another race." : "Cannot connect right now.", ImVec4( 0.25f, 0.68f, 1.00f, 1.0f ), 0.0f );
	if ( ImGui::BeginTable( "race_connect_secondary", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Direct", "ls_race_join", 0.0f, canJoin, "Leave the current race before joining another lobby." );
		ImGui::TableNextColumn(); CL_ImGuiRaceCommandButton( "Scan", "ls_race_refresh", 0.0f, canRefresh, "Scans LAN and the selected host port." );
		ImGui::EndTable();
	}
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
	CL_ImGuiRaceSectionTitle( "Lobbies", Cvar_VariableString( "ls_race_found_status" ) );
	if ( foundCount <= 0 ) {
		ImGui::TextDisabled( "No local Race lobbies found." );
	} else {
		for ( rowIndex = 0; rowIndex < 4; ++rowIndex ) {
			CL_ImGuiRaceFoundLobbyRow( rowIndex, canJoinFound );
		}
	}
}

static void CL_ImGuiRaceDrawSnapshotPanel( const lsRaceUiSnapshot_t *race ) {
	CL_ImGuiRaceSectionTitle( "Run" );
	if ( ImGui::BeginTable( "race_snapshot_grid", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Timer", race->timer[0] ? race->timer : "0.00" );
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Stage IGT", race->stageIgt[0] ? race->stageIgt : "--" );
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Stage", race->stage[0] ? race->stage : "-" );
		ImGui::TableNextColumn(); CL_ImGuiMiniStat( "Ready", race->ready[0] ? race->ready : "0/0" );
		ImGui::EndTable();
	}
	if ( race->countdownText[0] ) CL_ImGuiMiniStat( "Countdown", race->countdownText );
	CL_ImGuiMiniStat( "Checks", race->flags[0] ? race->flags : "sv_cheats 0 | god off | noclip off" );
}

static void CL_ImGuiRaceDrawActionStrip( const lsRaceUiSnapshot_t *race, bool isHost, bool canLeave, bool canChat, bool canConnect, bool canRefresh ) {
	float full = ImGui::GetContentRegionAvail().x;
	float buttonW = ( full - ImGui::GetStyle().ItemSpacing.x * 3.0f ) / 4.0f;
	if ( buttonW < 108.0f ) buttonW = 0.0f;
	CL_ImGuiRaceActionButton( canLeave ? "Leave Race" : "Join Race", canLeave ? "ls_race_leave" : "ls_race_connect",
		canLeave || canConnect, canLeave ? "You are not in a race lobby." : "Hosts cannot connect to themselves.",
		canLeave ? ImVec4( 0.95f, 0.30f, 0.22f, 1.0f ) : ImVec4( 0.25f, 0.68f, 1.00f, 1.0f ), buttonW );
	if ( buttonW > 0.0f ) ImGui::SameLine();
	CL_ImGuiRaceActionButton( "Chat", "ls_race_open_chat", canChat, "Join a race before opening chat.", ImVec4( 0.42f, 0.86f, 0.36f, 1.0f ), buttonW );
	if ( buttonW > 0.0f ) ImGui::SameLine();
	CL_ImGuiRaceCommandButton( "Scan", "ls_race_refresh", buttonW, canRefresh, "Scans LAN and the selected host port." );
	if ( buttonW > 0.0f ) ImGui::SameLine();
	CL_ImGuiRaceCommandButton( isHost ? "Start" : "Reconnect", isHost ? "ls_race_start" : "ls_race_connect", buttonW,
		isHost || canConnect, isHost ? "Only the in-game host can start here." : "Hosts cannot connect to themselves." );
	if ( race->countdownText[0] ) {
		ImGui::SameLine();
		CL_ImGuiRaceInlinePill( race->countdownText, IM_COL32( 255, 198, 70, 210 ), IM_COL32( 22, 16, 6, 255 ) );
	}
}

static void CL_ImGuiDrawRaceControlWindow( void ) {
	lsRaceUiSnapshot_t race;
	srRaceRow_t rows[8];
	bool isHost;
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
	float leftW;
	float rightW;
	float contentH;
	float connectH;
	float lobbyH;
	float rosterH;
	float bottomH;
	float rosterTableH;
	ImGuiWindowFlags flags;

	LS_RaceBuildSnapshot( &race );
	rowCount = race.playerCount;
	if ( rowCount > IM_ARRAYSIZE( rows ) ) rowCount = IM_ARRAYSIZE( rows );
	for ( rowIndex = 0; rowIndex < rowCount; ++rowIndex ) {
		rows[rowIndex] = race.players[rowIndex];
		CL_ImGuiRacePrettyToken( rows[rowIndex].stage );
	}
	isHost = !Q_stricmp( race.role, "Host" );
	hideIp = Cvar_VariableIntegerValue( "ls_race_hide_ip" ) != 0;
	foundCount = Cvar_VariableIntegerValue( "ls_race_found_count" );
	canLeave = race.active != 0;
	canChat = race.active != 0;
	canJoinManual = !isHost && race.active == 0;
	canConnect = !isHost;
	canRefresh = !isHost;
	canJoinFound = !isHost && race.active == 0;
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
	CL_ImGuiRaceDrawTopBar( &race, rowCount );
	CL_ImGuiRaceDrawActionStrip( &race, isHost, canLeave, canChat, canConnect, canRefresh );
	ImGui::Spacing();

	leftW = ImGui::GetContentRegionAvail().x * 0.34f;
	if ( leftW < 270.0f ) leftW = 270.0f;
	if ( leftW > 360.0f ) leftW = 360.0f;
	rightW = ImGui::GetContentRegionAvail().x - leftW - ImGui::GetStyle().ItemSpacing.x;
	contentH = ImGui::GetContentRegionAvail().y;
	if ( contentH < 360.0f ) contentH = 360.0f;
	connectH = 436.0f;
	if ( contentH < connectH + 96.0f + ImGui::GetStyle().ItemSpacing.y ) {
		connectH = contentH - 96.0f - ImGui::GetStyle().ItemSpacing.y;
	}
	if ( connectH < 336.0f ) connectH = 336.0f;
	lobbyH = contentH - connectH - ImGui::GetStyle().ItemSpacing.y;
	if ( lobbyH < 64.0f ) lobbyH = 64.0f;

	ImGui::BeginChild( "race_left_column", ImVec2( leftW, contentH ), false, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::BeginChild( "race_connect_panel", ImVec2( 0, connectH ), true, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	CL_ImGuiRaceDrawConnectionPanel( hideIp, isHost, canJoinManual, canConnect, canRefresh );
	ImGui::EndChild();
	ImGui::Spacing();
	ImGui::BeginChild( "race_lobby_panel", ImVec2( 0, lobbyH ), true, ImGuiWindowFlags_NoSavedSettings );
	CL_ImGuiRaceDrawLobbyList( foundCount, canJoinFound );
	ImGui::EndChild();
	ImGui::EndChild();

	ImGui::SameLine();
	bottomH = contentH * 0.40f;
	if ( bottomH < 210.0f ) bottomH = 210.0f;
	if ( bottomH > 270.0f ) bottomH = 270.0f;
	rosterH = contentH - bottomH - ImGui::GetStyle().ItemSpacing.y;
	if ( rosterH < 178.0f ) {
		rosterH = 178.0f;
		bottomH = contentH - rosterH - ImGui::GetStyle().ItemSpacing.y;
		if ( bottomH < 122.0f ) bottomH = 122.0f;
	}
	ImGui::BeginChild( "race_main_column", ImVec2( rightW, contentH ), false, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::BeginChild( "race_roster_panel", ImVec2( 0, rosterH ), true, ImGuiWindowFlags_NoSavedSettings );
	CL_ImGuiRaceSectionTitle( "Players", race.ready[0] ? race.ready : "0/0" );
	if ( rowCount > 0 ) {
		rosterTableH = ImGui::GetContentRegionAvail().y - 2.0f;
		if ( rosterTableH < 72.0f ) rosterTableH = 72.0f;
		CL_ImGuiDrawRaceRowsTable( "race_control_roster", rows, rowCount, rosterTableH, false );
	} else {
		ImGui::TextDisabled( "No players in lobby." );
	}
	ImGui::EndChild();
	ImGui::Spacing();
	ImGui::BeginChild( "race_bottom_panel", ImVec2( 0, bottomH ), false, ImGuiWindowFlags_NoSavedSettings );
	if ( ImGui::BeginTable( "race_bottom_columns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings ) ) {
		ImGui::TableSetupColumn( "chat", ImGuiTableColumnFlags_WidthStretch, 1.45f );
		ImGui::TableSetupColumn( "run", ImGuiTableColumnFlags_WidthStretch, 1.00f );
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::BeginChild( "race_chat_panel", ImVec2( 0, 0 ), true, ImGuiWindowFlags_NoSavedSettings );
		CL_ImGuiRaceSectionTitle( "Chat" );
		CL_ImGuiRaceDrawChatLog( &race, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - 4.0f );
		CL_ImGuiRaceDrawChatComposer( canChat );
		ImGui::EndChild();
		ImGui::TableNextColumn();
		ImGui::BeginChild( "race_run_panel", ImVec2( 0, 0 ), true, ImGuiWindowFlags_NoSavedSettings );
		CL_ImGuiRaceDrawSnapshotPanel( &race );
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::EndChild();
	ImGui::EndChild();

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
