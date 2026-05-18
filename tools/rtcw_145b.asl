// ----------------------------------------
// GAME: Return to Castle Wolfenstein
// Version: RtCW Patch 1.42d / 1.45a / 1.45b
// Patch by KoRrNiK (1.45a and 1.45b)
//
// This splitter reads a stable in-process ASL block instead of fixed build
// addresses. It signature-scans WolfSP.exe for:
//   RTCW145B_ASL_V1
// ----------------------------------------

state("WolfSP", "1.45b") {
	string16 bsp       : "WolfSP.exe", 0x0;
	byte cs            : "WolfSP.exe", 0x0;

	int client_status  : "WolfSP.exe", 0x0;
	byte ESC           : "WolfSP.exe", 0x0;

	float camera_x     : "WolfSP.exe", 0x0;
	float xpos         : "WolfSP.exe", 0x0;
	float ypos         : "WolfSP.exe", 0x0;
	float zpos         : "WolfSP.exe", 0x0;

	int finish         : "WolfSP.exe", 0x0;
	byte stuck         : "WolfSP.exe", 0x0;
}

state("WolfSP", "1.45a") {
	string16 bsp       : "WolfSP.exe", 0x693664;
	byte cs            : "WolfSP.exe", 0xEA7B64;

	int client_status  : "WolfSP.exe", 0x613420;
	byte ESC           : "WolfSP.exe", 0xCCAF24;

	float camera_x     : "WolfSP.exe", 0x7A2F9C;
	float xpos         : "WolfSP.exe", 0x77B0DC;
	float ypos         : "WolfSP.exe", 0x7A2FA4;
	float zpos         : "WolfSP.exe", 0x77B0E0;

	int finish         : "WolfSP.exe", 0xDBC164;
	byte stuck         : "WolfSP.exe", 0xDCB9E1;
}

// Patch by Knightmare | The bytes were found by Hoyo & KoRrNiK
state("WolfSP", "1.42d") {
	string16 bsp       : 0x13D4, 0x8;
	byte cs            : 0x26F4, 0x0;

	int client_status  : 0xB24EE0;
	byte ESC           : "WolfSP.exe", 0x6899D8;

	float camera_x     : "WolfSP.exe", 0xDA9D3C;
	float xpos         : "WolfSP.exe", 0x5F8DA4;
	float ypos         : "WolfSP.exe", 0x5F8DA8;
	float zpos         : "WolfSP.exe", 0x5F8DAC;

	int finish         : "qagamex86.dll", 0x57D7D0, 0x42c;
	byte stuck         : "WolfSP.exe", 0x0;
}

startup {
	int m_chap = 0, i_chap = 0;

	vars.mapListChapter1 = new List<string> { "escape1", "escape2", "tram", "village1", "crypt1", "crypt2", "church", "boss1" };
	vars.mapListChapter2 = new List<string> { "forest", "rocket", "baseout", "assault" };
	vars.mapListChapter3 = new List<string> { "sfm", "factory", "trainyard", "swf" };
	vars.mapListChapter4 = new List<string> { "norway", "xlabs", "boss2" };
	vars.mapListChapter5 = new List<string> { "dam", "village2", "chateau", "dark", "dig", "castle", "end" };

	vars.chapterNames = new List<string> { "Ominous Rumors + Dark Secret", "Weapons of Vengeance", "Deadly Designs", "Deathshead's Playground", "Return Engagement + Operation Resurrection" };
	vars.individualNames1 = new List<string> { "Escape!", "Castle Keep", "Tram Ride", "Village", "Catacombs", "Crypt", "The Defiled Church", "Tomb" };
	vars.individualNames2 = new List<string> { "Forest Compound", "Rocket Base", "Radar Installation", "Air Base Assault" };
	vars.individualNames3 = new List<string> { "Kugelstadt", "The Bombed Factory", "The Trainyards", "Secret Weapons Facility" };
	vars.individualNames4 = new List<string> { "Ice Station Norway", "X-Labs", "Super Soldier" };
	vars.individualNames5 = new List<string> { "Bramburg Dam", "Paderborn Village", "Chateau Schufstaffel", "Unhallowed Ground", "The Dig", "Return to Castle Wolfenstein", "Heinrich" };

	settings.Add("cat_all", true, "Full game");

	settings.Add("chaptersOnly", false, "Chapters");
	foreach (var names in vars.chapterNames) {
		m_chap++;
		settings.Add("cat_chap" + m_chap, false, names, "chaptersOnly");
	}

	for (int i = 1; i <= 5; i++) {
		settings.Add("individualLevelsC" + i, false, "Chapter " + i + " Individual Levels");
		foreach (var names in (i == 1 ? vars.individualNames1 : i == 2 ? vars.individualNames2 : i == 3 ? vars.individualNames3 : i == 4 ? vars.individualNames4 : vars.individualNames5)) {
			i_chap++;
			settings.Add("miss" + i_chap + "_chap_" + i, false, names, "individualLevelsC" + i);
		}
		i_chap = 0;
	}

	Action<string> DebugOutput = (text) => {
		print("[RTCW Autosplitter] " + text);
	};
	vars.DebugOutput = DebugOutput;

	Action StartLRTGameTime = () => {
		int raw = vars.lrtMs;
		if (raw < 0) raw = 0;
		vars.lrtRunStarted = true;
		vars.lrtGameTimeMs = raw;
		vars.lrtLastRawMs = raw;
		vars.finishSplitDone = false;
		vars.startArmed = false;
		timer.IsGameTimePaused = false;
	};
	vars.StartLRTGameTime = StartLRTGameTime;

	vars.OFS_VERSION = 0x10;
	vars.OFS_MAP = 0x20;
	vars.OFS_CLIENT_STATUS = 0x80;
	vars.OFS_KEYCATCHERS = 0x84;
	vars.OFS_CUTSCENE = 0x88;
	vars.OFS_IS_LOADING = 0x8C;
	vars.OFS_FINISH = 0x90;
	vars.OFS_STUCK = 0x94;
	vars.OFS_PM_TYPE = 0x98;
	vars.OFS_LRT_MS = 0xAC;
	vars.OFS_LRT_SEGMENT_MS = 0xB0;
	vars.OFS_XPOS = 0xB4;
	vars.OFS_YPOS = 0xB8;
	vars.OFS_ZPOS = 0xBC;
	vars.OFS_CAMERA_X = 0xC0;

	refreshRate = 84;
}

init {
	vars.debugMessage = false;
	vars.running = false;
	vars.useStableASL = false;
	vars.aslBase = 0L;

	var mainModule = modules.FirstOrDefault(m => m.ModuleName.ToLower() == "wolfsp.exe");
	if (mainModule == null) {
		if (vars.debugMessage) vars.DebugOutput("WolfSP.exe module not found.");
		version = "Unknown";
		return false;
	}

	byte[] moduleBytes = game.ReadBytes(mainModule.BaseAddress, mainModule.ModuleMemorySize);
	byte[] signature = new byte[] { 0x52, 0x54, 0x43, 0x57, 0x31, 0x34, 0x35, 0x42, 0x5F, 0x41, 0x53, 0x4C, 0x5F, 0x56, 0x31, 0x00 };
	long aslBase = 0;

	for (int i = 0; i <= moduleBytes.Length - signature.Length; i++) {
		bool found = true;
		for (int j = 0; j < signature.Length; j++) {
			if (moduleBytes[i + j] != signature[j]) {
				found = false;
				break;
			}
		}
		if (found) {
			aslBase = mainModule.BaseAddress.ToInt64() + i;
			break;
		}
	}

	if (aslBase != 0) {
		version = game.ReadString((IntPtr)(aslBase + (int)vars.OFS_VERSION), 8);
		if (version == "1.45b") {
			vars.aslBase = aslBase;
			vars.useStableASL = true;
		} else {
			if (vars.debugMessage) vars.DebugOutput("Unsupported ASL block version: " + version);
			return false;
		}
	} else {
		int idGame = mainModule.ModuleMemorySize;
		switch (idGame) {
			case 14643200:
				version = "1.42d";
				break;
			case 19324928:
				version = "1.45a";
				break;
			default:
				if (vars.debugMessage) vars.DebugOutput("Unrecognized game version. Module size: " + idGame);
				version = "Unknown";
				return false;
		}
	}

	vars.bsp = "";
	vars.oldBsp = "";
	vars.cs = 0;
	vars.oldCs = 0;
	vars.xpos = vars.ypos = vars.zpos = 0.0f;
	vars.oldXpos = vars.oldYpos = vars.oldZpos = 0.0f;
	vars.finish = vars.stuck = vars.client_status = vars.keyCatchers = vars.isLoading = 0;
	vars.camera_x = 0.0f;
	vars.lrtMs = 0;
	vars.lrtSegmentMs = 0;
	vars.lrtRunStarted = false;
	vars.lrtGameTimeMs = 0;
	vars.lrtLastRawMs = 0;
	vars.finishSplitDone = false;
	vars.startArmed = false;
	vars.loadStarted = false;

	vars.firstcs = true;
	vars.bsp_list = new List<string>();
	vars.visited = new List<string>();
	vars.running = true;

	if (vars.debugMessage) vars.DebugOutput("Game found | Found Patch " + version);
}

exit {
	timer.IsGameTimePaused = true;
	vars.running = false;
	vars.lrtRunStarted = false;
}

shutdown {
	timer.IsGameTimePaused = true;
	vars.running = false;
	vars.lrtRunStarted = false;
}

update {
	if (!vars.running) return false;

	vars.oldBsp = vars.bsp;
	vars.oldCs = vars.cs;
	vars.oldXpos = vars.xpos;
	vars.oldYpos = vars.ypos;
	vars.oldZpos = vars.zpos;

	if (vars.useStableASL) {
		long aslBase = vars.aslBase;

		vars.bsp = game.ReadString((IntPtr)(aslBase + (int)vars.OFS_MAP), 32);
		vars.client_status = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_CLIENT_STATUS));
		vars.keyCatchers = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_KEYCATCHERS));
		vars.cs = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_CUTSCENE));
		vars.isLoading = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_IS_LOADING));
		vars.finish = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_FINISH));
		vars.stuck = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_STUCK));
		vars.lrtMs = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_LRT_MS));
		vars.lrtSegmentMs = game.ReadValue<int>((IntPtr)(aslBase + (int)vars.OFS_LRT_SEGMENT_MS));
		vars.xpos = game.ReadValue<float>((IntPtr)(aslBase + (int)vars.OFS_XPOS));
		vars.ypos = game.ReadValue<float>((IntPtr)(aslBase + (int)vars.OFS_YPOS));
		vars.zpos = game.ReadValue<float>((IntPtr)(aslBase + (int)vars.OFS_ZPOS));
		vars.camera_x = game.ReadValue<float>((IntPtr)(aslBase + (int)vars.OFS_CAMERA_X));
	} else {
		vars.bsp = current.bsp;
		vars.client_status = current.client_status;
		vars.cs = current.cs;
		vars.finish = current.finish;
		vars.stuck = version == "1.45a" ? current.stuck : 0;
		vars.xpos = current.xpos;
		vars.ypos = current.ypos;
		vars.zpos = current.zpos;
		vars.camera_x = current.camera_x;
		vars.lrtMs = 0;
		vars.lrtSegmentMs = 0;

		if (version == "1.45a") {
			if (current.client_status == 0 || current.ESC == 2) {
				vars.loadStarted = true;
			} else if (current.camera_x != 0) {
				vars.loadStarted = false;
			}
		} else if (version == "1.42d") {
			if ((current.client_status != 8 && current.client_status != 1) || current.ESC == 1) {
				vars.loadStarted = true;
			} else if (current.camera_x != 0) {
				vars.loadStarted = false;
			}
		}
		vars.isLoading = vars.loadStarted ? 1 : 0;
	}

	if (timer.CurrentPhase == TimerPhase.NotRunning && vars.isLoading != 0) {
		vars.startArmed = true;
	}

	if (vars.debugMessage) {
		vars.DebugOutput("POS: X " + vars.xpos + " Y " + vars.ypos + " Z " + vars.zpos +
			" CS " + vars.cs + " F " + vars.finish + " CLS " + vars.client_status + " S " + vars.stuck +
			" L " + vars.isLoading + " LRT " + vars.lrtMs + " MAP " + vars.bsp);
	}
}

start {
	int listChapters = 0;
	bool firstLevelChapter = false;

	vars.bsp_list.Clear();
	for (int i = 1; i <= 5; i++) {
		foreach (var maps in (i == 1 ? vars.mapListChapter1 : i == 2 ? vars.mapListChapter2 : i == 3 ? vars.mapListChapter3 : i == 4 ? vars.mapListChapter4 : vars.mapListChapter5)) {
			if (settings["miss" + (listChapters + 1) + "_chap_" + i]) vars.bsp_list.Add("/" + maps + ".bsp");
			if (settings["cat_all"] || settings["cat_chap" + i]) vars.bsp_list.Add("/" + maps + ".bsp");
			listChapters++;
		}
		listChapters = 0;
	}

	if (settings["cat_all"] && vars.bsp == "/cutscene1.bsp" && vars.cs == 1 && vars.oldCs == 0) {
		if (vars.debugMessage) vars.DebugOutput("Timer started");
		vars.StartLRTGameTime();
		vars.firstcs = true;
		vars.visited.Clear();
		vars.visited.Add("/cutscene1.bsp");
		vars.visited.Add("/escape1.bsp");
		return true;
	}

	for (int i = 1; i <= 5; i++) {
		foreach (var maps in (i == 1 ? vars.mapListChapter1 : i == 2 ? vars.mapListChapter2 : i == 3 ? vars.mapListChapter3 : i == 4 ? vars.mapListChapter4 : vars.mapListChapter5)) {
			firstLevelChapter = listChapters == 0;
			listChapters++;
			if ((settings["cat_chap" + i] && firstLevelChapter) || settings["miss" + listChapters + "_chap_" + i]) {
				if (vars.bsp == "/" + maps + ".bsp" &&
					(vars.oldBsp != "/" + maps + ".bsp" || vars.startArmed)) {
					if (vars.debugMessage) vars.DebugOutput("Timer started");
					vars.StartLRTGameTime();
					vars.firstcs = true;
					vars.visited.Clear();
					vars.visited.Add("/" + maps + ".bsp");
					return true;
				}
			}
		}
		listChapters = 0;
	}
}

split {
	if (!vars.running) return false;

	bool isOld = version == "1.42d" && vars.finish != 0;
	bool isNew = (version == "1.45a" || version == "1.45b") && vars.finish == 4 && vars.cs == 0 && vars.stuck != 3;
	bool cordVillage1 = vars.zpos > 4500.0 && vars.zpos < 4580.0 && vars.xpos > -460.0 && vars.xpos < -300.0;
	bool cordTram = vars.xpos < -3850.0 && vars.ypos > -1300.0;
	bool cordBoss2 = vars.xpos >= 1454.0 && vars.oldXpos < 1454.0 && vars.xpos <= 1500.0 && vars.oldXpos > 1300.0;
	bool cordDark = vars.xpos > 3100.0 && vars.xpos < 3360.0 && vars.zpos < 3230.0 && vars.zpos > 2970.0;
	bool cordEscape1 = vars.ypos > 150.0 && vars.ypos < 350.0 && vars.zpos >= 882.0 && vars.zpos <= 1037.0;
	bool finishSignal = version == "1.45b" && isNew;

	int listChapters = 0;
	bool stoppedTimer = false;
	bool stoppedCutscene = false;

	if (finishSignal && !vars.finishSplitDone) {
		bool selectedFinish = false;
		int finishListChapters = 0;

		for (int i = 1; i <= 5; i++) {
			foreach (var maps in (i == 1 ? vars.mapListChapter1 : i == 2 ? vars.mapListChapter2 : i == 3 ? vars.mapListChapter3 : i == 4 ? vars.mapListChapter4 : vars.mapListChapter5)) {
				finishListChapters++;
				if (vars.bsp == "/" + maps + ".bsp") {
					if (settings["miss" + finishListChapters + "_chap_" + i]) {
						selectedFinish = true;
					}
					if (settings["cat_chap" + i] &&
						((i == 1 && maps == "boss1") ||
						 (i == 2 && maps == "assault") ||
						 (i == 3 && maps == "swf") ||
						 (i == 4 && maps == "boss2") ||
						 (i == 5 && maps == "end"))) {
						selectedFinish = true;
					}
				}
			}
			finishListChapters = 0;
		}

		if (selectedFinish) {
			if (vars.debugMessage) vars.DebugOutput("The timer has stopped on selected finish (" + vars.bsp + ")");
			vars.finishSplitDone = true;
			return true;
		}
	}

	if (vars.bsp != vars.oldBsp) {
		vars.finishSplitDone = false;
		if (vars.debugMessage) vars.DebugOutput("Map changed to " + vars.bsp);
		if (vars.bsp_list.Contains(vars.bsp) && !vars.visited.Contains(vars.bsp)) {
			if (vars.debugMessage) vars.DebugOutput("Map change valid.");
			vars.visited.Add(vars.bsp);
			vars.firstcs = true;
			return true;
		} else if (vars.debugMessage) {
			vars.DebugOutput("Map change ignored.");
		}
	}

	for (int i = 1; i <= 5; i++) {
		if (i == 1 && vars.bsp == "/boss1.bsp" && (settings["cat_chap" + i] || settings["miss8_chap_" + i])) stoppedCutscene = true;
		if (settings["cat_chap" + i]) {
			if ((i == 2 && vars.bsp == "/assault.bsp") || (i == 3 && vars.bsp == "/swf.bsp")) stoppedCutscene = true;
		}
		if (i == 5 && vars.bsp == "/end.bsp") stoppedCutscene = true;
		if (stoppedCutscene && vars.cs == 1 && vars.oldCs == 0) {
			if (vars.firstcs == false) {
				if (vars.debugMessage) vars.DebugOutput("Second cutscene.");
				return true;
			}
			if (vars.firstcs == true) {
				vars.firstcs = false;
				if (vars.debugMessage) vars.DebugOutput("First cutscene.");
			}
		}
		stoppedCutscene = false;
	}

	if ((settings["cat_chap4"] || settings["miss3_chap_4"]) && vars.bsp == "/boss2.bsp" && cordBoss2) {
		if (vars.debugMessage) vars.DebugOutput("The timer has stopped (BOSS2)");
		return true;
	}

	for (int i = 1; i <= 5; i++) {
		foreach (var maps in (i == 1 ? vars.mapListChapter1 : i == 2 ? vars.mapListChapter2 : i == 3 ? vars.mapListChapter3 : i == 4 ? vars.mapListChapter4 : vars.mapListChapter5)) {
			listChapters++;

			if ((version == "1.45a" || version == "1.45b") && vars.finish == 4 && vars.stuck != 0) continue;

			if (i == 1 && maps == "boss1") continue;
			if (i == 4 && maps == "boss2") continue;
			if (i == 5 && maps == "end") continue;

			if (settings["miss" + listChapters + "_chap_" + i] && vars.bsp == "/" + maps + ".bsp") {
				if (i == 1 && ((isNew && maps == "escape1" && cordEscape1) || (isNew && maps == "tram" && cordTram) || (isNew && maps == "village1" && cordVillage1) || isOld || (isNew && maps != "escape1"))) stoppedTimer = true;
				if (i == 2 && (((maps == "forest" || maps == "assault") && vars.cs == 1 && vars.oldCs == 0 && vars.firstcs == true) || isOld || isNew)) stoppedTimer = true;
				if ((i == 3 || i == 4) && (isOld || isNew)) stoppedTimer = true;
				if (i == 5 && ((isNew && maps == "dark" && cordDark) || isOld || isNew)) stoppedTimer = true;

				if (stoppedTimer) {
					if (vars.debugMessage) vars.DebugOutput("The timer has stopped (" + maps + ")");
					return true;
				}
			}
		}
		listChapters = 0;
	}
}

gameTime {
	if (vars.running && version == "1.45b" && vars.lrtRunStarted) {
		int raw = vars.lrtMs;
		if (raw < 0) raw = 0;
		if (raw < vars.lrtGameTimeMs) {
			raw = vars.lrtGameTimeMs;
		}

		vars.lrtLastRawMs = vars.lrtMs;
		vars.lrtGameTimeMs = raw;
		timer.IsGameTimePaused = vars.isLoading != 0;
		return TimeSpan.FromMilliseconds(raw);
	}
	return null;
}

isLoading {
	if (!vars.running) return true;
	if (version == "1.45b") return vars.isLoading != 0;
	return vars.isLoading != 0;
}
