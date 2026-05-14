#include "race_host_shared.h"

#include "imgui.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define RACE_HOST_SERVICE_MS 4
#define RACE_HOST_FRAME_MS   16

/* ---------- entry point ---------- */
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow ) {
    WNDCLASSA wc;
    WSADATA wsaData;
    MSG msg;
    RECT desired;
    DWORD windowStyle;
    DWORD lastServiceMs = 0;
    DWORD lastFrameMs = 0;
    int port = 27960;

    (void)hPrevInstance;
    gInstance = hInstance;
    memset( &msg, 0, sizeof( msg ) );

    if ( lpCmdLine && lpCmdLine[0] ) {
        char *eq = strstr( lpCmdLine, "--port=" );
        if ( eq ) {
            int v = atoi( eq + 7 );
            if ( v > 0 && v < 65536 ) port = v;
        } else {
            int v = atoi( lpCmdLine );
            if ( v > 0 && v < 65536 ) port = v;
        }
    }

    memset( &raceHost, 0, sizeof( raceHost ) );
    raceHost.socketId = INVALID_SOCKET;
    raceHost.port = port;
    Race_Copy( raceHost.bindAddress, sizeof( raceHost.bindAddress ), "0.0.0.0" );
    raceHost.maxPlayers = RACE_MAX_PLAYERS;
    raceHost.session = (int)( time( NULL ) & 0x7fffffff );
    raceHost.state = RACE_STATE_OFFLINE;
    raceHost.mode = 0;
    raceHost.mission = 1;
    raceHost.difficulty = 3;
    raceHost.antiCheat = 1;
    raceHost.autoReadyCheck = 1;
    raceHost.queueEnabled = 1;
    raceHost.privateLobby = 0;
    raceHost.pauseAlertMs = 30000;
    raceHost.nextRunId = 1;
    raceHost.countdownMs = 5000;
    Race_Copy( raceHost.ilMap, sizeof( raceHost.ilMap ), "escape1" );
    Race_Copy( raceHost.targetMap, sizeof( raceHost.targetMap ), "escape1" );
    Race_Copy( raceHost.hostName, sizeof( raceHost.hostName ), "RaceHost" );
    Race_BuildConfigPath();
    Race_InitUiState();
    Race_LoadServerConfig( 1 );

    if ( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 ) {
        MessageBoxA( NULL, "WSAStartup failed.", "RtCW Race Host", MB_OK | MB_ICONERROR );
        return 1;
    }

    SetProcessDPIAware();

    memset( &wc, 0, sizeof( wc ) );
    wc.lpfnWndProc = Race_WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = "RtCWRaceHostWindow";
    wc.hCursor = LoadCursor( NULL, IDC_ARROW );
    wc.hIcon = LoadIconA( hInstance, MAKEINTRESOURCEA( IDI_RACEHOST ) );
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    if ( !RegisterClassA( &wc ) ) {
        MessageBoxA( NULL, "RegisterClass failed.", "RtCW Race Host", MB_OK | MB_ICONERROR );
        WSACleanup();
        return 1;
    }

    windowStyle = WS_OVERLAPPEDWINDOW;
    desired.left = 0;
    desired.top = 0;
    desired.right = 1440;
    desired.bottom = 920;
    AdjustWindowRect( &desired, windowStyle, FALSE );
    gMainWnd = CreateWindowExA( 0, "RtCWRaceHostWindow", "RtCW Race Host",
        windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, desired.right - desired.left, desired.bottom - desired.top,
        NULL, NULL, hInstance, NULL );
    if ( !gMainWnd ) {
        MessageBoxA( NULL, "CreateWindow failed.", "RtCW Race Host", MB_OK | MB_ICONERROR );
        WSACleanup();
        return 1;
    }
    {
        HICON icon = LoadIconA( hInstance, MAKEINTRESOURCEA( IDI_RACEHOST ) );
        if ( icon ) {
            SendMessageA( gMainWnd, WM_SETICON, ICON_BIG, (LPARAM)icon );
            SendMessageA( gMainWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon );
        }
    }

    if ( !Race_CreateOpenGLContext( gMainWnd ) ) {
        MessageBoxA( NULL, "OpenGL context creation failed.", "RtCW Race Host", MB_OK | MB_ICONERROR );
        DestroyWindow( gMainWnd );
        WSACleanup();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if ( !Race_InitImGui() ) {
        MessageBoxA( NULL, "ImGui initialization failed.", "RtCW Race Host", MB_OK | MB_ICONERROR );
        Race_ShutdownImGui();
        DestroyWindow( gMainWnd );
        WSACleanup();
        return 1;
    }

    Race_BuildGui();
    if ( gConfigLoaded ) Race_GuiLogSrv( "%s", gConfigNotice );
    else Race_GuiLogSrv( "%s", gConfigNotice[0] ? gConfigNotice : "config not found" );
    Race_GuiLogSrv( "host offline - set Bind IP/port, then press Start Host" );
    Race_GuiLogSrv( "type 'help' in the console below for commands" );

    ShowWindow( gMainWnd, nCmdShow );
    UpdateWindow( gMainWnd );

    while ( gRunning ) {
        DWORD now;
        DWORD waitMs;
        while ( PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE ) ) {
            if ( msg.message == WM_QUIT ) {
                gRunning = 0;
                break;
            }
            TranslateMessage( &msg );
            DispatchMessageA( &msg );
        }
        if ( !gRunning ) break;
        now = GetTickCount();
        if ( !lastServiceMs || (DWORD)( now - lastServiceMs ) >= RACE_HOST_SERVICE_MS ) {
            Race_PollNetwork();
            Race_Tick();
            lastServiceMs = now;
        }
        if ( !lastFrameMs || (DWORD)( now - lastFrameMs ) >= RACE_HOST_FRAME_MS ) {
            Race_GuiRefresh();
            Race_RenderFrame();
            lastFrameMs = GetTickCount();
        }

        now = GetTickCount();
        waitMs = RACE_HOST_SERVICE_MS;
        if ( lastServiceMs && (DWORD)( now - lastServiceMs ) < RACE_HOST_SERVICE_MS ) {
            waitMs = RACE_HOST_SERVICE_MS - (DWORD)( now - lastServiceMs );
        }
        if ( lastFrameMs && (DWORD)( now - lastFrameMs ) < RACE_HOST_FRAME_MS ) {
            DWORD frameWait = RACE_HOST_FRAME_MS - (DWORD)( now - lastFrameMs );
            if ( frameWait < waitMs ) waitMs = frameWait;
        }
        if ( waitMs > 8 ) waitMs = 8;
        if ( waitMs > 0 ) MsgWaitForMultipleObjects( 0, NULL, FALSE, waitMs, QS_ALLINPUT );
    }

    Race_ShutdownImGui();
    if ( raceHost.socketId != INVALID_SOCKET ) closesocket( raceHost.socketId );
    WSACleanup();
    return (int)msg.wParam;
}
