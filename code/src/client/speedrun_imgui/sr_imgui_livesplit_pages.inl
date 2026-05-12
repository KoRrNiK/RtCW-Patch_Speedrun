// LiveSplit-specific ImGui settings pages.

static void CL_ImGuiDrawTimerPage( void ) {
	static const char *typeLabels[] = { "In-Game Only", "External LiveSplit" };
	static const int typeValues[] = { 0, 1 };
	static const char *modeLabels[] = { "Full Game", "Chapter", "Individual Level" };
	static const int modeValues[] = { 0, 1, 2 };
	static const char *missionLabels[] = { "1: Ominous Rumors", "2: Vengeance", "3: Deadly Designs", "4: Deathshead", "5: Resurrection" };
	static const int missionValues[] = { 1, 2, 3, 4, 5 };
	static const char *compareLabels[] = { "Personal Best", "Best Segments", "Average Segments" };
	static const int compareValues[] = { 0, 1, 2 };
	static const char *timingLabels[] = { "Game Time", "Real Time" };
	static const int timingValues[] = { 0, 1 };
	int runMode = s_ls_mode ? s_ls_mode->integer : 0;
	bool raceLocked = CL_ImGuiRaceSettingsLocked();

	CL_ImGuiDrawPinnedSettings();
	CL_ImGuiSectionHeader( "Timer Configuration", "Core LiveSplit and in-game timer options for the current run." );
	CL_ImGuiBeginAutoBox( "timer_card" );
	CL_ImGuiBoolCvar( "Enable Timer", s_cg_livesplit );
	CL_ImGuiBoolCvarName( "Show LiveSplit Panel", "ls_draw", "1" );
	if ( raceLocked ) {
		ImGui::TextDisabled( "Race controls category settings until you leave the lobby." );
	}
	ImGui::BeginDisabled( raceLocked );
	CL_ImGuiComboCvar( "LiveSplit Type", s_ls_type, typeLabels, typeValues, IM_ARRAYSIZE( typeValues ) );
	CL_ImGuiComboCvar( "Run Mode", s_ls_mode, modeLabels, modeValues, IM_ARRAYSIZE( modeValues ) );
	if ( runMode == 1 ) {
		CL_ImGuiComboCvar( "Chapter", s_ls_mission, missionLabels, missionValues, IM_ARRAYSIZE( missionValues ) );
	}
	if ( runMode == 2 ) {
		CL_ImGuiStringComboCvarName( "IL Map", "ls_map", "escape1", s_mapLabels, s_mapValues, IM_ARRAYSIZE( s_mapValues ) );
	}
	CL_ImGuiBoolCvar( "100% Category", s_ls_100pct );
	CL_ImGuiComboCvar( "Compare Against", s_ls_compare, compareLabels, compareValues, IM_ARRAYSIZE( compareValues ) );
	CL_ImGuiComboCvar( "Timing Method", s_ls_timing, timingLabels, timingValues, IM_ARRAYSIZE( timingValues ) );
	ImGui::EndDisabled();
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Demo Recording", NULL );
	CL_ImGuiBeginAutoBox( "demo_card" );
	CL_ImGuiBoolCvar( "Auto-Record Demos", s_sp_autorecord );
	ImGui::TextDisabled( "Automatically records a demo when the timer starts." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawColorsPage( void ) {
	static const float ahead[4] = { 0.25f, 0.85f, 0.25f, 1.00f };
	static const float behind[4] = { 0.85f, 0.25f, 0.25f, 1.00f };
	static const float gold[4] = { 1.00f, 0.85f, 0.20f, 1.00f };
	static const float header[4] = { 0.35f, 0.75f, 0.20f, 1.00f };
	static const float timer[4] = { 0.70f, 0.95f, 0.50f, 1.00f };
	static const float text[4] = { 0.88f, 0.91f, 0.86f, 1.00f };
	static const float bg[4] = { 0.04f, 0.04f, 0.06f, 0.82f };
	static const float border[4] = { 0.25f, 0.42f, 0.18f, 0.70f };
	static const float mapname[4] = { 0.55f, 0.62f, 0.50f, 0.85f };
	static const float current[4] = { 0.18f, 0.30f, 0.12f, 0.85f };
	static const float completed[4] = { 0.45f, 0.75f, 0.32f, 1.00f };
	static const float future[4] = { 0.45f, 0.50f, 0.43f, 0.82f };
	static const float dim[4] = { 0.32f, 0.36f, 0.30f, 0.70f };
	static const float paused[4] = { 1.00f, 0.75f, 0.25f, 1.00f };

	CL_ImGuiSectionHeader( "LiveSplit Colors", "Overlay colors share the page scroll instead of a second nested list." );
	CL_ImGuiColorCvarName( "Ahead", "ls_clr_ahead", ahead );
	CL_ImGuiColorCvarName( "Behind", "ls_clr_behind", behind );
	CL_ImGuiColorCvarName( "Gold", "ls_clr_gold", gold );
	CL_ImGuiColorCvarName( "Header", "ls_clr_header", header );
	CL_ImGuiColorCvarName( "Timer", "ls_clr_timer", timer );
	CL_ImGuiColorCvarName( "Text", "ls_clr_text", text );
	CL_ImGuiColorCvarName( "Paused", "ls_clr_paused", paused );
	CL_ImGuiColorCvarName( "Background", "ls_clr_bg", bg );
	CL_ImGuiColorCvarName( "Border", "ls_clr_border", border );
	CL_ImGuiColorCvarName( "Map Name", "ls_clr_mapname", mapname );
	CL_ImGuiColorCvarName( "Current Split", "ls_clr_current", current );
	CL_ImGuiColorCvarName( "Completed", "ls_clr_completed", completed );
	CL_ImGuiColorCvarName( "Future", "ls_clr_future", future );
	CL_ImGuiColorCvarName( "Time Column - all", "ls_clr_split_time", text );
	CL_ImGuiColorCvarName( "Time Column - current", "ls_clr_split_time_current", current );
	CL_ImGuiColorCvarName( "Time Column - completed", "ls_clr_split_time_completed", completed );
	CL_ImGuiColorCvarName( "Dim", "ls_clr_dim", dim );
	CL_ImGuiColorCvarName( "Segment Timer", "ls_clr_segtimer", timer );
	CL_ImGuiColorCvarName( "Separator", "ls_clr_sep", border );
	CL_ImGuiColorCvarName( "Highlight", "ls_clr_highlight", current );
	CL_ImGuiColorCvarName( "Label", "ls_clr_label", mapname );
}

static void CL_ImGuiResetLiveSplitStyleDefaults( void ) {
	Cvar_Set( "ls_x", "6.666843" );
	Cvar_Set( "ls_y", "92.444427" );
	Cvar_Set( "ls_w", "215" );
	Cvar_Set( "ls_scale", "0.550000" );
	Cvar_Set( "ls_align", "0" );
	Cvar_Set( "ls_maxrows", "6" );
	Cvar_Set( "ls_opacity", "0.900000" );
	Cvar_Set( "ls_opacity_ui", "0.900000" );
	Cvar_Set( "ls_bgalpha", "0.78" );
	Cvar_Set( "ls_text_shadow", "1" );
	Cvar_Set( "ls_draw", "1" );
	Cvar_Set( "ls_showtimer", "1" );
	Cvar_Set( "ls_showheader", "1" );
	Cvar_Set( "ls_showstats", "1" );
	Cvar_Set( "ls_showseg", "1" );
	Cvar_Set( "ls_showrgt", "1" );
	Cvar_Set( "ls_showpb", "1" );
	Cvar_Set( "ls_showbest", "1" );
	Cvar_Set( "ls_showseps", "1" );
	Cvar_Set( "ls_showdeltas", "1" );
	Cvar_Set( "ls_showbestdeltas", "1" );
	Cvar_Set( "ls_showatt", "1" );
	Cvar_Set( "ls_100pct", "0" );
	Cvar_Set( "ls_imgui_rounding", "0" );
	Cvar_Set( "ls_imgui_padding", "5" );
	Cvar_Set( "ls_imgui_component_gap", "4" );
	Cvar_Set( "ls_imgui_show_title", "0" );
	Cvar_Set( "ls_imgui_show_border", "1" );
	Cvar_Set( "ls_imgui_header_bg", "0" );
	Cvar_Set( "ls_imgui_show_status", "0" );
	Cvar_Set( "ls_imgui_gradient", "0" );
	Cvar_Set( "ls_imgui_gradient_angle", "230" );
	Cvar_Set( "ls_imgui_current_bg", "1" );
	Cvar_Set( "ls_imgui_border_size", "0.500000" );
	Cvar_Set( "ls_imgui_font", "1" );
	Cvar_Set( "ls_imgui_show_prevseg", "1" );
	Cvar_Set( "ls_imgui_show_ghostseg", "0" );
	Cvar_Set( "ls_imgui_show_bestsegments", "0" );
	Cvar_Set( "ls_imgui_prev_gold_rainbow", "0" );
	Cvar_Set( "ls_imgui_row_size", "0.88" );
	Cvar_Set( "ls_imgui_name_size", "0.92" );
	Cvar_Set( "ls_imgui_bestdelta_size", "0.920000" );
	Cvar_Set( "ls_imgui_delta_size", "0.88" );
	Cvar_Set( "ls_imgui_time_size", "0.94" );
	Cvar_Set( "ls_split_countdown_lead", "10" );
	Cvar_Set( "ls_imgui_gold_rainbow", "1" );
	Cvar_Set( "ls_imgui_col_name", "0.347525" );
	Cvar_Set( "ls_imgui_col_best", "0.574001" );
	Cvar_Set( "ls_imgui_col_delta", "0.763122" );
	Cvar_Set( "ls_imgui_timer_size", "1.630000" );
	Cvar_Set( "ls_imgui_stage_size", "1.450000" );
	Cvar_Set( "ls_imgui_info_size", "0.650000" );
	Cvar_Set( "ls_imgui_rgt_size", "1.250000" );
	Cvar_Set( "ls_imgui_timer_gap", "2" );
	Cvar_Set( "ls_imgui_info_gap", "24" );
	Cvar_Set( "ls_imgui_timer_split", "0.400000" );
	Cvar_Set( "ls_imgui_show_sob", "1" );
	Cvar_Set( "ls_imgui_show_possible_save", "1" );
	Cvar_Set( "ls_imgui_show_best_possible", "1" );
	Cvar_Set( "ls_imgui_bold_title", "1" );
	Cvar_Set( "ls_imgui_bold_attempts", "1" );
	Cvar_Set( "ls_imgui_bold_status", "0" );
	Cvar_Set( "ls_imgui_bold_header", "1" );
	Cvar_Set( "ls_imgui_bold_timer", "1" );
	Cvar_Set( "ls_imgui_bold_stage", "1" );
	Cvar_Set( "ls_imgui_bold_info", "1" );
	Cvar_Set( "ls_imgui_bold_splits", "0" );
	Cvar_Set( "ls_imgui_bold_split_name", "0" );
	Cvar_Set( "ls_imgui_bold_split_best", "1" );
	Cvar_Set( "ls_imgui_bold_split_delta", "1" );
	Cvar_Set( "ls_imgui_bold_split_time", "1" );
	Cvar_Set( "ls_imgui_bold_prev", "0" );
	Cvar_Set( "ls_imgui_bold_prev_label", "0" );
	Cvar_Set( "ls_imgui_bold_prev_value", "1" );
	Cvar_Set( "ls_imgui_bold_ghost", "0" );
	Cvar_Set( "ls_imgui_bold_stats", "0" );
	Cvar_Set( "ls_imgui_bold_stats_values", "0" );
	Cvar_Set( "ls_imgui_bold_stat_sob_label", "0" );
	Cvar_Set( "ls_imgui_bold_stat_sob_value", "1" );
	Cvar_Set( "ls_imgui_bold_stat_possible_label", "0" );
	Cvar_Set( "ls_imgui_bold_stat_possible_value", "1" );
	Cvar_Set( "ls_imgui_bold_stat_best_label", "0" );
	Cvar_Set( "ls_imgui_bold_stat_best_value", "1" );
	Cvar_Set( "ls_imgui_bold_rgt", "1" );
	Cvar_Set( "ls_imgui_text_gradient", "0" );
	Cvar_Set( "ls_imgui_text_gradient_angle", "0" );
	Cvar_Set( "ls_clr_bg", "10 10 15 0.63" );
	Cvar_Set( "ls_clr_bg2", "10 10 15 0.90" );
	Cvar_Set( "ls_clr_border", "29 52 24 0.58" );
	Cvar_Set( "ls_clr_sep", "22 36 18 0.34" );
	Cvar_Set( "ls_clr_highlight", "13 28 12 0.27" );
	Cvar_Set( "ls_clr_text", "214 224 210 0.92" );
	Cvar_Set( "ls_clr_text_gradient2", "255 255 255 0.59" );
	Cvar_Set( "ls_clr_timer", "224 246 214 1.00" );
	Cvar_Set( "ls_clr_title", "90 210 58 1.00" );
	Cvar_Set( "ls_clr_category", "132 158 120 0.88" );
	Cvar_Set( "ls_clr_header_bg", "10 18 12 0.68" );
	Cvar_Set( "ls_clr_column_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_current", "86 210 55 1.00" );
	Cvar_Set( "ls_clr_completed", "120 130 118 0.62" );
	Cvar_Set( "ls_clr_future", "124 124 124 1.00" );
	Cvar_Set( "ls_clr_split_time", "218 226 214 0.92" );
	Cvar_Set( "ls_clr_split_time_current", "224 246 214 1.00" );
	Cvar_Set( "ls_clr_split_time_completed", "150 160 146 0.72" );
	Cvar_Set( "ls_clr_ahead", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_behind", "220 72 72 1.00" );
	Cvar_Set( "ls_clr_gold", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_stage_timer", "178 190 172 0.84" );
	Cvar_Set( "ls_clr_status_live", "84 205 55 1.00" );
	Cvar_Set( "ls_clr_status_ready", "80 92 76 0.90" );
	Cvar_Set( "ls_clr_status_pause", "255 191 64 1.00" );
	Cvar_Set( "ls_clr_status_done", "64 217 64 1.00" );
	Cvar_Set( "ls_clr_status_text", "6 10 6 0.95" );
	Cvar_Set( "ls_clr_pb_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_pb_value", "218 226 214 0.92" );
	Cvar_Set( "ls_clr_best_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_best_value", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_prev_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_prev_ahead", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_prev_behind", "220 72 72 1.00" );
	Cvar_Set( "ls_clr_prev_gold", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_ghost_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_ghost_time", "178 190 172 0.84" );
	Cvar_Set( "ls_clr_stat_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_sob_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_sob", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_stat_possible_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_possible_save", "72 220 80 1.00" );
	Cvar_Set( "ls_clr_stat_possible_zero", "112 118 112 0.62" );
	Cvar_Set( "ls_clr_stat_possible_missing", "82 92 76 0.70" );
	Cvar_Set( "ls_clr_stat_best_possible_label", "128 142 118 0.68" );
	Cvar_Set( "ls_clr_stat_best_possible", "255 220 50 1.00" );
	Cvar_Set( "ls_clr_rgt", "218 226 214 0.92" );
	Cvar_Set( "ls_clr_empty", "112 118 112 0.48" );
}

static void CL_ImGuiDrawLiveSplitElementStylesPage( void ) {
	static const float ahead[4] = { 0.25f, 0.85f, 0.25f, 1.00f };
	static const float behind[4] = { 0.85f, 0.25f, 0.25f, 1.00f };
	static const float gold[4] = { 1.00f, 0.85f, 0.20f, 1.00f };
	static const float header[4] = { 0.35f, 0.75f, 0.20f, 1.00f };
	static const float timer[4] = { 0.70f, 0.95f, 0.50f, 1.00f };
	static const float text[4] = { 0.88f, 0.91f, 0.86f, 1.00f };
	static const float bg[4] = { 0.03f, 0.04f, 0.03f, 0.82f };
	static const float border[4] = { 0.25f, 0.42f, 0.18f, 0.70f };
	static const float mapname[4] = { 0.55f, 0.62f, 0.50f, 0.85f };
	static const float current[4] = { 0.18f, 0.30f, 0.12f, 0.85f };
	static const float completed[4] = { 0.45f, 0.75f, 0.32f, 1.00f };
	static const float future[4] = { 0.45f, 0.50f, 0.43f, 0.82f };
	static const float dim[4] = { 0.32f, 0.36f, 0.30f, 0.70f };
	static const char *fontLabels[] = { "Segoe UI", "Consolas", "Arial", "Tahoma", "Verdana", "Trebuchet", "Calibri", "Courier", "Impact", "Times", "Georgia", "Lucida Console", "Segoe Bold", "Candara", "Corbel", "Calibri Bold", "Segoe Semibold", "Segoe Light", "Segoe Italic", "Arial Italic", "Arial Bold Italic", "Cambria", "Cambria Bold", "Constantia", "Constantia Bold", "Comic Sans", "Comic Sans Bold", "Gadugi", "Gadugi Bold", "Bahnschrift", "Palatino Italic", "Palatino Bold", "Arial Black" };
	static const int fontValues[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
	static const float statusLive[4] = { 0.35f, 0.75f, 0.20f, 1.00f };
	static const float statusReady[4] = { 0.32f, 0.36f, 0.30f, 0.90f };
	static const float statusPause[4] = { 1.00f, 0.75f, 0.25f, 1.00f };
	static const float statusDone[4] = { 0.25f, 0.85f, 0.25f, 1.00f };

	CL_ImGuiSectionHeader( "LiveSplit Elements", "Every component has its own visibility, bold and color options." );
	if ( ImGui::Button( "Reset LiveSplit Style", ImVec2( 170, 0 ) ) ) {
		CL_ImGuiResetLiveSplitStyleDefaults();
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "small dark default preset" );
	ImGui::Spacing();

	CL_ImGuiBeginAutoBox( "ls_box_panel" );
	ImGui::TextUnformatted( "Panel / background" );
	CL_ImGuiBoolCvarName( "Show LiveSplit Panel", "ls_draw", "1" );
	CL_ImGuiBoolCvarName( "Panel Border", "ls_imgui_show_border", "1" );
	CL_ImGuiBoolCvarName( "Header Background", "ls_imgui_header_bg", "0" );
	CL_ImGuiBoolCvarName( "Panel Gradient", "ls_imgui_gradient", "0" );
	CL_ImGuiComboCvarName( "LiveSplit Font", "ls_imgui_font", "1", fontLabels, fontValues, IM_ARRAYSIZE( fontValues ) );
	CL_ImGuiSliderCvarName( "Width", "ls_w", "215", 80.0f, 400.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Global Scale", "ls_scale", "0.550000", 0.5f, 2.5f, "%.2f" );
	CL_ImGuiSliderCvarName( "Padding", "ls_imgui_padding", "5", 3.0f, 14.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Component Gap", "ls_imgui_component_gap", "4", 0.0f, 12.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Border Thickness", "ls_imgui_border_size", "0.500000", 0.5f, 4.0f, "%.1f" );
	CL_ImGuiSliderCvarName( "Gradient Angle DEG", "ls_imgui_gradient_angle", "230", 0.0f, 360.0f, "%.0f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_panel", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Gradient top / solid background", "ls_clr_bg", bg );
		CL_ImGuiColorCvarName( "Gradient bottom", "ls_clr_bg2", dim );
		CL_ImGuiColorCvarName( "Panel Border", "ls_clr_border", border );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_text_gradient" );
	ImGui::TextUnformatted( "Text gradient" );
	CL_ImGuiBoolCvarName( "Gradient on text", "ls_imgui_text_gradient", "0" );
	CL_ImGuiSliderCvarName( "Text Gradient Angle DEG", "ls_imgui_text_gradient_angle", "0", 0.0f, 360.0f, "%.0f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_text_gradient", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Text gradient end", "ls_clr_text_gradient2", header );
		ImGui::TreePop();
	}
	ImGui::TextDisabled( "Uses each component color as start and this color as end." );
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_title" );
	ImGui::TextUnformatted( "Title / category" );
	CL_ImGuiBoolCvarName( "Title", "ls_imgui_show_title", "0" );
	CL_ImGuiBoolCvarName( "Attempt Counter", "ls_showatt", "1" );
	CL_ImGuiBoolCvarName( "Bold title/category", "ls_imgui_bold_title", "1" );
	CL_ImGuiBoolCvarName( "Bold attempts", "ls_imgui_bold_attempts", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_title", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Header background fill", "ls_clr_header_bg", header );
		CL_ImGuiColorCvarName( "Game title text", "ls_clr_title", header );
		CL_ImGuiColorCvarName( "Category / attempt text", "ls_clr_category", mapname );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_status" );
	ImGui::TextUnformatted( "Status chip" );
	CL_ImGuiBoolCvarName( "LIVE / READY / DONE", "ls_imgui_show_status", "0" );
	CL_ImGuiBoolCvarName( "Bold status text", "ls_imgui_bold_status", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_status", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "LIVE chip background", "ls_clr_status_live", statusLive );
		CL_ImGuiColorCvarName( "READY chip background", "ls_clr_status_ready", statusReady );
		CL_ImGuiColorCvarName( "PAUSE chip background", "ls_clr_status_pause", statusPause );
		CL_ImGuiColorCvarName( "DONE chip background", "ls_clr_status_done", statusDone );
		CL_ImGuiColorCvarName( "Chip text", "ls_clr_status_text", text );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_columns" );
	ImGui::TextUnformatted( "Column header row" );
	CL_ImGuiBoolCvar( "Header columns", s_ls_showheaders );
	CL_ImGuiBoolCvarName( "Separators", "ls_showseps", "1" );
	CL_ImGuiBoolCvarName( "Bold column labels", "ls_imgui_bold_header", "1" );
	if ( ImGui::TreeNodeEx( "Colors##ls_columns", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Column labels", "ls_clr_column_label", mapname );
		CL_ImGuiColorCvarName( "Separator lines", "ls_clr_sep", border );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_timer" );
	ImGui::TextUnformatted( "Timer component: IGT + Stage + PB/BEST" );
	CL_ImGuiBoolCvar( "IGT Main Timer", s_ls_showtimer );
	CL_ImGuiBoolCvar( "Stage Timer", s_ls_showsegtimer );
	CL_ImGuiBoolCvarName( "Stage PB", "ls_showpb", "1" );
	CL_ImGuiBoolCvarName( "Stage BEST", "ls_showbest", "1" );
	ImGui::Separator();
	CL_ImGuiBoolCvarName( "Bold IGT", "ls_imgui_bold_timer", "1" );
	CL_ImGuiBoolCvarName( "Bold stage", "ls_imgui_bold_stage", "1" );
	CL_ImGuiBoolCvarName( "Bold PB/BEST", "ls_imgui_bold_info", "1" );
	CL_ImGuiSliderCvarName( "IGT Size", "ls_imgui_timer_size", "1.630000", 0.75f, 2.00f, "%.2f" );
	CL_ImGuiSliderCvarName( "Stage Size", "ls_imgui_stage_size", "1.450000", 0.55f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "PB/BEST Size", "ls_imgui_info_size", "0.650000", 0.55f, 1.25f, "%.2f" );
	CL_ImGuiSliderCvarName( "PB/BEST horizontal gap", "ls_imgui_info_gap", "24", 24.0f, 120.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Timer / stats gap", "ls_imgui_timer_gap", "2", 2.0f, 24.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "PB/BEST side position", "ls_imgui_timer_split", "0.400000", 0.40f, 0.76f, "%.2f" );
	ImGui::Separator();
	if ( ImGui::TreeNodeEx( "Colors##ls_timer", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "IGT normal", "ls_clr_timer", timer );
		CL_ImGuiColorCvarName( "IGT ahead / finished", "ls_clr_ahead", ahead );
		CL_ImGuiColorCvarName( "IGT behind", "ls_clr_behind", behind );
		CL_ImGuiColorCvarName( "Stage Color", "ls_clr_stage_timer", timer );
		CL_ImGuiColorCvarName( "PB label", "ls_clr_pb_label", mapname );
		CL_ImGuiColorCvarName( "PB value", "ls_clr_pb_value", text );
		CL_ImGuiColorCvarName( "BEST label", "ls_clr_best_label", mapname );
		CL_ImGuiColorCvarName( "BEST value", "ls_clr_best_value", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_splits" );
	ImGui::TextUnformatted( "Split rows" );
	CL_ImGuiBoolCvarName( "PB Delta (+/-)", "ls_showdeltas", "1" );
	CL_ImGuiBoolCvarName( "Best Delta (+/-)", "ls_showbestdeltas", "1" );
	CL_ImGuiBoolCvarName( "Current Row Background", "ls_imgui_current_bg", "1" );
	CL_ImGuiBoolCvarName( "Rainbow gold BEST +/-", "ls_imgui_gold_rainbow", "1" );
	CL_ImGuiBoolCvarName( "Bold all split rows", "ls_imgui_bold_splits", "0" );
	CL_ImGuiBoolCvarName( "Bold stage names", "ls_imgui_bold_split_name", "0" );
	CL_ImGuiBoolCvarName( "Bold BEST +/-", "ls_imgui_bold_split_best", "1" );
	CL_ImGuiBoolCvarName( "Bold +/-", "ls_imgui_bold_split_delta", "1" );
	CL_ImGuiBoolCvarName( "Bold split time", "ls_imgui_bold_split_time", "1" );
	CL_ImGuiSliderCvarName( "Row Size", "ls_imgui_row_size", "0.88", 0.75f, 1.45f, "%.2f" );
	CL_ImGuiSliderCvarName( "Map Name Font", "ls_imgui_name_size", "0.92", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "BEST +/- Font", "ls_imgui_bestdelta_size", "0.920000", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "+/- Font", "ls_imgui_delta_size", "0.88", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "Time Font", "ls_imgui_time_size", "0.94", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiSliderCvarName( "Stage Name Width", "ls_imgui_col_name", "0.347525", 0.22f, 0.78f, "%.2f" );
	CL_ImGuiSliderCvarName( "BEST +/- Width", "ls_imgui_col_best", "0.574001", 0.34f, 0.88f, "%.2f" );
	CL_ImGuiSliderCvarName( "+/- Width", "ls_imgui_col_delta", "0.763122", 0.44f, 0.94f, "%.2f" );
	if ( ImGui::TreeNodeEx( "Colors##ls_splits", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Future split name", "ls_clr_future", future );
		CL_ImGuiColorCvarName( "Completed split name", "ls_clr_completed", completed );
		CL_ImGuiColorCvarName( "Current split name", "ls_clr_current", current );
		CL_ImGuiColorCvarName( "Current row background", "ls_clr_highlight", current );
		CL_ImGuiColorCvarName( "Time column - all", "ls_clr_split_time", text );
		CL_ImGuiColorCvarName( "Time column - current", "ls_clr_split_time_current", current );
		CL_ImGuiColorCvarName( "Time column - completed", "ls_clr_split_time_completed", completed );
		CL_ImGuiColorCvarName( "Gold / best delta", "ls_clr_gold", gold );
		CL_ImGuiColorCvarName( "Ahead delta", "ls_clr_ahead", ahead );
		CL_ImGuiColorCvarName( "Behind delta", "ls_clr_behind", behind );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_ghost" );
	ImGui::TextUnformatted( "Ghost segment" );
	CL_ImGuiBoolCvarName( "Ghost Segment", "ls_imgui_show_ghostseg", "0" );
	CL_ImGuiBoolCvarName( "Bold ghost", "ls_imgui_bold_ghost", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_ghost", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Ghost label", "ls_clr_ghost_label", mapname );
		CL_ImGuiColorCvarName( "Ghost time", "ls_clr_ghost_time", timer );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_stats" );
	ImGui::TextUnformatted( "Statistics: master" );
	CL_ImGuiBoolCvar( "Statistics", s_ls_showstats );
	CL_ImGuiBoolCvarName( "Previous Segment", "ls_imgui_show_prevseg", "1" );
	CL_ImGuiBoolCvarName( "Bold all statistics", "ls_imgui_bold_stats", "0" );
	CL_ImGuiBoolCvarName( "Bold all statistic values", "ls_imgui_bold_stats_values", "0" );
	if ( ImGui::TreeNodeEx( "Colors##ls_stats", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Statistics labels", "ls_clr_stat_label", mapname );
		ImGui::TreePop();
	}
	ImGui::EndChild();
	const bool statAllBold = Cvar_VariableIntegerValue( "ls_imgui_bold_stats" ) != 0;
	const bool statValueAllBold = statAllBold || Cvar_VariableIntegerValue( "ls_imgui_bold_stats_values" ) != 0;

	CL_ImGuiBeginAutoBox( "ls_box_stat_sob" );
	ImGui::TextUnformatted( "Statistic: Sum of best" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_sob", "1" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_sob_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_sob_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_sob", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Sum of best label", "ls_clr_stat_sob_label", mapname );
		CL_ImGuiColorCvarName( "Sum of best value", "ls_clr_stat_sob", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_stat_possible" );
	ImGui::TextUnformatted( "Statistic: Possible save" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_possible_save", "1" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_possible_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_possible_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_possible", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Possible save label", "ls_clr_stat_possible_label", mapname );
		CL_ImGuiColorCvarName( "Possible save value", "ls_clr_stat_possible_save", ahead );
		CL_ImGuiColorCvarName( "Possible save zero", "ls_clr_stat_possible_zero", dim );
		CL_ImGuiColorCvarName( "Possible save missing", "ls_clr_stat_possible_missing", dim );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_stat_best" );
	ImGui::TextUnformatted( "Statistic: Best possible" );
	CL_ImGuiBoolCvarName( "Show row", "ls_imgui_show_best_possible", "1" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold label", "ls_imgui_bold_stat_best_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold value", "ls_imgui_bold_stat_best_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_stat_best", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Best possible label", "ls_clr_stat_best_possible_label", mapname );
		CL_ImGuiColorCvarName( "Best possible value", "ls_clr_stat_best_possible", text );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_prev" );
	ImGui::TextUnformatted( "Statistic: Previous segment" );
	ImGui::TextDisabled( "Shown with Statistics; this box keeps separate styling." );
	CL_ImGuiBoolCvarName( "Rainbow when gold", "ls_imgui_prev_gold_rainbow", "0" );
	ImGui::BeginDisabled( statAllBold );
	CL_ImGuiBoolCvarName( "Bold previous label", "ls_imgui_bold_prev_label", "0" );
	ImGui::EndDisabled();
	ImGui::BeginDisabled( statValueAllBold );
	CL_ImGuiBoolCvarName( "Bold previous value", "ls_imgui_bold_prev_value", "1" );
	ImGui::EndDisabled();
	if ( ImGui::TreeNodeEx( "Colors##ls_prev", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "Previous label", "ls_clr_prev_label", mapname );
		CL_ImGuiColorCvarName( "Previous ahead", "ls_clr_prev_ahead", ahead );
		CL_ImGuiColorCvarName( "Previous behind", "ls_clr_prev_behind", behind );
		CL_ImGuiColorCvarName( "Previous gold", "ls_clr_prev_gold", gold );
		ImGui::TreePop();
	}
	ImGui::EndChild();

	CL_ImGuiBeginAutoBox( "ls_box_extra" );
	ImGui::TextUnformatted( "RGT / best segments" );
	CL_ImGuiBoolCvarName( "RGT", "ls_showrgt", "1" );
	CL_ImGuiBoolCvarName( "Best Segments", "ls_imgui_show_bestsegments", "0" );
	CL_ImGuiBoolCvarName( "Bold RGT", "ls_imgui_bold_rgt", "1" );
	CL_ImGuiSliderCvarName( "RGT Size", "ls_imgui_rgt_size", "1.250000", 0.65f, 1.60f, "%.2f" );
	CL_ImGuiIntSliderCvarName( "Max Visible Splits", "ls_maxrows", "6", 0, 20 );
	if ( ImGui::TreeNodeEx( "Colors##ls_extra", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		CL_ImGuiColorCvarName( "RGT", "ls_clr_rgt", text );
		CL_ImGuiColorCvarName( "Inactive / empty", "ls_clr_empty", dim );
		ImGui::TreePop();
	}
	ImGui::EndChild();
}

static void CL_ImGuiDrawDisplayPage( void ) {
	static const char *alignLabels[] = { "Left", "Right" };
	static const int alignValues[] = { 0, 1 };

	CL_ImGuiSectionHeader( "LiveSplit Panel Layout", "These controls affect both the new ImGui overlay and the fallback renderer panel." );
	CL_ImGuiBeginAutoBox( "display_layout_card" );
	CL_ImGuiComboCvarName( "Content Side", "ls_align", "0", alignLabels, alignValues, IM_ARRAYSIZE( alignValues ) );
	CL_ImGuiSliderCvarName( "X Position", "ls_x", "6.666843", 0.0f, 580.0f, "%.0f" );
	CL_ImGuiSliderCvarName( "Y Position", "ls_y", "92.444427", 0.0f, 440.0f, "%.0f" );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Transparency", NULL );
	CL_ImGuiBeginAutoBox( "display_opacity_card" );
	CL_ImGuiSliderCvarName( "Splits Opacity", "ls_opacity", "0.900000", 0.0f, 1.0f, "%.2f" );
	CL_ImGuiSliderCvarName( "Splits In Menu", "ls_opacity_ui", "0.900000", 0.0f, 1.0f, "%.2f" );
	ImGui::TextDisabled( "Background alpha is now controlled by Style > Background color alpha." );
	ImGui::EndChild();

	CL_ImGuiSectionHeader( "Text", NULL );
	CL_ImGuiBeginAutoBox( "display_text_card" );
	CL_ImGuiBoolCvarName( "Text Shadow", "ls_text_shadow", "1" );
	ImGui::TextDisabled( "Colors and LiveSplit layout are grouped in this Style page." );
	ImGui::EndChild();
}

static void CL_ImGuiDrawStylePage( void ) {
	CL_ImGuiDrawLiveSplitElementStylesPage();
	CL_ImGuiDrawDisplayPage();
}