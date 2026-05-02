/*
===========================================================================
cl_update.c  - Background version-update checker for RtCW Patch

Queries the GitHub Releases API in a worker thread and draws a
minimal, modern notification if a newer version is found.
===========================================================================
*/

#include "client.h"

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#include <math.h>
#include <time.h>

#define UPDATE_URL_HOST  "api.github.com"
#define UPDATE_URL_PATH  "/repos/KoRrNiK/RtCW-Patch_Speedrun/releases/latest"

static struct {
	volatile int   checked;     /* 0=not started, 1=checking, 2=done */
	volatile int   available;   /* 1 = newer version found           */
	char           latestVer[32];
	char           releaseUrl[256];
} upd;

/*  JSON helper  */
static int Upd_JsonString( const char *json, const char *key,
						   char *outBuf, int maxLen )
{
	char search[64];
	const char *p, *start, *end;
	int len;

	Com_sprintf( search, sizeof( search ), "\"%s\"", key );
	p = strstr( json, search );
	if ( !p ) return 0;
	p += strlen( search );
	while ( *p && ( *p == ' ' || *p == ':' || *p == '\t' ) ) p++;
	if ( *p != '"' ) return 0;
	start = ++p;
	end = strchr( start, '"' );
	if ( !end ) return 0;
	len = (int)( end - start );
	if ( len >= maxLen ) len = maxLen - 1;
	memcpy( outBuf, start, len );
	outBuf[len] = '\0';
	return 1;
}

/*  Version comparison  */
static int Upd_VersionNewer( const char *localVer, const char *remoteVer )
{
	if ( remoteVer[0] == 'v' || remoteVer[0] == 'V' ) remoteVer++;
	if ( localVer[0]  == 'v' || localVer[0]  == 'V' ) localVer++;
	return ( Q_stricmp( remoteVer, localVer ) > 0 ) ? 1 : 0;
}

/*  Worker thread  */
static DWORD WINAPI Upd_CheckThread( LPVOID param )
{
	HINTERNET hInet = NULL, hConn = NULL, hReq = NULL;
	char buf[4096];
	DWORD bytesRead = 0, totalRead = 0;

	(void)param;

	hInet = InternetOpenA( "RtCW-Patch-UpdateCheck/1.0",
		INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0 );
	if ( !hInet ) goto done;

	hConn = InternetConnectA( hInet, UPDATE_URL_HOST,
		INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL,
		INTERNET_SERVICE_HTTP, 0, 0 );
	if ( !hConn ) goto done;

	hReq = HttpOpenRequestA( hConn, "GET", UPDATE_URL_PATH,
		NULL, NULL, NULL,
		INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE |
		INTERNET_FLAG_NO_UI  | INTERNET_FLAG_RELOAD, 0 );
	if ( !hReq ) goto done;

	HttpAddRequestHeadersA( hReq,
		"Accept: application/vnd.github+json\r\nUser-Agent: RtCW-Patch\r\n",
		-1, HTTP_ADDREQ_FLAG_ADD );

	if ( !HttpSendRequestA( hReq, NULL, 0, NULL, 0 ) ) goto done;

	totalRead = 0;
	while ( InternetReadFile( hReq, buf + totalRead,
			sizeof( buf ) - 1 - totalRead, &bytesRead ) ) {
		if ( bytesRead == 0 ) break;
		totalRead += bytesRead;
		if ( totalRead >= sizeof( buf ) - 1 ) break;
	}
	buf[totalRead] = '\0';

	if ( totalRead > 0 ) {
		char tagName[32] = {0};
		if ( Upd_JsonString( buf, "tag_name", tagName, sizeof( tagName ) ) ) {
			Q_strncpyz( upd.latestVer, tagName, sizeof( upd.latestVer ) );
			Upd_JsonString( buf, "html_url", upd.releaseUrl, sizeof( upd.releaseUrl ) );
			if ( Upd_VersionNewer( PRODUCT_VERSION, tagName ) ) {
				upd.available = 1;
			}
		}
	}

done:
	if ( hReq )  InternetCloseHandle( hReq );
	if ( hConn ) InternetCloseHandle( hConn );
	if ( hInet ) InternetCloseHandle( hInet );
	upd.checked = 2;
	return 0;
}

/*  Debug: simulate an available update  */
static void Upd_SimulateUpdate_f( void )
{
	upd.checked   = 2;
	upd.available = 1;
	Q_strncpyz( upd.latestVer, "1.46", sizeof( upd.latestVer ) );
	Q_strncpyz( upd.releaseUrl, "https://github.com/KoRrNiK/RtCW-Patch_Speedrun/releases/latest",
				sizeof( upd.releaseUrl ) );
	Cvar_Set( "sp_updateAvailable", "1" );
	Cvar_Set( "sp_updateVersion", upd.latestVer );
	Com_Printf( "^2Update: Simulating notification (version %s)\n",
		upd.latestVer );
}

/*  Open the release page when the user clicks the update badge  */
static void Upd_OpenRelease_f( void )
{
	if ( upd.releaseUrl[0] ) {
		Sys_OpenURL( upd.releaseUrl, qfalse );
	} else {
		Sys_OpenURL( "https://github.com/KoRrNiK/RtCW-Patch_Speedrun/releases/latest", qfalse );
	}
}

/* =====================================================================
   Public API
   ===================================================================== */

void SCR_UpdateInit( void )
{
	HANDLE hThread;

	if ( upd.checked != 0 ) return;
	upd.checked   = 1;
	upd.available = 0;
	upd.latestVer[0]  = '\0';
	upd.releaseUrl[0] = '\0';

	/* cvars used by UI menu to show/hide update button */
	Cvar_Set( "sp_updateAvailable", "0" );
	Cvar_Set( "sp_updateVersion", "" );

	hThread = CreateThread( NULL, 0, Upd_CheckThread, NULL, 0, NULL );
	if ( hThread ) CloseHandle( hThread );

	Cmd_AddCommand( "xq9_sim", Upd_SimulateUpdate_f );
	Cmd_AddCommand( "sp_openUpdate", Upd_OpenRelease_f );
}

void SCR_UpdateShutdown( void )
{
	Cmd_RemoveCommand( "xq9_sim" );
	Cmd_RemoveCommand( "sp_openUpdate" );
}

/* flag: have we already pushed cvars to the UI? */
static qboolean s_cvarsSet;

void SCR_UpdateDraw( void )
{
	if ( !cls.rendererStarted ) return;

	/* Once the background thread finishes and finds an update,
	   set cvars so the UI menu button becomes visible. */
	if ( !s_cvarsSet && upd.checked == 2 && upd.available && upd.latestVer[0] ) {
		Cvar_Set( "sp_updateAvailable", "1" );
		Cvar_Set( "sp_updateVersion", upd.latestVer );
		s_cvarsSet = qtrue;
	}

	if ( cls.state <= CA_DISCONNECTED ) {
		/* ---- Main menu: version label + build date, bottom-left ---- */
		{
			char   verStr[64];
			vec4_t col = { 0.52f, 0.56f, 0.48f, 0.50f };
			Com_sprintf( verStr, sizeof( verStr ), "v%s  (%s %s)", PRODUCT_VERSION, PRODUCT_DATE, PRODUCT_TIME );
			SCR_DrawStringExt( 6, 468, 6.0f, verStr, col, qtrue );
		}

		/* ---- Main menu: current date+time, bottom-right ---- */
		{
			time_t     rawTime;
			struct tm *ti;
			char       dtStr[32];
			int        dtLen;
			float      dtX;
			vec4_t     dtCol = { 0.52f, 0.56f, 0.48f, 0.50f };

			time( &rawTime );
			ti = localtime( &rawTime );
			Com_sprintf( dtStr, sizeof( dtStr ), "%02d.%02d.%04d  %02d:%02d",
				ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900,
				ti->tm_hour, ti->tm_min );
			dtLen = (int)strlen( dtStr );
			dtX   = 640.0f - 6.0f - (float)(dtLen * 6);
			SCR_DrawStringExt( (int)dtX, 468, 6.0f, dtStr, dtCol, qtrue );
		}

	} else {
		/* ---- In-game: top-centre notification (only when update available) ---- */
		char   verBuf[64];
		int    verLen, seg, numSegs, fontSize;
		float  centerX, textW, textX, textY;
		float  lineW, lineX, lineY1, lineY2;
		float  segW, t, a, pulse, bgH;
		vec4_t lc, bgCol, txtCol;

		if ( upd.checked != 2 || !upd.available ) return;

		pulse = 0.82f + 0.18f * (float)sin( (double)cls.realtime * 0.0025 );

		Com_sprintf( verBuf, sizeof( verBuf ),
					 "v%s Update Available", upd.latestVer );
		verLen = (int)strlen( verBuf );

		fontSize = 5;
		centerX  = 320.0f;
		textW    = (float)( verLen * fontSize );
		textX    = centerX - textW * 0.5f;
		lineW    = textW + 50.0f;
		lineX    = centerX - lineW * 0.5f;
		lineY1   = 4.0f;
		textY    = lineY1 + 4.0f;
		lineY2   = textY + (float)fontSize + 3.0f;

		numSegs = 48;
		segW    = lineW / (float)numSegs;
		bgH     = lineY2 - lineY1 + 1.0f;

		/* top gradient line */
		for ( seg = 0; seg < numSegs; seg++ ) {
			t = (float)seg / (float)( numSegs - 1 );
			a = ( t <= 0.5f ) ? ( t * 2.0f ) : ( ( 1.0f - t ) * 2.0f );
			a = a * a;
			lc[0] = 0.75f; lc[1] = 0.90f; lc[2] = 1.0f;
			lc[3] = a * 0.70f * pulse;
			SCR_FillRect( lineX + (float)seg * segW, lineY1,
						  segW + 0.5f, 1, lc );
		}

		/* gradient dark backdrop */
		for ( seg = 0; seg < numSegs; seg++ ) {
			t = (float)seg / (float)( numSegs - 1 );
			a = ( t <= 0.5f ) ? ( t * 2.0f ) : ( ( 1.0f - t ) * 2.0f );
			a = a * a;
			bgCol[0] = 0.0f; bgCol[1] = 0.0f; bgCol[2] = 0.04f;
			bgCol[3] = a * 0.32f * pulse;
			SCR_FillRect( lineX + (float)seg * segW, lineY1,
						  segW + 0.5f, bgH, bgCol );
		}

		/* text */
		txtCol[0] = 0.88f; txtCol[1] = 0.95f; txtCol[2] = 1.0f;
		txtCol[3] = 0.95f * pulse;
		SCR_DrawStringExt( (int)textX, (int)textY,
						   (float)fontSize, verBuf, txtCol, qtrue );

		/* bottom gradient line */
		for ( seg = 0; seg < numSegs; seg++ ) {
			t = (float)seg / (float)( numSegs - 1 );
			a = ( t <= 0.5f ) ? ( t * 2.0f ) : ( ( 1.0f - t ) * 2.0f );
			a = a * a;
			lc[0] = 0.65f; lc[1] = 0.82f; lc[2] = 1.0f;
			lc[3] = a * 0.38f * pulse;
			SCR_FillRect( lineX + (float)seg * segW, lineY2,
						  segW + 0.5f, 1, lc );
		}
	}
}



#else /* !_WIN32 - stubs */

void SCR_UpdateInit( void )     {}
void SCR_UpdateShutdown( void ) {}
void SCR_UpdateDraw( void )     {}

#endif
