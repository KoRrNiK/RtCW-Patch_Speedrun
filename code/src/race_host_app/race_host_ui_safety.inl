typedef enum {
    RACE_CONFIRM_NONE = 0,
    RACE_CONFIRM_STOP_HOST,
    RACE_CONFIRM_STOP_RACE,
    RACE_CONFIRM_RESET_LOBBY
} raceConfirmAction_t;

static raceConfirmAction_t gConfirmAction = RACE_CONFIRM_NONE;
static int gConfirmOpen;
static char gConfirmTitle[96];
static char gConfirmText[384];
static char gConfirmButton[64];

static int Race_StateIsActiveRun( void ) {
    return raceHost.state == RACE_STATE_LOADING ||
           raceHost.state == RACE_STATE_COUNTDOWN ||
           raceHost.state == RACE_STATE_RACING;
}

static void Race_OpenConfirm( raceConfirmAction_t action, const char *title, const char *text, const char *button ) {
    gConfirmAction = action;
    gConfirmOpen = 1;
    Race_Copy( gConfirmTitle, sizeof( gConfirmTitle ), title );
    Race_Copy( gConfirmText, sizeof( gConfirmText ), text );
    Race_Copy( gConfirmButton, sizeof( gConfirmButton ), button );
}

static void Race_RequestStopHost( void ) {
    if ( Race_HostIsRunning() && ( Race_StateIsActiveRun() || Race_PlayerCount() > 0 || Race_QueueCount() > 0 ) ) {
        Race_OpenConfirm(
            RACE_CONFIRM_STOP_HOST,
            "Stop host?",
            "This will close the lobby socket, notify connected players, clear the roster, and drop the current queue.",
            "Stop Host" );
        return;
    }
    Race_StopHost();
}

static void Race_RequestStopRace( void ) {
    if ( Race_StateIsActiveRun() ) {
        Race_OpenConfirm(
            RACE_CONFIRM_STOP_RACE,
            "Stop race?",
            "The current race will be stopped and players will be returned to lobby state.",
            "Stop Race" );
        return;
    }
    Race_StopRace();
}

static void Race_RequestResetLobby( void ) {
    if ( Race_StateIsActiveRun() ) {
        Race_OpenConfirm(
            RACE_CONFIRM_RESET_LOBBY,
            "Reset lobby?",
            "This resets the current session, clears load/start state, and promotes queued players. Active run progress will be discarded.",
            "Reset Lobby" );
        return;
    }
    Race_ResetLobby();
}

static void Race_ExecuteConfirmAction( void ) {
    raceConfirmAction_t action = gConfirmAction;
    gConfirmAction = RACE_CONFIRM_NONE;
    switch ( action ) {
        case RACE_CONFIRM_STOP_HOST:
            Race_StopHost();
            break;
        case RACE_CONFIRM_STOP_RACE:
            Race_StopRace();
            break;
        case RACE_CONFIRM_RESET_LOBBY:
            Race_ResetLobby();
            break;
        default:
            break;
    }
}

static void Race_DrawConfirmModal( void ) {
    ImVec2 center;
    ImVec2 displaySize;
    if ( gConfirmOpen ) {
        ImGui::OpenPopup( "race_confirm_action" );
        gConfirmOpen = 0;
    }
    displaySize = ImGui::GetIO().DisplaySize;
    center = ImVec2( displaySize.x * 0.5f, displaySize.y * 0.5f );
    ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
    ImGui::SetNextWindowSize( ImVec2( 460.0f, 0.0f ), ImGuiCond_Appearing );
    if ( ImGui::BeginPopupModal( "race_confirm_action", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings ) ) {
        ImGui::PushFont( gImguiFontBold ? gImguiFontBold : gImguiFontUi );
        ImGui::TextUnformatted( gConfirmTitle[0] ? gConfirmTitle : "Confirm action" );
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextWrapped( "%s", gConfirmText[0] ? gConfirmText : "Are you sure?" );
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if ( ImGui::Button( "Cancel", ImVec2( 150.0f, 44.0f ) ) ) {
            gConfirmAction = RACE_CONFIRM_NONE;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor( ImGuiCol_Button, Race_LogColor( RACE_LOG_WARN ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, Race_LogColor( RACE_LOG_ERROR ) );
        ImGui::PushStyleColor( ImGuiCol_Text, Race_Rgba( 255, 255, 255, 255 ) );
        if ( ImGui::Button( gConfirmButton[0] ? gConfirmButton : "Confirm", ImVec2( 180.0f, 44.0f ) ) ) {
            Race_ExecuteConfirmAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor( 3 );
        ImGui::EndPopup();
    }
}
